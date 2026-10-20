#!/usr/bin/env python3
"""THE BOX IMPOSTOR BANKS WITH THE WORLD (SPEC.md 88.5.4.6).

    python3 tests/skiesbank.py [--machine os8088_5150_herc_gla]

A solid too small to tell apart is drawn as its box (88.5.4), and that box was
an AXIS-ALIGNED SCREEN RECTANGLE: the top was projected and only its ROW kept,
the half-width was laid along the screen's own horizontal, and the rectangle
they spanned was filled through cs_rect. Level that is right. Banked it is
wrong twice over, and the field reported both:

  * it does not TILT. The polygon path's faces track the ground because they
    are projected vertices; the impostor's four corners were the screen's
    axes, so a building stood upright while the horizon rotated under it.
  * it COMPRESSES. The rectangle's height was the VERTICAL PART of the
    projected up axis, which is |up2| cos r - so at 50 degrees of bank a
    building lost 36% of its height, and got it back as the wings came level.

88.5.4.6 keeps the same three projected points and turns them into a banked
rectangle: the top's x is kept as well as its row, the height is the LENGTH of
the up axis rather than its vertical part, and the half-width is turned onto
the screen-space horizontal, which is (cos r, -sin r) exactly.

The gate pins ONE pose over Paris and rolls the aeroplane under it, so the
building, the eye and the depth are identical and the only thing that changes
is the bank - and it is KEYED ON THE OBJECT, because a roll moves the frustum
and the frame's impostors are not the same set at every bank: comparing the
first of each compares two different buildings, which reads exactly like the
axis changing length. What it reads is the fill itself - a breakpoint on cs_rect and on
cs_poly, kept only when the return address lands inside cs_boxlod, so a FACE's
polygon is never mistaken for the impostor - and then the four corners the
fill was given:

  1. LEVEL the impostor is the rectangle it always was: the width vector is
     horizontal to the pixel.
  2. BANKED it is a QUAD, and its width vector is perpendicular to its up
     axis - the corners really are a rotated rectangle and not a shear.
  3. It is drawn along the WHOLE projected up axis and not the vertical part
     of it - which is the compression exactly, and at 50 degrees is a third
     of the building's height.
  4. And its half-width does not change with the bank, being projected along
     the camera's own x, which no roll can compress.

Its axis LENGTH is deliberately NOT asserted to be constant: the top of a box
is further from the eye than its base, so an off-centre building's verticals
converge, and rolling moves the building across the frame - so the honest
invariant is that the axis is used whole, not that it is the same length. The
readings on the pinned pose are 9.1 px level, 10.6 at 30 and 12.5 at 50, and
the polygon path leans by the same rule.

LEVEL IS THE RECTANGLE, and check 1 says so: since 88.5.4.7 the test is on Ry
alone, so wings level draws exactly what shipped - byte for byte over the whole
view on all four pinned poses - and only a banked frame is a quad at all.

--clobber-bank is the red run (docs/WRITING-TESTS.md 1): it NOPs the jump that
sends a banked impostor to the quad, so every impostor takes the upright
rectangle again - which is exactly what shipped - and checks 2, 3 and 4 must
go red.
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
MAXROW = 240
D = 2400                        # ...back down the city view's own heading
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-bank", action="store_true",
                    help="the upright rectangle back: checks 2-4 go red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    mp = dispapps._map("skies")

    def off(n):
        return dispapps.bss_off("skies", n)

    hx, hz = math.sin(math.radians(30)), math.cos(math.radians(30))
    eye = (int(150 - D * hx), 300 + D // 8, int(-900 - D * hz), 30, -5)

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")

        def poke(n, d):
            m.write(lin + base + off(n), d)

        def word(n):
            v = int.from_bytes(m.read(lin + base + off(n), 2), "little")
            return v - 65536 if v >= 32768 else v

        m.advance(frames=40)
        m.run()
        m.type_text("f")
        m.advance(frames=120)
        m.run()

        if a.clobber_bank:
            # The one test that sends a banked impostor to the quad:
            # `or dx,dx / jnz .quad` on Ry (88.5.4.7). Two bytes of NOP puts
            # every impostor back on the upright rectangle.
            lo, hi = mp["cs_boxlod"], mp["cs_stackverts"]
            code = m.read(lin + lo, hi - lo)
            i = code.find(b"\x09\xD2\x75")          # or dx,dx / jnz
            if i < 0 or code.count(b"\x09\xD2\x75") != 1:
                sys.exit("skiesbank: cs_boxlod does not choose the quad the "
                         "way this patch expects - re-read it before "
                         "trusting the red run")
            m.pause()
            m.write(lin + lo + i + 2, b"\x90\x90")
            m.run()
            print("  (the upright rectangle back: checks 2-4 must fail)")

        lo, hi = mp["cs_boxlod"], mp["cs_stackverts"]

        def impostor(roll):
            """Pin the pose at this bank and read the fill the impostor got."""
            x, y, z, hdg, pitch = eye
            m.pause()
            for n, v in (("cs_px", x), ("cs_py", y), ("cs_pz", z)):
                poke(n, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            for n, v in (("cs_hdg", hdg), ("cs_pitch", pitch),
                         ("cs_roll", roll)):
                poke(n, ((v * 65536 // 360) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_state", b"\x01")
            poke("cs_pause", b"\x01")
            # a poke is a TELEPORT: every object looked at again (88.5.2)
            port = word("cs_airport") & 0xFFFF
            objs = int.from_bytes(m.read(lin + port + 18, 2), "little")
            nobj = int.from_bytes(m.read(lin + port + 20, 2), "little")
            for o in range(objs, objs + nobj * 20, 20):
                m.write(lin + o + 18, b"\x00\x00")
            m.run()
            m.advance(frames=6)
            m.run()
            m.bp_exec(lin + mp["cs_render"], lin + mp["cs_rect"],
                      lin + mp["cs_poly"])
            m.run()
            if m.wait_stop(60) is None:
                sys.exit("skiesbank: nothing stopped")
            while m.regs()["ip"] != mp["cs_render"]:
                m.run()
                if m.wait_stop(60) is None:
                    sys.exit("skiesbank: cs_render never ran")
            got = {}
            for _ in range(400):
                m.run()
                if m.wait_stop(60) is None:
                    break
                r = m.regs()
                if r["ip"] == mp["cs_render"]:
                    break
                ret = int.from_bytes(m.read((r["ss"] << 4) + r["sp"], 2),
                                     "little")
                if not lo <= ret < hi:
                    continue                    # a FACE's polygon, not ours
                kind = "quad" if r["ip"] == mp["cs_poly"] else "rect"
                # KEYED ON THE OBJECT (SPEC.md 88.5.4.6): a roll moves the
                # frustum, so the frame's impostors are not the same set at
                # every bank and comparing the FIRST of each would compare two
                # different buildings - which reads exactly like the axis
                # changing length.
                # ...and read what was actually HANDED TO THE FILL, not the
                # working values behind it: the upright arm never writes the
                # four corners, so a check on cs_brx/cs_bry alone would still
                # pass with the impostor drawn as a rectangle.
                if kind == "quad":
                    v = m.read(lin + base + off("cs_pv"), 16)
                    pts = [(int.from_bytes(v[i:i + 2], "little", signed=True),
                            int.from_bytes(v[i + 2:i + 4], "little",
                                           signed=True))
                           for i in range(0, 16, 4)]
                else:                           # cs_rect: AX,BX,CX,DX
                    def sw(n):
                        return r[n] - 65536 if r[n] >= 32768 else r[n]
                    x0, y0, x1, y1 = sw("ax"), sw("bx"), sw("cx"), sw("dx")
                    pts = [(x1, y1), (x1, y0), (x0, y0), (x0, y1)]
                up = (pts[1][0] - pts[0][0], pts[1][1] - pts[0][1])
                rv = ((pts[0][0] - pts[3][0]) / 2.0,
                      (pts[0][1] - pts[3][1]) / 2.0)
                got[word("cs_obj") & 0xFFFF] = (
                    kind, word("cs_bwp"), rv[0], rv[1], up[0], up[1],
                    word("cs_by1") - word("cs_by0"))
            m.bp_exec()
            m.run()
            if not got:
                sys.exit("skiesbank: no impostor was filled at roll %d - the "
                         "pose no longer has one" % roll)
            return got

        seen = {}
        for roll in (0, 30, 50):
            seen[roll] = impostor(roll)
            print("    roll %2d: %d impostor(s)" % (roll, len(seen[roll])))
        common = sorted(set(seen[0]) & set(seen[30]) & set(seen[50]))
        if not common:
            sys.exit("skiesbank: no building is an impostor at all three "
                     "banks - the pose no longer separates them")
        print("    %d building(s) impostored at every bank" % len(common))

        def ln(x, y):
            return math.hypot(x, y)

        obj = common[0]
        for roll in (0, 30, 50):
            kind, bwp, brx, bry, dxu, dyu, vert = seen[roll][obj]
            print("      roll %2d %-4s w=%d R=(%.1f,%.1f) drawn up=(%d,%d) "
                  "vertical part %d"
                  % (roll, kind, bwp, brx, bry, dxu, dyu, abs(vert)))

        kind0, w0, rx0, ry0, dx0, dy0, vert0 = seen[0][obj]
        check(ry0 == 0 and abs(rx0 - w0) <= 0.5,
              "level, the width vector is HORIZONTAL and whole: R = "
              "(%.1f, %.1f) against a half-width of %d" % (rx0, ry0, w0))

        for roll in (30, 50):
            kind, w, rx, ry, dxu, dyu, vert = seen[roll][obj]
            want = (w * math.cos(math.radians(roll)),
                    -w * math.sin(math.radians(roll)))
            err = ln(rx - want[0], ry - want[1])
            check(kind == "quad" and err <= 1.0,
                  "banked %d it is a %s and its width vector is the ROLL's: "
                  "R = (%.1f, %.1f) against w (cos r, -sin r) = (%.1f, %.1f), "
                  "%.2f px out (want a quad within 1)"
                  % (roll, kind, rx, ry, want[0], want[1], err))

        for roll in (30, 50):
            kind, w, rx, ry, dxu, dyu, vert = seen[roll][obj]
            full = ln(dxu, dyu)
            vert = abs(vert)
            check(full >= vert * 1.15,
                  "banked %d the impostor is drawn along the WHOLE projected "
                  "axis: %.1f px against the %.1f of its vertical part, which "
                  "is what the rectangle used to be (want 15%% more at least)"
                  % (roll, full, vert))
            check(abs(dxu) >= 2,
                  "...and that axis LEANS: its x is %d px, so the shape is "
                  "not an upright rectangle by another name" % dxu)

        # ...and the box does not change SIZE as it rolls: the half-width is
        # projected along the camera's own x, which no bank can compress.
        for roll in (30, 50):
            w = seen[roll][obj][1]
            check(abs(w - w0) <= 1,
                  "banked %d the half-width is unchanged: %d px against %d "
                  "level" % (roll, w, w0))

    print("skiesbank: %s" % ("ok" if not bad else "FAILED %d" % len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
