#!/usr/bin/env python3
"""The cycle check returns exactly what the uncut core returns (SPEC.md 40.7).

    python3 tests/unit/t_frcycle.py                # the fast tier's stride
    python3 tests/unit/t_frcycle.py --stride 3     # the soak row's

SPEC.md 40.7's claim needs no sweep to be TRUE - `frac_iter` is a function of
(zx, zy) alone, so an orbit that returns to a state it has already been in
retraces it forever and can never reach the escape test, which makes it
already an FR_CAP point - but it needs one to stay true.  What could break it
is not the mathematics: it is the BOOKKEEPING.  A reference refreshed at the
wrong moment, seeded from something that is not a state of the orbit, or left
over from the previous pixel would compare against a state this orbit was
never in, and the answer would be FR_CAP for a point that escapes - a black
speck in the outer field, which is the failure SPEC.md 40 names.

So the property is the direct one: for every c on the lattice and for all five
types, the core WITH the check and the core WITHOUT it return the same escape
index.  Both are tools/frref.py's, which reads FR_CYCK and the rest out of
apps/fractal/fractal.asm, so a constant changed in the assembly moves this
test rather than leaving it agreeing with itself.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import frref                                                  # noqa: E402

K = frref.K
FAIL = []

# The measured saving per type over this sweep, less a third - a floor rather
# than a target, because the sweep is a lattice and the shipped views are not.
# Measured at the default stride: mandel 16%, rabbit 37%, ship 13%,
# tricorn 13%. The floors are those less a third - a floor, not a target.
#
# THE DENDRITE HAS NO FLOOR and that is a statement about the Dendrite: its
# interior is 21 pixels of 54,400 at its default view, so a lattice sweep
# barely samples one and the rate it reports is noise. A floor there would
# fail on the stride rather than on the detector.
FLOORS = {"mandel": 10.0, "dendrite": 0.0, "rabbit": 24.0,
          "ship": 8.0, "tricorn": 8.0}


def bad(msg):
    FAIL.append(msg)
    print("t_frcycle: " + msg)


def shape():
    """FR_CYCK is what makes the refresh test one AND rather than a divide."""
    k = K["FR_CYCK"]
    if k < 2 or (k & (k - 1)):
        bad("FR_CYCK is %d, which is not a power of two - `test di, FR_CYCK-1` "
            "is not a modulo any more" % k)
    if k > K["FR_CAP"]:
        bad("FR_CYCK %d exceeds the iteration cap %d, so the reference is "
            "never refreshed at all" % (k, K["FR_CAP"]))
    print("t_frcycle: FR_CYCK = %d, a power of two inside the cap" % k)


def agrees(stride):
    """The whole of it: with the check == without it, every type."""
    types = frref.types()
    lim = K["FR_CLAMP"]
    n = 0
    rates = []
    for t in types:
        wrong = 0
        first = None
        nit_off = nit_on = 0
        for cx in range(-lim, lim + 1, stride):
            for cy in range(0, lim + 1, stride):
                if t["flg"] & K["FF_JUL"]:
                    a = (t["jcx"], t["jcy"])
                    z = (cx, cy)
                else:
                    a = (cx, cy)
                    z = (0, 0)
                c0, c1 = [0], [0]
                off = frref.frac_iter(z[0], z[1], a[0], a[1], t["flg"],
                                      count=c0, cycle=False)
                on = frref.frac_iter(z[0], z[1], a[0], a[1], t["flg"],
                                     count=c1, cycle=True)
                nit_off += c0[0]
                nit_on += c1[0]
                n += 1
                if off != on:
                    wrong += 1
                    if first is None:
                        first = (cx, cy, off, on)
        if wrong:
            cx, cy, off, on = first
            bad("%s: %d points differ - e.g. (%d,%d) = (%.5f,%.5f) reads %d "
                "without the check and %d with it"
                % (t["name"], wrong, cx, cy, cx / 4096.0, cy / 4096.0,
                   off, on))
        # ...AND it has to still DETECT. Missing a cycle is safe and silent -
        # the picture is identical and the frame is merely slower - so the
        # agreement above passes a detector that has stopped detecting
        # entirely. These floors are the measured savings less a third.
        saved = 100.0 * (nit_off - nit_on) / max(nit_off, 1)
        floor = FLOORS[t["name"]]
        if saved < floor:
            bad("%s: the check saves %.1f%% of iterations, under the %.0f%% "
                "floor - it is still EXACT and has stopped paying for itself"
                % (t["name"], saved, floor))
        rates.append((t["name"], saved))
    print("t_frcycle: %d (point, type) pairs at stride %d, the check changes "
          "nothing" % (n, stride))
    print("t_frcycle: iterations saved  " +
          "  ".join("%s %.0f%%" % r for r in rates))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stride", type=int, default=149,
                    help="lattice stride; a prime, so the sample does not "
                         "land on a grid the arithmetic is periodic in. 149 "
                         "rather than something coarser because that is what "
                         "a FALSE detection needs to be found: comparing zx "
                         "alone - the obvious way to make the check cheaper - "
                         "reads FR_CAP for points that escape, and at stride "
                         "421 the sweep passes it")
    a = ap.parse_args()
    shape()
    agrees(a.stride)
    if FAIL:
        sys.exit("t_frcycle: %d finding(s)" % len(FAIL))
    print("t_frcycle: ok")


if __name__ == "__main__":
    main()
