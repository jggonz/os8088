#!/usr/bin/env python3
"""What does clicking into an FTPD Setup field cost? (SPEC.md 77.45)

    make && python3 tests/ftpdflick.py [--machine os8088_5150_cga_gla]

PERFORMANCE.md Part 3.1's instrument, pointed at the one gesture SPEC.md
77.44 left behind: a press that moves the CARET. The caret is a 1px bar in an
8px cell, so the honest cost of moving it is the cell it leaves and the cell
it arrives at - and until 77.45 it was `FDD_PAGE`, which on this page is
`fd_clear_content` plus every field, label, tick and help line.

`frames` x 55.5 ms is the VISIBLE REDRAW: PERFORMANCE.md's first defect, the
one you watch happen on a 4.77MHz machine. `changed` is the honest before/
after difference, which for a caret move is two cells at most.

The four gestures priced, each a place the page used to be redrawn whole:

  into a field         - nothing had the caret, one cell gains it
  to the NEXT field    - one cell loses it, one gains it
  within the SAME field- the widest a caret move can be
  onto the background  - one cell loses it and nothing gains it
  a TICK with a field focused - 77.44's narrow path, which the caret used to
                         widen back to the whole page

IT IS A GATE AS WELL AS A MEASUREMENT.  It is registered in tests/suite.py as
a row that asks a question, so it has to be able to answer NO: every gesture
is held to a bound derived from the GEOMETRY rather than from the last
measurement, and a miss exits 1.  A caret move is two 8x8 cells plus the
mouse pointer's own cell; a tick is 77.44's 12px box plus the same pointer;
nothing may take more than four displayed frames or leave the screen
unsettled.  Measured, the caret rows are 15-52 changed pixels against a bound
of 224, and before 77.45 they were ~4,300 in 9-10 frames - so the bound sits
an order of magnitude clear of both, which is the point: a change that costs
a few more pixels does not fail, and a change back to FDD_PAGE cannot pass.

The FLOOR matters as much as the ceiling.  A session whose Setup click missed
draws nothing at all and would satisfy every bound above, so the three
in-field caret rows must also change AT LEAST the bar's own 8 pixels.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sym                                          # noqa: E402
import dispcp                                           # noqa: E402
import os88geom                                         # noqa: E402

S = os88sym.linear
MACHINE = "os8088_5150_cga_gla"   # GLaBIOS: the IBM ROM is not in this
                                  # tree (tools/martypc/README.md), and this
                                  # is a DRAWING measurement, not a disk one
TAG = "fast"                      # `--tag ref` after `make FTPDSLOW=1`
CMP = None                        # ...then `--tag fast --cmp ref`
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]
    if a == "--tag":
        TAG = sys.argv[i + 1]
    if a == "--cmp":
        CMP = sys.argv[i + 1]

# The Setup page's own geometry, from apps/ftpd/ftpd.asm. The content box's
# origin is the window's content origin; these are the offsets into it.
FD_PAD, FD_BTNH, FD_TEXTX = 4, 14, 6
FD_SETY, FD_SROW, FD_FLDX, FD_FLDW = 22, 16, 112, 160
FD_BTNW = 44                            # the toolbar button's width

# THE BOUNDS (SPEC.md 77.45).  Geometry, not measurement - see the header.
ARROW = 96          # the pointer is drawn INTO the framebuffer (SPEC.md 7.1),
                    # so gfx_lock takes it off and puts it back inside every
                    # hold, and a gesture measured by pressing a mouse button
                    # has the arrow on the thing being drawn by definition
CELL = 8 * 8        # one glyph cell: what a caret leaves, and what it enters
TICK_BOX = 12 * 12  # SPEC.md 77.44's "ONE 12px box"
CARET_MAX = 2 * CELL + ARROW            # 224
TICK_MAX = TICK_BOX + ARROW             # 240
FRAMES_MAX = 4      # 1-3 measured; 9-10 before 77.45
BAR = 8             # the caret bar is 1px wide and 8 rows tall, and a row
                    # that moved it changed at least that much


def field_xy(ox, oy, i, col=4):
    """The middle of field `i`'s row, `col` cells in from its left edge."""
    return ox + FD_FLDX + 4 + col * 8, oy + FD_SETY + i * FD_SROW + 7


def capture(m, mo, x, y, frames=60, edge="press"):
    """Move there RUNNING, then clock ONE button edge while PAUSED.

    The move has to be outside the capture window - a serial mouse delivers a
    packet per movement and the pointer is drawn INTO the framebuffer (SPEC.md
    7.1), so measuring the travel would price the cursor rather than the page.

    `edge` is which half of the gesture is being priced, and it is not a
    detail: the four FIELDS take the PRESS (SPEC.md 13.8.8) and the three
    buttons take the RELEASE, so a tick measured on its press prices
    os88ui_arm's pressed look and never sees the toggle at all.
    """
    mo.to(x, y)
    os88marty.settle(m)
    if edge == "release":
        m.pause()
        m.mouse(0, 0, l=True)       # the press, OUTSIDE the window: it arms
        m.run()                     # the control and draws it down, which is
        os88marty.settle(m)         # not what this row is about
    m.pause()
    m.mouse(0, 0, l=(edge == "press"))
    r = m.flicker(frames=frames)
    if edge == "press":
        m.mouse(0, 0, l=False)
    m.run()
    os88marty.settle(m)
    moved = [f for f in r["per_frame"] if f["changed"]]
    span = ((moved[-1]["cycles"] - moved[0]["cycles"])
            / (r["cpu_mhz"] * 1000.0) if len(moved) > 1 else 0.0)
    bb = None
    for f in r["per_frame"]:
        b = f["bbox"]
        if not f["changed"] or not b:
            continue
        bb = b if bb is None else [min(bb[0], b[0]), min(bb[1], b[1]),
                                   max(bb[2], b[2]), max(bb[3], b[3])]
    return {"frames": len(moved), "span": span, "bbox": bb,
            "changed": max((f["changed"] for f in moved), default=0),
            "flash": max((f["transient"] for f in moved), default=0),
            "settled": r["settled"]}


# The window's RENDERED pixels, so two builds can be compared: the claim these
# paths make is "the picture is the same, only the number of times it was
# drawn changed" (SPEC.md 77.14's reference build), and a capture of ONE build
# cannot check that. tools/os88marty.py owns it - tests/ftpdfocus.py and
# tests/gfxlk.py want the same rectangle for the same reason.
crop = os88marty.crop_rgb


def show(what, r):
    print("  %-28s %2d frame(s) = %6.1f ms   changed %5d   flash %4d  %s%s"
          % (what, r["frames"], r["span"], r["changed"], r["flash"],
             r["bbox"], "" if r["settled"] else "   (NOT SETTLED)"))


rows = []
shots = []
with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    w = dispcp.win_list(m, S)
    wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
    w = dispcp.win_list(m, S)
    wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "FTPD.O88")
    time.sleep(2)
    os88marty.settle(m)
    slot = dispcp.win_list(m, S)[-1]
    wx, wy, ww, wh = dispcp.win_rect(m, S, slot)
    print("FTPD at (%d,%d) %dx%d on %s\n" % (wx, wy, ww, wh, MACHINE))

    # wm_content / wm_geom, which is what fd_layout asks (kernel/wm.inc)
    ox, oy = wx + 1, wy + os88geom.TITLE_H
    cw = ww - 2
    # ...and the Setup button, which is the way IN. It is RIGHT-ALIGNED in the
    # toolbar row (fd_layout's .setbx), not laid at a constant.
    sx = ox + cw - FD_PAD - 1 - FD_BTNW // 2
    sy = oy + FD_PAD + FD_BTNH // 2
    mo.click(sx, sy)
    time.sleep(1.5)
    os88marty.settle(m)
    w_, h_, d_ = m.fbuf()
    os88marty.write_png_rgb("build/ftpdflick-setup.png", w_, h_, d_)
    print("  (Setup page open - build/ftpdflick-setup.png)\n")

    # THE POINTER CANNOT BE PARKED AWAY HERE. This measures a mouse PRESS, so
    # the arrow is by definition sitting on the thing being drawn - and it is
    # drawn INTO the framebuffer (SPEC.md 7.1), so every gfx_lock hold takes it
    # off and puts it back. That is up to 96 px of `changed` and `transient`
    # of the harness's own making, at the click point, in EVERY row below and
    # in the reference build too - so it cancels in the A/B and is never the
    # difference between 2 cells and a page.

    x0, y0 = field_xy(ox, oy, 0)
    x1, y1 = field_xy(ox, oy, 1)

    # TEXT IN THE FIRST FIELD, so the cell the caret leaves holds a CHARACTER
    # and takes os88line_caroff's opaque-run path rather than its white-fill
    # one. An empty field exercises the other half by itself.
    mo.click(x0, y0)
    time.sleep(1.0)
    os88marty.settle(m)
    m.type_text("192.168.1.100")
    time.sleep(1.0)
    os88marty.settle(m)

    def step(what, x, y, **kw):
        rows.append((what, capture(m, mo, x, y, **kw)))
        # ...with the POINTER PARKED, or the crop carries the arrow's own
        # cell and two builds differ by where the mouse happened to stop
        mo.to(wx + ww + 20, wy + 4)
        os88marty.settle(m)
        shots.append((what, crop(m, wx, wy, ww, wh)))

    step("within the SAME field", x0 - 24, y0)
    step("to the NEXT field", x1, y1)
    step("back, onto a CHARACTER", x0 + 8, y0)
    # ...and a tick with a field still focused: SPEC.md 77.44's narrow path,
    # which a focused field used to widen back to FDD_PAGE.
    ty = oy + FD_SETY + 4 * FD_SROW + 6
    tx = ox + FD_TEXTX + 6
    step("a TICK press, field focused", tx, ty)
    # ...and the RELEASE, which is where a tick actually toggles - WITH A
    # FIELD FOCUSED, because that is the case SPEC.md 77.44 had to widen back
    # to FDD_PAGE and 77.45 does not. The row above ended in a release of its
    # own, which took the caret away, so the focus is put back first.
    mo.click(x0, y0)
    time.sleep(1.0)
    os88marty.settle(m)
    step("a TICK release, field focused", tx, ty, edge="release")
    # The BACKGROUND: to the right of the field column, which ends at
    # FD_FLDX + FD_FLDW. Not the toolbar row - Done is in it.
    mo.click(x0, y0)
    time.sleep(1.0)
    os88marty.settle(m)
    step("onto the background", ox + FD_FLDX + FD_FLDW + 40, y1)
    w_, h_, d_ = m.fbuf()
    os88marty.write_png_rgb("build/ftpdflick-after.png", w_, h_, d_)

print()
for what, r in rows:
    show(what, r)

# The bound each gesture is held to, and whether it also has a FLOOR. The two
# tick rows move a box rather than a bar, so they have no floor: 77.44's path
# is not what this test is about.
BOUNDS = {
    "within the SAME field":       (CARET_MAX, BAR),
    "to the NEXT field":           (CARET_MAX, BAR),
    "back, onto a CHARACTER":      (CARET_MAX, BAR),
    "onto the background":         (CARET_MAX, 0),
    "a TICK press, field focused": (TICK_MAX, 0),
    "a TICK release, field focused": (TICK_MAX, 0),
}
bad = []
for what, r in rows:
    cap, floor = BOUNDS[what]
    if not r["settled"]:
        bad.append("%s: NOT SETTLED, so its numbers were measured against a "
                   "moving target" % what)
    if r["changed"] > cap:
        bad.append("%s: %d changed px, more than %d - a caret move is two "
                   "cells and a tick is one 12px box (SPEC.md 77.45)"
                   % (what, r["changed"], cap))
    if r["changed"] < floor:
        bad.append("%s: %d changed px, FEWER than the bar's own %d - the "
                   "gesture drew nothing, so the session missed rather than "
                   "the path being narrow" % (what, r["changed"], floor))
    if r["frames"] > FRAMES_MAX:
        bad.append("%s: %d frame(s) = %.1f ms of visible redraw, more than %d "
                   "- before SPEC.md 77.45 this was 9-10"
                   % (what, r["frames"], r["span"], FRAMES_MAX))

import pickle                                            # noqa: E402
path = "build/ftpdflick-%s.pkl" % TAG
with open(path, "wb") as f:
    pickle.dump({"w": ww, "h": wh, "shots": shots}, f)
print("\n  window pixels after each gesture -> %s" % path)

if CMP:
    other = pickle.load(open("build/ftpdflick-%s.pkl" % CMP, "rb"))
    print("\n  against %s:" % CMP)
    diff = 0
    for (what, a), (_, b) in zip(shots, other["shots"]):
        n = sum(1 for i in range(0, min(len(a), len(b)), 3)
                if a[i:i + 3] != b[i:i + 3])
        diff += n
        print("    %-28s %d differing pixel(s)" % (what, n))
    print("    %s" % ("SAME PICTURE, every gesture" if not diff
                      else "*** %d differing pixels ***" % diff))
    if diff:
        bad.append("--cmp %s: %d differing pixel(s). SPEC.md 77.45's claim is "
                   "that only the NUMBER of draws changed, so the picture has "
                   "to be identical" % (CMP, diff))
print("\n  a FULL Setup repaint is ~15 drawing calls and ~150 glyph cells "
      "(PERFORMANCE.md Part 2)")

if bad:
    print("\nftpdflick: FAIL")
    for b in bad:
        print("  * %s" % b)
    sys.exit(1)
print("\nftpdflick: PASS - no gesture on the Setup page costs more than the "
      "cells it changed (SPEC.md 77.45)")
