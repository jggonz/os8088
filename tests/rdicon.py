#!/usr/bin/env python3
"""Icons on a redirected volume: the document pass AND the harvest (SPEC.md
62.9.2.1, 62.9.2.2).

    python3 tests/rdicon.py [machine]

WHAT IT GUARDS is the second half of the mount's icon work. `disk_mount`'s
redirected tail (§62.9.2.1) runs pass 4a' - the cache-only classification -
and then used to `jmp short .done`, PAST §54.3's pass 4b. The reason written
down for that was *"a redirected volume composes no document bodies"*, and it
does not survive reading `assoc_docicon`: pass 4b composes from the
ASSOCIATION's glyph, which is machine-wide and warm out of the boot volume's
`ASSOC.DAT`, and it does no I/O at all. So a `.TXT` on the RAM disk had
exactly the same claim on a Note Pad page as a `.TXT` on a floppy and was
handed the generic diamond instead - the field's *"RAM disks almost never
show an assoc icon, even when the cache is there and has it populated"*.

THE DISK IS `RAMSEED=1`'s, which is what that knob is for (§62.9.5.1): the
store comes up holding `DOCS/`, `MINES.O88` and two `.TXT` files with no copy
to drive and no file dialog in the way. The kernel is byte-identical to the
shipped one - RAMSEED reaches `RAMDISK.DRV` alone - so this is a private tree
for the DISK's sake and `os88sym` resolves against the same map either way.

THE FOUR CHECKS, and the second is what makes the other two mean anything:

  L  the volume listed at all - four entries, which is the seed
  F  the FOLDER's reference byte is live. That is pass 4a', which was never
     broken, so it says the listing, the store and the reference index are
     all working before anything else is blamed on a pass
  D  the DOCUMENT's reference byte is live - SPEC.md 62.9.2.1's missing 4b
  P  the PACKAGE's is live - SPEC.md 62.9.2.2's harvest. `MINES.O88` is in
     the seed and in no store this machine has warmed, so the only way to its
     icon is reading the header off the volume, which is what `FSCAP_LOCAL`
     tells the mount it may do. Before the bit it was the generic diamond
     however long you browsed

It reads REFERENCE BYTES out of the acting window's own cache rather than
judging pixels, which is `tests/icoshed.py`'s instrument and for its reason: a
composed page and a generic diamond are both "some ink in a 16x16 cell", and
the thing that actually changed is whether the entry names a row in the store.
"""
import struct
import sys

sys.path.insert(0, "tools")
import os88build
import os88marty as M
import os88ui
from os88mouse import Mouse
from os88geom import WIN_SIZE, MAX_WIN, FS_N, FS_VSEG, FS_IOFH, FV_ICOIX
from os88geom import ICO_R_NONE

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"

# ctrl.inc's geometry and page.inc's, mirrored for tests/rdmount.py's reason
CP_I0Y, CP_IROWH, CP_RX = 6, 14, 96
CP_DBY1, CP_DROWH, CP_IDRV = 20, 26, 2
RP_MNTX, RP_MNTW, RP_B0Y, RP_BH = 2, 64, 52, 16
DRVR_SZ, DRVR_SEG, RD_ROW = 16, 2, 3
TITLE_H = 18

# kernel/files.inc's state block. FS_N, FS_VSEG, FS_IOFH, FV_ICOIX and
# ICO_R_NONE are ASKED of tools/os88geom.py rather than typed: they are read
# by tests/icoshed.py too, and a second copy is exactly what t_mirror refuses
# (a copy that agrees is the seed of the next one that does not). FS_SIZE is
# per-arm and only bounds the read, so it stays local and generous.
FS_SIZE = 57

# the seed's root, in the order SPEC.md 19.4 sorts it
ROW_FOLDER, ROW_PKG, ROW_DOC = 0, 1, 2   # DOCS, MINES.O88,
                                         # NOTES.TXT, README.TXT
SEED_N = 4

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o=0):
    return b[o] | (b[o + 1] << 8)


def slot0(m):
    b = bytes(m.read(m.sym("fm_pool"), FS_SIZE))
    return (_u16(b, FS_N), _u16(b, FS_VSEG), b[FS_IOFH])


def refbyte(m, row):
    """The acting window's reference byte for entry `row` (SPEC.md 25.9)."""
    n, vseg, iofh = slot0(m)
    if not vseg or not iofh:
        return None
    return m.readseg(vseg, (iofh << 8) + FV_ICOIX + row, 1)[0]


def wins(m):
    b = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    return [tuple(_u16(b[i * WIN_SIZE:], o) for o in (2, 4, 6, 8))
            for i in range(MAX_WIN) if _u16(b[i * WIN_SIZE:], 0) & 2]


def quiet(m, s=15.0):
    try:
        M.settle(m, limit=s)
    except M.MartyError:
        pass


t = os88build.tree("RAMSEED=1").apply()
with os88ui.boot(t.img("os8088-360.img"), apps=t.img("apps360.img"),
                 machine=MACHINE) as ui:
    m = ui.m
    mo = Mouse(marty=m)
    print(f"== {MACHINE} : a document on a redirected volume has an icon ==")

    mo.menu(8, 8, 8, 40)                        # chip menu -> Control Panel
    quiet(m)
    cp = [w for w in wins(m) if w[2] >= 280 and w[3] >= 100]
    if not cp:
        sys.exit("no Control Panel")
    cp = cp[0]
    x0, y0 = cp[0] + 1, cp[1] + TITLE_H

    mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)     # Drivers
    quiet(m)
    mo.click(x0 + CP_RX + 40,                                   # tick Ram Disk
             y0 + CP_DBY1 + RD_ROW * CP_DROWH + CP_DROWH // 2)
    quiet(m, 25.0)
    if not _u16(m.read(m.sym("drv_tab") + RD_ROW * DRVR_SZ + DRVR_SEG, 2)):
        sys.exit("the RAM disk driver did not load")

    nst = m.read(m.sym("cp_nst"), 1)[0]         # its page, then Mount
    mo.click(x0 + 40, y0 + CP_I0Y + nst * CP_IROWH + 7)
    quiet(m, 25.0)
    mo.click(x0 + CP_RX + RP_MNTX + RP_MNTW // 2, y0 + RP_B0Y + RP_BH // 2)
    quiet(m, 25.0)
    mo.click(cp[0] + 8, cp[1] + 9)              # close the panel
    quiet(m, 20.0)

    # open the RAM disk. Its zone is the one the mount just added, so it is
    # resolved by NAME out of the guest's own tables rather than clicked at a
    # remembered coordinate (docs/WRITING-TESTS.md 6).
    ui.open_drive("D")
    quiet(m, 25.0)

    n, vseg, iofh = slot0(m)
    check("the RAM disk listed", n == SEED_N,
          "(%d entries, the seed is %d)" % (n, SEED_N))
    fref = refbyte(m, ROW_FOLDER)
    check("pass 4a' resolved the FOLDER", fref is not None
          and fref != ICO_R_NONE,
          "(reference %s)" % ("none" if fref is None else "%#04x" % fref))
    dref = refbyte(m, ROW_DOC)
    check("pass 4b composed the DOCUMENT", dref is not None
          and dref != ICO_R_NONE,
          "(reference %s - 0xFF is the blank the icon index is filled with)"
          % ("none" if dref is None else "%#04x" % dref))
    pref = refbyte(m, ROW_PKG)
    check("the harvest resolved the PACKAGE", pref is not None
          and pref != ICO_R_NONE,
          "(reference %s - FSCAP_LOCAL is what lets the mount read the "
          "header)" % ("none" if pref is None else "%#04x" % pref))

print()
if fails:
    print(f"{len(fails)} FAILED: {', '.join(fails)}")
    sys.exit(1)
print("all pass")
