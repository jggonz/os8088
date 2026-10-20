#!/usr/bin/env python3
"""CLEAR SKIES' air is a TILE, and it tiles (SPEC.md 88.7.6.4).

    python3 tests/unit/t_csair.py

`cs_lifts` is four rects of lift and sink placed inside a CS_AIRTILE square
that repeats round the location's own centre, so the same weather stands
everywhere in all nine worlds. Two things have to hold and neither is
visible in a flight until somebody has flown for five minutes and found
nothing, which is how the first version was reported:

  - **every rect lies WHOLLY inside the tile.** The wrap is an AND, so a rect
    that crossed the edge would be cut in half and the other half would not
    reappear - it would simply be smaller than the table says.
  - **the tile is worth flying into.** Coverage under about a tenth is the
    old complaint back; over about a half is weather rather than thermals.

CS_LIFTCLR is no longer a constraint on where a rect may go: it is a CALM
BUBBLE tested before the tile, so the circuit is still and the tile is free
to be dense. That independence is the whole of 88.7.6.4.
"""
import math
import os
import random
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "apps", "skies", "csflight.inc")
ASM = os.path.join(ROOT, "apps", "skies", "skies.asm")
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def equ(path, name):
    m = re.search(r"^%s\s+equ\s+(-?\d+)" % name, open(path).read(), re.M)
    if not m:
        raise SystemExit("t_csair: %s is not defined in %s" % (name, path))
    return int(m.group(1))


def main():
    text = open(SRC).read()
    clr, n = equ(SRC, "CS_LIFTCLR"), equ(SRC, "CS_NLIFT")
    m = re.search(r"^cs_lifts:\n((?:\s*dw .*\n)+)", text, re.M)
    check(bool(m), "cs_lifts is a table of dw rows in csflight.inc")
    if not m:
        return 1
    rows = []
    for line in m.group(1).splitlines():
        v = [int(t) for t in line.split(";")[0].split("dw")[1].split(",")]
        rows.append(v)
    check(len(rows) == n, "CS_NLIFT is %d and the table has %d" % (n, len(rows)))
    check(all(len(r) == 5 for r in rows),
          "every row is CSAIR_SIZE's five words (%s)"
          % [len(r) for r in rows if len(r) != 5])

    tile = equ(SRC, "CS_AIRTILE")
    check(tile > 0 and (tile & (tile - 1)) == 0,
          "CS_AIRTILE is a power of two, because the wrap is an AND (%d)"
          % tile)
    area = 0
    for i, (dx, dz, hw, hd, rate) in enumerate(rows):
        check(0 <= dx - hw and dx + hw < tile and
              0 <= dz - hd and dz + hd < tile,
              "rect %d (%d,%d %dx%d) lies WHOLLY inside the %d tile - the "
              "wrap would cut it, not move it"
              % (i, dx, dz, 2 * hw, 2 * hd, tile))
        check(hw > 0 and hd > 0, "rect %d has size (%d x %d)" % (i, hw, hd))
        check(rate != 0, "rect %d does something (rate %d)" % (i, rate))
        area += 4 * hw * hd

    pct = 100.0 * area / (tile * tile)
    check(10.0 <= pct <= 50.0,
          "...and the tile is worth flying into: %.0f%% of it is lift or "
          "sink, which a glider meets inside a minute" % pct)
    up = sum(4 * r[2] * r[3] for r in rows if r[4] > 0)
    down = sum(4 * r[2] * r[3] for r in rows if r[4] < 0)
    check(up > 0 and down > 0 and max(up, down) <= 2 * min(up, down),
          "...and there is both lift and sink to find, in comparable"
          " measure (%d up, %d down; %.0f%% / %.0f%% of the tile)"
          % (sum(1 for r in rows if r[4] > 0),
             sum(1 for r in rows if r[4] < 0),
             100.0 * up / (tile * tile), 100.0 * down / (tile * tile)))
    check(clr > 0,
          "the calm bubble round the field is still there (CS_LIFTCLR %d)"
          % clr)
    # ...AND NO CORE SITS IN IT (88.7.6.5.1). The field read "37 up, a short
    # sweep, then instantly 11 down" crossing the bubble's edge, because rect
    # 0's core was 1,700 m out against an 1,800 m edge. The edge ramps now,
    # but a core inside the bubble or its ramp is a thermal with its middle
    # taken out. The release is the tile's CENTRE (88.7.6.7), so it is the
    # one copy of each rect that can be near it
    rmp = equ(SRC, "CS_LIFTRMP")
    half = tile // 2
    near = [(i, dx, dz, abs(dx - half) + abs(dz - half))
            for i, (dx, dz, hw, hd, rate) in enumerate(rows)
            if rate > 0 and abs(dx - half) + abs(dz - half) < clr + rmp // 2]
    check(not near,
          "no lift rect's CORE is inside the calm bubble or the first half of "
          "its %d m ramp round the release, the tile's centre (%s)"
          % (rmp, near))

    # --- AND THE AIR CAN BE FOUND (88.7.6.7): "it took me 5 minutes before I
    #     hit a single one". cs_lift and cs_airtile as built - the release at
    #     the tile's centre, each tile one of eight orientations - flown on
    #     36 headings from the release and on 120 from anywhere, to the first
    #     metre of 1 m/s of lift
    def variant(tx, tz):
        return ((tx * 3) ^ (tz * 5)) & 7

    def air(x, z):
        d = abs(x) + abs(z)
        if d < clr:
            return 0
        k = min(256, (d - clr) // 2)
        x += half
        z += half
        cx, cz, v = x & (tile - 1), z & (tile - 1), variant(x >> 12, z >> 12)
        if v & 1:
            cx, cz = cz, cx
        if v & 2:
            cx = -cx & (tile - 1)
        if v & 4:
            cz = -cz & (tile - 1)
        for dx, dz, hw, hd, rate in rows:
            u, w = abs(cx - dx), abs(cz - dz)
            if u <= hw and w <= hd:
                mm = max(u * 256 // hw, w * 256 // hd)
                return (256 - mm * mm // 256) * k // 256 * rate // 256
        return 0

    def first(sx, sz, hdg, limit=20000):
        a = math.radians(hdg)
        for d in range(0, limit, 40):
            if air(int(sx + d * math.sin(a)), int(sz + d * math.cos(a))) >= 128:
                return d
        return limit

    av = re.search(r"^cs_airvar:\n((?:    .*\n)+?)    ret", text, re.M)
    check(bool(av) and re.sub(r"\s*;.*", "", av.group(1)).split() ==
          "mov ah, al shl al, 1 add al, ah mov bh, bl shl bl, 1 shl bl, 1 "
          "add bl, bh xor al, bl and al, 7".split(),
          "cs_airvar is the (3 tx xor 5 tz) and 7 this models")
    own = sorted(first(0, 0, h) for h in range(0, 360, 10))
    rnd = random.Random(7)
    anywhere = sorted(first(rnd.randrange(3000, 3000 + tile),
                            rnd.randrange(3000, 3000 + tile), rnd.randrange(360))
                      for _ in range(120))
    print("      first lift from the release: median %.1f km, 90%% %.1f, worst "
          "%.1f; from anywhere: median %.1f km, 90%% %.1f"
          % (own[18] / 1000, own[32] / 1000, own[-1] / 1000,
             anywhere[60] / 1000, anywhere[108] / 1000))
    check(own[18] <= 1600,
          "from the release, half the headings meet lift inside 1.6 km "
          "(median %d m)" % own[18])
    check(own[-1] <= 6000,
          "...and EVERY heading inside 6 km - three take-off lines met none "
          "in 20 (worst %d m)" % own[-1])
    check(anywhere[60] <= 1200,
          "...and from anywhere, half the headings inside 1.2 km (%d m)"
          % anywhere[60])
    # no tile is its neighbour's mirror across their shared edge, nor the same
    # picture: either is the grid the map would show (88.16.1)
    def mat(v):
        m = [[0, 1], [1, 0]] if v & 1 else [[1, 0], [0, 1]]
        if v & 2:
            m = [[-m[0][0], -m[0][1]], m[1]]
        if v & 4:
            m = [m[0], [-m[1][0], -m[1][1]]]
        return m

    def rel(a, b):
        ta = [[a[0][0], a[1][0]], [a[0][1], a[1][1]]]
        return [[sum(ta[i][k] * b[k][j] for k in range(2)) for j in range(2)]
                for i in range(2)]
    pairs = []
    for tx in range(-6, 6):
        for tz in range(-6, 6):
            a = mat(variant(tx, tz))
            if rel(a, mat(variant(tx + 1, tz))) in ([[-1, 0], [0, 1]],
                                                     [[1, 0], [0, 1]]):
                pairs.append(("x", tx, tz))
            if rel(a, mat(variant(tx, tz + 1))) in ([[1, 0], [0, -1]],
                                                     [[1, 0], [0, 1]]):
                pairs.append(("z", tx, tz))
    check(variant(0, 0) == 0 and not pairs,
          "the release's tile is the table as written, and no two neighbours "
          "are mirrors across their edge or the same picture (%s)" % pairs[:4])
    # the swoop's two ends are the ones the tone slides between
    lo, hi = equ(ASM, "CS_SWOOPLO"), equ(ASM, "CS_SWOOPHI")
    t, d = equ(ASM, "CS_SWOOPT"), equ(ASM, "CS_SWOOPD")
    check(lo + t * d == hi,
          "the swoop lands exactly on its far end: %d + %d x %d = %d, want %d"
          % (lo, t, d, lo + t * d, hi))
    print("  %s" % ("ok" if not bad else "FAILED: %d" % len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
