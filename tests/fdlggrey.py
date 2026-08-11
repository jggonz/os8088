#!/usr/bin/env python3
"""The file dialog's default button: REDRAWN IN PLACE must equal FRESHLY PAINTED.

font_char is transparent, so a button redrawn without an erase ors its new
caption onto the old one - and a greyed caption then comes out identical to a
live one while the frame around it dithers correctly, which reads as SPEC.md
47 rule 1 being broken when the mechanism is fine.

Both states are checked, because the fix must not break the one that worked:
  live    -> refused    (backspace the name box empty)
  refused -> live       (type a character)
and each is compared against the same state reached by a FULL repaint, which
white-fills the pane first.
"""
import sys, struct
sys.path.insert(0, "tools")
import os88marty as M
from os88mouse import Mouse


def drive_y(m, n=1):
    """The middle of drive zone n, read from the KERNEL rather than assumed.

    The zone pitch is [desk_zstep] and SPEC.md 26.4 made it adapter-dependent
    - 60 rows with the 32-row icon, 34 with the CGA's short one - so a
    hard-coded y that worked on every adapter for months silently started
    landing one zone out. DESK_ZY0 is 32 and [desk_zh1] is the zone's height
    less one, both from desk.inc.
    """
    step = int.from_bytes(m.read(m.sym("desk_zstep"), 2), "little")
    h1 = int.from_bytes(m.read(m.sym("desk_zh1"), 2), "little")
    return 32 + n * step + h1 // 2


MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
WIN_SIZE, MAX_WIN = 28, 12
FD_BX1, FD_BX2, FD_BY0, FD_BH, TITLE_H = 224, 286, 20, 13, 18
fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def wins(m):
    blob = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    out = []
    for i in range(MAX_WIN):
        r = blob[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if struct.unpack_from("<H", r, 0)[0] & 2:
            out.append(tuple(struct.unpack_from("<H", r, o)[0]
                             for o in (2, 4, 6, 8, 10)))
    return out


def mup(m):
    return next((w for w in wins(m) if w[2] == 240 and w[3] == 100), None)


def dlg(m):
    return next((w for w in wins(m) if w[2] == 300), None)


def disk_win(m):
    return next((w for w in wins(m)
                 if (w[2], w[3]) not in ((240, 100), (300, 170), (300, 155))),
                None)


def btn(m):
    """The default button's pixels, as a tuple so two states compare exactly."""
    fd = dlg(m)
    ox, oy = fd[0] + 1, fd[1] + TITLE_H
    W, H, px = m.fbuf()
    out = []
    for y in range(oy + FD_BY0, oy + FD_BY0 + FD_BH + 1):
        for x in range(ox + FD_BX1, ox + FD_BX2 + 1):
            out.append(px[(y * W + x) * 3] < 0x40)
    return tuple(out)


def repaint(m, mo):
    """Force a full fdlg_paint by nudging the window, and put it back."""
    fd = dlg(m)
    x, y = fd[0] + fd[2] // 2, fd[1] + 9
    mo.menu(x, y, x + 8, y)
    M.settle(m)
    fd = dlg(m)
    x, y = fd[0] + fd[2] // 2, fd[1] + 9
    mo.menu(x, y, x - 8, y)
    M.settle(m)


with M.launch("build/os8088-360.img", apps="build/muptest.img",
              machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    vw = int.from_bytes(m.read(m.sym("vid_w"), 2), "little")
    print(f"== {MACHINE} : fdlg default button, redraw vs repaint ==")

    mo.dblclick(vw - 40, drive_y(m))
    M.settle(m)
    d = disk_win(m)
    mo.click(d[0] + d[2] // 2, d[1] + 9)
    M.settle(m)
    mo.dblclick(d[0] + 40, d[1] + 18 + 30)
    M.settle(m)
    w = mup(m)
    cx, cy = w[0] + 1, w[1] + 18
    two = (cx + 100 + 31, cy + 40 + 9)
    mo.menu(two[0], two[1], two[0] + 2, two[1])
    M.settle(m)
    check("dialog up", dlg(m) is not None)
    if not dlg(m):
        sys.exit("no dialog")

    for _ in range(16):
        m.key("Backspace")
    M.settle(m)
    nlen = m.read(m.sym("fdlg_nlen"), 1)[0]
    check("name box empty", nlen == 0, f"(fdlg_nlen={nlen})")
    grey_redrawn = btn(m)
    repaint(m, mo)
    grey_fresh = btn(m)
    d0 = sum(a != b for a, b in zip(grey_redrawn, grey_fresh))
    check("REFUSED: redrawn == freshly painted", d0 == 0, f"({d0} px differ)")
    check("REFUSED: the caption is DITHERED, not solid",
          sum(grey_fresh) < 130, f"(ink {sum(grey_fresh)})")

    m.key("KeyA")
    M.settle(m)
    live_redrawn = btn(m)
    repaint(m, mo)
    live_fresh = btn(m)
    d1 = sum(a != b for a, b in zip(live_redrawn, live_fresh))
    check("LIVE: redrawn == freshly painted", d1 == 0, f"({d1} px differ)")
    check("LIVE is not the same picture as REFUSED",
          live_fresh != grey_fresh,
          f"(ink {sum(live_fresh)} vs {sum(grey_fresh)})")

print()
if fails:
    print("FAILURES:"); [print("  " + f) for f in fails]; sys.exit(1)
print("all pass")
