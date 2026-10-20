#!/usr/bin/env python3
"""THE SCENE READS THE FACING, NOT THE HEADING (SPEC.md 88.7.8.2), on MartyPC.

    python3 tests/skiesfacing.py [--machine os8088_5150_herc_gla]

Reported off the glass: *"if you do a vertical 180 in the Pitts Special it
stops drawing buildings in the distance, lines on the runway, and the
movement of the runway gets weird - a reverse vertical 180 clears it"*.

The camera's forward vector is `(sh cp, sp, ch cp)`, so its horizontal part
is the heading SCALED BY cos(pitch) - and past the vertical that is negative
and the aeroplane is pointed the other way along its own heading. The matrix
has always had it right, because cp is a factor of both terms it builds.
Three things that work in the heading's frame directly - 88.5.1's cull, the
occluder's across, and 88.6.2.4's which-threshold-is-ahead - took (sh, ch)
as the facing and had no cos(pitch) in them at all. A loop leaves [cs_pitch]
past a quarter turn and nothing puts it back (88.7.8.1), which is why the
state sticks until the loop is flown backwards.

THE A/B IS EXACT AND NEEDS NO TOLERANCE, which is what makes this testable:

    (hdg = H,        pitch = p,    roll = r)
    (hdg = H + 180,  pitch = 180-p, roll = r + 180)

are ONE ATTITUDE, and at p = 0 THE SAME CAMERA. Every row of cs_matrix comes out identical term for
term - the quarter table (88.5.9) reflects exactly, so sh, ch, cp and cr all
negate exactly, and MUL14(-a, -b) is MUL14(a, b) to the bit. So the two
attitudes must file the same objects, sort them the same way, pick the same
runway threshold and put the same pixels in the 3D window. Check 0 asserts
the premise itself off the guest's own cs_m, so a row whose A/B had stopped
being an A/B would say so rather than compare two different cameras.

THE COMPARISON IS THE WHOLE SCREEN, PANEL INCLUDED, and it did not used to
be. This row first carved the panel out on the reasoning that it reads the
Euler triple and 180/180 is a different triple from 0/0 - which is true and
is not a licence, because the panel draws an ATTITUDE and those two triples
are one attitude. The 80 pixels the carve-out was hiding were SPEC.md
88.9.2.6: the attitude line off the glass entirely and the compass reading
the reciprocal. An exclusion in a gate is where the next defect lives.

EVERY POSE CLEARS CSO_SEEN FIRST (cs_skipclr by hand). 88.5.1 files an
object that was drawn LAST frame without consulting the cone at all, so the
defect is invisible to anything already on the glass: what is up stays up
and only strangers are refused. That is the *"in the distance"* in the
report, and a row that did not make strangers of them would measure nothing.

--clobber-facing is the red run for the scene (docs/WRITING-TESTS.md 1): it
NOPs the four bytes of `neg ax / neg bx` in cs_matrix, so [cs_fsinh]/
[cs_fcosh] are just [cs_sinh]/[cs_cosh] again - the code exactly as the field
had it. --clobber-panel is the red run for the PANEL half: one byte each
turns SPEC.md 88.9.2.6's two folds off (`jns` over the fold becomes `jmp
short`), and the attitude line leaves the glass and the compass reads the
reciprocal again.
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
CSA_OBJS, CSA_NOBJ = 18, 20
CSO_FLAGS, CSO_SKIP, CSO_SIZE = 16, 18, 20
CSO_SEEN_HI = 0x80                      # CSO_SEEN >> 8
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
    ap.add_argument("--clobber-facing", action="store_true",
                    help="the facing pair becomes the heading again: red")
    ap.add_argument("--clobber-panel", action="store_true",
                    help="the ADI and the compass read the raw triple: red")
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

        def ub(n):
            return m.readseg(seg, base + off(n), 1)[0]

        def poke(n, d):
            m.write(lin + base + off(n), d)

        def rec(at, o):
            return int.from_bytes(m.readseg(seg, at + o, 2), "little")

        m.advance(frames=30)
        m.run()
        if a.clobber_facing:
            # `cmp word [cs_cosp], 0 / jns .face / neg ax / neg bx`, and it is
            # the two NEGs that are 88.7.8.2. Anchored on the jns so that a
            # bare `F7 D8 F7 DB` elsewhere in the file cannot be hit
            lo = mp["cs_matrix"]
            code = m.read(lin + lo, 0x80)
            j = code.find(b"\x79\x04\xF7\xD8\xF7\xDB")
            if j < 0 or code.find(b"\x79\x04\xF7\xD8\xF7\xDB", j + 1) >= 0:
                sys.exit("skiesfacing: cs_matrix does not derive the facing "
                         "pair the way this patch expects")
            m.pause()
            m.write(lin + lo + j + 2, b"\x90" * 4)
            m.run()
            print("  (the facing pair is the heading again: this must fail)")

        if a.clobber_panel:
            # 88.9.2.6's two folds, each turned off by ONE BYTE: `jns` over
            # the fold becomes `jmp short`, so the raw Euler triple reaches
            # the attitude indicator and the compass again
            for nm, pat, where in (
                    ("the ADI", b"\x81\xC1\x00\x40\x79\x08\xF7\xD8"
                                b"\x05\x00\x80", "cs_d_adi"),
                    ("the compass", b"\x81\xC3\x00\x40\x79\x03"
                                    b"\x80\xF4\x80", "cs_k_hdg")):
                lo = mp[where]
                code = m.read(lin + lo, 0x120)
                j = code.find(pat)
                if j < 0 or code.find(pat, j + 1) >= 0:
                    sys.exit("skiesfacing: %s does not fold the attitude the "
                             "way this patch expects" % where)
                m.pause()
                m.write(lin + lo + j + 4, b"\xEB")     # jns -> jmp short
                m.run()
                print("  (%s reads the raw triple again: must fail)" % nm)

        m.type_text("f")
        m.advance(frames=90)
        m.run()
        check(ub("cs_back") != 0, "the bracket took a mode")

        ap_ = uw("cs_airport")
        ax_, az = sg(rec(ap_, CSA_X)), sg(rec(ap_, CSA_Z))
        elev, hdg = sg(rec(ap_, CSA_ELEV)), rec(ap_, CSA_HDG)
        hlen = sg(rec(ap_, CSA_HLEN))
        objs, nobj = rec(ap_, CSA_OBJS), rec(ap_, CSA_NOBJ)

        m.pause()
        poke("cs_pause", b"\x01")            # the attitude is the row's, not
        m.run()                              # the flight model's
        m.advance(frames=2)
        m.pause()
        # LET THE TAKE-OFF PROMPT GO FIRST (88.7.9). Its strip is inside the
        # 3D window, and it expires eight frames into the FIRST pose and is
        # already gone by the second - ~5,000 window pixels of difference that
        # is history rather than geometry, and reads exactly like the thing
        # under test. It has to EXPIRE and not be poked: [cs_toastt] reaching
        # zero is what erases the strip, so a zero written into it leaves the
        # message on the glass for good
        for _ in range(120):
            if ub("cs_toastt") == 0:
                break
            m.run()
            m.advance(frames=4)
            m.pause()
        else:
            sys.exit("skiesfacing: the take-off prompt never expired")
        m.run()
        m.advance(frames=4)
        m.pause()
        rwsin, rwcos = sg(uw("cs_rwsin")), sg(uw("cs_rwcos"))
        vx0, vy0 = uw("cs_wx0"), uw("cs_vy")
        vw, vh = uw("cs_ww"), uw("cs_wh")
        print("    --- %d objects, half-length %d m; the 3D window is "
              "%dx%d at (%d,%d), and the PANEL IS IN THE COMPARISON"
              % (nobj, hlen, vw, vh, vx0, vy0))

        def strangers():
            """cs_skipclr by hand: nothing was drawn last frame (88.5.1)."""
            for i in range(nobj):
                at = objs + i * CSO_SIZE
                f = m.readseg(seg, at + CSO_FLAGS + 1, 1)[0]
                m.write(lin + at + CSO_FLAGS + 1, bytes([f & ~CSO_SEEN_HI]))
                m.write(lin + at + CSO_SKIP, b"\x00\x00")
            at = mp["cs_rwobj"]
            f = m.readseg(seg, at + CSO_FLAGS + 1, 1)[0]
            m.write(lin + at + CSO_FLAGS + 1, bytes([f & ~CSO_SEEN_HI]))
            m.write(lin + at + CSO_SKIP, b"\x00\x00")

        def window(px, w):
            """The 3D view alone, rows of rgb24 - reported beside the whole
            screen so a failure says WHERE, the scene or the panel."""
            return [px[((vy0 + y) * w + vx0) * 3:
                       ((vy0 + y) * w + vx0 + vw) * 3] for y in range(vh)]

        def pose(t, y, dh, pitch, roll):
            px_ = ax_ + (t * rwsin) // 32768
            pz = az + (t * rwcos) // 32768
            for nm, v in (("cs_px", px_), ("cs_py", elev + y), ("cs_pz", pz)):
                m.write(lin + base + off(nm),
                        ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", ((hdg + dh) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_pitch", (pitch & 0xFFFF).to_bytes(2, "little"))
            poke("cs_roll", (roll & 0xFFFF).to_bytes(2, "little"))
            poke("cs_state", b"\x01")
            # SETTLE ON WHOLE RENDERED FRAMES AND NOT ON DISPLAY FRAMES.
            # A Clear Skies frame over the city is ~140 ms - many display
            # frames - so `advance(frames=n)` samples the middle of one, and
            # a settle over display frames stops on a PLATEAU in a scene that
            # is still being drawn. It passed alone and failed under
            # `os88soak.py` at width 4 for exactly that reason, reading 1,909
            # lit pixels where the finished picture has 3,206
            # (docs/plans/SOAK-PARALLEL.md 1: contention does not make a row
            # slow, it makes it less thorough). cs_render's own top is a
            # frame BOUNDARY - the glass holds the last complete picture and
            # [cs_nvisn] still holds that frame's count, so the number and
            # the pixels describe the SAME frame - and it is exact in guest
            # terms, so no amount of host load can move it
            m.bp_exec(lin + mp["cs_render"])

            def boundary():
                m.run()
                if m.wait_stop(60) is None:
                    sys.exit("skiesfacing: cs_render never ran - is the "
                             "scene being drawn at all?")

            boundary()
            # STRANGERS ONCE, HERE, and not every frame: 88.5.1's cone is a
            # BOUND and cs_matrix widens it past 15 degrees of pitch or roll,
            # so the upright pose is culled against a narrower cone than the
            # inverted one and the two can legitimately file a different
            # COUNT while drawing the same picture. Clearing once lets both
            # converge on what the true frustum accepts, which is the same
            # set - and the defect still shows, because a cone that refuses
            # every stranger never lets one become SEEN
            strangers()
            shot, same = None, 0
            for i in range(30):
                boundary()
                w, h, q = m.fbuf()
                same = same + 1 if q == shot else 0
                shot = q
                if i >= 3 and same >= 2:
                    break
            else:
                sys.exit("skiesfacing: the picture never settled")
            nvis = uw("cs_nvisn")
            mat = [sg(int.from_bytes(m.readseg(seg, base + off("cs_m") + 2 * i,
                                               2), "little"))
                   for i in range(9)]
            rwrev = ub("cs_rwrev")
            m.bp_exec()
            m.run()
            print("      %-26s %2d whole frames to settle" % ("", i + 1))
            return nvis, rwrev, window(shot, w), mat, shot

        def ab(what, t, y, roll=0):
            # (H, p, r) and (H+180, 180-p, r+180) are ONE attitude, so the
            # pair generalises: `roll` banks BOTH arms by the same real angle
            print("    --- %s" % what)
            A = pose(t, y, 0, 0, roll)
            B = pose(t, y, 0x8000, 0x8000, (roll + 0x8000) & 0xFFFF)
            d = sum(1 for ra, rb in zip(A[2], B[2])
                    for i in range(0, len(ra), 3)
                    if ra[i:i + 3] != rb[i:i + 3])
            full = sum(1 for i in range(0, len(A[4]), 3)
                       if A[4][i:i + 3] != B[4][i:i + 3])
            lit = sum(1 for r in A[2] for i in range(0, len(r), 3)
                      if r[i:i + 3] != b"\0\0\0")
            print("      upright  nvisn %2d rwrev %d | inverted nvisn %2d "
                  "rwrev %d | %d of %d window and %d of %d SCREEN pixels "
                  "differ (%d lit)"
                  % (A[0], A[1], B[0], B[1], d, vw * vh, full, len(A[4]) // 3,
                     lit))
            check(A[3] == B[3],
                  "%s: the two attitudes ARE one camera (cs_m %s)"
                  % (what, "matches" if A[3] == B[3] else
                     "%s vs %s" % (A[3], B[3])))
            check(A[0] > 1 and lit > 500,
                  "%s: there is a scene to be wrong about (%d objects, %d lit)"
                  % (what, A[0], lit))
            check(A[0] == B[0],
                  "%s: the cull files the same objects inverted (%d vs %d)"
                  % (what, A[0], B[0]))
            check(A[1] == B[1],
                  "%s: cs_rwline picks the same threshold (%d vs %d)"
                  % (what, A[1], B[1]))
            check(d == 0,
                  "%s: and the 3D window is the SAME PICTURE (%d differ)"
                  % (what, d))
            check(full == 0,
                  "%s: ...and so is the WHOLE SCREEN, panel and all (%d "
                  "differ, of which %d are outside the window)"
                  % (what, full, full - d))

        # 1 - THE APPROACH. The distance is the point: at 1.5 km every object
        #     ahead is a stranger, and a cone that has them all behind it
        #     files nothing. This is *"buildings in the distance"*
        ab("1.5 km out on the approach, 120 m up", -(hlen + 1500), 120)
        # 2 - ON THE STRIP, where cs_rwline is reached and its threshold test
        #     decides which end the stripes start from: *"lines on the runway,
        #     and the movement of the runway gets weird"*
        ab("on the runway, 300 m short of the far threshold", hlen - 300, 3)
        # 3 - and over the middle at height, which is where the occluder pass
        #     has candidates and an `along` of the wrong sign takes every one
        #     of them out of the ranking
        ab("400 m up over the middle of the strip", 0, 400)
        # 4 - AND IN A 60 DEGREE BANK, which is the ATTITUDE INDICATOR's real
        #     test: level, the line is centred and level in both arms whether
        #     or not the fold works on the roll, so only a banked pose says
        #     the roll half of 88.9.2.6 is right. 60 and not 90: at exactly
        #     90 cos(roll) is EXACTLY zero over a whole 64-unit window
        #     (88.9.2.2), the guarded divide answers +-30000 by the sign of
        #     the numerator, and a line-only horizon cannot say which way
        #     "vertical" leans - the two arms then differ for a reason that
        #     is the representation's and not the fold's
        ab("60 degrees of bank, 400 m up over the strip", 0, 400, 10923)

    print()
    if bad:
        print("skiesfacing: %d FAILED" % len(bad))
        for b in bad:
            print("  - %s" % b)
        return 1
    print("skiesfacing: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
