#!/usr/bin/env python3
"""THE AMPHIBIAN STARTS FACING THE CITY, AND FLOATING (SPEC.md 88.7.7.3).

The field flew the A5 at Paris-LBG and reported: *"I have to turn all the way
around to get to the POIs."*  It was right, and it was right at three of the
nine locations.  `CSA_WHDG` is the water strip's heading and `cs_reset` uses
it for BOTH ends of the pose - which end of the strip the hull sits on and
which way the nose points - so a strip declared along its own axis in the
wrong direction starts every A5 session looking away from everything the
world was built for.  At LBG the nearest point of interest, Notre-Dame at 889
metres, was **164 degrees behind the nose**.

The strip is the same water either way: `CSA_WLEN`/`CSA_WWID` are half-extents
about `CSA_WX`/`CSA_WZ`, so reversing the heading moves the aeroplane to the
other end of the identical rectangle.  That is the whole of the fix, and the
whole of what makes it invisible - nothing about the picture changes, so
nobody notices a strip laid the wrong way round until they fly it.

TWO ASSERTIONS PER LOCATION, and both are decided host-side out of the world
overlays (`csworlds.overlay`, `t_csworld.py`'s reader), so this costs seconds
and no emulator:

1. **The spawn is over DRAWN water.**  `cs_reset` sets `[cs_onwater]` = 1
   without asking, so an A5 whose strip has been moved off the river starts
   the session floating on grass and says nothing.  This is the point-in-
   polygon of §88.7.7.1 (`cs_inwater`) applied to the one point that routine
   never sees.

2. **Reversing the strip must not IMPROVE the view.**  Mean turn to the
   distinct points of interest, this heading against its reverse.  A plain
   "the POIs are ahead" would be wrong - Rio's five stand on both sides of
   Santos Dumont and no heading has them all in front - so the measure is
   comparative and carries a MARGIN.  The margin is defended by the two
   numbers either side of it rather than picked.  With the strips laid
   right, the worst location WAS SDU, where reversing gained **29.9
   degrees** - a real trade, Corcovado coming into view as Sugarloaf went
   out - until SPEC.md 88.7.7.5 laid that strip down the bay at Sugarloaf;
   reversing it now LOSES 18.6, and no laid-right strip gains anything.
   Laid wrong, the four that can be laid wrong gain **76.2 (SFO), 115.5
   (Issy), 139.2 (LBG) and 166.8 (LCY)**.  45 sits in the gap below 76.2.

--clobber-hdg <loc> reverses one location's strip in the reader, which is the
"break it on purpose" arm: it must go red on LBG, LCY and SFO and must NOT on
SDU.
"""
import argparse
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import csworlds                                             # noqa: E402

MARGIN = 45                     # degrees of mean turn a reversal must not gain


def equates(src):
    """skies.asm's record offsets, read rather than copied (t_csworld's)."""
    out = {}
    for line in open(os.path.join(ROOT, "apps", "skies", src)):
        m = re.match(r"^(CS[A-Z]*_[A-Z0-9_]+)\s+equ\s+"
                     r"(-?(?:0[xX][0-9A-Fa-f]+|\d+))\s*(?:;|$)", line)
        if m:
            out[m.group(1)] = int(m.group(2), 0)
    return out


def sg(v):
    return v - 65536 if v > 32767 else v


def inpoly(pts, px, pz):
    """cs_wpoly's own test: every edge's cross product one sign."""
    seen = 0
    for i in range(len(pts)):
        x0, z0 = pts[i]
        x1, z1 = pts[(i + 1) % len(pts)]
        c = (x1 - x0) * (pz - z0) - (z1 - z0) * (px - x0)
        seen |= 1 if c > 0 else (2 if c < 0 else 0)
    return seen != 3


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clobber-hdg", metavar="LOC",
                    help="reverse this location's strip, to watch it go red")
    args = ap.parse_args()

    E = equates("skies.asm")
    need = ("CSA_WX CSA_WZ CSA_WHDG CSA_WLEN CSA_OBJS CSA_NOBJ CSO_SIZE "
            "CSO_MODEL CSO_X CSO_Z CSO_NAME CSO_FLAGS CSO_POI CSM_TYPE "
            "CSM_NF CSM_VERTS CSM_FACES CSM_FLAT CSI_RIVER").split()
    missing = [n for n in need if n not in E]
    if missing:
        sys.exit("t_csamph: skies.asm no longer defines %s" % ", ".join(missing))
    g = globals()
    g.update({n: E[n] for n in need})

    bad, walked = [], 0
    for loc, world in csworlds.LOCATIONS:
        ov = csworlds.overlay(world)
        base = csworlds.world_map(world).get("cs_a_" + loc)
        if base is None:
            bad.append("%s: the world's map has no cs_a_%s" % (loc, loc))
            continue
        rd = lambda o, b=base: int.from_bytes(ov[b + o:b + o + 2], "little")
        wlen = sg(rd(CSA_WLEN))
        if wlen == 0:
            print("  %-5s no water strip - nothing for this gate to say" % loc)
            continue
        walked += 1
        whdg = rd(CSA_WHDG)
        if args.clobber_hdg == loc:
            whdg = (whdg + 32768) & 0xFFFF
            print("  %-5s CLOBBERED: strip reversed to %.0f deg"
                  % (loc, whdg * 360.0 / 65536.0))
        wx, wz = sg(rd(CSA_WX)), sg(rd(CSA_WZ))

        quads, pois = [], {}
        objs, nobj = rd(CSA_OBJS), rd(CSA_NOBJ)
        for o in range(objs, objs + nobj * CSO_SIZE, CSO_SIZE):
            ord_ = lambda x, b=o: int.from_bytes(ov[b + x:b + x + 2], "little")
            ox, oz = sg(ord_(CSO_X)), sg(ord_(CSO_Z))
            if ord_(CSO_FLAGS) & CSO_POI:
                at = ord_(CSO_NAME)
                # ONE ENTRY PER NAME: the Golden Gate is four objects and
                # Christ the Redeemer three, so a per-object mean would
                # weight a location by how finely its landmarks are modelled
                pois.setdefault(ov[at:ov.index(b"\0", at)].decode("ascii"),
                                (ox, oz))
            md = ord_(CSO_MODEL)
            if ov[md + CSM_TYPE] != CSM_FLAT:
                continue            # water is a ribbon, and a ribbon is FLAT
            vt = int.from_bytes(ov[md + CSM_VERTS:md + CSM_VERTS + 2], "little")
            f = int.from_bytes(ov[md + CSM_FACES:md + CSM_FACES + 2], "little")
            for _ in range(ov[md + CSM_NF]):
                cnt, ink = ov[f], ov[f + 1]
                if ink == CSI_RIVER:
                    quads.append(
                        [(ox + sg(int.from_bytes(ov[vt + 4 * i:vt + 4 * i + 2],
                                                 "little")),
                          oz + sg(int.from_bytes(ov[vt + 4 * i + 2:vt + 4 * i + 4],
                                                 "little")))
                         for i in ov[f + 3:f + 3 + cnt]])
                f += 3 + cnt

        def pose(h):
            """cs_reset's own arithmetic: 60 m in from the strip's threshold,
            pointing along it."""
            th = h * 2 * math.pi / 65536.0
            off = -(wlen - 60)
            return wx + off * math.sin(th), wz + off * math.cos(th)

        def meanturn(h):
            px, pz = pose(h)
            deg = h * 360.0 / 65536.0
            return sum(abs((math.degrees(math.atan2(x - px, z - pz)) - deg
                            + 540) % 360 - 180)
                       for x, z in pois.values()) / len(pois)

        sx, sz = pose(whdg)
        wet = any(inpoly(q, int(round(sx)), int(round(sz))) for q in quads)
        here, back = meanturn(whdg), meanturn((whdg + 32768) & 0xFFFF)
        print("  %-5s hdg %5.1f  spawn (%6.0f,%6.0f)  %-8s  %2d POIs  "
              "mean turn %5.1f, reversed %5.1f"
              % (loc, whdg * 360.0 / 65536.0, sx, sz,
                 "on water" if wet else "DRY LAND", len(pois), here, back))
        if not quads:
            bad.append("%s: a water strip in a world with no drawn water" % loc)
        elif not wet:
            bad.append("%s: the A5 starts at (%.0f, %.0f), which is not on any "
                       "drawn water - cs_reset would float it on land"
                       % (loc, sx, sz))
        if here - back > MARGIN:
            bad.append("%s: the strip points AWAY - mean turn to the %d points "
                       "of interest is %.1f deg and reversing the strip would "
                       "make it %.1f (gain %.1f, margin %d)"
                       % (loc, len(pois), here, back, here - back, MARGIN))

    if walked < 5:
        bad.append("only %d locations had a strip to walk - the reader is "
                   "finding nothing" % walked)
    if bad:
        print("\nt_csamph: FAIL")
        for b in bad:
            print("  - " + b)
        return 1
    print("\nt_csamph: ok - %d water strips, every A5 afloat and looking the "
          "right way" % walked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
