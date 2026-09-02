#!/usr/bin/env python3
"""SPEC.md 13.8.3: the Standard File dialog's buttons fire on the RELEASE.

    python3 tests/fdlgup.py [machine]

Before this, a press on `Cancel` fired Cancel - there was no way to think
better of a mis-aimed press, which is the whole of what SPEC.md 13.6 says a
control with no safe prefix action wants the release for.

FIVE CASES, and the shape of the file is muptest's (tests/mouseup.py): the
signal is read out of the WINDOW TABLE wherever it can be, so a pass is a
memory read rather than a picture.

  A  press Cancel, slide OFF it, release      -> the dialog is STILL UP
  B  ...and the button came back up while held (the tracking edge, 13.8.2)
  C  press Cancel, release ON it              -> the dialog is GONE
  D  press Cancel, release on ANOTHER button  -> still up, nothing fired
  E  a press draws the button DOWN            -> the pane changed under it

C is the one that says the feature did not eat the feature it decorates: a
build that had simply stopped dispatching would pass A, B and D.

The dialog is reached the way tests/fdlggrey.py reaches it - muptest's second
window puts one up - because a package that opens a Standard File dialog is
the only way to get one on screen.
"""
import sys
import time

sys.path.insert(0, "tools")
import os88marty as M
from os88mouse import Mouse
from os88fixture import need
from os88geom import WIN_SIZE, MAX_WIN

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
W_FLAGS, W_X, W_Y, W_W, W_H, W_TITLE = 0, 2, 4, 6, 8, 10

# fdlg.inc's button column, content-relative. Mirrored so that a change to
# either side has to be made twice deliberately - which is what a gate is for.
FD_BX1, FD_BX2 = 224, 286
FD_BY0, FD_BY1, FD_BY2 = 20, 40, 60
FD_BH = 13

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


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


def drive_y(m, n=1):
    step = int.from_bytes(m.read(m.sym("desk_zstep"), 2), "little")
    h1 = int.from_bytes(m.read(m.sym("desk_zh1"), 2), "little")
    return 32 + n * step + h1 // 2


def dlg(m):
    return titled(m, "fdlg_s_topen") or titled(m, "fdlg_s_tsave")


def btn_pt(d, which):
    """The centre of button `which` (1..3), in SCREEN coordinates."""
    cx, cy = d[0] + 1, d[1] + 18
    top = (FD_BY0, FD_BY1, FD_BY2)[which - 1]
    return cx + (FD_BX1 + FD_BX2) // 2, cy + top + FD_BH // 2


def lit(m, mono, rect):
    if mono:
        w, h, rows = m.vram()
        return sum(rows[y][x] for y in range(rect[1], rect[3] + 1)
                   for x in range(rect[0], rect[2] + 1))
    w, h, px = m.fbuf()
    n = 0
    for y in range(rect[1], rect[3] + 1):
        for x in range(rect[0], rect[2] + 1):
            i = (y * w + x) * 3
            if px[i] or px[i + 1] or px[i + 2]:
                n += 1
    return n


def open_dialog(m, mo):
    """muptest's window two puts one up (tests/fdlggrey.py's route)."""
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


need("build/muptest.img")          # `all` builds nothing under tests/

with M.launch("build/os8088-360.img", apps="build/muptest.img",
              machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    mono = m.video()["type"] in ("cga", "mda", "herc")
    print(f"== {MACHINE} : fdlg fires on the release (SPEC.md 13.8.3) ==")

    d = open_dialog(m, mo)
    check("dialog up", d is not None)
    if not d:
        sys.exit("no dialog")

    cancel = btn_pt(d, 2)
    rect = (d[0] + 1 + FD_BX1, d[1] + 18 + FD_BY1,
            d[0] + 1 + FD_BX2, d[1] + 18 + FD_BY1 + FD_BH)

    # --- E: the press DRAWS it down --------------------------------------
    mo.to(cancel[0], cancel[1] - 40)        # park off the column first
    time.sleep(0.8)
    up = lit(m, mono, rect)
    mo.to(*cancel)
    mo._edge(True)
    time.sleep(0.8)
    down = lit(m, mono, rect)
    check("a press draws Cancel DOWN", down < up - 100,
          f"({down} lit held, {up} upright)")

    # --- B: it comes back UP while still held, off the button -------------
    mo.to(cancel[0] - 90, cancel[1], l=True)        # l=True: STILL HELD
    time.sleep(1.2)
    off = lit(m, mono, rect)
    check("...and back UP when the pointer slides off it", off == up,
          f"({off} lit, upright is {up})")

    # --- A: the release lands off it: NOTHING fires -----------------------
    mo._edge(False)
    M.settle(m)
    check("a slide-off release does NOT close the dialog", dlg(m) is not None)

    # --- D: released on a DIFFERENT button: nothing fires -----------------
    d = dlg(m)
    mo.to(*btn_pt(d, 2))
    mo._edge(True)
    time.sleep(0.6)
    mo.to(*btn_pt(d, 3), l=True)            # slide onto Drive, still held
    time.sleep(1.0)
    mo._edge(False)
    M.settle(m)
    check("released on ANOTHER button, nothing fires", dlg(m) is not None)

    # --- C: press AND release on Cancel: it closes ------------------------
    # The one that says the feature did not eat the feature it decorates.
    d = dlg(m)
    mo.to(*btn_pt(d, 2))
    mo._edge(True)
    time.sleep(0.6)
    mo._edge(False)
    M.settle(m)
    check("press-and-release ON Cancel closes the dialog", dlg(m) is None)

print()
if fails:
    print(f"{len(fails)} FAILED: {', '.join(fails)}")
    sys.exit(1)
print("all pass")
