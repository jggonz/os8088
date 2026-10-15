#!/usr/bin/env python3
"""hdtake - SPEC.md 52.1.1: on a 286 or better, HDD.DRV's own IDE rung TAKES
a drive the BIOS also knows, once it has proved it is the same disk.

    make && python3 tests/hdtake.py

THE REASON is the PC speaker. An AT BIOS moves an IDE sector by PIO with
interrupts OFF, so every int 13h read loses the IRQ0s due inside it, and a
Video Player stream on the speaker - whose clock IS one IRQ0 a sample
(SPEC.md 98.3.15) - played slow by exactly what was lost: the owner's 286 and
86Box's both played a 15.1 s file in ~17.5 through the BIOS, and in 15.1 with
the BIOS told there was no disk. Rung 1's loop runs with interrupts on.

QEMU by name: it is the one emulator here with a 286-class CPU AND a BIOS
that knows an IDE disk (SeaBIOS, if=ide) - which is exactly the pairing this
is about. MartyPC is an 8088, where rung 1 never runs.

The fixture: a raw 16-head, 63-sector disk with one FAT16 volume, CALC.O88 in
its root, and a system floppy whose SYSTEM.CFG wants HDD.DRV (bit 1).
  1. HDD.DRV's device row 0 is an IDE row (HDK_IDE) on 1F0h unit 0, carrying
     the int 13h drive it was taken from - 80h - in HDD_BIOS: the BIOS's
     report and IDENTIFY's agreed on the geometry, and LBA 0 read through both
     rungs was the same 512 bytes.
  2. ...and there is ONE row, not two: the takeover replaced the BIOS row,
     which is what keeps the volume from mounting twice.
  3. The volume works through it: C:'s window lists CALC.O88, and launching it
     - the whole package read through rung 1 - opens the Calculator.
`--blank` is the refusal: the same disk with LBA 0's 55AA wiped, which is
what two BLANK disks would share - so geometry alone would pair them - and
row 0 must stay the BIOS's, untouched: unit 80h, HDD_BIOS 80h (every row
the ROM reads carries its int 13h drive there since kernel size pass 11) and
HDD_BASE 0.
Break on purpose: hd_twins's call taken out of hd_ready, and row 0 stays a
BIOS row (step 1 fails); its signature test taken out, and --blank fails.
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

HDD_SIZE, HDD_KIND, HDD_UNIT, HDD_BIOS, HDD_BASE = 12, 0, 1, 3, 10
HDK_BIOS, HDK_IDE = 1, 2
DRVR_SIZE = 16


class QH(object):
    """A private QEMU with the floppy and an IDE disk."""

    def __init__(self, tmp, sysimg, appsimg, hdimg, boot="a"):
        self.sock = os.path.join(tmp, "qmp.sock")
        self.pid = os.path.join(tmp, "qemu.pid")
        self.pidn = None
        atexit.register(self.close)
        os88qemu.own(self.pid, self.sock)   # (the launch site's teardown)
        subprocess.run(
            ["qemu-system-i386", "-machine", "pc,vmport=off", "-m", "4",
             "-drive", "file=%s,format=raw,if=floppy" % sysimg,
             "-drive", "file=%s,format=raw,if=floppy,index=1" % appsimg,
             "-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % hdimg,
             "-boot", boot, "-chardev", "msmouse,id=m0",
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

    def read(self, linear, n):
        return bytes(xmcheck.read_bytes(self.sock, linear, n))

    def readseg(self, seg, off, n):
        return self.read((seg << 4) + off, n)

    def mouse(self, *args):
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "mouse.py"), self.sock]
                       + [str(a) for a in args], check=True,
                       capture_output=True, cwd=ROOT)

    def done(self, cap=2.0):
        os88qemu.ui_done(self, xmcheck.sym, cap=cap)

    def click(self, x, y, cap=2.0):
        self.mouse("click", x, y)
        self.done(cap)

    def need(self, cond, what, secs=30):
        if not os88qemu.acted(self, cond, secs=secs, what=what, poll=0.1):
            raise RuntimeError("timed out waiting for %s" % what)

def launch(q, ui, S, bad, tag):
    """C:'s window, CALC.O88 in it, and the Calculator opened off it"""
    try:
        before = set(o.i for o in ui.windows())
        xmcheck.dblclick(q.sock, *geom.drive_pt(q, "C", S))
        box = {}

        def disk():
            for o in ui.windows():
                if o.i not in before and o.visible:
                    box["w"] = o
                    return True
            return False
        if not os88qemu.acted(q, disk, secs=30, what="C:'s window"):
            raise RuntimeError("C:'s window never opened")
        w = box["w"]
        os88qemu.pace(q, 1)
        i, _ = ui.entry("CALC.O88", w)
        before = set(o.i for o in ui.windows())
        xmcheck.dblclick(q.sock, *ui.row_xy(w, i - ui.scroll(w)))
        opened = os88qemu.acted(
            q, lambda: any(o.i not in before and o.visible
                           for o in ui.windows()), secs=30,
            what="the Calculator")
        print("   %s C: lists CALC.O88 and it launches: %s" % (tag, opened))
        if not opened:
            bad.append("%s CALC.O88 off C: did not open" % tag)
    except Exception as e:                      # (os88ui raises naming it)
        bad.append("%s C: did not work: %s" % (tag, e))


def vrow(q, S, v):
    """volume v's dsk_vtab row: (kind, unit, class, DV_BUNIT)"""
    r = q.read(S("dsk_vtab") + v * 16, 16)
    return r[0], r[1], r[3], r[14]


def boot_leg(bad):
    """--boot: the INSTALLED machine. C: is the kernel's own boot volume,
    adopted through int 13h before any driver loads (52.10.3), and must be
    HANDED to HDD.DRV (a take: OSAPI_VOL_ADD with DX != 0) - then GIVEN BACK when the driver goes,
    and taken again when it comes back"""
    import rdpreserve as rp
    import rdxms
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    q = None
    try:
        cfg = os.path.join(tmp, "SYSTEM.CFG")
        open(cfg, "wb").write(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                              + b"DW" + bytes([1, 2])
                              + (1 << 1).to_bytes(2, "little") + b"\0\0")
        hdimg = os.path.join(tmp, "hd.img")
        subprocess.run(
            [sys.executable, "tools/os88hdd.py", "--raw", "--out", hdimg,
             "--heads", "16", "--spt", "63", "--cyls", "130",
             "--kernel", os88build.at("build/kernel.sys"),
             "--vbr", os88build.at("build/boothd.bin"),
             "--mbr", os88build.at("build/mbr.bin"),
             "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
             "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
             "--file", "CALC.O88=" + os88build.at("build/calc.o88"),
             "--file", "SYSTEM.CFG=" + cfg], check=True, capture_output=True)
        fa, fb = os.path.join(tmp, "a.img"), os.path.join(tmp, "b.img")
        for f in (fa, fb):
            shutil.copy(os88build.at("build/apps.img"), f)
        q = QH(tmp, fa, fb, hdimg, boot="c")
        os88qemu.acted(q, lambda: os.path.exists(q.sock), secs=30,
                       what="the QMP socket")
        xmcheck.wait_desktop(q.sock, "hdtake")
        S = xmcheck.sym
        boot = q.read(S("dsk_bootvol"), 1)[0]
        drow = S("drv_tab") + 1 * DRVR_SIZE + geom.DRVR_SEG
        seg = lambda: struct.unpack("<H", q.read(drow, 2))[0]
        k = vrow(q, S, boot)
        print("   B1. booted from volume %d; its row: kind %d unit %02Xh "
              "class %d bunit %02Xh; HDD.DRV at %04Xh" % ((boot,) + k
                                                         + (seg(),)))
        if boot != 2 or k[0] != 1 or k[3] != 0x80:
            bad.append("B1: the boot volume C: was not handed to the driver "
                       "(kind %d, bunit %02Xh)" % (k[0], k[3]))
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
        launch(q, ui, S, bad, "B2.")
        rdxms.menu_pick(q, ui, "Apple", "Control")
        box = {}

        def got():
            try:
                box["w"] = ui.window("Control Panel")
                return box["w"].visible
            except os88ui.UIError:
                return False
        q.need(got, "the Control Panel", 20)
        q.done(4.0)
        cp = box["w"]
        x0, y0 = cp.x + 1, cp.y + rp.TITLE_H
        hx = x0 + rp.CP_RX + 40
        hy = y0 + rp.CP_DBY1 + 1 * rp.CP_DROWH + rp.CP_DROWH // 2
        q.click(x0 + 40, y0 + rp.CP_I0Y + rp.CP_IDRV * rp.CP_IROWH + 7)
        q.click(hx, hy)                         # the Hard disk row: OFF
        q.need(lambda: seg() == 0, "HDD.DRV to unload", 30)
        q.done(4.0)
        k = vrow(q, S, boot)
        print("   B3. HDD.DRV unloaded; C:'s row: kind %d unit %02Xh class %d "
              "bunit %02Xh" % k)
        if k != (0, 0x80, 0, 0):
            bad.append("B3: C: was not GIVEN BACK to the BIOS when the driver "
                       "went (%r)" % (k,))
        q.click(hx, hy)                         # ...and ON again
        q.need(lambda: seg() != 0, "HDD.DRV to load again", 30)
        q.done(4.0)
        k = vrow(q, S, boot)
        print("   B4. HDD.DRV again; C:'s row: kind %d unit %02Xh class %d "
              "bunit %02Xh" % k)
        if k[0] != 1 or k[3] != 0x80:
            bad.append("B4: C: was not taken again when the driver came back "
                       "(%r)" % (k,))
    except Exception as e:
        bad.append("--boot: %s" % e)
    finally:
        if q is not None:
            q.close()
        shutil.rmtree(tmp, ignore_errors=True)
    return report(bad)


def main():
    blank = "--blank" in sys.argv[1:]
    bad = []
    if "--boot" in sys.argv[1:]:
        return boot_leg(bad)
    # -DHDTOOL_KB and every other define off the Makefile's own recipe, and
    # the result checked against build/hdd.bin (tools/os88drvsym.py)
    try:
        syms = os88drvsym.symbols("hdd.bin")
    except os88drvsym.Mismatch as e:
        sys.exit("hdtake: %s" % e)
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    q = None
    try:
        hdimg = os.path.join(tmp, "hd.img")
        subprocess.run([sys.executable, "tools/os88hdd.py", "--raw",
                        "--noboot", "--out", hdimg, "--heads", "16",
                        "--spt", "63", "--cyls", "130", "--file",
                        "CALC.O88=" + os88build.at("build/calc.o88")],
                       check=True, capture_output=True)
        if blank:                       # (LBA 0's signature, gone)
            with open(hdimg, "r+b") as f:
                f.seek(510)
                f.write(b"\0\0")
        sysimg = os.path.join(tmp, "sys.img")
        shutil.copy(os88build.at("build/os8088.img"), sysimg)
        cfg = os.path.join(tmp, "SYSTEM.CFG")
        open(cfg, "wb").write(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                              + b"DW" + bytes([1, 2])
                              + (1 << 1).to_bytes(2, "little") + b"\0\0")
        subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg,
                        cfg, "SYSTEM.CFG"], check=True, capture_output=True)
        appsimg = os.path.join(tmp, "apps.img")   # (B:, which the desktop
        shutil.copy(os88build.at("build/apps.img"), appsimg)  # wait reads)
        q = QH(tmp, sysimg, appsimg, hdimg)
        os88qemu.acted(q, lambda: os.path.exists(q.sock), secs=30,
                       what="the QMP socket")
        xmcheck.wait_desktop(q.sock, "hdtake")
        S = xmcheck.sym
        row = S("drv_tab") + 1 * DRVR_SIZE + geom.DRVR_SEG
        seg = struct.unpack("<H", q.read(row, 2))[0]
        if not seg:
            bad.append("HDD.DRV is not loaded - SYSTEM.CFG's bit 1 was not "
                       "read, and this tests nothing")
            return report(bad)
        base = seg << 4
        nd = q.read(base + syms["hd_ndev"], 1)[0]
        rows = [q.read(base + syms["hd_devs"] + i * HDD_SIZE, HDD_SIZE)
                for i in range(nd)]
        for i, r in enumerate(rows):
            print("   1. device %d: kind %d unit %02Xh bios %02Xh base %04Xh "
                  "geometry %d/%d/%d" % (
                      i, r[HDD_KIND], r[HDD_UNIT], r[HDD_BIOS],
                      struct.unpack_from("<H", r, HDD_BASE)[0],
                      struct.unpack_from("<H", r, 4)[0],
                      struct.unpack_from("<H", r, 6)[0],
                      struct.unpack_from("<H", r, 8)[0]))
        if blank:
            r = rows[0] if rows else None
            if r is None or r[HDD_KIND] != HDK_BIOS or \
                    r[HDD_UNIT] != 0x80 or r[HDD_BIOS] != 0x80 or \
                    struct.unpack_from("<H", r, HDD_BASE)[0]:
                bad.append("a disk with no 55AA at LBA 0 was taken, or its "
                           "BIOS row left changed")
            return report(bad)
        if not rows or rows[0][HDD_KIND] != HDK_IDE or \
                rows[0][HDD_BIOS] != 0x80 or rows[0][HDD_UNIT] != 0 or \
                struct.unpack_from("<H", rows[0], HDD_BASE)[0] != 0x1F0:
            bad.append("the BIOS's drive 80h was not taken by rung 1 (row 0 "
                       "is not IDE 1F0h unit 0 with HDD_BIOS 80h)")
        if nd != 1:
            bad.append("%d device rows for one disk, not 1" % nd)
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
        launch(q, ui, S, bad, "3.")
    finally:
        if q is not None:
            q.close()
        shutil.rmtree(tmp, ignore_errors=True)
    return report(bad)


def report(bad):
    for b in bad:
        print("   FAIL: %s" % b)
    print("hdtake: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
