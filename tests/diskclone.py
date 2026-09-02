#!/usr/bin/env python3
"""Does Clone Disk... produce a disk that is byte for byte the source?

    python3 tests/diskclone.py

SPEC.md 18.99 / 22.21.  The assertion that matters is not a screenshot: a
clone either is an exact copy or it is not, and MartyPC can hand the HOST both
floppies back (`Marty.flush`, docs/MARTYPC-DEBUG.md).  So this drives the
menu, presses the keys, and then **diffs the two disks the guest is holding**
- which is the operation's whole contract in one comparison and cannot pass
for the wrong reason.

Three passes, in one boot because a boot is eight seconds and a clone is
twenty:

  1. **Cross drive.** A: -> B:, one Enter, no prompts, because both disks are
     in the machine and nothing has to move (SPEC.md 18.99.4).  Then
     drive 0 and drive 1 are read back and compared byte for byte.

  2. **Same drive.** A: -> A:, which is the one-floppy machine's case and the
     one the buffer planner exists for.  The disk is never actually swapped,
     so this is also the test of SPEC.md 18.99.3: the target prompt's identity
     check must FIRE, and land on CLS_SAME rather than writing.

  3. **Esc.** At a prompt, which is the only place input is required - and the
     claim and CLONE.DRV must both be gone afterwards (SPEC.md 22.21.2).

  4. **`Write Img...`** as far as this machine can take it (SPEC.md 18.99.8).
     The round trip is NOT here: a 360KB disk's image is 720 sectors and a
     360KB volume's data area is 708, so on the only floppy geometry MartyPC's
     os8088 machines have there is nowhere to put one.  Everything up to the
     transfer is here - the item, the dialog, and that a CANCEL puts the
     machine back exactly as it found it, which is the half SPEC.md 22.21.5
     says the command is built around.

The steps are read out of the cloner's own job block rather than off the
glass: CL_STEP is one byte at a known offset in the claim `[clo_seg]` names,
and a byte is not a rendering.
"""
import os
import re
import sys
import time

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
sys.path.insert(0, "tests/unit")
import os88marty as M                                     # noqa: E402
from os88mouse import Mouse                               # noqa: E402
import dispcp                                             # noqa: E402
from harness import check, done                           # noqa: E402

MACHINE = "os8088_5150_cga_gla"
SRC = "build/os8088-360.img"
APPS = "build/apps360.img"
# ABSOLUTE: `flush` is the EMULATOR writing the file, and its cwd is
# MartyPC's run directory rather than the repo root - a relative path
# there lands somewhere nothing then reads, or does not land at all.
OUT = os.path.abspath(os.path.join("build", "clonetest"))

# The File menu's title cell and the item pitch.  SPEC.md 22's own layout -
# titles at x 112/160/208/248, MENU_ITEM_H = 16 in a 19px bar - so item k's
# centre is 28 + 16k and 'Clone Disk...' is item FM_ICLONE.
FILE_X, BAR_Y = 120, 8
ITEM0_Y, ITEM_H = 28, 16


def equ(path, name):
    src = open(path).read()
    m = re.search(r"^%s\s+equ\s+(\d+)" % name, src, re.M)
    if not m:
        sys.exit("%s: no `%s equ`" % (path, name))
    return int(m.group(1))


FS_EDIT = equ("kernel/files.inc", "FS_EDIT")
FS_SIZE = equ("kernel/files.inc", "FS_SIZE")
FM_ICLONE = equ("kernel/files.inc", "FM_ICLONE")
FM_IWIMG = equ("kernel/files.inc", "FM_IWIMG")
CL_STEP = equ("kernel/clone.inc", "CL_STEP")
CL_SRC = equ("kernel/clone.inc", "CL_SRC")
CL_TGT = equ("kernel/clone.inc", "CL_TGT")
CLS_PICK = equ("kernel/clone.inc", "CLS_PICK")
CLS_SRC = equ("kernel/clone.inc", "CLS_SRC")
CLS_TGT = equ("kernel/clone.inc", "CLS_TGT")
CLS_SAME = equ("kernel/clone.inc", "CLS_SAME")
CL_CHUNK = equ("kernel/clone.inc", "CL_CHUNK")
CL_XON = equ("kernel/clone.inc", "CL_XON")
CL_TOT = equ("kernel/clone.inc", "CL_TOT")
CLE_IMG = 0xFE          # clone.inc's sentinel; not a decimal equ, so it is
                        # written here and asserted against the source below
MOD_CLONE = equ("kernel/mod.inc", "MOD_CLONE")
if not re.search(r"^CLE_IMG\s+equ\s+0x%02X" % CLE_IMG, open("kernel/clone.inc").read(),
                 re.M | re.I):
    sys.exit("tests/diskclone.py: CLE_IMG moved in kernel/clone.inc")
MODR_SIZE = equ("kernel/mod.inc", "MODR_SIZE")


def word(m, addr):
    b = m.read(addr, 2)
    return b[0] | (b[1] << 8)


class Probe:
    """The kernel state this test asserts on, by name."""

    def __init__(self, m):
        self.m = m
        self.pool = m.sym("fm_pool")
        self.clo_seg = m.sym("clo_seg")
        self.mod_tab = m.sym("mod_tab")

    def edit(self):
        """The FS_EDIT of the first window that has one armed (0 = none)."""
        for slot in range(4):
            b = self.m.read(self.pool + slot * FS_SIZE, FS_SIZE)
            if b[FS_EDIT]:
                return b[FS_EDIT]
        return 0

    def claim(self):
        return word(self.m, self.clo_seg)

    def job(self, off):
        seg = self.claim()
        if not seg:
            return None
        return self.m.readseg(seg, off, 1)[0]

    def jobw(self, off):
        seg = self.claim()
        if not seg:
            return None
        b = self.m.readseg(seg, off, 2)
        return b[0] | (b[1] << 8)

    def image(self):
        """CLONE.DRV's MODR_SEG - 0 when the image is not resident."""
        return word(self.m, self.mod_tab + MOD_CLONE * MODR_SIZE)


def open_clone(m, mo):
    """File > Clone Disk..., and settle."""
    mo.menu(FILE_X, BAR_Y, FILE_X + 30, ITEM0_Y + FM_ICLONE * ITEM_H)
    M.settle(m)


def open_wimg(m, mo):
    """File > Write Img..., and settle."""
    mo.menu(FILE_X, BAR_Y, FILE_X + 30, ITEM0_Y + FM_IWIMG * ITEM_H)
    M.settle(m)


def toast(m):
    """What cw_toast_say is currently showing, as text."""
    b = m.read(m.sym("toast_buf"), 24)
    return b.split(b"\x00")[0].decode("ascii", "replace").rstrip()


def disks_agree(m, tag):
    os.makedirs(OUT, exist_ok=True)
    a = os.path.join(OUT, "a.img")
    b = os.path.join(OUT, "b.img")
    m.flush(0, a)
    m.flush(1, b)
    da, db = open(a, "rb").read(), open(b, "rb").read()
    check(da == db, "%s: B: is byte for byte A:" % tag,
          "a clone is an EXACT copy or it is nothing - a file copy would "
          "agree about the files and differ about the boot sector, the free "
          "clusters and the layout (SPEC.md 18.99)",
          got=db, want=da)


with M.launch(SRC, apps=APPS, machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    p = Probe(m)
    dispcp.open_drive(m, mo, lambda n: m.sym(n), M.settle, letter="A")
    print("== %s : Clone Disk... (SPEC.md 18.99/22.21) ==" % MACHINE)

    # --- 1. cross drive ------------------------------------------------------
    open_clone(m, mo)
    check(p.edit() == 7, "the pick prompt is armed",
          "FS_EDIT 7 is the cloner's one mode (SPEC.md 22.21.1); 0 means "
          "fm_c_clone refused and 6 means it could not read CLONE.DRV",
          got=p.edit(), want=7)
    check(p.claim() != 0, "...and the job block is claimed",
          "[clo_seg] IS the operation (SPEC.md 18.99.1)")
    check(p.job(CL_STEP) == CLS_PICK, "...at CLS_PICK", "",
          got=p.job(CL_STEP), want=CLS_PICK)
    check((p.job(CL_SRC), p.job(CL_TGT)) == (0, 1),
          "...offering A: -> B:",
          "clo_nextvol starts on the volume AFTER the source, so a two-drive "
          "machine offers the OTHER drive first (SPEC.md 22.21.1)",
          got=(p.job(CL_SRC), p.job(CL_TGT)), want=(0, 1))

    t0 = time.time()
    m.key("Enter")
    M.settle(m, limit=180)
    print("   cross-drive clone: %.1fs of wall clock" % (time.time() - t0))
    check(p.edit() == 0, "the mode ended", "", got=p.edit(), want=0)
    check(p.claim() == 0, "...the claim went back (SPEC.md 22.21.2)", "",
          got=p.claim(), want=0)
    check(p.image() == 0, "...and so did CLONE.DRV", "", got=p.image(), want=0)
    disks_agree(m, "cross drive")

    # --- 2. same drive, and the disk was never swapped -----------------------
    open_clone(m, mo)
    m.key("Space")                          # B: -> IMG ...
    M.settle(m)
    check(p.job(CL_TGT) == CLE_IMG, "Space offers the image file",
          "IMG is one more stop on the target ring rather than a mode "
          "(SPEC.md 22.21.5), so it sits between the last drive and the wrap",
          got=p.job(CL_TGT), want=CLE_IMG)
    m.key("Space")                          # ... -> A:, wrapping
    M.settle(m)
    check(p.job(CL_TGT) == 0, "...and then walks round to A:",
          "clo_nextvol wraps past the free rows (SPEC.md 22.21.1)",
          got=p.job(CL_TGT), want=0)
    m.key("Enter")
    M.settle(m)
    check(p.job(CL_STEP) == CLS_SRC, "a same-drive clone asks for the source",
          "the two drives being one is what makes the swap prompts necessary",
          got=p.job(CL_STEP), want=CLS_SRC)

    t0 = time.time()
    m.key("Enter")                          # the read pass
    M.settle(m, limit=180)
    print("   read pass: %.1fs of wall clock" % (time.time() - t0))
    check(p.job(CL_STEP) == CLS_TGT, "...then for the target", "",
          got=p.job(CL_STEP), want=CLS_TGT)

    m.key("Enter")                          # ...and it was never swapped
    M.settle(m, limit=120)
    check(p.job(CL_STEP) == CLS_SAME,
          "the identity check FIRES on an un-swapped disk",
          "SPEC.md 18.99.3: the boot sector in the drive is the one just read, "
          "so this is the guard against a user pressing Enter without getting "
          "up. Landing on CLS_TGT-done instead means the check answered no - "
          "the first suspect is [dsk_rah_busy] not being raised for the read, "
          "which serves the SOURCE's sector 0 out of the cache",
          got=p.job(CL_STEP), want=CLS_SAME)

    check(p.jobw(CL_CHUNK) == p.jobw(CL_TOT),
          "...and the whole disk fits ONE trip on this machine",
          "SPEC.md 18.99.4: a 360KB disk against a 5150's heap is one read and "
          "one write, so the user gets up twice and not twenty-two times",
          got=p.jobw(CL_CHUNK), want=p.jobw(CL_TOT))
    check(p.job(CL_XON) == 0, "...out of CONVENTIONAL memory, on an 8088",
          "there is no store above 1MB on this machine and xm_caps says so "
          "(SPEC.md 41): a 1 here means the tier was guessed rather than asked",
          got=p.job(CL_XON), want=0)

    # --- 3. ...and Y writes it anyway ----------------------------------------
    # The disk in the drive IS the source, so the write puts A:'s own bytes
    # back over A: - which is a real exercise of clo_wr and of clo_fin's
    # sector-0-last (SPEC.md 18.99.2) with an assertion that cannot be fooled:
    # the disk afterwards must be the disk before, to the byte.
    before = os.path.join(OUT, "before.img")
    m.flush(0, before)
    t0 = time.time()
    m.key("KeyY")
    M.settle(m, limit=180)
    print("   write pass: %.1fs of wall clock" % (time.time() - t0))
    check(p.edit() == 0, "Y completes the clone", "", got=p.edit(), want=0)
    check(p.claim() == 0, "...and gives the claim back", "",
          got=p.claim(), want=0)
    after = os.path.join(OUT, "after.img")
    m.flush(0, after)
    da, db = open(before, "rb").read(), open(after, "rb").read()
    check(da == db, "a disk cloned onto itself is unchanged",
          "every sector was read and written back, sector 0 last - a wrong "
          "buffer offset, a wrong LBA or a lost sector 0 all show here",
          got=db, want=da)

    # --- 4. Esc, at a prompt -------------------------------------------------
    open_clone(m, mo)
    check(p.edit() == 7, "a second clone arms", "", got=p.edit(), want=7)
    m.key("Escape")
    M.settle(m)
    check(p.edit() == 0, "Esc ends it where it stands", "", got=p.edit(), want=0)
    check(p.claim() == 0, "...the claim goes back", "", got=p.claim(), want=0)
    check(p.image() == 0, "...and CLONE.DRV with it",
          "fm_edit_end is the one place both happen (SPEC.md 22.21.2)",
          got=p.image(), want=0)
    check(p.claim() == 0 and p.edit() == 0,
          "...and the machine is still answering afterwards", "")

    # --- 5. Write Img..., and what it refuses (SPEC.md 18.99.8) -------------
    # THE ROUND TRIP IS NOT TESTED HERE AND CANNOT BE: an image of a 360KB
    # disk is 720 sectors and a 360KB volume's data area is 708, so on the
    # only floppy geometry MartyPC's machines have (`pcxt_2_360k_floppies`,
    # every os8088 config in ibm5150.toml) there is nowhere to put one. What
    # IS on this machine is every part of the command up to the transfer -
    # the menu item, the resident thunk, fdlg_open's fence, the completion
    # proc, the module load and the geometry table - and the refusal exercises
    # all of them and asserts a verdict rather than a screenshot.
    open_wimg(m, mo)
    fdlg = word(m, m.sym("fdlg_win"))
    check(fdlg != 0, "Write Img... opens the file dialog",
          "fm_c_wimg's whole body is fdlg_open_x (SPEC.md 22.21.5); 0 here "
          "means the item was greyed by fm_fmt_ok or the window was not live",
          got=fdlg, want="non-zero")
    check(p.edit() == 0, "...and arms NO mode of its own",
          "nothing is armed and nothing is claimed until the dialog commits: "
          "a cancel calls nothing back (SPEC.md 38.2), so anything armed here "
          "would be left with nothing to end it",
          got=p.edit(), want=0)
    check(p.claim() == 0, "...and claims nothing", "", got=p.claim(), want=0)
    check(p.image() == 0, "...and does not load CLONE.DRV either",
          "the completion proc loads it, for the same cancel (SPEC.md 22.21.5)",
          got=p.image(), want=0)

    # ...and the CANCEL, which is the half of this command SPEC.md 22.21.5
    # says it is built around: fdlg_close runs and there is no commit, so the
    # requester never hears. Nothing was armed and nothing was claimed BECAUSE
    # of that - a mode or an image taken on this side would be left with
    # nothing to end it. The four reads below are the same four as above, and
    # that is the point: cancelling must return the machine to exactly what
    # opening the dialog left.
    was = toast(m)
    m.key("Escape")
    M.settle(m)
    check(word(m, m.sym("fdlg_win")) == 0, "Esc closes it",
          "SPEC.md 38.2: fdlg_close, and [fdlg_win] is the modal latch",
          got=word(m, m.sym("fdlg_win")), want=0)
    check(p.edit() == 0, "...leaving no mode armed", "", got=p.edit(), want=0)
    check(p.claim() == 0, "...no claim outstanding", "", got=p.claim(), want=0)
    check(p.image() == 0, "...and CLONE.DRV not resident",
          "a cancel calls nothing back, so an image loaded before the dialog "
          "would never be dropped (SPEC.md 22.21.5)",
          got=p.image(), want=0)
    check(toast(m) == was, "...and says nothing at all",
          "there is no verdict for a cancel: the user changed their mind and "
          "the machine has not been anywhere", got=toast(m), want=was)

done("diskclone")
