#!/usr/bin/env python3
"""The axis-phased pass order is still a permutation, and rc=0 is still today's
(SPEC.md 40.6).

    python3 tests/unit/t_frstepv.py

`fr_stepv` is the one copy of the arithmetic that says which row is which, and
SPEC.md 40.1 rests the WHOLE restore cache on it: rows are appended in the
order it produces them and nothing else records their heights, so a change
that drops a row, emits one twice, or reorders one silently replays a finished
frame at the wrong heights - and the fractal is the one window in the tree
with no reference to notice that against.

Four properties, each checked at every canvas height a window can be clamped
to and at every axis row inside it:

  1. rc = 0 is EXACTLY the order this package walked before the phase existed.
     That is what makes the Burning Ship, both Julias and every off-axis view
     byte-identical - by construction rather than by test.
  2. the order is a permutation of 0..ch-1: every row once, no row twice.
  3. a row's twin is the row IMMEDIATELY before it, and in the same pass -
     which is the whole point, because it means fr_line still holds it and no
     cache is read.
  4. pass 0's bands cover the canvas. Pass 0 opens at rc mod 4 now, so up to
     three rows sit above every band; fr_band's top rule is what puts them
     back, and this is what says so.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import frref                                                  # noqa: E402

FAIL = []


def bad(msg):
    FAIL.append(msg)
    print("t_frstepv: " + msg)


def before_the_phase(ch):
    """The order fr_stepv walked when the passes counted from row 0."""
    return ([(0, r) for r in range(0, ch, 4)] +
            [(1, r) for r in range(2, ch, 4)] +
            [(2, r) for r in range(1, ch, 2)])


# every ch a wm_fit clamp can produce, plus the shipped geometries
HEIGHTS = list(range(1, 60)) + [80, 118, 120, 127, 137, 170, 180, 199, 200]


def main():
    twins_seen = 0
    for ch in HEIGHTS:
        if frref.order(ch, 0) != before_the_phase(ch):
            bad("ch=%d: rc=0 is NOT the pre-phase order - four of the five "
                "types are no longer byte-identical" % ch)
        for rc in range(ch):
            o = frref.order(ch, rc)
            rows = [r for _, r in o]
            if sorted(rows) != list(range(ch)):
                dup = len(rows) - len(set(rows))
                bad("ch=%d rc=%d: %d rows, %d distinct, %d duplicated - the "
                    "cache's row naming is broken" % (ch, rc, len(rows),
                                                      len(set(rows)), dup))
                break
            for i, (pas, r) in enumerate(o):
                tw = 2 * rc - r
                if tw == r or not 0 <= tw < ch:
                    continue
                if i and o[i - 1][1] == tw:
                    twins_seen += 1
                    if o[i - 1][0] != pas:
                        bad("ch=%d rc=%d: row %d's twin %d is in pass %d, not "
                            "%d - the band heights would disagree"
                            % (ch, rc, r, tw, o[i - 1][0], pas))
            painted = set()
            for pas, r in o:
                if pas:
                    break
                top, bot = frref.band(pas, r, ch)
                painted |= set(range(top, bot + 1))
            if painted != set(range(ch)):
                miss = sorted(set(range(ch)) - painted)
                bad("ch=%d rc=%d: pass 0 leaves rows %r unpainted - a white "
                    "line for a quarter of the render" % (ch, rc, miss[:8]))
    # the shipped geometry, stated rather than merely checked
    ch, rc = 170, 85
    o = frref.order(ch, rc)
    free = sum(1 for i, (p, r) in enumerate(o)
               if i and 0 <= 2 * rc - r < ch and 2 * rc - r != r
               and o[i - 1][1] == 2 * rc - r)
    if free != 84:
        bad("the shipped 170-row canvas mirrors %d rows, not the 84 SPEC.md "
            "40.6 is written against" % free)
    print("t_frstepv: %d heights x every axis row, %d twins, 84 of 170 free "
          "at the shipped geometry" % (len(HEIGHTS), twins_seen))
    if FAIL:
        sys.exit("t_frstepv: %d finding(s)" % len(FAIL))
    print("t_frstepv: ok")


if __name__ == "__main__":
    main()
