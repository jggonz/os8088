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

THE PICTURES ARE TAKEN AT THE WORKER'S FRAME BOUNDARIES, with the world
frozen (px_simoff). The first cut counted px_frames from a mark and ran the
sim: a guard's step owes a frame every few seconds in scene A, so a frame
already IN FLIGHT when a poke landed answered the wait, and the glass was
read a pose behind or torn mid-present - one run in two on the windowed
Textured Full leg (7,425 RGB bytes), and once on C160 Textured Full (666),
with the renderer drawing both pictures right. Every poke is now made with
the guest stopped at px_frame_begin or px_frame_end, and every read follows
a frame that ENDED with nothing owed (pxslib's hold/drawn). Broken on
purpose - the Textured delta frame's ceiling put-back skipped, the forced
frame's left alone - every Textured leg goes red (2,226-2,451 RGB bytes
windowed, 256-264 on C160, 237-238 on CGA4) and the Flat ones stay green.
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
    """The picture, read off a guest drawn() left STOPPED after a frame that
    ended with nothing owed. The bracket's is the framebuffer itself; the
    window's is the RENDERED desktop, which MartyPC finishes a raster after
    the bytes land - so two CRT frames are run first (the sim frozen and
    nothing owed: the worker draws nothing in them) and the guest is then
    stopped between frames again (hold), ready for the next poke."""
    if windowed:
        g.m.advance(frames=2)
        w, h, px = g.m.fbuf(0)
        got = px[BAR * w * 3:]      # the desktop under the menu bar's clock
        g.hold()
    else:
        got = g.m.read(B800, 16384)
    return got


def leg(g, label, windowed, turns, rung="tex", lowres=True):
    """Every poke is made with the guest STOPPED between two frames and every
    read after a frame that ended with nothing owed (pxslib's hold/drawn):
    the first cut counted px_frames from a mark, so a frame already in
    flight answered the wait and the glass was read a pose behind, or torn
    mid-present - one run in two on the windowed Textured Full leg. The
    world is frozen (px_simoff, main), so nothing but the pokes moves."""
    m = g.m
    g.hold()
    g.poke_byte("px_detail", pxslib.PXD[rung])     # pin(): the pick, taken
    g.poke_byte("px_res", 1 if lowres else 0)      # by px_apply between
    g.poke_byte("px_sizeix", pxslib.SIZES.index(64))   # frames (px_pend)
    g.poke_byte("px_pend", 1)
    px, py, head = pxslib.scene_at("a")            # scene(): the pose, owed
    g.eye_poke(px, py, head)                       # WHOLE
    g.pcell_poke(px, py)
    for k in ("px_tap", "px_kturn", "px_kfwd", "px_kstr"):
        g.poke_byte(k, 0)
    g.force_all_poke()
    g.drawn()
    forced = []
    for _ in range(turns):                         # turn(): the heading with
        head = (head + pxslib.PX_TURN) & 0xFFF     # px_dirty and NOTHING
        g.eye_poke(px, py, head)                   # forced - the delta fill
        g.pcell_poke(px, py)
        g.poke_byte("px_dirty", 1)
        g.drawn()
        forced.append(g.byte("px_fown"))           # the frame's own force:
                                                   # 0, or it was no delta
    before = glass(g, windowed)
    g.force_all_poke()              # the same pose, every column, every row
    g.drawn()
    after = glass(g, windowed)
    m.run()
    diff = [i for i in range(len(before)) if before[i] != after[i]]
    check(not diff, "%s: %d turn frames then a full redraw leave the glass identical "
          "(%s)" % (label, turns, "all %d bytes" % len(before) if not diff else
                    "%d differ, first at %d" % (len(diff), diff[0])))
    check(not any(forced), "%s: the turn frames were deltas, none forced (%s)"
          % (label, forced))
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
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.kernel_up(m)          # open_game's first act is a click
        g = pxslib.open_game(m)
        st = g.state()
        print("   PXSTEIN.O88: window %d, part 0 at %04x, texok %d" % (g.win, g.seg, st["texok"]))
        check(st["texok"] == 1, "the Textured rung is on offer")
        g.sim(False)                    # the world frozen: no guard, door or
                                        # weapon moves between the pictures
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
