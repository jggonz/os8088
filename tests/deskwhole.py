#!/usr/bin/env python3
"""A damage pass whose WINDOW FRAMES overflow the region takes `.whole`, and
leaves a whole repaint's screen (SPEC.md 11.91.6, 26.9.9)

    make && python3 tests/deskwhole.py              # Hercules and VGA
    python3 tests/deskwhole.py --machine os8088_xt_vga

`wm_dmg_gray` builds the desktop dither's region as the damage minus every
visible window FRAME (`wm_dmg_occl`), minus every drop-shadow L
(`wm_occl_l`), minus every touched desktop zone (`desk_zones_r`), in
`wm_clip_tab`'s WM_CLIP_MAX = 16 fragments. Since SPEC.md 26.9.9 the Ls'
overflow has its own fallback (`.frames`) and a zone that would overflow is
refused before it is drawn, so `.whole` - dither every band of the damage,
clear `[wm_dmg_stwin]` so every window in it repaints whole, owe every
touched zone again, re-seed the bands from the damage - is reached only when
the FRAMES ALONE overflow. tests/deskzoom.py used to reach it and no longer
does; this row is the one that does, on purpose, and proves it.

THE LAYOUT, and why it overflows. `wm_clip_split` replaces a fragment an
occluder overlaps with up to four - the full-width piece above it, the one
below it, and the pieces left and right of it in the rows they share - so a
frame strictly inside one fragment is +3 and a frame that reaches into
several fragments cuts each. Seven windows - the four Disk windows
FM_MAXWIN allows and three Calculators:

    the cascade   three Disk windows shrunk to FM_MIN_W x FM_MIN_H (194x92),
                  then three Calculators (226x137), back to front, each 8 px
                  LEFT of and 28 px BELOW the last: (231,40) (223,68)
                  (215,96) (207,124) (199,152) (191,180)
    the mover     A:'s root window, 194x92, dragged from (7,28) at the
                  top-left to (439,230) at the bottom-right

The damage is the union of where the mover was and where it is, (7,28) to
(633,323), and holds the whole cascade. The first window is +3 (4
fragments). Each one after it reaches into the full-width piece below the
last one (cut in three: left, right, below - +2) and, starting 8 px further
left, into the slivers left of the windows above it (each cut in two): 7,
10; the first Calculator is wider than the Disk windows and cuts the pieces
right of them as well, 14; and the second Calculator's split needs a
SEVENTEENTH slot. The mover would take it to 26. `region()` below replays
`wm_clip_split` over the LIVE records and prints that uncapped count; the
row refuses to run the gesture on a layout the model does not overflow, and
then asserts on the machine that `.whole` WAS taken - a breakpoint on the
label - with neither `wm_occl_l` nor `.frames` reached before it in that
pass, which is what says it was the frames' overflow and not the Ls'.

No parked window overlaps the vacated rect, (7,28)-(201,120): on 11.91.2's
narrow path not one of them would be repainted. `.whole` dithers every band
of the damage, them included, so it must repaint them all - asserted per
window (`wm_draw_win`'s slots) and then pixel for pixel against
`[cp_dirty]`'s forced whole repaint (tools/deskclip.py's verify).

THE RESTORE LEG, on the same desktop: the mover zoomed over the whole band
and restored, the restore traced. Its damage is the whole band - every
window and both drive cells - and its frames overflow the same way, so it
asserts the same three things plus both cells drawn. What it adds is a
damage that is mostly UNCOVERED ground, which is where `.whole`'s re-seed of
the bands is visible; the drag's stale ground lies in fragments the walk
had finished with before it overflowed, so a fill clipped to the sixteen it
left still reaches all of it.

Every y here moves by an EVEN delta (x is snapped to 8 by SPEC.md 11.94): a
Disk window moved an odd total brings its scroll trough back in the other
dither phase from a whole repaint's, which is tests/deskzoom.py's note and
not this row's subject.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1), measured on Hercules and
VGA in a private tree (`make BUILD=<dir>`, `OS88_BUILD=<dir>`), never
committed:
  * `mov word [wm_dmg_stwin], 0` taken out of `.whole`: the DRAG leg goes
    red twice - none of the six parked windows is redrawn, and the dither
    `.whole` laid over them stays: 30,907 px on Hercules, 31,135 on VGA.
    (The restore arms no vacated rect, so its leg stays green.)
  * `.whole`'s re-seed (`call wm_dmg_bands` after its pops) taken out: the
    fill is clipped to the sixteen fragments the overflow left, and the
    RESTORE leg goes red - the zoomed window's picture survives in a strip
    beside the restored mover, 1,214 px on Hercules and 220 on VGA. (The
    drag leg stays green, for the reason above.)
`call COLD_SEG:desk_dmg_zones_x` at `.whole` taken out stays GREEN on both
legs and both adapters, and must: the frames overflow before `desk_zones_r`
has drawn a zone or cleared a bit, so the mask it rebuilds is the one
wm_paint_dmg built a few instructions earlier. It is load-bearing only for
an overflow AFTER the zones, which desk_zones_r's own count (SPEC.md 26.9.9)
made unreachable - the `jc .whole` after it can no longer be taken either.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88ui               # noqa: E402
import os88geom as geom     # noqa: E402
import deskclip as dc       # noqa: E402

BDIR = os.environ.get("OS88_BUILD", os.path.join(ROOT, "build"))
CLIP_MAX = 16               # kernel.asm's WM_CLIP_MAX

# The cascade, back to front: three Disk windows at FM_MIN_W x FM_MIN_H and
# then three Calculators, each 8 px left of and 28 px below the last. Every x
# is 7 mod 8, which is where SPEC.md 11.94's snap puts a frame anyway.
CASCADE = [(231 - 8 * i, 40 + 28 * i) for i in range(6)]
FROM, TO = (7, 28), (439, 230)
FM_MIN = (194, 92)          # files.inc's FM_MIN_W x FM_MIN_H

MARKS = ["wm_paint_dmg", "wm_paint_dmg.out", "wm_dmg_gray.whole",
         "wm_dmg_gray.frames", "wm_occl_l", "wm_draw_win", "desk_draw_zone"]


# --- the model: wm_dmg_bands, then wm_dmg_occl's walk of wm_clip_split ------

def region(seed, frames):
    """Replay wm_clip_subr/wm_clip_split UNCAPPED over `frames` (inclusive
    rects, back to front) and answer the most fragments the list held - the
    kernel's overflows exactly when this exceeds CLIP_MAX."""
    lst, peak = [seed], 1
    for o in frames:
        i = 0
        while i < len(lst):
            r = lst[i]
            if r[2] < o[0] or r[0] > o[2] or r[3] < o[1] or r[1] > o[3]:
                i += 1
                continue
            last = lst.pop()            # the last moves into the slot...
            if i < len(lst):
                lst[i] = last
            y1, y2 = max(r[1], o[1]), min(r[3], o[3])
            for f in ((r[0], r[1], r[2], o[1] - 1),     # above
                      (r[0], o[3] + 1, r[2], r[3]),     # below
                      (r[0], y1, o[0] - 1, y2),         # left
                      (o[2] + 1, y1, r[2], y2)):        # right
                if f[0] <= f[2] and f[1] <= f[3]:
                    lst.append(f)
                    peak = max(peak, len(lst))
        if not lst:
            break
    return peak


def even(w, x, y):
    """(x, y) with y nudged so the move's y delta is even (see above)."""
    return x, y + ((y - w.y) & 1)


def run(machine):
    print(machine)
    with os88ui.boot(os.path.join(BDIR, "os8088-360.img"),
                     apps=os.path.join(ROOT, "build", "apps360.img"),
                     machine=machine) as ui:
        m = ui.m
        fails = []

        def check(cond, what):
            print("   %s  %s" % ("ok " if cond else "FAIL", what))
            if not cond:
                fails.append(what)

        def took_whole(hits, wins, what):
            """The pass that took `.whole`, checked: exactly one, reached
            from the FRAMES' overflow, and every one of `wins` drawn in it"""
            ps = [p for p in dc.passes(hits)
                  if any(h["name"] == "wm_dmg_gray.whole" for h in p)]
            check(len(ps) == 1, "one damage pass took wm_dmg_gray's .whole "
                                "(%d did)" % len(ps))
            if not ps:
                return None
            names = [h["name"] for h in ps[0]]
            cut = names.index("wm_dmg_gray.whole")
            check("wm_occl_l" not in names[:cut]
                  and "wm_dmg_gray.frames" not in names[:cut],
                  "...from the FRAMES' overflow: neither wm_occl_l nor "
                  ".frames ran before it")
            drawn = {h["hit"]["slot"] for h in ps[0]
                     if h["name"] == "wm_draw_win"}
            miss = [w.title for w in wins if w.i not in drawn]
            check(not miss, "%s (missed: %r)" % (what, miss))
            return ps[0]

        def toggle(w):
            """tests/deskzoom.py's: double-click the title bar and wait for
            the record to change, on the guest's clock"""
            ui.mo.dblclick(w.x + 40, w.y + 5)
            ui._wait(lambda: ui._rect(w.i) != (w.x, w.y, w.w, w.h),
                     "the title-bar double-click to zoom %r" % (w.title,),
                     15.0)
            ui.settle()
            return ui._refresh(w)

        def small(w):
            """FM_MIN_W x FM_MIN_H (files.inc), the floor a Disk window's
            grow box stops at"""
            return dc.shrink(ui, w, FM_MIN[0] - w.w, FM_MIN[1] - w.h)

        # FM_MAXWIN = 4 Disk windows (files.inc). A drive's zone opens a new
        # one only while the ones it has are away from its root (SPEC.md
        # 22.1), so each is sent into a folder before the next is asked for;
        # the fourth, A:'s root, is the mover
        disks = []
        for drv, sub in (("A", "SYSTEM"), ("A", "MEDIA"), ("B", "APPS")):
            w = ui.open_drive(drv)
            ui.open(sub, win=w)
            disks.append(w)
        calcs = [ui.open("CALC.O88", win=disks[-1]) for _ in range(3)]
        disks = [small(ui._refresh(w)) for w in disks]
        mover = small(ui.open_drive("A"))

        for w, xy in zip(disks + calcs, CASCADE):
            w = ui._refresh(w)
            ui.move_window(w, *even(w, *xy))
        mover = ui._refresh(mover)
        mover = ui.move_window(mover, *even(mover, *FROM))
        ui.mo.to(4, 4)
        ui.settle()

        tx, ty = even(mover, *TO)
        old = (mover.x, mover.y, mover.x + mover.w, mover.y + mover.h)
        new = (tx, ty, tx + mover.w, ty + mover.h)
        dmg = (min(old[0], new[0]), min(old[1], new[1]),
               max(old[2], new[2]), max(old[3], new[3]))
        band = (geom.word(m, "vid_band_x0"), geom.MBAR_H,     # wm_dmg_bands'
                geom.word(m, "vid_band_xe") - 1,              # display 0
                geom.word(m, "vid_dock_y0") - 1)
        seed = (max(dmg[0], band[0]), max(dmg[1], band[1]),
                min(dmg[2], band[2]), min(dmg[3], band[3]))
        parked = [ui._refresh(w) for w in disks + calcs]
        for w in parked:
            print("  parked %-10s (%d,%d,%d,%d)" % (w.title[:10], w.x, w.y,
                                                    w.w, w.h))
        z = geom.zorder(m, ui.sym)                  # [0] is the backmost
        frames = [(w.x, w.y, w.x + w.w - 1, w.y + w.h - 1)
                  for w in sorted(parked, key=lambda o: z.index(o.i))]
        frames.append((tx, ty, tx + mover.w - 1, ty + mover.h - 1))
        need = region(seed, frames)
        print("  mover %r -> (%d,%d); damage %r; the region needs %d "
              "fragments uncapped" % ((mover.x, mover.y, mover.w, mover.h),
                                      tx, ty, dmg, need))
        check(need > CLIP_MAX, "SETUP: the frames alone overflow %d fragments "
                               "(the model says %d)" % (CLIP_MAX, need))
        inside = [w for w in parked
                  if dmg[0] < w.x and w.x + w.w < dmg[2]
                  and dmg[1] < w.y and w.y + w.h < dmg[3]]
        clear = [w for w in parked
                 if w.x > old[2] or w.x + w.w < old[0]
                 or w.y > old[3] or w.y + w.h < old[1]]
        check(len(inside) == len(parked) and len(clear) == len(parked),
              "SETUP: every parked window lies inside the damage and clear "
              "of the vacated rect")
        if fails:
            return fails

        trig = dc.drag_setup(ui, tx - mover.x, ty - mover.y, mover)
        hits = dc.armed(ui, trig, names=MARKS)
        ui.settle()
        got = ui._refresh(mover)
        check((got.x, got.y) != (mover.x, mover.y),
              "SETUP: the drag happened (mover now at %d,%d)" % (got.x, got.y))
        took_whole(hits, parked, "(c) every parked window is repainted "
                   "whole, none of them near the vacated rect")
        bad = dc.verify(ui)
        check(bad == 0, "the drag leaves a whole repaint's screen (%d pixels "
                        "differ)" % bad)

        # THE RESTORE LEG: the mover zoomed over the whole desktop band,
        # untraced, then restored with the marks armed. The damage is the
        # whole band - every window and both drive cells - and nothing was
        # vacated, so what `.whole` owes here is its DITHER: the bands
        # re-seeded from the damage, not the sixteen fragments the overflow
        # left, or the zoomed window's pixels stay wherever no window and
        # no cell is drawn back
        mover = ui._refresh(mover)
        was = (mover.x, mover.y, mover.w, mover.h)
        mover = toggle(mover)
        zoomed = (mover.x, mover.y, mover.w, mover.h)
        check(zoomed != was, "SETUP: the mover zoomed %r -> %r"
              % (was, zoomed))
        hits = dc.armed(ui, lambda: ui.mo.dblclick(mover.x + 40, mover.y + 5),
                        names=MARKS)
        ui.settle()
        mover = ui._refresh(mover)
        check((mover.x, mover.y, mover.w, mover.h) == was,
              "SETUP: the restore put it back at %r" % (was,))
        p = took_whole(hits, parked + [mover],
                       "every window is repainted whole")
        if p:
            zones = sorted({h["hit"]["zone"] for h in p
                            if h["name"] == "desk_draw_zone"})
            check(0 in zones and 1 in zones,
                  "both drive cells are drawn (zones drawn: %r)" % (zones,))
        bad = dc.verify(ui)
        check(bad == 0, "the restore leaves a whole repaint's screen (%d "
                        "pixels differ)" % bad)
        return fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", action="append")
    a = ap.parse_args()
    fails = [m for m in a.machine or ["os8088_5150_herc_gla", "os8088_xt_vga"]
             if run(m)]
    print("deskwhole: %s" % ("PASS" if not fails else "FAIL: " + " ".join(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
