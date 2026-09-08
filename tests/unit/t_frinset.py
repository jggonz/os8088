#!/usr/bin/env python3
"""`fr_inset` never claims a pixel the core would have escaped (SPEC.md 40.5).

    python3 tests/unit/t_frinset.py              # the fast tier's stride, 32
    python3 tests/unit/t_frinset.py --stride 2   # the soak row's
    python3 tests/unit/t_frinset.py --stride 1   # exhaustive; ~40 minutes

SPEC.md 40 names the class of optimisation this belongs to and the reason it
needs a gate before it ships: it "can silently corrupt half a frame", because
a wrong answer is not a crash - it is a black speck in the outer field, or a
missing filament, on a picture nobody has a reference for.  `fr_inset` skips
FR_CAP iterations for a point it claims is interior, so a claim that is wrong
by one lattice point paints one pixel the wrong colour and nothing anywhere
says so.

SO THE PROPERTY CHECKED HERE IS NOT "is this point in the Mandelbrot set".
The picture owes nothing to the mathematical set; it owes everything to what
`frac_iter` returns in Q4.12 with a cap of 48.  A claim is wrong exactly when
the core would have escaped, and that is what is swept:

  1. every lattice point `fr_inset` can claim, against the core;
  2. the two BOXES that make it cheap - a gate that rejects a point the
     algebraic test would have claimed is a silent loss of coverage, and one
     that admits a point outside the shape's true extent is an OVERFLOW, the
     failure the file header's whole signed-word argument is about;
  3. that every intermediate inside the gates fits a signed word.

tools/frref.py is the model, and it reads its constants out of
apps/fractal/fractal.asm - so widening a gate or cutting FR_INMARG in the
assembly moves this test rather than leaving it agreeing with itself.
"""
import argparse
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import frref                                                  # noqa: E402

K = frref.K
FAIL = []


def bad(msg):
    FAIL.append(msg)
    print("t_frinset: " + msg)


def claims_vs_core(stride):
    """(1) Every point the test claims is one the core calls interior."""
    claimed, wrong, offenders = frref.sweep(stride)
    if not claimed:
        bad("the sweep claimed NOTHING - the gates cannot all be right")
    for cx, cy in offenders:
        bad("fr_inset claims c=(%d,%d) = (%.6f,%.6f) but the core escapes it "
            "at %d" % (cx, cy, cx / 4096.0, cy / 4096.0,
                       frref.frac_iter(0, 0, cx, cy, 0)))
    if wrong:
        bad("%d of %d claimed points disagree with frac_iter" % (wrong, claimed))
    else:
        print("t_frinset: %d claimed points, stride %d, 0 disagreements"
              % (claimed, stride))
    return claimed


def boxes():
    """(2) The rejection boxes against the shapes' true extent.

    Both are closed forms, so the extent is arithmetic rather than a survey:
    the main cardioid c = e^it/2 - e^2it/4 reaches Re 0.375 at t = pi/3,
    Re -0.75 at t = pi and |Im| 3*sqrt(3)/8 at t = 2pi/3; the period-2 bulb
    is the disc of radius 1/4 about -1.  A gate INSIDE those loses claims
    silently; a gate outside them is what lets a square overflow."""
    one = K["FR_ONE"]
    want_sy = math.ceil(3 * math.sqrt(3) / 8 * one)            # 2661
    if K["FR_SYMAX"] < want_sy:
        bad("FR_SYMAX %d cuts the cardioid, whose |Im| reaches %d"
            % (K["FR_SYMAX"], want_sy))
    if K["FR_SYMAX"] > want_sy + 8:
        bad("FR_SYMAX %d is looser than the cardioid's %d - the gate is what "
            "bounds the squares" % (K["FR_SYMAX"], want_sy))
    want_hi = math.ceil(0.375 * one) - K["FR_QTR"]             # 512
    want_lo = math.floor(-0.75 * one) - K["FR_QTR"]            # -4096
    if K["FR_CDXHI"] < want_hi or K["FR_CDXLO"] > want_lo:
        bad("the cardioid's dx box [%d,%d] cuts its true [%d,%d]"
            % (K["FR_CDXLO"], K["FR_CDXHI"], want_lo, want_hi))
    if K["FR_CDXLO"] < -one or K["FR_CDXHI"] > one:
        bad("the cardioid's dx box [%d,%d] admits |dx| > 1.0, which is what "
            "keeps dx*dx inside a signed word"
            % (K["FR_CDXLO"], K["FR_CDXHI"]))
    if K["FR_BULBR"] != one // 4:
        bad("FR_BULBR %d is not the bulb's radius 0.25" % K["FR_BULBR"])
    if K["FR_BULBR2"] != frref.qsq(K["FR_BULBR"]):
        bad("FR_BULBR2 %d is not qsq(FR_BULBR) = %d"
            % (K["FR_BULBR2"], frref.qsq(K["FR_BULBR"])))
    print("t_frinset: boxes agree with the closed forms")


def ranges():
    """(3) Nothing inside the gates leaves a signed word.

    The corners are the worst case for every product here, so this is a
    bound rather than a sample."""
    lim = 32767
    ay, dx = K["FR_SYMAX"], max(abs(K["FR_CDXLO"]), abs(K["FR_CDXHI"]))
    y2, x2 = frref.qsq(ay), frref.qsq(dx)
    if y2 < 0 or x2 < 0 or x2 + y2 > lim:
        bad("q = dx^2 + cy^2 reaches %d, outside a signed word" % (x2 + y2))
    q = K["FR_ONE"] - 1                       # the gate lets nothing larger by
    worst = max(abs(q + K["FR_CDXLO"]), abs(q + K["FR_CDXHI"]))
    if worst > lim:
        bad("q + dx reaches %d, outside a signed word" % worst)
    prod = frref.qmul(q, worst)
    if abs(prod) > lim:
        bad("qmul(q, q+dx) reaches %d, outside a signed word" % prod)
    if frref.qsq(K["FR_BULBR"]) * 2 > lim:
        bad("the bulb's (cx+1)^2 + cy^2 leaves a signed word")
    print("t_frinset: every intermediate inside the gates fits a signed word "
          "(worst q = %d, worst product = %d)" % (x2 + y2, prod))


def flags_are_mandelbrot_only():
    """fr_inset returns on the first compare unless the flag word is zero,
    and exactly one of the five types has that.  If a sixth ever arrives
    with no flags and a different shape, this is what says so."""
    zero = [t["name"] for t in frref.types() if t["flg"] == 0]
    if zero != ["mandel"]:
        bad("types with a zero flag word are %r - fr_inset's `cmp byte "
            "[fr_flg], 0` gate now admits something it was never swept for"
            % (zero,))
    for t in frref.types():
        if t["flg"] and frref.inset(-2048, 0, t["flg"]):
            bad("fr_inset claims a point for %s, which it does not describe"
                % t["name"])
    print("t_frinset: the gate admits %s and nothing else" % zero[0])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stride", type=int, default=32,
                    help="lattice stride; 1 is exhaustive and takes minutes")
    a = ap.parse_args()
    boxes()
    ranges()
    flags_are_mandelbrot_only()
    claims_vs_core(a.stride)
    if FAIL:
        sys.exit("t_frinset: %d finding(s)" % len(FAIL))
    print("t_frinset: ok")


if __name__ == "__main__":
    main()
