#!/usr/bin/env python3
"""A WING PAYS FOR ITS LIFT (SPEC.md 88.7.12).

    python3 tests/skiesdrag.py [--clobber-ind] [--clobber-air]

The model had parasitic drag only - CSP_DRAGK, going as v^2 - and that term
falls away as the speed falls, so a slow aeroplane barely dragged. The field
flew all four consequences: a Cessna holding 60 knots on 18% of its power,
hanging at 150 for minutes, most of the runway used on the roll-out, and a
jet that could not be landed at all.

The oracle is the DRAG CURVE, read off the guest one tick at a time with the
throttle shut: the change in [cs_spd] over a tick IS the drag in units a
tick. That is integer arithmetic in the guest and it repeats exactly - where
a settled-speed sweep does not, because below the stall the aeroplane dives
and pins at the 1.25 VMAX cap (a first version of this row read 189 knots at
every throttle from 10% to 100% for exactly that reason).

Three claims:

  1. the curve has a MINIMUM and rises on both sides of it - which is what
     induced drag is, and what a v^2 law can never produce;
  2. it does not run away below the stall: the divisor is floored at the
     stall's own q, because a departed wing is not making the lift it would
     be charged for. Without the floor the Cessna reads 28 units a tick at
     10 m/s against 16 of full thrust and can never accelerate again;
  3. the brake is an AIRBRAKE in the air (SPEC.md 88.7.12.1) and roughly
     doubles the low-speed drag.

--clobber-ind zeroes [CSP_INDK] in the live record: the curve goes
monotonic, which is the code the field flew. --clobber-air NOPs the brake
test in the air path.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSP_VSTALL, CSP_VMAX, CSP_INDK = 2, 6, 44
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--clobber-ind", action="store_true",
                    help="CSP_INDK goes to 0: parasitic only, red")
    ap.add_argument("--clobber-air", action="store_true",
                    help="the airbrake test comes out of the air path, red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    import os88ui                                           # noqa: E402
    import dispapps                                         # noqa: E402
    mp = dispapps._map("skies")

    with os88ui.boot("build/os8088-360.img", apps="build/apps360.img",
                     machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")

        def off(n):
            return dispapps.bss_off("skies", n)

        def uw(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")

        def poke(n, d):
            m.write(lin + base + off(n), d)

        m.advance(frames=30)
        m.run()
        if a.clobber_air:
            # `cmp byte [cs_kbrake], 0 / je` in the AIR path - anchored on the
            # induced term's own `cmp ax, CS_INDMAX` (83 F8 40) just above it
            lo = mp["cs_step"]
            code = m.read(lin + lo, 0x600)
            j = code.find(b"\x83\xF8\x40")
            if j < 0:
                sys.exit("skiesdrag: cannot find the air path's induced term")
            k = code.find(b"\x80\x3E", j)
            if k < 0 or k - j > 0x40 or code[k + 4:k + 6] != b"\x00\x74":
                sys.exit("skiesdrag: no `cmp byte [cs_kbrake], 0 / je` there")
            m.pause()
            # THE `je` BECOMES A `jmp`, so the brake is never applied. NOPping
            # the compare instead leaves the branch reading whatever flags the
            # induced term left and applies the brake ALWAYS - which fails the
            # claim, but for the opposite reason to the one being tested.
            m.write(lin + lo + k + 5, b"\xEB")
            m.run()
            print("  (the airbrake is skipped: must fail claim 3)")
        m.type_text("f")
        m.advance(frames=120)
        m.run()
        pl = uw("cs_plane")

        def rec(o):
            return int.from_bytes(m.readseg(seg, pl + o, 2), "little")

        vs, vmax, indk = rec(CSP_VSTALL), rec(CSP_VMAX), rec(CSP_INDK)
        print("    --- VSTALL %d m/s, VMAX %d m/s, CSP_INDK %d"
              % (vs // 128, vmax // 128, indk))
        check(indk > 0 or a.clobber_ind, "the record carries an induced term")
        if a.clobber_ind:
            m.pause()
            m.write((seg << 4) + pl + CSP_INDK, b"\x00\x00")
            m.run()
            print("  (CSP_INDK is 0: parasitic only, the code the field flew)")

        m.bp_exec(lin + mp["cs_step"])

        def drag(v, brake=0):
            """One tick with the throttle shut: the fall in cs_spd IS the drag.

            NO m.pause() here - the guest is already stopped at the
            breakpoint, and pausing it again leaves the next run with
            nothing to resume (tests/skiesfleet.py's own bug)."""
            m.run()
            if m.wait_stop(20) is None:
                sys.exit("skiesdrag: cs_step stopped firing")
            poke("cs_spd", v.to_bytes(2, "little"))
            poke("cs_thr", b"\x00\x00")
            poke("cs_thrust", b"\x00\x00")
            poke("cs_thracc", b"\x00\x00")
            poke("cs_kbrake", bytes([brake]))
            for nm in ("cs_pitch", "cs_roll", "cs_rrate", "cs_prate"):
                poke(nm, b"\x00\x00")
            poke("cs_py", (3000 * 256).to_bytes(4, "little"))
            poke("cs_state", b"\x01")
            m.run()
            if m.wait_stop(20) is None:
                sys.exit("skiesdrag: cs_step stopped firing")
            return v - uw("cs_spd")

        pts = [int(f * vmax) for f in
               (0.32, 0.40, 0.50, 0.60, 0.70, 0.80, 0.90, 1.00)]
        curve = [(v, drag(v)) for v in pts]
        print("      v (m/s)  %s" % " ".join("%5.1f" % (v / 128.0)
                                             for v, _ in curve))
        print("      drag     %s" % " ".join("%5d" % d for _, d in curve))
        lo_i = min(range(len(curve)), key=lambda i: curve[i][1])
        check(0 < lo_i < len(curve) - 1,
              "the curve has a minimum INSIDE the range (at %.1f m/s)"
              % (curve[lo_i][0] / 128.0))
        check(curve[0][1] > curve[lo_i][1],
              "...and it rises on the SLOW side (%d against %d)"
              % (curve[0][1], curve[lo_i][1]))
        check(curve[-1][1] > curve[lo_i][1],
              "...and on the fast side (%d against %d)"
              % (curve[-1][1], curve[lo_i][1]))

        # 2 - the floor: well below the stall it must not run away
        slow = drag(vs // 3)
        atstall = drag(vs)
        print("      at %d m/s (a third of the stall): %d a tick, against %d "
              "at the stall" % (vs // 3 // 128, slow, atstall))
        check(slow <= atstall + 1,
              "below the stall the induced term is FLOORED, not runaway "
              "(%d against %d)" % (slow, atstall))

        # 3 - the airbrake
        v = int(0.45 * vmax)
        plain, braked = drag(v), drag(v, brake=1)
        print("      at %.1f m/s: %d a tick, %d with the brake held"
              % (v / 128.0, plain, braked))
        check(braked >= plain * 2 - 1,
              "the brake is an AIRBRAKE up here (%d against %d)"
              % (braked, plain))
        m.bp_exec()
        m.run()

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
