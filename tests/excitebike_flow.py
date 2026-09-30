#!/usr/bin/env python3
"""EXCITEBIKE wave 4: the game around the race, on MartyPC's 4.77 MHz XT (SPEC.md 102.4, 102.6).

    make excitebikedisk && python3 tests/excitebike_flow.py [--adapter vga|cga|both] [--ai] [--quick]

FLOW (default)   Everything a player sees between the desktop and a lap, driven with the
                 keys a player uses and read back from the guest's own variables:

   title         the screen is up, the idle frames count, B shows the best times and Esc
                 comes back; attract does NOT start before 560 idle frames and DOES at 560,
                 the opponents and the AI-driven rider run, and a key ends it
   menus         Enter / Up / Down / Esc through the mode and the course, Selection A
                 (no opponents) and B (three; two on the CGA), the countdown (READY 3, 2, 1,
                 GO! on the HUD, nobody moves, the clock is 0), the pause (Enter: the steps
                 stop, the HUD says PAUSED, Enter resumes)
   rank          the finish is made by the game's own code (the rider is put across the line
                 with a clock the test chooses) and the rank read at EVERY boundary: par - 1 and
                 par are first, par + 1 and par + 399 second, par + 400 and par + 799 third,
                 par + 800 not qualified - and on the repeat of the last course, where the
                 windows shrink by 50 / 100 a repeat, at their new edges
   campaign      a qualified ride goes to the next course; the fifth goes to course 1 on the
                 SECOND PASS (the harder set on both laps and its own par: the column array is
                 compared with the model's); the fifth of that pass repeats; a ride that does
                 not qualify is GAME OVER and then the title; best times: a better time is kept
                 and flagged, a worse one is not
   leaving       Esc in a race is the title; Alt+Enter leaves from the title, a menu and a race;
                 the heap is EXACTLY what it was before the window opened (every claim)
   custom        EXBTRACK.DAT beside the package is a sixth course (a scratch disk carries one; a
                 second carries a damaged one and must show five)

AI (--ai)        5,000 steps of Selection B under a SEEDED random pad script (generated here,
                 no recording): every 2nd frame each opponent's state is read and checked -
                 mode, lane, column and temperature in range, speed never above the kicker's
                 ceiling and not above the turbo cap for more than a kicker's decay, position
                 never going backwards except across a respawn, nobody standing still (no
                 progress in 700 steps without a respawn) - and at least one respawn happens
                 and the game keeps rendering.

Nothing external is read: no ROM, no disassembly.
"""
import argparse
import os
import random
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty as M          # noqa: E402
import os88ui                  # noqa: E402
import excitebike_video as V   # noqa: E402
import excitebike_front as F   # noqa: E402
import exbsim                  # noqa: E402

X = exbsim.X                   # tools/excitebike_assets.py (tests/excitebike_assets.py is a test)

P = exbsim.PHYS
ST_TITLE, ST_MODE, ST_TRACK, ST_RACE, ST_RESULT, ST_OVER, ST_BEST, ST_ATTRACT = range(8)
RANKS = ("1st", "2nd", "3rd", "not qualified")
IDLE_ATTRACT = 560
NAI = {"vga": 3, "cga": 2, "herc": 2}


class Flow:
    """A booted guest with the package open, inside its bracket, on the title."""

    def __init__(self, ui, tag, sym, ref, art):
        self.ui, self.m, self.tag, self.art, self.ref = ui, ui.m, tag, art, ref
        self.g = V.Game(ui, tag, sym, ref)
        self.sym = sym
        self.sz = sym["xm_stateend"] - sym["xm_state"]

    # ---- reading and poking ----------------------------------------------------
    def b(self, n):
        return self.g.b(n)

    def w(self, n):
        return self.g.w(n)

    def sw(self, n):
        return struct.unpack("<h", self.g.rd(n, 2))[0]

    def put(self, n, v, size=None):
        self.g.put(n, v, size)

    def state(self):
        return self.b("xb_state")

    def hud(self):
        return bytes(self.g.rd("xb_hudstr", 20)).decode("latin1")

    # ---- driving ---------------------------------------------------------------
    def key(self, name, hold=.05, settle=.5):
        m = self.m
        m.key(name, up=False)
        M.pace(m, hold)
        m.key(name, down=False)
        M.guest_sleep(m, settle)

    def wait(self, cond, what, guest=60):
        M.until(self.m, lambda _: cond(), what, guest=guest)

    def wait_state(self, s, guest=60):
        self.wait(lambda: self.state() == s, "state %d (it is %d)" % (s, self.state()), guest)

    def pause_write(self, fn):
        """Write guest memory with the machine stopped between two of its instructions."""
        self.m.pause()
        fn()
        self.m.run()

    # ---- the menus -------------------------------------------------------------
    def to_race(self, sel, course, wait_cd=False):
        """From the title: the mode, the course, the race."""
        assert self.state() == ST_TITLE, "not on the title (%d)" % self.state()
        self.key("Enter")
        self.wait_state(ST_MODE)
        if self.b("xb_cur") != sel:                    # (the cursor starts on the last mode chosen)
            self.key("ArrowDown")
        assert self.b("xb_cur") == sel, ("the mode cursor", self.b("xb_cur"))
        self.key("Enter")
        self.wait_state(ST_TRACK)
        while self.b("xb_cur") != course:              # ...and on the last course
            self.key("ArrowDown" if self.b("xb_cur") < course else "ArrowUp", settle=.3)
        self.key("Enter", settle=0 if wait_cd else .5)
        self.wait_state(ST_RACE)
        assert self.b("xb_sel") == sel, ("the mode", self.b("xb_sel"))
        assert self.b("xb_track") == course, ("the course", self.b("xb_track"))
        if not wait_cd:
            self.skip_countdown()

    def skip_countdown(self):
        """The three seconds of READY are asserted elsewhere; every other item starts on the
        line with the race running."""
        self.pause_write(lambda: self.put("xb_cd", 1, 2))
        self.wait(lambda: self.w("xb_cd") == 0, "the countdown ends")

    def finish(self, diff, track=None, flag=None):
        """Put the rider across the line with a clock of par + diff (in hundredths): the
        game's own finish, the coast, the rank."""
        t = self.b("xb_track") if track is None else track
        f = self.b("xb_flag") if flag is None else flag
        par = self.par_of(t, f)

        def go():
            if track is not None:
                self.put("xb_track", track)
            if flag is not None:
                self.put("xb_flag", flag)
            self.put("xb_cs", par + diff, 2)
            self.put("xm_fin", 1)
            self.put("xb_fint", 199, 2)
            self.put("xb_cd", 0, 2)
        self.pause_write(go)
        self.wait_state(ST_RESULT)
        M.guest_sleep(self.m, .4)                      # the screen draws
        return par

    def par_of(self, track, flag):
        if track >= len(self.art.tracks):
            return self.custom_par
        return self.art.tracks[track]["par2" if flag else "par"]

    def result(self):
        return {"rank": self.b("xb_rank"), "qual": self.b("xb_qual"), "ftime": self.w("xb_ftime"),
                "par": self.w("xb_par"), "diff": self.sw("xb_diff"), "newbest": self.b("xb_newbest"),
                "posn": self.b("xb_posn")}

    def to_title(self):
        """Back to the title from anywhere in the game (Esc until it is there)."""
        for _ in range(4):
            if self.state() == ST_TITLE:
                return
            self.key("Escape", settle=.7)
        self.wait_state(ST_TITLE)

    def lit(self, x0, y0, x1, y1):
        self.m.pause()
        n = nonblack(self.m, x0, y0, x1, y1, self.tag)
        self.m.run()
        return n

    def px(self):
        return struct.unpack("<I", self.g.rd("xm_pos", 4))[0] >> 8

    def shot(self, name):
        self.m.pause()
        w, h, px = self.m.fbuf(0)
        os.makedirs(V.OUT, exist_ok=True)
        M.write_png_rgb(os.path.join(V.OUT, "flow-%s-%s.png" % (self.tag, name)), w, h, px)
        self.m.run()


def nonblack(m, x0, y0, x1, y1, tag=None):
    """Lit pixels in a box of the 320 x 200 screen (the framebuffer is doubled on VGA).  On the
    Hercules the box is in the 40-cell text grid, which sits 16 game pixels in from the left of the
    360-wide picture (SPEC.md 102.7); the card's memory is read, not its picture (the framebuffer
    is a monitor's, with the tube's own margins)."""
    if tag == "herc":
        raw = m.read(0xB0000, 0x8000)
        n = 0
        for y in range(y0, y1):
            base = (y & 3) * 0x2000 + (y >> 2) * 90
            for x in range(x0 + 16, x1 + 16, 4):        # a byte is four game pixels
                if raw[base + (x >> 2)]:
                    n += 1
        return n * 2                                     # (about two lit pixels a byte; a floor, not a count)
    w, h, px = m.fbuf(0)
    sx, sy = w // 320, h // 200
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            o = ((y * sy) * w + x * sx) * 3
            if px[o] or px[o + 1] or px[o + 2]:
                n += 1
    return n


def boot(tag, apps=None):
    return os88ui.boot(V.at("build/os8088-360.img"), apps=V.at(apps or "build/excitebike360.img"),
                       machine=V.MACHINE[tag])


def open_flow(ui, tag, sym, ref, art):
    ui.open_drive("B")
    ui.settle()
    c0 = F.claims(ui.m)
    ui.open("EXCBIKE.O88")
    ui.settle()
    fl = Flow(ui, tag, sym, ref, art)
    M.until(ui.m, lambda _: fl.w("xb_reveal") >= fl.w("xb_frontheight") > 0,
            "the splash reveal finishes", guest=30)
    ui.settle()
    c_pre = F.claims(ui.m)
    fl.g.enter(flow=True)
    return fl, c0, c_pre


# ---------------------------------------------------------------------------------
# the items
# ---------------------------------------------------------------------------------
def item_title(fl):
    tag = fl.tag
    assert fl.state() == ST_TITLE
    i0 = fl.w("xb_idle")
    M.guest_sleep(fl.m, .6)
    i1 = fl.w("xb_idle")
    assert i1 > i0 + 5, ("the idle frames do not count", i0, i1)
    assert fl.lit(40, 20, 280, 40) > 50, "the title screen is blank"
    fl.shot("title")
    # B: the best times, Esc back
    fl.key("KeyB")
    fl.wait_state(ST_BEST)
    assert fl.lit(30, 20, 300, 60) > 50, "the best times screen is blank"
    fl.key("Escape")
    fl.wait_state(ST_TITLE)
    # 500 idle frames is not yet the demo; 560 is
    fl.pause_write(lambda: fl.put("xb_idle", 500, 2))
    M.guest_sleep(fl.m, .5)
    assert fl.state() == ST_TITLE, "the demo began before 560 idle frames"
    fl.pause_write(lambda: fl.put("xb_idle", IDLE_ATTRACT - 6, 2))
    fl.wait_state(ST_ATTRACT, guest=20)
    assert fl.b("xb_attract") == 1 and fl.b("xb_nai") == NAI[tag], (
        "the demo is Selection B with the AI driving", fl.b("xb_attract"), fl.b("xb_nai"))
    M.guest_sleep(fl.m, 5.5)                        # the countdown, then GO
    p0 = fl.px()
    M.guest_sleep(fl.m, 1.5)
    p1 = fl.px()
    assert fl.b("xb_paused") == 0 and p1 - p0 > 30, ("the demo's rider is not riding by itself", p0, p1)
    assert fl.hud()[13:20] not in ("PAUSED ", "READY 1"), fl.hud()
    fl.shot("attract")
    fl.key("KeyX")
    fl.wait_state(ST_TITLE, guest=20)
    assert fl.b("xb_attract") == 0 and fl.b("xb_nai") == 0, "a key did not end the demo cleanly"
    print(tag, "title: idle frames count, B lists best times, no demo at 500, the demo at 560 "
          "(Selection B, AI driving, %d opponents), a key ends it: PASS" % NAI[tag], flush=True)


def item_menus(fl):
    """The menus' own keys: A / B letters and Up / Down on the mode, Up / Down on the course list and its
    ends, and Esc back one screen at a time."""
    tag = fl.tag
    fl.key("Enter")
    fl.wait_state(ST_MODE)
    fl.key("KeyB", settle=.3)
    assert fl.b("xb_cur") == 1, "B picks Selection B"
    fl.key("KeyA", settle=.3)
    assert fl.b("xb_cur") == 0, "A picks Selection A"
    fl.key("ArrowUp", settle=.3)
    assert fl.b("xb_cur") == 1, "Up flips the mode"
    fl.key("ArrowDown", settle=.3)
    assert fl.b("xb_cur") == 0, "Down flips it back"
    fl.key("Enter")
    fl.wait_state(ST_TRACK)
    n = 5
    for _ in range(n + 2):                          # past the end: it stops on the last course
        fl.key("ArrowDown", settle=.2)
    assert fl.b("xb_cur") == n - 1, ("the list stops at its last course", fl.b("xb_cur"))
    for _ in range(n + 2):                          # ...and at the first
        fl.key("ArrowUp", settle=.2)
    assert fl.b("xb_cur") == 0, ("the list stops at its first course", fl.b("xb_cur"))
    fl.key("Escape")
    fl.wait_state(ST_MODE)
    fl.key("Escape")
    fl.wait_state(ST_TITLE)
    print(tag, "menus: A / B / Up / Down on the mode, the course list stops at both ends, Esc goes back a "
          "screen at a time: PASS", flush=True)


def banner_pixels(fl):
    """The framebuffer pixels (sx,sy offsets) currently showing palette slot 13 in the top 64 lines: the
    banner plates, pennants (and the arrows below).  Returns (offsets, blue rgb, orange rgb)."""
    rgb = exbsim.X.slot_rgb(fl.art, fl.b("xb_theme"))
    fl.m.pause()
    w, h, px = fl.m.fbuf(0)
    fl.m.run()
    sx, sy = w // 320, h // 200
    blue = rgb[13]
    offs = []
    for y in range(0, 64):
        for x in range(0, 320):
            o = ((y * sy) * w + x * sx) * 3
            if all(abs(px[o + k] - blue[k]) <= 8 for k in range(3)):
                offs.append(o)
    assert len(offs) > 50, ("no banner plate pixels found in the top band", len(offs))
    return offs, blue, rgb[12]


def banner_sample(fl, banner):
    """(xb_bfl, (share of the plate pixels now orange, share still blue))."""
    offs, blue, orange = banner
    fl.m.pause()
    w, h, px = fl.m.fbuf(0)
    bfl = fl.b("xb_bfl")
    fl.m.run()
    def near(o, c):
        return all(abs(px[o + k] - c[k]) <= 8 for k in range(3))
    a = sum(1 for o in offs if near(o, orange)) / len(offs)
    b = sum(1 for o in offs if near(o, blue)) / len(offs)
    return bfl, (a, b)


def item_race_start(fl):
    tag = fl.tag
    # Selection A, the first course: the countdown
    fl.to_race(0, 0, wait_cd=True)
    assert fl.b("xb_nai") == 0, "Selection A has no opponents"
    saw = set()
    for _ in range(80):
        cd = fl.w("xb_cd")
        if cd == 0:
            break
        saw.add(fl.hud()[13:20])
        assert fl.w("xm_stepn") == 0 and fl.w("xb_cs") == 0, "the rider or the clock moved during READY"
        M.guest_sleep(fl.m, .05)
    assert {"READY 3", "READY 2"} <= saw, ("the countdown text", saw)
    for _ in range(20):                             # GO! is on the HUD for 45 steps (0.75 s)
        if fl.hud()[13:20] == "  GO!  ":
            break
        M.guest_sleep(fl.m, .04)
    assert fl.hud()[13:20] == "  GO!  ", ("GO!", fl.hud())
    M.guest_sleep(fl.m, 1.2)
    assert fl.hud()[13:16] != "  G", "GO! did not go away"
    assert fl.w("xm_stepn") > 30, "the rider's steps do not run after GO"
    fl.shot("race-a")
    # the lap flash: the sim reports a lap (xm_ev), the HUD says LAP 2 for 150 steps (2.5 s), then the gauge
    banner = banner_pixels(fl) if tag == "vga" else None
    flashed = []
    seen = []
    for _ in range(12):                              # the sim clears xm_ev at the end of a frame: a poke
        fl.m.pause()                                 # between the report and the clear is lost, so ask
        fl.put("xm_lap", 2)                          # again until the flash shows
        fl.put("xm_ev", 0x10)
        fl.m.run()
        M.guest_sleep(fl.m, .05)
        seen.append(fl.hud()[13:20])
        if seen[-1] == "LAP 2  ":
            break
    assert "LAP 2  " in seen, ("the lap flash", sorted(set(seen)))
    seen = []
    for _ in range(90):
        seen.append(fl.hud()[13:20])
        if banner:
            flashed.append(banner_sample(fl, banner))
        else:
            flashed.append((fl.b("xb_bfl"), None))
        M.guest_sleep(fl.m, .04)
    assert seen[-1] != "LAP 2  ", "the lap flash never ends"
    assert "LAP 2  " in seen[:20], "the lap flash does not last"
    # the BANNER FLASH (video.inc xb_banner): VGA only.  While the lap flash runs DAC 13 alternates with the
    # theme's slot 12; the blue plate pixels seen before the flash turn orange in the framebuffer and back
    # again, and the state byte ends 0.  The CGA and the Hercules never flash (no palette to rewrite).
    states = [b for b, _ in flashed]
    if tag == "vga":
        on = [f for b, f in flashed if b == 1]
        off = [f for b, f in flashed if b == 0]
        assert on and off, ("the banner flash never alternated", states)
        assert max(f[0] for f in on) > .8, ("the flashed banner pixels are not the theme's orange", on)
        assert max(f[1] for f in off) > .8, ("the banner pixels are not the theme's blue between flashes", off)
        assert fl.b("xb_bfl") == 0 and banner_sample(fl, banner)[1][1] > .8, "the banner did not go back to blue"
        print(tag, "banner flash: DAC 13 alternated %d flashed / %d plain samples between blue and orange, back to blue: PASS" %
              (len(on), len(off)), flush=True)
    else:
        assert set(states) == {0}, ("the banner flash ran on an adapter with no palette", states)
    # pause: Enter stops the steps and the HUD says so; Enter resumes
    fl.key("Enter")
    assert fl.b("xb_paused") == 1
    s0 = fl.w("xm_stepn")
    M.guest_sleep(fl.m, .5)
    assert fl.w("xm_stepn") == s0, "steps ran while paused"
    assert fl.hud()[13:20] == "PAUSED ", fl.hud()
    fl.key("Enter")
    assert fl.b("xb_paused") == 0
    M.guest_sleep(fl.m, .5)
    assert fl.w("xm_stepn") > s0, "steps did not resume"
    assert fl.hud()[13:20] != "PAUSED ", fl.hud()
    # the finish: FINISH! on the HUD, the rider coasts 200 steps, then the results - by the game's own timer
    fl.pause_write(lambda: (fl.put("xb_cs", 4500, 2), fl.put("xm_fin", 1)))
    seen = set()
    for _ in range(40):
        seen.add(fl.hud()[13:20])
        if fl.state() == ST_RESULT:
            break
        M.guest_sleep(fl.m, .1)
    assert "FINISH!" in seen, ("the finish message", seen)
    fl.wait_state(ST_RESULT, guest=10)
    assert fl.w("xb_ftime") == fl.w("xb_cs") and abs(fl.w("xb_ftime") - 4500) <= 3, (
        "the finish time is the clock the rider stopped on", fl.w("xb_ftime"), fl.w("xb_cs"))
    fl.key("Escape", settle=.8)
    fl.wait_state(ST_TITLE)
    print(tag, "race start: READY 3/2/1 with the rider and the clock still, GO!, Enter pauses (steps stop, "
          "PAUSED) and resumes, the lap flash, FINISH! and the coast to the results, Esc is the title: PASS", flush=True)
    # Selection B: the opponents
    fl.to_race(1, 1)
    n = NAI[tag]
    assert fl.b("xb_nai") == n, ("opponents", fl.b("xb_nai"), n)
    M.guest_sleep(fl.m, 1.0)
    recs = [fl.g.rd("xa_recs", 16 * 4)[i * 16] for i in range(3)]
    assert recs[:n] == [1] * n and recs[n:] == [0] * (3 - n), ("the opponent records", recs)
    fl.shot("race-b")
    fl.key("Escape", settle=.8)
    fl.wait_state(ST_TITLE)
    print(tag, "Selection B: %d opponents in play, Selection A none: PASS" % n, flush=True)


def item_rank(fl):
    tag = fl.tag
    # the boundaries on course 1, first pass (par is what the compiler says)
    cases = [(-100, 0), (0, 0), (1, 1), (399, 1), (400, 2), (799, 2), (800, 3), (2500, 3)]
    for diff, rank in cases:
        fl.to_race(0, 0)
        par = fl.finish(diff)
        r = fl.result()
        assert r["par"] == par, ("par", r["par"], par)
        assert r["ftime"] == par + diff, ("time", r["ftime"], par + diff)
        assert r["diff"] == diff, ("difference", r["diff"], diff)
        assert r["rank"] == rank, ("rank at par %+d" % diff, r["rank"], RANKS[rank])
        assert r["qual"] == (1 if rank < 3 else 0), ("qualified", r)
        fl.shot("result-%d" % rank) if diff in (0, 2500) else None
        if rank == 3:
            fl.key("Enter")
            fl.wait_state(ST_OVER)
            fl.shot("gameover")
            fl.key("Enter")
            fl.wait_state(ST_TITLE)
        else:
            fl.key("Escape", settle=.7)
            fl.wait_state(ST_TITLE)
    print(tag, "rank: par-100 / par = 1st, +1 / +399 = 2nd, +400 / +799 = 3rd, +800 / +2500 = not qualified; "
          "game over, then the title: PASS", flush=True)


def item_campaign(fl, ref):
    tag = fl.tag
    art = fl.art
    # 1st place on course 1 goes on to course 2 (through the results, Enter); the best time of a course is
    # the fastest ride of the session (the race-start item has already set one: 45.00)
    def best0():
        return struct.unpack_from("<H", fl.g.rd("xb_best", 24), 0)[0]
    par0 = fl.par_of(0, 0)
    b_old = best0()
    assert b_old, "an earlier finish should have left a best time"
    fl.to_race(0, 0)
    fl.finish(b_old - 100 - par0)                  # 1.00 s faster than the best: a NEW best, and 1st
    r = fl.result()
    assert r["newbest"] == 1 and r["rank"] == 0 and best0() == b_old - 100, ("a faster time is a best", r, best0())
    fl.key("Enter", settle=1.0)
    fl.wait_state(ST_RACE)
    assert fl.b("xb_track") == 1 and fl.b("xb_flag") == 0, "the next course"
    assert fl.b("xb_sel") == 0
    fl.skip_countdown()
    # ...a worse ride on course 2 (par + 5.00): 3rd, qualified
    fl.finish(500)
    r = fl.result()
    assert r["rank"] == 2 and r["qual"] == 1, r
    fl.key("Escape", settle=.7)
    fl.wait_state(ST_TITLE)
    # a slower time on course 1 than the best is NOT a new best and leaves it alone
    fl.to_race(0, 0)
    fl.finish(b_old - par0)                        # the old best, 1.00 s slower than the new one
    assert fl.result()["newbest"] == 0, "a slower time is not a best time"
    assert best0() == b_old - 100, ("the best time kept", best0())
    fl.key("Escape", settle=.7)
    fl.wait_state(ST_TITLE)
    print(tag, "campaign: 1st goes to the next course; a faster time is a best time and a slower one is not: "
          "PASS", flush=True)
    # ---- the fifth course, 1st: the second pass starts at course 1 with the harder set
    fl.to_race(0, 4)
    fl.finish(-1)
    fl.key("Enter", settle=1.2)
    fl.wait_state(ST_RACE)
    assert (fl.b("xb_track"), fl.b("xb_flag"), fl.b("xb_rep")) == (0, 1, 0), (
        "the second pass", fl.b("xb_track"), fl.b("xb_flag"), fl.b("xb_rep"))
    fl.skip_countdown()
    ncols = fl.w("xb_ncols")
    exp = ref.course_cids(0, 1)
    got = list(fl.g.rd("xb_cid", len(exp)))
    assert ncols == len(exp), ("the second pass's column count", ncols, len(exp))
    assert got == exp, "the second pass's column array is not pass 2 on both laps"
    first = ref.course_cids(0, 0)
    assert got != first, "the second pass equals the first"
    par2 = art.tracks[0]["par2"]
    fl.finish(0)
    r = fl.result()
    assert r["par"] == par2 and r["rank"] == 0, ("the second pass's par", r, par2)
    # the fifth course of the second pass repeats the fifth, with the windows shrinking
    fl.key("Escape", settle=.7)
    fl.wait_state(ST_TITLE)
    print(tag, "second pass: the fifth course's 1st goes to course 1 with the harder set on both laps (the "
          "column array equals the model's) and its own par: PASS", flush=True)
    fl.to_race(0, 3)
    fl.finish(0, track=4, flag=1)              # the 5th course, second pass
    assert fl.result()["qual"] == 1
    fl.key("Enter", settle=1.2)
    fl.wait_state(ST_RACE)
    assert (fl.b("xb_track"), fl.b("xb_flag"), fl.b("xb_rep")) == (4, 1, 1), (
        "the last course repeats", fl.b("xb_track"), fl.b("xb_flag"), fl.b("xb_rep"))
    fl.skip_countdown()
    # rep = 1: the windows are 350 and 700
    for diff, rank in ((349, 1), (350, 2), (699, 2), (700, 3)):
        fl.finish(diff)
        r = fl.result()
        assert r["rank"] == rank, ("repeat 1, par %+d" % diff, r["rank"], RANKS[rank])
        if rank == 3:
            fl.key("Enter")
            fl.wait_state(ST_OVER)
            fl.key("Enter")
            fl.wait_state(ST_TITLE)
            break
        fl.key("Escape", settle=.7)
        fl.wait_state(ST_TITLE)
        # back into the same repeat: the campaign state is xb_track / xb_flag / xb_rep, which the
        # title's "start" resets - set them as the campaign would have left them
        fl.to_race(0, 4)
        fl.pause_write(lambda: (fl.put("xb_flag", 1), fl.put("xb_rep", 1)))
    print(tag, "last course repeat: at the first repeat 2nd is under +350 and 3rd under +700: PASS", flush=True)


def item_leave(fl, c0, c_pre, close=True):
    tag = fl.tag
    m = fl.m
    # Alt+Enter from the title, from a menu, from a race
    for where in ("title", "menu", "race"):
        if where != "title":
            fl.key("Enter")
            fl.wait_state(ST_MODE)
            if where == "race":
                fl.key("Enter")
                fl.wait_state(ST_TRACK)
                fl.key("Enter")
                fl.wait_state(ST_RACE)
                fl.skip_countdown()
        m.alt("Enter", hold=.15)
        M.until(m, lambda _: fl.b("xb_fs") == 0, "Alt+Enter leaves from the %s" % where, guest=30)
        fl.ui.settle()
        assert F.claims(m) == c_pre, ("Alt+Enter from the %s leaked" % where, c_pre, F.claims(m))
        assert not (m.read(0x417, 1)[0] & 8), "Alt release lost across the mode switch"
        # and back in: the title, records kept
        fl.put("xb_noflow", 0)
        m.key("Enter", up=False)
        M.pace(m, .05)
        m.key("Enter", down=False)
        M.until(m, lambda _: fl.b("xb_fs") == 1 and fl.b("xb_state") == ST_TITLE, "back on the title",
                guest=60)
        fl.wait(lambda: fl.w("xb_idle") > 2, "the title's frames")
    # Esc on the title leaves too
    fl.key("Escape", settle=.5)
    M.until(m, lambda _: fl.b("xb_fs") == 0, "Esc leaves from the title", guest=30)
    fl.ui.settle()
    assert F.claims(m) == c_pre, "Esc from the title leaked"
    assert fl.b("xb_error") == 0
    print(tag, "leaving: Alt+Enter from the title, a menu and a race, and Esc from the title, each back to the "
          "desktop with the claim map exactly as it was: PASS", flush=True)
    if close:
        fl.ui.close(fl.ui.window("Excitebike"))
        fl.ui.settle()
        c_end = F.claims(m)
        assert F.paras(c_end) == F.paras(c0), ("closing the window left the heap changed",
                                               F.paras(c_end), F.paras(c0))
        print(tag, "closed: the heap is back to the desktop's own (%d paragraphs): PASS" % F.paras(c_end),
              flush=True)


def flow_arm(tag, quick):
    art = X.Art()
    ref = exbsim.Ref(art)
    sym = V.symbols()
    with boot(tag) as ui:
        fl, c0, c_pre = open_flow(ui, tag, sym, ref, art)
        item_title(fl)
        item_menus(fl)
        item_race_start(fl)
        item_rank(fl)
        item_campaign(fl, ref)
        item_leave(fl, c0, c_pre)
    print(tag, "flow: PASS", flush=True)


# ---------------------------------------------------------------------------------
# EXBTRACK.DAT
# ---------------------------------------------------------------------------------
def custom_file(art):
    """A designed course: a lap of plain columns, a ramp and a hurdle, as (id, run) pairs and the
    trigger of the ramp's script, in the layout of SPEC.md 102.4."""
    pcs = art.pieces_by_name
    pieces = ["plain_a"] * 14 + ["ramp_b"] + ["plain_c"] * 12 + ["hurdle_lo_a"] + ["plain_b"] * 10 + ["finish"]
    cols, starts = X.lap_columns(pieces, pcs)
    ids = [art.col_ids[c] for c in cols]
    runs = []
    for i in ids:
        if runs and runs[-1][0] == i and runs[-1][1] < 255:
            runs[-1][1] += 1
        else:
            runs.append([i, 1])
    stream = bytes(b for r in runs for b in r) + b"\x00\x00"
    sid = {n: k + 1 for k, n in enumerate(art.scripts)}
    trig = [(x, sid[pcs[p]["script"]]) for x, p in starts if pcs[p]["script"] != "-"]
    par = 3210
    blob = b"EXBT" + struct.pack("<HH", par, len(stream)) + stream + struct.pack("<H", len(trig))
    for x, s in trig:
        blob += struct.pack("<HH", x, s)
    assert 12 <= len(blob) <= 224, len(blob)
    return blob, ids, par


def custom_disk(name, data):
    path = V.at("build/%s.img" % name)
    tmp = V.at("build/%s.dat" % name)
    open(tmp, "wb").write(data)
    files = ["build/excbike.o88", "apps/excitebike/README.md"] + [
        "build/excitebike-art/" + f for f in ("EXBV.GFX", "EXBC.GFX", "EXBH.GFX", "EXBSPL.VGA", "EXBSPL.CGA", "EXBSPL.HRC")]
    named = [V.at(f) for f in files]
    # the file must be called EXBTRACK.DAT on the volume: os88disk names a file by its base name
    real = V.at("build/%s/EXBTRACK.DAT" % name)
    os.makedirs(os.path.dirname(real), exist_ok=True)
    os.replace(tmp, real)
    subprocess.run([sys.executable, V.at("tools/os88disk.py"), "-o", path, "--size", "360"] + named + [real],
                   check=True, cwd=ROOT, stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable, V.at("tools/os88disk.py"), "--verify", path], check=True, cwd=ROOT,
                   stdout=subprocess.DEVNULL)
    return "build/%s.img" % name


def custom_arm(tag):
    art = X.Art()
    ref = exbsim.Ref(art)
    sym = V.symbols()
    blob, ids, par = custom_file(art)
    good = custom_disk("excitebikecustom", blob)
    bad = custom_disk("excitebikecustombad", b"EXBX" + blob[4:])
    short = custom_disk("excitebikecustomshort", blob[:len(blob) - 3])
    with boot(tag, good) as ui:
        fl, c0, c_pre = open_flow(ui, tag, sym, ref, art)
        assert fl.b("xb_custom") == 1, "EXBTRACK.DAT was not taken"
        fl.custom_par = par
        # the course list shows six
        fl.key("Enter")
        fl.wait_state(ST_MODE)
        fl.key("Enter")
        fl.wait_state(ST_TRACK)
        for _ in range(5):
            fl.key("ArrowDown", settle=.3)
        fl.shot("custom-list")
        fl.key("ArrowDown", settle=.3)                # the end of the list: stays
        fl.key("Enter")
        fl.wait_state(ST_RACE)
        assert fl.b("xb_track") == 5, ("the custom course is course 6", fl.b("xb_track"))
        fl.skip_countdown()
        n = fl.w("xb_ncols")
        assert n == 43 + 2 * len(ids), ("columns", n, 43 + 2 * len(ids))
        got = list(fl.g.rd("xb_cid", n))
        assert got[43:43 + len(ids)] == ids and got[43 + len(ids):] == ids, "the stream did not expand as designed"
        fl.finish(-50)
        r = fl.result()
        assert r["par"] == par and r["rank"] == 0, ("the custom par", r)
        fl.key("Enter", settle=1.0)                    # a custom course has no next: the title
        fl.wait_state(ST_TITLE)
        item_leave(fl, c0, c_pre)
    print(tag, "custom: EXBTRACK.DAT is course 6 (par %d, both laps the stream, the trigger of its ramp), "
          "the finish ranks against its par, and the title follows: PASS" % par, flush=True)
    for name, img in (("bad magic", bad), ("truncated", short)):
        with boot(tag, img) as ui:
            fl, c0, c_pre = open_flow(ui, tag, sym, ref, art)
            assert fl.b("xb_custom") == 0, "a damaged EXBTRACK.DAT (%s) was taken" % name
            fl.key("Enter")
            fl.wait_state(ST_MODE)
            fl.key("Enter")
            fl.wait_state(ST_TRACK)
            for _ in range(6):
                fl.key("ArrowDown", settle=.3)
            fl.key("Enter")
            fl.wait_state(ST_RACE)
            assert fl.b("xb_track") == 4, ("five courses only", fl.b("xb_track"))
    print(tag, "custom: a damaged file (bad magic, truncated) is refused and the game has its five courses: "
          "PASS", flush=True)


# ---------------------------------------------------------------------------------
# the opponents' invariants
# ---------------------------------------------------------------------------------
def pad_script(seed, steps):
    """A seeded pseudo-random pad (generated here: no recording): throttle mostly held, the turbo
    often, lane and lean keys in bursts."""
    rnd = random.Random(seed)
    out = []
    while len(out) < steps:
        n = rnd.randrange(8, 60)
        base = rnd.choice((exbsim.INP_A, exbsim.INP_A, exbsim.INP_A | exbsim.INP_B,
                           exbsim.INP_A | exbsim.INP_B, exbsim.INP_B, 0))
        extra = 0
        for bit in (exbsim.INP_U, exbsim.INP_D, exbsim.INP_L, exbsim.INP_R):
            if rnd.random() < 0.2:
                extra |= bit
        out += [base | extra] * n
    return out[:steps]


AI_STEPS = 5000
RING = 256


def ai_arm(tag, steps=AI_STEPS, course=3, flag=1):
    art = X.Art()
    ref = exbsim.Ref(art)
    sym = V.symbols()
    n = NAI[tag]
    script = pad_script(0x4E01, steps)
    sz = sym["xm_stateend"] - sym["xm_state"]

    def off(name):
        return sym[name] - sym["xm_state"]
    with boot(tag) as ui:
        ui.open_drive("B")
        ui.settle()
        ui.open("EXCBIKE.O88")
        ui.settle()
        g = V.Game(ui, tag, sym, ref)
        M.until(ui.m, lambda _: g.w("xb_reveal") >= g.w("xb_frontheight") > 0, "the reveal", guest=30)
        ui.settle()
        g.put("xb_nai", n)
        g.put("xb_track", course)                      # the fourth course, second pass: the most hazards
        g.put("xb_flag", flag)
        g.enter(sim=True)
        m = g.m
        g.put("xb_tmode", 1)
        g.put("xb_tsn", 0, 2)
        g.put("xb_tsi", 0, 2)
        g.put("xb_treset", 1)
        g.put("xb_tpause", 0)
        m.run()
        M.until(m, lambda _: g.b("xb_tpause") == 1 and g.b("xb_treset") == 0, "a fresh field", guest=30)
        m.pause()
        fed = [0]

        def feed(mm):
            tsi = struct.unpack("<H", mm.read(g.a("xb_tsi"), 2))[0]
            while fed[0] < len(script) and fed[0] < tsi + RING - 8:
                k = min(len(script) - fed[0], tsi + RING - 8 - fed[0], RING - (fed[0] % RING), 64)
                mm.write(g.a("xb_tscript") + fed[0] % RING, bytes(script[fed[0]:fed[0] + k]))
                fed[0] += k
        feed(m)
        g.put("xb_tsn", len(script), 2)
        g.put("xb_tsi", 0, 2)
        g.put("xb_tpause", 0)
        ncols = g.w("xb_ncols")
        # per opponent: the last position, steps since it moved 8 pixels, respawn count seen, ticks over
        # the turbo cap in a row
        st = [{"px": None, "still": 0, "resp": 0, "hi": 0, "maxspd": 0, "modes": set()} for _ in range(n)]
        frames = [0]
        samples = [0]
        viol = []
        seen_resp = [0]

        def on_hit(mm, rec):
            feed(mm)
            frames[0] += 1
            if frames[0] % 2:
                return None
            samples[0] += 1
            steps_now = struct.unpack("<H", mm.read(g.a("xb_tsi"), 2))[0]
            blk = mm.read(g.a("xa_st"), sz * n)
            recs = mm.read(g.a("xa_recs"), 16 * 4)
            for i in range(n):
                b = blk[i * sz:(i + 1) * sz]
                d = st[i]
                pos = struct.unpack_from("<I", b, off("xm_pos"))[0]
                px = pos >> 8
                speed = struct.unpack_from("<H", b, off("xm_speed"))[0]
                temp = struct.unpack_from("<H", b, off("xm_temp"))[0]
                col = struct.unpack_from("<H", b, off("xm_col"))[0]
                mode = b[off("xm_mode")]
                lane = b[off("xm_lane")]
                lap = b[off("xm_lap")]
                resp = recs[i * 16 + 6]
                d["modes"].add(mode)
                d["maxspd"] = max(d["maxspd"], speed)

                def bad(msg):
                    viol.append("frame %d opponent %d: %s" % (frames[0], i, msg))
                if mode > 3:
                    bad("mode %d" % mode)
                if speed > P["SPD_MAX"]:
                    bad("speed %#x over the kicker's ceiling %#x" % (speed, P["SPD_MAX"]))
                if not P["LANE_MIN"] <= lane <= P["LANE_MAX"]:
                    bad("lane %d" % lane)
                if col >= ncols + P["COL_AHEAD"]:
                    bad("column %d of %d" % (col, ncols))
                if temp > P["TEMP_MAX"]:
                    bad("temperature %#x" % temp)
                if lap not in (1, 2):
                    bad("lap %d" % lap)
                if speed > P["SPD_B"] + 0x10 and mode == 0:
                    d["hi"] += 1
                    if d["hi"] > 45:                       # ~90 steps over the turbo cap on the ground
                        bad("held %#x above the turbo cap on the ground" % speed)
                else:
                    d["hi"] = 0
                if resp != d["resp"]:
                    ppx = struct.unpack("<H", mm.read(g.a("xm_pos") + 1, 2))[0]
                    dx = ((px - ppx + 32768) & 0xFFFF) - 32768
                    # a respawn puts the opponent at the screen's edge: 110 behind the player or 215
                    # ahead (a column snaps it on to plain ground: up to 24 columns = 192 pixels on)
                    # and a couple of frames of riding since
                    if not (-150 <= dx <= -60 or 200 <= dx <= 260 or px >= (8 * ncols - 112) - 4
                            or px <= 8):                  # (the course's start is as far back as it goes)
                        bad("respawned %d pixels from the player" % dx)
                    seen_resp[0] += resp - d["resp"]
                    d["resp"] = resp
                    d["px"] = None
                    d["still"] = 0
                if d["px"] is not None:
                    if px < d["px"]:
                        bad("went backwards %d -> %d without a respawn" % (d["px"], px))
                    if px - d["px"] < 8:
                        d["still"] += 1
                    else:
                        d["still"] = 0
                    # 700 steps is ~ 320 samples (2.2 steps a sample); the world's end is a stop
                    if d["still"] > 200 and px < ((8 * ncols - 112) - 16):
                        bad("standing still: %d samples without moving 8 pixels (mode %d, speed %#x)" % (
                            d["still"], mode, speed))
                    if px - d["px"] >= 8:
                        d["px"] = px
                else:
                    d["px"] = px
            return None
        with M.bp_trace(m, g.a("xb_presented"), on_hit=on_hit, cap=steps + 1200) as tr:
            tr.until(lambda: g.b("xb_tpause") == 1 or tr.overflowed, "%d steps" % steps, limit=1500)
        assert not tr.overflowed, "the trace buffer overflowed"
        assert g.b("xb_error") == 0
        assert g.w("xb_tsi") >= steps, ("the run stopped early", g.w("xb_tsi"))
        assert not viol, "opponent invariants violated:\n  " + "\n  ".join(viol[:12])
        assert seen_resp[0] >= 1, "no opponent respawned in %d steps" % steps
        f0 = g.w("xb_frames")
        M.guest_sleep(m, .1)
        print("%s AI invariants: %d steps, %d opponents, %d samples, speeds <= %s, modes seen %s, %d respawns: PASS"
              % (tag, steps, n, samples[0], ["%#x" % d["maxspd"] for d in st],
                 sorted(set().union(*[d["modes"] for d in st])), seen_resp[0]), flush=True)


# ---------------------------------------------------------------------------------
# the collision rule, by calling the guest's own routine (xa_pair) on blocks the test writes
# ---------------------------------------------------------------------------------
def call_guest(m, base, sym, name, **args):
    """Run a guest routine to its return (the harness of tests/excitebike_front.py's Probe.call, for
    any label): the registers in `args`, the rest as they were."""
    saved = m.regs()
    regs = ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp", "ss", "ds", "es", "flags")
    m.cmd(cmd="park", cs=base >> 4, ip=sym[name])
    for r in regs:
        m.setreg(r, args.get(r, saved[r]))
    sp = (saved["sp"] - 2) & 65535
    m.setreg("sp", sp)
    m.write((saved["ss"] << 4) + sp, struct.pack("<H", saved["ip"]))
    m.bp_exec((saved["cs"] << 4) + saved["ip"])
    m.run()
    assert m.wait_stop(30) == "breakpoint", name + " never returned"
    out = m.regs()
    for r in regs:
        m.setreg(r, saved[r])
    return out


def collide_arm(tag):
    art = X.Art()
    ref = exbsim.Ref(art)
    sym = V.symbols()
    sz = sym["xm_stateend"] - sym["xm_state"]

    def off(n):
        return sym[n] - sym["xm_state"]
    with boot(tag) as ui:
        ui.open_drive("B")
        ui.settle()
        ui.open("EXCBIKE.O88")
        ui.settle()
        g = V.Game(ui, tag, sym, ref)
        M.until(ui.m, lambda _: g.w("xb_reveal") >= g.w("xb_frontheight") > 0, "the reveal", guest=30)
        ui.settle()
        g.put("xb_nai", 2)
        g.enter(sim=True)
        m = g.m
        g.put("xb_tpause", 1)                          # nothing runs: the blocks below are the test's own
        M.guest_sleep(m, .3)
        m.pause()
        m.bp_exec(g.a("xb_clk_sample"))                # stop INSIDE the package (a routine's return
        m.run()                                        # address is a near one, in our segment)
        assert m.wait_stop(30) == "breakpoint", "the package did not run"
        base = g.base
        A0, A1 = g.sym["xa_st"], g.sym["xa_st"] + sz

        def block(addr, px, lane, speed, mode=0, height=0, fin=0):
            raw = bytearray(m.read(base + addr, sz))
            struct.pack_into("<I", raw, off("xm_pos"), px << 8)
            struct.pack_into("<H", raw, off("xm_speed"), speed)
            struct.pack_into("<H", raw, off("xm_A"), height)
            struct.pack_into("<H", raw, off("xm_gh"), height)
            raw[off("xm_lane")] = lane
            raw[off("xm_mode")] = mode
            raw[off("xm_fin")] = fin
            raw[off("xm_ev")] = 0
            m.write(base + addr, bytes(raw))

        def rd(addr, n, size=2):
            fmt = "<H" if size == 2 else "<B"
            return struct.unpack(fmt, m.read(base + addr + off(n), size))[0]

        def pair():
            call_guest(m, base, sym, "xa_pair", si=A0, bx=A1, ds=base >> 4, es=base >> 4)

        cases = []
        # (name, block 0 (px, lane, speed, mode, height, fin), block 1, expected: (speed0, speed1, mode0, mode1))
        # 1  a touch: 5 px apart in the same lane, block 0 REAR and slower than the crash limit:
        #    the rear loses a quarter, the front gains 16
        block(A0, 100, 26, 0x0300); block(A1, 105, 26, 0x02F0)
        pair()
        assert rd(A0, "xm_speed") == 0x0300 - (0x0300 >> 2), ("rear speed", hex(rd(A0, "xm_speed")))
        assert rd(A1, "xm_speed") == 0x02F0 + 16, ("front speed", hex(rd(A1, "xm_speed")))
        assert rd(A0, "xm_mode", 1) == 0 and rd(A1, "xm_mode", 1) == 0, "a soft touch must not crash"
        # 2  the same, block 1 the rear: the roles swap
        block(A0, 105, 26, 0x02F0); block(A1, 100, 26, 0x0300)
        pair()
        assert rd(A1, "xm_speed") == 0x0300 - (0x0300 >> 2) and rd(A0, "xm_speed") == 0x02F0 + 16, "roles"
        # 3  a hard closing speed (rear 0x300 against 0x100 ahead = 0x200 > 0x140): the rear crashes
        block(A0, 100, 26, 0x0300); block(A1, 106, 27, 0x0100)
        pair()
        assert rd(A0, "xm_mode", 1) == 2, "a hard hit must crash the rear bike"
        assert rd(A1, "xm_mode", 1) == 0, "...and only the rear"
        assert rd(A0, "xm_mtimer") == 72, ("the crash timer", rd(A0, "xm_mtimer"))
        assert rd(A0, "xm_ev", 1) & 1, "a crash event is owed"
        # 4  no touch: 14 pixels apart, 5 lanes apart, 0x1000 of height apart, a crashed bike, a finished one
        for nm, b0, b1 in (("14 px apart", (100, 26, 0x300, 0, 0, 0), (114, 26, 0x300, 0, 0, 0)),
                           ("5 lanes apart", (100, 26, 0x300, 0, 0, 0), (103, 31, 0x300, 0, 0, 0)),
                           ("0x1000 of height apart", (100, 26, 0x300, 1, 0x2000, 0), (103, 26, 0x300, 1, 0x3000, 0)),
                           ("one crashed", (100, 26, 0x300, 2, 0, 0), (103, 26, 0x300, 0, 0, 0)),
                           ("one finished", (100, 26, 0x300, 0, 0, 1), (103, 26, 0x300, 0, 0, 0))):
            block(A0, *b0); block(A1, *b1)
            pair()
            assert rd(A0, "xm_speed") == 0x300 and rd(A1, "xm_speed") == 0x300, ("no touch", nm)
            assert rd(A0, "xm_mode", 1) == b0[3] and rd(A1, "xm_mode", 1) == b1[3], ("mode", nm)
        # 5  exactly at the limits: 13 px and 4 lanes and 0xFFF DO touch
        for nm, b0, b1 in (("13 px", (100, 26, 0x300), (113, 26, 0x300)),
                           ("4 lanes", (100, 26, 0x300), (103, 30, 0x300))):
            block(A0, *b0); block(A1, *b1)
            pair()
            assert rd(A0, "xm_speed") != 0x300 or rd(A1, "xm_speed") != 0x300, ("a touch", nm)
        print(tag, "collision: the rear bike loses a quarter of its speed and the front gains 16; a closing speed "
              "over 0x140 crashes the rear; nothing at 14 pixels, 5 lanes, 0x1000 of height, or with a crashed or "
              "finished rider (the guest's own xa_pair, on blocks the test wrote): PASS", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", choices=("vga", "cga", "herc", "both"), default="both")
    ap.add_argument("--ai", action="store_true", help="the 5,000-step opponent invariants")
    ap.add_argument("--custom", action="store_true", help="EXBTRACK.DAT")
    ap.add_argument("--flow", action="store_true", help="the flow items (the default when nothing else is named)")
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--steps", type=int, default=AI_STEPS)
    ap.add_argument("--collide", action="store_true", help="the collision rule, by calling the guest's own routine")
    a = ap.parse_args()
    tags = ("vga", "cga") if a.adapter == "both" else (a.adapter,)
    run_flow = a.flow or not (a.ai or a.custom or a.collide)
    for t in tags:
        if run_flow:
            flow_arm(t, a.quick)
        if a.custom:
            custom_arm(t)
        if a.collide:
            collide_arm(t)
        if a.ai:
            ai_arm(t, a.steps)
    print("excitebike_flow: PASS")


if __name__ == "__main__":
    main()
