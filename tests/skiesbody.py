#!/usr/bin/env python3
"""THE ELEVATOR IS A BODY RATE (SPEC.md 88.7.8), on MartyPC.

    python3 tests/skiesbody.py [--machine os8088_5150_herc_gla]

Every model used to add the elevator straight into [cs_pitch], which is a
rotation about the WORLD's wing axis and the aeroplane's only while the
wings are level. Both faces of that were reported off the glass, and this
row is one check each, read at a cs_step breakpoint so a tick is a tick.

  1. WINGS LEVEL nothing changes: the whole tick is world pitch, at the
     record's CSP_PITCHR, and the heading does not move. The conversion is
     cos(0) and sin(0), so this is the check that says the fix costs the
     ordinary case nothing.

  2. IN A 90 DEGREE BANK back-pressure is ALL TURN: the world pitch stands
     still and the heading moves by CSP_TURNK plus the whole of CSP_PITCHR,
     which is the turn getting tighter under the stick. "Holding up makes
     the plane go towards the sky rather than making the turn steeper" was
     the report.

  3. OVER THE TOP OF A LOOP the controls are still the right way round.
     Pitch 180 and roll 180 is the aeroplane upright and facing back - what
     you get by looping and then rolling level - and cos(roll) is -1 there,
     so nose-up must move [cs_pitch] DOWN and the aeroplane must CLIMB.
     [cs_vs] is what says it climbed, since that is sin(pitch) x speed and
     not an angle anyone has to reason about.

  4. And the trainer is wired to the same thing through cs_axisp: a Cessna
     in its steepest bank turns measurably faster with the stick back than
     with it centred.

AND ONE FIELD REPORT LATER, CHECK 5 (SPEC.md 88.7.8.1). 88.7.8 drops the
`1/cos(pitch)` on the heading term for being singular at the vertical, and
dropped its SIGN with it - so past a quarter turn, which is where a loop
leaves the pitch and nothing puts it back, a banked pull turned the wrong
way: *"after acrobatics the elevator inverts relative to the banked turn -
in a 90 degree left bank, holding up will go to the right"*. Check 5 reads
the elevator's OWN contribution to the heading (held minus centred, one tick
from the pinned attitude) and requires it to reverse past the vertical, the
way cos(pitch) does. --clobber-invert is its red run and NOPs the four
instructions that keep the sign.

--clobber-body is the red run for checks 1 to 4 (docs/WRITING-TESTS.md 1): a `ret` on
cs_elev's first byte returns the body rate unresolved and adds nothing to
the heading, which is the model exactly as it was, and checks 2, 3 and 4 go
red.
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEG = 65536.0 / 360.0
CSP_PITCHR, CSP_TURNK, CSP_MAXROLL = 20, 24, 28
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def sg(v):
    return v - 0x10000 if v >= 0x8000 else v


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-invert", action="store_true",
                    help="drop 88.7.8.1's sign again: check 5 goes red")
    ap.add_argument("--clobber-body", action="store_true",
                    help="cs_elev returns the rate unresolved: rows go red")
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
        if a.clobber_body:
            m.pause()
            m.write(lin + mp["cs_elev"], b"\xC3")
            m.run()
            print("  (cs_elev returns the rate unresolved: this run must fail)")

        def fly(row):
            if byte("cs_back") != 0:
                m.type_text("f")
                m.advance(frames=60)
                m.run()
            po = [rec(mp["cs_drplane"], 2 * i) for i in range(4)]
            ui.mo.click((po[0] + po[2]) // 2, (po[1] + po[3]) // 2)
            m.advance(frames=20)
            m.run()
            top = rec(mp["cs_drplane"], 22)
            ui.mo.click(po[0] + 20, top + 1 + 12 * row + 6)
            m.advance(frames=20)
            m.run()
            plane = w("cs_plane")
            check(plane == rec(mp["cs_planes"], 2 * row),
                  "row %d picks the record it names (%04x)" % (row, plane))
            m.type_text("f")
            m.advance(frames=80)
            m.run()
            check(byte("cs_back") != 0, "row %d: the bracket took a mode" % row)
            return plane

        def pin(pitch_deg, roll_deg):
            for nm, v in (("cs_px", -2400), ("cs_py", 900), ("cs_pz", -2000)):
                poke(nm, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", (7282).to_bytes(2, "little"))
            poke("cs_pitch", (int(pitch_deg * DEG) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_roll", (int(roll_deg * DEG) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_rrate", b"\x00\x00")
            poke("cs_prate", b"\x00\x00")
            poke("cs_taproll", b"\x00")
            poke("cs_tappitch", b"\x00")
            poke("cs_spd", (70 * 128).to_bytes(2, "little"))
            poke("cs_thr", (100).to_bytes(2, "little"))
            poke("cs_state", b"\x01")

        def ticks(key, pitch_deg, roll_deg, n=5):
            """Hold `key`, pin the attitude, and read the state at the top of
            each of n consecutive cs_step calls."""
            if key:
                m.key(key, down=True, up=False)
                # CONFIRMED and not waited for: a press reaches the guest
                # some ticks after the emulator is handed it, and a fixed
                # wait that is enough on an idle box is not on a loaded one
                nm = ("cs_kroll" if key in ("ArrowLeft", "ArrowRight")
                      else "cs_kpitch")
                for i in range(20):
                    m.advance(frames=2)
                    m.run()
                    if byte(nm) != 0:
                        break
                else:
                    sys.exit("skiesbody: the guest never saw %s" % key)
            m.bp_exec(lin + mp["cs_step"])
            m.run()
            if m.wait_stop(30) is None:
                sys.exit("skiesbody: cs_step never ran")
            pin(pitch_deg, roll_deg)
            out = []
            for _ in range(n):
                out.append((sg(w("cs_pitch")), w("cs_hdg"), sg(w("cs_vs"))))
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skiesbody: cs_step never ran")
            m.bp_exec()
            m.run()
            if key:
                m.key(key, down=False, up=True)
            return out

        def deltas(seq, i):
            return [sg((seq[k + 1][i] - seq[k][i]) & 0xFFFF)
                    for k in range(len(seq) - 1)]

        # =====================================================================
        pitts = fly(1)
        pr = rec(pitts, CSP_PITCHR)
        tk = rec(pitts, CSP_TURNK)
        print("    --- PITTS: PITCHR %d (%.1f deg), TURNK %d"
              % (pr, pr / DEG, tk))

        # --- 1. wings level: all pitch, no heading ---------------------------
        seq = ticks("ArrowDown", 0, 0)          # Down is the stick BACK
        dp, dh = deltas(seq, 0), deltas(seq, 1)
        print("      level:   pitch %s  hdg %s" % (dp, dh))
        check(all(abs(x - pr) <= 1 for x in dp),
              "wings level, the whole tick is world PITCH at CSP_PITCHR (%s "
              "against %d)" % (dp, pr))
        check(all(x == 0 for x in dh),
              "...and the heading does not move at all (%s)" % dh)

        # --- 2. a 90 degree bank: all turn, no pitch -------------------------
        seq = ticks("ArrowDown", 0, 90)
        dp, dh = deltas(seq, 0), deltas(seq, 1)
        print("      90 bank: pitch %s  hdg %s" % (dp, dh))
        check(all(abs(x) <= 2 for x in dp),
              "in a 90 degree bank the world PITCH stands still (%s)" % dp)
        check(all(abs(x - (tk + pr)) <= 3 for x in dh),
              "...and the whole of CSP_PITCHR goes into the TURN, on top of "
              "CSP_TURNK (%s against %d)" % (dh, tk + pr))

        # --- 3. over the top of a loop ---------------------------------------
        seq = ticks("ArrowDown", 180, 180)
        dp = deltas(seq, 0)
        vs = [s[2] for s in seq]
        print("      inverted-and-rolled-level: pitch %s  vs %s" % (dp, vs))
        check(all(x < 0 for x in dp),
              "at roll 180 the same key moves [cs_pitch] the OTHER way (%s)"
              % dp)
        check(vs[-1] > 0 and vs[-1] > vs[0],
              "...which is the nose coming UP: the aeroplane climbs (%s)" % vs)

        # --- 4. the trainer is wired to it too -------------------------------
        c172 = fly(0)
        mx = rec(c172, CSP_MAXROLL)
        bank = mx / DEG
        held = deltas(ticks("ArrowDown", 0, bank), 1)
        idle = deltas(ticks(None, 0, bank), 1)
        print("      C172 at %.0f of bank: hdg held %s, centred %s"
              % (bank, held, idle))
        check(held[-1] > idle[-1],
              "the trainer turns FASTER with the stick back than centred "
              "(%d against %d a tick)" % (held[-1], idle[-1]))

        # --- 5. past the vertical the TURN reverses (SPEC.md 88.7.8.1) -------
        # The wing axis has a vertical component of cos(pitch) sin(roll), so
        # the elevator's own contribution to the heading follows cos(pitch)
        # and REVERSES past a quarter turn. It did not: 88.7.8 dropped the
        # 1/cos(pitch) for being singular at the vertical and dropped its sign
        # with it, so after a loop a banked pull turned the wrong way - "in a
        # 90 degree left bank, holding up will go to the right".
        #
        # ONE DELTA A POSE, from the pinned attitude: cs_elev reads cs_pitch
        # before this tick's update, so the first tick is the only one that
        # is at the pitch that was asked for - the clamp drags the rest back
        # toward CSP_MAXPITCH.
        if a.clobber_invert:
            pat = (b"\x8B\x1E" + (base + off("cs_pitch")).to_bytes(2, "little")
                   + b"\x81\xC3\x00\x40\x79\x02\xF7\xD8")
            lo2 = mp["cs_elev"]
            blob = m.read(lin + lo2, 96)
            k = blob.find(pat)
            if k < 0:
                sys.exit("skiesbody: cs_elev does not carry 88.7.8.1's sign "
                         "where this expects - re-read it before trusting "
                         "the red run")
            m.pause()
            m.write(lin + lo2 + k, b"\x90" * len(pat))
            m.run()
            print("  (the sign of cos(pitch) dropped again: must fail)")

        def elev_turn(pitch_deg, roll_deg=90):
            """What the ELEVATOR alone adds to the heading in one tick."""
            held = deltas(ticks("ArrowDown", pitch_deg, roll_deg, n=2), 1)[0]
            idle = deltas(ticks(None, pitch_deg, roll_deg, n=2), 1)[0]
            return held - idle

        e0 = elev_turn(0)
        print("      elevator's own turn in a 90 bank: level %d" % e0)
        wrong = []
        for p in (130, 170, -130, -170):
            e = elev_turn(p)
            print("      ...at pitch %4d: %d" % (p, e))
            if e0 == 0 or (e > 0) == (e0 > 0):
                wrong.append((p, e))
        check(abs(e0) > 4,
              "the elevator turns the aeroplane at all in a 90 degree bank "
              "(%d a tick)" % e0)
        check(not wrong,
              "...and past the vertical it turns the OTHER way (%s did not, "
              "against %d level)"
              % (", ".join("%d deg: %d" % w for w in wrong) or "none", e0))

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
