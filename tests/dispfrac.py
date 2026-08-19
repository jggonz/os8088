#!/usr/bin/env python3
"""Does apps/fractal's restore cache survive an adapter change? (SPEC.md 40.1)

    make && python3 tests/dispfrac.py

THIS IS A GATE ON A CONCERN THAT TURNED OUT NOT TO REPRODUCE, and it is worth
having for exactly that reason: the reasoning that raised it was plausible and
wrong, so the next reader deserves a test rather than a paragraph.

The concern was that `fr_setup` re-derives `fr_cw` / `fr_ch` from the live
content box while `fr_kick` is the only thing that resets the run cache - so
an adapter change could leave the cache holding rows laid out for the old
width, and `fr_replay` would put them back at the new one. Fractal's window is
322x199 and a CGA's desktop band is 155, so the box really does move: content
320x180 becomes 320x136.

TWO THINGS ALREADY STOP IT, and this measures both.

`fr_redraw` banks the dimensions the cache was BUILT for - `fr_ccw` / `fr_cch`
- and compares them against the live box before it replays anything, falling
through to `fr_kick` when they differ. It runs from `fr_paint`, and an adapter
change ends in `wm_paint_all` (SPEC.md 39.11.2), so every window's W_PAINT is
called and Fractal's asks that question.

And `fr_worker` calls `fr_setup` only when `[fr_restart]` is non-zero, so
between the switch and the repaint it keeps rendering at the width the cache
was built for rather than half-way to the new one. The window where a mixed
cache could exist is therefore closed at both ends.

WHAT IS ASSERTED is the invariant those two maintain: after the dust settles
the cache's own dimensions equal the live content box, on both adapters and
after coming back. A cache that disagreed with the box by even one column is
the bug this was parked for.
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
import dispfit                                              # noqa: E402
import dispapps                                             # noqa: E402

S = os88sym.linear
VID_VGA, VID_CGA = 0, 2
TITLE_H = 18
WATCH = ["fr_cw", "fr_ch", "fr_ccw", "fr_cch", "fr_cn", "fr_prog", "fr_ckb"]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)

    fail = []
    say = lambda s: print("  " + s)
    settle = os88marty.settle
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        mo = os88mouse.Mouse(marty=m)
        if dispfit.kind(m) != VID_VGA:
            sys.exit("dispfrac: %s did not come up on the VGA" % a.machine)

        dispcp.open_drive(m, mo, S, settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        wx, wy, ww, wh = dispcp.win_rect(m, S, disk)
        dispcp.open_named(m, mo, S, settle, wx, wy, "APPS")
        seg = None
        for _ in range(3):
            wx, wy, ww, wh = dispcp.win_rect(m, S, disk)
            mo.click(wx + 40, wy + TITLE_H // 2)
            settle(m)
            dispcp.open_named(m, mo, S, settle, wx, wy, "FRACTAL.O88")
            m.advance(frames=200)                           # ...and let it get
            m.run()                                         # some rows down
            got = dispapps.pkg_seg(m, 0)
            if got is not None:
                seg = got[1]
                slot = got[0]
                break
        if seg is None:
            sys.exit("dispfrac: fractal did not open")

        def look(tag):
            m.advance(frames=200)                   # a render is never still,
            m.run()                                 # so this cannot settle()
            w = dispapps.words(m, seg, "fractal", WATCH)
            r = dispcp.win_rect(m, S, slot)
            say("%-4s rect %r  %r" % (tag, r, w))
            box = (r[2] - 2, r[3] - TITLE_H - 1)
            if (w["fr_cw"], w["fr_ch"] + 10) != box:    # FR_STRIP_H = 10
                fail.append("%s: fr_cw/fr_ch %dx%d does not describe the "
                            "content box %dx%d"
                            % (tag, w["fr_cw"], w["fr_ch"], box[0], box[1]))
            if w["fr_cn"] and (w["fr_ccw"], w["fr_cch"]) != (w["fr_cw"],
                                                             w["fr_ch"]):
                fail.append("%s: the cache holds %d entries laid out for "
                            "%dx%d inside a %dx%d content box - fr_redraw's "
                            "compare did not fire (SPEC.md 40.1)"
                            % (tag, w["fr_cn"], w["fr_ccw"], w["fr_cch"],
                               w["fr_cw"], w["fr_ch"]))
            return w

        before = look("VGA")
        dispcp.open_panel(m, mo, S, settle)
        dispfit.switch_to(m, mo, settle, 1, VID_CGA)
        during = look("CGA")
        if during["fr_ch"] >= before["fr_ch"]:
            fail.append("the content box did not shrink (%d -> %d), so this "
                        "run had nothing to test"
                        % (before["fr_ch"], during["fr_ch"]))
        dispfit.switch_to(m, mo, settle, 0, VID_VGA)
        after = look("VGA")
        if after["fr_ch"] != before["fr_ch"]:
            fail.append("the box did not come back: %d against %d"
                        % (after["fr_ch"], before["fr_ch"]))
        if not after["fr_prog"]:
            fail.append("it is not rendering any more after the round trip "
                        "(fr_prog = 0)")

    print()
    for f in fail:
        print("dispfrac: FAIL: %s" % f)
    if fail:
        return 1
    print("dispfrac: the restore cache and the content box agree on both "
          "adapters and after coming back - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
