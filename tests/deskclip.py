#!/usr/bin/env python3
"""A desktop cell is drawn only where the pass reveals it (SPEC.md 11.91.6)

    make && python3 tests/deskclip.py            # kern_big, Hercules and VGA
    python3 tests/deskclip.py --small            # kern_small, Hercules

`wm_paint_dmg` used to draw every desktop cell its damage touched WHOLE, so a
window lying on the cell had its own pixels painted over and was owed a
repaint, and every window above THAT one was marked transitively - measured
at 296 ms of the Task Manager's W_PAINT for a drag that never touched it,
and a 164.5 ms transitive W_PAINT on VGA behind an in-place cell repaint
(docs/plans/completed/DESK-CLIP-PLAN.md). This row drives three of that
plan's gestures through tools/deskclip.py's own scenarios, each with ONE
window that sits over a cell and that nothing but the cell would mark:

  cell   the cell repainted in place (a mount) under that window
  close  a second window closed whose frame reaches the cell, not the window
  drag   a shrunk window dragged over the cell column, its vacated rect
         clear of the window

and asserts, per gesture and adapter:

  (a) the spared window is NOT drawn - no `wm_draw_win` names it;
  (b) its pixels over the cell match a forced whole repaint (`[cp_dirty]`).
      ONLY that rect: the whole repaint is not a perfect reference (the
      plan's 6.2 - it under-draws a title whose visible part spans two
      fragments, which `drag` puts on the glass);
  (c) `cell` promotes nobody - nothing moved - so no `wm_draw_title` runs
      after the window pass (`.wdone`), which is where the promotion is.

kern_small has the cheaper half (option B): a cell the pass reveals NONE of
is not drawn and marks nobody, anything else is drawn whole as before. So
`--small` runs `drag` - where the mover and the window parked over the cell
cover it between them - asserting (a), (b) and that no cell is drawn at all,
and `cell` asserting (b) and (c).

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1), each measured:
  * the region's frame subtraction taken out: the cell is drawn over the
    window that is no longer marked - (b), 495 pixels on Hercules when the
    region was `wm_zone_r`'s own. Since SPEC.md 26.9.9 a kern_big cell is
    drawn into wm_dmg_gray's region (`desk_zones_r`), whose frames are
    `wm_dmg_occl`'s - the same subtraction, one region per pass;
  * (`ico_clip`'s columns were the fourth break, 32 pixels; SPEC.md 11.3.5
    withdrew it - a cell's picture is a gfx_blit1 band, cut exactly by the
    walk, and tests/deskflash.py's head break is what reaches that now)
  * desk_zones_paint_x's three C2 stores taken out: the window over the cell
    is marked by the cell's own damage again - (a), and (c) where the front
    window is not redrawn for a reason of its own (Hercules);
  * kern_small's `jz .r` after `cw_wm_zone_r` taken out: the covered cells
    are drawn and the window repaints - (a).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
SMALL = "--small" in sys.argv
if SMALL:
    os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")

import os88build            # noqa: E402
import os88sym              # noqa: E402
import os88ui               # noqa: E402
import deskclip as dc       # noqa: E402

if SMALL:
    # tests/small128.py's private tree, and its reason for naming smallk/
    _T = os88build.tree(targets=("small", "apps360.img"))
    os.environ["OS88_BUILD"] = os.path.join(_T.dir, "smallk")
    os88sym.default_defines("KERN_SMALL")
    IMAGE, APPS = _T.img("small360.img"), _T.img("apps360.img")
    RUNS = [("os8088_5150_herc_gla", "drag"), ("os8088_5150_herc_gla", "cell")]
else:
    BDIR = os.environ.get("OS88_BUILD", os.path.join(ROOT, "build"))
    IMAGE = os.path.join(BDIR, "os8088-360.img")
    APPS = os.path.join(ROOT, "build", "apps360.img")
    RUNS = [(m, s) for m in ("os8088_5150_herc_gla", "os8088_xt_vga")
            for s in ("cell", "close", "drag")]

MARKS = ["wm_paint_dmg", "wm_paint_dmg.out", "wm_draw_win", "wm_draw_title",
         "desk_draw_zone", "wm_dmg_wins.wdone"]
fails = []


def promoted(p):
    """Title bars a pass drew AFTER its window loop: the promotion's."""
    names = [h["name"] for h in p]
    if "wm_dmg_wins.wdone" not in names:
        return 0
    return names[names.index("wm_dmg_wins.wdone"):].count("wm_draw_title")


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)


for machine, sc in RUNS:
    with os88ui.boot(IMAGE, apps=APPS, machine=machine) as ui:
        print("== %s %s / %s" % ("kern_small" if SMALL else "kern_big",
                                 machine, sc))
        trig, done = dc.SCENARIOS[sc](ui)
        slot, rect = dc.LAST["spared"]
        if rect is None:
            check(False, "SETUP: the spared window lies over the cell")
            continue
        hits = dc.armed(ui, trig, names=MARKS)
        ui.settle()
        check(done(), "SETUP: the gesture happened")
        ps = dc.passes(hits)
        check(bool(ps), "a damage pass ran")
        names = [h["name"] for p in ps for h in p]
        drawn = [h["hit"]["slot"] for p in ps for h in p
                 if h["name"] == "wm_draw_win"]
        if SMALL and sc == "cell":
            pass                # B draws a partly visible cell WHOLE: the
                                # window over it is owed, as it always was
        else:
            check(slot not in drawn,
                  "(a) window %d over the cell is not redrawn (drawn: %r)"
                  % (slot, drawn))
        if SMALL and sc == "drag":
            n = names.count("desk_draw_zone")
            check(n == 0, "the covered cells are not drawn (%d draws)" % n)
        if sc == "cell":
            n = sum(promoted(p) for p in ps)
            check(n == 0, "(c) nothing moved, so nothing is promoted "
                          "(%d title bars drawn after the window pass)" % n)
        bad = dc.verify(ui, only=rect)
        check(bad == 0, "(b) window %d over the cell, %r, matches a whole "
                        "repaint (%d pixels differ)" % (slot, rect, bad))

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  every cell drawn only where it shows, and nobody else owed for it")
