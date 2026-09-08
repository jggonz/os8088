#!/usr/bin/env python3
"""CLEAR SKIES (SPEC.md 88) flies, lands where it should, crashes where it
should, and its frames do not flash.

    python3 tests/skies.py [--machine os8088_5150_herc_gla|os8088_5150_cga_gla|os8088_xt_vga]

FIVE ASSERTIONS, read out of the package's own bss rather than off the glass
wherever the question is about the world (docs/WRITING-TESTS.md 8):

**It draws, and it advances.** `cs_frames` climbs, and the glass differs
between two samples - the same pair SPEC.md 85.1's gate asks, because a page
flip that never latches leaves a counter climbing over a still picture.

**It takes off.** Full throttle on the runway, the stick back once the speed
passes VROT, and the state goes to FLYING with the altitude climbing - the
whole of SPEC.md 88.7's ground model in one flight.

**Nothing stale.** The world paused, the glass against a forced full redraw
of the same scene: the dirty-row scheme's erase must leave no pixel behind,
and the box beside the view must be dark (SPEC.md 88.3.1).

**It crashes, and comes back.** The nose pushed over into the ground is a
crash: `cs_crashes` increments, the picture freezes for CS_CRASHT ticks, and
the aeroplane is back at the airport's reset point on the runway heading with
the engine closed.

**It does not flash** (SPEC.md 85.1's instrument, 88.3): the ink per DISPLAYED
frame priced against its neighbours, with the same 70% floor, on a raster
whose whole view is redrawn and blitted every frame.

**The raster is the one the design names**: the view's width on Hercules is
400 and on CGA 320, read out of cs_ww, because a wrong table row draws a
plausible picture that is simply the wrong size.
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
FLOOR = 70                              # SPEC.md 85.1's margin: a blit caught
                                        # part-way down is tearing, not a gap
CS_ST_GROUND, CS_ST_AIR, CS_ST_CRASH = 0, 1, 2
CS_CRASHT = 36
VROT = 28 * 128                         # apps/skies/csworld.inc's Cessna,
                                        # 16.7 m/s (SPEC.md 88.7.4)
POP = bytes(bin(i).count("1") for i in range(256))


def lit(fb):
    n = 0
    for b in fb:
        n += POP[b]
    return n


def open_game(m):
    """Boot to the desktop and get SKIES.O88 open. Returns (slot, seg, bss)."""
    S = os88sym.linear
    os88marty.settle(m)
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    wx, wy = dispcp.win_rect(m, S, disk)[:2]
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "GAMES")
    wx, wy = dispcp.win_rect(m, S, disk)[:2]
    rows = [r[0] for r in dispcp.listing(m, S)]
    if "SKIES.O88" not in rows:
        sys.exit("skies: SKIES.O88 is not on the apps disk")
    row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                           rows.index("SKIES.O88"))
    x, y = dispcp.row_xy(wx, wy, row)
    mo.dblclick(x, y)
    m.advance(frames=200)
    m.run()
    got = dispapps.pkg_seg(m, 0)
    if got is None:
        sys.exit("skies: SKIES.O88 did not open")
    slot, seg = got
    base = int.from_bytes(m.readseg(seg, 8, 2), "little")
    return slot, seg, base


class Bss:
    def __init__(self, m, seg, base):
        self.m, self.seg, self.base = m, seg, base

    def word(self, name):
        return int.from_bytes(self.m.readseg(
            self.seg, self.base + dispapps.bss_off("skies", name), 2), "little")

    def sword(self, name):
        v = self.word(name)
        return v - 65536 if v >= 32768 else v

    def byte(self, name):
        return self.m.readseg(self.seg,
                              self.base + dispapps.bss_off("skies", name), 1)[0]

    def poke(self, name, data):
        self.m.write((self.seg << 4) + self.base + dispapps.bss_off("skies", name),
                     data)

    def metres(self, name):
        """A 16.8 dword position's whole metres, signed."""
        v = int.from_bytes(self.m.readseg(
            self.seg, self.base + dispapps.bss_off("skies", name), 4), "little")
        if v >= 1 << 31:
            v -= 1 << 32
        return v // 256


def view_rows(m, r, back):
    """The glass, row by row over the VIEW's rows: (inside, outside) - the
    bytes of each row that are the view, and the bytes of the box beside it
    (Hercules only: on CGA the view is the box's whole width)."""
    vy, wh = r.word("cs_vy"), r.word("cs_wh")
    wb0, wbn = r.word("cs_wb0"), r.word("cs_wbn")
    if back == 3:
        fb = m.read(0xB0000, 0x8000)
        box0 = r.word("cs_vx") // 8              # the box's first byte of 90
        rows = [(y & 3) * 0x2000 + (y >> 2) * 90 for y in range(vy, vy + wh)]
        stride = 80
    else:
        fb = m.read(0xB8000, 0x4000)
        box0 = 0
        rows = [(y & 1) * 0x2000 + (y >> 1) * 80 for y in range(vy, vy + wh)]
        stride = 80
    inside, outside = [], []
    for o in rows:
        row = fb[o + box0:o + box0 + stride]
        inside.append(row[wb0:wb0 + wbn])
        outside.append(row[:wb0] + row[wb0 + wbn:])
    return inside, outside


def panel_rows(m, r, back):
    """The glass over the PANEL's rows (below the view), one bytes per row."""
    vy, wh, vh = r.word("cs_vy"), r.word("cs_wh"), r.word("cs_vh")
    if back == 3:
        fb = m.read(0xB0000, 0x8000)
        box0 = r.word("cs_vx") // 8
        return [fb[(y & 3) * 0x2000 + (y >> 2) * 90 + box0:][:80]
                for y in range(vy + wh, vy + vh)]
    fb = m.read(0xB8000, 0x4000)
    return [fb[(y & 1) * 0x2000 + (y >> 1) * 80:][:80] for y in range(wh, vh)]


def until(m, pred, frames, step=15, what="the condition"):
    """Advance the guest in steps until pred() holds, or `frames` are spent.
    The guest's own clock, never the host's (docs/WRITING-TESTS.md 7)."""
    spent = 0
    while spent < frames:
        m.advance(frames=step)
        m.run()
        spent += step
        if pred():
            return True
    return False


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    bad = []

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        slot, seg, base = open_game(m)
        r = Bss(m, seg, base)
        print("  SKIES.O88: window %d, segment %04x, bss at %04x"
              % (slot, seg, base))
        if r.byte("cs_want") == 0:
            bad.append("cs_adapter found no mode on this display")

        m.type_text("f")                        # into the bracket
        m.advance(frames=60)
        m.run()
        # cs_viewh, not cs_wh: cs_pclip BORROWS cs_wh for the length of the
        # panel's own drawing, so a live sample of that word answers with the
        # box's height whenever it lands inside the cockpit (SPEC.md 88.9.2.3)
        back, ww, wh = r.byte("cs_back"), r.word("cs_ww"), r.word("cs_viewh")
        print("  backend %d, view %dx%d in a %dx%d box"
              % (back, ww, wh, r.word("cs_vw"), r.word("cs_vh")))
        if back == 0:
            bad.append("no raster was adopted: the bracket refused its mode")
        # THE ADAPTER'S OWN DEFAULT (SPEC.md 88.13.4): Hercules opens at the
        # table's MODERATE 400-wide view and the other two at cs_fulltab's
        # row, which is the geometry each of them shipped with
        want = {3: (400, 112), 2: (320, 112), 1: (320, 144)}.get(back)
        if want and (ww, wh) != want:
            bad.append("backend %d drew a %dx%d view, not %dx%d (SPEC.md 88.3)"
                       % (back, ww, wh, want[0], want[1]))

        # --- on the runway, engine off, the take-off prompt up ----------------
        st, spd, thr = r.byte("cs_state"), r.word("cs_spd"), r.word("cs_thr")
        print("  at entry: state %d, speed %d, throttle %d, msg %d"
              % (st, spd, thr, r.byte("cs_msg")))
        if st != CS_ST_GROUND or spd or thr:
            bad.append("the first frame is not a parked aeroplane (state %d, "
                       "speed %d, throttle %d)" % (st, spd, thr))
        x0, z0, hdg0 = r.metres("cs_px"), r.metres("cs_pz"), r.word("cs_hdg")

        # --- it draws, and it advances -----------------------------------------
        f0 = r.word("cs_frames")
        m.pause()
        w, h, before = m.fbuf(0)
        m.run()
        m.advance(frames=90)
        m.run()
        f1 = r.word("cs_frames")
        m.pause()
        w, h, after = m.fbuf(0)
        m.run()
        moved = sum(1 for i in range(0, len(before), 3)
                    if before[i:i + 3] != after[i:i + 3])
        print("  frames %d -> %d on a parked aeroplane; %d of %d pixels moved"
              % (f0, f1, moved, w * h))
        if f1 <= f0:
            bad.append("cs_frames did not move (%d -> %d): the loop stopped"
                       % (f0, f1))

        # --- it takes off --------------------------------------------------------
        # A frame slower than CS_MAXSTEP ticks LOSES simulation time (SPEC.md
        # 85.6), so the roll and the climb take longer on the glass than on
        # the clock: Mode X on this 8088 is ~4.3 fps against Hercules' 6.5,
        # and read 6543 of VROT at the end of the Hercules-sized wait
        slow = 2 if back == 1 else 1
        m.key("KeyW", down=True, up=False)      # full throttle...
        # 50 ticks at 2 a tick, and a frame slower than CS_MAXSTEP ticks
        # loses steps (SPEC.md 85.6): 180 frames was 3.0 s on CGA's 60 Hz
        # and read 96 once at 5.5 fps, so the wait is twice what it needs
        ok = until(m, lambda: r.word("cs_thr") >= 100, 400)
        m.key("KeyW", down=False, up=True)
        print("  throttle %d after holding W" % r.word("cs_thr"))
        if not ok:
            bad.append("W held for 400 frames did not open the throttle "
                       "(cs_thr = %d)" % r.word("cs_thr"))
        ok = until(m, lambda: r.word("cs_spd") >= VROT, 1200 * slow, 30)
        print("  speed %d (VROT %d), state %d" % (r.word("cs_spd"), VROT,
                                                 r.byte("cs_state")))
        if not ok:
            bad.append("the aeroplane never reached VROT (%d of %d)"
                       % (r.word("cs_spd"), VROT))
        m.key("ArrowDown", down=True, up=False) # ...the stick back...
        ok = until(m, lambda: r.byte("cs_state") == CS_ST_AIR, 300)
        m.key("ArrowDown", down=False, up=True)
        print("  state %d, pitch %d, altitude %d m"
              % (r.byte("cs_state"), r.sword("cs_pitch"), r.metres("cs_py")))
        if not ok:
            bad.append("the stick back at VROT did not lift off (state %d)"
                       % r.byte("cs_state"))
        # --- AND A PAUSED AEROPLANE IS SILENT (SPEC.md 88.8.1) ---------------
        # cs_sound_step lives inside the sim loop and a pause skips it, so a
        # tone raised before the pause simply held. Asked HERE because this is
        # the one place in the suite with the engine actually running: at
        # entry cs_tone is 0 and a check that reads 0 either side proves
        # nothing.
        tone0 = r.word("cs_tone")
        m.type_text("p")
        ok = until(m, lambda: r.byte("cs_pause") != 0, 200)
        quiet = until(m, lambda: r.word("cs_tone") == 0, 200)
        m.type_text("p")
        back_on = until(m, lambda: r.word("cs_tone") != 0, 300)
        print("  engine tone %d, paused -> %s, resumed -> %d"
              % (tone0, "silent" if quiet else "still sounding",
                 r.word("cs_tone")))
        if not (tone0 and ok):
            bad.append("the engine was not sounding before the pause, so the "
                       "silence proves nothing (tone %d, paused %d)"
                       % (tone0, r.byte("cs_pause")))
        elif not quiet:
            bad.append("PAUSED and the engine tone is still %d (SPEC.md 88.8.1)"
                       % r.word("cs_tone"))
        elif not back_on:
            bad.append("...and it never came back on when the pause ended "
                       "(tone %d)" % r.word("cs_tone"))

        alt0 = r.metres("cs_py")
        ok = until(m, lambda: r.metres("cs_py") >= alt0 + 30, 900 * slow, 30)
        print("  climbed to %d m at %d units of pitch, %d m/s"
              % (r.metres("cs_py"), r.sword("cs_pitch"), r.word("cs_spd") // 128))
        if not ok:
            bad.append("no climb: %d -> %d m" % (alt0, r.metres("cs_py")))
        f2 = r.word("cs_frames")
        m.advance(frames=90)
        m.run()
        f3 = r.word("cs_frames")
        print("  frames %d -> %d in flight" % (f2, f3))
        if f3 <= f2:
            bad.append("cs_frames stopped in flight (%d -> %d)" % (f2, f3))

        # --- Mode X: the page SHOWN is the page drawn (SPEC.md 88.3, 53.10) ---
        # Every row is redrawn on the page nobody is looking at and the flip
        # is what puts it on the glass, so a loop that runs and a frame that
        # is drawn prove nothing on their own: the owner once saw this backend
        # freeze on its first frame with the loop running. The rendered
        # picture, a second apart in flight, has to differ every time.
        # (Broken on purpose before it was registered: the flip patched to
        # always name page 0 and cs_page0 poked to page 1, so nothing is
        # ever drawn on the page shown - frames 6 -> 21 and 3 of 3 samples
        # identical.)
        if back == 1:
            shown = []
            for _ in range(4):
                m.advance(frames=60)
                m.pause()
                shown.append(m.fbuf(0)[2])
                m.run()
            same = sum(1 for a, b in zip(shown, shown[1:]) if a == b)
            print("  rendered frames a second apart in flight: %d of %d identical"
                  % (same, len(shown) - 1))
            if same:
                bad.append("%d of %d rendered frames a second apart were "
                           "identical in flight: the page flip is not showing "
                           "what was drawn (SPEC.md 88.3)" % (same, len(shown) - 1))

        # --- the instruments follow the aeroplane (SPEC.md 88.9.1) -----------
        # The panel rows of the glass, twice a second apart in a climb: the
        # altitude in feet changes every tick, so the rows must differ. (They
        # did not, once: the glyphs marked their spans and not the blit's row
        # range, and an instrument reached the glass only when the throttle
        # bar happened to widen it - 88.3.3.)
        panel0 = None
        if back in (2, 3):
            m.pause()
            panel0 = panel_rows(m, r, back)
            m.run()
            ink = sum(POP[x] for row in panel0 for x in row)
            print("  ink in the panel rows: %d" % ink)
            if back == 3 and ink < 8000:
                bad.append("the panel rows hold %d lit pixels: the cockpit "
                           "(SPEC.md 88.9.2) is not drawn - its face alone is "
                           "a quarter of 640x88" % ink)

        # --- it does not flash (SPEC.md 85.1's instrument) --------------------
        # Straight out of VRAM, once per DISPLAYED frame, in flight, where the
        # whole view is rewritten every frame.
        if back in (2, 3):
            fbseg, banks = (0xB800, 2) if back == 2 else (0xB000, 4)
            samples = []
            for _ in range(FRAMES):
                m.advance(frames=1)
                m.pause()
                samples.append(lit(m.read(fbseg << 4, banks * 0x2000)))
                m.run()
            s = sorted(samples)
            med = s[len(s) // 2]
            worst, at = 100, 0
            for i in range(1, len(samples) - 1):
                near = min(samples[i - 1], samples[i + 1])
                if near <= 0:
                    continue
                rr = 100 * samples[i] // near
                if rr < worst:
                    worst, at = rr, i
            print("  ink per displayed frame: min %d median %d max %d over %d; "
                  "thinnest against its neighbours %d%% (frame %d)"
                  % (s[0], med, s[-1], len(s), worst, at))
            if med == 0:
                bad.append("nothing is lit on any frame")
            elif worst < FLOOR:
                bad.append("a displayed frame carried %d%% of the ink of the "
                           "two around it: the frame is being taken apart ON "
                           "THE GLASS (SPEC.md 85.1, 88.3)" % worst)

        # --- nothing stale on the glass (SPEC.md 88.3.1) ----------------------
        # The world paused, the glass as the dirty-row scheme left it against
        # the same scene with every row forced to refill and redraw: a pixel
        # that differs is one the scheme failed to erase. The first build
        # refilled fifteen bytes to the right of every erase and passed every
        # other check here; it also lit a bar past the view's edge, so the
        # box's bytes beside the view have to be dark too.
        if panel0 is not None:
            m.advance(frames=30)
            m.pause()
            panel1 = panel_rows(m, r, back)
            m.run()
            changed = sum(1 for a, b in zip(panel0, panel1) if a != b)
            print("  panel rows that changed over a second of climb: %d of %d"
                  % (changed, len(panel0)))
            if not changed:
                bad.append("the instruments did not change on the glass over a "
                           "second of climb (SPEC.md 88.9.1, 88.3.3)")

        def stale_check(when):
            r.poke("cs_pause", b"\x01")
            m.advance(frames=20)
            m.pause()
            before, outside = view_rows(m, r, back)
            m.run()
            r.poke("cs_rowkind", b"\x03" * r.word("cs_wh"))
            m.advance(frames=20)
            m.pause()
            after, _ = view_rows(m, r, back)
            m.run()
            r.poke("cs_pause", b"\x00")
            stale = sum(POP[x ^ y] for a, b in zip(before, after)
                        for x, y in zip(a, b))
            spill = sum(POP[x] for row in outside for x in row)
            print("  stale pixels %s: %d of %d; lit beside the view: %d"
                  % (when, stale, len(before) * len(before[0]) * 8, spill))
            if stale > 8:
                bad.append("%d pixels on the glass differ from a full redraw of "
                           "the same scene %s: the dirty-row scheme leaves "
                           "stale pixels (SPEC.md 88.3.1)" % (stale, when))
            if spill:
                bad.append("%d pixels lit in the box beside the view %s: the "
                           "refill or the blit reaches past it (SPEC.md "
                           "88.3.1)" % (spill, when))

        if back in (2, 3):
            stale_check("after a straight climb")
            # ...and after the nose comes up further: rows go from ground to
            # sky with nothing drawn on them, which is the case the first
            # build's kind byte never saw (it was always written as 0)
            m.key("ArrowDown", down=True, up=False)
            m.advance(frames=45)
            m.run()
            m.key("ArrowDown", down=False, up=True)
            m.advance(frames=15)
            m.run()
            stale_check("after pitching up")

        # --- it crashes, and comes back ------------------------------------------
        c0 = r.word("cs_crashes")
        m.key("ArrowUp", down=True, up=False)   # the nose over, into the ground
        ok = until(m, lambda: r.byte("cs_state") == CS_ST_CRASH, 1500, 30)
        m.key("ArrowUp", down=False, up=True)
        print("  crash: state %d, crashes %d -> %d, why at %04x"
              % (r.byte("cs_state"), c0, r.word("cs_crashes"),
                 r.word("cs_crashwhy")))
        if not ok:
            bad.append("the nose held down never met the ground (state %d, "
                       "altitude %d m)" % (r.byte("cs_state"), r.metres("cs_py")))
        elif r.word("cs_crashes") != c0 + 1:
            bad.append("cs_crashes read %d after one crash" % r.word("cs_crashes"))
        # READ IT AT THE RESET, not fifteen frames later. `until` steps in
        # blocks, so the aeroplane has been flying again for up to a block by
        # the time the loop returns - and the throttle is the one field that
        # moves on its own, which is how this read 6 for 0 under a loaded box
        # and passed every time it ran alone (docs/WRITING-TESTS.md 13 row 8).
        at = {}

        def landed():
            if r.byte("cs_state") != CS_ST_GROUND:
                return False
            at.setdefault("p", (r.metres("cs_px"), r.metres("cs_pz"),
                                r.word("cs_hdg"), r.word("cs_thr")))
            return True

        ok = until(m, landed, CS_CRASHT * 4 + 60, 15)
        x1, z1, hdg1, thr1 = at.get("p", (r.metres("cs_px"), r.metres("cs_pz"),
                                          r.word("cs_hdg"), r.word("cs_thr")))
        print("  after the crash: state %d at (%d, %d) heading %d; spawned at "
              "(%d, %d) heading %d; throttle %d"
              % (r.byte("cs_state"), x1, z1, hdg1, x0, z0, hdg0, thr1))
        if not ok:
            bad.append("the crash never reset (state %d after %d ticks)"
                       % (r.byte("cs_state"), CS_CRASHT * 4))
        elif (x1, z1) != (x0, z0) or hdg1 != hdg0 or thr1:
            bad.append("the reset did not put the aeroplane back where it "
                       "started: (%d, %d)/%d against (%d, %d)/%d, throttle %d"
                       % (x1, z1, hdg1, x0, z0, hdg0, thr1))

        m.type_text("f")                        # ...and F leaves (SPEC.md 11.2.1)
        m.advance(frames=90)
        m.run()
        q = r.byte("cs_quit")
        print("  after F: cs_quit %d" % q)
        if not q:
            bad.append("F did not leave the bracket")

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
