#!/usr/bin/env python3
"""PIXELSTEIN 3D draws, advances, does not flash - and its frame on the two
pinned scenes (SPEC.md 96.1, 96.10; docs/plans/PIXELSTEIN-PLAN.md 15).

    python3 tests/pixelstein.py [--machine os8088_5150_cga_gla] [--windowed]
                                [--c160] [--frames 12] [--shots DIR]

tests/tank.py's three questions, then the numbers:

**It draws.** px_frames climbs, read out of the package's bss - a bracket
that has stopped looks exactly like one drawing into a page nobody shows.

**It advances.** With a turn key held the glass differs between two samples.

**It walks.** The eye poked to the spawn facing SOUTH (two open tiles
before the wall) and Up held for three quarters of a second: px_py grows by
PX_SPEED a tick, within the ticks the BIOS counted, and px_px does not move.
The check that catches a clobbered step - wave 1's first cut lost the y
step across the collision call and walked south at 0 and north at a tile a
tick, and nothing measured it because every measured frame turns.

**It does not flash.** The ink of each displayed frame against its two
neighbours', while turning: a frame under 70% of them is a picture taken
apart on the glass (the FLOOR against the MEDIAN, not a mean).

**The frame.** Both pinned scenes - A the spawn's corridor, B the doorway
turn - at the default (Textured Low res 64 x 80, wave 2), at Textured Full
64 x 80 and the 48 x 80 Low res fallback beside it (96.1's fork), at the two
Flat rungs and at Wire, each the
median of --frames consecutive frames, MartyPC's cycle counter between two
entries to px_frame_begin (tests/tankperf.py's method; the package's own
px_ftime - cast, compose and present alone - is printed beside it), and
TWO FRAMES A ROW (SPEC.md 96.10): the FULL REPAINT (pxslib.force_all_poke
at every stop - the seven history arrays, not px_force alone, which
composes nothing on a still eye) and the TURN frame (the heading stepped
by PX_TURN at every stop, nothing forced: what the delta-fill writes and
the present sends when a player turns). **Fullscreen on the 5150 CGA and
Hercules the default must read >= 8.0 (A) / >= 7.0 (B) on the FULL
REPAINT** - the dearer of the two, the promise 96.1 writes on that
configuration; every other number here is REPORTED and never gated: Mode X
on os8088_xt_vga (a correctness machine, never a timing one -
docs/MARTYPC-DEBUG.md), the C160 retime on a genuine CGA (--c160: the
second Mode item), and windowed (--windowed) on an 8086, which PLAN 15 says
is reported and not promised.
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

FLOOR = 70
SAMPLES = 40
PROMISE = {"a": 8.0, "b": 7.0}
GATED = ("os8088_5150_cga_gla", "os8088_5150_herc_gla")
# (rung, low res, Size, label): the default first, then what SPEC.md 96.1
# reports beside it - Textured Full 64 x 80, the 48 x 80 Low res fallback
# the fork names, the two Flat rungs and Wire
RUNGS = (("tex", True, 64, "Textured Low res 64x80 (the default)"),
         ("tex", False, 64, "Textured Full 64x80"),
         ("tex", True, 48, "Textured Low res 48x80 (the fallback)"),
         ("flat", True, 64, "Flat Low res 64x80"),
         ("flat", False, 64, "Flat Full 64x80"),
         ("wire", True, 64, "Wire Low res 64x80"))
MODES = (("full", "full repaint"), ("turn", "turn"))
WALK_FRAMES = 45                    # MartyPC video frames Up is held: ~13 ticks
FAIL = []


def check(ok, what):
    print("   %-66s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def lit(px):
    """Non-black pixels of a rendered frame (packed rgb24)."""
    n = 0
    for i in range(0, len(px), 3):
        if px[i] or px[i + 1] or px[i + 2]:
            n += 1
    return n


def shot(m, path):
    m.pause()
    w, h, px = m.fbuf(0)
    os88marty.write_png_rgb(path, w, h, px)
    m.run()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/games360.img")
    ap.add_argument("--windowed", action="store_true",
                    help="stay on the desktop: reported, never promised (PLAN 15)")
    ap.add_argument("--c160", action="store_true",
                    help="the second Mode item on a genuine CGA (SPEC.md 88.15)")
    ap.add_argument("--frames", type=int, default=12)
    ap.add_argument("--shots", help="write a screendump per rung and scene here")
    ap.add_argument("--stages", action="store_true",
                    help="also split the fullscreen frame by stage (cast / compose / "
                         "present / loop) at the default and at Flat Full, scene A "
                         "- the residual's home (docs/reports/PXS-FRAME-*.md)")
    a = ap.parse_args()
    os.chdir(ROOT)
    if a.shots:
        os.makedirs(a.shots, exist_ok=True)
    world = "windowed" if a.windowed else ("c160" if a.c160 else "fullscreen")
    gated = a.machine in GATED and not a.windowed and not a.c160

    # --- the two scenes are what 96.10 says they are, on the host, first ---
    # B exists to be the delta-fill-heavy TURN frame (PLAN's XT-6); a level
    # edit that quietly made it the easy scene would leave the promise's
    # second half asserting nothing. A binds the FULL repaint on its own (the
    # most crossings a ray in E1M1; the forced compose is the same everywhere)
    sa, ca = pxslib.turn_stores("a")
    sb, cb = pxslib.turn_stores("b")
    print("   a PX_TURN turn writes %d Flat stores in %d columns at A, %d in %d at B"
          % (sa, ca, sb, cb))
    check(sb >= 1.3 * sa, "scene B's turn delta-fill is >= 1.3x scene A's on the "
          "host (%d vs %d stores)" % (sb, sa))

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        g = pxslib.open_game(m)
        st = g.state()
        print("   PXSTEIN.O88: window %d, part 0 at %04x, tier %d, handoff %s"
              % (g.win, g.seg, st["tier"], g.handoff()))
        if not a.windowed:
            if a.c160:
                m.pause()
                g.poke_byte("px_mode", 2)           # PXB_C160
                g.poke_byte("px_fsxm", 0)           # FSXM_TEXT80
                m.run()
            g.enter_fsx()
        st = g.state()
        print("   %s: backend %s, Auto at position %d -> rung %d, lowres %d"
              % (world, st["back"], st["apos"], st["rung"], st["lowres"]))
        check(st["back"] != "none", "a backend was adopted (%s)" % st["back"])

        # --- it draws, it advances ------------------------------------------
        f0 = g.word("px_frames")
        m.pause()
        w, h, before = m.fbuf(0)
        m.run()
        m.key("ArrowRight", down=True, up=False)    # a TURN: a strafe at the
        m.advance(frames=120)                       # spawn meets its side wall
        m.run()
        f1 = g.word("px_frames")
        m.pause()
        w, h, after = m.fbuf(0)
        m.run()
        moved = sum(1 for i in range(0, len(before), 3)
                    if before[i:i + 3] != after[i:i + 3])
        print("   frames %d -> %d while turning; %d of %d pixels moved"
              % (f0, f1, moved, w * h))
        check(f1 > f0, "px_frames climbed (%d -> %d)" % (f0, f1))
        check(moved >= 50, "the glass changed (%d pixels)" % moved)
        m.key("ArrowRight", down=False, up=True)

        # --- it walks ---------------------------------------------------------
        m.advance(frames=30)                        # the turn's keys drained
        m.run()
        px0, py0, _ = g.scene("a")                  # the spawn, facing east...
        m.pause()
        g.eye_poke(px0, py0, 1024)                  # ...turned south: two open
        m.run()                                     # tiles before the wall
        g.wait_frames(1)
        t0 = g.ticks()
        m.key("ArrowUp", down=True, up=False)
        m.advance(frames=WALK_FRAMES)
        m.run()
        m.key("ArrowUp", down=False, up=True)
        t1 = g.ticks()
        m.advance(frames=10)                        # the last owed step spent
        m.run()
        px1, py1, _ = g.eye()
        dt = (t1 - t0) & 0xFFFFFF
        dy = py1 - py0
        lo, hi = pxslib.PX_SPEED * max(dt - 3, 1), pxslib.PX_SPEED * (dt + 2)
        print("   walked south for %d ticks: py %d -> %d (+%d), px %d -> %d"
              % (dt, py0, py1, dy, px0, px1))
        check(px1 == px0, "walking south left px alone (%d -> %d)" % (px0, px1))
        check(lo <= dy <= hi, "walking south moved py by PX_SPEED a tick: +%d in "
              "%d ticks, want %d..%d" % (dy, dt, lo, hi))
        m.key("ArrowRight", down=True, up=False)    # turning again for the
        m.advance(frames=20)                        # flash leg below
        m.run()

        # --- it does not flash ----------------------------------------------
        samples = []
        for _ in range(SAMPLES):
            m.advance(frames=1)
            m.pause()
            samples.append(lit(m.fbuf(0)[2]))
            m.run()
        m.key("ArrowRight", down=False, up=True)
        s = sorted(samples)
        med = s[len(s) // 2]
        worst, at = 100, 0
        for i in range(1, len(samples) - 1):
            near = min(samples[i - 1], samples[i + 1])
            if near <= 0:
                continue
            r = 100 * samples[i] // near
            if r < worst:
                worst, at = r, i
        print("   ink per displayed frame: min %d median %d max %d; thinnest "
              "against its neighbours %d%% (frame %d)" % (s[0], med, s[-1], worst, at))
        check(med > 0, "something is lit")
        check(worst >= FLOOR, "no displayed frame under %d%% of its neighbours "
              "(SPEC.md 85.1's flash gate)" % FLOOR)

        # --- the frame, both scenes, three rungs, two modes --------------------
        results = []
        texok = g.state()["texok"]
        check(texok == 1, "every part the Textured rung needs was carved (px_texok %d)"
              % texok)
        for rung, lowres, size, label in RUNGS:
            if rung == "tex" and not texok:
                continue
            g.pin(rung=rung, lowres=lowres, size=size)
            for scene in ("a", "b"):
                for mode, mlabel in MODES:
                    m.advance(frames=30)        # let the loop drain what the
                    m.run()                     # last leg's keys left behind
                    px, py, head = g.scene(scene)
                    g.wait_frames(1)
                    times = g.frame_times(a.frames, mode=mode)
                    ex, ey, eh = g.eye()
                    want_h = head if mode == "full" else \
                        (head + (a.frames + 1) * pxslib.PX_TURN) & 0xFFF
                    check((ex, ey, eh) == (px, py, want_h),
                          "the eye stayed at scene %s for the %s measurement "
                          "(%s, want %s)" % (scene.upper(), mlabel, (ex, ey, eh),
                                             (px, py, want_h)))
                    cyc = pxslib.median([t[0] for t in times])
                    draw = pxslib.median([t[1] for t in times]) * 0.838 / 1000.0
                    ms = pxslib.ms(cyc)
                    fps = 1000.0 / ms if ms else 0
                    st = g.state()
                    r0, r1 = g.byte("px_r0"), g.byte("px_r1")
                    rows = (r1 - r0 + 1) if r1 >= r0 else 0
                    results.append((label, scene, mlabel, ms, fps, draw,
                                    st["cols"], rows))
                    print("   %-38s scene %s %-12s %6.1f ms = %5.2f fps (draw "
                          "%6.1f ms, %d cols, %d rows presented, %d frames)"
                          % (label, scene.upper(), mlabel, ms, fps, draw,
                             st["cols"], rows, len(times)))
                    # THE ROWS PRESENTED ARE ASSERTED, NOT NOTED (SPEC.md
                    # 96.5): a full repaint sends the whole band, and the
                    # turn frame sends what the host's model of px_r0..px_r1
                    # says it should - which is 80 on both pinned scenes,
                    # because the range is one min/max over all columns and
                    # a column nearer than 2.5 tiles spans them all. A
                    # number that sat at its ceiling as a note could neither
                    # regress nor be seen to improve
                    if mode == "full":
                        want = pxslib.ROWS
                    else:
                        want = pxslib.turn_present_rows(
                            scene, st["cols"], rung, a.frames + 1,
                            2 if st["back"] == "modex" else 1)
                    check(rows == want, "the %s frame presented %d rows on scene "
                          "%s, the host's model of 96.5's range says %d"
                          % (mlabel, rows, scene.upper(), want))
                    if a.shots and mode == "full":
                        shot(m, os.path.join(a.shots, "wave2-%s-%s-%s-%s%d-%s.png"
                             % (a.machine, world, rung,
                                "low" if lowres else "full", size, scene)))
                    if gated and rung == "tex" and lowres and size == 64 and mode == "full":
                        check(fps >= PROMISE[scene],
                              "the default reads %.2f fps on scene %s (full "
                              "repaint), >= %.1f (96.1's promise)"
                              % (fps, scene.upper(), PROMISE[scene]))
        # --- the stages (--stages, fullscreen): where the residual lives -------
        stages = []
        if a.stages and not a.windowed:
            for rung, lowres, size, label in RUNGS:  # Wire too: its compose is
                if rung == "tex" and not texok:      # the stage that differs
                    continue
                g.pin(rung=rung, lowres=lowres, size=size)
                for mode, mlabel in MODES:
                    m.advance(frames=30)
                    m.run()
                    g.scene("a")
                    g.wait_frames(1)
                    st = g.stage_times(a.frames, mode=mode)
                    med = {k: pxslib.median([x[k] for x in st]) for k in st[0]}
                    stages.append((label, mlabel, g.state()["cols"], med))
                    print("   %-38s A %-12s cast %6.1f  compose %6.1f  present %6.1f  "
                          "loop %5.1f  prologue %4.1f  = %6.1f ms"
                          % (label, mlabel, pxslib.ms(med["cast"]), pxslib.ms(med["compose"]),
                             pxslib.ms(med["present"]), pxslib.ms(med["loop"]),
                             pxslib.ms(med["prologue"]), pxslib.ms(med["frame"])))
        if not a.windowed:
            g.leave_fsx()
            check(g.byte("px_inbr") == 0, "Esc left the bracket")

    print()
    if stages:
        print("   %s: the stages, scene A (median of %d frames, clk and ms)"
              % (a.machine, a.frames))
        for label, mlabel, cols, med in stages:
            print("     %-38s %-12s %d cols: cast %7d (%5.1f ms, %5.0f/col)  compose %7d "
                  "(%5.1f ms, %5.0f/col)  present %7d (%5.1f ms)  loop %6d  prologue %5d"
                  % (label, mlabel, cols, med["cast"], pxslib.ms(med["cast"]),
                     med["cast"] / cols, med["compose"], pxslib.ms(med["compose"]),
                     med["compose"] / cols, med["present"], pxslib.ms(med["present"]),
                     med["loop"], med["prologue"]))
    print("   %s %s: the frame table (median of %d frames; draw = cast + compose "
          "+ present; rows = the last frame's present)" % (a.machine, world, a.frames))
    for label, scene, mlabel, ms, fps, draw, cols, rows in results:
        print("     %-38s %s %-12s %6.1f ms %5.2f fps  draw %6.1f ms  %d cols  %d rows"
              % (label, scene.upper(), mlabel, ms, fps, draw, cols, rows))
    if FAIL:
        print("pixelstein: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pixelstein: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
