#!/usr/bin/env python3
"""Does the pointer still change SHAPE over a window that asks for one?

    make && python3 tests/curshape.py

SPEC.md 7.2: a window's cursor shape rides in the high byte of `W_FLAGS`, and
`cur_shape_pass` asks once per UI pass which window the pointer is over and
whether that window wants a different picture. Missile Command asks for the
crosshair, because the pointer IS the gunsight (SPEC.md 48).

WHY THIS FILE EXISTS. `cur_shape_pass` used to detect pointer movement by
comparing the live position against the last one it answered for - three word
compares behind a call, on every pass, 671 times a second. SPEC.md 7.2.1.1
made `[cur_shchk]` authoritative instead: whatever MOVES the pointer sets it,
and the UI ladder tests that one byte. There are four such movers - the mouse
report, the keyboard mouse, the menu's warp and the boot's cursor home - and
**a fifth one added later that forgets the flag is a pointer that silently
stops changing shape**. Nothing in tests/ covered the shape at all when that
change was made, which is why it is covered now.

The three positions are one assertion each, and the second is the one that
catches a flag stuck ON rather than off:

  * inside Missile Command's CONTENT   -> the crosshair; the app's to dress
  * on its own TITLE BAR               -> the arrow; chrome is the kernel's
  * on the bare DESKTOP                -> the arrow
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

S = os88sym.linear
CUR_ARROWSH, CUR_CROSSSH = 0, 1


def shape(m):
    return m.read(S("cur_shape"), 1)[0]


def name(v):
    return {CUR_ARROWSH: "arrow", CUR_CROSSSH: "CROSS"}.get(v, "?%d" % v)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    say = lambda s: print("  " + s, flush=True)
    bad = []
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy, _, _ = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "GAMES")
        os88marty.settle(m)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "MISSILE.O88")
        os88marty.settle(m)
        mx, my, mw, mh = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        say("Missile Command at (%d,%d) %dx%d" % (mx, my, mw, mh))
        for where, (px, py), want in (
                ("its CONTENT",   (mx + mw // 2, my + mh // 2), CUR_CROSSSH),
                ("its TITLE BAR", (mx + mw // 2, my + 4),       CUR_ARROWSH),
                ("the DESKTOP",   (8, 190),                     CUR_ARROWSH)):
            mo.to(px, py)
            os88marty.settle(m)
            got = shape(m)
            say("pointer over %-14s cur_shape = %d %s (want %s)"
                % (where, got, name(got), name(want)))
            if got != want:
                bad.append("%s: got %s, want %s" % (where, name(got), name(want)))
    for b in bad:
        say("FAIL " + b)
    print("curshape: %s" % ("the pointer's shape tracks the window under it "
                            "- PASS" if not bad else "the shape does NOT track"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
