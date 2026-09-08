#!/usr/bin/env python3
"""TANK ATTACK (SPEC.md 85) runs, and its frames do not flash.

    python3 tests/tank.py [--machine os8088_5150_cga_gla]

THREE ASSERTIONS, and they are three different questions.

**It draws.** `tk_frames` must climb. A foreign-mode bracket that has stopped
looks exactly like one that is drawing into a page nobody is showing, which is
how SPEC.md 53.10's own failure mode presents - so the frame counter is read
out of the package's bss rather than inferred from the glass.

**It ADVANCES.** The glass must differ between two samples taken a second
apart. That is the other half of the same question and neither half implies
the other: a page flip that never latches leaves a counter climbing over a
still picture, and a hang leaves a still picture with a still counter.

**It does not flash**, which is the whole reason SPEC.md 85.1 has the design
it has. The instrument is `pace`, which samples once per DISPLAYED frame:
what must never happen is a displayed frame with almost NOTHING on it, which
is what a screen cleared and redrawn in front of the raster looks like
(SPEC.md 78.5 measures exactly that on the windowed side, where the emptiest
frame of "whole figure" ordering is 0% ink). So the ink is sampled per
displayed frame and its FLOOR is asserted against its median, not its mean -
a mean hides a dip and a dip is the defect.

It is a GATE and not a measurement: the frame rate is in
docs/plans/completed/GFX-FSX-PLAN.md, because a number that fails a build when a container
gets slower teaches nobody anything.
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
import dispapps                                             # noqa: E402
import dispcp                                               # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRAMES = 40                             # displayed frames sampled for the floor
FLOOR = 70                              # ...and the share of its NEIGHBOURS'
                                        # ink a frame may not go under. A
                                        # screen cleared in front of the raster
                                        # reads near ZERO on the frames that
                                        # catch it - SPEC.md 78.5 measures a
                                        # windowed version of exactly that at
                                        # 0% ink and 56% of frames under half
                                        # full - so the margin here is for a
                                        # blit the raster catches part-way
                                        # down, which is tearing and not
                                        # flashing: nothing was erased, so what
                                        # shows is two pictures rather than a
                                        # gap in one
TK_LIVES = 4                            # apps/tank's own TK_LIVES
TK_GRACE = 115                          # ...and its grace, turn rate and the
TK_CURVE0 = 36                          # first step of the difficulty curve
TK_TURN = 2
TK_LGCELLS = 11                         # ...and tklogo.inc's cells in the word
POP = bytes(bin(i).count("1") for i in range(256))


def lit(fb):
    """Lit pixels in a framebuffer sample. A 2bpp CGA byte counts its bits,
    which over-counts white against cyan by two to one - consistently, which
    is all a FLOOR against a MEDIAN needs."""
    n = 0
    for b in fb:
        n += POP[b]
    return n


def open_game(m):
    """Boot to the desktop and get TANK.O88 open. Returns (slot, seg, bss)."""
    S = os88sym.linear
    os88marty.settle(m)
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    wx, wy = dispcp.win_rect(m, S, disk)[:2]
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "GAMES")
    wx, wy = dispcp.win_rect(m, S, disk)[:2]
    rows = [r[0] for r in dispcp.listing(m, S)]
    if "TANK.O88" not in rows:
        sys.exit("tank: TANK.O88 is not on the apps disk")
    row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                           rows.index("TANK.O88"))
    x, y = dispcp.row_xy(wx, wy, row)
    # NOT open_named: it settles, and a window whose game is running never
    # settles again once the bracket is up.
    mo.dblclick(x, y)
    m.advance(frames=200)
    m.run()
    got = dispapps.pkg_seg(m, 0)
    if got is None:
        sys.exit("tank: TANK.O88 did not open")
    slot, seg = got
    base = int.from_bytes(m.readseg(seg, 8, 2), "little")
    return slot, seg, base


def word(m, seg, base, name):
    return int.from_bytes(
        m.readseg(seg, base + dispapps.bss_off("tank", name), 2), "little")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    bad = []

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        slot, seg, base = open_game(m)
        print("  TANK.O88: window %d, segment %04x, bss at %04x"
              % (slot, seg, base))
        # --- the attract window animates (SPEC.md 85.10) --------------------
        # Cheapest claim first: the worker got hired. task_spawn refuses from
        # an entry proc and tk_hire retries on every paint, so a failure here
        # is silent - the window draws once and then simply never moves, which
        # reads as a design decision rather than as a bug.
        m.advance(frames=60)
        m.run()
        if not (word(m, seg, base, "tk_hired") & 0xFF):
            bad.append("the attract worker was never spawned: the panel is a "
                       "still picture and nothing below it is measurable")
        prog = word(m, seg, base, "tk_at_prog")
        glow = word(m, seg, base, "tk_at_glow") & 0xFF
        print("  attract: drawn in %d/%d cells, glow=%d"
              % (prog, TK_LGCELLS, glow))
        if prog != TK_LGCELLS or not glow:
            bad.append("the logo did not finish drawing in (%d of %d, glow=%d)"
                       % (prog, TK_LGCELLS, glow))
        # ...and then it never stops: the gleam walks the stroke and the score
        # band rolls. Both are read as a CHANGE and not as a direction - the
        # cursor wraps 53 -> 0 and the band 5 -> 0, so a > test fails one lap
        # in six for no reason.
        h0 = word(m, seg, base, "tk_lgi")
        s0 = word(m, seg, base, "tk_at_scr")
        m.advance(frames=240)
        m.run()
        h1 = word(m, seg, base, "tk_lgi")
        s1 = word(m, seg, base, "tk_at_scr")
        print("  attract: gap on cell %d -> %d, band row %d -> %d"
              % (h0, h1, s0, s1))
        if h0 == h1:
            bad.append("the gaps never left cell %d: a letter takes about a "
                       "second and this waited four (SPEC.md 85.10.1)" % h0)
        if s0 == s1:
            bad.append("the score band did not roll (row %d both times)" % s0)

        m.type_text("f")                        # into the bracket
        m.advance(frames=45)                    # THE GRACE IS READ EARLY, and
        m.run()                                 # has to be: tk_ocool counts
        early = m.readseg(seg, base + dispapps.bss_off("tank", "tk_ocool"), 18)
        etyp = m.readseg(seg, base + dispapps.bss_off("tank", "tk_otype"), 18)
        m.advance(frames=105)                   # DOWN from its start, so a
        m.run()                                 # sample taken late says
                                                # nothing about where it began

        back = word(m, seg, base, "tk_back") & 0xFF
        vw = word(m, seg, base, "tk_vw")
        vh = word(m, seg, base, "tk_vh")
        print("  backend %d, viewport %dx%d" % (back, vw, vh))
        if back == 0:
            bad.append("no raster was adopted: the bracket refused its mode")

        # --- the difficulty curve, and the clock the player moves on --------
        # The first tank's grace is TK_GRACE plus the beginner's allowance
        # (SPEC.md 85.6.3), and it counts DOWN from there, so this is read as
        # a ceiling rather than an equality.
        live = [i for i in range(12, 18) if etyp[i] == 4]
        n = word(m, seg, base, "tk_ntank")
        print("  tanks faced %d, first tank's grace at 45 frames %s (starts "
              "at %d, TK_GRACE is %d)"
              % (n, [early[i] for i in live], TK_GRACE + TK_CURVE0, TK_GRACE))
        if n != 1:
            bad.append("tk_ntank is %d after one tank: the difficulty curve "
                       "reads the wrong number" % n)
        if not live:
            bad.append("no tank had spawned 45 frames into the bracket")
        for i in live:
            if early[i] > TK_GRACE + TK_CURVE0:
                bad.append("a fresh tank's grace is %d, over the %d the curve "
                           "allows" % (early[i], TK_GRACE + TK_CURVE0))
            if early[i] <= TK_GRACE:
                bad.append("the first tank's grace was down to %d after 45 "
                           "frames, at or under TK_GRACE (%d): the beginner's "
                           "allowance of SPEC.md 85.6.3 is not being added"
                           % (early[i], TK_GRACE))

        # THE PLAYER TURNS ON THE TICK, NOT ON THE FRAME (SPEC.md 85.6.4).
        # This is the whole assertion and it is only meaningful on a machine
        # whose frame rate is nowhere near its tick rate - which is this one,
        # and is why the bug was reported off a 5150 and not off a 386.
        f0 = word(m, seg, base, "tk_frames")
        t0 = word(m, seg, base, "tk_last")
        a0 = word(m, seg, base, "tk_pa") & 0xFF
        m.key("KeyD", down=True, up=False)
        m.advance(frames=300)
        m.run()
        m.key("KeyD", down=False, up=True)
        f1 = word(m, seg, base, "tk_frames")
        t1 = word(m, seg, base, "tk_last")
        a1 = word(m, seg, base, "tk_pa") & 0xFF
        df, dt, da = f1 - f0, (t1 - t0) & 0xFFFF, (a1 - a0) & 0xFF
        print("  held D for %d frames / %d ticks: heading +%d = %.2f a tick, "
              "%.2f a frame" % (df, dt, da, da / max(1, dt), da / max(1, df)))
        if dt < 20 or da == 0:
            bad.append("the turn-rate sample is degenerate (%d ticks, %d "
                       "units): nothing was measured" % (dt, da))
        elif not (TK_TURN * 0.6 <= da / dt <= TK_TURN * 1.15):
            bad.append("the player turned %.2f units a tick against TK_TURN = "
                       "%d: steering is paced by something other than the "
                       "clock (SPEC.md 85.6.4 - it used to be the FRAME, and "
                       "that is what made a 5150 unplayable)"
                       % (da / dt, TK_TURN))

        # --- it draws -------------------------------------------------------
        f0 = word(m, seg, base, "tk_frames")
        m.pause()
        w, h, before = m.fbuf(0)
        m.run()
        m.advance(frames=90)
        m.run()
        f1 = word(m, seg, base, "tk_frames")
        m.pause()
        w, h, after = m.fbuf(0)
        print("  frames %d -> %d" % (f0, f1))
        if f1 <= f0:
            bad.append("tk_frames did not move (%d -> %d): the loop stopped"
                       % (f0, f1))

        # --- it advances ----------------------------------------------------
        moved = sum(1 for i in range(0, len(before), 3)
                    if before[i:i + 3] != after[i:i + 3])
        print("  %d of %d pixels moved between two samples" % (moved, w * h))
        if moved < 50:
            bad.append("the glass did not change (%d pixels): a frame counter "
                       "that climbs over a still picture is a flip that never "
                       "latched" % moved)

        # --- it does not flash ----------------------------------------------
        # Straight out of VRAM, once per DISPLAYED frame, the way
        # tests/wireflick.py does it: fbuf would move three quarters of a
        # megabyte a sample and m.flicker() wants a screen that settles,
        # which a game never does again (SPEC.md 78.5).
        m.run()
        fbseg, banks = (0xB800, 2) if back == 2 else (0xB000, 4)
        samples = []
        for _ in range(FRAMES):
            m.advance(frames=1)
            m.pause()
            samples.append(lit(m.read(fbseg << 4, banks * 0x2000)))
            m.run()
        m.pause()
        s = sorted(samples)
        med = s[len(s) // 2]
        # A FLASH IS A DIP, NOT A LOW FRAME, and the difference is the whole
        # measurement. A scene that legitimately loses half its ink - an
        # object leaving the view, the ridge swinging out of it - walks DOWN
        # and stays down; a frame taken apart on the glass is thin between two
        # full ones. So each frame is priced against its NEIGHBOURS, which is
        # the only comparison a moving picture supports.
        worst, at = 100, 0
        for i in range(1, len(samples) - 1):
            near = min(samples[i - 1], samples[i + 1])
            if near <= 0:
                continue
            r = 100 * samples[i] // near
            if r < worst:
                worst, at = r, i
        thin = sum(1 for i in range(1, len(samples) - 1)
                   if min(samples[i - 1], samples[i + 1]) > 0 and
                   100 * samples[i] // min(samples[i - 1], samples[i + 1]) < 90)
        print("  ink per displayed frame: min %d median %d max %d over %d"
              % (s[0], med, s[-1], len(s)))
        print("  thinnest against its neighbours: %d%% (frame %d); %d frame(s) "
              "under 90%%" % (worst, at, thin))
        if med == 0:
            bad.append("nothing is lit on any frame")
        elif worst < FLOOR:
            bad.append("a displayed frame carried %d%% of the ink of the two "
                       "around it: the frame is being taken apart ON THE "
                       "GLASS, which is what SPEC.md 85.1 exists to prevent"
                       % worst)
        # --- and nothing lands outside the viewport --------------------
        # Hercules puts a 640-pixel viewport in a 720-pixel row, so five
        # bytes either side belong to nobody and NOTHING EVER ERASES THEM -
        # the erase is the frame's own dirty spans. A single byte of ink that
        # reaches them stays for the session, and it collects. It has to be
        # driven to be seen: a stationary player almost never lays a segment
        # that ends on the last byte of a row, which is the case that spills.
        if back == 3:
            m.run()
            for k in ("KeyD", "KeyW", "KeyA", "KeyW"):
                m.key(k, down=True, up=False)
                m.advance(frames=100)
                m.run()
                m.key(k, down=False, up=True)
            m.pause()
            fb = m.read(0xB000 << 4, 4 * 0x2000)
            spill, first = 0, None
            for y in range(348):
                d = (y & 3) * 0x2000 + (y >> 2) * 90
                for b in list(range(0, 5)) + list(range(85, 90)):
                    if fb[d + b]:
                        spill += 1
                        if first is None:
                            first = (y, b, fb[d + b])
            print("  margin bytes lit outside the viewport: %d" % spill)
            if spill:
                bad.append("%d byte(s) of ink outside the 640-pixel viewport, "
                           "first at row %d byte %d = %02X - nothing erases "
                           "the margins, so it collects" % ((spill,) + first))

        # --- and the game ENDS, with something to do about it -----------
        # Forced from outside rather than played to: four lives at about six
        # seconds a hit is half a minute of guest time to assert one thing.
        m.run()
        m.pause()
        m.write((seg << 4) + base + dispapps.bss_off("tank", "tk_lives"),
                b"\x00")
        m.write((seg << 4) + base + dispapps.bss_off("tank", "tk_dead"),
                b"\x01")
        m.run()
        m.advance(frames=40)
        m.run()
        mid = word(m, seg, base, "tk_over") & 0xFF
        m.advance(frames=180)
        m.run()
        end = word(m, seg, base, "tk_over") & 0xFF
        print("  game over: %d during the fly-in, %d after it" % (mid, end))
        if mid != 1:
            bad.append("the last life did not start the fly-in (tk_over=%d)"
                       % mid)
        if end != 2:
            bad.append("the fly-in never settled (tk_over=%d): TK_GOANIM is "
                       "18 ticks and this waited far longer" % end)
        # This game scored nothing, so it took no row and there must be NO
        # initials prompt: while one is up every key belongs to it, N included.
        # The sentinel for "no row" is a byte offset (TK_NHS * 4) and was once
        # written unscaled, which put the prompt up after every losing game and
        # then stamped the letters off the end of row 1.
        ient = word(m, seg, base, "tk_ient") & 0xFF
        idx = word(m, seg, base, "tk_hsidx")
        print("  scored nothing: hsidx=%d prompt=%d" % (idx, ient))
        if ient:
            bad.append("the initials prompt opened for a game that took no "
                       "row (tk_hsidx=%d): every key from here is the "
                       "prompt's" % idx)
        # --- and a typed initial does NOT redraw the world (SPEC.md 85.9.1) --
        # Only tk_render moves tk_frames, so the counter standing still across
        # a keystroke IS the assertion: the prompt was repainted on its own.
        # Forced from outside, because this game scored nothing and so never
        # sees the prompt on its own.
        m.write((seg << 4) + base + dispapps.bss_off("tank", "tk_ient"),
                b"\x01")
        m.write((seg << 4) + base + dispapps.bss_off("tank", "tk_ipos"),
                b"\x00")
        m.run()
        m.advance(frames=60)
        m.run()
        qf0 = word(m, seg, base, "tk_frames")
        m.type_text("z")
        m.advance(frames=60)
        m.run()
        qf1 = word(m, seg, base, "tk_frames")
        ipos = word(m, seg, base, "tk_ipos") & 0xFF
        print("  a typed initial: ipos %d, tk_frames +%d (a full render is +1)"
              % (ipos, qf1 - qf0))
        if ipos != 1:
            bad.append("the initials prompt did not take the key (ipos=%d)"
                       % ipos)
        elif qf1 != qf0:
            bad.append("a typed initial cost %d full render(s): the prompt is "
                       "redrawing the world for one glyph, which is a second a "
                       "letter on the target machine (SPEC.md 85.9.1)"
                       % (qf1 - qf0))
        m.write((seg << 4) + base + dispapps.bss_off("tank", "tk_ient"),
                b"\x00")
        m.write((seg << 4) + base + dispapps.bss_off("tank", "tk_still"),
                b"\x00")
        m.run()

        m.type_text("n")                        # ...and N must start again
        m.advance(frames=90)
        m.run()
        again = word(m, seg, base, "tk_over") & 0xFF
        lives = word(m, seg, base, "tk_lives") & 0xFF
        print("  after N: over=%d lives=%d" % (again, lives))
        if again != 0 or lives != TK_LIVES:
            bad.append("N did not start a fresh game (over=%d lives=%d)"
                       % (again, lives))

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
