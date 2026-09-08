#!/usr/bin/env python3
"""THE HORIZON CAPTURES THE LAST THREE TICKS (SPEC.md 88.7.3), on MartyPC.

    python3 tests/skiesease.py [--machine os8088_5150_herc_gla]

A key is a stick that is hard over or centred, so an axis moves in whole
CSP_ROLLR quanta and can only STOP where the quanta fall - 10 degrees on the
Pitts, three ticks to a frame, so the pilot's quantum is 30 degrees and level
flight is not in it. `cs_ease` shortens the last three ticks of an approach
so the angle lands ON the horizon instead of stepping over it.

Every reading here is taken at a `cs_step` BREAKPOINT and not after a frame:
the model steps per tick and a frame spends one, two or three of them, so a
per-frame sample cannot see whether the angle passed through zero or landed
on it - which is the whole question.

  1. held toward level from an angle that is NOT a multiple of the rate, both
     aeroplanes and both axes land EXACTLY on the horizon, within three ticks
     of coming inside three rates of it;
  2. the eased ticks are not a crawl - each is at least 60% of the full rate,
     which is what keeps a continuous roll from hitching at level;
  3. held AWAY from the horizon nothing is eased: every tick is the full rate;
  4. and the Pitts' horizon is a HALF TURN - held toward inverted from 173
     degrees it lands exactly on 180, which is where an aerobatic aeroplane
     flies upside down.

--clobber-ease is the red run (docs/WRITING-TESTS.md 1): it puts a `ret` on
cs_ease's first byte, which returns the step unchanged - the model exactly as
it was - and checks 1 and 4 must go red.

There WAS a --clobber-hold beside it, which NOPed the one instruction that
arms 88.7.3.1's detent, and it is gone: since 88.7.3.2 the approach is
arranged to land on a FRAME'S LAST TICK, so the hold has no ticks left to
discard and removing it changes not one reading here. It is a rounding
safety net now - it still fires when the spread lands a tick early - and a
knob that cannot go red is worse than no knob at all (docs/WRITING-TESTS.md
1). --clobber-ease covers the whole mechanism.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEG = 65536.0 / 360.0
CSP_ROLLR, CSP_PITCHR = 16, 20
HALF = 32768
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def sg(v):
    return v - 0x10000 if v >= 0x8000 else v


def tohorizon(a):
    """The signed distance from a 16-bit angle to the nearest half turn."""
    d = a & 0x7FFF
    return (HALF - d) if d >= 0x4000 else -d


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-ease", action="store_true",
                    help="return the step unchanged: the row must go red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    mp = dispapps._map("skies")

    def off(n):
        return dispapps.bss_off("skies", n)

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")

        def w(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")

        def byte(n):
            return m.readseg(seg, base + off(n), 1)[0]

        def poke(n, d):
            m.write(lin + base + off(n), d)

        def rec(at, o):
            return int.from_bytes(m.readseg(seg, at + o, 2), "little")

        m.advance(frames=30)
        m.run()
        if a.clobber_ease:
            m.pause()
            m.write(lin + mp["cs_ease"], b"\xC3")
            m.run()
            print("  (cs_ease returns its step unchanged: this run must fail)")
        def pick(row):
            po = [rec(mp["cs_drplane"], 2 * i) for i in range(4)]
            ui.mo.click((po[0] + po[2]) // 2, (po[1] + po[3]) // 2)
            m.advance(frames=20)
            m.run()
            # THE OPEN LIST'S FIRST ROW IS OS88UI_DR_TOP (SPEC.md 13.14.2)
            # and no longer the row under the box: a list that would not fit
            # below its control slides UP into the window. The Plane list is
            # short enough that the two agree today, and the arithmetic that
            # assumed it would drift silently the moment a sixth aeroplane is
            # written - it would click a row and pick another.
            top = rec(mp["cs_drplane"], 22)
            ui.mo.click(po[0] + 20, top + 1 + 12 * row + 6)
            m.advance(frames=20)
            m.run()

        def airborne(roll, pitch):
            """Level at 600 m and 70 m/s, the attitude pinned, engine in.

            Called with the guest ALREADY HALTED at a cs_step breakpoint, so
            it neither pauses nor resumes."""
            for nm, v in (("cs_px", -2400), ("cs_py", 600), ("cs_pz", -2000)):
                poke(nm, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", (7282).to_bytes(2, "little"))
            poke("cs_pitch", (pitch & 0xFFFF).to_bytes(2, "little"))
            poke("cs_roll", (roll & 0xFFFF).to_bytes(2, "little"))
            poke("cs_spd", (70 * 128).to_bytes(2, "little"))
            poke("cs_thr", (100).to_bytes(2, "little"))
            poke("cs_state", b"\x01")
            # ...AND THE RAMP OUT IS CLEARED WITH IT (SPEC.md 88.7.3.3).
            # Pinning an attitude is a TELEPORT: cs_hzo holds the rate the
            # LAST approach came down to and how fast it is climbing back,
            # which describes an aeroplane we are no longer flying. Left
            # alone it caps the first ticks after the pin and the row reads
            # `held AWAY from level every tick is the full rate` as
            # [974, 1143, 1312, 1481, 1650] - a ramp, correctly, of the
            # wrong flight
            m.write(lin + base + off("cs_hzo"), b"\x00" * 8)

        def axis_of(key):
            return "cs_kroll" if key in ("ArrowLeft", "ArrowRight") \
                else "cs_kpitch"

        def keyclear(key, tries=40):
            """...and the PREVIOUS key is let go before the next goes down.

            The two arrows on one axis are -1 and +1 in the same byte, so a
            press that lands while the last break code is still in flight
            reads as BOTH DOWN - which is 0, the same byte a key nobody has
            got. `keydown` then returned at once on a stale latch and the row
            measured six ticks of an aeroplane holding perfectly still, which
            is how `held AWAY from level every tick is the full rate` came to
            read [0, 0, 0, 0, 0]."""
            nm = axis_of(key)
            for i in range(tries):
                if byte(nm) == 0:
                    return
                m.advance(frames=4)
                m.run()
                if i and i % 10 == 0:
                    # A LOST BREAK CODE IS SELF-HEALING AND ONLY THAT WAY
                    # (SPEC.md 9.7): the key-down map is advice, and a break
                    # dropped inside a long IF=0 window leaves the bit set
                    # until that key is pressed again. So press and release
                    # BOTH arrows of this axis rather than waiting longer.
                    for k in (("ArrowLeft", "ArrowRight") if nm == "cs_kroll"
                              else ("ArrowUp", "ArrowDown")):
                        m.key(k, down=True, up=False)
                        m.advance(frames=2)
                        m.run()
                        m.key(k, down=False, up=True)
                        m.advance(frames=2)
                        m.run()
            sys.exit("skiesease: %s never came back up ([%s] = %d)"
                     % (key, nm, byte(nm)))

        def keydown(key, tries=40):
            """Run until the GUEST says it has the key, and not a fixed wait.

            A press is delivered to the emulator's input queue and reaches
            the guest some ticks later; `advance(frames=6)` was enough on an
            idle box and not on a loaded one, where this row failed with the
            aeroplane standing still for seven ticks and then flying - which
            reads exactly like the model being broken. [cs_kroll] and
            [cs_kpitch] are the guest's own answer to 'have you got it'."""
            nm = axis_of(key)
            for _ in range(tries):
                m.advance(frames=4)
                m.run()
                if byte(nm) != 0:
                    return
            sys.exit("skiesease: the guest never saw %s ([%s] stayed 0)"
                     % (key, nm))

        def ticks(key, name, pin, n=10):
            """The angle at the top of each of n consecutive cs_step calls.

            THE KEY GOES DOWN FIRST and the attitude is pinned at the FIRST
            stop, not before it: a poke followed by a free run loses the
            approach to the ticks that pass while the breakpoint is being
            armed, and the trainer's own return-to-level eats it besides."""
            keyclear(key)                       # the LAST key is up first
            m.key(key, down=True, up=False)
            keydown(key)                        # ...CONFIRMED, not waited for
            m.bp_exec(lin + mp["cs_step"])
            m.run()
            if m.wait_stop(30) is None:
                sys.exit("skiesease: cs_step never ran")
            pin()                               # halted at the top of a tick
            out, left = [], []
            for _ in range(n):
                out.append(sg(w(name)))
                left.append(byte("cs_tleft"))   # ...and WHERE IN THE FRAME
                m.run()                         # this tick falls (88.7.3.2)
                if m.wait_stop(30) is None:
                    sys.exit("skiesease: cs_step never ran")
            m.bp_exec()
            m.run()
            m.key(key, down=False, up=True)
            return out, left

        def run(plane, axis, start_deg, key, rate, label, want_half=False):
            angle = int(start_deg * DEG)
            seq, left = ticks(key, axis,
                              lambda: airborne(angle if axis == "cs_roll"
                                               else 0,
                                               0 if axis == "cs_roll"
                                               else angle))
            deg = [round(x / DEG, 2) for x in seq]
            print("      %-22s %s" % (label, deg))
            # the first reading is the pinned start; the model has not run yet
            target = HALF if want_half else 0
            landed = [i for i, x in enumerate(seq)
                      if (abs(x) if not want_half else abs(abs(x) - HALF)) == 0]
            check(bool(landed),
                  "%s lands EXACTLY on the horizon (%s)"
                  % (label, "tick %d" % landed[0] if landed else "never: %s" % deg))
            if not landed:
                return
            k = landed[0]
            inside = [i for i in range(k) if abs(tohorizon(seq[i])) <= 3 * rate]
            if inside:
                check(k - inside[0] <= 3,
                      "%s takes at most three ticks from inside three rates "
                      "(%d)" % (label, k - inside[0]))
                steps = [abs(seq[i + 1] - seq[i]) for i in range(inside[0], k)]
                steps = [s if s < HALF else 65536 - s for s in steps]
                # THE SPREAD IS EVEN, and that is the contract 88.7.3.2 put
                # in place of a floor under the tick. There is no floor any
                # more and there cannot be one: the distance a frame has left
                # to travel is whatever it is, and pinned 7 degrees from
                # inverted the Pitts covers 7 degrees in that frame however
                # they are divided. What the ladder promises is that they are
                # divided EQUALLY - a plateau and not a dive - and that the
                # last of them is the frame's last tick, so the hold discards
                # nothing. The old check read 60% of the rate and passed on
                # 78%-then-22%, which is the shape the field called a pause.
                check(max(steps) - min(steps) <= 1,
                      "%s: the eased ticks are EQUAL, so the approach is a "
                      "plateau and not a dive (%s)" % (label, steps))
                # seq[i] is read at the TOP of tick i, so the tick that
                # LANDED is k-1 and its cs_tleft is left[k-1]
                check(left[k - 1] == 1,
                      "%s lands on the frame's LAST tick, so the hold "
                      "discards none (cs_tleft %d)" % (label, left[k - 1]))

        for row, name in ((0, "CESSNA"), (1, "PITTS")):
            if byte("cs_back") != 0:
                m.type_text("f")
                m.advance(frames=60)
                m.run()
            pick(row)
            plane = w("cs_plane")
            check(plane == rec(mp["cs_planes"], 2 * row),
                  "%s: the list's row %d is in hand (%04x)" % (name, row, plane))
            m.type_text("f")
            m.advance(frames=80)
            m.run()
            check(byte("cs_back") != 0, "%s: the bracket took a mode" % name)
            rr = rec(plane, CSP_ROLLR)
            pr = rec(plane, CSP_PITCHR)
            print("    --- %s: ROLLR %d (%.1f deg), PITCHR %d (%.1f deg)"
                  % (name, rr, rr / DEG, pr, pr / DEG))
            # 1/2 - toward level on both axes, from angles the rate cannot hit
            run(plane, "cs_roll", 2.6 * rr / DEG, "ArrowLeft", rr,
                "%s roll -> level" % name)
            # ArrowDown is the stick BACK - the nose comes UP - so a nose
            # below the horizon is brought to it with Down, not Up
            run(plane, "cs_pitch", -2.4 * pr / DEG, "ArrowDown", pr,
                "%s pitch -> horizon" % name)
            # 3 - away from it, nothing eased
            angle = int(0.4 * rr)
            seq, _ = ticks("ArrowRight", "cs_roll",
                           lambda: airborne(angle, 0), 6)
            steps = [seq[i + 1] - seq[i] for i in range(len(seq) - 1)]
            check(all(s == rr for s in steps),
                  "%s: held AWAY from level every tick is the full rate (%s)"
                  % (name, steps))

        # 4 - the Pitts' other horizon: INVERTED level
        run(w("cs_plane"), "cs_roll", 173.0, "ArrowRight",
            rec(w("cs_plane"), CSP_ROLLR), "PITTS roll -> inverted",
            want_half=True)

        # 5 - AND A FRAME IS DRAWN ON IT (SPEC.md 88.7.3.1), which is the
        #     half a per-TICK sample cannot see and the pilot only ever sees.
        #     cs_steps spends up to three ticks between renders, so before
        #     the hold the ease landed mid-frame and the frame's remaining
        #     ticks carried the axis straight off: sampled once a frame from
        #     26 deg it read -40.0, -13.4, +10.0, +40.0 and level was in the
        #     gap. Every start angle is asked, because ONE of them landing on
        #     a frame boundary by luck is exactly what the old code did.
        rd = mp["cs_render"]
        lvl = []
        for start in (26, 28, 30):
            # THE KEY GOES DOWN FIRST AND THE PIN AT THE FIRST STOP, ticks()'
            # own reason: a poke followed by a free run loses the approach to
            # the frames that pass while the breakpoint is being armed
            keyclear("ArrowLeft")
            m.key("ArrowLeft", down=True, up=False)
            keydown("ArrowLeft")
            m.bp_exec(lin + rd)
            m.run()
            if m.wait_stop(30) is None:
                sys.exit("skiesease: cs_render never ran")
            airborne(int(start * DEG), 0)
            rolls = []
            for _ in range(22):
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skiesease: cs_render never ran")
                rolls.append(sg(w("cs_roll")))
            m.bp_exec()
            m.run()
            m.key("ArrowLeft", down=False, up=True)
            m.advance(frames=8)
            m.run()
            n = sum(1 for r in rolls if r == 0)
            horiz = [r for r in rolls if r == 0 or abs(r) == HALF]
            lvl.append((n, len(horiz)))
            print("      PITTS frames from %2d deg: %s"
                  % (start, [round(r / DEG, 1) for r in rolls[:12]]))
            # A DETENT AND NOT A STOP: the roll carries on through it, and
            # EVERY horizon the roll passes gets a frame - a level-only count
            # cannot tell one held horizon from four, and 22 frames at 30 deg
            # a frame is nearly two whole rolls
            check(n >= 1 and len(horiz) >= 3 and len(horiz) < len(rolls) // 3,
                  "PITTS from %d deg: a FRAME is drawn on EVERY horizon the "
                  "roll passes, and the roll carries on (%d level, %d "
                  "horizons of %d frames)" % (start, n, len(horiz), len(rolls)))
        check(all(x[0] >= 1 and x[1] >= 3 for x in lvl),
              "...on every start angle, which is what makes it the hold and "
              "not luck (%s)" % [x for x in lvl])
        m.type_text("f")
        m.advance(frames=40)
        m.run()

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
