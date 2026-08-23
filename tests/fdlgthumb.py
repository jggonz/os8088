#!/usr/bin/env python3
"""SPEC.md 13.10.5: the Standard File dialog's thumb is dragged too.

    python3 tests/fdlgthumb.py [machine]

A registered soak row, like tests/fmthumb.py.

The dialog is the SECOND consumer, and it is not a copy of the first: it has
no incremental scroll at all (every change to fdlg_scrl repaints the whole
list through fdlg_draw_list), a thumb press here used to do NOTHING rather
than page, and it is MODAL - so the gesture record it shares with the Disk
window can only be reached through fdlg_grab.

  A  a press on the thumb no longer does nothing   (the gesture goes live)
  B  the thumb moves with the hand                 (the block's own pos)
  C  ...and the VIEW does not, at rate 0           (fdlg_scrl unchanged)
  D  x is never read (13.10.5.2)
  E  the release commits                           (fdlg_scrl = the pos)
  F  the two bars do not share a gesture (13.10.5.10) - the drag record names
     the DIALOG's bar, not whatever was filled into os88ui_ksb last

The route to a dialog is tests/fdlgup.py's, because a package that opens one
is the only way to get one on screen; the listing is this test's own disk, so
"six rows fit" has more than six things to show.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, "tools")
import os88marty as M
from os88mouse import Mouse
from os88geom import WIN_SIZE, MAX_WIN

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
MACHINE = ARGS[0] if ARGS else "os8088_5150_cga_gla"
RATE = int(next((a.split("=")[1] for a in sys.argv[1:]
                 if a.startswith("--rate=")), "0"))
W_FLAGS, W_X, W_Y, W_W, W_H, W_TITLE = 0, 2, 4, 6, 8, 10
SBCELL, SBMINH = 10, 8

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o=0):
    return b[o] | (b[o + 1] << 8)


def wins(m):
    blob = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    out = []
    for i in range(MAX_WIN):
        r = blob[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if _u16(r, W_FLAGS) & 2:
            out.append(tuple(_u16(r, o) for o in (W_X, W_Y, W_W, W_H, W_TITLE)))
    return out


def titled(m, sym):
    want = m.sym(sym) - (M.KERNEL_SEG << 4)
    return next((w for w in wins(m) if w[4] == want), None)


def dlg(m):
    return titled(m, "fdlg_s_topen") or titled(m, "fdlg_s_tsave")


def drive_y(m, n=1):
    step = _u16(m.read(m.sym("desk_zstep"), 2))
    h1 = _u16(m.read(m.sym("desk_zh1"), 2))
    return 32 + n * step + h1 // 2


def open_dialog(m, mo):
    """muptest's window two puts one up (tests/fdlgup.py's route)."""
    vw = _u16(m.read(m.sym("vid_w"), 2))
    mo.dblclick(vw - 40, drive_y(m))
    M.settle(m)
    d = wins(m)[-1]
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


def ksb(m):
    b = m.read(m.sym("os88ui_ksb"), 14)
    return [_u16(b, i * 2) for i in range(7)]


def thumb(blk):
    x1, y1, x2, y2, total, fit, pos = blk
    if total <= fit:
        return None
    top = y1 + SBCELL + 1
    th = (y2 - SBCELL - 1) - top + 1
    if th < SBMINH:
        return None
    h = max((fit * th) // total, SBMINH)
    t = top + (pos * th) // total
    return min(t, top + th - h), h


def scrl(m):
    return _u16(m.read(m.sym("fdlg_scrl"), 2))


def dragpos(m):
    return _u16(m.read(m.sym("os88ui_sbd_pos"), 2))


def dragon(m):
    return m.read(m.sym("os88ui_sbd_on"), 1)[0]


def dragid(m):
    b = m.read(m.sym("os88ui_sbd_id"), 4)
    return _u16(b, 0), _u16(b, 2)


# A dialog listing with more than FD_ROWS in it. muptest's own disk carries one
# package and nothing else, so the bar it draws has no thumb at all.
#
# **NOT os88fixture.need(), AND THAT IS NOT A PREFERENCE.** `need` runs `make`
# for its target, and the Makefile's VIDSTAMP rule removes build/kernel.bin
# whenever the KNOB SET differs from the one that built it - deliberately, so
# that a knob change cannot boot the previous configuration. A knob gate calling
# `need` therefore deletes the very kernel it is about to test, from a make that
# then reports "up to date". So the fixture is built here, with the same two
# tools the Makefile would have used and no `make` at all.
DISK = "build/fdthumb.img"
if not os.path.exists(DISK):
    os.makedirs("build/fdthumbsrc", exist_ok=True)
    # Z-NAMED, so MUPTEST.O88 is row 0 of the Disk window however the listing
    # is ordered - the route below double-clicks row 0 to launch it, and with
    # plainly-named fillers it launched a text editor on FILE00.TXT instead
    # and the dialog never appeared.
    if not os.path.exists("build/muptest.o88"):
        subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                               "-o", "build/muptest.bin",
                               "tests/muptest/muptest.asm"])
        subprocess.check_call([sys.executable, "tools/os88pkg.py",
                               "build/muptest.bin", "-o", "build/muptest.o88"])
    names = ["build/muptest.o88"]
    for i in range(24):
        n = "build/fdthumbsrc/ZFILE%02d.TXT" % i
        open(n, "w").write("row %d\n" % i)
        names.append(n)
    subprocess.check_call([sys.executable, "tools/os88disk.py",
                           "-o", DISK, "--size", "360"] + names)

with M.launch("build/os8088-360.img", apps=DISK, machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    mono = m.video()["type"] in ("cga", "mda", "herc")
    print(f"== {MACHINE} : the DIALOG's thumb drag (SPEC.md 13.10.5) ==")

    d = open_dialog(m, mo)
    check("a Standard File dialog is up", d is not None, f"{d}")
    if not d:
        sys.exit("no dialog")

    blk = ksb(m)
    t = thumb(blk)
    check("...with a scrollable listing", t is not None,
          f"(bar {blk[:4]}, total {blk[4]}, fit {blk[5]}, pos {blk[6]})")
    if not t:
        sys.exit("nothing to scroll")
    top, h = t
    x1, y1, x2, y2, total, fit, _ = blk
    cx = (x1 + x2) // 2
    print(f"  thumb: top {top} h {h}; total {total} fit {fit}")

    # --- A: the press takes it (it used to do nothing at all) --------------
    was = scrl(m)
    mo.to(cx, top + h // 2)
    mo._edge(True)
    time.sleep(0.8)
    check("a press on the thumb starts a gesture", dragon(m) == 1)
    check("...and scrolls nothing yet", scrl(m) == was,
          f"(fdlg_scrl {scrl(m)}, was {was})")

    # --- F: it is the DIALOG's bar that was taken -------------------------
    check("the gesture names the dialog's bar (13.10.5.10)",
          dragid(m) == (x1, y1), f"({dragid(m)} vs {(x1, y1)})")

    # --- B/C: drag DOWN ----------------------------------------------------
    target = y2 - SBCELL - 1 - h // 2
    mo.to(cx, target, l=True)
    time.sleep(1.2)
    after = dragpos(m)
    t2 = thumb(ksb(m))
    check("...the element tracked it", after > 0, f"(pos {after})")
    check("the thumb moved with the hand",
          t2 is not None and t2[0] > top + 2,
          f"(top {top} -> {t2 and t2[0]}, block pos {ksb(m)[6]})")
    if RATE == 0:
        check("...and the VIEW did not, at rate 0", scrl(m) == was,
              f"(fdlg_scrl {scrl(m)}, was {was})")
    else:
        check(f"...and the VIEW followed, at rate {RATE}", scrl(m) != was,
              f"(fdlg_scrl {scrl(m)}, was {was})")

    # --- D: x is never read -------------------------------------------------
    mo.to(cx - 150, target, l=True)
    time.sleep(1.2)
    check("x is never read: 150px off the bar is the same pos",
          dragpos(m) == after, f"({dragpos(m)} vs {after})")

    # --- E: the release commits --------------------------------------------
    want = dragpos(m)
    mo._edge(False)
    M.settle(m)
    check("the release commits the dragged pos", scrl(m) == want,
          f"(fdlg_scrl {scrl(m)}, wanted {want})")
    check("...and the gesture is over", dragon(m) == 0)
    check("...the bar's thumb agrees with the view", ksb(m)[6] == want,
          f"(block pos {ksb(m)[6]})")
    check("...and the dialog is still up", dlg(m) is not None)

print("FAILED: " + ", ".join(fails) if fails else "all passed")
sys.exit(1 if fails else 0)
