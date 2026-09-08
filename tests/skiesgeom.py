#!/usr/bin/env python3
"""CLEAR SKIES' clip and projection (SPEC.md 88.5.5-88.5.7), every polygon
and segment of a frame held to a host-side replay of the guest's own
arithmetic, on scenes pinned by poke - BANKED ones above all.

    python3 tests/skiesgeom.py [--machine os8088_5150_herc_gla] [--scene ...]

WHY THIS ROW EXISTS. tests/skies.py flies straight, and a straight climb was
fine. The owner flew a climbing turn on the 5150 and photographed a solid
wall filling half the view, a river polygon reaching the top of the sky
and a road drawn as a curve - and the frame rate at 3 fps under the fill.
MartyPC reproduced all of it at 20 degrees of bank, and the trace found TWO
faults, neither of which a straight flight reaches:

  - `cs_sidepass` emitted the inside end of an edge BEFORE computing the
    crossing, and `.emit` leaves that end's z in AX, so `cs_cxing` was
    handed z for the distance and put the crossing almost on the OUTSIDE
    point (88.5.7.1). Only the "A inside, B past the plane" case: the other
    computed first, which is why one edge of a polygon went and its
    neighbour did not.
  - the quarter-metre projection (`cs_project2`) shifted the product by 13
    through the middle-word form, which is sound only while |product| is
    under 2^23, and the clamp let 500 x 65536 through: every point between
    1,024 and 4,000 pixels from the centre came back with the wrong sign
    (88.5.6.1). The side-clip crossings sit at 1,772 - every one of them.

AND ONE INVARIANT THAT IS NOT A REPLAY. A world-vertical edge must project
vertical when the camera has neither pitch nor roll, because yaw alone never
mixes Y into X or Z. The replay cannot catch a fault in the ALGORITHM - it
reproduces the guest's own - so this one is checked directly. It is the
invariant behind "buildings lean over", which was reported off the machine
and turned out to be the horizon under a steep bank (SPEC.md 88.5.8).

THE REFERENCE is the guest's integer arithmetic, replayed: the camera-space
vertices and their flags are read out of the package's bss at each
polygon and edge (the transform is trusted - it is the same nine multiplies
a straight flight exercises), and the near pass, the four side passes and
the per-scale projection are done again here, `idiv` truncation, Q15 and
table buckets included. Both faults move a point by hundreds of pixels; the
tolerance is TOL.

AND THE WINDING IS THE WHOLE POLYGON'S AREA (SPEC.md 88.5.10). The back-face
test reads a signed area out of the projected points, and it read ONE
TRIANGLE'S - a fraction of a trapezoid's, and noise once the points are
rounded to whole pixels, which is why the Empire State's two crown faces went
on and off a frame at a time on the machine. It is a fan over the whole
polygon now, and this row holds the guest's own 32-bit sum to the shoelace of
the same points computed here: EXACTLY, because both are integer sums of the
same products. That is an outside fact about the polygon and not a replay of
the guest's arithmetic.

AND THE IMPOSTOR'S SIZE. cs_boxlod stands a distant solid up as ONE
SCREEN-AXIS-ALIGNED RECTANGLE, which is invisible at a few pixels and, in a
bank, the only thing on the glass that did not rotate. Its gate was on
CSM_RAD - wx + wz + h/2, which under-states a tall building's height - and
let 22 x 3 rectangles through on a 400-wide view. cs_rect has exactly one
caller, so any stop there is an impostor and its size is checked against
CS_LODPX, read out of skies.asm rather than mirrored (SPEC.md 88.5.4.1).

--clobber-proj, --clobber-side, --clobber-lod and --clobber-fan are the red runs
(docs/WRITING-TESTS.md 1): each patches one fault back into the guest's code
and the row must fail on it.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import dispapps                                             # noqa: E402
import skies as skiestest                                   # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOL = 3                                 # pixels: the replay is exact, the
                                        # margin is for a rounding it misses
CS_NEAR = 40
CS_MAXPV = 10
LODPX = int(re.search(r"^CS_LODPX\s+equ\s+(\d+)", open(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "apps", "skies",
    "skies.asm")).read(), re.M).group(1))   # read, not mirrored
SCENES = {   # airport (the launcher's Location row), x, y, z (metres);
             # heading, pitch, roll (65536 to the turn)
    "issy60":  (0, -2430, 35, -2097, 7646, 876, 10923),  # 60 right, climbing out
    "issy30":  (0, -2430, 60, -2097, 7646, 876, 5461),   # 30 right
    "level":   (0, -2689, 40, -2409, 7282, 876, 0),      # the straight climb
    "lbg60":   (1, 4305, 14, 4926, 13653, 876, 10923),   # Paris-LBG, just off
    "lbgm30":  (1, 4600, 40, 4945, 46421, 876, -5461),   # ...turned back over
                                                         # it, 30 left: the
                                                         # runway half behind
                                                         # the eye
    # --- LOW AND AMONG THE BUILDINGS, which none of the five above is: the
    #     owner photographed a solid standing on a slope it should not have,
    #     at 85 m over a city with the throttle open. A box that close is
    #     clipped by the near plane AND by two side planes at once, which a
    #     climb-out at 40 m over open country never asks for.
    "city85":  (0, 2400, 85, 0, 45875, 0, 0),            # by the Louvre, 252
    "city85b": (0, 200, 85, -200, 45875, 876, 1800),     # under the tower
    "city85c": (0, 3800, 85, -500, 20000, 0, 2000),      # over Notre-Dame
    "inval26": (0, 1320, 26, -1180, 0, 0, 0),            # LEVEL and 800 m
                                                         # from Les Invalides
                                                         # at 26 m: a wall 33
                                                         # pixels tall and
                                                         # WHOLLY IN VIEW,
                                                         # which is what the
                                                         # vertical invariant
                                                         # needs (88.5.8)
    "imp26":   (0, 2000, 26, -1800, 0, 0, 0),            # LEVEL at the foot of
                                                         # the Montparnasse
                                                         # tower, looking
                                                         # north up the city:
                                                         # the anonymous
                                                         # blocks here are
                                                         # what drew a 22 x 3
                                                         # IMPOSTOR (88.5.4.1)
    "lbg85":   (1, 4350, 85, 4800, 20000, 0, 3000),      # off Le Bourget
}
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def idiv(a, b):
    """8086 idiv: the quotient truncates toward zero."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def mul14(a, b):
    """MUL14: imul, the pair left one, DX - floor(a b / 32768)."""
    return (a * b) >> 15


class Ref:
    """The guest's clip and projection, replayed for one object."""
    PLANES = ((0, 1), (0, -1), (1, 1), (1, -1))     # component, sign

    def __init__(self, sclx, scly, vcx, vcy, pshr, near):
        self.sclx, self.scly, self.vcx, self.vcy = sclx, scly, vcx, vcy
        self.pshr, self.near = pshr, near
        self.bound, self.shift = {0: (125, 11), 2: (500, 13), 4: (2000, 15)}[pshr]

    # --- projection (cs_project0/2/4, cs_pnear, cs_ktabs) --------------------
    @staticmethod
    def krow_z(row):
        if row < 1024:
            return max(row, CS_NEAR)
        if row < 1792:
            return 1024 + 4 * (row - 1024) + 2
        return 4096 + 64 * (row - 1792) + 32

    def psh(self, prod):
        dx = prod >> 16
        if dx >= self.bound:
            return 4000
        if dx < -(self.bound + 1):
            return -4000
        return prod >> self.shift

    def project(self, p):
        x, y, z = p
        if z < (CS_NEAR << self.pshr):
            return self.pnear(x, y, z)
        zm = z >> self.pshr
        if zm < 1024:
            row = zm
        elif zm < 4096:
            row = 1024 + ((zm - 1024) >> 2)
        else:
            row = min(2047, 1792 + ((zm - 4096) >> 6))
        zr = self.krow_z(row)
        kx, ky = (self.sclx * 2048) // zr, (self.scly * 2048) // zr
        return (self.psh(x * kx) + self.vcx, self.vcy - self.psh(y * ky))

    def pnear(self, x, y, z):
        if abs(x) > 9 * z:
            sx = 4000 if x >= 0 else -4000
        else:
            sx = idiv(x * self.sclx, z)
        if abs(y) > 14 * z:
            sy = 4000 if y >= 0 else -4000
        else:
            sy = idiv(y * self.scly, z)
        return (sx + self.vcx, self.vcy - sy)

    # --- the clippers (cs_ncross, cs_cdist, cs_cxing) -------------------------
    def ncross(self, a, b):
        """The point where a -> b crosses z = near, as cs_ncross computes it."""
        den = b[2] - a[2]
        num = self.near - a[2]
        return (idiv((b[0] - a[0]) * num, den) + a[0],
                idiv((b[1] - a[1]) * num, den) + a[1],
                self.near)

    @staticmethod
    def dist(plane, p):
        c, s = Ref.PLANES[plane]
        v = p[c] if s > 0 else -p[c]
        return (p[2] >> 2) - (v >> 4)

    @staticmethod
    def cross(a, b, da, db):
        t = idiv(da * 32767, da - db)
        return tuple(a[i] + mul14(b[i] - a[i], t) for i in range(3))

    def clip_face(self, pts, fv):
        """cs_fclip: the near pass, then the four sides; the projected result."""
        out = []
        n = len(pts)
        for i in range(n):
            a, b = i, (i + 1) % n
            if fv[a]:
                out.append(pts[a])
                if not fv[b]:
                    out.append(self.ncross(pts[a], pts[b]))
            elif fv[b]:
                out.append(self.ncross(pts[a], pts[b]))
        for plane in range(4):
            if not out:
                break
            d = [self.dist(plane, p) for p in out]
            if all(v >= 0 for v in d):
                continue
            nxt = []
            for i in range(len(out)):
                j = (i + 1) % len(out)
                if d[i] >= 0:
                    if d[j] >= 0:
                        nxt.append(out[i])
                    else:
                        nxt.append(out[i])
                        nxt.append(self.cross(out[i], out[j], d[i], d[j]))
                elif d[j] >= 0:
                    nxt.append(self.cross(out[i], out[j], d[i], d[j]))
            out = nxt[:CS_MAXPV]
        return [self.project(p) for p in out]

    def clip_edge(self, a, b, fva, fvb):
        """cs_edge1's general path; None when nothing is drawn."""
        if fva + fvb == 0:
            return None
        if fva == 0:
            a = self.ncross(b, a)
        elif fvb == 0:
            b = self.ncross(a, b)
        for plane in range(4):
            da, db = self.dist(plane, a), self.dist(plane, b)
            if da < 0:
                if db < 0:
                    return None
                a = self.cross(a, b, da, db)
            elif db < 0:
                b = self.cross(a, b, da, db)
        return (self.project(a), self.project(b))


def far(p, q):
    return max(abs(p[0] - q[0]), abs(p[1] - q[1]))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--scene", action="append",
                    help="one of %s (default: all)" % ", ".join(SCENES))
    ap.add_argument("--clobber-proj", action="store_true",
                    help="put the middle-word shift back in cs_project2: must go red")
    ap.add_argument("--clobber-side", action="store_true",
                    help="emit before the crossing in cs_sidepass again: must go red")
    ap.add_argument("--clobber-lod", action="store_true",
                    help="let cs_boxlod draw any size again: must go red")
    ap.add_argument("--clobber-fan", action="store_true",
                    help="wind on ONE triangle again, not the whole polygon: "
                         "must go red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    scenes = a.scene or list(SCENES)
    mp = dispapps._map("skies")
    maxv = (mp["cs_cyv"] - mp["cs_cxv"]) // 2

    def off(n):
        return dispapps.bss_off("skies", n)
    sg = lambda v: v - 0x10000 if v >= 0x8000 else v   # noqa: E731

    windall = 0                         # faces whose winding was checked, over
                                        # every scene: a run that examined none
                                        # must not pass
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        slot, seg, base = skiestest.open_game(m)
        lin = seg << 4

        def w(name):
            return int.from_bytes(m.readseg(seg, base + off(name), 2), "little")

        def sw(name):
            return sg(w(name))

        def byte(name):
            return m.readseg(seg, base + off(name), 1)[0]

        def words(name, n):
            d = m.readseg(seg, base + off(name), 2 * n)
            return [sg(int.from_bytes(d[2 * i:2 * i + 2], "little")) for i in range(n)]

        def poke(name, data):
            m.write(lin + base + off(name), data)

        def name_of(ob):
            nm = int.from_bytes(m.readseg(seg, ob + 14, 2), "little")
            return m.readseg(seg, nm, 24).split(b"\0")[0].decode("ascii", "replace")

        m.type_text("f")
        m.advance(frames=30)
        m.run()
        check(byte("cs_back") != 0, "the bracket adopted a raster (back %d)" % byte("cs_back"))
        sclx, scly, vcx, vcy = w("cs_sclx"), w("cs_scly"), w("cs_vcx"), w("cs_vcy")
        print("  view scales %d/%d, centre (%d,%d)" % (sclx, scly, vcx, vcy))

        # --- the red runs: one fault each, patched back in ----------------------
        if a.clobber_proj:
            lo, hi = mp["cs_project2"], mp["cs_project4"]
            code = bytearray(m.read(lin + lo, hi - lo))
            new = bytes.fromhex("d1e0d1d2d1e0d1d2d1e0d1d2" "89d0")
            old = bytes.fromhex("88e088d4" "d1f8d1f8d1f8d1f8d1f8")
            n = code.count(new)
            if n != 2:
                sys.exit("skiesgeom: cs_project2 does not carry the pair shift twice (%d)" % n)
            m.pause()
            m.write(lin + lo, bytes(code.replace(new, old)))
            m.run()
            print("  (cs_project2's shift put back to the middle-word form: this run must fail)")
        if a.clobber_lod:
            # cs_boxlod's two `cmp si, CS_LODPX`, which nasm emits as the
            # sign-extended imm8 form; 127 is past every rectangle it can
            # draw, so the refusal never fires - the gate exactly as it was
            lo, hi = mp["cs_boxlod"], mp["cs_stackverts"]
            code = m.read(lin + lo, hi - lo)
            sites = [lo + i for i in range(len(code) - 2)
                     if code[i] == 0x83 and code[i + 1] == 0xFE
                     and code[i + 2] == LODPX]
            if len(sites) != 2:
                sys.exit("skiesgeom: cs_boxlod does not test CS_LODPX twice "
                         "the way this patch expects (%d found)" % len(sites))
            m.pause()
            for st in sites:
                m.write(lin + st + 2, b"\x7F")
            m.run()
            print("  (cs_boxlod's size refusal raised past every rectangle: "
                  "this run must fail)")
        if a.clobber_side:
            lo, hi = mp["cs_sidepass"], mp["cs_edges"]
            code = m.read(lin + lo, hi - lo)
            site = None
            for i in range(len(code) - 6):
                if code[i] == 0xE8 and code[i + 3] == 0xE8:
                    rel = sg(int.from_bytes(code[i + 1:i + 3], "little"))
                    if lo + i + 3 + rel == mp["cs_cxing"]:
                        site = i
                        break
            if site is None:
                sys.exit("skiesgeom: no `call cs_cxing` followed by a call in cs_sidepass")
            s1, s2 = lo + site, lo + site + 3
            t1 = mp["cs_cxing"]
            t2 = s2 + 3 + sg(int.from_bytes(code[site + 4:site + 6], "little"))
            m.pause()
            m.write(lin + s1 + 1, ((t2 - (s1 + 3)) & 0xFFFF).to_bytes(2, "little"))
            m.write(lin + s2 + 1, ((t1 - (s2 + 3)) & 0xFFFF).to_bytes(2, "little"))
            m.run()
            print("  (cs_sidepass emits before it computes the crossing again: this run must fail)")

        # --- 88.5.10's three sites, found by their BYTES in cs_faces: the
        #     `cmp cx, 4` that sends a quadrilateral to its diagonals, the
        #     `mov ax, [cs_pn] / dec ax / dec ax` that sets the general fan's
        #     length, and the `mov byte [cs_pwind], 0` that the sign test
        #     straddles. Resolved rather than mirrored, because any of them
        #     is one edit from moving and a remembered offset would stop at a
        #     wrong instruction and check nothing.
        flo, fhi = mp["cs_faces"], mp["cs_wire"]
        fcode = m.read(lin + flo, fhi - flo)
        g = fcode.find(b"\x8B\x0E" + (base + off("cs_pn")).to_bytes(2, "little")
                       + b"\x83\xF9\x03")
        h = fcode.find(b"\x83\xF9\x04\x75", g) if g >= 0 else -1
        i = fcode.find(b"\xA1" + (base + off("cs_pn")).to_bytes(2, "little") + b"\x48\x48")
        j = fcode.find(b"\xC6\x06" + (base + off("cs_pwind")).to_bytes(2, "little") + b"\x00\x78")
        if g < 0 or h < 0 or i < 0 or j < 0:
            sys.exit("skiesgeom: cs_faces does not hold the quad test, the "
                     "fan's length and its sign test where 88.5.10 puts them "
                     "(%d, %d, %d)" % (h, i, j))
        quadn, fanlen, wsite = flo + h + 2, flo + i + 3, flo + j
        if a.clobber_fan:
            # no polygon has 0 points, so every one takes the general fan -
            # with a length of one, which is the single triangle exactly
            m.pause()
            m.write(lin + quadn, b"\x00")
            m.write(lin + fanlen, b"\xB0\x01")   # mov al, 1
            m.run()
            print("  (the winding is one triangle again, not the whole "
                  "polygon: this run must fail)")

        render, panel = mp["cs_render"], mp["cs_panel"]
        stops = {mp[k]: k for k in ("cs_drawobj", "cs_poly", "cs_seg", "cs_edge1",
                                    "cs_panel", "cs_rect")}
        stops[wsite] = "cs_wind"

        def frames(n):
            m.bp_exec(lin + render)
            m.run()
            if m.wait_stop(20) is None:
                sys.exit("skiesgeom: cs_render never ran")
            for _ in range(n):
                m.run()
                if m.wait_stop(20) is None:
                    sys.exit("skiesgeom: the frame never came")
            m.bp_exec()

        # WHICH ROW OF THE LOCATION LIST each of the two Paris runways is, read
        # off cs_ports rather than assumed: the list is nine long and sorted by
        # its own names since SPEC.md 88.6.4, so Paris-Issy is not row 0 any
        # more and the next rename would move it again.
        nports = (mp["cs_apnames"] - mp["cs_ports"]) // 2
        ports = [int.from_bytes(m.readseg(seg, mp["cs_ports"] + 2 * i, 2), "little")
                 for i in range(nports)]
        recs = [mp["cs_a_issy"], mp["cs_a_lbg"]]
        try:
            LROW = [ports.index(r) for r in recs]
        except ValueError:
            sys.exit("skiesgeom: cs_a_issy/cs_a_lbg are not both in cs_ports")

        airport = 0
        for sc in sorted(scenes, key=lambda n: SCENES[n][0]):
            row, x, y, z, hdg, pitch, roll = SCENES[sc]
            if row != airport:
                # --- the runway is built at bracket entry from the launcher's
                #     pick (cs_runway_build), so a scene at the other airport
                #     leaves the bracket, picks it in the Location list
                #     (SPEC.md 88.10) and comes back in --------------------
                m.type_text("f")
                m.advance(frames=40)
                m.run()
                po = [int.from_bytes(m.readseg(seg, mp["cs_drport"] + 2 * i, 2), "little")
                      for i in range(4)]
                mo = os88mouse.Mouse(marty=m)
                mo.click((po[0] + po[2]) // 2, (po[1] + po[3]) // 2)
                m.advance(frames=20)
                m.run()
                # the open list's first row is os88ui_drfit's answer and no
                # longer the row under the box (SPEC.md 13.14.2)
                top = int.from_bytes(m.readseg(seg, mp["cs_drport"] + 22, 2), "little")
                mo.click(po[0] + 20, top + 1 + 12 * LROW[row] + 6)
                m.advance(frames=20)
                m.run()
                check(w("cs_airport") == recs[row],
                      "the Location list picked %s at row %d (cs_airport %04x)"
                      % (("Paris-Issy", "Paris-LBG")[row], LROW[row], w("cs_airport")))
                m.type_text("f")
                m.advance(frames=40)
                m.run()
                airport = row
            m.pause()
            for name, v in (("cs_px", x), ("cs_py", y), ("cs_pz", z)):
                poke(name, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", (hdg & 0xFFFF).to_bytes(2, "little"))
            poke("cs_pitch", (pitch & 0xFFFF).to_bytes(2, "little"))
            poke("cs_roll", (roll & 0xFFFF).to_bytes(2, "little"))
            poke("cs_state", b"\x01")
            poke("cs_pause", b"\x01")
            # THE WORLD IS THE PICKED LOCATION'S since SPEC.md 88.6.4, so the
            # skips to clear are the ones in the table its record names and
            # not a global cs_objtab, which no longer exists.
            ap = w("cs_airport")
            objs = int.from_bytes(m.read(lin + ap + 18, 2), "little")
            nobj = int.from_bytes(m.read(lin + ap + 20, 2), "little")
            for o in range(objs, objs + nobj * 20, 20):
                m.write(lin + o + 18, b"\x00\x00")
            m.run()
            frames(3)
            # --- one frame, held to the replay at every polygon and segment --
            m.bp_exec(lin + render)
            m.run()
            if m.wait_stop(20) is None:
                sys.exit("skiesgeom: cs_render never ran")
            m.bp_exec(lin + render, *[lin + s for s in stops])
            cur, ref, npoly, nseg, worst = "?", None, 0, 0, 0
            imp = (0, None)             # the biggest IMPOSTOR rectangle
                                        # (88.5.4.1): cs_rect has exactly one
                                        # caller, cs_boxlod, so any stop there
                                        # is one
            lean = (0, None, None)      # the worst world-vertical edge that
            leann = 0                   # did not come out vertical (88.5.8),
                                        # and how many were looked at - a
                                        # check that examined nothing must
                                        # not pass
            pending = None                  # (edge, reference) awaiting its cs_seg
            wind = (0, None)                # the worst winding against the
            windn = 0                       # shoelace of the same points
            errs = []

            def geom():
                pshr, near = byte("cs_pshr"), sw("cs_near")
                return (Ref(sclx, scly, vcx, vcy, pshr, near),
                        list(zip(words("cs_cxv", maxv), words("cs_cyv", maxv), words("cs_czv", maxv))),
                        words("cs_fv", maxv), byte("cs_pwhole"))

            def settle_pending(k):
                nonlocal pending
                if pending is not None and pending[1] is not None:
                    errs.append("%s edge %s: the guest drew nothing where the replay draws %s (next stop %s)"
                                % (cur, pending[0], pending[1], k))
                pending = None

            while True:
                m.run()
                if m.wait_stop(20) is None:
                    sys.exit("skiesgeom: the trace stalled in %s" % sc)
                ip = m.status()["ip"]
                k = stops.get(ip)
                if ip == render or k == "cs_panel":
                    settle_pending(k)
                    break
                r = m.regs()
                ax, bx, cx, dx, si = (r[q] & 0xFFFF for q in ("ax", "bx", "cx", "dx", "si"))
                if k != "cs_seg":
                    settle_pending(k)
                if k == "cs_drawobj":
                    ob = int.from_bytes(m.readseg(seg, si, 2), "little")
                    cur = name_of(ob)
                elif k == "cs_rect":
                    x0, y0, x1, y1 = (sg(v) for v in (ax, bx, cx, dx))
                    big = max(x1 - x0, y1 - y0)
                    if big > imp[0]:
                        imp = (big, cur)
                if k == "cs_wind":
                    # SI:CX is the guest's twice-signed-area, whatever the
                    # face is about to be; the shoelace of cs_pv is the same
                    # quantity computed here, and they are integers.
                    v = (si << 16) | cx
                    if v >= 1 << 31:
                        v -= 1 << 32
                    n = w("cs_pn")
                    pv = words("cs_pv", 2 * n)
                    q = [(pv[2 * t], pv[2 * t + 1]) for t in range(n)]
                    sho = sum(q[t][0] * q[(t + 1) % n][1] - q[(t + 1) % n][0] * q[t][1]
                              for t in range(n))
                    windn += 1
                    if abs(v - sho) > wind[0]:
                        wind = (abs(v - sho), (cur, n, v, sho))
                    continue
                if k == "cs_poly":
                    R, pts, fv, whole = geom()
                    fn = w("cs_fn")
                    idx = list(m.readseg(seg, w("cs_fidx"), fn))
                    got = [(sg(int.from_bytes(m.readseg(seg, si + 4 * i, 2), "little")),
                            sg(int.from_bytes(m.readseg(seg, si + 4 * i + 2, 2), "little")))
                           for i in range(cx)]
                    if whole or all(fv[i] == 1 for i in idx):
                        want = [R.project(pts[i]) for i in idx]
                    else:
                        want = R.clip_face([pts[i] for i in idx], [fv[i] for i in idx])
                    npoly += 1
                    # A WORLD-VERTICAL EDGE MUST PROJECT VERTICAL when the
                    # camera has neither pitch nor roll: yaw alone never
                    # mixes Y into X or Z, so its two ends share a screen x.
                    # This is the invariant behind "buildings lean over" and
                    # it is independent of the replay above - the replay
                    # reproduces the guest's own algorithm, so an algorithmic
                    # fault would agree with itself.
                    #
                    # WHICH EDGES ARE VERTICAL is asked of the vertices and
                    # not of the face table: CS_SIDES emits base+1, top+1,
                    # top+0, base+0, so (0,1) and (2,3) are the columns - but
                    # only where the band is a PRISM. A TAPERED one (a dome,
                    # a spire, the Empire State's setback) leans by design,
                    # and reading the pairs off the table alone reported that
                    # as a fault the moment 88.5.10's winding stopped culling
                    # those faces by accident. Two ends at the same camera x
                    # AND z are at the same world x and z, a yaw being a
                    # rotation, so that is the question asked.
                    # ...on a face that was NOT CLIPPED, because a clip
                    # renumbers the list. cs_pwhole is the guest's own word.
                    if not roll and not pitch and cx == 4 and whole:
                        for va, vb in ((0, 1), (2, 3)):
                            pa, pb = pts[idx[va]], pts[idx[vb]]
                            if pa[0] != pb[0] or pa[2] != pb[2]:
                                continue        # not a vertical edge at all
                            dx_ = abs(got[va][0] - got[vb][0])
                            dy_ = abs(got[va][1] - got[vb][1])
                            if dy_ >= 8 and abs(got[va][0]) < 4000:
                                leann += 1
                                if dx_ > lean[0]:
                                    lean = (dx_, cur, (got[va], got[vb]))
                    if len(want) != len(got):
                        errs.append("%s face %s: %d points against the replay's %d: %s vs %s"
                                    % (cur, idx, len(got), len(want), got, want))
                    else:
                        d = max(far(p, q) for p, q in zip(got, want))
                        worst = max(worst, d)
                        if d > TOL:
                            errs.append("%s face %s (scale %d): %s against the replay's %s (%d px off)"
                                        % (cur, idx, R.pshr, got, want, d))
                elif k == "cs_edge1":
                    R, pts, fv, whole = geom()
                    ea, eb = ax & 0xFF, ax >> 8
                    if whole or (fv[ea] == 1 and fv[eb] == 1):
                        want = (R.project(pts[ea]), R.project(pts[eb]))
                    else:
                        want = R.clip_edge(pts[ea], pts[eb], fv[ea], fv[eb])
                    pending = ((ea, eb), want)
                elif k == "cs_seg":
                    got = ((sg(ax), sg(bx)), (sg(cx), sg(dx)))
                    nseg += 1
                    if pending is None:
                        errs.append("%s: a segment %s with no edge before it" % (cur, got))
                    elif pending[1] is None:
                        errs.append("%s edge %s: drawn %s where the replay draws nothing" % (cur, pending[0], got))
                    else:
                        d = max(far(got[0], pending[1][0]), far(got[1], pending[1][1]))
                        worst = max(worst, d)
                        if d > TOL:
                            errs.append("%s edge %s: %s against the replay's %s (%d px off)"
                                        % (cur, pending[0], got, pending[1], d))
                    pending = None
            m.bp_exec()
            m.run()
            for e in errs[:6]:
                print("      " + e)
            check(not errs, "%s: %d polygons and %d segments match the replay (worst %d px, %d wrong)"
                  % (sc, npoly, nseg, worst, len(errs)))
            check(npoly + nseg >= 6, "%s: the frame had something to check (%d polygons, %d segments)"
                  % (sc, npoly, nseg))
            windall += windn
            check(windn >= 1 and wind[0] == 0,
                  "%s: %d faces wound on the WHOLE polygon's area (worst %d "
                  "off the shoelace%s)"
                  % (sc, windn, wind[0],
                     ": %s, %d points, %d against %d" % wind[1] if wind[0] else ""))
            if imp[0]:
                check(imp[0] <= LODPX,
                      "%s: no impostor is bigger than CS_LODPX (%d px%s)"
                      % (sc, imp[0], ", " + imp[1] if imp[1] else ""))
            if not roll and not pitch:
                check(leann >= 4 and lean[0] <= 1,
                      "%s: %d world-vertical edges, and they stay vertical "
                      "with the wings level (worst %d px%s)"
                      % (sc, leann, lean[0],
                         ": %s %s" % (lean[1], lean[2]) if lean[1] else ""))
        m.type_text("f")
        m.advance(frames=30)
        m.run()
    check(windall >= 40,
          "the winding was checked on %d faces over the whole run" % windall)

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
