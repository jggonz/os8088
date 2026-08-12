#!/usr/bin/env python3
"""Does a window dragged across the seam give back the rows only ONE display
has? (SPEC.md 39.16.3)

    make && python3 tests/dispstrad.py

The virtual desktop is a UNION and not a rectangle (SPEC.md 39.2.1), so past
the seam the two displays stop agreeing about which rows exist: a 720x348
Hercules beside a 640x200 CGA has rows 220..347 on the left and nothing at all
on the right. `wm_fit` never lets a window meet that - it confines a frame to
the box of the display its ORIGIN is on (39.16.1) - but a DRAG is deliberately
allowed to cross, and a window tall enough to use those rows then has its
bottom-right corner in the dead zone: drawn nowhere, clickable nowhere.

THE ASSERTION IS ARITHMETIC AND NOT A SCREENSHOT, because there is nothing to
photograph: the dead zone is where nothing is drawn, so a capture of a broken
build and a capture of a fixed one differ only in the part of the window that
was never on either monitor. What the rule promises is that the frame's last
row is one the second display actually has, and that is a compare.

...and it drags a window that is ALREADY too tall rather than growing one
first. The Disk window's template is 320x200 at y=80, so on the Hercules it
keeps all 200 rows and ends at row 280 - sixty rows past the CGA's last. No
grow box needed, and one fewer thing to go wrong before the thing under test.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402

TITLE_H = 18
VID_CTX_SZ, VID_CTX_CW, VID_CTX_CH = 42, 14, 16
VID_CTX_VX, VID_CTX_VY = 36, 38
S = os88sym.linear


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def ctx(m, d):
    return m.read(S("vid_ctx") + d * VID_CTX_SZ, VID_CTX_SZ)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)

    fail = []
    say = lambda s: print("  " + s)
    settle = os88marty.settle
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        if len(m.cards()) != 2:
            sys.exit("dispstrad: %s is not a two-card machine" % a.machine)
        m.run()
        settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_panel(m, mo, S, settle)
        dispcp.set_primary(m, mo, S, settle, 0)             # Hercules primary,
        dispcp.set_mode(m, mo, S, settle, "right")          # as the field is
        dispcp.close_panel(m, mo, S, settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("dispstrad: the Control Panel did not turn Extend on")

        c0, c1 = ctx(m, 0), ctx(m, 1)
        seam = u16(c1, VID_CTX_VX)
        bot1 = u16(c1, VID_CTX_VY) + u16(c1, VID_CTX_CH)
        bot0 = u16(c0, VID_CTX_VY) + u16(c0, VID_CTX_CH)
        say("display 0 %dx%d at (%d,%d), display 1 %dx%d at (%d,%d)"
            % (u16(c0, VID_CTX_CW), u16(c0, VID_CTX_CH),
               u16(c0, VID_CTX_VX), u16(c0, VID_CTX_VY),
               u16(c1, VID_CTX_CW), u16(c1, VID_CTX_CH), seam,
               u16(c1, VID_CTX_VY)))
        if bot1 >= bot0:
            sys.exit("dispstrad: display 1 is not the SHORTER one (%d vs %d), "
                     "so there are no rows for this rule to give back"
                     % (bot1, bot0))

        dispcp.open_drive(m, mo, S, settle, "B", card=0)
        w = dispcp.win_list(m, S)
        if not w:
            sys.exit("dispstrad: no Disk window")
        slot = w[-1]
        wx, wy, ww, wh = dispcp.win_rect(m, S, slot)
        say("the window on display 0: (%d,%d) %dx%d, last row %d against "
            "display 1's %d" % (wx, wy, ww, wh, wy + wh, bot1 - 1))
        if wy + wh <= bot1:
            sys.exit("dispstrad: it already fits display 1's rows, so dragging "
                     "it across proves nothing")

        # ...across the seam. Two thirds of it over the second card, so the
        # straddle is unambiguous and the title bar stays reachable.
        mo.drag(wx + ww // 2, wy + TITLE_H // 2,
                seam - ww // 3 + ww // 2, wy + TITLE_H // 2)
        settle(m, card=1)
        nx, ny, nw, nh = dispcp.win_rect(m, S, slot)
        say("...dragged to (%d,%d) %dx%d, last row %d" % (nx, ny, nw, nh,
                                                          ny + nh))
        if not nx < seam < nx + nw - 1:
            sys.exit("dispstrad: the window does not straddle the seam")

        if ny + nh > bot1:
            fail.append("its last row is %d and display 1 ends at %d, so %d "
                        "row(s) of its right-hand side are in the DEAD ZONE "
                        "(SPEC.md 39.16.3)" % (ny + nh, bot1 - 1,
                                               ny + nh - bot1 + 1))
        elif nh == wh:
            fail.append("the height did not change at all (%d), which means "
                        "this run never had anything to give back" % nh)
        else:
            say("height %d -> %d: the frame now ends on a row display 1 has"
                % (wh, nh))

        # ...and the WIDTH is untouched, because side-by-side displays tile in
        # x and spanning them is the entire point of extending.
        if nw != ww:
            fail.append("the width moved %d -> %d: only the axis the displays "
                        "OVERLAP in may be narrowed (SPEC.md 39.16.3)"
                        % (ww, nw))

    print()
    for f in fail:
        print("dispstrad: FAIL: %s" % f)
    if fail:
        return 1
    print("dispstrad: a straddling window gives back the rows only one display "
          "has, and keeps its width - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
