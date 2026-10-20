#!/usr/bin/env python3
"""SPEC.md 5.6.9: gfx_points draws the SAME pixels as a gfx_pixel loop.

THE ONLY CLAIM WORTH GATING.  The slot exists to replace a per-point
OSAPI_GFX_PIXEL loop, so what has to be true is that it IS that loop - the same
pixels, under the same clip, with the same dither.  Anything weaker passes a
slot that plots at the wrong address, skips the wrong points, or draws through
a clip nobody set.

tests/ptstest draws one coordinate set TWICE into its window: band A through
OSAPI_GFX_POINTS and band B, PT_DY rows lower, one gfx_pixel a point.  This
reads the two bands off the framebuffer and requires them equal.  No golden
image and no reference build: the comparison is inside one frame.

Four cases, stacked down the window - a solid ink, a DITHER ink (the (x+y)
parity arm), a solid ink with the window's own clip region ARMED, and a solid
PAPER.  The third is SPEC.md 5.6.9.1's: gfx_ls_bx1..by2 still holds the box
case 2 left at case 2's y, and a slot that trusted it would draw outside the
clip.

THE FOURTH IS THE ONE THIS ROW WENT THREE REVISIONS WITHOUT, and SPEC.md
5.6.9.3.1 is what that cost: `gfx_ls_ink` answers THREE classes and the first
three cases only ever ask the slot to SET a bit, so a build that drew paper as
ink passed all of them.  Every app-side erase in the tree goes through this
slot (SPEC.md 5.12.7), so that is a figure that is drawn and never rubbed out.

AND IT RUNS ON BOTH KERNELS.  `--small` boots the `make small` tree, because
the commit loop is not one routine on both builds: kern_small expands
GFXPT_LOOP once and asks the class per point, kern_big expands it three times
and dispatches once a call (SPEC.md 5.6.9.3).  Nothing in the suite ran the
one-loop expansion at all until the `gfxptsmall` row, which is exactly where
5.6.9.3.1 lived.

BREAK IT ON PURPOSE (docs/WRITING-TESTS.md 1) - all four were run:

  * draw every OTHER point:      cases 1, 2 and 3 all red.  RED.
  * drop the dither arm:         case 2 red, 1 and 3 green.  RED, and targeted.
  * draw paper as ink
    (SPEC.md 5.6.9.3.1):         case 4 red, 1 to 3 green, and on `--small`
                                 ALONE - which is the defect itself, so this
                                 one was run backwards: the row was written
                                 against the broken kernel and went green on
                                 the fix.  RED, and targeted.
  * remove the box invalidation
    (SPEC.md 5.6.9.1):           ALL FOUR STAY GREEN.

THE THIRD ONE IS THE HONEST LIMIT OF THIS ROW, and it is written here rather
than discovered later.  The invalidation guards against a stale box that is too
BIG - the `.miss` arm re-resolves a box that is too small, so only an
over-large one draws through a clip nobody set.  That needs a call with the
region DISARMED (box = the whole screen) followed by one with it ARMED whose
points fall outside the armed region.  Every point here is inside the window's
content, so the stale box and the correct one give the same pixels.

Covering it wants a FIFTH case whose pattern reaches PAST the content's right
edge with the clip armed: band B goes through gfx_pixel and is clipped
properly, band A with a stale whole-screen box would draw the overflow, and the
two would differ.  It also wants a wider crop than PT_W to see the overflow.
Left undone deliberately - it is a separate shape, and a row that claims
coverage it has not got is worse than one that names the gap.

THE FIRST VERSION OF THIS ROW WAS A FALSE GREEN, twice over, which is why the
`lit` bound below is not decoration:

  * the ink was white on the window's white content, so both bands were solid
    and identical and all three cases passed on any kernel at all;
  * the pattern's second twelve points REPEATED the first twelve in reverse, so
    a kernel patched to draw every other point still drew every position.
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

# --- WHICH KERNEL, decided BEFORE os88sym is imported anywhere -------------
# os88sym resolves against build/kernel.bin unless told otherwise, and the two
# kernels differ by far more than the build number - so on `--small` without
# this the row dies at its first symbol saying the map describes a DIFFERENT
# kernel, which points at the kernel rather than at the row.  It is
# tests/paint1small.py's pattern, and it has to run at IMPORT time because
# tests/ptsext.py imports this module for its own constants.
SMALL = "--small" in sys.argv
if SMALL:
    import os88build as _B                              # noqa: E402
    _B.use_build("build/smallk")
    os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")

import os88marty                                        # noqa: E402
import os88ui                                           # noqa: E402

SYS = "build/small360.img" if SMALL else "build/os8088-360.img"
APP = "build/ptstest360.img"

PT_DY   = 20        # all four must match tests/ptstest/ptstest.asm
PT_W    = 120
PT_H    = 18
PT_STEP = 40
CASES = ("solid ink", "dither ink", "solid ink, clip ARMED", "solid PAPER")

# ...and which way up each one is.  A PAPER case is a dark pattern on a lit
# ground, so "is this a pattern at all" is the same question asked of the
# pixels that are OUT (SPEC.md 5.6.9.3.1).
PAPER = (False, False, False, True)


def density(A, paper):
    """The minority pixels in band A - lit for an ink case, dark for paper."""
    return sum(1 for row in A for v in row if bool(v) != paper)


def band(rows, x0, y0, w, h):
    """`w` pixels of `h` rows, off m.vram's rows-of-bits."""
    return [tuple(rows[y][x0:x0 + w]) for y in range(y0, y0 + h)]


def main():
    machine = "os8088_5150_herc_gla"
    for i, a in enumerate(sys.argv[1:]):
        if a == "--machine":
            machine = sys.argv[i + 2]

    with os88ui.boot(SYS, apps=APP, machine=machine) as ui:
        m = ui.m
        w = ui.path("B:/PTSTEST.O88")
        ui.settle()

        # the content origin off the guest's OWN window record - a remembered
        # coordinate is what docs/WRITING-TESTS.md 1 is about
        w = ui.window("PtsTest")
        cx, cy = w.content[0], w.content[1]
        _, _, rows = m.vram()

        bad = 0
        for n, name in enumerate(CASES):
            x0 = cx + 8
            ya = cy + 4 + n * PT_STEP
            yb = ya + PT_DY
            A = band(rows, x0, ya, PT_W, PT_H)
            B = band(rows, x0, yb, PT_W, PT_H)
            lit = density(A, PAPER[n])
            if A == B:
                print("  ok   case %d (%-22s) %4d %s pixels agree"
                      % (n + 1, name, lit, "dark" if PAPER[n] else "lit"))
            else:
                bad += 1
                diff = sum(1 for ra, rb in zip(A, B)
                           for va, vb in zip(ra, rb) if va != vb)
                print("  FAIL case %d (%-22s) %4d lit, %d pixels differ"
                      % (n + 1, name, lit, diff))
                for r, (ra, rb) in enumerate(zip(A, B)):
                    if ra != rb:
                        cols = [c for c in range(PT_W) if ra[c] != rb[c]]
                        print("        row %2d: %d columns, first %r"
                              % (r, len(cols), cols[:8]))
            # A BAND THAT IS ALL GROUND OR ALL INK PROVES NOTHING, and the
            # first version of this row was green for exactly that reason: the
            # ink was white on the window's white content, so both bands were
            # solid and identical. The package lays a BLACK ground now, so a
            # real pattern is a small minority of lit pixels.
            if not (8 <= lit <= PT_W * PT_H // 4):
                bad += 1
                print("  FAIL case %d: %d %s of %d - that is not a PATTERN, so"
                      " equal bands prove nothing"
                      % (n + 1, lit, "dark" if PAPER[n] else "lit",
                         PT_W * PT_H))

        if bad:
            print("gfxpoints: %d of %d cases FAILED" % (bad, len(CASES)))
            return 1
        print("gfxpoints: %d cases, gfx_points == a gfx_pixel loop on %s"
              % (len(CASES), "kern_small" if SMALL else "kern_big"))
        return 0


if __name__ == "__main__":
    sys.exit(main())
