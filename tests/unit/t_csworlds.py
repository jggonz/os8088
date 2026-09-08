#!/usr/bin/env python3
"""EVERY CLEAR SKIES WORLD COSTS ABOUT WHAT PARIS COSTS (SPEC.md 88.6.4).

Nine locations stand in eight worlds since 88.6.4, and the whole frame budget
the simulator was built against - TWELVE FRAMES A SECOND on a 4.77 MHz 8088
with a Hercules card - was measured on ONE of them. SPEC.md 88.12's table is
Paris and nothing else, so a world written afterwards can miss that budget by
a factor and nothing says so: the picture still draws, it just draws slowly,
and slowly is invisible in an emulator (CLAUDE.md's three defects).

So this prices every world the way the renderer does, host-side, off
`build/skies.bin`, and holds each of them against PARIS - the one whose
milliseconds are actually known.

WHAT IT PRICES. A frame costs the objects the cull FILES, and each of those
costs its vertices transformed and its faces walked (88.5). So an object's
weight here is its expanded vertex count plus three per face plus one per
edge, which is the same shape as the renderer's own work and needs no
emulator to compute. The world's headline number is not the sum of that,
though - a world is not drawn all at once. It is the PEAK LOAD: the worst
sum over every sample eye position of the objects that could reach it, which
is the frame a player can actually make the machine draw.

WHAT IT REFUSES. A world whose peak load is more than 15% over Paris', a
world that can put more than 30 objects in one frame (CS_NVIS is 32 and the
33rd is dropped SILENTLY, which is a skyline that loses buildings rather
than a machine that slows down), a model over CS_MAXV, a collidable object
standing on its own runway, and a range past CS_FAR.

It is deliberately NOT about how a world looks. tests/unit/t_csworld.py is
the one that keeps buildings out of the water; this one is about the clock.
"""
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import dispapps                                             # noqa: E402

BASE = "PARIS-ISSY"     # the world SPEC.md 88.12's frame table was measured on
OVER = 1.15             # ...and how far over it another world may be
NVIS_MAX = 30           # CS_NVIS is 32 and the 33rd object is dropped silently
RWY_CLEAR = 20          # metres a collidable base keeps off its own runway.
                        # Not a style rule - Paris-Issy ships a shed 63 m off
                        # the edge and that is what a city aerodrome looks
                        # like. What this refuses is a box that OVERLAPS the
                        # rectangle, which is a crash on the take-off roll
SAMPLE_R = 6000         # how far a sample eye looks, in Manhattan metres


def equates(src):
    out = {}
    for line in open(src):
        m = re.match(r"^(CS[A-Z]*_[A-Z0-9_]+)\s+equ\s+"
                     r"(-?(?:0[xX][0-9A-Fa-f]+|\d+))\s*(?:;|$)", line)
        if m:
            # base 0, because CSO_POI is `equ 0x0100` and a `\d+` reader takes
            # the leading 0 and calls it zero - which is a flag test that is
            # always false and a gate that passes because it tested nothing
            out[m.group(1)] = int(m.group(2), 0)
    return out


def main():
    img = open(os.path.join(ROOT, "build", "skies.bin"), "rb").read()
    mp = dispapps._map("skies")
    E = equates(os.path.join(ROOT, "apps", "skies", "skies.asm"))
    need = ["CSM_TYPE", "CSM_NV", "CSM_NF", "CSM_NE", "CSM_VERTS", "CSM_FACES",
            "CSM_FLAT", "CSM_STACK", "CSO_MODEL", "CSO_FAR", "CSO_X", "CSO_Z",
            "CSO_RANGE", "CSO_NAME", "CSO_FLAGS", "CSO_SIZE", "CSO_COLLIDE",
            "CSM_EDGES", "CSI_NINK",
            "CSO_POI", "CSO_DENSE", "CSA_NAME", "CSA_X", "CSA_Z", "CSA_HDG",
            "CSA_HLEN", "CSA_HWID", "CSA_RWY", "CSA_OBJS", "CSA_NOBJ",
            "CS_MAXV", "CS_FAR"]
    miss = [n for n in need if n not in E]
    if miss:
        sys.exit("t_csworlds: skies.asm no longer defines %s" % ", ".join(miss))

    def w(off):
        v = int.from_bytes(img[off:off + 2], "little")
        return v - 65536 if v >= 32768 else v

    def zstr(at):
        return img[at:].split(b"\0")[0].decode("ascii", "replace")

    def model(at):
        """The expanded vertex count and the primitive counts. A STACK's
        CSM_NV is its LEVELS and each level stands for four corners, which is
        exactly the arithmetic cs_project does before it transforms any of
        them."""
        typ, nv = img[at + E["CSM_TYPE"]], img[at + E["CSM_NV"]]
        return {"verts": nv * 4 if typ == E["CSM_STACK"] else nv,
                "nf": img[at + E["CSM_NF"]], "ne": img[at + E["CSM_NE"]]}

    def weight(md):
        return md["verts"] + 3 * md["nf"] + md["ne"]

    def wellformed(at, nm, who):
        """Every face and edge index inside the model's own vertices.

        A model declares its counts in its header and its faces and edges in
        tables somewhere else, and nothing checks that the two agree: an index
        one past the end reads whatever the next model's bytes happen to be,
        transforms it, and draws a face into the middle distance. That is a
        picture, not a crash, so it survives every screendump. Worth having
        now that seven worlds were written at once against a format whose
        STACK vertices are FOUR to a declared level."""
        out, nv = [], model(at)["verts"]
        typ = img[at + E["CSM_TYPE"]]
        f, faces = w(at + E["CSM_FACES"]), img[at + E["CSM_NF"]]
        if faces and not f:
            out.append("%s: %s declares %d faces and no face table"
                       % (who, nm, faces))
        for _ in range(faces if f else 0):
            n = img[f]
            if not 3 <= n <= 8:
                out.append("%s: %s has a face of %d vertices" % (who, nm, n))
                break
            if img[f + 1] >= E["CSI_NINK"]:
                out.append("%s: %s has a face in ink %d, and there are %d"
                           % (who, nm, img[f + 1], E["CSI_NINK"]))
            for i in img[f + 3:f + 3 + n]:
                if i >= nv:
                    out.append("%s: %s indexes vertex %d of %d in a face"
                               % (who, nm, i, nv))
            f += 3 + n
        e, edges = w(at + E["CSM_EDGES"]), img[at + E["CSM_NE"]]
        if edges and not e:
            out.append("%s: %s declares %d edges and no edge table"
                       % (who, nm, edges))
        for k in range(2 * edges if e else 0):
            if img[e + k] >= nv:
                out.append("%s: %s indexes vertex %d of %d in an edge"
                           % (who, nm, img[e + k], nv))
        if typ not in (E["CSM_FLAT"], E["CSM_STACK"]):
            out.append("%s: %s is model type %d" % (who, nm, typ))
        return out

    # --- the locations, out of the table the drop-down reads (88.6.4) --------
    n_ports = (mp["cs_apnames"] - mp["cs_ports"]) // 2
    ports = [w(mp["cs_ports"] + 2 * i) for i in range(n_ports)]

    bad, rows = [], []
    for a in ports:
        who = zstr(w(a + E["CSA_NAME"]))
        objs, nobj = w(a + E["CSA_OBJS"]), w(a + E["CSA_NOBJ"])
        ax, az = w(a + E["CSA_X"]), w(a + E["CSA_Z"])
        hlen, hwid = w(a + E["CSA_HLEN"]), w(a + E["CSA_HWID"])
        hdg = w(a + E["CSA_HDG"]) & 0xFFFF
        th = hdg * 2.0 * math.pi / 65536.0
        ux, uz = math.sin(th), math.cos(th)         # along the runway
        px, pz = math.cos(th), -math.sin(th)        # across it

        # THE DESIGNATION IS THE HEADING, in tens of degrees, and the two are
        # written down in different fields of the same record - so a runway
        # aimed one way and lettered another is a copy-paste nobody would see
        # until they read the panel with a compass.
        des = re.search(r"(\d\d)", zstr(w(a + E["CSA_RWY"])))
        if not des:
            bad.append("%s: its runway designation has no number in it" % who)
        else:
            want = int(des.group(1)) * 10 % 360
            got = hdg * 360.0 / 65536.0
            off = abs((got - want + 180) % 360 - 180)
            if off > 10:
                bad.append("%s: it is lettered %s and heads %.0f degrees"
                           % (who, zstr(w(a + E["CSA_RWY"])), got))

        items, npoi, nden, total = [], 0, 0, 0
        rwy_here = [1e9, None]
        for o in range(objs, objs + nobj * E["CSO_SIZE"], E["CSO_SIZE"]):
            md = model(w(o + E["CSO_MODEL"]))
            far = w(o + E["CSO_FAR"])
            ox, oz = w(o + E["CSO_X"]), w(o + E["CSO_Z"])
            fl, rng = w(o + E["CSO_FLAGS"]) & 0xFFFF, w(o + E["CSO_RANGE"])
            nm = zstr(w(o + E["CSO_NAME"]))
            bad.extend(wellformed(w(o + E["CSO_MODEL"]), nm, who))
            if far:
                bad.extend(wellformed(far, nm + "'s far model", who))
            if md["verts"] > E["CS_MAXV"]:
                bad.append("%s: %s has %d vertices, over CS_MAXV %d"
                           % (who, nm, md["verts"], E["CS_MAXV"]))
            if far and model(far)["verts"] > E["CS_MAXV"]:
                bad.append("%s: %s's far model is over CS_MAXV" % (who, nm))
            if rng > E["CS_FAR"]:
                bad.append("%s: %s has range %d, past CS_FAR %d"
                           % (who, nm, rng, E["CS_FAR"]))
            if abs(ox) > 12000 or abs(oz) > 12000:
                bad.append("%s: %s stands at (%d,%d), outside the 12 km box"
                           % (who, nm, ox, oz))
            if fl & E["CSO_POI"]:
                npoi += 1
            if fl & E["CSO_DENSE"]:
                nden += 1
            # a collidable base may not stand on its own runway
            if fl & E["CSO_COLLIDE"]:
                dx, dz = ox - ax, oz - az
                al, ac = dx * ux + dz * uz, dx * px + dz * pz
                d = max(abs(al) - hlen, abs(ac) - hwid)
                if d < RWY_CLEAR:
                    bad.append("%s: %s is %.0f m from the runway rectangle, "
                               "under the %d m this wants"
                               % (who, nm, d, RWY_CLEAR))
                rwy_here[0] = min(rwy_here[0], d)
            # THE BUDGET IS THE DEFAULT RUNG'S (SPEC.md 88.13.1), so a
            # CSO_DENSE object is counted for CS_NVIS and not for the peak.
            # High is a 286/386 rung and is MEANT to be over what a 4.77 MHz
            # 8088 can carry; pricing it here would make the budget refuse
            # the feature rather than the regression it exists to catch.
            items.append((ox, oz, rng, weight(md), bool(fl & E["CSO_DENSE"])))
            if not fl & E["CSO_DENSE"]:
                total += weight(md)

        # --- the PEAK LOAD: the worst frame the world can be asked for ------
        eyes = [(ox, oz) for ox, oz, _, _, _ in items]
        eyes += [(round(ax + ux * s), round(az + uz * s))
                 for s in (-hlen, 0, hlen, 3000, 6000)]
        peak, peakn = 0, 0
        for ex, ez in eyes:
            load = n = 0
            for ox, oz, rng, wt, dense in items:
                if abs(ox - ex) + abs(oz - ez) <= min(rng, SAMPLE_R):
                    n += 1                      # CS_NVIS binds on every CPU
                    if not dense:
                        load += wt              # ...the WEIGHT is the default's
            peak, peakn = max(peak, load), max(peakn, n)
        if peakn > NVIS_MAX:
            bad.append("%s: %d objects can be in one frame, and CS_NVIS is "
                       "32 with the rest dropped silently" % (who, peakn))
        rows.append((who, nobj, npoi, nden, total, peak, peakn,
                     rwy_here[0]))

    base = [r for r in rows if r[0] == BASE]
    if not base:
        sys.exit("t_csworlds: %s is not in the location table any more, and "
                 "it is what every other world is priced against" % BASE)
    bpeak = base[0][5]

    print("  %-13s %4s %4s %4s %7s %7s %4s %8s %8s"
          % ("location", "objs", "poi", "dens", "weight", "peak", "vis",
             "rwy m", "vs base"))
    for who, nobj, npoi, nden, total, peak, peakn, rwy in rows:
        ratio = peak / float(bpeak) if bpeak else 0.0
        flag = "  <-- OVER" if ratio > OVER else ""
        print("  %-13s %4d %4d %4d %7d %7d %4d %8s %7.2fx%s"
              % (who, nobj, npoi, nden, total, peak, peakn,
                 "-" if rwy > 1e8 else "%.0f" % rwy, ratio, flag))
        if ratio > OVER:
            bad.append("%s's peak frame is %.2fx %s's, and %s is the only "
                       "world whose milliseconds are known (SPEC.md 88.12)"
                       % (who, ratio, BASE, BASE))

    if bad:
        print()
        for b in sorted(set(bad)):
            print("  " + b)
        sys.exit("t_csworlds: %d world(s) outside the budget (SPEC.md 88.6.4)"
                 % len(set(b.split(":")[0] for b in bad)))
    print("  csworlds: %d locations, peak frame %d..%d against %s's %d "
          "(SPEC.md 88.6.4)" % (len(rows), min(r[5] for r in rows),
                                max(r[5] for r in rows), BASE, bpeak))


if __name__ == "__main__":
    main()
