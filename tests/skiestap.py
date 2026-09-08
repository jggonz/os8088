#!/usr/bin/env python3
"""A TAP IS AN EVENT, AND THE HORIZON HAS TO BE SEEN (SPEC.md 88.7.5.2).

    python3 tests/skiestap.py [--machine os8088_5150_herc_gla]

Two remainders of §88.7.5.1, both on the Magister, both reported off the
glass: *"responding to about one in three taps"* and *"still completely
skipping the horizon"*. They are the same shape - the simulation's grid is
finer than the question - and this row is the two measurements that say so.

  1. THE TAP. The shortest press MartyPC can express (`key` down and up in
     one command) is walked across a whole frame in twelve phases, and every
     one of them must turn the aeroplane. `OSAPI_KEY_DOWN` is a LEVEL read,
     so without the int 16h latch a tap only registers when a poll happens
     to fall inside it - measured at 4 of 12, which is the owner's "one in
     three" to the phase.

  2. THE HORIZON. A held approach to level is read at each DRAWN frame -
     `cs_render`, not `cs_step`, because the tick sequence is not what the
     pilot sees - and EXACTLY ONE of them must show level. One and not zero
     is §88.7.3's capture made visible; one and not four is the promise that
     this is not Tank Attack's lock, which the owner reported as lag.

--clobber-tap is the red run (docs/WRITING-TESTS.md 1): it cuts the latch out
of `cs_stick`, which is the machine as it was, and check 1 goes red at 4
of 12.

There WAS a --clobber-hold for check 2, which turned `cs_ease`'s `stc` into
a `clc` so the caller could not tell a landing from a step toward one, and
it is gone for TWO reasons that arrived together. What it actually removed
was `cs_att_lag` zeroing `cs_rrate` on arrival - and since 88.7.5.2.1 that
only happens when the stick is CENTRED, which check 2 never is, so the patch
is a no-op here. And the detent it was aimed at is a rounding safety net now
rather than the mechanism: 88.7.3.2 arranges the landing to be a frame's
LAST tick, so removing the hold altogether still leaves exactly one drawn
frame on the horizon. No A/B can distinguish it in these scenarios, and a
knob that cannot go red is worse than no knob (docs/WRITING-TESTS.md 1).
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
TICK = 4772727 // 18                    # one system tick, in guest cycles
NPH = 12                                # phases of a frame the tap is tried at
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
    ap.add_argument("--clobber-tap", action="store_true",
                    help="cut the int 16h latch out of cs_stick: check 1 red")
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
        if a.clobber_tap:
            lo, hi = mp["cs_stick"], mp["cs_steps"]
            code = m.read(lin + lo, hi - lo)
            for nm in ("cs_taproll", "cs_tappitch"):
                # `mov bl, [cs_tap*]` - the one instruction that spends the
                # latch; `mov bl, 0` in its place is the level read alone
                pat = b"\x8a\x1e" + (base + off(nm)).to_bytes(2, "little")
                i = code.find(pat)
                if i < 0:
                    sys.exit("skiestap: cs_stick does not read [%s] the way "
                             "this patch expects" % nm)
                m.pause()
                m.write(lin + lo + i, b"\xb3\x00\x90\x90")
                m.run()
            print("  (the tap latch cut out of cs_stick: this run must fail)")
        # --- the Magister, in the air -----------------------------------------
        po = [rec(mp["cs_drplane"], 2 * i) for i in range(4)]
        ui.mo.click((po[0] + po[2]) // 2, (po[1] + po[3]) // 2)
        m.advance(frames=25)
        m.run()
        top = rec(mp["cs_drplane"], 22)
        ui.mo.click(po[0] + 20, top + 1 + 12 * 2 + 6)        # row 2
        m.advance(frames=25)
        m.run()
        plane = w("cs_plane")
        check(plane == rec(mp["cs_planes"], 2 * 2),
              "the Magister is in hand (%04x)" % plane)
        m.type_text("f")
        m.advance(frames=100)
        m.run()
        check(byte("cs_back") != 0, "the bracket took a mode")

        def pin(roll=0):
            for nm, v in (("cs_px", -2400), ("cs_py", 600), ("cs_pz", -2000)):
                poke(nm, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", (7282).to_bytes(2, "little"))
            poke("cs_pitch", b"\x00\x00")
            poke("cs_roll", (int(roll * DEG) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_rrate", b"\x00\x00")
            poke("cs_prate", b"\x00\x00")
            poke("cs_taproll", b"\x00")
            poke("cs_tappitch", b"\x00")
            poke("cs_spd", (120 * 128).to_bytes(2, "little"))
            poke("cs_state", b"\x01")

        # --- 1. the tap, at twelve phases of a frame --------------------------
        hits, rolls = 0, []
        for ph in range(NPH):
            m.bp_exec(lin + mp["cs_stick"])
            m.run()
            if m.wait_stop(30) is None:
                sys.exit("skiestap: cs_stick never ran")
            m.bp_exec()
            m.pause()
            pin()
            m.run()
            m.advance(cycles=(ph + 1) * (3 * TICK) // NPH)
            m.run()
            m.key("ArrowRight")             # down AND up, one command
            m.advance(frames=45)
            m.run()
            r = round(sg(w("cs_roll")) / DEG, 2)
            rolls.append(r)
            if abs(r) > 0.2:
                hits += 1
            m.key("ArrowRight", down=False, up=True)
            m.advance(frames=5)
            m.run()
        print("      roll after each tap: %s" % rolls)
        check(hits == NPH,
              "every one of %d taps turns the aeroplane (%d)" % (NPH, hits))

        # --- 2. the horizon, per DRAWN frame ----------------------------------
        for start in (20, 30, 45):
            m.key("ArrowLeft", down=True, up=False)
            m.advance(frames=6)
            m.run()
            m.bp_exec(lin + mp["cs_render"])
            m.run()
            if m.wait_stop(30) is None:
                sys.exit("skiestap: cs_render never ran")
            m.pause()
            pin(start)
            m.run()
            seen = []
            for _ in range(12):
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skiestap: cs_render never ran")
                seen.append(round(sg(w("cs_roll")) / DEG, 2))
            m.bp_exec()
            m.run()
            m.key("ArrowLeft", down=False, up=True)
            m.advance(frames=20)
            m.run()
            level = [i for i, v in enumerate(seen) if abs(v) < 0.6]
            print("      held from %+3d, per drawn frame: %s" % (start, seen))
            check(len(level) == 1,
                  "held from %+d: EXACTLY ONE drawn frame shows level (%s)"
                  % (start, level or "none"))

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
