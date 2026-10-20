#!/usr/bin/env python3
"""The glass after incremental frames against a forced full redraw of the
same pose - the delta-fill ghost gate (SPEC.md 97.5, 97.10).

    python3 tests/pxs160.py [--machine os8088_5150_cga_gla] [--turns 3]

tests/pxssim.py holds the SHADOW to the host after incremental turn frames;
this row holds THE GLASS to itself: on the 160x100x16 text backend (C160,
the expanding present - every shadow byte laid at every other address of
the character rows, 88.15) and on CGA 320x200x4 (the two-bank row copy), a
textured scene is composed forcibly, turned --turns times with nothing
forced, and the framebuffer at B800 is read; then the SAME pose is redrawn
whole (px_force_all's memory) and read again. The two must be identical:
any writer that bypassed the row range, any column the skip left stale, any
present that sent too few rows shows here as a differing byte, on the
device and not in the shadow. Windowed the same question is asked of the
rendered desktop through the WIN1 blit - BELOW THE MENU BAR, whose clock
ticked over a minute between the two captures of the first leg once (the
leg carries the 0.9 s generation and the 0.6 s transpose, so it is the one
that straddles a minute; 60 RGB bytes of the clock's digits differed and
nothing of the game - tests/pxsfsx.py's slice, review of wave 2's second
fix round).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))     # LAST, so it wins (pxslib)
import os88marty                                                # noqa: E402
import pxslib                                                    # noqa: E402

FAIL = []
B800 = 0xB8000
BAR = 20                        # the menu bar's rows, with the clock in them


def check(ok, what):
    print("   %-66s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def glass(g, windowed):
    g.m.pause()
    if windowed:
        w, h, px = g.m.fbuf(0)
        got = px[BAR * w * 3:]      # the desktop under the menu bar's clock
    else:
        got = g.m.read(B800, 16384)
    g.m.run()
    return got


def leg(g, label, windowed, turns, rung="tex", lowres=True):
    g.pin(rung=rung, lowres=lowres, size=64)
    px, py, head = g.scene("a")
    g.wait_frames(1)
    for _ in range(turns):
        head = (head + pxslib.PX_TURN) & 0xFFF
        g.turn(px, py, head)
        g.wait_frames(1)
    before = glass(g, windowed)
    g.force()                       # the same pose, every column, every row
    g.wait_frames(1)
    after = glass(g, windowed)
    diff = [i for i in range(len(before)) if before[i] != after[i]]
    check(not diff, "%s: %d turn frames then a full redraw leave the glass identical "
          "(%s)" % (label, turns, "all %d bytes" % len(before) if not diff else
                    "%d differ, first at %d" % (len(diff), diff[0])))
    ex, ey, eh = g.eye()
    check((ex, ey, eh) == (px, py, head), "%s: the eye stayed put" % label)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/games360.img")
    ap.add_argument("--turns", type=int, default=3)
    a = ap.parse_args()
    os.chdir(ROOT)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        g = pxslib.open_game(m)
        st = g.state()
        print("   PXSTEIN.O88: window %d, part 0 at %04x, texok %d" % (g.win, g.seg, st["texok"]))
        check(st["texok"] == 1, "the Textured rung is on offer")
        leg(g, "windowed, Textured Low res", True, a.turns)
        leg(g, "windowed, Textured Full", True, a.turns, lowres=False)
        leg(g, "windowed, Flat Low res", True, a.turns, rung="flat")
        # --- the text backend, the row's namesake ---------------------------
        m.pause()
        g.poke_byte("px_mode", 2)           # PXB_C160
        g.poke_byte("px_fsxm", 0)           # FSXM_TEXT80
        m.run()
        g.enter_fsx()
        check(g.state()["back"] == "cga16", "the bracket is the 160x100x16 retime (%s)"
              % g.state()["back"])
        leg(g, "C160, Textured Low res", False, a.turns)
        leg(g, "C160, Textured Full", False, a.turns, lowres=False)
        leg(g, "C160, Flat Low res", False, a.turns, rung="flat")
        g.leave_fsx()
        # --- and CGA 320x200x4, the two-bank copy ---------------------------
        m.pause()
        g.poke_byte("px_mode", 1)           # PXB_CGA4
        g.poke_byte("px_fsxm", 1)           # FSXM_CGA320
        m.run()
        g.enter_fsx()
        check(g.state()["back"] == "cga4", "the bracket is CGA 320x200x4 (%s)" % g.state()["back"])
        leg(g, "CGA4, Textured Low res", False, a.turns)
        leg(g, "CGA4, Textured Full", False, a.turns, lowres=False)
        g.leave_fsx()
    if FAIL:
        print("pxs160: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pxs160: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
