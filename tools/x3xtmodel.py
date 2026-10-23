#!/usr/bin/env python3
"""x3xtmodel.py - the XT backend's arithmetic, modelled in numpy integers
(SPEC.md 121.4; docs/xtmp3/XT-STUDY.md). The study's subject.

Every stage of the decoder's reconstruction can run in one of three forms:

  'float'  the reference: exact arithmetic in float64;
  'int'    exact 8086 integer arithmetic: 16-bit data, `imul` 16x16->32 by a
           Q15/Q14 constant, a defined rounding, 16-bit wrap where the target
           wraps;
  'tab'    the XT form: the stage's multiplier input is turned into a CODE by
           a global, fixed code map (a table lookup on the target), and every
           product c * x is read from a product table
           T[row][code] = round(S * c * decode(code)).

A stage under test runs in its XT form while every other stage runs in
float, so each approximation is measured alone (the brief's section 14);
the profiles then combine them.

Integer formats at the stage boundaries (the XT model's; tools/x3sim.py's
STANDARD model owns its own, see XT-STUDY.md 'Plugging into x3sim'):

  requantised line  xr     float units where PCM full scale = 1.0;
                           Q_R = 13 when held as int16 (1.0 = 8192, +-4.0;
                           the stress set peaks at 1.25)
  subband sample    s      int16 Q_S = 11 (synthesis input, +-16.0)
  matrixing nodes          int16 Q11 too: Lee's DCT peaks at 11 FS on a
                           full-scale sweep (XT-STUDY.md 'Formats')
  V value           V      int16 Q_V = 11
  window output     y      int16 Q_O = 13 (+-4.0 full scale); every product
                           entry fits a word (the largest, 1.145 x 4 x 5294
                           LSB, is 24,245), partial sums wrap mod 2^16 like
                           the target's `add` and the final sum is in range;
                           PCM8 = sat((y + 32) >> 6) + 128
"""
import math

import numpy as np

import x3xtdsp as dsp

Q_R = 13
Q_S = 11
Q_V = 11
Q_O = 13


def wrap16(a):
    return ((np.asarray(a, dtype=np.int64) + 32768) & 0xFFFF) - 32768


def sat16(a):
    return np.clip(np.asarray(a, dtype=np.int64), -32768, 32767)


def rnd_shift(a, s):
    """Round half away from zero of a / 2^s (integers)."""
    a = np.asarray(a, dtype=np.int64)
    if s == 0:
        return a
    h = 1 << (s - 1)
    return np.where(a >= 0, (a + h) >> s, -((-a + h) >> s))


def to_int(x, q):
    return sat16(np.round(np.asarray(x) * (1 << q)).astype(np.int64))


# ============================================================ code maps
class CodeMap:
    """A global, fixed, sign-symmetric code map. `lv` are the non-negative
    decode values in integer units of 2^-q (lv[0] == 0), so code k decodes
    to sign(k) * lv[|k|] / 2^q. 2*len(lv)-1 codes are used out of 2^bits."""

    def __init__(self, name, lv, q, bits):
        self.name = name
        self.lv = np.asarray(lv, dtype=np.int64)
        assert self.lv[0] == 0 and np.all(np.diff(self.lv) > 0), name
        self.q = q
        self.bits = bits
        self.L = len(self.lv)
        mids = (self.lv[:-1] + self.lv[1:]) / 2.0
        self._mids = mids
        self._enc = {}

    @property
    def vmax(self):
        return self.lv[-1] / float(1 << self.q)

    def encode(self, x):
        """Nearest level (a requantiser's table built offline from exact
        values does this). x in float units. Saturates."""
        a = np.abs(np.asarray(x, dtype=np.float64)) * (1 << self.q)
        k = np.searchsorted(self._mids, a, side="left")
        return np.where(np.asarray(x) < 0, -k, k).astype(np.int64)

    def decode(self, code):
        code = np.asarray(code, dtype=np.int64)
        v = self.lv[np.abs(code)]
        return np.where(code < 0, -v, v)

    def decode_f(self, code):
        return self.decode(code) / float(1 << self.q)

    def enc_table(self, s, qin, mode="round"):
        """The target's encoder: CODE = ENC[index] for an int16 v in Q `qin`.
        mode 'round': index = (v + 2^(s-1)) >> s, ENC[b] = nearest level to
        b * 2^s (one `add` more on the target; zero maps to code 0).
        mode 'floor': index = v >> s, ENC[b] = nearest level to the bin's
        centre - no add, but the bin holding 0 is [0, 2^s) and its centre is
        not 0, so silence decodes to a DC offset (XT-STUDY.md 'Encoder').
        mode 'tz': index = v >> s, ENC[b] = the level nearest the bin edge
        closer to zero: silence is 0, every magnitude shrinks (a gain loss).
        Returns (table, lookup)."""
        key = (s, qin, mode)
        if key not in self._enc:
            nb = 1 << (16 - s)
            b = np.arange(nb, dtype=np.int64) - (nb >> 1)
            if s == 0 or mode == "round":
                at = b * (1 << s)
            elif mode == "floor":
                at = b * (1 << s) + ((1 << s) >> 1)
            else:
                at = np.where(b >= 0, b * (1 << s), (b + 1) * (1 << s))
            self._enc[key] = self.encode(at / float(1 << qin))
        tab = self._enc[key]
        if mode == "round" and s > 0:
            def look(v):
                v = np.asarray(v, dtype=np.int64)
                return tab[((v + (1 << (s - 1))) >> s).clip(-(1 << (15 - s)), (1 << (15 - s)) - 1) + (1 << (15 - s))]
        else:
            def look(v):
                v = np.asarray(v, dtype=np.int64)
                return tab[(v >> s) + (1 << (15 - s))]
        return tab, look

    def table_bytes(self, entry=2):
        return (1 << self.bits) * entry

    def steps(self):
        return np.diff(self.lv)


def map_uniform(bits, vmax, q):
    L = (1 << (bits - 1))
    step = max(1, int(round(vmax * (1 << q) / (L - 1))))
    return CodeMap("lin%d" % bits, np.arange(L) * step, q, bits)


def map_seg(bits, seglens, s0, q, name=None):
    """Piecewise linear with power-of-two steps (G.711's shape): segment i
    has seglens[i] levels at step 2^(s0+i). Expands at OPEN with additions
    only (x3xttab.py)."""
    lv = [0]
    for i, n in enumerate(seglens):
        st = 1 << (s0 + i)
        for _ in range(n):
            lv.append(lv[-1] + st)
    L = 1 << (bits - 1)
    assert len(lv) <= L, (len(lv), L)
    return CodeMap(name or "seg%d" % bits, lv, q, bits)


def map_mu(bits, vmax, q, mu=255.0):
    L = 1 << (bits - 1)
    k = np.arange(L) / (L - 1)
    lv = vmax * ((1 + mu) ** k - 1) / mu * (1 << q)
    lv = np.round(lv).astype(np.int64)
    lv = np.maximum.accumulate(lv)
    for i in range(1, L):
        if lv[i] <= lv[i - 1]:
            lv[i] = lv[i - 1] + 1
    return CodeMap("mu%d" % bits, lv, q, bits)


def map_lloyd(bits, data, weights, q, iters=60, vmax=None):
    """Error-optimised levels (weighted Lloyd-Max on |x|), integer-rounded.
    The weights make each training excerpt count by its relative error, so
    quiet material is not drowned by loud (XT-STUDY.md)."""
    L = 1 << (bits - 1)
    a = np.abs(np.asarray(data, dtype=np.float64))
    w = np.asarray(weights, dtype=np.float64)
    top = vmax if vmax is not None else np.quantile(a, 0.99999)
    # start from a mu-law spread
    lv = top * ((256.0) ** (np.arange(L) / (L - 1)) - 1) / 255.0
    for _ in range(iters):
        mids = (lv[:-1] + lv[1:]) / 2
        k = np.searchsorted(mids, a)
        sw = np.bincount(k, weights=w, minlength=L)
        sx = np.bincount(k, weights=w * a, minlength=L)
        new = np.where(sw > 0, sx / np.maximum(sw, 1e-300), lv)
        new[0] = 0.0
        new = np.maximum.accumulate(new)
        lv = new
    li = np.round(lv * (1 << q)).astype(np.int64)
    for i in range(1, L):
        if li[i] <= li[i - 1]:
            li[i] = li[i - 1] + 1
    return CodeMap("lloyd%d" % bits, li, q, bits)


def map_cost(cm, data, weights):
    """Weighted relative quantisation error of a map on training data (the
    objective the seg search minimises)."""
    a = np.asarray(data, dtype=np.float64)
    e = a - cm.decode_f(cm.encode(a))
    return float(np.sum(weights * e * e))


def map_seg_opt(bits, data, weights, q, vmax):
    """Search the power-of-two segment shapes (first step 2^s0, segment
    lengths) for the least weighted error, keeping the top level at or
    above vmax. A small exhaustive search over doubling patterns."""
    L = (1 << (bits - 1)) - 1
    best = None
    unit = 1 << q
    for s0 in range(0, 12):
        for nseg in range(2, 13):
            # equal segments, then bias toward the small end or the large
            for tilt in (-2, -1, 0, 1, 2):
                base = [L // nseg] * nseg
                rem = L - sum(base)
                for i in range(rem):
                    base[i if tilt >= 0 else nseg - 1 - i] += 1
                if tilt:
                    for i in range(nseg // 2):
                        mv = min(abs(tilt) * 2, base[-1 - i] - 1 if tilt > 0 else base[i] - 1)
                        if tilt > 0:
                            base[i] += mv
                            base[-1 - i] -= mv
                        else:
                            base[i] -= mv
                            base[-1 - i] += mv
                top = 0
                for i, n in enumerate(base):
                    top += n * (1 << (s0 + i))
                if top < vmax * unit or top > 4 * vmax * unit or top >= (1 << 15):
                    continue
                cm = map_seg(bits, base, s0, q, name="optseg%d" % bits)
                c = map_cost(cm, data, weights)
                if best is None or c < best[0]:
                    best = (c, base, s0)
    c, base, s0 = best
    cm = map_seg(bits, base, s0, q, name="optseg%d" % bits)
    cm.seg = (tuple(base), s0)
    return cm


def map_g711(bits, q, s0, vmax_hint=None):
    """mu-law's exact segment shape: 8 segments of 16 at doubling steps."""
    L = (1 << (bits - 1)) - 1
    n = L // 8
    segs = [n] * 8
    segs[0] += L - 8 * n
    cm = map_seg(bits, segs, s0, q, name="g711_%d" % bits)
    cm.seg = (tuple(segs), s0)
    return cm


# ======================================================= product tables
def ptable(cm, c, qin, qout, F=15):
    """T[code] = round(c * 2^qout * decode(code) / 2^qin), via the generator's
    exact integer form: c_fixed = round(|c| * 2^(qout - qin + F)), entry =
    round_half_away(c_fixed * lv / 2^F), sign of code (and of c applied by
    the kernel's add/sub). Returns int64 array indexed by code + L - 1, and
    the integer c_fixed."""
    cf = int(round(abs(c) * 2.0 ** (qout - cm.q + F)))
    pos = rnd_shift(cf * cm.lv, F)            # codes 0..L-1
    full = np.concatenate([-pos[:0:-1], pos])
    return full, cf


class TableBank:
    """Product rows for a set of coefficients sharing one code map."""

    def __init__(self, cm, coefs, qout, F=15):
        self.cm = cm
        self.coefs = np.asarray(coefs, dtype=np.float64)
        self.rows = []
        self.cfix = []
        for c in self.coefs:
            r, cf = ptable(cm, c, cm.q, qout, F)
            self.rows.append(r)
            self.cfix.append(cf)
        self.rows = np.array(self.rows)        # (nrows, 2L-1)
        self.off = cm.L - 1

    def look(self, row, code):
        return self.rows[row, code + self.off]


# =================================================== the synthesis window
def unique_rows(coef, eps=0.0):
    """Distinct |c| among the tap coefficients (exact equality, or within eps
    when clustering); returns (values, index map same shape as coef)."""
    a = np.abs(coef).ravel()
    vals = np.unique(a)
    vals = vals[vals > 0]
    idx = np.searchsorted(vals, np.abs(coef))
    idx = np.where(np.abs(coef) > 0, idx, -1)
    return vals, idx


def palette(coef, K, weight=None):
    """An error-aware nonuniform coefficient palette: K representatives of
    |c| minimising sum w * (c - rep)^2 with w = the tap's importance (the
    expected energy of its input), by Lloyd iterations seeded at quantiles
    of log|c| so small coefficients keep representatives of their own."""
    a = np.abs(coef).ravel()
    nz = a > 0
    v = a[nz]
    w = np.ones_like(v) if weight is None else np.asarray(weight).ravel()[nz]
    lg = np.log(v)
    reps = np.exp(np.quantile(lg, (np.arange(K) + 0.5) / K))
    for _ in range(100):
        # assign in the log domain (relative error), weight by importance
        d = np.abs(np.log(v)[:, None] - np.log(reps)[None, :])
        k = np.argmin(d, axis=1)
        for r in range(K):
            m = k == r
            if m.any():
                reps[r] = np.sum(w[m] * v[m]) / np.sum(w[m])
    d = np.abs(np.log(v)[:, None] - np.log(reps)[None, :])
    k = np.argmin(d, axis=1)
    out = np.zeros_like(a)
    out[nz] = reps[k]
    return (np.sign(coef.ravel()) * out).reshape(coef.shape), np.sort(reps)


def window_int(U_int, coef):
    """'int' window: 16-bit V (Q_V) times a Q15 coefficient by `imul`, the
    32-bit sum of the taps, then rounded to Q_O. D's largest |c| is 1.145,
    so the coefficient is held Q14 (+-2.0)."""
    cq = np.round(coef * (1 << 14)).astype(np.int64)
    acc = np.einsum("tji,ji->tj", U_int.astype(np.int64), cq)
    return wrap16(rnd_shift(acc, 14 + Q_V - Q_O))


def window_tab(U_code, rowidx, rowsign, bank):
    """'tab' window: sum of +-T[row][code] in 16 bits, wrapping."""
    T = bank.rows
    off = bank.off
    nr, ncodes = T.shape
    tj = U_code.shape
    acc = np.zeros(tj[:2], dtype=np.int64)
    for i in range(tj[2]):
        r = rowidx[:, i]
        live = r >= 0
        if not live.any():
            continue
        vals = T[np.where(live, r, 0)[None, :], U_code[:, :, i] + off]
        acc += np.where(live[None, :], vals * rowsign[None, :, i], 0)
    return wrap16(acc)


# ====================================================== fast DCT (matrixing)
class DctPlan:
    """DCT-II of size N, X[n] = sum_k x[k] cos(pi n (2k+1) / 2N), by Byeong
    Gi Lee's recursion (1984):

      even half: X[2m]   = DCT_{N/2}(x[k] + x[N-1-k])[m]
      odd half:  Y       = DCT_{N/2}((x[k] - x[N-1-k]) / (2cos(phi_k)))
                 X[2m+1] = Y[m] + Y[m+1]   (Y[N/2] = 0)
                 phi_k = (2k+1) pi / 2N

    M(32) = 80 multiplies by 31 distinct constants 1/(2cos phi) in
    [0.5, 10.19]. Chosen over the 2cos(phi) form, whose odd outputs come out
    of an alternating running sum that accumulates rounding: at Q11 the
    2cos form overflowed on a full-scale sweep and lost 1-6 dB elsewhere
    (XT-STUDY.md 'Matrixing'). A multiply is a node with an input VALUE
    (which the 'tab' form codes) and a constant; `depth` 0 is the outermost
    split."""

    def __init__(self, N):
        self.N = N
        self.consts = []
        self.depths = []
        self._count(N, 0)

    def _count(self, N, dep):
        if N == 1:
            return
        self._count(N // 2, dep + 1)
        for k in range(N // 2):
            self.consts.append(0.5 / math.cos((2 * k + 1) * math.pi / (2 * N)))
            self.depths.append(dep)
        self._count(N // 2, dep + 1)

    def run(self, x, mul, halve=None):
        """x: (T, N) array. mul(values, const, node index) -> products."""
        self._node = 0
        return self._run(x, mul)

    def _run(self, x, mul):
        N = x.shape[1]
        if N == 1:
            return x.copy()
        a = x[:, :N // 2]
        b = x[:, ::-1][:, :N // 2]
        ev = self._run(a + b, mul)
        d = a - b
        cols = []
        for k in range(N // 2):
            c = 0.5 / math.cos((2 * k + 1) * math.pi / (2 * N))
            cols.append(mul(d[:, k], c, self._node))
            self._node += 1
        Y = self._run(np.stack(cols, axis=1), mul)
        out = np.empty((x.shape[0], N), dtype=Y.dtype)
        out[:, 0::2] = ev
        Z = Y.copy()
        Z[:, :-1] += Y[:, 1:]
        out[:, 1::2] = Z
        return out


def dct_to_V(X):
    """V[0..63] from the 32-point DCT-II of the slot's subband samples:
    V[i] = X[16+i] (i < 16), V[16] = 0, V[32-i] = -V[i], V[48+i] = -X[i],
    V[96-i] = V[i] (i in 49..63)."""
    T = X.shape[0]
    V = np.zeros((T, 64), dtype=X.dtype)
    V[:, 0:16] = X[:, 16:32]
    V[:, 16] = 0
    for i in range(1, 16):
        V[:, 32 - i] = -V[:, i]
    V[:, 32] = -V[:, 0]
    for i in range(16):
        V[:, 48 + i] = -X[:, i]
    for i in range(49, 64):
        V[:, 96 - i] = V[:, i]
    return V


def dct_float(S):
    plan = DctPlan(32)
    return plan.run(S, lambda v, c, n: v * c)


def dct_int(S_int, cq=11):
    """'int' matrixing: 16-bit data at Q_S, each multiply `imul` by
    round(c * 2^cq) (c <= 10.19, so Q11 fits a word) and rounded back (add
    2^(cq-1) before the shift - two instructions the target pays). Sums wrap
    at 16 bits like the target's adds."""
    plan = DctPlan(32)

    def mul(v, c, n):
        return wrap16(rnd_shift(v.astype(np.int64) * int(round(c * (1 << cq))), cq))

    return wrap16(plan.run(S_int.astype(np.int64), mul))


def csd(c, terms, lo=-14, hi=4):
    """The best approximation of c by at most `terms` signed powers of two
    2^e, lo <= e <= hi (exhaustive greedy on the residual, which is optimal
    for canonical signed digits to within one digit)."""
    best = None
    def rec(res, k, acc):
        nonlocal best
        err = abs(res)
        if best is None or err < best[0]:
            best = (err, list(acc))
        if k == 0 or err == 0:
            return
        e = int(math.floor(math.log2(abs(res)))) if res else lo
        for ee in (e, e + 1):
            if lo <= ee <= hi:
                sgn = 1 if res > 0 else -1
                acc.append((sgn, ee))
                rec(res - sgn * 2.0 ** ee, k - 1, acc)
                acc.pop()
    rec(c, terms, [])
    return best[1]


def sa_mul(v, digits):
    """x * c by shifts and adds the way the target does it: left shifts
    exact, right shifts arithmetic (floor), 16-bit wrap."""
    v = np.asarray(v, dtype=np.int64)
    acc = np.zeros_like(v)
    for sgn, e in digits:
        t = (v << e) if e >= 0 else (v >> (-e))
        acc = acc + sgn * t
    return wrap16(acc)


def dct_sa(S_int, terms):
    """'sa' matrixing: every Lee multiply a shift-and-add sequence of at
    most `terms` signed powers of two."""
    plan = DctPlan(32)
    cache = {}

    def mul(v, c, n):
        if c not in cache:
            cache[c] = csd(c, terms)
        return sa_mul(wrap16(v), cache[c])
    return wrap16(plan.run(S_int.astype(np.int64), mul))


def split_rows(c):
    """BYTE-SPLIT product tables for a constant c: x * c = H[x >> 8] +
    L[x & 255] for an int16 x, with c held as c16 = round(c * 2^16):
    H[h] = round(c16 * 256 h / 2^16) over the signed high byte, L[l] =
    round(c16 * l / 2^16) over the unsigned low byte, rounding half away
    from zero. H is stored mod 2^16: x = 256h + l with l >= 0, so an
    in-range product can need an H beyond a word, and the word sum is
    exact mod 2^16 whenever |x c| < 2^15 (x3xttab.py). Two lookups, no code map,
    error under one LSB. 1 KB a constant as four 256-byte planes (each
    table's low and high bytes apart, so the byte is the index)."""
    c16 = int(round(abs(c) * 65536))
    h = np.arange(-128, 128, dtype=np.int64)
    l_ = np.arange(256, dtype=np.int64)
    H = wrap16(rnd_shift(c16 * h * 256, 16))
    L = rnd_shift(c16 * l_, 16)
    if c < 0:
        H, L = -H, -L
    return H, L


def split_mul(v, rows):
    H, L = rows
    v = wrap16(v)
    return wrap16(H[(v >> 8) + 128] + L[v & 255])


def dct_split(S_int):
    plan = DctPlan(32)
    cache = {}

    def mul(v, c, n):
        if c not in cache:
            cache[c] = split_rows(c)
        return split_mul(v, cache[c])
    return wrap16(plan.run(S_int.astype(np.int64), mul))


def dct_tab(S_int, cm, s_enc, cq_out=Q_V):
    """'tab' matrixing: every multiply input is coded by ENC[v >> s_enc] and
    the product read from T[const][code] (scale 1, Q unchanged)."""
    plan = DctPlan(32)
    _, look = cm.enc_table(s_enc, Q_S)
    rows = {}

    def mul(v, c, n):
        if c not in rows:
            rows[c] = ptable(cm, c, Q_S, Q_S)[0]
        code = look(wrap16(v))
        return wrap16(rows[c][code + cm.L - 1])

    return wrap16(plan.run(S_int.astype(np.int64), mul)), len(plan.consts)


# ================================================================= IMDCT
# The 36-point IMDCT is an 18-point DCT-IV z[n] = sum_k X[k] cos(pi(2n+1)
# (2k+1)/72) folded into 36 outputs. Its 324 matrix entries take only 18
# magnitudes cos(pi m / 72), m odd; the direct 'tab' form reads one row per
# (n, k) with a static sign, which is what the unrolled target does for each
# nonzero line.
_n18 = np.arange(18)
DCT4_18 = np.cos(np.pi * (2 * _n18[:, None] + 1) * (2 * _n18[None, :] + 1) / 72)


def imdct_fold36(z):
    """y[0..35] of the long IMDCT from z = DCT-IV_18(X): y = COS36 X."""
    # COS36[i,k] = cos(pi/72 (2i+19)(2k+1)); for i in 0..8: (2i+19) = 2(i+9)+1
    # -> z[i+9]; i in 9..26: 2i+19 = 144 - (2(26-i)+1) ... -> -z[26-i];
    # i in 27..35: 2i+19 = 144 + 2(i-27)+1 -> -z[i-27]
    y = np.empty(z.shape[:-1] + (36,), dtype=z.dtype)
    y[..., 0:9] = z[..., 9:18]
    for i in range(9, 27):
        y[..., i] = -z[..., 26 - i]
    for i in range(27, 36):
        y[..., i] = -z[..., i - 27]
    return y


_n6 = np.arange(6)
DCT4_6 = np.cos(np.pi * (2 * _n6[:, None] + 1) * (2 * _n6[None, :] + 1) / 24)


def imdct_fold12(z):
    y = np.empty(z.shape[:-1] + (12,), dtype=z.dtype)
    y[..., 0:3] = z[..., 3:6]
    for i in range(3, 9):
        y[..., i] = -z[..., 8 - i]
    for i in range(9, 12):
        y[..., i] = -z[..., i - 9]
    return y


# ======================================================= the decode chain
COS_ROWS = np.cos(np.pi * np.arange(1, 36, 2) / 72.0)     # 18 magnitudes


def _cos_row_sign(mat):
    """Map each entry of a DCT-IV matrix to (row of COS_ROWS, sign)."""
    a = np.abs(mat)
    r = np.argmin(np.abs(a[..., None] - COS_ROWS[None, None, :]), axis=-1)
    assert np.allclose(COS_ROWS[r], a, atol=1e-12)
    return r, np.sign(mat).astype(np.int64)


R18, SG18 = _cos_row_sign(DCT4_18)
R6, SG6 = _cos_row_sign(DCT4_6)
SPLIT18 = [split_rows(c) for c in COS_ROWS]


class Stats:
    def __init__(self):
        self.wraps = {}
        self.sats = {}

    def wrap(self, stage, exact, wrapped):
        n = int(np.sum(np.asarray(exact) != np.asarray(wrapped)))
        self.wraps[stage] = self.wraps.get(stage, 0) + n

    def sat(self, stage, n):
        self.sats[stage] = self.sats.get(stage, 0) + int(n)


def _toint_count(x, q, stats, stage):
    v = np.round(np.asarray(x) * (1 << q)).astype(np.int64)
    n = np.sum((v > 32767) | (v < -32768))
    if n:
        stats.sat(stage, n)
    return sat16(v)


def stage_antialias(xr, bt, form, cfg, stats):
    """xr float (G,32,18) -> float (form float/skip) or int16 Q_R."""
    if form == "skip":
        return xr
    if form == "float":
        return dsp.alias_reduce_dec(xr, bt)
    G = xr.shape[0]
    if form in ("int", "split"):
        X = _toint_count(xr, Q_R, stats, "aa_in")
        cs = np.round(dsp.CS * 32768).astype(np.int64)
        ca = np.round(dsp.CA * 32768).astype(np.int64)
        if form == "split":
            # the generator's constants: round(c * 2^16), x3xttab.AA_C16
            srows = {int(c): split_rows(f) for c, f in zip(list(cs) + list(ca),
                                                         list(dsp.CS) + list(dsp.CA))}

            def mul(v, c):
                return np.array([split_mul(v[i], srows[int(c[i])]) for i in range(len(v))])
        else:
            def mul(v, c):
                return rnd_shift(v * c, 15)
    else:
        cm = cfg["map_aa"]
        code = cm.encode(xr)            # the requantiser emits codes
        rows_cs = [ptable(cm, c, Q_R, Q_R)[0] for c in dsp.CS]
        rows_ca = [ptable(cm, c, Q_R, Q_R)[0] for c in dsp.CA]
        X = code
    out = np.zeros(xr.shape, dtype=np.int64)
    for g in range(G):
        if form in ("int", "split"):
            flat = X[g].reshape(-1).copy()
        else:
            # untouched lines pass as their decoded code value at Q_R
            flat = rnd_shift(cm.decode(X[g].reshape(-1)) << max(0, Q_R - cm.q),
                             max(0, cm.q - Q_R))
        if bt[g] != 2:
            for sb in range(1, 32):
                lo = 18 * sb - 1 - np.arange(8)
                hi = 18 * sb + np.arange(8)
                if form in ("int", "split"):
                    a, b = flat[lo].copy(), flat[hi].copy()
                    flat[lo] = wrap16(mul(a, cs) - mul(b, ca))
                    flat[hi] = wrap16(mul(b, cs) + mul(a, ca))
                else:
                    ca_ = X[g].reshape(-1)[lo]
                    cb_ = X[g].reshape(-1)[hi]
                    off = cm.L - 1
                    # rows hold |c| products; ca < 0, so the kernel's add and
                    # sub are swapped against the formula's signs
                    lo_v = np.array([rows_cs[k][ca_[k] + off] + rows_ca[k][cb_[k] + off] for k in range(8)])
                    hi_v = np.array([rows_cs[k][cb_[k] + off] - rows_ca[k][ca_[k] + off] for k in range(8)])
                    flat[lo] = wrap16(lo_v)
                    flat[hi] = wrap16(hi_v)
        out[g] = flat.reshape(32, 18)
    return out


def stage_imdct(xa, bt, form, cfg, stats, prune=32):
    """xa (G,32,18) float or int16 Q_R -> windowed 36-sample outputs y
    (G,32,36) as float or int16 Q_S (pre-overlap)."""
    G = xa.shape[0]
    fl = np.issubdtype(np.asarray(xa).dtype, np.floating)
    if form == "float":
        xf = xa if fl else xa / float(1 << Q_R)
        y = np.zeros((G, 32, 36))
        for g in range(G):
            y[g] = dsp.imdct_granule(xf[g], bt[g])
        y[:, prune:, :] = 0
        return y
    X = _toint_count(xa, Q_R, stats, "imdct_in") if fl else xa.astype(np.int64)
    wform = cfg.get("imdctwin", "int")
    y = np.zeros((G, 32, 36), dtype=np.int64)
    if form == "tab":
        cm = cfg["map_im"]
        _, look = cm.enc_table(cfg.get("s_im", 0), Q_R)
        rows = np.array([ptable(cm, c, Q_R, Q_R)[0] for c in COS_ROWS])
        off = cm.L - 1
    for g in range(G):
        Xg = X[g][:prune]
        if bt[g] == 2:
            parts = []
            for w in range(3):
                xs = Xg[:, 6 * w:6 * w + 6]
                if form == "int":
                    cq = np.round(DCT4_6 * 32768).astype(np.int64)
                    z = rnd_shift(xs @ cq.T, 15)
                elif form == "split":
                    z = np.zeros((xs.shape[0], 6), dtype=np.int64)
                    for k in range(6):
                        for r in range(6):
                            z[:, r] += SG6[r, k] * split_mul(xs[:, k], SPLIT18[R6[r, k]])
                else:
                    code = look(xs)
                    z = np.zeros((xs.shape[0], 6), dtype=np.int64)
                    for k in range(6):
                        z += rows[R6[:, k]][:, code[:, k] + off].T * SG6[:, k][None, :]
                z = wrap16(z)
                parts.append(imdct_fold12(z))
            for w in range(3):
                yw = _win_apply(parts[w], dsp.SINE12, wform, cfg, stats)
                y[g, :prune, 6 + 6 * w:18 + 6 * w] += yw
        else:
            if form == "int":
                cq = np.round(DCT4_18 * 32768).astype(np.int64)
                z = rnd_shift(Xg @ cq.T, 15)
            elif form == "split":
                z = np.zeros((Xg.shape[0], 18), dtype=np.int64)
                for k in range(18):
                    for r in range(18):
                        z[:, r] += SG18[r, k] * split_mul(Xg[:, k], SPLIT18[R18[r, k]])
            else:
                code = look(Xg)
                z = np.zeros((Xg.shape[0], 18), dtype=np.int64)
                for k in range(18):
                    nzk = code[:, k] != 0
                    if nzk.any():
                        z += rows[R18[:, k]][:, code[:, k] + off].T * SG18[:, k][None, :]
            z = wrap16(z)
            y[g, :prune] = _win_apply(imdct_fold36(z), dsp.block_window(bt[g]),
                                      wform, cfg, stats)
    return wrap16(y)


def _win_apply(yz, w, form, cfg, stats):
    """Window the folded IMDCT outputs (int16, Q_R) into Q_S."""
    if form == "split":
        out = np.zeros_like(yz)
        for i in range(len(w)):
            if w[i]:
                out[:, i] = split_mul(yz[:, i], split_rows(w[i] / (1 << (Q_R - Q_S))))
        return out
    if form in ("int", "float"):
        wq = np.round(w * 32767).astype(np.int64)
        return rnd_shift(yz * wq[None, :], 15 + Q_R - Q_S)
    cm = cfg["map_iw"]
    _, look = cm.enc_table(cfg.get("s_iw", 0), Q_R)
    code = look(yz)
    out = np.zeros_like(yz)
    for i in range(len(w)):
        if w[i] == 0:
            continue
        row = ptable(cm, w[i], Q_R, Q_S)[0]
        out[:, i] = row[code[:, i] + cm.L - 1]
    return out


def stage_overlap(y, prune=32):
    """Overlap-add and frequency inversion -> S (slots, 32)."""
    G = y.shape[0]
    fl = np.issubdtype(y.dtype, np.floating)
    out = np.zeros((G, 32, 18), dtype=y.dtype)
    over = np.zeros((32, 18), dtype=y.dtype)
    for g in range(G):
        cur = y[g, :, :18] + over
        out[g] = cur if fl else wrap16(cur)
        over = y[g, :, 18:]
    out[:, 1::2, 1::2] *= -1
    return out.transpose(0, 2, 1).reshape(G * 18, 32)


def decode_chain(xr, bt, cfg, stats=None):
    """The configurable decoder: returns (pcm float in [-2,2), stats).
    cfg keys: aa, imdct, imdctwin, dct, win ('float'|'int'|'tab'|'skip' for
    aa), coef (32,16) taps, div (1|2|4), maps and encoder shifts."""
    stats = stats or Stats()
    div = cfg.get("div", 1)
    prune = 32 // div
    xr = xr.copy()
    xr[:, prune:, :] = 0
    xa = stage_antialias(xr, bt, cfg.get("aa", "float"), cfg, stats)
    imf = cfg.get("imdct", "float")
    if imf == "float" and cfg.get("imdctwin", "float") == "float":
        xf = xa if np.issubdtype(np.asarray(xa).dtype, np.floating) else xa / float(1 << Q_R)
        xf = xf.copy()
        xf[:, prune:, :] = 0
        S = dsp.hybrid_synthesis(xf, bt, antialias=False)
    else:
        if imf == "float":
            imf = "int"
        y = stage_imdct(xa, bt, imf, cfg, stats, prune)
        S = stage_overlap(y, prune)            # int16, Q_S
    return synth_chain(S, cfg, stats), stats


def synth_chain(S, cfg, stats):
    """Matrixing and window. S float or int16 Q_S."""
    div = cfg.get("div", 1)
    phases = list(range(0, 32, div))
    coef = cfg.get("coef", dsp.DTAP)
    dform = cfg.get("dct", "float")
    wform = cfg.get("win", "float")
    fl = np.issubdtype(np.asarray(S).dtype, np.floating)
    Sf = S if fl else S / float(1 << Q_S)
    if dform == "float":
        V = dsp.synth_V(Sf)
        Vf = V
    else:
        Si = _toint_count(S, Q_S, stats, "dct_in") if fl else S.astype(np.int64)
        if dform == "int":
            X = dct_int(Si)
        elif dform == "split":
            X = dct_split(Si)
        elif dform.startswith("sa"):
            X = dct_sa(Si, int(dform[2:]))
        else:
            X, _ = dct_tab(Si, cfg["map_dct"], cfg.get("s_dct", 0))
        V = dct_to_V(wrap16(X))          # int16, Q_V == Q_S
        Vf = V / float(1 << Q_V)
    if wform == "float":
        U = dsp.synth_taps(Vf, phases)
        y = np.einsum("tji,ji->tj", U, coef[phases])
        return y.reshape(-1)
    Vi = _toint_count(Vf, Q_V, stats, "win_in") if np.issubdtype(np.asarray(V).dtype, np.floating) else V.astype(np.int64)
    U = dsp.synth_taps(Vi.astype(np.float64), phases).astype(np.int64)
    if wform == "int":
        y = window_int(U, coef[phases])
    else:
        bank, rowidx, rowsign = cfg["win_bank"]
        _, look = bank.cm.enc_table(cfg.get("s_win", 0), Q_V, cfg.get("enc_mode", "round"))
        y = window_tab(look(U), rowidx[phases], rowsign[phases], bank)
    return y.reshape(-1) / float(1 << Q_O)


def make_win_bank(cm, coef, qout=Q_O):
    """The window's product rows: one per distinct |c| (exact sharing of
    equal magnitudes, sign by add/sub), and the (32,16) row/sign maps."""
    vals, idx = unique_rows(coef)
    bank = TableBank(cm, vals, qout)
    sign = np.sign(coef).astype(np.int64)
    return bank, idx, sign
