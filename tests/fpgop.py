#!/usr/bin/env python3
"""fpgop - a file copy is ONE bar, and it reaches full (SPEC.md 12.8.8).

    make && python3 tests/fpgop.py [drv|bios ...]

A 120KB file is copied B: -> C: with the user's own Copy/Paste on
os8088_xt_hdd, and the row reads the widget's state when the paste is over:
`[fpg_done]` against the operation's scale `[fpg_opt]`, and the lit extent
`[fpg_px]` against the trough's 62 pixels. Two arms, because C: reaches the
disk two different ways:

  drv   booted off a FLOPPY, so C: is HDD.DRV's (a DVK_DRV volume) and its
        sectors go through drv_blk_call_x, not the int 13h loop
  bios  booted off the VHD, so C: is the kernel's own boot partition

Each arm also asserts ONE fpg_opbegin (one operation, not one per chunk) and
no fpg_hatch between it and its fcp_opend (fcp_xfer's end of it) - the gaps between chunks (a FAT flush, a directory
commit) last a frame or two, so a hatch there was a flash and not
information.

VERIFIED TO FAIL: with dsk_xfer's driver arm stepping nothing (the code
before the fix), `drv` reads 241 of 480 units and 31 of 62 pixels - the bar
stopped at half and the paste ended, which is what the field reported.
"""
import os
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty as M                                       # noqa: E402
import os88ui                                               # noqa: E402
import fcproom as F                                         # noqa: E402
import rdcz                                                 # noqa: E402
import subprocess                                           # noqa: E402

TROUGH = 62
fails = []


def arm(kind):
    data = os.urandom(120 * 1024)
    tmp = tempfile.mkdtemp(prefix="fpgop-")
    vhd, flop = F.fixture(tmp, "empty", data)
    boot = "build/os8088-360.img" if kind == "drv" else None
    m = M.launch(boot, apps=flop, machine=F.MACHINE,
                 extra=["--mount", "hd:0:" + vhd], boot=False)
    try:
        m.run()
        ui = os88ui.UI(m)
        ui.up(limit=300)
        M.no_saver(m)
        src = ui.open_drive("B")
        F.select(ui, F.NAME, src)
        ui.menu_pick("Edit", "Copy")
        ui.open_drive("C")
        with M.bp_trace(m, "fpg_opbegin", "fcp_opend", "fpg_hatch", cap=2000) as tr:
            ui.menu_pick("Edit", "Paste")
            M.until(m, lambda mm: mm.read(mm.sym("fcp_busy"), 1)[0] == 0,
                    "the paste to finish", guest=240.0, poll=0.05)

        def rd(s):
            return int.from_bytes(m.read(m.sym(s), 2), "little")
        opt, done, px = rd("fpg_opt"), rd("fpg_done"), rd("fpg_px") & 0xFF
        err = m.read(m.sym("fcp_err"), 1)[0]
    finally:
        m.quit()
    names = [h["name"] for h in tr.hits]
    begins = names.count("fpg_opbegin")
    late = 0
    if begins:
        i = names.index("fpg_opbegin")
        j = names.index("fcp_opend", i) if "fcp_opend" in names[i:] else len(names)
        late = names[i:j].count("fpg_hatch")
    print("fpgop: %s: %d of %d units, %d of %d pixels, %d operation(s), "
          "%d hatch(es) inside it, fcp_err %d"
          % (kind, done, opt, px, TROUGH, begins, late, err), flush=True)
    if err:
        fails.append("%s: the paste ended fcp_err %d" % (kind, err))
    if begins != 1:
        fails.append("%s: %d fpg_opbegin calls, not one" % (kind, begins))
    if not opt or done < opt or px != TROUGH:
        fails.append("%s: the bar ended at %d of %d units, %d of %d pixels"
                     % (kind, done, opt, px, TROUGH))
    if late:
        fails.append("%s: %d hatch(es) inside the operation" % (kind, late))
    if F.ID.partition(vhd).read(F.NAME) != data:
        fails.append("%s: the copy on C: is not the floppy's file" % kind)


def paste(ui, src, name, dst):
    m = ui.m
    sw = ui.open_drive(src)
    rdcz.select(ui, name, win=sw)
    ui.menu_pick("Edit", "Copy")
    dw = ui.open_drive(dst[0])
    if dst[2:]:                         # a folder: opened IN that drive's
        ui.open(dst[3:], win=dw)        # window, which need not be the front
    with M.bp_trace(m, "fpg_opbegin", "fcp_opend", "fpg_hatch", cap=2000) as tr:
        ui.menu_pick("Edit", "Paste")
        M.until(m, lambda mm: mm.read(mm.sym("fcp_busy"), 1)[0] == 0,
                "the paste to finish", guest=240.0, poll=0.05)

    def rd(s):
        return int.from_bytes(m.read(m.sym(s), 2), "little")
    return (rd("fpg_opt"), rd("fpg_done"), rd("fpg_px") & 0xFF,
            m.read(m.sym("fcp_err"), 1)[0], [h["name"] for h in tr.hits])


def judge(kind, opt, done, px, err, names):
    begins = names.count("fpg_opbegin")
    late = 0
    if begins:
        i = names.index("fpg_opbegin")
        j = names.index("fcp_opend", i) if "fcp_opend" in names[i:] else len(names)
        late = names[i:j].count("fpg_hatch")
    print("fpgop: %s: %d of %d units, %d of %d pixels, %d operation(s), "
          "%d hatch(es) inside it, fcp_err %d"
          % (kind, done, opt, px, TROUGH, begins, late, err), flush=True)
    if err:
        fails.append("%s: the paste ended fcp_err %d" % (kind, err))
    if begins != 1:
        fails.append("%s: %d fpg_opbegin calls, not one" % (kind, begins))
    if not opt or done < opt or px != TROUGH:
        fails.append("%s: the bar ended at %d of %d units, %d of %d pixels"
                     % (kind, done, opt, px, TROUGH))
    if late:
        fails.append("%s: %d hatch(es) inside the operation" % (kind, late))


def ram():
    """A REDIRECTED volume both ways: a 40KB file B: -> the RAM disk D: (its
    writes are RAMDISK.DRV's OSAPI_FS_PROG reports), then D: -> B:\\OUT (its
    reads are FSV_READAT, which must open a phase for those reports to count
    at all)."""
    tmp = tempfile.mkdtemp(prefix="fpgop-")
    big = os.path.join(tmp, F.NAME)
    open(big, "wb").write(os.urandom(40 * 1024))
    flop = os.path.join(tmp, "b.img")
    tiny = os.path.join(tmp, "KEEP.TXT")
    open(tiny, "wb").write(b"x")
    subprocess.check_call(["python3", "tools/os88disk.py", "-o", flop,
                           "--size", "360", big, "OUT:" + tiny],
                          cwd=ROOT, stdout=subprocess.DEVNULL)
    with os88ui.boot("build/os8088-360.img", apps=flop,
                     machine="os8088_5150_cga_gla", quick=True) as ui:
        rdcz.mount_ramdisk(ui)
        judge("ram-in", *paste(ui, "B", F.NAME, "D"))
        judge("ram-out", *paste(ui, "D", F.NAME, "B:/OUT"))


def main():
    for k in sys.argv[1:] or ["drv", "bios", "ram"]:
        ram() if k == "ram" else arm(k)
    for f in fails:
        print("fpgop: FAIL: " + f)
    print("fpgop: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
