#!/usr/bin/env python3
"""Does Write Img... put an image on a floppy, byte for byte?

    python3 tests/wimgtrip.py

SPEC.md 18.99.8 and 38.6.2, and docs/FIELD-NOTES.md 32. `tests/diskclone.py`
drives the command as far as the chooser and a cancel, and says why it stops
there: an image of a 360KB disk is 720 sectors and a 360KB volume's data area
is 708, so on a machine whose drives are all 360KB there is nowhere to put the
FILE. This row is the other half, on `os8088_5150_cga_720b_gla` - a 360KB A:
and a 720KB B:, which is also the reporter's own 5150.

The fixture is two artefacts `make all` already builds: `apps360.img` put as a
FILE on a 720KB data disk in B:, and the 360KB system disk in A: as the
target. The apps disk is not the system disk, so the assertion cannot pass by
the write doing nothing - that is checked before the write, as the positive
control, and after it drive 0 must BE the apps image, every one of its 720
sectors.

What it caught first, before it existed: the Standard File dialog handed its
callback a size of 0:0 (SPEC.md 38.6.2), and Write Img sizes the geometry off
that figure alone, so every image was refused as 'Not a disk image'. The leg
that reads CL_TOT after the dialog is that regression, asserted on the job
block rather than inferred from a toast.
"""
import os
import re
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests/unit")
import os88marty as M                                     # noqa: E402
import os88ui                                             # noqa: E402
import os88build                                          # noqa: E402
from harness import check, done                           # noqa: E402

MACHINE = "os8088_5150_cga_720b_gla"
SYS = "build/os8088-360.img"
IMAGE = "build/apps360.img"
IMGNAME = "APPS360.IMG"


def equ(path, name):
    src = open(path).read()
    m = re.search(r"^%s\s+equ\s+(\d+)" % name, src, re.M)
    if not m:
        sys.exit("%s: no `%s equ`" % (path, name))
    return int(m.group(1))


FS_EDIT = equ("kernel/files.inc", "FS_EDIT")
FS_SIZE = equ("kernel/files.inc", "FS_SIZE")
CL_STEP = equ("kernel/clone.inc", "CL_STEP")
CL_TOT = equ("kernel/clone.inc", "CL_TOT")
CL_SPT = equ("kernel/clone.inc", "CL_SPT")
CLS_WIMG = equ("kernel/clone.inc", "CLS_WIMG")
CLS_PICK = equ("kernel/clone.inc", "CLS_PICK")
CL_TGT = equ("kernel/clone.inc", "CL_TGT")
CLE_IMG = int(re.search(r"^CLE_IMG\s+equ\s+(0x[0-9A-Fa-f]+)",
                        open("kernel/clone.inc").read(), re.M).group(1), 16)
TITLE_H = 18            # SPEC.md 11: a window's content starts 18 below it
FS_SEL = equ("kernel/files.inc", "FS_SEL")
FS_DRV = equ("kernel/files.inc", "FS_DRV")


def u16(b, o=0):
    return b[o] | (b[o + 1] << 8)


def chooser(ui, form):
    """The Standard File chooser, or None - it is a Disk window in a chooser
    role (SPEC.md 38.1), captioned 'Open' or 'Save As' by its form."""
    try:
        w = ui.chooser(limit=60)
    except os88ui.UIError:
        return None
    return w if w.title == form else None


def chblk(m):
    """The chooser's own pool block, linear - [fdlg_blk] is a KERNEL_SEG
    offset (SPEC.md 38.1)."""
    return (M.KERNEL_SEG << 4) + u16(m.read(m.sym("fdlg_blk"), 2))


def edit(ui, dw):
    """The requester Disk window's OWN edit mode. Its block and not the first
    non-zero one in fm_pool: with a Save chooser up the chooser's block holds
    mode 8 (its name box, SPEC.md 38.5) beside the requester's 7."""
    return ui.m.read(ui._fsblk(dw) + FS_EDIT, 1)[0]


def job(m, off, n=1):
    seg = u16(m.read(m.sym("clo_seg"), 2))
    if not seg:
        return None
    b = m.readseg(seg, off, n)
    return b[0] if n == 1 else u16(b)


def to_b(ui, m, w):
    """Walk the chooser's Drive button to B: (SPEC.md 38.11), confirmed on
    the chooser's OWN volume - FS_DRV of its block, which is what it lists;
    the globals are put back only at a commit (38.7)."""
    def drv():
        return m.read(chblk(m) + FS_DRV, 1)[0]
    for _ in range(4):                  # Drive walks the volumes
        if drv() == 1:
            break
        ui.chooser_button(ui.CH_DRIVE, w)
        M.settle(m)
    check(drv() == 1, "...and its Drive button reaches B:", "",
          got=drv(), want=1)


def pick_image(ui, m):
    """File > Write Img..., then B: and the image's row in the chooser."""
    ui.menu_pick("File", "Write Img...")
    w = chooser(ui, "Open")
    check(w is not None, "Write Img... opens the Open chooser",
          "fm_c_wimg is fdlg_open_x and nothing else (SPEC.md 22.21.5)")
    if w is None:
        done("wimgtrip")
    to_b(ui, m, w)
    idx = ui.entry(IMGNAME, w)[0]
    row = ui.scroll_to(idx, win=w)
    ui.mo.click(*ui.row_xy(w, row))
    M.settle(m)
    sel = u16(m.read(chblk(m) + FS_SEL, 2))
    check(sel == idx, "a click on the row selects the image",
          "the Open form's single click is the Disk window's: it selects "
          "and does not answer (SPEC.md 38.4)", got=sel, want=idx)


# PER-PROCESS, for docs/WRITING-TESTS.md 5.5: the runner runs rows side by
# side and scratch_disk would otherwise be one file three machines rebuild.
FIX = M.scratch_disk("build/wimgtrip-%d.img" % os.getpid(), IMAGE, size=720)
want = open(os88build.at(IMAGE), "rb").read()
flush = os.path.abspath(os88build.at("build/wimgtrip-%d.a" % os.getpid()))

try:
    with os88ui.boot(SYS, apps=FIX, machine=MACHINE) as ui:
        m = ui.m
        print("== %s : Write Img... round trip (SPEC.md 18.99.8) ==" % MACHINE)
        dw = ui.open_drive("A")

        # --- 1. a write that FAILS must SAY so (SPEC.md 18.99.7) -----------
        # No emulated drive here fails a write on its own, so the fault is
        # made: the first WRITE int 13h is caught at the gate and [dsk_unit]
        # pointed at drive 3, which is not there - so every attempt and the
        # whole per-sector fallback time out (80h), exactly as a transfer
        # the platter will not take. What is asserted is the VERDICT. It was
        # silence: the verb returned CLA_ERR with CF still set, the resident
        # side repacked it as error 83h, and toast_say refused that as past
        # its table - the mode just ended, with nothing to photograph.
        pick_image(ui, m)
        m.key("Enter")
        M.settle(m)
        check(edit(ui, dw) == 7 and job(m, CL_STEP) == CLS_WIMG,
              "Write Img arms its confirmation", "",
              got=(edit(ui, dw), job(m, CL_STEP)), want=(7, CLS_WIMG))
        m.breakpoints([{"type": "int", "addr": 0x13}])
        m.key("Enter")
        hit = False
        for _ in range(200):
            if not m.wait_stop(60):
                break
            r = m.regs()
            if r["ax"] >> 8 == 0x03 and r["dx"] & 0xFF == 0:
                m.write(m.sym("dsk_unit"), bytes([3]))
                m.setreg("dx", (r["dx"] & 0xFF00) | 3)
                hit = True
                break
            m.run()
        m.breakpoints([])
        m.run()
        check(hit, "...and its first write to A: is reached, and failed", "")
        M.settle(m, limit=400)
        check(ui.toast()[0] == "Disk error",
              "a FAILED Write Img says 'Disk error'",
              "clo_key must answer CF=0 with CLA_ERR/CERR_IO; with the carry "
              "left set the resident caller repacks it as code 83h and "
              "toast_say says nothing (SPEC.md 18.99.7)",
              got=ui.toast()[0], want="Disk error")
        check(edit(ui, dw) == 0 and u16(m.read(m.sym("clo_seg"), 2)) == 0,
              "...and still ends the mode and gives the claim back", "")
        # FIRST, because it cannot change A: - the write fails at the first
        # chunk and sector 0 goes down last (SPEC.md 18.99.2) - while the round
        # trip below replaces the system disk, CLONE.DRV and all, and a second
        # Write Img after it could not load the module at all ('No disk').

        # --- 1b. Clone Disk... with an IMAGE as its target: the Save box ---
        # SPEC.md 18.99.8. CLONE.DRV opens this box ITSELF (size pass 8),
        # through fdf_fdlg_open with the resident fm_img_done_x as the
        # completion proc - where it used to answer CLA_SAVE and have
        # fm_editkey open it. So what is asserted is everything the image now
        # hands the box: the requester window (the prompt must still be armed
        # under it, because a cancel comes back to it), the default name, and
        # - through the commit - the SAVE half of the proc reaching the CLONE
        # and not Uncompress To's join, with the box's answer copied into
        # clo_fnbuf by the image (clo_fnget). A whole image of A: cannot be
        # written here (B: holds the apps image, 706 free sectors of the 720
        # it needs), and that is useful rather than a gap: the refusal is
        # clo_froom's, reached only once clo_saved has run on the clone's own
        # claim with the name the box gave.
        ui.menu_pick("File", "Clone Disk...")
        M.settle(m)
        for _ in range(6):
            if job(m, CL_TGT) == CLE_IMG:
                break
            m.key("Space")
            M.settle(m)
        check(job(m, CL_TGT) == CLE_IMG, "Clone Disk's Space reaches IMG", "",
              got=job(m, CL_TGT), want=CLE_IMG)
        m.key("Enter")
        M.settle(m)
        d = chooser(ui, "Save As")
        check(d is not None, "Enter on IMG opens the Save As chooser",
              "clo_key's .pickgo: fdf_fdlg_open, with BX = the window the key "
              "came to - a wrong window is refused and nothing opens")
        if d is None:
            done("wimgtrip")
        name = bytes(m.read(m.sym("fdlg_name"), 13)).split(b"\0")[0]
        box = bytes(m.read(m.sym("fm_ebuf"), 13)).split(b"\0")[0]
        check((name, box) == (b"DISK.IMG", b"DISK.IMG"),
              "...on the default name DISK.IMG, in the box",
              "staged by the image into fm_hdrbuf, which the chooser seeds "
              "fdlg_name from - and the box on the glass is the status-line "
              "editor in mode 8, whose buffer is fm_ebuf (SPEC.md 38.5)",
              got=(name, box), want=(b"DISK.IMG", b"DISK.IMG"))
        check(edit(ui, dw) == 7 and job(m, CL_STEP) == CLS_PICK,
              "...with the pick prompt still armed under it", "",
              got=(edit(ui, dw), job(m, CL_STEP)), want=(7, CLS_PICK))
        to_b(ui, m, d)
        m.key("Enter")                          # commit DISK.IMG on B:
        M.settle(m, limit=200)
        fn = bytes(m.read(m.sym("clo_fnbuf"), 13)).split(b"\0")[0]
        check(fn == b"DISK.IMG", "the box's answer reaches clo_fnbuf",
              "clo_fnget, in the image, at CLV_SAVED", got=fn, want=b"DISK.IMG")
        check(ui.toast()[0] == "Disk full",
              "...and the clone's own room check refuses it: 'Disk full'",
              "706 sectors free on B: against 720 - clo_froom, which only a "
              "CLV_SAVED dispatched to clo_saved on the live claim reaches",
              got=ui.toast()[0], want="Disk full")
        check(edit(ui, dw) == 0 and u16(m.read(m.sym("clo_seg"), 2)) == 0,
              "...and the mode ends and the claim goes back", "")

        # --- 2. ...and one that works puts the image down exactly ----------
        m.flush(0, flush)
        before = open(flush, "rb").read()
        check(before != want, "the target is NOT the image before the write",
              "the positive control: A: is the system disk and the image is "
              "the apps disk, so a write that did nothing cannot pass below")

        pick_image(ui, m)
        m.key("Enter")
        M.settle(m)
        check(ui.toast()[0] != "Not a disk image",
              "the dialog's answer is NOT refused as 'Not a disk image'",
              "SPEC.md 38.6.2: fdlg_commit took the size AFTER fdlg_close had "
              "freed the listing it looks the name up in, so every Open "
              "reported 0:0 and clo_geom_img matched no layout",
              got=ui.toast()[0], want="(anything else)")
        check(edit(ui, dw) == 7 and job(m, CL_STEP) == CLS_WIMG,
              "...it arms the confirmation instead (CLS_WIMG)",
              "FS_EDIT 7 is the cloner's mode (SPEC.md 22.21.1)",
              got=(edit(ui, dw), job(m, CL_STEP)), want=(7, CLS_WIMG))
        check((job(m, CL_TOT, 2), job(m, CL_SPT, 2)) == (720, 9),
              "...with the geometry of a 360KB disk, from the SIZE",
              "clo_geom_img: 368,640 bytes is 720 sectors of 9 a track - "
              "this is the dialog's DX:CX arriving intact",
              got=(job(m, CL_TOT, 2), job(m, CL_SPT, 2)), want=(720, 9))

        m.key("Enter")
        M.settle(m, limit=400)
        check(ui.toast()[0] == "Wrote A:", "Enter writes it: 'Wrote A:'", "",
              got=ui.toast()[0], want="Wrote A:")
        check(edit(ui, dw) == 0 and u16(m.read(m.sym("clo_seg"), 2)) == 0,
              "...the mode ends and the claim goes back", "")

        m.flush(0, flush)
        after = open(flush, "rb").read()
        bad = [i // 512 for i in range(0, len(want), 512)
               if after[i:i + 512] != want[i:i + 512]]
        check(len(after) == len(want) and not bad,
              "drive 0 IS the image, every sector of it",
              "an image is the disk and nothing else: a wrong LBA, a lost "
              "sector 0 (SPEC.md 18.99.2) or a chunk read out of the file at "
              "the wrong offset all show here as sector numbers",
              got="%d sectors differ, first %s" % (len(bad), bad[:8]),
              want="0 sectors differ")

finally:
    for p in (FIX, FIX + ".args", flush):
        try:
            os.remove(p)
        except OSError:
            pass

done("wimgtrip")
