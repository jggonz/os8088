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
import os
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
