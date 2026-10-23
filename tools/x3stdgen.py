#!/usr/bin/env python3
"""x3stdgen: the STANDARD decoder's straight-line synthesis, made on the host (SPEC.md 121.4).

    python3 tools/x3stdgen.py --out build/x3sgen.inc
    python3 tools/x3stdgen.py --selfcheck

The N-point DCT-II of the synthesis (N = 32, 16, 8) and the 16-product window
are the decoder's two hottest stages, and their ORDER OF OPERATIONS is part of
the bit-exact definition (integer adds saturate, so they do not associate).
So their code is not transcribed by hand: this file TRACES tools/x3sim.py's
own dct2_i - the reference's recursion, run on symbolic values, with its
iadd32 / isub32 / imul32 / imul2_32 / s32 patched to record what they are
asked to do - and writes the operations it saw, in order, as straight-line
NASM macro calls. apps/xtmp3/x3std.asm defines the macros (the 8086 arithmetic
of each one) and %includes the result.

Every constant is addressed in the SHIPPED table (x3std.bin's ROT, SYNW and
SYNIDX, tools/x3tables.py): the generator finds each multiplier's word there,
so the code and the blob cannot disagree about a number.

--selfcheck interprets the emitted program in Python with the same int32 /
int16 semantics the macros implement and holds it bit-exact to x3sim.dct2_i,
to_v and IntSynth on random vectors (saturating ones included), and checks
that every multiplier the macros treat as unsigned is non-negative.

The emitted macros:
  X3SG_DCT32 / X3SG_DCT16 / X3SG_DCT8   one slot's DCT-II (XS_ADD, XS_SUB,
      XS_NEG, XS_MULC, and XS_ROT: two products by a table word |c| each,
      added or subtracted, rounded once) - inputs at
      [bx + 72 k] (subband k of the granule's 18-slot rows), temporaries in
      xs_t, each output X[k] rounded into V and stored in ring column k at
      [di + 64 k] and [di + 64 k + 32] (the doubled ring)
  X3SG_WIN1 / X3SG_WIN2 / X3SG_WIN4     one slot's window: N outputs, each
      XS_W0 then the non-zero taps XS_WT (disp from di, the coefficient's
      address) then XS_WOUT m (round, clip, store to xs_pcmt + 2 m)
"""
import argparse
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np        # noqa: E402
import x3sim as X         # noqa: E402
import x3tables as TB     # noqa: E402

I32 = (-(1 << 31), (1 << 31) - 1)
IN_STRIDE = 72            # bytes between subbands k and k+1 of one slot
COL_STRIDE = 64           # bytes a ring column (32 words: 16 slots, twice)

# --- the ROT table's words, where the code finds its multipliers -------------


def rot_words():
    """The ROT table as x3tables lays it out (blob order), word by word."""
    for name, data, _ in TB.table_blobs():
        if name == 'ROT':
            return list(np.frombuffer(data, '<i2'))
    raise SystemExit('x3stdgen: no ROT table')


def synw_words():
    for name, data, _ in TB.table_blobs():
        if name == 'SYNW':
            return list(np.frombuffer(data, '<i2'))
    raise SystemExit('x3stdgen: no SYNW table')


# --- THE TRACE ----------------------------------------------------------------

class Sym:
    __slots__ = ('ref',)

    def __init__(self, ref):
        self.ref = ref

    def __neg__(self):
        return Neg(self)


class Neg:
    __slots__ = ('of',)

    def __init__(self, of):
        self.of = of


def trace_dct(n):
    """x3sim.dct2_i on n symbolic inputs: ([(op, dst, args)], [output refs])."""
    ops = []

    def new(op, *args):
        d = ('t', len(ops))
        ops.append((op, d, args))
        return Sym(d)

    def iadd32(a, b, st):
        return new('add', a.ref, b.ref)

    def isub32(a, b, st):
        return new('sub', a.ref, b.ref)

    def imul32(a, c, st, sh):
        assert sh == 15 and c >= 0
        return new('mulc', a.ref, int(c))

    def imul2_32(a, c1, b, c2, st, sh=15):
        assert sh == 15
        return new('rot', a.ref, int(c1), b.ref, int(c2))

    def s32(x, st):
        assert isinstance(x, Neg), 's32 of something other than a negation'
        return new('neg', x.of.ref)

    saved = {k: getattr(X, k) for k in ('iadd32', 'isub32', 'imul32', 'imul2_32', 's32')}
    try:
        X.iadd32, X.isub32, X.imul32, X.imul2_32, X.s32 = iadd32, isub32, imul32, imul2_32, s32
        outs = X.dct2_i([Sym(('in', k)) for k in range(n)])
    finally:
        for k, v in saved.items():
            setattr(X, k, v)
    return ops, [o.ref for o in outs]


def allocate(ops, outs):
    """Temporaries into reusable dword slots by liveness: {ref: slot}."""
    last = {}
    for i, (op, d, args) in enumerate(ops):
        for a in args:
            if isinstance(a, tuple) and a[0] == 't':
                last[a] = i
    for o in outs:
        last[o] = len(ops)          # outputs live to the end
    free, slot, nslots = [], {}, 0
    for i, (op, d, args) in enumerate(ops):
        for a in args:              # operands that die here free their slots
            if isinstance(a, tuple) and a[0] == 't' and last[a] == i and a not in outs:
                free.append(slot[a])
        if free:
            slot[d] = free.pop()
        else:
            slot[d] = nslots
            nslots += 1
    return slot, nslots


# --- THE EMITTED PROGRAM -------------------------------------------------------

def emit_dct(n, rot):
    ops, outs = trace_dct(n)
    slot, nslots = allocate(ops, outs)
    index = {}
    for i, w in enumerate(rot):
        index.setdefault(int(w), i)

    def opnd(r):
        if r[0] == 'in':
            return 'bx+%d' % (r[1] * IN_STRIDE)
        return 'xs_t+%d' % (slot[r] * 4)

    def kaddr(c):
        if c not in index:
            raise SystemExit('x3stdgen: multiplier %d is not in ROT' % c)
        return 'xs_rot+%d' % (index[c] * 2)

    lines = ['%%macro X3SG_DCT%d 0' % n]
    prog = []
    for op, d, args in ops:
        if op in ('add', 'sub'):
            lines.append('    XS_%s %s, %s, %s' % (op.upper(), opnd(d), opnd(args[0]), opnd(args[1])))
        elif op == 'neg':
            lines.append('    XS_NEG %s, %s' % (opnd(d), opnd(args[0])))
        elif op == 'mulc':
            lines.append('    XS_MULC %s, %s, %s' % (opnd(d), opnd(args[0]), kaddr(args[1])))
        elif op == 'rot':
            a, c1, b, c2 = args       # sat32((a c1 + b c2 + 2^14) >> 15), each
            lines.append('    XS_ROT %s, %s, %s, %d, %s, %s, %d' % (   # by |c|, signed
                opnd(d), opnd(a), kaddr(abs(c1)), int(c1 < 0), opnd(b), kaddr(abs(c2)),
                int(c2 < 0)))
        prog.append((op, d, args))
    for k, o in enumerate(outs):
        lines.append('    XS_TOV %d, %s' % (k * COL_STRIDE, opnd(o)))
    lines.append('%endmacro')
    return lines, (ops, outs), nslots


def window_taps(div):
    """[(m, [(disp, coef word index)])] for one slot's outputs (x3sim
    IntSynth): term (age g, ring column c, SYNW word w)."""
    out = []
    for m in range(32 // div):
        j = div * m
        a, b = TB.STD.SYNIDX[j]
        taps = []
        for i in range(8):
            ce, cd = TB.STD.SYNW[j][i]
            for age, col, coef, w in ((2 * i, a // div, ce, j * 16 + 2 * i),
                                      (2 * i + 1, b // div, cd, j * 16 + 2 * i + 1)):
                if coef:
                    taps.append((col * COL_STRIDE + 32 - 2 * age, w, age, col, coef))
        out.append((m, taps))
    return out


def emit_window(div):
    lines = ['%%macro X3SG_WIN%d 0' % div]
    for m, taps in window_taps(div):
        lines.append('    XS_W0')
        for disp, w, *_ in taps:
            lines.append('    XS_WT %d, xs_synw+%d' % (disp, w * 2))
        lines.append('    XS_WOUT %d' % (m * 2))
    lines.append('%endmacro')
    return lines


def make_inc():
    rot = rot_words()
    out = ['; x3sgen.inc - made by tools/x3stdgen.py from tools/x3sim.py\'s dct2_i and',
           '; tools/x3tables.py; do not edit. The STANDARD decoder\'s synthesis, straight',
           '; line (SPEC.md 121.4): apps/xtmp3/x3std.asm defines every XS_ macro.']
    nmax = 0
    for n in (32, 16, 8):
        lines, _, ns = emit_dct(n, rot)
        nmax = max(nmax, ns)
        out += lines
    for div in (1, 2, 4):
        out += emit_window(div)
    out.insert(3, 'X3SG_TSLOTS equ %d' % nmax)
    return '\n'.join(out) + '\n'


# --- THE SELF-CHECK: the emitted program, interpreted -------------------------

def sat32(v):
    return max(I32[0], min(I32[1], v))


def run_dct(prog, x):
    """The program's arithmetic on a list of Python ints (the macros')."""
    ops, outs = prog
    val = {('in', k): int(v) for k, v in enumerate(x)}
    for op, d, args in ops:
        if op == 'add':
            val[d] = sat32(val[args[0]] + val[args[1]])
        elif op == 'sub':
            val[d] = sat32(val[args[0]] - val[args[1]])
        elif op == 'neg':
            val[d] = sat32(-val[args[0]])
        elif op == 'mulc':
            val[d] = sat32((val[args[0]] * args[1] + (1 << 14)) >> 15)
        elif op == 'rot':
            a, c1, b, c2 = args
            val[d] = sat32((val[a] * c1 + val[b] * c2 + (1 << 14)) >> 15)
    return [val[o] for o in outs]


def to_v(v):
    return max(-32768, min(32767, (v + (1 << 13)) >> 14))


def selfcheck():
    bad = []
    rng = random.Random(8088)
    rot = rot_words()
    for n in (32, 16, 8):
        _, prog, _ = emit_dct(n, rot)
        for trial in range(300):
            scale = rng.choice((1 << 12, 1 << 20, 1 << 28, 1 << 31))
            x = [rng.randrange(-scale, scale) for _ in range(n)]
            if trial % 50 == 0:
                x = [rng.choice((I32[0], I32[1])) for _ in range(n)]   # saturating
            want = [int(v[0]) for v in X.dct2_i([np.array([v], dtype=np.int64) for v in x])]
            got = run_dct(prog, x)
            if got != want:
                bad.append('DCT%d differs on trial %d' % (n, trial))
                break
            if [to_v(v) for v in got] != [int(X.to_v(np.array([v]))[0]) for v in want]:
                bad.append('to_v differs, DCT%d' % n)
                break
    # the window: the taps against IntSynth's own (age, index, coefficient)
    synw = synw_words()
    for div in (1, 2, 4):
        syn = X.IntSynth(div)
        for m, taps in window_taps(div):
            mine = sorted((age, col, coef) for _, w, age, col, coef in taps)
            ref = sorted((int(syn.age[m, t]), int(syn.idx[m, t]), int(syn.co[m, t]))
                         for t in range(16) if syn.co[m, t])
            if mine != ref:
                bad.append('window div %d output %d differs from IntSynth' % (div, m))
            for _, w, _, _, coef in taps:
                if synw[w] != coef:
                    bad.append('SYNW word %d is not the coefficient' % w)
    # multipliers the hand-written macros of x3std.asm treat as unsigned (a
    # 32 x 16 product's low word is a MUL): every table they read is
    # non-negative (the generated code takes |c| from ROT and a sign)
    S = TB.STD
    for name, vals in (('AA', list(S.AA_CS) + list(S.AA_CA)),
                       ('IMW', [v for W in S.IMW36 for k in 'w w9 a b c d'.split() for v in W[k]]
                        + [v for k in 'w w9 a b c d'.split() for v in S.IMW12[k]]),
                       ('IS', [v for p in S.IS1_Q + S.IS1_QMS for v in p]
                        + [v for t in (S.IS2_Q, S.IS2_QMS) for s in (0, 1) for v in t[s]])):
        if min(vals) < 0:
            bad.append('%s has a negative multiplier' % name)
    if make_inc() != make_inc():
        bad.append('two generations differ')
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', metavar='INC')
    ap.add_argument('--selfcheck', action='store_true')
    a = ap.parse_args()
    rc = 0
    if a.selfcheck:
        bad = selfcheck()
        for m in bad:
            print('x3stdgen: FAIL %s' % m)
        print('x3stdgen: selfcheck %s' % ('ok' if not bad else 'FAILED'))
        rc = 1 if bad else 0
    if a.out:
        with open(a.out, 'w') as f:
            f.write(make_inc())
    if not (a.selfcheck or a.out):
        ap.print_help()
    return rc


if __name__ == '__main__':
    sys.exit(main())
