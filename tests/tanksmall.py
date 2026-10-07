#!/usr/bin/env python3
"""TANK ATTACK's `APP_SMALL` arm plays on the 128KB machine (SPEC.md 85.3.5.1).

    python3 tests/tanksmall.py [--machine os8088_5150_cga_128k]

The shipped package claims a flat 32KB for its shadow and its HUD template,
and a 128KB machine has ~20KB of largest run once mem_claim has shed the
purgeable caches - so it used to switch the video mode, fail the claim and
bounce back to the desktop with no message at all. That is the whole of why
SPEC.md 24.5 kept it off the small disks, under a reason ("the fullscreen
surface") that was wrong: kern_small has every byte of SPEC.md 53.

`make smallapps` builds the arm that fits - the template as a SPAN STORE and
the claim off a ladder - and this is the only thing that drives it. Nothing in
`all` builds it, `tests/tank.py` and its two neighbours run the SHIPPED
package, and every path this file asserts on (tk_tmenc, tk_tmspans,
tk_tmrowput, the ladder) is compiled out of that one.

WHAT EACH CHECK CATCHES, because a green row nobody can break is worse than no
row (docs/WRITING-TESTS.md 1):

  the claim         `tk_shseg` != 0. Put TK_SHKB back to 32 and this is the
                    failure the whole change exists to remove - and it is the
                    one that used to be SILENT, because tk_r_setup's CF=1 ends
                    the bracket before a frame is drawn
  the rung          which rung the ladder actually took, reported rather than
                    asserted: 18 is what a 128KB machine measures today, and a
                    kernel that grows may push it to 17 without this being
                    wrong. What IS asserted is that the pool it came with is
                    big enough to hold the store
  the store is USED `tk_tmpl` = 1 after turns and a crack. This is the check
                    with the most reach: the store is rebuilt by tk_tmenc on
                    every template change, and an encode that will not fit
                    clears this byte and drops the panel back to being drawn
                    every frame. Break the gap rule, the pool size or
                    tk_tmenc's park/encode/unpark arithmetic and it lands here
  the pool          `tk_tmlen` <= `tk_tmcap`, which is the invariant tk_tmenc
                    is supposed to enforce rather than a budget
  it DRAWS          tk_frames climbs, and the glass differs between two
                    samples a second apart - tests/tank.py's pair, for its
                    reason: a page that never latches leaves a counter
                    climbing over a still picture and a hang leaves the other
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "unit"))

# BEFORE the imports, not inside main(): os88sym resolves kernel symbols against
# $OS88_BUILD/$OS88_DEFINES at IMPORT time, so setting them later leaves this
# row reading kern_big's map against a kern_small guest - which does not raise,
# it just quietly fails to find the window it opened.
os.environ["OS88_BUILD"] = "build/smallk"
os.environ["OS88_DEFINES"] = "KERN_SMALL"

import os88ui                                               # noqa: E402
import os88marty                                            # noqa: E402
import dispapps                                             # noqa: E402
from harness import check, done                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TURNS = ("KeyA", "KeyD", "KeyA")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_128k")
    ap.add_argument("--image", default="build/small360.img")
    ap.add_argument("--apps", default="build/smallapps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)

    def off(n):
        return dispapps.bss_off("tank", n, small=True)

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.open_drive("B")
        ui.open("GAMES")
        w = ui.open("TANK.O88")
        ui.raise_window(w)
        m.advance(frames=120)
        m.run()
        got = dispapps.pkg_seg(m, 0)
        check(got is not None, "TANK.O88 is on the small apps disk and opens",
              "SMALLPKGS carries the APP_SMALL build and SMALLOMIT_GAMES no "
              "longer names tank; a disk built with neither has nothing here")
        if got is None:
            return done("tanksmall")
        slot, seg = got
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")

        def word(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")

        def byte(n):
            return m.readseg(seg, base + off(n), 1)[0]

        ui.menu_pick("Game", "Play")
        m.advance(frames=300)
        m.run()
        try:                            # the shadow claimed and a frame drawn;
            os88marty.until(            # the checks below say which did not
                m, lambda _: word("tk_shseg") != 0 and word("tk_frames") > 0,
                "the game to claim its shadow and draw", poll=0.2, limit=20)
        except os88marty.MartyError:
            pass

        check(word("tk_shseg") != 0,
              "the shadow claim was GRANTED on a %s machine" % a.machine,
              "tk_r_setup returns CF=1 and tk_fsx_main leaves the bracket "
              "before it draws, which is a mode switch, a blank screen and a "
              "bounce back to the desktop with nothing said - the failure "
              "SPEC.md 85.3.5.1 exists to remove",
              got=word("tk_shseg"), want="a segment")
        rung, cap = byte("tk_shkb"), word("tk_tmcap")
        print("  ladder rung %d KB, %d bytes of span pool" % (rung, cap))
        check(cap >= 1024, "the rung it took carries a usable pool",
              "16KB is the bottom rung - the shadow alone and the pre-85.3.5 "
              "game - so landing there means the arena moved under us and the "
              "rows below are measuring a build with no template at all",
              got=cap, want=">= 1024 bytes")

        def state(tag):
            tm, ln = byte("tk_tmpl"), word("tk_tmlen")
            print("  %-14s tmpl=%d pool %d/%d frames %d"
                  % (tag, tm, ln, cap, word("tk_frames")))
            check(tm == 1, "%s: the span store is still in use" % tag,
                  "tk_tmenc clears [tk_tmpl] when the pool will not hold the "
                  "store, and the panel goes back to being drawn every frame - "
                  "correct, and 61-67 ms a frame slower (SPEC.md 85.3.5)",
                  got=tm, want=1)
            check(ln <= cap, "%s: the store fits its pool" % tag,
                  "tk_tmenc's own invariant, not a budget: it refuses a write "
                  "that would pass the cap", got=ln, want="<= %d" % cap)

        state("in play")
        for k in TURNS:                 # a ridge transition each way, which is
            m.key(k, down=True, up=False)   # what re-encodes the widest band
            m.advance(frames=120)
            m.run()
            m.key(k, down=False, up=True)
            m.advance(frames=180)
            m.run()
        state("after turns")
        m.write((seg << 4) + base + off("tk_dead"), b"\x01")
        m.advance(frames=200)
        m.run()
        state("cracked")

        f0 = word("tk_frames")
        s0 = m.fbuf(None)[2]
        os88marty.pace(m, 1.5)
        m.run()
        f1 = word("tk_frames")
        s1 = m.fbuf(None)[2]
        check(f1 > f0, "it is still drawing", "tk_frames stopped climbing, so "
              "the loop is not running", got="%d -> %d" % (f0, f1), want="more")
        check(s0 != s1, "and the glass ADVANCES",
              "a counter climbing over a still picture is a page that never "
              "reaches the screen - tests/tank.py's own pair of questions",
              got="identical", want="two different frames")
    return done("tanksmall")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
