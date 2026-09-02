#!/usr/bin/env python3
"""SPEC.md 13.8.3: the Control Panel acts on the RELEASE, not the press.

    python3 tests/cpup.py [machine]

The Scheduler page is the one this drives, because its effect is a single
KERNEL BYTE - `[sch_coop]` - so a pass is a memory read rather than a picture,
which is tests/mouseup.py's discipline and the right one for a behaviour
change. What is asserted is the mechanism, and the mechanism is shared by all
five pages: one probe, one arm, one release that re-runs the same ladder.

  A  press Cooperative, slide OFF the pane, release   -> mode UNCHANGED
  B  press Cooperative, release on the OTHER row      -> mode UNCHANGED
  C  press AND release on Cooperative                 -> mode CHANGED

C is the one that says the conversion did not eat the feature: a panel that
had simply stopped dispatching would pass A and B.

THE PANEL IS AN ON-DEMAND MODULE (SPEC.md 2.8) and this is also the first
test of its entry table growing - CPE_ONUP and CPE_ONDRAG took MOD_NENT from
4 to 8. A wrong index there does not fault, it far-calls into the module's own
data, so "the panel still works at all" is a real assertion here and not a
formality.
"""
import sys
import time

sys.path.insert(0, "tools")
import os88marty as M
from os88mouse import Mouse
from os88geom import WIN_SIZE, MAX_WIN

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
W_FLAGS, W_X, W_Y, W_W, W_H, W_TITLE = 0, 2, 4, 6, 8, 10

# ctrl.inc's geometry, mirrored deliberately (a gate that derives it from the
# kernel is testing the kernel against itself).
CP_RX = 96                      # pane left, content-relative
CP_B0Y1, CP_B0Y2, CP_B1Y2 = 22, 41, 61      # the two mode bands, pane-relative
CP_PGX, CP_PR0Y, CP_PROWH = 4, 26, 20       # ...and the radio glyphs in them
TITLE_H = 18

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o):
    return b[o] | (b[o + 1] << 8)


def panel(m):
    """The Control Panel window, by its title pointer."""
    want = m.sym("cp_ttl") - (M.KERNEL_SEG << 4)
    blob = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    for i in range(MAX_WIN):
        r = blob[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if _u16(r, W_FLAGS) & 2 and _u16(r, W_TITLE) == want:
            return tuple(_u16(r, o) for o in (W_X, W_Y, W_W, W_H))
    return None


def coop(m):
    return m.read(m.sym("sch_coop"), 1)[0]


def row_pt(w, which):
    """The centre of mode row `which` (0 = Pre-emptive, 1 = Cooperative)."""
    cx, cy = w[0] + 1, w[1] + TITLE_H
    y = (CP_B0Y1 + CP_B0Y2) // 2 if which == 0 else (CP_B0Y2 + CP_B1Y2) // 2
    return cx + CP_RX + 40, cy + y


with M.launch("build/os8088-360.img", apps="build/apps360.img",
              machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    print(f"== {MACHINE} : Control Panel fires on the release (13.8.3) ==")

    # The chip menu's Control Panel item. menu, not click: a menu cannot be
    # opened with a click (os88mouse's own note).
    mo.menu(8, 8, 8, 40)                    # chip menu -> Control Panel,
                                            # tests/hddcp.py's own coordinates
    M.settle(m)
    w = panel(m)
    check("the Control Panel opened", w is not None,
          "" if w else "(the module's entry table is the first suspect)")
    if not w:
        sys.exit("no panel")

    was = coop(m)
    other = 1 if was == 0 else 0
    print(f"  [sch_coop] starts {was}; the row under test selects {other}")
    target = row_pt(w, other)

    # --- 0: the row draws PRESSED while held, and only that row ----------
    # SPEC.md 13.8.3's second half. The measurement is the two 12x12 glyph
    # boxes and nothing else, so the page's labels and captions cannot be
    # mistaken for the control - PERFORMANCE.md Part 3.1's own trap.
    def glyph(which):
        cx, cy = w[0] + 1, w[1] + TITLE_H
        gy = cy + (CP_PR0Y if which == 0 else CP_PR0Y + CP_PROWH)
        gx = cx + CP_RX + CP_PGX
        return (gx, gy, gx + 11, gy + 11)

    # 1bpp is read out of VRAM and mode 12h out of the RENDERED frame: on
    # Hercules the rendered raster has alternating blank scanlines, so a solid
    # box there measures ~44% lit and an inversion measures IDENTICAL either
    # side of it (docs/MARTYPC-DEBUG.md). Neither route can stand in for the
    # other, and VGA is a third renderer, so it is asserted rather than skipped.
    mono = m.video()["type"] in ("cga", "mda", "herc")

    def lit(rect):
        if mono:
            ww, hh, rows = m.vram()
            return sum(rows[y][x] for y in range(rect[1], rect[3] + 1)
                       for x in range(rect[0], rect[2] + 1))
        ww, hh, px = m.fbuf()
        n = 0
        for y in range(rect[1], rect[3] + 1):
            for x in range(rect[0], rect[2] + 1):
                i = (y * ww + x) * 3
                if px[i] or px[i + 1] or px[i + 2]:
                    n += 1
        return n

    up_t, up_o = lit(glyph(other)), lit(glyph(was))
    mo.to(*target)
    mo._edge(True)
    time.sleep(0.8)
    held_t, held_o = lit(glyph(other)), lit(glyph(was))
    check("the held row's glyph draws PRESSED", held_t != up_t,
          f"({up_t} lit upright, {held_t} held)")
    check("...and the OTHER row's does not move", held_o == up_o,
          f"({held_o} lit, was {up_o})")
    mo.to(w[0] + 20, target[1], l=True)     # slide off, STILL HELD
    time.sleep(1.2)
    check("...and it comes back UP when the pointer slides off",
          lit(glyph(other)) == up_t,
          f"({lit(glyph(other))} lit, upright is {up_t})")
    mo._edge(False)
    M.settle(m)

    # --- A: press the row, slide off the pane, release --------------------
    mo.to(*target)
    mo._edge(True)
    time.sleep(0.7)
    mo.to(w[0] + 20, target[1], l=True)     # l=True: STILL HELD, into the
    time.sleep(1.0)                         # item list on the left
    mo._edge(False)
    M.settle(m)
    check("a slide-off release does NOT change the mode", coop(m) == was,
          f"([sch_coop] = {coop(m)})")

    # --- B: press it, release on the OTHER row ----------------------------
    mo.to(*target)
    mo._edge(True)
    time.sleep(0.7)
    mo.to(*row_pt(w, was), l=True)
    time.sleep(1.0)
    mo._edge(False)
    M.settle(m)
    check("released on the OTHER row, nothing fires", coop(m) == was,
          f"([sch_coop] = {coop(m)})")

    # --- C: press AND release on it ---------------------------------------
    mo.to(*target)
    mo._edge(True)
    time.sleep(0.7)
    mo._edge(False)
    M.settle(m)
    check("press-and-release ON the row DOES change it", coop(m) == other,
          f"([sch_coop] = {coop(m)}, wanted {other})")

    # --- and the panel is still alive, which the module ABI decides -------
    check("...and the panel is still up and drawing", panel(m) is not None)

print()
if fails:
    print(f"{len(fails)} FAILED: {', '.join(fails)}")
    sys.exit(1)
print("all pass")
