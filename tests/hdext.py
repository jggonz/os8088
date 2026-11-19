#!/usr/bin/env python3
"""hdext - logical drives, and the two large-disk transports (SPEC.md 52.4.1,
52.1.5, 52.1.6), on QEMU's 386 with SeaBIOS.

    make && python3 tests/hdext.py [--loop | --lba | --ebios | --xt]

QEMU by name, for hdtake's reason: it is the one emulator here with a 286-class
CPU and a BIOS that knows an IDE disk - and `ide-hd`'s cyls/heads/secs and
lcyls/lheads/lsecs set the drive's own geometry and the one SeaBIOS reports,
independently, which is exactly the pair every leg below is about.

Every fixture is built HERE, sector by sector: an MBR, the FAT16 volumes
(each one tools/os88hdd.py's, sliced out of a disk of its own), and for the
logical drives the chain of extended boot records DOS's FDISK writes - entry 0
the drive, relative to ITS record; entry 1 the next link, relative to the
EXTENDED partition. Each volume carries a marker file named for where it is.

(default) issue #386's disk: C: a primary, then an extended partition (0Fh)
          holding two logical drives. HDD.DRV must mount all three - hd_vols
          rows on slots 0, 4 and 5, the two logical ones PARENTED on slot 1 -
          and CALC.O88 must launch off the SECOND logical drive.
--loop    the same disk with the second record's link pointing BACK at the
          first: the walk must stop (a forward-only chain), and the two
          logical drives must be mounted ONCE each - three volumes, not more.
--lba     rung 1 past 504MB (52.1.5): a 1GB drive whose BIOS reports
          1,024/16/63, so rung 1 TAKES it with the BIOS's geometry (52.1.1)
          and the one partition, past cylinder 1,023, is reachable only in
          LBA mode. Row 0 must be IDE with HDF_LBA, and CALC.O88 launch.
--ebios   rung 0 past 8GB (52.1.6): a 9GB drive SeaBIOS TRANSLATES to
          1,024/255/63, so rung 1 cannot pair it and its IDE row must stay
          unusable (no HDF_XCYL: the double-mount guard); the partition past
          BIOS cylinder 1,023 is reachable only through AH=42h. Row 0 must be
          the BIOS's with HDF_EBIOS, ONE volume mounted, and CALC.O88 launch.
--xt      issue #386's own machine: the default leg's disk on MartyPC's 8088
          with an XT-IDE, so the chain is read through the card's ROM on rung
          0. Row 0 the BIOS's, the same three volumes, CALC.O88 off E:.

Break on purpose: hd_mount's call to hd_mount_chain taken out, and the default
leg finds one volume; the forward test in the link taken out, and --loop mounts
the logical drives again; hd_xfer_ide's LBA arm refused, and --lba lists
nothing; hd_at_xcyl's BIOS scan skipped, and --ebios mounts the volume twice.
"""
import atexit
import os
import shutil
import signal
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88build                                              # noqa: E402
import os88geom as geom                                       # noqa: E402
import os88qemu                                               # noqa: E402
import os88ui                                                 # noqa: E402
import xmcheck                                                # noqa: E402
import os88drvsym                                             # noqa: E402

SECTOR = 512
HDD_SIZE, HDD_KIND, HDD_FLAGS, HDD_BIOS = 12, 0, 2, 3
HDK_BIOS, HDK_IDE = 1, 2
HDF_LBA, HDF_EBIOS, HDF_XCYL = 0x04, 0x08, 0x10
HDV_SIZE, HDV_USED, HDV_PART, HDV_BASE, HDV_PARENT = 16, 0, 3, 4, 11
DRVR_SIZE = 16
VOL_CYLS = 20                       # each volume: 20 x 16 x 63 = ~10MB, FAT16


def volume(tmp, name, files):
    """One FAT16 volume's sectors: os88hdd.py's, out of a disk of its own"""
    img = os.path.join(tmp, name + ".img")
    args = [sys.executable, "tools/os88hdd.py", "--raw", "--noboot", "--out",
            img, "--heads", "16", "--spt", "63", "--cyls", str(VOL_CYLS)]
    for f in files:
        args += ["--file", f]
    subprocess.run(args, check=True, capture_output=True)
    d = open(img, "rb").read()
    lba, secs = struct.unpack_from("<II", d, 446 + 8)
    return d[lba * SECTOR:(lba + secs) * SECTOR]


def entry(ptype, lba, secs):
    """A partition entry. The CHS halves say 1023/254/63 - 'read the LBA' -
    which is what every tool writes past cylinder 1,023 and what this driver
    never reads: it addresses by the LBA fields (SPEC.md 18.7)"""
    return bytes([0, 254, 0xFF, 0xFF, ptype, 254, 0xFF, 0xFF]) + \
        struct.pack("<II", lba, secs)


def table(entries):
    s = bytearray(SECTOR)
    for i, e in enumerate(entries):
        s[446 + 16 * i:446 + 16 * (i + 1)] = e
    s[510:512] = b"\x55\xAA"
    return bytes(s)


def put(f, lba, data):
    f.seek(lba * SECTOR)
    f.write(data)


def ext_disk(tmp, loop, path=None, total=None):
    """issue #386's shape: C: primary, D: and E: logical in a 0Fh extended.
    PATH and TOTAL put it in a disk of a set size (the XT leg's VHD)"""
    calc = "CALC.O88=" + os88build.at("build/calc.o88")
    mk = lambda nm: "%s=%s" % (nm, marker(tmp, nm))
    vc = volume(tmp, "c", [mk("PRIM.TXT")])
    vd = volume(tmp, "d", [mk("LOG1.TXT")])
    ve = volume(tmp, "e", [mk("LOG2.TXT"), calc])
    n = len(vc) // SECTOR
    cyl = 16 * 63
    c0 = 63                                         # C: at the era's floor
    x0 = ((c0 + n + cyl - 1) // cyl) * cyl          # the extended's start
    d_rel = 63                                      # D: one track into EBR1
    e_ebr = ((d_rel + n + cyl - 1) // cyl) * cyl    # EBR2, rel. to x0
    xlen = e_ebr + 63 + n
    hd = path or os.path.join(tmp, "hd.img")
    with open(hd, "wb") as f:
        f.truncate((total or (x0 + xlen + cyl)) * SECTOR)
        put(f, 0, table([entry(0x06, c0, n), entry(0x0F, x0, xlen)]))
        put(f, c0, vc)
        put(f, x0, table([entry(0x06, d_rel, n),
                          entry(0x05, e_ebr, n + 63)]))
        put(f, x0 + d_rel, vd)
        link = entry(0x05, 0, n + 63) if loop else bytes(16)
        put(f, x0 + e_ebr, table([entry(0x06, 63, n), link]))
        put(f, x0 + e_ebr + 63, ve)
    total = (x0 + xlen + cyl)
    return hd, [c0, x0 + d_rel, x0 + e_ebr + 63], total


def far_disk(tmp, at, total):
    """one FAT16 partition at LBA `at` of a `total`-sector sparse disk"""
    calc = "CALC.O88=" + os88build.at("build/calc.o88")
    v = volume(tmp, "far", ["FAR.TXT=" + marker(tmp, "FAR.TXT"), calc])
    n = len(v) // SECTOR
    hd = os.path.join(tmp, "hd.img")
    with open(hd, "wb") as f:
        f.truncate(total * SECTOR)
        put(f, 0, table([entry(0x06, at, n)]))
        put(f, at, v)
    return hd


def marker(tmp, name):
    p = os.path.join(tmp, name)
    open(p, "wb").write(("this is %s\r\n" % name).encode())
    return p


class QH(object):
    """A private QEMU: the floppies, and the IDE disk on ide-hd with the
    drive's geometry and SeaBIOS's set apart"""

    def __init__(self, tmp, sysimg, appsimg, hdimg, phys, logical):
        self.sock = os.path.join(tmp, "qmp.sock")
        self.pid = os.path.join(tmp, "qemu.pid")
        self.pidn = None
        atexit.register(self.close)
        os88qemu.own(self.pid, self.sock)
        dev = "ide-hd,drive=hd0,bus=ide.0,unit=0,cyls=%d,heads=%d,secs=%d" \
            % phys
        if logical:
            dev += ",lcyls=%d,lheads=%d,lsecs=%d" % logical
        os88qemu.start(
            ["qemu-system-i386", "-machine", "pc,vmport=off", "-m", "4",
             "-drive", "file=%s,format=raw,if=floppy" % sysimg,
             "-drive", "file=%s,format=raw,if=floppy,index=1" % appsimg,
             "-drive", "file=%s,format=raw,if=none,id=hd0" % hdimg,
             "-device", dev,
             "-boot", "a", "-chardev", "msmouse,id=m0",
             "-serial", "chardev:m0", "-display", "none",
             "-qmp", "unix:%s,server,nowait" % self.sock,
             "-daemonize", "-pidfile", self.pid], check=True)
        self.pidn = int(open(self.pid).read().strip())

    def close(self):
        if self.pidn:
            try:
                os.kill(self.pidn, signal.SIGTERM)
                os88qemu.gone(self.pidn)
            except OSError:
                pass
            self.pidn = None

    def hmp(self, *cmds):
        return xmcheck.qmp(self.sock, *cmds)

    def hmp_burst(self, lines):
        return xmcheck.qmp_burst(self.sock, lines)

    def read(self, linear, n):
        return bytes(xmcheck.read_bytes(self.sock, linear, n))

    def readseg(self, seg, off, n):
        return self.read((seg << 4) + off, n)

    def done(self, cap=2.0):
        os88qemu.ui_done(self, xmcheck.sym, cap=cap)


def launch(q, ui, S, bad, letter, tag):
    """LETTER:'s window, CALC.O88 in it, and the Calculator opened off it"""
    try:
        before = set(o.i for o in ui.windows())
        os88qemu.mouse_dblclick(q, S, q.hmp, *geom.drive_pt(q, letter, S))
        box = {}

        def disk():
            for o in ui.windows():
                if o.i not in before and o.visible:
                    box["w"] = o
                    return True
            return False
        if not os88qemu.acted(q, disk, secs=30, what="%s:'s window" % letter):
            raise RuntimeError("%s:'s window never opened" % letter)
        w = box["w"]
        q.done(1.0)
        i, _ = ui.entry("CALC.O88", w)
        before = set(o.i for o in ui.windows())
        os88qemu.mouse_dblclick(q, S, q.hmp, *ui.row_xy(w, i - ui.scroll(w)))
        opened = os88qemu.acted(
            q, lambda: any(o.i not in before and o.visible
                           for o in ui.windows()), secs=30,
            what="the Calculator")
        print("   %s %s: lists CALC.O88 and it launches: %s"
              % (tag, letter, opened))
        if not opened:
            bad.append("%s CALC.O88 off %s: did not open" % (tag, letter))
    except Exception as e:                      # (os88ui raises naming it)
        bad.append("%s %s: did not work: %s" % (tag, letter, e))


def boot(tmp, hdimg, phys, logical):
    sysimg = os.path.join(tmp, "sys.img")
    shutil.copy(os88build.at("build/os8088.img"), sysimg)
    cfg = os.path.join(tmp, "SYSTEM.CFG")
    open(cfg, "wb").write(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                          + b"DW" + bytes([1, 2])
                          + (1 << 1).to_bytes(2, "little") + b"\0\0")
    subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg,
                    cfg, "SYSTEM.CFG"], check=True, capture_output=True)
    appsimg = os.path.join(tmp, "apps.img")
    shutil.copy(os88build.at("build/apps.img"), appsimg)
    q = QH(tmp, sysimg, appsimg, hdimg, phys, logical)
    os88qemu.acted(q, lambda: os.path.exists(q.sock), secs=30,
                   what="the QMP socket")
    xmcheck.wait_desktop(q.sock, "hdext")
    return q


def drv_at(q, S):
    """HDD.DRV's image base, linear - 0 when it is not loaded"""
    row = S("drv_tab") + 1 * DRVR_SIZE + geom.DRVR_SEG
    seg = struct.unpack("<H", q.read(row, 2))[0]
    if not seg:
        return 0
    return geom.drv_base(seg)


def state(read, base, syms):
    """(device rows, live hd_vols rows) out of HDD.DRV's own image"""
    nd = read(base + syms["hd_ndev"], 1)[0]
    devs = [read(base + syms["hd_devs"] + i * HDD_SIZE, HDD_SIZE)
            for i in range(nd)]
    vols = []
    for i in range(6):
        r = read(base + syms["hd_vols"] + i * HDV_SIZE, HDV_SIZE)
        if r[HDV_USED]:
            vols.append((r[HDV_PART], r[HDV_PARENT],
                         struct.unpack_from("<I", r, HDV_BASE)[0], r[1]))
    for i, r in enumerate(devs):
        print("   device %d: kind %d flags %02Xh bios %02Xh geometry "
              "%d/%d/%d" % (i, r[HDD_KIND], r[HDD_FLAGS], r[HDD_BIOS],
                            struct.unpack_from("<H", r, 4)[0],
                            struct.unpack_from("<H", r, 6)[0],
                            struct.unpack_from("<H", r, 8)[0]))
    for v in vols:
        print("   volume: slot %d parent %d base %d device %d" % v)
    return devs, vols


def chain_ok(bad, leg, read, base, syms, vols, bases):
    """the walk stopped at the last record, and three volumes once each"""
    last = read(base + syms["hd_pslot"], 1)[0]
    print("   the walk stopped at slot %d" % last)
    if last != 5:                           # EBR2 is the last record read
        bad.append("the chain walk went on to slot %d past the second "
                   "record (want 5): %s" % (
                       last, "a BACKWARD link was followed" if leg
                       else "a link that is not there"))
    want = [(0, 0, bases[0]), (4, 1, bases[1]), (5, 1, bases[2])]
    got = sorted((v[0], v[1], v[2]) for v in vols)
    if got != want:
        bad.append("%s: volumes %r, want slots/parents/bases %r"
                   % (leg or "default", got, want))


def vhd_footer(c, h, s):
    """bigvol's: a fixed VHD's footer for MartyPC's XT-IDE"""
    size = c * h * s * SECTOR
    f = bytearray(512)
    f[0:8] = b"conectix"
    struct.pack_into(">IIQI", f, 8, 2, 0x00010000, 0xFFFFFFFFFFFFFFFF, 0)
    f[28:32] = b"mrty"
    struct.pack_into(">I", f, 32, 0x00010000)
    f[36:40] = b"Wi2k"
    struct.pack_into(">QQHBBI", f, 40, size, size, c, h, s, 2)
    f[68:84] = bytes(range(16))
    struct.pack_into(">I", f, 64, (~sum(f)) & 0xFFFFFFFF)
    return size, bytes(f)


def xt_leg():
    """--xt: issue #386's machine - an 8088 with an XT-IDE (MartyPC's
    os8088_xt_hdd), where rung 1 never runs and the chain is read through
    the card's ROM on rung 0. The disk is the default leg's, in a 654/16/63
    VHD (bigvol's geometry, one MartyPC's controller accepts). The BIOS
    knows the drive, so HDD.DRV comes up by the machine's choice (SPEC.md
    51.3.2) with no SYSTEM.CFG of ours"""
    import os88marty as M
    import instdeep as ID
    if not os.path.exists("build/martypc/run/martypc_headless"):
        print("hdext: SKIP: no MartyPC - `make marty` first")
        return 0
    bad = []
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    try:
        syms = os88drvsym.symbols("hdd.bin")
        run_dir = M.stage_run_dir("hdext")
        vhd = os.path.join(run_dir, ID.VHD_REL)
        size, foot = vhd_footer(654, 16, 63)
        _, bases, _ = ext_disk(tmp, False, vhd, size // SECTOR)
        with open(vhd, "r+b") as f:
            f.seek(size)
            f.write(foot)
        with M.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=ID.MACHINE, run_dir=run_dir) as m:
            M.settle(m)
            S = m.sym
            ui = os88ui.UI(m)
            ui.ready()
            row = S("drv_tab") + 1 * DRVR_SIZE + geom.DRVR_SEG
            seg = struct.unpack("<H", m.read(row, 2))[0]
            if not seg:
                bad.append("HDD.DRV is not loaded - this tests nothing")
                return report(bad)
            base = geom.drv_base(seg)
            devs, vols = state(m.read, base, syms)
            if not devs or devs[0][HDD_KIND] != HDK_BIOS:
                bad.append("row 0 is not the XT-IDE ROM's (rung 0)")
            chain_ok(bad, "", m.read, base, syms, vols, bases)
            try:
                ui.path("E:/CALC.O88")          # raises naming what it saw
                print("   2. E: lists CALC.O88 and it launches: True")
            except Exception as e:              # noqa: BLE001
                bad.append("2. E: did not work: %s" % e)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return report(bad)


def marty_leg(leg):
    """THE TWO CHAIN LEGS THROUGH os88marty's API, for kern_mega (Bochs's
    386, tests/megaport.txt) - the same disk handed over as its C: with
    `--mount hd:0:`, the driver's segment a SELECTOR read through m.lin. The
    transports' legs set QEMU's ide-hd geometries and have no twin here."""
    import os88marty as M
    if leg not in ("", "--loop"):
        print("hdext: SKIP: %s is QEMU's (ide-hd's lcyls/lheads/lsecs)" % leg)
        return 0
    mega = bool(os.environ.get("OS88_MEGA"))
    bad = []
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    try:
        syms = os88drvsym.symbols("hdd.bin")
        hd, bases, _ = ext_disk(tmp, leg == "--loop")
        cfg = os.path.join(tmp, "SYSTEM.CFG")
        open(cfg, "wb").write(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                              + b"DW" + bytes([1, 2])
                              + (1 << 1).to_bytes(2, "little") + b"\0\0")
        sysimg = os.path.join(tmp, "sys.img")
        shutil.copy(M.system_disk(os88build.at("build/os8088.img")), sysimg)
        subprocess.run([sys.executable, "tools/os88fat.py", "del", sysimg,
                        "SYSTEM.CFG"], capture_output=True)
        subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg,
                        cfg, "SYSTEM.CFG"], check=True, capture_output=True)
        with M.launch(sysimg, apps=os88build.at("build/apps.img"),
                      machine="os8088_xt_hdd" if mega else "at286_vga_hdd",
                      boot=False, extra=["--mount", "hd:0:" + hd]) as m:
            m.run()
            M.kernel_up(m)
            S = m.sym
            ui = os88ui.UI(m)
            ui.ready()
            row = S("drv_tab") + 1 * DRVR_SIZE + geom.DRVR_SEG
            seg = struct.unpack("<H", m.read(row, 2))[0]
            if not seg:
                bad.append("HDD.DRV is not loaded - this tests nothing")
                return report(bad)
            base = m.lin(seg, 0) if mega else geom.drv_base(seg)
            devs, vols = state(m.read, base, syms)
            chain_ok(bad, leg, m.read, base, syms, vols, bases)
            try:
                ui.path("E:/CALC.O88")          # raises naming what it saw
                print("   2. E: lists CALC.O88 and it launches: True")
            except Exception as e:              # noqa: BLE001
                bad.append("2. E: did not work: %s" % e)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return report(bad)


def main():
    a = sys.argv[1:]
    leg = "--loop" if "--loop" in a else "--lba" if "--lba" in a else \
        "--ebios" if "--ebios" in a else ""
    if "--xt" in a:
        return xt_leg()
    import os88marty
    if os.environ.get("OS88_MEGA") or os88marty.emulator() != "marty":
        return marty_leg(leg)
    try:
        syms = os88drvsym.symbols("hdd.bin")
    except os88drvsym.Mismatch as e:
        sys.exit("hdext: %s" % e)
    bad = []
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    q = None
    try:
        if leg in ("", "--loop"):
            hd, bases, total = ext_disk(tmp, leg == "--loop")
            cyls = (total + 16 * 63 - 1) // (16 * 63)
            q = boot(tmp, hd, (cyls, 16, 63), None)
            S = xmcheck.sym
            base = drv_at(q, S)
            if not base:
                bad.append("HDD.DRV is not loaded - this tests nothing")
                return report(bad)
            devs, vols = state(q.read, base, syms)
            chain_ok(bad, leg, q.read, base, syms, vols, bases)
            # the kernel letters a volume by the row it gets, and they are
            # registered in walk order: C: the primary, D: and E: the chain
            # (SPEC.md 18.7.1) - so the SECOND logical drive is E:
            letter = None
            for v in vols:
                if v[0] == 5:
                    letter = "E"
            ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
            if letter:
                launch(q, ui, S, bad, letter, "2.")
        elif leg == "--lba":
            far = 1100000                   # past 1,024 x 16 x 63
            hd = far_disk(tmp, far, 2080 * 16 * 63)
            q = boot(tmp, hd, (2080, 16, 63), (1024, 16, 63))
            S = xmcheck.sym
            base = drv_at(q, S)
            if not base:
                bad.append("HDD.DRV is not loaded - this tests nothing")
                return report(bad)
            devs, vols = state(q.read, base, syms)
            r = devs[0] if devs else None
            if r is None or r[HDD_KIND] != HDK_IDE or r[HDD_BIOS] != 0x80 \
                    or not r[HDD_FLAGS] & HDF_LBA:
                bad.append("row 0 is not rung 1's take with HDF_LBA")
            if len(devs) != 1:
                bad.append("%d device rows for one disk, not 1" % len(devs))
            if [(v[0], v[2]) for v in vols] != [(0, far)]:
                bad.append("volumes %r, want slot 0 at %d" % (vols, far))
            ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
            launch(q, ui, S, bad, "C", "2.")
        else:
            far = 16600000                  # past 1,024 x 255 x 63
            total = 9 * 1024 * 2048
            hd = far_disk(tmp, far, total)
            q = boot(tmp, hd, (16383, 16, 63), (1024, 255, 63))
            S = xmcheck.sym
            base = drv_at(q, S)
            if not base:
                bad.append("HDD.DRV is not loaded - this tests nothing")
                return report(bad)
            devs, vols = state(q.read, base, syms)
            r = devs[0] if devs else None
            if r is None or r[HDD_KIND] != HDK_BIOS or \
                    not r[HDD_FLAGS] & HDF_EBIOS:
                bad.append("row 0 is not the BIOS's with HDF_EBIOS")
            for i, d in enumerate(devs[1:], 1):
                if d[HDD_KIND] == HDK_IDE and d[HDD_FLAGS] & HDF_XCYL:
                    bad.append("IDE row %d may pass cylinder 1,023 beside a "
                               "BIOS row that is the same disk translated "
                               "- the double mount" % i)
            if [(v[0], v[2], v[3]) for v in vols] != [(0, far, 0)]:
                bad.append("volumes %r, want ONE: slot 0 at %d on device 0"
                           % (vols, far))
            ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
            launch(q, ui, S, bad, "C", "2.")
    finally:
        if q is not None:
            q.close()
        shutil.rmtree(tmp, ignore_errors=True)
    return report(bad)


def report(bad):
    for b in bad:
        print("   FAIL: %s" % b)
    print("hdext: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
