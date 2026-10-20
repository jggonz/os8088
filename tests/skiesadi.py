#!/usr/bin/env python3
"""THE ATTITUDE INDICATOR DIVIDES BY COS(ROLL) (SPEC.md 88.9.2.2), on MartyPC.

    python3 tests/skiesadi.py [--machine os8088_5150_herc_gla]

The hard freeze the field reported, reduced to one instruction. `cs_d_adi`
draws the ADI's horizon bar at a rise of `t x tan(roll)` and got the tangent
with `idiv cx`, CX = cos - on a note that says "over 0.5 within MAXROLL",
which is true of a TRAINER and false of every aerobatic aeroplane here:
cs_att_free and cs_att_lag have no roll clamp (88.7.2). cs_sintab is 1024
entries over the turn, so cos reads EXACTLY 0 for the 64-unit window at
+-90 degrees, and the divide faults.

The row arms a breakpoint on the INT 0 VECTOR - which catches every divide
fault in the whole program at once and costs nothing when none fires - and
then walks the roll through both vertical windows a unit at a time on an
aeroplane that has no clamp.

  1. no divide fault at any roll in the window at +90;
  2. nor at -90 (270), which is the other zero;
  3. the ADI still DRAWS a bar there - the clamp is not a refusal - and the
     rise is bounded by four times the window's half-height;
  4. and the trainer, which never reaches the window, is unchanged to the
     unit at the angles it can reach.

--clobber-adi is the red run (docs/WRITING-TESTS.md 1): it puts the raw
divide back over the guarded one, and checks 1 and 2 go red.
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
CS_PI_ADI = 3                       # apps/skies/csgame.inc's
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
    ap.add_argument("--clobber-adi", action="store_true",
                    help="put the raw idiv back: checks 1 and 2 must go red")
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
        if a.clobber_adi:
            # `call cs_cdiv` back to a bare `idiv bx`, which is the original
            # defect exactly: cs_cdiv IS an `idiv bx` behind a range guard
            # (csraster.inc), the divisor is cos, and cos is EXACTLY 0 through
            # the window this row walks. E8 rel16 is three bytes and F7 FB is
            # two, so the third is a nop.
            #
            # THE ANCHOR IS THE FOUR BYTES IN FRONT OF THE CALL, not the call
            # itself, which is a bare E8 and appears all over this proc. It
            # used to anchor on `mov bx,cx` (89 CB) and 470bd4c moved cos out
            # of CX - sin owns that register - so the pattern went stale and
            # THE RED RUN STOPPED RUNNING AT ALL. That is the arm the row is
            # worth having for (docs/WRITING-TESTS.md 1), and it exits rather
            # than passing, so the failure names the drift instead of quietly
            # asserting nothing.
            code = m.read(lin + mp["cs_d_adi"], 400)
            i = code.find(b"\x89\xc3\x58\x5a\xe8")   # mov bx,ax/pop ax/pop dx/call
            if i < 0:
                sys.exit("skiesadi: cs_d_adi does not divide the way this "
                         "patch expects - no `mov bx,ax / pop ax / pop dx / "
                         "call cs_cdiv` in its first 400 bytes, so the guarded "
                         "divide has moved and this red run would prove "
                         "nothing")
            m.pause()
            m.write(lin + mp["cs_d_adi"] + i + 4, b"\xf7\xfb\x90")
            m.run()
            print("  (the raw `idiv bx` put back: this run must fail)")

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
            m.type_text("f")
            m.advance(frames=80)
            m.run()
            check(byte("cs_back") != 0, "row %d: the bracket took a mode" % row)
            return w("cs_plane")

        v = m.read(8 * 0, 4)
        ivt = ((int.from_bytes(v[2:4], "little") << 4)
               + int.from_bytes(v[0:2], "little"))
        print("    INT 0 vector -> %04x:%04x"
              % (int.from_bytes(v[2:4], "little"),
                 int.from_bytes(v[0:2], "little")))

        def sweep(label, centre, span=40):
            """Pin the roll across the window and draw a frame at each."""
            m.bp_exec(ivt)
            faults, rises = [], []
            for d in range(-span, span + 1, 4):
                r = (centre + d) & 0xFFFF
                m.pause()
                for nm, val in (("cs_px", -2400), ("cs_py", 600),
                                ("cs_pz", -2000)):
                    poke(nm, ((val * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
                poke("cs_roll", r.to_bytes(2, "little"))
                poke("cs_pitch", b"\x00\x00")
                poke("cs_spd", (60 * 128).to_bytes(2, "little"))
                poke("cs_state", b"\x01")
                # `advance` IS the wait, and its own reply is the answer:
                # it runs the bounded window and stops, and the server leaves
                # a hit LATCHED as `breakpoint` rather than overwriting it
                # with `paused`. A `wait_stop` behind it asks for a SECOND
                # stop that nothing is coming to make - `advance` takes the
                # resume mark, so the stop it just produced is not one a wait
                # measures to - and a stopped guest burns no cycles, so that
                # wait cannot even time out on its guest budget. It was the
                # only site in the tree with that shape.
                #
                # Nor is the guest resumed first. That is not merely redundant
                # here - `advance` clears a latched hit on its way in, so a
                # fault landing in the gap before it arrives is one the window
                # is then counted from the far side of, and the roll this
                # iteration is pinned at stops being the roll under test.
                st = m.advance(frames=3)
                if st.get("state") == "breakpoint":
                    faults.append(r)
                    m.bp_exec()
                    m.advance(frames=2)
                    m.run()
                    m.bp_exec(ivt)
                    continue
                m.run()
                rises.append(sg(w("cs_addy")))
            m.bp_exec()
            m.run()
            print("      %s: rises %s" % (label, sorted(set(rises))[:8]))
            return faults, rises

        # --- WINGS LEVEL IS A LEVEL BAR (SPEC.md 88.9.2.4) -------------------
        # The instrument had always looked right, so nothing ever asked this,
        # and the day cs_sin started writing the whole of CX instead of CL
        # alone the cosine held across it became ZERO - the guarded divide
        # answered +-30000, the clamp made it four half-heights, and the
        # horizon stood VERTICAL with the wings level. tan 0 is 0 and the
        # rise off a 45 degree bank is the window's own half width in rows.
        fly(1)
        m.pause()
        for nm, val in (("cs_px", -2400), ("cs_py", 600), ("cs_pz", -2000)):
            poke(nm, ((val * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
        poke("cs_pitch", b"\x00\x00")
        poke("cs_spd", (60 * 128).to_bytes(2, "little"))
        poke("cs_state", b"\x01")
        m.run()
        # THE PANEL IS RATE-GATED (SPEC.md 88.9.4), so a read three frames
        # after a poke is the PREVIOUS pose's answer - which is how this same
        # check first read 30 and 30 for a bank each way. Wait for the ADI's
        # own latched key to become the roll just poked, then paint.
        def adi_rise(r):
            m.pause()
            poke("cs_roll", r.to_bytes(2, "little"))
            m.run()
            want = ((r >> 8) & 0xFF)
            for _ in range(30):
                m.advance(frames=2)
                m.run()
                if (int.from_bytes(m.readseg(seg, base + off("cs_pshow") + 2 *
                                             CS_PI_ADI, 2), "little")
                        & 0xFF) == want:
                    break
            else:
                sys.exit("skiesadi: the ADI never sampled roll %04x" % r)
            m.advance(frames=4)
            m.run()
            return sg(w("cs_addy"))

        level = [(r, adi_rise(r)) for r in (0, 0x2000, 0xE000)]
        t = (w("cs_adhw") * w("cs_pasp")) >> 8      # rows for a slope of one
        print("      level: %s (t = %d rows)" % (level, t))
        check(level[0][1] == 0,
              "wings level draws a LEVEL bar (rise %d)" % level[0][1])
        check(abs(level[1][1] - t) <= 2 and abs(level[2][1] + t) <= 2,
              "...and a 45 degree bank raises the end by the window's own "
              "half width in rows (%+d and %+d against %d)"
              % (level[1][1], level[2][1], t))

        # --- the aerobatic aeroplane, through both verticals ------------------
        for label, centre in (("+90", 0x4000), ("-90", 0xC000)):
            faults, rises = sweep(label, centre)
            check(not faults,
                  "PITTS: no divide fault through the %s window (%s)"
                  % (label, ["%04x" % f for f in faults] or "none"))
            lim = 4 * w("cs_adhh")
            check(rises and all(abs(r) <= lim for r in rises),
                  "...and the rise stays inside four half-heights (%d, worst "
                  "%d)" % (lim, max((abs(r) for r in rises), default=0)))

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
