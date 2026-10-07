#!/usr/bin/env python3
"""A desktop ZONE damages the windows over it, and only those (SPEC.md 11.91)

    make && python3 tests/zonedmg.py [--machine os8088_5150_herc_gla]

`wm_paint_dmg` redraws every desktop zone its damage touches WHOLE and
unclipped - ground, picture, caption - so a window sitting over such a zone
has had its own pixels painted over and must be redrawn. That used to be
reached by FOLDING the zone into the damage rect, which over-reached: the
rect is one box, so every window the box reached was owed it, and a cover
dragged past the corner of a cell owed the Task Manager beside it two rows
of graph (tests/tmgraph.py's BAR leg). The zones now grow a box of their
own, `[wm_dmg_zb]`, and `wm_dmg_wins` asks it PER WINDOW, as it asks the
dock strip's.

This row is the OTHER half of that rule, the half nothing gated: a window
over a zone the damage reached must still be redrawn when the damage itself
never touches the window.

  W   B:'s Disk window, shrunk to its minimum and put so its BOTTOM-RIGHT
      corner hangs over the top-left of B:'s desktop cell.
  V   A:'s Disk window, put BELOW W - its frame and shadow clear of W's by a
      few rows - with its top edge across the bottom of the same cell.
  then V is CLOSED. Its frame is the damage: it reaches the cell and not W.
      The cell is drawn whole, over W's corner; W must be put back.

IT COMPARES PIXELS, because the pixels are the subject: W's frame and
content where they overlap the cell, read off the Hercules framebuffer once
the damage pass has settled, against the same rect after a forced whole
repaint (`[cp_dirty]`, `wm_paint_all`, which draws the desktop and then
every window over it whole and so cannot get it wrong). A Disk window's
listing does not change by itself, and the pointer is parked off the rect.

**SINCE SPEC.md 11.91.6 kern_big draws the cell only where it shows**, so
the cell no longer reaches W at all and the per-window zone test is that
kernel's OVERFLOW fallback: this row still asserts the pixels, and
tests/deskclip.py asserts that W is not even redrawn. The per-window test is
still every partly visible cell's answer on kern_small, and the break below
is that kernel's (and was kern_big's before 11.91.6):

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1): put `clc` in front of
the `jc .mset` after `wm_dmg_wins`' zone test at `.mnodmg` and the overlap
comes back as B:'s cell: 348 of the 840 pixels differ, measured. On kern_big
today the break is tests/deskclip.py's - the region a cell is drawn into
(wm_dmg_gray's own since SPEC.md 26.9.9) without its frame subtraction -
which read 313 pixels in this layout when that region was `wm_zone_r`'s
(deskclip's `close`, measured).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui             # noqa: E402
import os88geom as geom   # noqa: E402
import os88sym            # noqa: E402

MACHINE = "os8088_5150_herc_gla"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

EQ = os88sym.equates()
DESK_PX, DESK_CW = EQ["DESK_PX"], EQ["DESK_CW"]
DESK_ZOVER, DESK_ZY0 = EQ["DESK_ZOVER"], EQ["DESK_ZY0"]
PARK = (4, 4)                   # the menu bar's left end: clear of everything
fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def cell_rect(m, ordinal):
    """desk_cell_rect (SPEC.md 26.9): the cell plus the caption overhang."""
    rows = geom.word(m, "desk_rows")
    col, row = divmod(ordinal, rows)
    x = geom.word(m, "vid_desk_zx") - col * DESK_PX
    y = DESK_ZY0 + row * geom.word(m, "desk_zstep")
    return (x - DESK_ZOVER, y, x + DESK_CW + DESK_ZOVER - 1,
            y + geom.word(m, "desk_zh1"))


def occ(w):
    """A window's occupied rect, drop shadow included (wm_win_rect)."""
    return (w.x, w.y, w.x + w.w, w.y + w.h)


def isect(a, b):
    r = (max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3]))
    return r if r[0] <= r[2] and r[1] <= r[3] else None


def grab(m, r):
    _, _, rows = m.vram("herc")
    return [bytes(rows[y][r[0]:r[2] + 1]) for y in range(r[1], r[3] + 1)]


with os88ui.boot("build/os8088-360.img", apps="build/apps360.img",
                 machine=MACHINE) as ui:
    m = ui.m
    cell = cell_rect(m, geom.drive_ordinal(m, "B"))
    print("cell    : B: at %d..%d x %d..%d" % (cell[0], cell[2], cell[1], cell[3]))
    v = ui.open_drive("A")
    w = ui.open_drive("B")

    # --- W: B:'s window at its minimum, its corner over the cell's top-left
    # No os88ui verb resizes, so this is a hand-rolled drag on the grow box
    # (the frame's bottom-right corner) - confirmed off the record below.
    ui.raise_window(w)
    w = ui._refresh(w)
    gx, gy = w.x + w.w - 4, w.y + w.h - 4
    ui.mo.drag(gx, gy, max(20, gx - 200), max(30, gy - 150))
    w = ui._refresh(w)
    if not check(w.h < 120, "W shrank to %dx%d by its grow box" % (w.w, w.h)):
        sys.exit(1)
    # y 20 is the window manager's ceiling on this screen (move_window says
    # where it landed if that ever moves); the SETUP checks below are what
    # decide whether the geometry still makes the case
    w = ui.move_window(w, cell[0] + 40 - (w.w - 1), 20)

    # --- V: A:'s window BELOW W, its top edge across the cell's bottom ------
    v = ui.move_window(v, geom.word(m, "vid_w") - v.w - 10, w.y + w.h + 6)
    ui.mo.to(*PARK)
    ui.settle()

    ov = isect(occ(w), cell)
    print("W       : %dx%d at (%d,%d); over the cell at %r" % (w.w, w.h, w.x,
                                                              w.y, ov))
    print("V       : %dx%d at (%d,%d)" % (v.w, v.h, v.x, v.y))
    ok = check(ov is not None, "SETUP: W overlaps B:'s cell")
    ok &= check(isect(occ(v), cell) is not None, "SETUP: V overlaps B:'s cell")
    ok &= check(isect(occ(v), occ(w)) is None,
                "SETUP: V's frame and shadow are clear of W's")
    if not ok:
        sys.exit(1)

    # --- the damage: V goes. It reaches the cell and does not reach W -------
    ui.close(v)
    ui.mo.to(*PARK)
    ui.settle()
    got = grab(m, ov)

    m.write(m.sym("cp_dirty"), b"\x01")     # a whole repaint: the reference
    ui.settle()
    ref = grab(m, ov)
    bad = sum(1 for ra, rb in zip(got, ref) for p, q in zip(ra, rb) if p != q)
    check(bad == 0,
          "W over B:'s cell after V's close matches a whole repaint - %d of %d "
          "pixels differ%s" % (bad, (ov[2] - ov[0] + 1) * (ov[3] - ov[1] + 1),
                               "" if not bad else
                               " (the zone was drawn over W and W was not "
                               "marked: SPEC.md 11.91's per-window zone test)"))

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  a window over a damaged zone is redrawn, and the zone stays under it")
