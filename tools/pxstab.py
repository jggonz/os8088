#!/usr/bin/env python3
"""PIXELSTEIN 3D's fixed-point tables (SPEC.md 97.12), generated.

    python3 tools/pxstab.py [-o apps/pixelstein/pxtab.inc] [--check]

The engine's angle is 12 bits - 4,096 units to the turn, the quadrant in the
top two bits (SPEC.md 97.1) - and every trigonometric fact it needs is one of
three tables written here, so that the reference renderer (tools/pxssim.py),
the level tool's sight-line sweep (tools/pxslevel.py) and the package's own
cast read the SAME numbers. They are functions of nothing but this file, and
tests/unit/t_pxsgen.py regenerates them into a temporary file on every `make`
and holds the two to each other, the way docs/INDEX.md is held to
tools/os88index.py: the committed include cannot drift from the arithmetic
that produced it.

  px_sin   a QUARTER turn plus one - indices 0..1024 - in Q14 (16384 = 1.0).
           A full turn folds onto it in two tests: a is the angle,
           q = a >> 10, i = a & 1023; q0: +sin[i], q1: +sin[1024-i],
           q2: -sin[i], q3: -sin[1024-i]. cos(a) = sin(a + 1024). The 1,025th
           entry is what makes the reflection exact at 90 degrees; apps/skies'
           cssin.inc took the same 257th entry for the same reason.
  px_tan   the same quarter, Q8.8, CLAMPED AT 0x7FFF (127.996). It is the
           DDA's step - the y a ray moves per vertical grid line it crosses is
           |tan|, the x per horizontal line is |cot| = tan[1024 - i] - and it
           is read once a column into the patched immediates of the quadrant
           body (SPEC.md 97.2). A clamped entry means that walker crosses no
           line inside a 64-tile map and is PARKED (97.2.3): the clamp is the
           park rule's threshold, not an approximation of a step.
  px_fanN  the angle of screen column c relative to the heading, for every
           column count the Size and Detail rows offer - 48, 56, 64, 72, 80
           at four device pixels a column, and Mode X's 160 and 320 at two
           and one. A FLAT projection plane at PX_FOCAL device pixels, which
           is what keeps a straight wall straight: column c's centre sits at
           (c + 0.5) * k - W * k / 2 pixels from the axis and its angle is
           the arctangent of that over the focal length. The focal length is
           chosen so that 256 pixels span 60 degrees; a rung with fewer
           columns sees a NARROWER field at the same scale (CLEAR SKIES'
           sentence, SPEC.md 88.3.4: a smaller view is a smaller window on
           the same world rather than a zoom out), and Mode X's 320 sees
           71.6 degrees because its picture is the whole raster wide.

Nothing here is a matter of taste except the field, and that is one constant.
"""
import argparse
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(ROOT, "apps", "pixelstein", "pxtab.inc")

ANG = 4096                      # angle units a turn (SPEC.md 97.1)
QUARTER = ANG // 4
SIN_ONE = 16384                 # Q14
TAN_ONE = 256                   # Q8.8
TAN_CLAMP = 0x7FFF              # 127.996: past it the walker is parked
FOV_PX = 256                    # device pixels that span PX_FOV degrees...
FOV_DEG = 60.0                  # ...at the 64-column, 4-pixel rung
FOCAL = (FOV_PX / 2.0) / math.tan(math.radians(FOV_DEG / 2.0))
FANS = ((24, 8), (28, 8), (32, 8), (36, 8), (40, 8),
        (48, 4), (56, 4), (64, 4), (72, 4), (80, 4), (160, 2), (320, 1))
#      ^ Resolution: Low res - one ray per TWO shadow bytes, Size / 2 rays
#        across the same Size x 4 px view (docs/plans/PIXELSTEIN-PLAN.md 16),
#        eight device pixels a column at the same focal length: 32 rays at
#        Size 64, and 24/28/36/40 for the Size row's other four rungs (wave
#        2 - the 48 x 80 Low res fallback of SPEC.md 97.1 is the 24-ray fan)


def sin_table():
    out = []
    for i in range(QUARTER + 1):
        v = int(round(math.sin(i * 2.0 * math.pi / ANG) * SIN_ONE))
        out.append(min(v, SIN_ONE))
    assert out[0] == 0 and out[QUARTER] == SIN_ONE
    return out


def tan_table():
    out = []
    for i in range(QUARTER + 1):
        a = i * 2.0 * math.pi / ANG
        if i == QUARTER:
            v = TAN_CLAMP
        else:
            v = int(round(math.tan(a) * TAN_ONE))
        out.append(min(v, TAN_CLAMP))
    assert out[0] == 0 and out[QUARTER] == TAN_CLAMP
    return out


def fan_table(cols, k):
    """Column c's angle offset, in angle units, signed; left is negative."""
    out = []
    for c in range(cols):
        px = (c + 0.5) * k - cols * k / 2.0
        a = math.atan2(px, FOCAL)
        out.append(int(round(a * ANG / (2.0 * math.pi))))
    return out


def sin_q14(a):
    """The package's own fold of a 12-bit angle onto the quarter table."""
    t = sin_table()
    a &= ANG - 1
    q, i = a >> 10, a & (QUARTER - 1)
    if q == 0:
        return t[i]
    if q == 1:
        return t[QUARTER - i]
    if q == 2:
        return -t[i]
    return -t[QUARTER - i]


def cos_q14(a):
    return sin_q14(a + QUARTER)


def park_index():
    """The first quarter-index whose tangent is clamped (SPEC.md 97.2.3)."""
    t = tan_table()
    for i, v in enumerate(t):
        if v == TAN_CLAMP:
            return i
    return QUARTER


def rows(vals, per=12, fmt="%d"):
    out = []
    for i in range(0, len(vals), per):
        out.append("    dw " + ", ".join(fmt % v for v in vals[i:i + per]))
    return out


def generate():
    s, t = sin_table(), tan_table()
    park = park_index()
    L = []
    w = L.append
    w("; =============================================================================")
    w("; os8088 - apps/pixelstein/pxtab.inc")
    w(";")
    w("; GENERATED by tools/pxstab.py - do not edit by hand (SPEC.md 97.12).")
    w("; tests/unit/t_pxsgen.py regenerates it on every `make` and fails the fast")
    w("; tier if this file differs: the arithmetic lives in the tool, and this is")
    w("; only what it came to.")
    w(";")
    w("; The 12-bit angle (SPEC.md 97.1): %d units a turn, the quadrant in the" % ANG)
    w("; top two bits. sin(a) folds onto the quarter table below in two tests -")
    w("; q = a >> 10, i = a & 1023: q0 +sin[i], q1 +sin[1024-i], q2 -sin[i],")
    w("; q3 -sin[1024-i] - and cos(a) = sin(a + 1024). tan carries the same fold")
    w("; with the DDA's two steps read off it, |tan| = tan[i] and |cot| =")
    w("; tan[1024-i] in the even quadrants and the other way round in the odd ones.")
    w("; =============================================================================")
    w("")
    w("PX_ANG      equ %d              ; angle units a turn" % ANG)
    w("PX_QUARTER  equ %d              ; ...and a quarter of it" % QUARTER)
    w("PX_SINONE   equ %d             ; px_sin is Q14: this is 1.0" % SIN_ONE)
    w("PX_TANONE   equ %d               ; px_tan is Q8.8: this is 1.0" % TAN_ONE)
    w("PX_TANCLAMP equ 0x%04X            ; ...clamped here (127.996), and a" % TAN_CLAMP)
    w("                                  ; clamped step PARKS its walker (97.2.3)")
    w("PX_TANPARK  equ %d              ; the first quarter index that is clamped:" % park)
    w("                                  ; i >= this parks the vertical walker,")
    w("                                  ; i <= 1024 - this parks the horizontal")
    w("PX_FOCAL    equ %d               ; the projection plane, in device pixels" % int(round(FOCAL)))
    w("                                  ; (%.1f exactly): %d px span %g degrees" % (FOCAL, FOV_PX, FOV_DEG))
    w("")
    w("; --- px_sin: sin(i * 2pi / %d) * %d, i = 0..%d --------------------" % (ANG, SIN_ONE, QUARTER))
    w("px_sin:")
    L += rows(s)
    w("PX_SIN_N    equ %d" % len(s))
    w("")
    w("; --- px_tan: tan(i * 2pi / %d) * %d, clamped at PX_TANCLAMP ------------" % (ANG, TAN_ONE))
    w("px_tan:")
    L += rows(t)
    w("PX_TAN_N    equ %d" % len(t))
    w("")
    w("; --- px_fanN: column c's angle from the heading, signed, left negative -----")
    w("; A flat plane PX_FOCAL pixels away; column c of W at k pixels a column sits")
    w("; (c + 0.5) * k - W * k / 2 pixels from the axis.")
    for cols, k in FANS:
        f = fan_table(cols, k)
        span = f[-1] - f[0] + (f[1] - f[0])
        w("px_fan%d:                          ; %d columns at %d px: %.1f degrees"
          % (cols, cols, k, span * 360.0 / ANG))
        L += rows(f, fmt="%d")
    w("")
    w("PX_FAN_RUNGS equ %d" % len(FANS))
    w("px_fantab:                          ; the Size/Detail rows index this:")
    w("                                    ; word columns, word table, per rung")
    for cols, k in FANS:
        w("    dw %d, px_fan%d" % (cols, cols))
    w("")
    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if the committed include differs")
    a = ap.parse_args()
    text = generate()
    if a.check:
        have = open(a.out).read() if os.path.exists(a.out) else ""
        if have != text:
            sys.exit("pxstab: %s is not what tools/pxstab.py generates - run "
                     "`python3 tools/pxstab.py` and commit the result" % a.out)
        print("pxstab: %s matches" % a.out)
        return
    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    open(a.out, "w").write(text)
    print("pxstab: wrote %s (%d lines, park index %d)"
          % (a.out, text.count("\n"), park_index()))


if __name__ == "__main__":
    main()
