#!/usr/bin/env python3
"""A CAPTION BESIDE A COVERING WINDOW'S CORNER KEEPS ALL ITS ROWS (SPEC.md
11.3.4.3).

    make && python3 tests/clipgrow.py

A Disk window is dragged over the Task Manager so its TOP edge crosses the
Task Manager's caption and one of its SIDE edges runs through the middle of
it - the field report's two photographs, once from the right and once from
the left. The caption cells beside the Disk window are wholly visible, but
`wm_clip_split` cuts the region in horizontal strips, so each of those cells
is in TWO fragments: the full-width strip above the Disk window's top and the
side piece beside it. `wm_clip_rows` used to answer the taller of the two, and
the rows the other one held were dropped - the caption lost its bottom along
its whole length, beside the window as well as under it.

The assertion is the whole title strip against itself before the Disk window
covered it: every pixel the Disk window does not occupy (frame and shadow)
must be the pixel it was - except in the one cell each side edge runs
through, on the rows below the window's top. That cell's visible part is an
L, which one row range and one column mask cannot express, so it keeps
11.3.4's accepted under-draw; the row prints how many pixels that was.

On the kernel before 11.3.4.3 it FAILS on both legs, at 31 and 44 caption
pixels - the defect as reported, the caption's bottom rows missing beside
the window - and passes after with only the corner cell's 4 and 2.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88ui                                               # noqa: E402
import os88marty                                            # noqa: E402

TITLE_H = 18
CAP_Y = 5               # the caption's first row in the frame (wm_draw_title)


def strip(m, w):
    """The Task Manager's title strip: {(x, y): bit}, interior only."""
    _, _, rows = m.vram()
    return {(x, y): rows[y][x]
            for y in range(w.y + 2, w.y + TITLE_H - 2)
            for x in range(w.x + 1, w.x + w.w - 1)}


def leg(ui, name, tm, dk, x, y, ref):
    m = ui.m
    ui.move_window(dk, x, y)
    dk = ui._as_win(dk.i)
    os88marty.guest_sleep(m, 1.0)       # the damage repaint under it
    now = strip(m, tm)
    # The Disk window OCCUPIES (x, y)..(x+w, y+h) inclusive - its shadow.
    x1, y1, x2, y2 = dk.x, dk.y, dk.x + dk.w, dk.y + dk.h

    def under(px, py):
        return x1 <= px <= x2 and y1 <= py <= y2

    def corner(px, py):
        # a pixel of a CELL the window's side edge runs through, on a row the
        # window's top edge has reached: that cell's visible part is an L,
        # which one row range and one column mask cannot say, and 11.3.4's
        # under-draw is the accepted answer for it. Within 7 of either side
        # covers it wherever the caption's cells fall
        return py >= y1 and (x1 - 7 <= px < x1 or x2 < px <= x2 + 7)
    diff = [p for p, v in ref.items() if v != now[p] and not under(*p)]
    lost = [p for p in diff if not corner(*p)]
    seen = sum(1 for p in ref if not under(*p))
    print("   %s: Disk window at (%d, %d) over the caption: %d of %d visible "
          "title-strip pixels differ from the uncovered strip, %d of them in "
          "the cell the window's corner cuts (accepted, SPEC.md 11.3.4.3)"
          % (name, dk.x, dk.y, len(diff), seen, len(diff) - len(lost)))
    for p in sorted(lost)[:8]:
        print("      %r was %d, is %d" % (p, ref[p], now[p]))
    return lost, dk


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--kernel", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args()
    bad = []
    with os88ui.boot(a.kernel, apps=a.apps,
                     machine=os88marty.machine(a.machine)) as ui:
        m = ui.m
        dk = ui.open_drive("B")
        tm = ui._as_win(ui.path("A:/SYSTEM/TASKMGR.O88"))
        # Out of the way first, below the Task Manager's title bar, and raised:
        # the reference is the caption as an INACTIVE window draws it, which
        # is what it is once the Disk window covers it
        ui.move_window(dk, 8, tm.y + TITLE_H + 40)
        dk = ui._as_win(dk.i)
        tm = ui._as_win(tm.i)
        ref = strip(m, tm)
        ink = [p for p, v in ref.items() if v]
        if not ink:
            bad.append("the title strip read blank, so the row compared "
                       "nothing")
        # The caption's middle, and a top edge five rows into it: the strip
        # above holds 5 of the cell's 8 rows and the side piece 3, so the
        # strip WINS and the side piece's 3 are the rows that were dropped
        mid = tm.x + tm.w // 2
        top = tm.y + CAP_Y + 5
        print("   Task Manager at (%d, %d) %dx%d" % (tm.x, tm.y, tm.w, tm.h))
        lost, dk = leg(ui, "from the right", tm, dk, mid, top, ref)
        if lost:
            bad.append("from the right: %d caption pixels beside the Disk "
                       "window were not drawn, first at %r (SPEC.md "
                       "11.3.4.3)" % (len(lost), sorted(lost)[0]))
        lost, dk = leg(ui, "from the left", tm, dk, mid - dk.w, top, ref)
        if lost:
            bad.append("from the left: %d caption pixels beside the Disk "
                       "window were not drawn, first at %r (SPEC.md "
                       "11.3.4.3)" % (len(lost), sorted(lost)[0]))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
