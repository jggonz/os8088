#!/usr/bin/env python3
"""The reference model of apps/fractal's iteration core (SPEC.md 40).

    python3 tools/frref.py --frame            # where a frame's iterations go
    python3 tools/frref.py --sweep            # fr_inset vs the core, exhaustive
    python3 tools/frref.py --sweep --stride 8 # ...at a stride, for a budget

SPEC.md 40 has cited "a C model of this core" since the restore cache was
sized against it, and that model was never in the tree - so every number in
that section was unreproducible and the one optimisation the section warns
about (FT_SYM, "the one that can silently corrupt half a frame") had nothing
to be checked against.  This is that model, in the tree, and it is what
tests/unit/t_frinset.py drives.

IT READS ITS CONSTANTS OUT OF apps/fractal/fractal.asm rather than declaring
them, which is the whole point: a model with its own copy of FR_GUARD is a
model that agrees with the assembly until somebody edits one of them.  The
only things written down here are the SEMANTICS - the order of the escape
tests, and that qmul truncates toward zero - and those are what SPEC.md 40
pins as binding.

Q4.12 throughout: 1.0 = 4096, everything a signed 16-bit word, and the
32-bit product shifted right 12 by taking bits 12..27 of DX:AX exactly as
frac_iter's four shl/rcl pairs do.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "apps", "fractal", "fractal.asm")


# --- the constants, taken from the assembly ---------------------------------

def consts(path=SRC):
    """Every `NAME equ VALUE` in fractal.asm, as ints where the value is one."""
    out = {}
    for line in open(path):
        m = re.match(r"^(F[FRT]_[A-Z0-9_]+)\s+equ\s+(-?\w+)", line)
        if not m:
            continue
        v = m.group(2)
        try:                            # NASM writes 01h as readily as 0x01
            out[m.group(1)] = int(v[:-1], 16) if v[-1] in "hH" else int(v, 0)
        except ValueError:
            pass
    for need in ("FR_ONE", "FR_GUARD", "FR_FOUR", "FR_CAP", "FR_QTR",
                 "FR_SYMAX", "FR_CDXHI", "FR_CDXLO", "FR_BULBR", "FR_BULBR2",
                 "FR_INMARG", "FR_CLAMP", "FR_ZMAX", "FR_CYCK", "FF_ABS", "FF_NEG", "FF_JUL"):
        if need not in out:
            sys.exit("frref: %s defines no %s - the source moved and this "
                     "reader did not" % (os.path.relpath(path, ROOT), need))
    return out


K = consts()


def types(path=SRC):
    """The five rows of fr_types, as dicts.  Stride 16, eight words."""
    src = open(path).read()
    m = re.search(r"^fr_types:\s*$", src, re.M)
    if not m:
        sys.exit("frref: fractal.asm has no fr_types label")
    rows = []
    for line in src[m.end():].split("\n"):
        line = line.split(";")[0].strip()
        if not line:
            if rows:
                break
            continue
        if not line.startswith("dw "):
            break
        f = [t.strip() for t in line[3:].split(",")]
        if len(f) != 8:
            sys.exit("frref: fr_types row is %d fields, wanted 8" % len(f))
        rows.append(dict(name=f[0].replace("fr_s_", ""), flg=int(f[1], 0),
                         jcx=int(f[2], 0), jcy=int(f[3], 0),
                         cenx=int(f[4], 0), ceny=int(f[5], 0),
                         span=int(f[6], 0), sym=int(f[7], 0)))
    if len(rows) != 5:
        sys.exit("frref: read %d fr_types rows, wanted 5" % len(rows))
    return rows


# --- Q4.12 ------------------------------------------------------------------

def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def q12(p):
    """DX after frac_iter's four shl/rcl pairs: bits 12..27 of the product,
    with the toward-zero bias already applied by the caller for a signed one."""
    return s16((p >> 12) & 0xFFFF)


def qsq(a):
    """a*a >> 12.  Never negative, so no bias is needed - and frac_iter does
    not apply one, which is why this is a separate entry point."""
    return q12(a * a)


def qmul(a, b):
    """a*b >> 12, truncated TOWARD ZERO.  The +4095 on a negative product is
    what makes qmul(-a,b) == -qmul(a,b) exact, and SPEC.md 40 calls that
    identity load-bearing: it is what keeps the conjugate symmetry exact."""
    p = a * b
    if p < 0:
        p += K["FR_ONE"] - 1
    return q12(p)


# --- the core ---------------------------------------------------------------

def frac_iter(zx, zy, cx, cy, flg, count=None, cycle=True):
    """apps/fractal's frac_iter, instruction for instruction.

    Returns the escape index 0..FR_CAP-1, or FR_CAP for a point that never
    escaped.  The ORDER is binding (SPEC.md 40): both magnitude guards run
    before either square is formed.

    `cycle` is SPEC.md 40.7's check, and it is here with an OFF switch
    precisely so a test can assert the two agree: the map is a function of
    (zx, zy) alone, so a repeated state retraces forever and can never reach
    the escape test - it is already an FR_CAP point and stopping early is the
    same answer sooner.  The reference is z0 on entry and is replaced every
    FR_CYCK iterations, the refresh tested AFTER the countdown is decremented
    because that is the order the assembly runs them in."""
    cap, guard, four = K["FR_CAP"], K["FR_GUARD"], K["FR_FOUR"]
    di = cap
    hx, hy = zx, zy                             # the reference starts as z0
    mask = K["FR_CYCK"] - 1
    while True:
        if ((zx + guard) & 0xFFFF) >= guard * 2 + 1:
            return cap - di
        if ((zy + guard) & 0xFFFF) >= guard * 2 + 1:
            return cap - di
        x2, y2 = qsq(zx), qsq(zy)
        if s16(x2 + y2) >= four:
            return cap - di
        t = qmul(zx, zy)
        if flg & K["FF_ABS"] and t < 0:
            t = -t
        t = s16(t + t)
        if flg & K["FF_NEG"]:
            t = s16(-t)
        zx, zy = s16(x2 - y2 + cx), s16(t + cy)
        if count is not None:
            count[0] += 1
        if cycle and zx == hx and zy == hy:
            return cap                          # a repeat: it never escapes
        di -= 1
        if di == 0:
            return cap
        if cycle and not (di & mask):
            hx, hy = zx, zy


def inset(cx, cy, flg):
    """apps/fractal's fr_inset (SPEC.md 40.5): True = this c is PROVABLY one
    frac_iter returns FR_CAP for, established without iterating.

    Mandelbrot only - it is the one type whose flag word is zero.  In the
    assembly that gate is at fr_rowcalc's CALL SITE rather than inside
    fr_inset, so the other four types pay a compare instead of a compare
    wrapped in a near call; modelling the two together here is what makes
    inset() answer for a whole frame.  Every other gate below is the
    assembly's own, read out of it by consts()."""
    if flg != 0:
        return False
    ay = abs(cy)
    if ay > K["FR_SYMAX"]:                      # above BOTH shapes
        return False
    y2 = qsq(ay)
    dx = s16(cx - K["FR_QTR"])
    if K["FR_CDXLO"] <= dx <= K["FR_CDXHI"]:    # the main cardioid's box
        q = s16(qsq(dx) + y2)
        if q < K["FR_ONE"]:
            if qmul(q, s16(q + dx)) < s16((y2 >> 2) - K["FR_INMARG"]):
                return True
    if ay < K["FR_BULBR"]:                      # the period-2 bulb's box
        bx = s16(cx + K["FR_ONE"])
        if -K["FR_BULBR"] < bx < K["FR_BULBR"]:
            if s16(qsq(bx) + y2) < K["FR_BULBR2"] - K["FR_INMARG"]:
                return True
    return False


# --- the (pass, row) state machine, phased from the axis (SPEC.md 40.6) -----

def incv(pas):
    """fr_incv: the |d| increment of a pass - 4 for 0 and 1, 2 for pass 2."""
    return 2 if pas == 2 else 4


def stepv(pas, row, ch, rc):
    """fr_stepv, control flow for control flow.

    d = row - rc, and a pass walks d = 0, +k, -k, +2k, -2k ... so that -d is
    the row IMMEDIATELY after +d and fr_line still holds its twin.  rc = 0 is
    the order this package walked before the phase existed - not by
    coincidence but by construction, and t_frstepv.py checks it at every
    canvas height rather than taking the word for it."""
    cx = rc
    si = ch - 1 - rc
    if not si >= cx:                        # fr_stepv's `cmp si,cx / jae`
        si = cx                             # SI = max(rc, ch-1-rc)
    dx = incv(pas)
    d = row - cx
    state = "step"
    while True:
        if state == "step":
            if d > 0:
                d = -d                      # +d -> its twin
            elif d < 0:
                d = -d + dx                 # -d -> the next pair
            else:
                d = d + dx                  # d = 0 -> the first pair
            state = "chk"
        if state == "chk":
            if abs(d) > si:                 # past both edges: pass exhausted
                state = "nextpass"
            else:
                r = d + cx
                if not 0 <= r < ch:         # off ONE edge: skip, do not stop
                    state = "step"
                    continue
                return pas, r
        if state == "nextpass":
            pas += 1
            if pas >= 3:
                return pas, row             # frame complete
            dx = incv(pas)
            d = dx >> 1                     # 2 for pass 1, 1 for pass 2
            state = "chk"


def order(ch, rc):
    """Every (pass, row) a frame draws, in the order it draws them."""
    out = []
    pas, row = 0, rc
    while pas < 3:
        out.append((pas, row))
        pas, row = stepv(pas, row, ch, rc)
    return out


def band(pas, row, ch):
    """fr_band: the (top, bottom) canvas rows this row paints.

    The topmost pass-0 band reaches row 0 because pass 0 now opens at
    rc mod 4, so up to three rows would otherwise sit above every pass-0
    band and stay unpainted until pass 2."""
    bottom = min(row + (3, 1, 0)[pas], ch - 1)
    top = 0 if (pas == 0 and row < 4) else row
    return top, bottom


def axis_row(t, step, y0, ch):
    """fr_setup's [fr_mrc]: the canvas row cy = 0 falls on, or 0 for none."""
    if t["sym"] != 1 or y0 > 0:
        return 0
    rc, rem = divmod(-y0, step)
    if rem or rc >= ch:
        return 0
    return rc


# --- the view, as fr_setup derives it ---------------------------------------

def setup(t, z, cw, ch, cenx=None, ceny=None):
    """fr_setup's derived frame state: step, x0, y0, with fr_clamp applied."""
    cenx = t["cenx"] if cenx is None else cenx
    ceny = t["ceny"] if ceny is None else ceny
    step0 = (t["span"] & 0xFFFF) // cw or 1
    step = (step0 >> z) or 1
    hx, hy = s16((cw >> 1) * step), s16((ch >> 1) * step)
    lx, ly = K["FR_CLAMP"] - hx, K["FR_CLAMP"] - hy
    cenx = max(-lx, min(lx, cenx))
    ceny = max(-ly, min(ly, ceny))
    return step, s16(cenx - hx), s16(ceny - hy)


def frame(t, z, cw=320, ch=170, cenx=None, ceny=None, use_inset=False):
    """Render one frame.  Returns (iterations, interior px, inset hits)."""
    step, x0, y0 = setup(t, z, cw, ch, cenx, ceny)
    n = [0]
    interior = hits = 0
    for row in range(ch):
        pcy = s16(row * step + y0)
        pcx = x0
        for _ in range(cw):
            if use_inset and inset(pcx, pcy, t["flg"]):
                hits += 1
                interior += 1
            else:
                if t["flg"] & K["FF_JUL"]:
                    v = frac_iter(pcx, pcy, t["jcx"], t["jcy"], t["flg"], n)
                else:
                    v = frac_iter(0, 0, pcx, pcy, t["flg"], n)
                if v == K["FR_CAP"]:
                    interior += 1
            pcx = s16(pcx + step)
    return n[0], interior, hits


# --- the gate ---------------------------------------------------------------

def sweep(stride=1, verbose=False):
    """EVERY lattice point fr_inset can claim, against what the core returns.

    This is the harness SPEC.md 40 asked for before an optimisation of this
    class was allowed to ship, and the property it checks is the only one
    that matters: the picture owes nothing to the mathematical Mandelbrot
    set, it owes everything to what THIS core returns, so a claim is wrong
    exactly when frac_iter would have escaped.

    Returns (claimed, disagreements, first few offenders)."""
    lo_x = K["FR_CDXLO"] + K["FR_QTR"] - K["FR_ONE"] - 1   # the bulb's box too
    hi_x = K["FR_CDXHI"] + K["FR_QTR"] + 1
    lim_y = K["FR_SYMAX"] + 1
    claimed = wrong = 0
    bad = []
    for cx in range(lo_x, hi_x + 1, stride):
        for cy in range(-lim_y, lim_y + 1, stride):
            if not inset(cx, cy, 0):
                continue
            claimed += 1
            if frac_iter(0, 0, cx, cy, 0) != K["FR_CAP"]:
                wrong += 1
                if len(bad) < 8:
                    bad.append((cx, cy))
    if verbose:
        print("swept cx %d..%d, cy %d..%d, stride %d"
              % (lo_x, hi_x, -lim_y, lim_y, stride))
    return claimed, wrong, bad


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--frame", action="store_true",
                    help="iteration counts per type and zoom")
    ap.add_argument("--sweep", action="store_true",
                    help="fr_inset against the core over the lattice")
    ap.add_argument("--stride", type=int, default=1)
    ap.add_argument("--cw", type=int, default=320)
    ap.add_argument("--ch", type=int, default=170)
    a = ap.parse_args()
    if not (a.frame or a.sweep):
        ap.error("nothing asked for: --frame or --sweep")
    if a.frame:
        print("canvas %dx%d, 660 clocks an iteration on a 4.77MHz 8088\n"
              % (a.cw, a.ch))
        print("%-16s %2s %10s %10s %8s %8s" %
              ("type", "z", "iters", "iters+inset", "interior", "s -> s"))
        for t in types():
            for z in range(K["FR_ZMAX"] + 1):
                n0, ip, _ = frame(t, z, a.cw, a.ch)
                n1, _, hits = frame(t, z, a.cw, a.ch, use_inset=True)
                print("%-16s %2d %10d %10d %8d %5.1f -> %.1f" %
                      (t["name"], z, n0, n1, ip,
                       n0 * 660 / 4.77e6, n1 * 660 / 4.77e6))
    if a.sweep:
        claimed, wrong, bad = sweep(a.stride, verbose=True)
        print("fr_inset claimed %d lattice points, disagreements %d"
              % (claimed, wrong))
        for cx, cy in bad:
            print("  c=(%d,%d) = (%.6f,%.6f) escapes at %d"
                  % (cx, cy, cx / 4096.0, cy / 4096.0,
                     frac_iter(0, 0, cx, cy, 0)))
        return 1 if wrong else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
