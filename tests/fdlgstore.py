#!/usr/bin/env python3
"""THE FILE DIALOG LISTS INTO ITS OWN STORE (docs/plans/LISTING-HOME-PLAN.md).

    python3 tests/fdlgstore.py [machine]

A listing has no home of its own any more: a mount writes where its CALLER
keeps a store, and a caller with nowhere for one gets a quiet mount.  The
Standard File dialog claims one at `fdlg_open` and frees it at `fdlg_close`,
which is what lets `disk_dir` leave `.lowbss` entirely.

**THE FAILURE IS SILENT AND THAT IS THE WHOLE REASON FOR THE ROW.**
`fdlg_vclaim` falls back to the floor listing when the claim is refused, and
a dialog reading the floor looks EXACTLY like a dialog reading its own store:
same rows, same icons, same pixels.  So every other `fdlg*` row stays green
with the feature doing nothing, and stays green after the floor is deleted
and the fallback becomes a blank list.

Four things, none of them visible on the glass:

  1  `[fdlg_vseg]` is 0 with no dialog up - the claim is TRANSIENT, so a
     machine sitting on the desktop pays nothing for it.
  2  with a dialog up it names a real claim, and `[dsk_dseg]` IS that claim -
     the mount was aimed at it rather than at `LOW_SEG`.
  3  the store actually holds the listing: entry 0's name, read out of the
     claim, is one of the names the dialog is showing.
  4  and it goes back.  After Cancel, `[fdlg_vseg]` is 0 again and so is
     `[dsk_dseg]` - a `.bss` word may not be left naming a freed block, and
     this one is MOVABLE on kern_big and PURGEABLE on kern_small.  ZERO and
     not `LOW_SEG`: there is no floor listing to point back AT, and zero is
     what makes the next mount with nobody asking a quiet one.

VERIFIED TO FAIL by forcing `fdlg_vclaim` to skip the claim: 1 stays green,
2 goes red naming 0000, and 3 goes red because there is nothing in the store
to read.
"""
import sys

sys.path.insert(0, "tools")
import os88marty as M                                       # noqa: E402
import os88sym                                              # noqa: E402
from os88mouse import Mouse                                 # noqa: E402
from os88fixture import need                                # noqa: E402
from os88geom import WIN_SIZE, MAX_WIN                      # noqa: E402

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
W_FLAGS, W_X, W_Y, W_W, W_H, W_TITLE = 0, 2, 4, 6, 8, 10

# fdlg.inc's button column, content-relative - tests/fdlgup.py's mirror, and
# mirrored here for its reason rather than imported: that file drives a whole
# session at import time.
FD_BX1, FD_BX2 = 224, 286
FD_BY0, FD_BY1, FD_BY2 = 20, 40, 60
FD_BH = 13

_EQ = os88sym.equates()


def _u16(b, o):
    return b[o] | (b[o + 1] << 8)


def wins(m):
    blob = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    out = []
    for i in range(MAX_WIN):
        r = blob[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if _u16(r, W_FLAGS) & 2:
            out.append(tuple(_u16(r, o) for o in (W_X, W_Y, W_W, W_H,
                                                  W_TITLE)))
    return out


def titled(m, sym):
    want = m.sym(sym) - (M.KERNEL_SEG << 4)     # W_TITLE is a NEAR offset
    for w in wins(m):
        if w[4] == want:
            return w
    return None


def dlg(m):
    return titled(m, "fdlg_s_topen") or titled(m, "fdlg_s_tsave")


def drive_y(m, n=1):
    step = int.from_bytes(m.read(m.sym("desk_zstep"), 2), "little")
    h1 = int.from_bytes(m.read(m.sym("desk_zh1"), 2), "little")
    return 32 + n * step + h1 // 2


def open_dialog(m, mo):
    """muptest's window two puts one up (tests/fdlgup.py's route)."""
    vw = int.from_bytes(m.read(m.sym("vid_w"), 2), "little")
    mo.dblclick(vw - 40, drive_y(m))
    M.settle(m)
    d = [w for w in wins(m)][-1]
    mo.click(d[0] + d[2] // 2, d[1] + 9)
    M.settle(m)
    mo.dblclick(d[0] + 40, d[1] + 18 + 30)
    M.settle(m)
    w = wins(m)[-1]
    cx, cy = w[0] + 1, w[1] + 18
    two = (cx + 100 + 31, cy + 40 + 9)
    mo.menu(two[0], two[1], two[0] + 2, two[1])
    M.settle(m)
    return dlg(m)


def btn_pt(d, which):
    """The centre of button `which` (1..3), in SCREEN coordinates."""
    cx, cy = d[0] + 1, d[1] + 18
    top = (FD_BY0, FD_BY1, FD_BY2)[which - 1]
    return cx + (FD_BX1 + FD_BX2) // 2, cy + top + FD_BH // 2


def fail(msg):
    print("fdlgstore: FAIL: " + msg)
    sys.exit(1)


def w16(m, name):
    return int.from_bytes(m.read(m.sym(name), 2), "little")


need("build/muptest.img")

with M.launch("build/os8088-360.img", apps="build/muptest.img",
              machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    low = _EQ["LOW_SEG"]

    # --- 1. nothing up, nothing claimed ---------------------------------
    if w16(m, "fdlg_vseg") != 0:
        fail("[fdlg_vseg] is %04X on a desktop with no dialog on it - the "
             "store is meant to be TRANSIENT with the dialog, which is the "
             "whole of why it costs no resident byte"
             % w16(m, "fdlg_vseg"))

    d = open_dialog(m, mo)
    if not d:
        fail("no dialog came up - nothing below this can be measured")

    # --- 2. it claimed one, and the mount was aimed at it ---------------
    vseg = w16(m, "fdlg_vseg")
    dseg = w16(m, "dsk_dseg")
    doff = w16(m, "dsk_doff")
    print("fdlgstore: dialog up: fdlg_vseg=%04X dsk_dseg=%04X:%04X (LOW_SEG "
          "is %04X)" % (vseg, dseg, doff, low))
    if vseg == 0:
        fail("the dialog is up and [fdlg_vseg] is 0 - the claim was REFUSED "
             "and it fell back to the floor listing. On a machine with this "
             "much free heap that is the feature not working, not memory "
             "pressure")
    if dseg != vseg or doff != 0:
        fail("the dialog holds a store at %04X and the mount was aimed at "
             "%04X:%04X - dsk_dest_x was not called, or was called with the "
             "floor. The dialog then lists into a buffer it does not read "
             "and reads one nothing wrote" % (vseg, dseg, doff))

    # --- 3. and the listing is IN it ------------------------------------
    n = w16(m, "disk_nfiles")
    if n == 0:
        fail("the dialog is up and [disk_nfiles] is 0 - the mount published "
             "no listing at all, so there is nothing in the store to check")
    stride = _EQ["DSK_DE_STRIDE"]
    raw = m.read((vseg << 4), stride)
    name = raw.split(b"\0")[0].decode("latin1", "replace")
    print("fdlgstore: %d entries, entry 0 in the store is %r" % (n, name))
    if not name or not name[0].isprintable() or name[0] == " ":
        fail("entry 0 of the store reads %r - the mount aimed here and wrote "
             "nothing, so the dialog is drawing a listing out of an empty "
             "claim" % name)

    # --- 4. and it goes back --------------------------------------------
    mo.click(*btn_pt(d, 2))         # Cancel
    M.settle(m)
    if dlg(m):
        fail("Cancel did not take the dialog down - the teardown is what "
             "frees the store, so nothing below this is measurable")
    vseg2 = w16(m, "fdlg_vseg")
    dseg2 = w16(m, "dsk_dseg")
    print("fdlgstore: dialog gone: fdlg_vseg=%04X dsk_dseg=%04X"
          % (vseg2, dseg2))
    if vseg2 != 0:
        fail("[fdlg_vseg] is still %04X after the dialog closed - the claim "
             "leaks, and an Open/Cancel loop eats the heap 2KB at a time"
             % vseg2)
    if dseg2 != 0:
        fail("[dsk_dseg] still names %04X after the dialog closed - a .bss "
             "word is left naming a FREED block, and the next loud mount "
             "writes a listing into whatever took its place. It must be "
             "ZERO: there is no floor listing to point back at, and zero is "
             "what makes a mount nobody asked for a QUIET one" % dseg2)

print("fdlgstore: ok")
