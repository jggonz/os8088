#!/usr/bin/env python3
"""DOES A ZOOM LAND FLUSH ON AN EXTENDED DESKTOP? (SPEC.md 11.95.2.1)

    make && python3 tests/dispzoom.py [--machine os8088_5150_both_gla]

SPEC.md 11.95's standard rect is x = 0, w = the display's width, and 11.95.2
is the eight columns that buys back: before it, a maximized window sat with
seven columns of desktop dither showing down its left, because 11.94's snap
moved it there to land the content origin on a byte boundary that x = 0
already was.

**A SECOND CARD BROUGHT ALL OF THAT BACK**, and by a road neither section
could have known about. `wm_snap_ax` moves a window at x = 0..6 right to the
nearest aligned x and refuses when `x + W_W` would then hang off the screen -
a test written against `[vid_w]`, which before SPEC.md 39.16 WAS the screen.
On a two-card desktop it is the SUM, so the refusal that keeps a maximized
window at 0 on one display (7 + 640 > 640) stops firing on two
(7 + 640 <= 1360) and the window walks. `wm_flush_ck` then reads it as not
spanning the screen and puts the left border back on top of the gap.

So this row zooms the same window twice - once on a single display, once with
Extend on - and requires the SAME answer. The single-display half is the
control: it is what makes a failure "the second display broke it" rather than
"zoom is broken".

  ASSERTED:  zoomed on one display, the frame is flush at the left edge
             ...and exactly the primary's width
             zoomed on TWO, the frame is in the same place and the same size

It reads the window RECORD and not the glass, because the failure is 7 px of
geometry and a screenshot of a mostly-white window does not show it.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402

ROOT = os.path.dirname(HERE)
S = os88sym.linear


def u16(b, o=0):
    return int.from_bytes(b[o:o + 2], "little")


def zoom(m, mo, slot):
    r = dispcp.win_rect(m, S, slot)
    mo.dblclick(r[0] + 60, r[1] + 9)            # the title bar, not a box
    m.advance(frames=400)
    m.run()
    os88marty.settle(m)
    return dispcp.win_rect(m, S, slot)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)

    fails = []
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        if len(m.cards()) != 2:
            sys.exit("dispzoom: %s has %d video card(s), not 2"
                     % (a.machine, len(m.cards())))
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        slot = dispcp.win_list(m, S)[-1]

        pw = u16(m.read(S("vid_pw"), 2))
        one = zoom(m, mo, slot)
        print("   ONE display  (vid_pw=%d vid_w=%d): zoomed to W_X=%d W_W=%d"
              % (pw, u16(m.read(S("vid_w"), 2)), one[0], one[2]))
        if one[0] != 0:
            fails.append("on ONE display the zoom landed at x=%d, not 0 - "
                         "SPEC.md 11.95.2 is broken before the second card is "
                         "even attached" % one[0])
        if one[2] != pw:
            fails.append("on ONE display the zoom is %d wide and the display "
                         "is %d (SPEC.md 11.95)" % (one[2], pw))
        zoom(m, mo, slot)                       # ...and back, for a clean start

        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("dispzoom: the Control Panel did not turn Extend on")
        vw = u16(m.read(S("vid_w"), 2))
        two = zoom(m, mo, slot)
        print("   TWO displays (vid_pw=%d vid_w=%d): zoomed to W_X=%d W_W=%d"
              % (pw, vw, two[0], two[2]))
        if vw <= pw:
            sys.exit("dispzoom: Extend is on but vid_w (%d) is not past the "
                     "primary (%d), so nothing here is being tested" % (vw, pw))
        if two[0] != one[0] or two[2] != one[2]:
            fails.append(
                "the SECOND DISPLAY moved the zoom: %dx at %d on one display, "
                "%dx at %d on two. [vid_w] is the whole desktop and a screen "
                "edge is not (SPEC.md 11.95.2.1)"
                % (one[2], one[0], two[2], two[0]))

    if fails:
        for f in fails:
            print("dispzoom: %s" % f)
        return 1
    print("dispzoom: PASS - the zoom lands at x=%d, %d wide, with one display "
          "and with two" % (one[0], one[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
