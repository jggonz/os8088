#!/usr/bin/env python3
"""PIXELSTEIN 3D draws, advances, does not flash - and its frame on the two
pinned scenes (SPEC.md 97.1, 97.10; docs/plans/PIXELSTEIN-PLAN.md 15).

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
turn - and C, the sprite scene, at the default (Textured Low res 64 x 80,
the user's full-width direction, 97.1), at Textured Low res 48 x 80 (97.1's
fallback rung, reported beside it), at Textured Full 64 x 80, at the Flat
rungs and at Wire, each the
median of --frames consecutive frames, MartyPC's cycle counter between two
entries to px_frame_begin (tests/tankperf.py's method; the package's own
px_ftime - cast, compose and present alone - is printed beside it), and
TWO FRAMES A ROW (SPEC.md 97.10): the FULL REPAINT (pxslib.force_all_poke
at every stop - the seven history arrays, not px_force alone, which
composes nothing on a still eye) and the TURN frame (the heading stepped
by PX_TURN at every stop, nothing forced: what the delta-fill writes and
the present sends when a player turns). **Fullscreen on the 5150 CGA and
Hercules the default must read >= 8.0 (A) / >= 7.0 (B) / >= 8.0 (C) on the
FULL REPAINT** - the dearer of the two, the promise 97.1 writes on that
configuration; every other number here is REPORTED and never gated: Mode X
on os8088_xt_vga (a correctness machine, never a timing one -
docs/MARTYPC-DEBUG.md), the C160 retime on a genuine CGA (--c160: the
second Mode item), and windowed (--windowed) on an 8086, which PLAN 15 says
is reported and not promised.

**The finished frame (wave 3, 97.1's fork).** The rows above run with the
world FROZEN (px_simoff: no door, guard or weapon moves, so a pose is the
same pose at every stop) and they draw the weapon and whatever sprite the
pose shows - A and B show none, C (the brick room, three sprites) shows
the plan's scene-A count. A third mode, "finished", is the full repaint
with the world's sim RUNNING and the player invulnerable (px_god): the
doors' and guards' ticks, the pickups, the keys - the whole game's frame,
which is the quantity 97.1's 8.0 / 7.0 line was written against - and it
is GATED at the default on the two pinned scenes (8.0 / 7.0). THE POSE IS
PUT BACK AT EVERY STOP (pxslib.frame_times's pose hook): a guard that sees
the player walks, and review r1 found scene C's 64 x 80 finished frame
read with two sprites where the pose has three - so the sprite count is
ASSERTED (0 on A and B, 3 on C). A fourth pose, "A3", is scene A with
THREE GUARDS standing in the corridor two, four and six tiles ahead - the
plan's scene-A sprite count on the plan's scene A (PLAN 3's table) - the
frame the fork's trigger names, REPORTED with its verdict against the 8.0
line and never gated: the default is the user's decision (97.1). C and
the 48 x 80 and Flat Low res rungs are reported beside it.

**The table is SPEC.md 97.15's (wave 6's close review).** Every cell this run
measures is looked up in 97.15's table for the same machine and world - READ
OUT OF SPEC.md, so the document and the gate cannot hold two copies - and
must be within 5% of it (PIXELSTEIN-PLAN 7, wave 6's done-when: "the numbers
matching 97 within 5%"). ASSERTED on the two gated machines fullscreen,
REPORTED elsewhere: Mode X lands on whole 16.75 ms retraces, so one retrace
either way is 11-25% of a cell and a band cannot hold it, and C160 and
windowed are reported tables in the first place. A cell 97.15 leaves blank
or writes as a range is skipped and counted. A table that moves 5% is a
SPEC.md change before it is a code change (SPEC.md is updated FIRST).
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
PROMISE = {"a": 8.0, "b": 7.0}              # the two pinned scenes (97.1)
TRIGGER = 8.0                               # ...and the fork's line on A3
DEFAULT = 64                                # Size: the user's full width (97.1)
GATED = ("os8088_5150_cga_gla", "os8088_5150_herc_gla")
# (rung, low res, Size, label): the default first, then what SPEC.md 97.1
# reports beside it - the 48 x 80 Low res fallback the fork names, Textured
# Full 64 x 80, the two Flat rungs and Wire
RUNGS = (("tex", True, 64, "Textured Low res 64x80 (the default)"),
         ("tex", True, 48, "Textured Low res 48x80 (the fallback)"),
         ("tex", False, 64, "Textured Full 64x80"),
         ("flat", True, 64, "Flat Low res 64x80"),
         ("flat", True, 48, "Flat Low res 48x80"),
         ("flat", False, 64, "Flat Full 64x80"),
         ("flat", False, 48, "Flat Full 48x80"),
         ("wire", True, 64, "Wire Low res 64x80"))
MODES = (("full", "full repaint"), ("turn", "turn"))
FINISHED = (("tex", True, 64, "Textured Low res 64x80 (the default)"),
            ("tex", True, 48, "Textured Low res 48x80 (the fallback)"),
            ("flat", True, 64, "Flat Low res 64x80"))
SPRITES = {"a": 0, "b": 0, "c": 3, "a3": 3}  # candidates a pose must show
STAND, PXAF_SEEN = 1, 4
WALK_FRAMES = 45                    # MartyPC video frames Up is held: ~13 ticks
FAIL = []
SPEC_TOL = 0.05                     # the done-when's "within 5%"
SPEC_COLS = {"full repaint": 0, "turn": 1, "finished": 2}


def spec_table(machine, world):
    """SPEC.md 97.15's frame table for (machine, world), as
    {(label, scene, column): ms}. The header is `**`<machine>`, ...` for
    fullscreen and `**`<machine> --c160`` / `--windowed` for the others;
    a blank cell or a range (Mode X's retrace either way) is left out."""
    tag = machine + {"fullscreen": "", "c160": " --c160",
                     "windowed": " --windowed"}[world]
    text = open(os.path.join(ROOT, "SPEC.md")).read()
    at = text.find("### 97.15 ")
    end = text.find("\n### ", at + 1)
    sec = text[at:end if end > 0 else len(text)].splitlines()
    cells, on, label = {}, False, None
    for ln in sec:
        if ln.startswith("**`"):
            on = ln.startswith("**`%s`" % tag)
            continue
        if not on or not ln.startswith("|") or ln.startswith("|---") \
                or ln.startswith("| rung"):
            continue
        f = [c.strip() for c in ln.strip().strip("|").split("|")]
        if len(f) < 5:
            continue
        if f[0]:
            label = f[0]
        for col, cell in zip(("full repaint", "turn", "finished"), f[2:5]):
            ms = cell.split("=")[0].strip()
            try:
                cells[(label, f[1].lower(), col)] = float(ms)
            except ValueError:
                pass                    # blank, or a range: not a cell
    return cells


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


STEER = ("ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown")


def steer_released(m):
    """Every steering key UP in the kernel's own key map before a
    measurement (pxslib.release_held, which every row shares since wave 6's
    close). Review, wave 6 r2: one c160 run measured every pose with the
    heading spinning ("the eye stayed at scene A ... (896, 896, 3968)", 0
    rows presented), and the re-run was green. A pose is POKED, so only a
    key the kernel still believes is held can turn it: a break code the
    guest never saw."""
    return pxslib.release_held(m, STEER, "steering")


def restore(g, spawn):
    """Every actor back where the level placed it, STANDING (paused)."""
    for i, s in enumerate(spawn):
        g.actor_poke(i, x=s["x"], y=s["y"], state=STAND, ang=s["ang"], dir=s["dir"],
                     hp=s["hp"], flags=s["flags"] & ~PXAF_SEEN)


def poser(g, scene, spawn, px, py):
    """(pose, seen): the callable that puts a finished-frame pose back at a
    stop (the guest paused), and the list it appends px_nsc - the count the
    frame before it gathered - to. Scene C's guard (actor 1, at (31,13))
    stands where the level put it; A3 stands guards 0, 1 and 2 in scene A's
    corridor two, four and six tiles ahead of the eye, facing it; A and B
    stand everyone at their spawn, out of view."""
    seen = []
    if scene == "a3":                   # two, four and six tiles ahead, the
        where = [(px + 512, py, 2048, 2),         # farther two half a tile
                 (px + 1024, py - 128, 2048, 2),  # either side so all three
                 (px + 1536, py + 128, 2048, 2)]  # show (the corridor is
    else:
        where = [(spawn[i]["x"], spawn[i]["y"], spawn[i]["ang"], spawn[i]["dir"])
                 for i in range(3)]

    def pose():
        seen.append(g.byte("px_nsc"))
        for i, (x, y, ang, d) in enumerate(where):
            g.actor_poke(i, x=x, y=y, state=STAND, ang=ang, dir=d,
                         flags=spawn[i]["flags"] & ~PXAF_SEEN)
    return pose, seen


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

    # --- the two scenes are what 97.10 says they are, on the host, first ---
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
        g.sim(False)                    # the world frozen (wave 3): the poses
        g.god(True)                     # stay put, and nothing restarts the floor
        if not a.windowed:
            if a.c160:
                m.pause()
                g.poke_byte("px_mode", 2)           # PXB_C160
                g.poke_byte("px_fsxm", 0)           # FSXM_TEXT80
                m.run()
            fb = g.word("px_frames")
            g.enter_fsx()
            # THE FIRST FRAME FIRST: a bracket's entry builds both scaler
            # sets, the byte-texture set and (wave 3) the sprite set before
            # it draws - seconds on the 5150 (97.1, 97.6) - and enter_fsx
            # returns at the TOP of that path; a key held across it is
            # typematic in the BIOS buffer by the time the loop runs, and the
            # first cut of this wave read "1 -> 1 frames while turning" and a
            # tap spent on the next leg's walk
            g.wait_frames(1, f0=fb, limit=180.0)
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
            for scene in ("a", "b", "c"):
                if scene == "c" and (rung != "tex" or not lowres):
                    continue            # C, the sprite scene, Textured Low res
                for mode, mlabel in MODES:
                    m.advance(frames=30)        # let the loop drain what the
                    m.run()                     # last leg's keys left behind
                    steer_released(m)
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
                    # 97.5): a full repaint sends the whole band, and the
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
                    if scene != "c":    # (C's turn range holds the sprites'
                        check(rows == want, "the %s frame presented %d rows on scene "
                              "%s, the host's model of 97.5's range says %d"
                              % (mlabel, rows, scene.upper(), want))
                    else:               # rows too, which the model does not
                        check(rows == want if mode == "full" else rows >= 1,
                              "the %s frame presented %d rows on scene C" % (mlabel, rows))
                    if a.shots and mode == "full":
                        shot(m, os.path.join(a.shots, "wave3-%s-%s-%s-%s%d-%s.png"
                             % (a.machine, world, rung,
                                "low" if lowres else "full", size, scene)))
                    if gated and rung == "tex" and lowres and size == DEFAULT \
                            and mode == "full" and scene in PROMISE:
                        check(fps >= PROMISE[scene],
                              "the default reads %.2f fps on scene %s (full "
                              "repaint), >= %.1f (97.1's promise)"
                              % (fps, scene.upper(), PROMISE[scene]))
        # --- THE FINISHED FRAME (wave 3, 97.1's fork): the sim running -------
        # a full repaint at every stop with doors, guards and the weapon
        # ticking, the player invulnerable; scene C carries three sprites
        # and A3 is scene A with three guards posed in the corridor. The
        # pose is put back at every stop and the sprite count asserted
        # (review r1). Gated on A and B at the default; A3's verdict against
        # the fork's line is printed, and 97.1 carries the decision
        finished = []
        spawn = g.actors()                  # every actor as the level placed it
        if texok:
            g.sim(True)
            for rung, lowres, size, label in FINISHED:
                g.pin(rung=rung, lowres=lowres, size=size)
                for scene in ("a", "b", "c", "a3"):
                    m.advance(frames=30)
                    m.run()
                    px, py, head = g.scene("a" if scene == "a3" else scene)
                    pose, seen = poser(g, scene, spawn, px, py)
                    m.pause()
                    g._mark()
                    pose()
                    g.force_all_poke()
                    m.run()
                    g.wait_frames(1)
                    times = g.frame_times(a.frames, mode="sim", pose=pose)
                    cyc = pxslib.median([t[0] for t in times])
                    draw = pxslib.median([t[1] for t in times]) * 0.838 / 1000.0
                    ms = pxslib.ms(cyc)
                    fps = 1000.0 / ms if ms else 0
                    st = g.state()
                    nsc = sorted(set(seen[1:]))     # the count at every stop
                    pl = g.player()
                    finished.append((label, scene, ms, fps, draw, st["cols"], nsc))
                    print("   %-38s scene %-2s %-12s %6.1f ms = %5.2f fps (draw %6.1f ms, "
                          "%d cols, %s sprites, health %d)"
                          % (label, scene.upper(), "finished", ms, fps, draw, st["cols"],
                             nsc, pl["health"]))
                    check(pl["health"] == 100, "the player took no damage under px_god "
                          "(health %d)" % pl["health"])
                    check(nsc == [SPRITES[scene]], "every finished frame on %s carried %d "
                          "sprite candidates (saw %s)" % (scene.upper(), SPRITES[scene], nsc))
                    ex, ey, eh = g.eye()
                    check((ex, ey, eh) == (px, py, head), "the eye stayed at scene %s for "
                          "the finished measurement" % scene.upper())
                    if gated and rung == "tex" and size == DEFAULT and scene in PROMISE:
                        check(fps >= PROMISE[scene],    # THE PROMISE, finished (97.1)
                              "the FINISHED frame reads %.2f fps on scene %s at the "
                              "default, >= %.1f" % (fps, scene.upper(), PROMISE[scene]))
                    if rung == "tex" and size == DEFAULT and scene == "a3":
                        print("   THE FORK'S TRIGGER (97.1): scene A with the plan's three "
                              "sprites reads %.2f fps finished at the default - %s the "
                              "%.1f line (reported; the default is the user's decision)"
                              % (fps, "AT OR OVER" if fps >= TRIGGER else "UNDER", TRIGGER))
                    if a.shots and scene in ("c", "a3"):
                        shot(m, os.path.join(a.shots, "wave3-%s-%s-finished-%s-%s%d-%s.png"
                             % (a.machine, world, rung, "low" if lowres else "full",
                                size, scene)))
                    m.pause()
                    restore(g, spawn)
                    m.run()
            g.sim(False)
            g.pin(rung="tex", lowres=True, size=DEFAULT)
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
                    print("   %-38s A %-12s cast %6.1f  gather %5.1f  compose %6.1f  "
                          "present %6.1f  loop %5.1f  prologue %4.1f  = %6.1f ms"
                          % (label, mlabel, pxslib.ms(med["cast"]), pxslib.ms(med["gather"]),
                             pxslib.ms(med["compose"]), pxslib.ms(med["present"]),
                             pxslib.ms(med["loop"]), pxslib.ms(med["prologue"]),
                             pxslib.ms(med["frame"])))
        if not a.windowed:
            g.leave_fsx()
            check(g.byte("px_inbr") == 0, "Esc left the bracket")

    print()
    if stages:
        print("   %s: the stages, scene A (median of %d frames, clk and ms)"
              % (a.machine, a.frames))
        for label, mlabel, cols, med in stages:
            print("     %-38s %-12s %d cols: cast %7d (%5.1f ms, %5.0f/col)  gather %6d "
                  "(%4.1f ms)  compose %7d (%5.1f ms, %5.0f/col)  present %7d (%5.1f ms)  "
                  "loop %6d  prologue %5d"
                  % (label, mlabel, cols, med["cast"], pxslib.ms(med["cast"]),
                     med["cast"] / cols, med["gather"], pxslib.ms(med["gather"]),
                     med["compose"], pxslib.ms(med["compose"]),
                     med["compose"] / cols, med["present"], pxslib.ms(med["present"]),
                     med["loop"], med["prologue"]))
    print("   %s %s: the frame table (median of %d frames; draw = cast + compose "
          "+ present; rows = the last frame's present)" % (a.machine, world, a.frames))
    for label, scene, mlabel, ms, fps, draw, cols, rows in results:
        print("     %-38s %s %-12s %6.1f ms %5.2f fps  draw %6.1f ms  %d cols  %d rows"
              % (label, scene.upper(), mlabel, ms, fps, draw, cols, rows))
    if finished:
        print("   %s %s: THE FINISHED FRAME (the sim running, a full repaint, the pose "
              "put back at every stop; SPEC.md 97.1's fork is read on A3's default row)"
              % (a.machine, world))
        for label, scene, ms, fps, draw, cols, nsc in finished:
            print("     %-38s %-2s finished     %6.1f ms %5.2f fps  draw %6.1f ms  %d cols  "
                  "%s sprites" % (label, scene.upper(), ms, fps, draw, cols, nsc))
    # --- SPEC.md 97.15's table, cell for cell (the done-when's 5%) ---------
    table = spec_table(a.machine, world)
    meas = [(l, sc, ml, ms) for l, sc, ml, ms, _, _, _, _ in results] + \
           [(l, sc, "finished", ms) for l, sc, ms, _, _, _, _ in finished]
    hit = worst = 0
    worstat = ""
    far = []
    for label, scene, col, ms in meas:
        want = table.get((label, scene, col))
        if want is None or not ms:
            continue
        hit += 1
        d = abs(ms - want) / want
        if d > worst:
            worst, worstat = d, "%s %s %s %.1f against %.1f" % (label, scene.upper(),
                                                                 col, ms, want)
        if d > SPEC_TOL:
            far.append("%s %s %s: %.1f ms, 97.15 says %.1f (%+.1f%%)"
                       % (label, scene.upper(), col, ms, want, 100.0 * (ms - want) / want))
    print("   SPEC.md 97.15 (%s %s): %d of %d measured cells have a cell there; "
          "the worst is %.1f%% off (%s)" % (a.machine, world, hit, len(meas),
                                            100.0 * worst, worstat or "-"))
    for f in far:
        print("     over 5%%: %s" % f)
    if gated:
        check(hit >= len(meas) - 2, "97.15 carries a cell for every measured one "
              "(%d of %d)" % (hit, len(meas)))
        check(not far, "every measured cell is within 5%% of SPEC.md 97.15 "
              "(%d over)" % len(far))
    if FAIL:
        print("pixelstein: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pixelstein: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
