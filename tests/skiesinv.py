#!/usr/bin/env python3
"""INVERTED IS THE SAME AEROPLANE (SPEC.md 88.7.8.3), on MartyPC.

    python3 tests/skiesinv.py [--machine os8088_5150_herc_gla]

Reported off the machine, after a vertical 180 and a roll to level:

> The direction of travel is wrong. I seem to be going... partially sideways
> I think? Not where the nose is pointed.

`cs_step` turns the aeroplane by `CSP_TURNK x sin(roll)` a tick, and that is
the third place with 88.7.8.1's shape in it - the ELEVATOR's contribution to
the heading was fixed there, the SCENE's reading of the heading in 88.7.8.2,
and the BANK's own contribution, which is most of what a heading ever does,
was never looked at. Lift is along the body's up axis and the ground track is
sign(cos pitch) x the heading, so the lift's component to the track's RIGHT is
exactly `sign(cos pitch) sin(roll)` - ch^2 + sh^2 cancels everything else, so
no magnitude is dropped and only the sign was missing. Past the vertical a
right bank turned LEFT.

THE A/B IS THE SAME PAIR tests/skiesfacing.py uses, for the same reason:

    (hdg = H,        pitch = 0,    roll = 0)
    (hdg = H + 180,  pitch = 180,  roll = 180)

are one attitude. This row asserts that twice over before it asserts anything
else - `cs_m` identical (the same camera) and `right`.y identical under the
same key (the SAME PHYSICAL BANK, right wing down in both) - because without
the second one a difference in the turn would just be two aeroplanes banking
different ways. Then one tick of the model must produce the same WORLD
VELOCITY in both, stick centred and with ROLL RIGHT held.

WHAT THIS ROW SEES THAT skiesfacing CANNOT is MOTION: that row compares two
still frames and a still frame cannot show which way an aeroplane is turning.
The panel half of the report - the attitude line gone and the compass reading
the reciprocal, SPEC.md 88.9.2.6 - is skiesfacing's, whose comparison is the
whole screen since those two were found under the exclusion it used to carry.

THE SPEED IS PINNED EVERY TICK. Full throttle accelerates, the two arms are
not the same number of ticks past their pin, and the velocities then drift
apart in a way that has nothing to do with direction.

AND THE TOLERANCE IS ONE UNIT IN THE LAST PLACE, WHICH IS MUL14's FLOOR AND
NOT SLOP. `MUL14` keeps the high word of a doubled product, so it is
floor(a.b / 32768), and floor is not symmetric about zero: cos(0) is +32767
and cos(180) is -32767, so the horizontal speed comes out 8,959 in one arm and
-8,960 in the other from the same 70 m/s. It is 1/256 of a metre a tick, 0.01%
- and the defect this row is for moved the velocity by TENS of units and
turned the heading the other way, so the tolerance cannot hide it. The
DIRECTIONS are checked exactly.

--clobber-bank is the red run (docs/WRITING-TESTS.md 1): it NOPs the four
bytes of `add bx, 16384 / jns / neg ax` that carry the sign, so the turn is
`sin(roll)` again - the model exactly as the field had it - and the two arms
turn opposite ways.
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
CSP_TURNK = 24
SPD = 70 * 128
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def sg(v):
    return v - 0x10000 if v >= 0x8000 else v


def sg32(v):
    return v - 0x100000000 if v >= 0x80000000 else v


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-bank", action="store_true",
                    help="the turn loses cos(pitch)'s sign again: red")
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

        def uw(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")

        def ud(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 4), "little")

        def ub(n):
            return m.readseg(seg, base + off(n), 1)[0]

        def poke(n, d):
            m.write(lin + base + off(n), d)

        def rec(at, o):
            return int.from_bytes(m.readseg(seg, at + o, 2), "little")

        def mword(i):
            return sg(int.from_bytes(
                m.readseg(seg, base + off("cs_m") + 2 * i, 2), "little"))

        m.advance(frames=30)
        m.run()
        if a.clobber_bank:
            # `mov bx,[cs_pitch] / add bx,16384 / jns .bank / neg ax` - and
            # THOSE EIGHT BYTES ARE NOT UNIQUE: 88.7.8.1's sign in cs_elev is
            # the same instructions on the same registers, 1,387 bytes along,
            # and the first spelling of this patch found both and refused.
            # So the anchor is what can only be the TURN - the plane record's
            # CSP_TURNK loaded into BX and MUL14'd - and the sign is the eight
            # bytes fifteen past it
            lo = mp["cs_step"]
            code = m.read(lin + lo, 0x400)
            pat = b"\x8B\x5D\x18\xF7\xEB\xD1\xE0\xD1\xD2\x89\xD0\x8B\x1E"
            j = code.find(pat)
            if j < 0 or code.find(pat, j + 1) >= 0:
                sys.exit("skiesinv: cs_step does not turn on CSP_TURNK the "
                         "way this patch expects")
            at = j + len(pat) + 2
            if code[at:at + 8] != b"\x81\xC3\x00\x40\x79\x02\xF7\xD8":
                sys.exit("skiesinv: the turn is not signed by 88.7.8.3's four "
                         "instructions - nothing to take away")
            m.pause()
            m.write(lin + lo + at, b"\x90" * 8)
            m.run()
            print("  (the turn is sin(roll) again: this run must fail)")

        # the PITTS - no clamp and no return on either axis (88.7.2), which is
        # the only aeroplane that can BE at pitch 180
        if ub("cs_back") != 0:
            m.type_text("f")
            m.advance(frames=60)
            m.run()
        po = [rec(mp["cs_drplane"], 2 * i) for i in range(4)]
        ui.mo.click((po[0] + po[2]) // 2, (po[1] + po[3]) // 2)
        m.advance(frames=20)
        m.run()
        top = rec(mp["cs_drplane"], 22)
        ui.mo.click(po[0] + 20, top + 1 + 12 * 1 + 6)
        m.advance(frames=20)
        m.run()
        want = rec(mp["cs_planes"], 2)
        check(uw("cs_plane") == want,
              "row 1 picks the Pitts (%04x)" % uw("cs_plane"))
        m.type_text("f")
        m.advance(frames=80)
        m.run()
        check(ub("cs_back") != 0, "the bracket took a mode")
        tk = rec(uw("cs_plane"), CSP_TURNK)
        print("    --- PITTS: CSP_TURNK %d" % tk)

        def pin(dh, pitch, roll):
            for nm, v in (("cs_px", -2400), ("cs_py", 900), ("cs_pz", -2000)):
                m.write(lin + base + off(nm),
                        ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", ((7282 + dh) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_pitch", (pitch & 0xFFFF).to_bytes(2, "little"))
            poke("cs_roll", (roll & 0xFFFF).to_bytes(2, "little"))
            poke("cs_rrate", b"\x00\x00")
            poke("cs_prate", b"\x00\x00")
            poke("cs_taproll", b"\x00")
            poke("cs_tappitch", b"\x00")
            poke("cs_spd", SPD.to_bytes(2, "little"))
            poke("cs_thr", (100).to_bytes(2, "little"))
            poke("cs_state", b"\x01")

        def arm(key, dh, pitch, roll, n=6):
            """Hold `key`, pin the attitude, and step whole TICKS."""
            if key:
                m.key(key, down=True, up=False)
                nm = ("cs_kroll" if key in ("ArrowLeft", "ArrowRight")
                      else "cs_kpitch")
                for _ in range(20):
                    m.advance(frames=2)
                    m.run()
                    if ub(nm) != 0:
                        break
                else:
                    sys.exit("skiesinv: the guest never saw %s" % key)
            m.bp_exec(lin + mp["cs_step"])
            m.run()
            if m.wait_stop(30) is None:
                sys.exit("skiesinv: cs_step never ran")
            pin(dh, pitch, roll)
            out, prev = [], None
            for _ in range(n):
                # the SPEED is the row's, every tick: full throttle
                # accelerates and the arms are not the same number of ticks
                # past their pin, which reads as a difference of 1/256 m
                poke("cs_spd", SPD.to_bytes(2, "little"))
                p = (sg32(ud("cs_px")), sg32(ud("cs_py")), sg32(ud("cs_pz")))
                v = None if prev is None else tuple(x - y
                                                    for x, y in zip(p, prev))
                prev = p
                out.append((v, sg(uw("cs_hdg")), mword(1),
                            [mword(i) for i in range(9)], mword(4)))
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skiesinv: cs_step never ran")
            m.bp_exec()
            m.run()
            if key:
                m.key(key, down=False, up=True)
            return out

        def ab(kn, key, whole_matrix):
            print("    --- %s" % kn)
            A = arm(key, 0, 0, 0)
            B = arm(key, 0x8000, 0x8000, 0x8000)
            # the first two samples are dropped: cs_m is the LAST frame's and
            # a frame is several ticks, so the matrix has not caught the pin
            # up yet - the velocities have, being read from the position
            va = [r[0] for r in A[1:]]
            vb = [r[0] for r in B[1:]]
            ma, mb = A[-1][3], B[-1][3]
            rya, ryb = A[-1][2], B[-1][2]
            dha = [A[i + 1][1] - A[i][1] for i in range(len(A) - 1)]
            dhb = [B[i + 1][1] - B[i][1] for i in range(len(B) - 1)]
            print("      upright  v %s" % va)
            print("               d(hdg) %s   right.y %d" % (dha, rya))
            print("      inverted v %s" % vb)
            print("               d(hdg) %s   right.y %d" % (dhb, ryb))
            if whole_matrix:
                check(ma == mb,
                      "%s: the two attitudes ARE one camera (cs_m %s)"
                      % (kn, "matches" if ma == mb else "%s vs %s" % (ma, mb)))
            else:
                # ONCE THEY TURN THE HEADINGS DIVERGE - that is the thing
                # under test - so the premise here is the heading-FREE part
                # of the attitude: right.y = -sin(roll) cos(pitch) and
                # up.y = cos(roll) cos(pitch), which is "the same wing down
                # by the same amount" and nothing about which way it points
                check(A[-1][4] == B[-1][4],
                      "%s: up.y is the same in both (%d) - the arms are still "
                      "one attitude apart from the heading" % (kn, A[-1][4]))
            check(rya == ryb,
                  "%s: ...and the SAME PHYSICAL BANK - right.y %d in both"
                  % (kn, rya))
            worst = max(abs(x - y) for pa, pb in zip(va, vb)
                        for x, y in zip(pa, pb))
            check(worst <= 1,
                  "%s: so one tick moves the aeroplane the same way in the "
                  "WORLD - worst component %d of ~700, and MUL14's floor is "
                  "worth 1 of it" % (kn, worst))
            return dha, dhb

        # 1 - STRAIGHT. The control: with the stick centred the two arms must
        #     already agree, so a failure below is the BANK and not cs_move
        ab("stick centred", None, True)
        # 2 - and the report: the same key, the same wing down, and the turn
        dha, dhb = ab("ROLL RIGHT held", "ArrowRight", False)
        check(all(x > 0 for x in dha[1:]),
              "upright, a right bank turns the heading UP (%s)" % dha)
        check(all(x > 0 for x in dhb[1:]),
              "...and inverted-and-rolled-level it turns the SAME WAY, which "
              "is the report (%s)" % dhb)
        check(all(abs(x - y) <= 1 for x, y in zip(dha, dhb)),
              "...at the same RATE, to MUL14's one unit (%s vs %s)"
              % (dha, dhb))

    print()
    if bad:
        print("skiesinv: %d FAILED" % len(bad))
        for b in bad:
            print("  - %s" % b)
        return 1
    print("skiesinv: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
