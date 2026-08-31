#!/usr/bin/env python3
"""IS A BLANK CANVAS ACTUALLY BLANK, ON THE ADAPTER THAT STORES IT PACKED?
(SPEC.md 42.13.2.1)

    make && python3 tests/paintwipe.py

Every other paint row here opens a PICTURE and compares it against the file.
That is the right oracle for the routines that carry pixels about, and it is
blind to the one routine that invents them: `pt_wipe` fills the ground, so a
ground that is wrong is not a difference from the file, it is the state every
comparison starts from.  Nothing in `tests/` looked at an EMPTY canvas.

What got through: `pt_wipe` banks its fill word in `DX`, and the four-plane
body wanted the bare colour, so it took `DH` - the high half of the word the
packed body stores.  A `CWHITE` wipe laid `0x0FFF` instead of `0xFFFF`, so
one pixel in four was colour 0.  Identical on every row, because every row is
wiped alike, which on screen is a black vertical line a pixel wide every four
pixels down the whole picture - and in the saved `.BMP` with them, the canvas
being what a save is a single write of.

**It cannot be seen on VGA**, and that is the row's whole reason.  SPEC.md
42.13 stores the canvas as four planes on a colour adapter and as nibbles on
a 1bpp one, so `.row` - the body that was wrong - is the one a VGA machine
never runs.  The machine here is therefore Hercules, and the assertion is the
simplest one this suite has: a canvas that has been wiped white and drawn on
by nobody may not contain a dark pixel.

The canvas rect is ASKED for rather than computed, off the blit that draws
it: on 1bpp that is `gfx_blit4`, whose AX/BX/CX/DX are the destination x, y,
width and height, and the canvas is far and away the widest one Paint issues.
Where Paint puts its picture is not this test's opinion to hold.

The machine is the **GLaBIOS** Hercules twin rather than `os8088_5150_herc`,
because `ibm5150_82_v4` is IBM's ROM and is not in this tree, so a fresh
checkout cannot boot the period machine at all (docs/MARTYPC-DEBUG.md).  The
one thing a GLaBIOS machine is no good for is a DISK number, and this row
takes no timing of any kind - it reads pixels off a settled screen.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, dispcp, dispapps       # noqa: E402
from paintmove import pkg_syms                               # noqa: E402

S = os88sym.linear


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--kind", default="herc")
    ap.add_argument("--shot", default=None,
                    help="write the failing screen here")
    a = ap.parse_args(argv)
    os.chdir(os.path.join(HERE, ".."))

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        os88marty.settle(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        rows = [r[0] for r in dispcp.listing(m, S)]
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                               rows.index("PAINT.O88"))
        rx, ry = dispcp.row_xy(wx, wy, row)
        mo.to(rx, ry)
        os88marty.settle(m)

        # --- the canvas, out of PAINT'S OWN WORDS
        #
        # This used to hunt for the gfx_blit4 that drew the canvas and take
        # its rect. SPEC.md 42.15 ended that: one colour is one rect, so a
        # blank canvas is a gfx_fill and costs 1 ms instead of 980 - and a
        # blank canvas is this row's entire subject, so the blit it was
        # waiting for is exactly the one that can no longer happen. It read
        # as "PAINT.O88 did not open".
        #
        # pt_org latches both origins from the window record at the top of
        # every callback (SPEC.md 42), so after a settle they describe where
        # the canvas actually is, whatever drew it.
        mo.dblclick(rx, ry)
        os88marty.settle(m)
        slot = dispapps.pkg_seg(m, 0)       # (window index, segment)
        if slot is None:
            sys.exit("paintwipe: PAINT.O88 did not open")
        sym = pkg_syms("apps/paint/paint.asm")
        base = slot[1] << 4

        def word(n):
            b = m.read(base + sym[n], 2)
            return b[0] | (b[1] << 8)

        ox, oy = word("pt_cx0"), word("pt_cy0")
        cw, ch = word("pt_cw"), word("pt_ch")
        if cw < 1 or ch < 1:
            sys.exit("paintwipe: Paint reports a %dx%d canvas, so it is not "
                     "up yet" % (cw, ch))
        print("   canvas: %dx%d at %d,%d" % (cw, ch, ox, oy))

        # The pointer is a SHAPE ON THE SCREEN (SPEC.md 9), so park it off the
        # canvas before reading: left where the double-click left it, its
        # arrow is a few dozen dark pixels inside the box and every one of
        # them is a false failure.
        mo.to(4, 4)
        os88marty.settle(m)

        w, h, fb = m.vram(a.kind)
        if ox + cw > w or oy + ch > h:
            sys.exit("paintwipe: the canvas (%d,%d %dx%d) is off a %dx%d "
                     "screen" % (ox, oy, cw, ch, w, h))
        dark = [(x, y) for y in range(oy, oy + ch)
                for x in range(ox, ox + cw) if not fb[y][x]]

    if dark:
        cols = sorted({x - ox for x, _ in dark})
        print("paintwipe: FAIL - %d dark pixels of %d in a canvas nobody has "
              "drawn on" % (len(dark), cw * ch))
        print("   first: %s" % (dark[:8],))
        print("   canvas columns hit: %s%s"
              % (cols[:12], " ..." if len(cols) > 12 else ""))
        if len(cols) > 1:
            step = cols[1] - cols[0]
            if all(b - a == step for a, b in zip(cols, cols[1:])):
                print("   ...every %d columns, which is a WIPE defect rather "
                      "than a draw: pt_wipe's fill word (SPEC.md 42.13.2.1)"
                      % step)
        return 1
    print("paintwipe: PASS - %d canvas pixels, all white" % (cw * ch))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
