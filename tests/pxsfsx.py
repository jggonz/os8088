#!/usr/bin/env python3
"""The desktop comes back identical after a bracket in every setting (SPEC.md
53, 97.3, 97.10).

    python3 tests/pxsfsx.py [--machine os8088_5150_cga_gla]

Every Mode item this display offers x every Detail rung (Auto, Wire, Flat,
Textured) x both resolutions x three Sizes (48, 64, 80): pin the setting,
enter the bracket, draw a forced frame there, leave; the bracket's mode set,
its regeneration of part 2 for the adapter's phase, its transpose of part 3
and its present must leave nothing behind. At the end the original settings
are pinned again and the rendered desktop (below the menu bar, whose clock
moves) is compared with the one taken before the first bracket: identical,
or the row names how many pixels are not. The window's own state is checked
back too (rung, resolution, Size, Auto's position).

AND A SIZE PICKED NARROWER INSIDE THE BRACKET blacks the glass the wider
band gave up (px_band_blank, 97.3): Size 80 entered, a forced frame drawn,
Size 48 pinned in the bracket and drawn, and every device-row byte outside
the new band read off the framebuffer (pxslib.margins: CGA 320x200x4's two
banks, the Hercules box's four) must be zero - the mode set's clear was the
whole screen's and the present copies Size bytes at x0, so the first cut
left two 16-byte columns of the old picture lit for the rest of the
session (review, wave 2).
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
BAR = 20                        # the menu bar's rows, with the clock in them


def check(ok, what):
    print("   %-66s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def desktop(m):
    m.pause()
    w, h, px = m.fbuf(0)
    m.run()
    return w, h, px[BAR * w * 3:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/games360.img")
    ap.add_argument("--sizes", default="48,64,80")
    a = ap.parse_args()
    os.chdir(ROOT)
    sizes = [int(v) for v in a.sizes.split(",")]
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        g = pxslib.open_game(m)
        g.sim(False)                    # the world frozen and the player safe
        g.god(True)                     # (wave 3): 48 brackets of guest time
                                        # are minutes a guard could use
        st0 = g.state()
        print("   PXSTEIN.O88: window %d, part 0 at %04x, %s" % (g.win, g.seg, st0))
        modes = [g.byte("px_mode0")]
        if g.byte("px_mode1"):
            modes.append(g.byte("px_mode1"))
        fsxtab = g.m.read(g.addr("px_fsxtab"), 5)
        g.scene("a")
        g.wait_frames(1)
        m.advance(frames=40)
        m.run()
        w, h, before = desktop(m)
        n = 0
        for mode in modes:
            for rung in ("auto", "wire", "flat", "tex"):
                for lowres in (False, True):
                    for size in sizes:
                        m.pause()
                        g.poke_byte("px_mode", mode)
                        g.poke_byte("px_fsxm", fsxtab[mode])
                        m.run()
                        g.pin(rung=rung, lowres=lowres, size=size)
                        g.wait_frames(1)
                        g.enter_fsx()
                        g.force()
                        g.wait_frames(1)
                        st = g.state()
                        want_cols = (size // 2) if lowres else size
                        ok = st["inbr"] == 1 and st["back"] == pxslib.PXB[mode] and \
                            st["size"] == size and (rung == "auto" or st["cols"] == want_cols)
                        g.leave_fsx()
                        n += 1
                        if not ok:
                            check(False, "mode %d %s %s size %d: the bracket had the setting (%s)"
                                  % (mode, rung, "low" if lowres else "full", size, st))
        print("   %d brackets entered and left" % n)
        check(g.byte("px_inbr") == 0, "the last bracket was left")
        # --- the shrink inside a bracket: the abandoned margins blacked ------
        m.pause()
        g.poke_byte("px_mode", modes[0])
        g.poke_byte("px_fsxm", fsxtab[modes[0]])
        m.run()
        g.pin(rung="tex", lowres=True, size=80)
        g.wait_frames(1)
        g.enter_fsx()
        g.force()
        g.wait_frames(1)
        st = g.state()
        check(st["size"] == 80 and st["inbr"] == 1, "the bracket is at Size 80 (%s)" % st)
        g.pin(rung="tex", lowres=True, size=48)     # V, in the bracket
        g.wait_frames(1)
        g.force()
        g.wait_frames(1)
        m.pause()
        st = g.state()
        mg = g.margins()
        m.run()
        check(st["size"] == 48 and st["inbr"] == 1, "...then at Size 48 (%s)" % st)
        if mg is None:
            print("   (the margins are not modelled on %s: the shrink leg reads state alone)"
                  % st["back"])
        else:
            lit = sum(1 for b in mg if b)
            check(lit == 0, "the %d device bytes outside the 48-byte band are black on %s "
                  "after the shrink (%d lit)" % (len(mg), st["back"], lit))
        g.leave_fsx()
        n += 1
        # the original settings back, then the desktop
        m.pause()
        g.poke_byte("px_mode", modes[0])
        g.poke_byte("px_fsxm", fsxtab[modes[0]])
        m.run()
        g.pin(rung="auto", lowres=False, size=st0["size"])    # (48 since the fork
        g.scene("a")                                            # moved, 97.1)
        g.wait_frames(1)
        m.advance(frames=40)
        m.run()
        st = g.state()
        check((st["rung"], st["lowres"], st["size"], st["apos"], st["back"]) ==
              (st0["rung"], st0["lowres"], st0["size"], st0["apos"], st0["back"]),
              "the window's state is what it was (%s vs %s)" % (st, st0))
        w2, h2, after = desktop(m)
        diff = sum(1 for i in range(0, len(before), 3) if before[i:i + 3] != after[i:i + 3])
        check((w, h) == (w2, h2) and diff == 0,
              "the desktop below the menu bar is identical after every setting's bracket "
              "(%d of %d pixels differ)" % (diff, len(before) // 3))
    if FAIL:
        print("pxsfsx: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pxsfsx: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
