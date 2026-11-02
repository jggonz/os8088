#!/usr/bin/env python3
"""THE DOS WINDOW ON A HERCULES IS SNUG ROUND ITS 80x25 (SPEC.md 96.32.5).

The owner, off the pc5150: *"Can we make the window size on herc fit the
screen, instead of taking up the full width?"* - and then, of the height:
*"the 80x25 is more important than the dock cover, my request was just 'not
bigger than it needs to be for that'"*.

Until 96.32.5 the Hercules row of `dos_pref` asked for the full 720, so the
frame SPANNED the screen and the 640-pixel console band sat in it with 40
pixels of empty window either side. VGA and CGA are 640 wide and span for a
reason (96.32: 80 columns is 640 px of content, and only a spanning frame has
no side borders); a 720-wide card has 80 px to spare and needs none of it.

TWO LEGS, both on the machine the report came off (a Hercules 5150):

  1  GEOMETRY, out of the kernel's window record and the package's own band
     words: the frame lies on the screen, the band is 80 x 25 with all 25
     rows shown (`[con_vrows]`, vtop 0), and the frame is SNUG - its content
     is exactly the band's 640 pixels, borders included, nothing more. Red on
     the spanning build: `the frame is 720 wide, content 720 against a band
     of 640: 80 px of empty window beside the console`.
  2  THE GLASS, out of Hercules VRAM in guest coordinates (`m.vram`): output
     is pushed until the cursor is on the console's LAST row (24), and that
     row has ink in it on the screen, inside the frame. A geometry that is
     right in the words and a band that is drawn somewhere else would pass
     leg 1 alone.

Break it on purpose (docs/WRITING-TESTS.md 1): with the Hercules row of
`dos_pref` put back to 720 (and the template x to 0), leg 1 fails naming
the 80-pixel slack - done, on the build before 96.32.5.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402
from dosconcga import Dos                                      # noqa: E402

SYS = "build/os8088-360.img"
APPS = "build/apps360.img"
DOSPKG = "A:/APPS/DOS.O88"
MACHINE = "os8088_5150_herc_gla"         # the owner's adapter, 720x348
MBAR_H = 20


def fail(msg):
    print("doshercwin: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing - a plain `make` builds it" % p)

    with os88ui.boot(SYS, apps=APPS, machine=MACHINE) as ui:
        m = ui.m
        if not ui.path(DOSPKG):
            fail("could not launch %s" % DOSPKG)
        os88marty.settle(m)
        dos = Dos(m)
        vw, vh = ui._word("vid_w"), ui._word("vid_h")
        if (vw, vh) != (720, 348):
            fail("the desktop is %dx%d - this row is about a Hercules"
                 % (vw, vh))

        # --- 1: the frame is on the screen and snug round 80x25 -------------
        w = ui.window("DOS")
        cx, cy = dos.w("dos_conx"), dos.w("dos_cony")
        cols, rows = dos.w("dos_concols"), dos.w("dos_conrows")
        vrows, vtop = dos.w("con_vrows"), dos.w("con_vtop")
        print("doshercwin: frame x=%d y=%d %dx%d, band at (%d,%d) %dx%d, "
              "vrows %d vtop %d" % (w.x, w.y, w.w, w.h, cx, cy, cols, rows,
                                    vrows, vtop))
        if w.x < 0 or w.x + w.w > vw or w.y < MBAR_H or w.y + w.h > vh:
            fail("the frame (%d,%d %dx%d) is not on the %dx%d screen"
                 % (w.x, w.y, w.w, w.h, vw, vh))
        if cols != 80 or rows < 25 or vrows != 25 or vtop != 0:
            fail("the band is %dx%d with %d rows shown from row %d - the "
                 "whole 80x25 must be visible (SPEC.md 96.32.5)"
                 % (cols, rows, vrows, vtop))
        span = w.x == 0 and w.w == vw     # a spanning frame has no side
        content = w.w if span else w.w - 2   # borders, any other keeps both
        if content != cols * 8:           # (11.95.2, 11.95.3)
            fail("the frame is %d wide, content %d against a band of %d: "
                 "%d px of empty window beside the console (SPEC.md 96.32.5)"
                 % (w.w, content, cols * 8, content - cols * 8))
        if cx != w.x + 1 or cx % 8:
            fail("the band starts at x=%d in a frame at x=%d - it should be "
                 "the content origin, on the 8-pixel grid (11.94)"
                 % (cx, w.x))
        if cy + 25 * 8 > w.y + w.h - 1:
            fail("the band's last row ends at y=%d and the frame's bottom "
                 "border is at %d" % (cy + 25 * 8 - 1, w.y + w.h - 1))
        # ...and no taller than it needs to be: the bar, 25 rows, the chrome
        if w.h != 25 * 8 + 20 + 18 + 1:
            fail("the frame is %d tall; 80x25 needs %d" % (w.h, 239))

        # --- 2: the last row is on the glass --------------------------------
        for n in range(16):
            if dos.w("con_cy") >= 24:
                break
            m.type_text("VER\n")
            os88marty.settle(m)
        cur = dos.w("con_cy")
        vtop = dos.w("con_vtop")
        if cur != 24 or vtop != 0:
            fail("could not drive the cursor to the last row with nothing "
                 "scrolled off: cy %d, vtop %d" % (cur, vtop))
        _w, _h, px = m.vram("herc")
        y0 = cy + (cur - vtop) * 8
        if y0 + 8 > vh:
            fail("row 24 is at y=%d, off a %d-line screen" % (y0, vh))
        lit = sum(sum(px[y][cx:cx + 640]) for y in range(y0, y0 + 8))
        print("doshercwin: row 24 at y=%d..%d has %d lit pixels"
              % (y0, y0 + 7, lit))
        if lit < 20:
            fail("the console's last row (y=%d) has %d lit pixels - the "
                 "prompt on it is not on the glass" % (y0, lit))

    print("doshercwin: ok - %dx%d frame snug round 80x25, all 25 rows on "
          "the glass" % (w.w, w.h))


if __name__ == "__main__":
    main()
