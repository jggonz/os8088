#!/usr/bin/env python3
"""THE RUNWAY KEEPS ITS LINES PAST ITS OWN MIDDLE (SPEC.md 88.6.2.1).

    python3 tests/skiesrwy.py [--machine os8088_5150_herc_gla]

The field reported it as *"when over halfway down the runway all of its
lines disappear - on the ground or flying at a low height"*, and it is one
unsigned compare. `cs_drawobj`'s size test opens `cmp cx, 2600 / ja .out`
with CX = `cs_ocz`, the object's ORIGIN in camera z; `ja` is unsigned, so an
origin BEHIND the eye reads as 65,000-odd and is dropped as far and small.
The runway's origin is its own MIDPOINT, so that happens the moment you taxi
past the halfway board - and `.out` is below `cs_edges` AND below
`cs_rwline`, which is why the outline and the centreline went together.

The row walks the aeroplane down the runway at 2 m and counts, per frame,
how many times the guest ENTERS `cs_rwline` and `cs_rwsegu` - the centreline
and its segments. It is a count and not a picture on purpose: a picture of
a runway seen end-on is a few pixels wide and a screendump comparison would
be arguing about anti-aliasing that does not exist here.

It also flies the LONG FINAL (SPEC.md 88.6.2.2), which is the same runway
one bug later: `cs_rwline` decided *past the far end* on the QUOTIENT of
`metres x 16384 / hlen`, and that quotient stops fitting in AX one whole
runway length past the far end - where an 8086 answers with INT 0. The
window is a circuit: below RW_DASHH and within RW_DASHW of the axis is the
description of an approach.

The check is what `cs_rwsegu` is HANDED and not whether the machine survived,
because surviving is the ROM's decision and not ours: under GLaBIOS vector 0
is the BIOS's dummy handler, so the guest carries on with AX undefined and
draws a centreline from it. Liveness is checked too, for the ROM that does
not absorb it.

That same approach is also where the centreline never dashed (SPEC.md
88.6.2.3). The NEAR end has been clamped since the first build - `js .zero`
puts the stripes at the threshold you are aiming at - and the far end had no
such case, so it drew one solid line from a runway length out to the flare.
The field named the state as well as the symptom: leave and come back,
because at reset you are stood at the near end. The row reads the four
stripes at the far threshold, and that the last of them ends ON it.

--clobber-rwy is the red run for the first (docs/WRITING-TESTS.md 1): it
NOPs the three bytes of the `or cx, cx / jle` that the fix added and nothing
else, so the compare is unsigned-only again - the code exactly as the field
had it.

--clobber-far NOPs the nine bytes that test metres against the runway's
length, putting the divide back in front of the guard. IT NO LONGER GOES
RED FROM THESE POSES and that is a finding rather than a broken arm:
SPEC.md 88.6.2.4's facing space makes an aeroplane past the far threshold
SHORT of the threshold behind it, so `js .zero` returns before the divide is
reached at all. The guard stays - it is four instructions and the mirrored
case can still get there - but the window it closes is no longer one an
approach flies through. --clobber-thresh pokes [cs_rwfar] onto the threshold
itself, which still collapses .far's stripes wherever it is reached.
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
CSA_X, CSA_Z, CSA_ELEV, CSA_HDG, CSA_HLEN = 2, 4, 6, 8, 10
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
    ap.add_argument("--clobber-rwy", action="store_true",
                    help="the depth test goes back to unsigned-only: red")
    ap.add_argument("--clobber-far", action="store_true",
                    help="the far-end test goes back behind the divide: red")
    ap.add_argument("--clobber-thresh", action="store_true",
                    help="the far threshold's stripes collapse to one: red")
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

        def sw(n):
            return sg(int.from_bytes(m.readseg(seg, base + off(n), 2), "little"))

        def uw(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")

        def poke(n, d):
            m.write(lin + base + off(n), d)

        def rec(at, o):
            return int.from_bytes(m.readseg(seg, at + o, 2), "little")

        m.advance(frames=30)
        m.run()
        if a.clobber_rwy:
            # the three bytes the fix put in front of the unsigned compare:
            # `or cx, cx / jle`. ANCHORED ON `cmp cx, 2600` (81 F9 28 0A),
            # which is unique, because `mov cx, [cs_ocz] / or cx, cx / jle`
            # is NOT - cs_boxlod one proc up got this same case right (its
            # own comment says so) and reads identically
            lo = mp["cs_drawobj"]
            code = m.read(lin + lo, 0x400)
            j = code.find(b"\x81\xF9\x28\x0A")
            if j < 4 or code.find(b"\x81\xF9\x28\x0A", j + 1) >= 0:
                sys.exit("skiesrwy: cs_drawobj does not open the size test "
                         "with `cmp cx, 2600` the way this patch expects")
            i = j - 4                       # or cx, cx (2) + jle rel8 (2)
            if code[i:i + 3] != b"\x09\xC9\x7E":
                sys.exit("skiesrwy: the depth test is not guarded by "
                         "`or cx, cx / jle` - nothing to take away")
            m.pause()
            m.write(lin + lo + i, b"\x90\x90\x90\x90")
            m.run()
            print("  (the depth test is unsigned-only again: must fail)")
        if a.clobber_far:
            # `mov bx, [si+CSA_HLEN] / shl bx, 1 / cmp ax, bx / jae .solid` -
            # the whole of 88.6.2.2's guard, so the divide is reached again
            # with a dividend it cannot answer
            lo = mp["cs_rwline"]
            code = m.read(lin + lo, 0x100)
            j = code.find(b"\x8B\x5C\x0A\xD1\xE3\x39\xD8\x73")
            if j < 0 or code.find(b"\x8B\x5C\x0A\xD1\xE3\x39\xD8\x73",
                                  j + 1) >= 0:
                sys.exit("skiesrwy: cs_rwline does not guard its divide the "
                         "way this patch expects")
            m.pause()
            m.write(lin + lo + j, b"\x90" * 9)     # ...and the jae's rel8
            m.run()
            print("  (the far-end test is behind the divide again: must fail)")

        m.type_text("f")
        m.advance(frames=90)
        m.run()
        check(uw("cs_back") != 0, "the bracket took a mode")
        if a.clobber_thresh:
            # 88.6.2.3's far run starts where four stripes still fit; put it
            # ON the threshold and .far draws the whole strip solid and then
            # a stripe that will not fit - which is the picture the field
            # reported, from the code that shipped
            m.pause()
            m.write(lin + base + off("cs_rwfar"), b"\xFF\x7F")
            m.run()
            print("  (the far threshold's stripes are gone again: must fail)")
        ap_ = uw("cs_airport")
        ax_, az = sg(rec(ap_, CSA_X)), sg(rec(ap_, CSA_Z))
        elev, hdg = sg(rec(ap_, CSA_ELEV)), rec(ap_, CSA_HDG)
        hlen = sg(rec(ap_, CSA_HLEN))
        rws, rwc = sw("cs_rwsin"), sw("cs_rwcos")
        print("    --- %s: half-length %d m, heading %d"
              % ("the flown runway", hlen, hdg * 360 // 65536))
        m.pause()
        poke("cs_pause", b"\x01")
        m.run()
        m.advance(frames=2)
        m.pause()

        def at(t, y, back=False):
            """Stood t metres from the runway's middle, y above it."""
            px = ax_ + (t * rws) // 32768
            pz = az + (t * rwc) // 32768
            for nm, v in (("cs_px", px), ("cs_py", elev + y), ("cs_pz", pz)):
                m.write(lin + base + off(nm),
                        ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", ((hdg + (0x8000 if back else 0)) & 0xFFFF)
                 .to_bytes(2, "little"))
            poke("cs_pitch", b"\x00\x00")
            poke("cs_roll", b"\x00\x00")
            poke("cs_state", b"\x00" if y <= 2 else b"\x01")
            m.run()
            m.advance(frames=4)
            m.pause()
            out = {}
            for sym in ("cs_rwline", "cs_rwsegu"):
                m.bp_exec(lin + mp[sym])
                m.run()
                n = 0
                for _ in range(30):
                    if m.wait_stop(8) is None:
                        break
                    n += 1
                    m.run()
                m.pause()
                m.bp_exec()
                out[sym] = n
            return out["cs_rwline"], out["cs_rwsegu"]

        # 1 - ON THE GROUND, both halves. The near half is what always worked;
        #     the far half is the report, and the pair is the experiment - a
        #     row that only walked the far half could not tell a fix from a
        #     runway that had stopped being drawn at all
        rows = []
        for frac in (-0.9, -0.5, -0.1, 0.1, 0.5, 0.9):
            t = int(frac * hlen)
            nl, ns = at(t, 2)
            rows.append((t, nl, ns))
            print("      %+5d m from the middle: cs_rwline %2d, segments %2d"
                  % (t, nl, ns))
        near = [r for r in rows if r[0] < 0]
        far = [r for r in rows if r[0] > 0]
        check(all(r[1] > 0 for r in near),
              "the near half draws its centreline (%s)"
              % [r[1] for r in near])
        check(all(r[1] > 0 for r in far),
              "...AND SO DOES THE HALF PAST THE MIDDLE (%s)"
              % [r[1] for r in far])
        check(min(r[1] for r in far) == max(r[1] for r in near),
              "...as often as the near half, not merely sometimes (%d, %d)"
              % (min(r[1] for r in far), max(r[1] for r in near)))
        # the dashes are what a take-off roll is seen against (88.6.2), so a
        # line that survived as ONE solid segment would be a half-fix
        check(all(r[2] >= 4 for r in rows),
              "...and it is DASHED at both ends, not one solid segment (%s)"
              % [r[2] for r in rows])

        # 2 - and in the air at a low height, which is the other half of the
        #     report: the origin is behind the eye there too
        nl_air, ns_air = at(int(0.7 * hlen), 40)
        check(nl_air > 0,
              "low over the far half it is drawn too (%d, %d segments)"
              % (nl_air, ns_air))

        # 3 - THE LONG FINAL, off the FAR threshold (88.6.2.2, 88.6.2.3).
        #     Past the far end, below RW_DASHH and on the axis is where
        #     cs_rwline decided `past the far end` on a QUOTIENT that no
        #     longer fits in AX - an 8086 answers that with INT 0 - and it is
        #     also the approach that had no dashed case at all: at reset the
        #     aeroplane is stood at the NEAR end, so the far one is only ever
        #     met by flying a circuit, which is the state the field named
        du, rwfar = uw("cs_rwdu"), uw("cs_rwfar")
        print("    --- a stripe is %d in Q15, the far run starts at %d"
              % (du, rwfar))
        # SINCE 88.6.2.4 THIS IS THE NEAR-END CLAMP AND NOT .far. The walk
        # runs in FACING SPACE, so an aeroplane past the far threshold and
        # pointed back at it is short of the threshold BEHIND it: `js .zero`
        # puts u at 0 and the four stripes land on the threshold it is
        # aiming at, with the solid part running away down the rest. .far is
        # now only reached from ON the strip, which is what the poses below
        # the fence exercise.
        want = [(2 * k * du, (2 * k + 1) * du) for k in range(4)] \
            + [(8 * du, 32767)]

        def segments(t, y):
            """What cs_rwsegu is HANDED, stood t from the middle looking back:
            the u it starts at and the u it ends at, in Q15 of the strip."""
            at(t, y, back=True)
            m.bp_exec(lin + mp["cs_rwsegu"])
            m.run()
            out = []
            for _ in range(len(want) + 1):
                if m.wait_stop(8) is None:
                    break
                r = m.regs()
                out.append((r["ax"], r["cx"]))
                m.run()
            m.pause()
            m.bp_exec()
            return out

        for mult, what in ((1.5, "control"), (3.5, "the window"),
                           (4.5, "...and deeper into it")):
            t = int(mult * hlen)
            f0 = uw("cs_frames")
            segs = segments(t, 100)
            m.run()
            m.advance(frames=120)
            m.pause()
            drew = (uw("cs_frames") - f0) & 0xFFFF
            print("      %5d m past the far end (%s): %s, %d frames"
                  % (t - hlen, what, segs[:5], drew))
            check(segs[:5] == want,
                  "%.1f hlen out: four stripes at the threshold it is aiming "
                  "at, then solid away down the rest" % mult)
            check(len(segs) > 4 and segs[4][1] == 32767,
                  "...the solid part running to the far end (%s)"
                  % (segs[4] if len(segs) > 4 else None,))
            check(drew > 0, "...AND THE MACHINE IS STILL RENDERING (%d frames)"
                  % drew)

        # ...and ABOVE RW_DASHH it is still one segment end to end, which is
        # the case .far must not have eaten
        hi = segments(int(3.5 * hlen), 400)
        print("      1250 m past the far end at 400 m: %s" % (hi[:5],))
        check(hi[:1] == [(0, 32767)],
              "high above it, the line is one solid segment (%s)"
              % (hi[:1],))

        m.run()
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
