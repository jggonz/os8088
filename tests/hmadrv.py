#!/usr/bin/env python3
"""hmadrv - drivers and a driver's buffer in the HMA (SPEC.md 2.10.8,
docs/plans/HMA-PLAN.md 5).

    make && python3 tests/hmadrv.py

On a 286 or better with `.cold` in the HMA (tests/hmacold.py), what `.cold`
does not fill is a pool, and a driver goes there instead of the heap: the
build assembled it a second time at DRV_HMA_ORG, COLDHMA.SYS carries the
difference, and drv_load's last step copies, reads back and relocates the
image it has just loaded. A driver's own buffer goes there through
OSAPI_MEM_HMA - SOUND.DRV's double buffer first.

WHY QEMU: docs/TESTING.md's closed list, entry 1 - above 1MB is a 286-and-up
place and every MartyPC machine is an 8088. Nothing here is timed.

ONE MACHINE, A/B. The system disk is the shipped 1.44MB one plus a SYSTEM.CFG
that wants SOUND.DRV, HDD.DRV and NET.DRV - the three the build relocates -
with an SB16 and an IDE disk holding a FAT16 partition and HELLO.TXT:

  A. -m 128.
     1. all three rows name a segment in the HMA pool (derived from the origin), hold no heap
        (DRVR_KB 0), and dispatch at DRV_HMA_ORG + PKG_DISP;
     2. the tail's block table holds exactly those three images, each owned
        by its own segment, and each image IS its org-DRV_HMA_ORG assembly
        (build/<name>.hma.bin), header fields os88drv.py rewrites aside;
     3. HDD.DRV mounted the disk FROM THE HMA - its partition table read
        through hd_raw's bounce, every sector after it through the kernel's
        block dispatch at DRV_HMA_ORG + PKG_DISP - and a Disk window on C: lists HELLO.TXT;
     4. the Control Panel's Hard Disk page: HDDTOOL.DRV loads and its
        first paint calls BACK into HDD.DRV through DRV_HMA_ORG + PKG_DISP
        (HDT_INIT's CX) - its device table comes back equal to the
        resident's. A tool that assumed PKG_DISP froze a 286 here;
     5. SOUND.DRV plays from the HMA: RECORDER.O88's demo, played on the
        card, takes the double buffer (SPEC.md 34.5.2) - in the pool when its
        current free runs hold 8KB, otherwise in the heap. Both paths keep
        the 8237 in the right physical page and the stream runs out,
        which is the card's DMA and IRQs from above 1MB. The block is given
        back with the stream.
  B. -m 1, the NEGATIVE CONTROL (docs/WRITING-TESTS.md 1): the same disk and
     the same drivers, every row a heap segment holding its KB, and C: still
     mounted - so A's numbers say where the drivers ran and nothing else.

BROKEN ON PURPOSE, measured while it was written: with mem_own blind to an
HMA driver, HDD.DRV's own claims were refused and leg A3 found no volume;
with hd_raw's bounce clobbering DI, the same; and with sbl_fill_guard's
`add di, [sbl_sbase]` taken out, A5's halves read as nothing in the sweep -
WHILE THE CARD STILL "PLAYED" the stream out (state 1, an underrun), which is
why the bytes are read and the card's progress is not trusted alone. With
HDDTOOL.DRV's far pointer back at PKG_DISP (HD_ABI_VER 5's), A4 read
F3C6:000C and the sync never came back - the field's freeze; with mem_own
asking for a MEM_K_DRV claim at the tool's owner, A6 stopped at "Not enough
memory to copy" with nothing on C: but what the image already held.

  A. (continued)
     6. Install, from that page: the whole system disk copied onto C: with
        HDD.DRV in the pool - every sector a service call from the tool into
        the resident, every buffer a claim the tool makes through mem_own.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88drv     # noqa: E402
import os88geom    # noqa: E402
import os88qemu    # noqa: E402
import os88sym     # noqa: E402
import hmacold     # noqa: E402
import recorder    # noqa: E402
import vidxms      # noqa: E402
import xmcheck     # noqa: E402
from cycweb import pkg_syms  # noqa: E402

S = os88sym.linear
EQ = os88sym.equates(())
B = os.path.join(ROOT, "build")
DRVS = (("SOUND", 0, "sound"), ("HDD", 1, "hdd"), ("NET", 4, "net"))
WANT = (1 << 0) | (1 << 1) | (1 << 3)       # drv_cfgbit: sound, hdd, net
ORG = EQ["DRV_HMA_ORG"]
DRVR_SIZE = EQ["DRVR_SIZE"]
DRVR_ENT = EQ["DRVR_ENT"]


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def disks(tmp):
    sysimg = os.path.join(tmp, "sys.img")
    shutil.copy(os.path.join(B, "os8088.img"), sysimg)
    cfg = os.path.join(tmp, "SYSTEM.CFG")
    with open(cfg, "wb") as f:
        f.write(b"O88CFG\0\0" + struct.pack("<H", 3) + b"DW" + bytes([1, 2])
                + struct.pack("<H", WANT) + b"\0\0")
    subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg, cfg],
                   check=True, capture_output=True)
    apps = os.path.join(tmp, "rec.img")    # RECORDER.O88 alone, so it is
    rec = os.path.join(tmp, "RECORDER.O88")  # in view without a scroll
    shutil.copy(os.path.join(B, "recorder.o88"), rec)
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", apps,
                    "--size", "1440", rec], check=True, capture_output=True)
    hd = os.path.join(tmp, "hd.img")
    note = os.path.join(tmp, "HELLO.TXT")
    with open(note, "w") as f:
        f.write("hello from C:\r\n")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", hd, "--hdd",
                    "--mbr", os.path.join(B, "mbr.bin"),
                    "--boot", os.path.join(B, "boothd.bin"),
                    "--kernel", os.path.join(B, "kernel.sys"), note],
                   check=True, capture_output=True)
    return sysimg, apps, hd


def boot(tmp, mem, sysimg, apps, hd):
    extra = ["-audiodev", "none,id=snd", "-device", "sb16,audiodev=snd",
             "-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % hd]
    q = vidxms.Q(tmp, sysimg, apps, mem, extra)
    for _ in range(150):
        if os.path.exists(q.sock):
            break
        time.sleep(0.2)
    xmcheck.wait_desktop(q.sock, "hmadrv")
    floors = (EQ["HEAP_SEG"], EQ["COLD_RAM"] + EQ["VGABUF_PARA"])
    if not os88qemu.acted(q, lambda: u16(q.read(S("mem_base"), 2)) in floors,
                          secs=120, what="the blob to be given back"):
        raise SystemExit("hmadrv: the boot never finished (-m %d)" % mem)
    os88qemu.pace(q, 1)
    return q


def rows(q):
    tab = q.read(S("drv_tab"), DRVR_SIZE * 8)
    out = {}
    for name, r, _f in DRVS:
        row = tab[r * DRVR_SIZE:(r + 1) * DRVR_SIZE]
        out[name] = {"disp": u16(row, 0), "seg": u16(row, 2),
                     "kb": u16(row, 8), "err": row[14],
                     "ent": u16(row, DRVR_ENT)}
    return out


def blocks(q):
    hma = open(os.path.join(B, "coldhma.sys"), "rb").read()
    t = q.read(0xFFFF0 + u16(hma, 12), 8 * 6)
    return [(u16(t, k), u16(t, k + 2), u16(t, k + 4))
            for k in range(0, len(t), 6) if u16(t, k)]


def hma_image(name):
    """What the tail must have put in the HMA: the org-DRV_HMA_ORG assembly,
    with the two header fields os88drv.py rewrites taken from the .drv."""
    want = bytearray(open(os.path.join(B, name + ".hma.bin"), "rb").read())
    img = os88drv.image_unwrap(open(os.path.join(B, name + ".drv"),
                                    "rb").read())
    want[8:10] = img[8:10]
    want[31] = img[31]
    return bytes(want)


def c_lists(q, want_file):
    ui = hmacold.QUI(q, mouse=hmacold.QMouse(q), sym=S, verbose=False)
    ui.open_drive("C")
    names = [n for n, _t in ui.listing()]
    return want_file in names, names, ui


def record_play(q, ui, snd_seg, want, hma):
    """RECORDER's demo, played on the card: the double buffer's path
    (SPEC.md 34.5.2). The card is the witness - SOUND.DRV's own stream state
    goes to underrun-paused only when its ISR has taken the last half, which
    is every byte of the stream having gone through the 8237 and an IRQ for
    each half. The package's status line would need a paint to say so."""
    syms, _ = pkg_syms("apps/recorder/recorder.asm")
    sb = drv_syms("sound")
    org = ORG if hma else 0
    # A larger driver does not enlarge the pool. Predict buffer placement
    # from actual free runs before opening, rather than assuming 8KB is free.
    at, largest = 0x10, 0
    if hma:
        for start, size, _owner in sorted(blocks(q)):
            largest = max(largest, start-at)
            at = max(at, start+size)
        largest = max(largest, EQ["HMA_COLD_AT"]-at)
    buffer_hma = hma and largest >= 8192

    def drv(name, n=1):
        return q.read((snd_seg << 4) + org + sb[name] - ORG, n)
    ui.path("B:/RECORDER.O88")
    seg = None
    for w in os88geom.windows(q, S):
        if w.title == "Recorder":
            seg = u16(q.read(os88geom.winptr(q, w.i, S) + os88geom.W_SEG, 2))
    if not seg:
        want(False, "RECORDER.O88 opened a window (titles: %s)"
             % [w.title for w in os88geom.windows(q, S)])
        return

    def msg():                  # [rc_msg] POINTS at the status line
        at = u16(q.readseg(seg, syms["rc_msg"], 2))
        return bytes(q.readseg(seg, at, 32)).split(b"\0")[0] \
            .decode("latin-1")
    ui.menu_pick("Sound", "Demo")
    if not os88qemu.acted(q, lambda: msg().startswith("DEMO STAGED"),
                          secs=20, what="the demo staged"):
        want(False, "the demo staged (%r)" % msg())
        return
    under0 = u16(drv("sbl_under", 2))
    ui.menu_pick("Sound", "Play")
    want(msg() == "PLAYING (SB)" and drv("sbl_str_act")[0] == 1,
         "Play opened a stream on the card (%r)" % msg())
    sbase = u16(drv("sbl_sbase", 2))
    page = drv("sbl_page")[0]
    blk = [b for b in blocks(q) if b[2] == snd_seg and b[1] == 8192] \
        if hma else []
    if buffer_hma:
        want(len(blk) == 1 and sbase == ORG and page == 0x10,
             "the double buffer is an 8KB block of the pool owned by "
             "SOUND.DRV: %s, [sbl_sbase] %04X, 8237 page %02X"
             % (["%04X+%d" % (b[0], b[1]) for b in blk], sbase, page))
    else:
        want(not blk and sbase == 0 and page < 0x0A,
             "the double buffer uses the heap (HMA largest free run %d): "
             "[sbl_sbase] %04X, 8237 page %02X" % (largest, sbase, page))
    # THE BYTES, not just the card: both halves hold the demo's samples,
    # each at a whole half's place in the stream and the two a half apart
    # (the refill worker has usually turned half 0 over by now), written
    # through [sbl_seg]:[sbl_sbase] - the CPU's half of the double buffer
    # being where the 8237's half is.
    #
    # The two reads are ONE SNAPSHOT: the guest is stopped across them, or
    # the refill worker turns a half over between the first read and the
    # second and the pair describes no moment the machine was ever in. And
    # the demo is not a whole number of halves (RC_D_LEN 8000, a half 2048),
    # so the stream's LAST half is its tail and then silence: locate() takes
    # that one as exactly sweep[p:] followed by 0x80s, p a whole half's place
    # - the shape the row used to call "not the demo's samples" whenever it
    # happened to look after the worker reached the end.
    sw = recorder.sweep()

    def locate(chunk):
        p = sw.find(chunk)
        if p >= 0:
            return p
        p = len(sw) - len(sw) % half if half else 0
        n = len(sw) - p
        if half and 0 < n < half and chunk[:n] == sw[p:] \
                and chunk[n:] == b"\x80" * (half - n):
            return p
        return -1
    q.hmp("stop")
    try:
        half = u16(drv("sbl_half", 2))
        at = (u16(drv("sbl_seg", 2)) << 4) + sbase
        pos = [locate(bytes(q.read(at + k * half, half))) for k in (0, 1)]
    finally:
        q.hmp("cont")
    want(half and all(p >= 0 and p % half == 0 for p in pos)
         and (pos[1] - pos[0]) % (2 * half) == half,
         "its two halves are the demo's samples at %s, in the stream's "
         "order" % pos)
    done = os88qemu.acted(q, lambda: drv("sbl_str_state")[0] != 0 and
                          u16(drv("sbl_under", 2)) != under0, secs=30,
                          what="the stream's last half", poll=0.2)
    want(done, "the card played the stream out of it: state %d, %d "
         "underrun(s) (the ISR's end-of-data)"
         % (drv("sbl_str_state")[0], u16(drv("sbl_under", 2)) - under0))
    # ...and the stream closes on the package's next poll: a menu command
    # that does nothing else asks for one (tests/recorder.py)
    ui.menu_pick("Sound", "Stop")
    os88qemu.acted(q, lambda: drv("sbl_str_act")[0] == 0, secs=10,
                   what="the stream closed", poll=0.2)
    if hma:
        blk = [b for b in blocks(q) if b[2] == snd_seg and b[1] == 8192]
        want(not blk and u16(drv("sbl_seg", 2)) == 0,
             "...and gave the block back with the stream")


def open_hd_page(q, hd):
    """Control Panel -> the Hard Disk page. Out: (click, x0, y0, tseg) - the
    panel's content origin and HDDTOOL.DRV's segment, 0 when it never came."""
    org_res = lambda name, n=1: q.read(hd["base"] + hd["syms"][name] - ORG, n)
    ui = hmacold.QUI(q, mouse=hmacold.QMouse(q), sym=S, verbose=False)
    ui.menu_pick("Apple", "Control Panel")
    w = ui.wait_window("Control Panel")
    x0, y0 = w.x + 1, w.y + TITLE_H
    nst = q.read(S("cp_nst"), 1)[0]
    mo = hmacold.QMouse(q)

    def click(x, y):
        mo.to(x, y)
        mo._edge(True)                  # QMouse's own edges: os88mouse's
        mo._edge(False)                 # click() waits on MartyPC's clock
    for k in range(4):                  # SOUND, HDD and NET each have a page
        # ...below eight statics, so the list SCROLLS (SPEC.md 31.1.5): eight
        # rows from [cp_ltop], and the down arrow until the page is among them
        for _ in range(4):
            top = q.read(S("cp_ltop"), 1)[0]
            if nst + k < top + CP_LVIS:
                break
            click(x0 + CP_LAX1 + 8, y0 + CP_LAY + CP_LAH // 2)
            os88qemu.acted(q, lambda: q.read(S("cp_ltop"), 1)[0] != top,
                           secs=5, what="the list to scroll")
        top = q.read(S("cp_ltop"), 1)[0]
        click(x0 + 40, y0 + CP_I0Y + (nst + k - top) * CP_IROWH + 7)
        os88qemu.acted(q, lambda: q.read(S("cp_sel"), 1)[0]
                       == EQ["CP_ITEMS"] + k, secs=10,
                       what="driver page %d selected" % k)
        if os88qemu.acted(q, lambda: u16(org_res("hd_tseg", 2)) != 0,
                          secs=10, what="HDDTOOL.DRV loaded"):
            return click, x0, y0, u16(org_res("hd_tseg", 2))
    return click, x0, y0, 0


def close_panel(q):
    xmcheck.qmp(q.sock, "sendkey esc")
    os88qemu.acted(q, lambda: not [w for w in os88geom.windows(q, S)
                                   if w.title == "Control Panel"],
                   secs=10, what="the Control Panel to close")


def hd_page(q, hdd_seg, want, hma):
    """The Control Panel's Hard Disk page: HDD.DRV loads HDDTOOL.DRV into the
    heap and the tool's first act on a paint is hd_sync, a far call BACK into
    the resident through the dispatcher pointer HDT_INIT handed it (SPEC.md
    52.11). From the HMA that dispatcher is at DRV_HMA_ORG + PKG_DISP, not at
    PKG_DISP - a tool that assumed the latter froze the field 286 on this
    very click, with the file progress bar at 100%. The witness is the copy
    the call makes: the tool's device table equal to the resident's."""
    org = ORG if hma else 0
    hd = {"syms": drv_syms("hdd"), "base": (hdd_seg << 4) + org}
    tl = drv_syms("hddtool", hma=False)
    res = lambda name, n=1: q.read(hd["base"] + hd["syms"][name] - ORG, n)
    _click, _x0, _y0, tseg = open_hd_page(q, hd)
    if not tseg:
        want(False, "the Hard Disk page loaded HDDTOOL.DRV")
        return
    tool = lambda name, n=1: q.read((tseg << 4) + tl[name], n)
    disp = tool("hd_rdisp", 4)
    want(u16(disp) == org + 12 and u16(disp, 2) == hdd_seg,
         "HDDTOOL.DRV at %04X calls the resident through %04X:%04X"
         % (tseg, u16(disp, 2), u16(disp)))
    synced = lambda: tool("hd_ndev")[0] == res("hd_ndev")[0] >= 1 and \
        tool("hd_devs", 12) == res("hd_devs", 12)
    ok = os88qemu.acted(q, synced, secs=10, what="the page's hd_sync")
    want(ok, "...and the page's hd_sync came back: %d device(s), the tool's "
         "row 0 %s" % (tool("hd_ndev")[0], tool("hd_devs", 12).hex()))
    close_panel(q)


def hd_install(q, hdd_seg, hdimg, want):
    """...and what the field 286 was opening that page FOR: Install, with
    HDD.DRV in the HMA. Every sector the installer moves is a service call
    from the tool into the resident (hd_raw is a thunk in the tool), and its
    file copies read into the tool's own heap image - so this is the whole of
    the split's crossing, run from the pool. Asserted off the disk: the
    installer's SYSTEM folder, with the driver and its tool in it."""
    import hashlib
    import instdeep
    hd = {"syms": drv_syms("hdd"), "base": (hdd_seg << 4) + ORG}
    click, x0, y0, tseg = open_hd_page(q, hd)
    if not tseg:
        want(False, "the Hard Disk page loaded HDDTOOL.DRV for Install")
        return
    known = {w.i for w in os88geom.windows(q, S)}
    click(x0 + instdeep.CP_RX + instdeep.HDP_B2X + instdeep.HDP_BW2 // 2,
          y0 + instdeep.HDP_BY + instdeep.HDP_BH // 2)
    box = {}

    def opened():
        new = [w for w in os88geom.windows(q, S) if w.i not in known]
        if new:
            box["w"] = new[0]
        return bool(new)
    if not os88qemu.acted(q, opened, secs=30, what="the installer window"):
        want(False, "Install opened the installer window")
        return
    iw = box["w"]
    ix, iy = iw.x + 1, iw.y + TITLE_H
    bx = ix + instdeep.HIW_B0X + instdeep.HIW_BW0 // 2
    by = iy + instdeep.HIW_BY + instdeep.HIW_BH // 2
    click(bx, by)                               # Install: arms
    os88qemu.pace(q, 2)
    click(bx, by)                               # Install: goes

    def tree():
        try:
            return instdeep.partition(hdimg).tree()
        except SystemExit:
            return {}
    digest = lambda: hashlib.sha1(open(hdimg, "rb").read()).digest()
    if not os88qemu.acted(q, lambda: "SYSTEM" in tree(), secs=300,
                          what="the installer's SYSTEM folder"):
        want(False, "the install wrote SYSTEM/ on C: (%s)" % sorted(tree()))
        return
    last, still = digest(), 0
    while still < 8:                            # the drive going quiet
        os88qemu.pace(q, 2)
        d = digest()
        still = still + 1 if d == last else 0
        last = d
    t = tree()
    got = [p for p in ("KERNEL.SYS", "HDD.DRV", "HDDTOOL.DRV", "COLDHMA.SYS",
                       "SYSTEM/TASKMGR.O88") if p in t]
    want(len(got) == 5, "the install finished, from the HMA: %s on C:%s"
         % (got, "" if len(got) == 5 else " (all of it: %s)" % sorted(t)))
    if len(got) != 5:
        subprocess.run([sys.executable, "tools/shot.py", q.sock,
                        os.path.join(B, "hmadrv-install.png")])


CP_I0Y, CP_IROWH, TITLE_H = 6, 14, 18   # kernel/ctrl.inc's item list
CP_LVIS, CP_LAY, CP_LAH, CP_LAX1 = 8, 118, 12, 52   # ...and its arrows


def drv_syms(name, hma=True):
    import re
    stem = name + (".hma" if hma else "")
    out = os.path.join(B, stem + ".map")
    if not os.path.exists(out) or os.path.getmtime(out) < \
            os.path.getmtime(os.path.join(B, stem + ".bin")):
        raise SystemExit("hmadrv: no %s - it is written beside %s.bin"
                         % (out, stem))
    return {m.group(2): int(m.group(1), 16) for m in
            re.finditer(r"^\s*([0-9A-F]+)\s+[0-9A-F]+\s+(\S+)$",
                        open(out).read(), re.M)}


def main():
    os.chdir(ROOT)
    fails = []

    def want(cond, what):
        print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
        if not cond:
            fails.append(what)

    for need in ("os8088.img", "coldhma.sys", "recorder.o88",
                 "mbr.bin", "boothd.bin", "kernel.sys"):
        if not os.path.exists(os.path.join(B, need)):
            sys.exit("hmadrv: no build/%s - run `make`" % need)
    with tempfile.TemporaryDirectory(dir=B) as tmp:
        sysimg, apps, hd = disks(tmp)

        print("A: -m 128, SOUND + HDD + NET wanted, an SB16 and an IDE disk")
        q = boot(tmp, 128, sysimg, apps, hd)
        want(u16(q.read(S("api_coldseg"), 2)) == 0xFFFF,
             "`.cold` is in the HMA (tests/hmacold.py's premise)")
        r = rows(q)
        for name, _r, _f in DRVS:
            x = r[name]
            want((0xFFFF - (ORG - 0x10)//16) <= x["seg"] <= (0xFFFF - (ORG - EQ["HMA_COLD_AT"])//16) and x["kb"] == 0
                 and x["disp"] == ORG + 12 and x["err"] == 0,
                 "%s.DRV runs at %04X:%04X, %d KB of heap, error %d"
                 % (name, x["seg"], x["disp"], x["kb"], x["err"]))
        blk = blocks(q)
        want(sorted(b[2] for b in blk) == sorted(r[n]["seg"] for n, _r, _f
                                                 in DRVS),
             "the pool holds the three images and nothing else: %s"
             % ["%04X+%d/%04X" % b for b in blk])
        for name, _r, f in DRVS:
            # a LIVE driver has written its own variables since attach, so
            # what is compared is what nothing writes: the header and the
            # first 64 bytes of its entry proc, at the offsets the
            # relocation produced
            img = hma_image(f)
            base = (r[name]["seg"] << 4) + ORG
            got = q.read(base, len(img))
            ent = r[name]["ent"] - ORG
            same = got[:32] == img[:32] and \
                got[ent:ent + 64] == img[ent:ent + 64]
            want(same and u16(got, 4) == ORG,
                 "%s.DRV in the HMA is its org-%04X assembly: the header "
                 "(link base %04X) and the entry proc at %04X"
                 % (name, ORG, u16(got, 4), r[name]["ent"]))
        ok, names, ui = c_lists(q, "HELLO.TXT")
        want(ok, "HDD.DRV mounted C: from the HMA and it lists %s" % names)
        hd_page(q, r["HDD"]["seg"], want, True)
        record_play(q, ui, r["SOUND"]["seg"], want, True)
        hd_install(q, r["HDD"]["seg"], hd, want)
        q.close()

        print("B: -m 1, the same disks: no HMA")
        q = boot(tmp, 1, sysimg, apps, hd)
        r = rows(q)
        for name, _r, _f in DRVS:
            x = r[name]
            want(0 < x["seg"] < 0xA000 and x["kb"] > 0 and x["disp"] == 12,
                 "%s.DRV in the heap at %04X, %d KB" % (name, x["seg"],
                                                       x["kb"]))
        ok, names, ui = c_lists(q, "HELLO.TXT")
        want(ok, "...and C: is mounted all the same: %s" % names)
        hd_page(q, r["HDD"]["seg"], want, False)
        record_play(q, ui, r["SOUND"]["seg"], want, False)
        q.close()

    if fails:
        sys.exit("hmadrv: %d failed" % len(fails))
    print("hmadrv: ok")


if __name__ == "__main__":
    main()
