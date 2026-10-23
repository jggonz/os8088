#!/usr/bin/env python3
"""x3simxt: XTMP3's XT backend model, bit-exact for apps/xtmp3/x3xt.asm
(SPEC.md 121.4, 121.10; docs/xtmp3/XT.md; docs/xtmp3/XT-STUDY.md section 9).

    python3 tools/x3simxt.py --decode F.MP3 --profile std|fast|turbo|speech
                             [--out F.WAV]
    python3 tools/x3simxt.py --check [--all]     # golden PCM, tests/xtmp3/X3XT.txt
    python3 tools/x3simxt.py --golden            # rewrite X3XT.txt
    python3 tools/x3simxt.py --quality           # SNR/peak/DC/clips vs float and lame
    python3 tools/x3simxt.py --emit-inc FILE     # the part's constant include
    python3 tools/x3simxt.py --records F.MP3 OUT # the granule records, for the bench

tools/x3sim.py's parser and front end (frames, side information,
scalefactors, Huffman, quarter exponents, the stereo plan) produce the
granule record (SPEC.md 121.3); everything from requantisation onward here
is the XT backend, and x3xt.asm reproduces it byte for byte.  The tables are
tools/x3xttab.py's, built by its own recurrences, so the bytes this model
reads are the bytes OPEN expands.

THE XT CHAIN (binding: the order and every rounding are part of it)
-------------------------------------------------------------------
Values are integers.  "wrap" is 16-bit two's complement (the target's add);
nearly the whole chain wraps, as XT-STUDY.md 3 pins.  Formats:
  linear line   int16 Q13 (1.0 FS = 8,192)        subband sample  int16 Q11
  V (Lee out)   int16 Q11                         window sum      int16 Q13

 1 REQUANTISE (every profile, one linear path).  |ix| <= 15: q < -80 -> 0;
   q > 7 -> 32767; else RQL[q + 80][|ix|] (x3xttab's exact table).  |ix| >
   15: x3sim's pow43 (M, E), F = 2^(q&3 / 4) Q14: P = M*F (< 2^30), s = 15 -
   E - (q >> 2); s >= 31 -> 0; s >= 1 -> (P + 2^(s-1)) >> s; s <= 0 -> P <<
   -s; saturated to 32767.  The sign of ix.  (The study's RQ code table is
   not used: stereo and the downmix need linear values, and one path is
   one kernel.)
 2 STEREO AND THE DOWNMIX (the XT output is mono).  Each channel's effective
   block (a silent channel takes x3sim's _silent_gi) has a key (block_type,
   mixed).  When the keys agree (always, for a mono source), the spectra are
   mixed line by line in bitstream order: a plan's 'ms' line is L (the M
   value: L = (M+S)/sqrt2, R = (M-S)/sqrt2 with 1/sqrt2 in q, so their mean
   is M exactly); 'lr' (or no plan) is (L + R + 1) >> 1; an intensity line
   is (L*kl + L*kr + 2^14) >> 15, kl kr x3sim's Q14 factors, saturated.  One
   hybrid pass follows.  When the keys differ, each channel is
   stereo-processed (ms: sat(L+R), sat(L-R); is: sat((L*k + 2^13) >> 14))
   and runs its own hybrid to the windowed 36-point outputs, which are
   mixed as (a + b + 1) >> 1 before the one overlap.
 3 REORDER a short block (x3sim's reorder_index: [3k + w] in a subband).
   Lines at and above 18 * 32/div are zero (the reduced rate).
 4 ANTIALIAS over boundaries 1..aa (aa = 31 long, 1 mixed short, none short;
   at most nsb - 1): lo' = lo cs + hi ca, hi' = hi cs - lo ca, lo = x[18b-1-i],
   hi = x[18b+i], ca the magnitude.  std/speech: byte-split products
   (AASPL, H[x>>8 & 255] + L[x & 255]); fast/turbo: both inputs coded by
   ENCR and the four products read from AATAB.  Sums wrap.
 5 IMDCT a subband.  LONG: when at least FAST_MIN of its 18 lines are
   nonzero, the FAST form on the linear lines (minimp3's 18-point
   factorisation: two 9-point DCT-IIIs with `imul` by Q15 constants rounded
   (a c + 2^14) >> 15, halving (x + 1) >> 1, adds wrapping; z[9+i] = (co
   (-tw1) + si (-tw0) + 2^14) >> 15, z[8-i] = (co (-tw0) + si tw1 + 2^14) >>
   15, one rounding of the 32-bit sum); else the SPARSE form: each line
   coded by ENCR (b = (v + 2) >> 2 clamped to 8191; ties toward zero), and
   z[r] = sum over the lines of +-IMDCT[row][code] (18 rows of |cos(pi m /
   72)| over the R map), wrapping.  z (the 18-point DCT-IV, Q13) folds to 36
   outputs.  SHORT: always sparse, three 6-point DCT-IVs folded to 12 each,
   at 6 + 6w.  Then the WINDOW: each of the 36 outputs times its window
   value / 4 by IMWIN's byte-split rows (Q13 -> Q11; zero-window outputs
   are 0); block types 0/1/3 (a mixed block's subbands 0 and 1 are 0);
   short windows sum where they overlap, wrapping.
 6 OVERLAP: out = y[0..17] + overlap (wrap), overlap = y[18..35]; odd
   subbands' odd slots negated.  One overlap (mono) of 32 x 18 words.
 7 MATRIXING a slot: Lee's DCT-II of size N = 32/div on the subband samples
   (x3xtmodel.DctPlan's recursion), every multiply LEE's byte-split rows,
   wrapping.  X[div l] of the 32-point DCT is the N-point DCT's X[l].
 8 V CODES: each kept X value is coded once, ENCV[x + 4096] for |x| <
   4096, else the nearest V level (ties toward zero; the slow path,
   counted); a V value is +-X (x3xtmodel.dct_to_V) and its code is the
   same sign times code(X).
 9 THE WINDOW: output j of a slot is acc = 32 + sum over the profile's kept
   taps i of sign(D[32i+j]) WIN[|D|][code(V_age i [j or 32+j])], wrapping
   (V[j] at even ages, V[32+j] at odd).  PCM8 = 255 if acc >= 8192, 0 if acc
   < -8192 (each clip counted), else (acc >> 6) + 128.
Profiles: std 16 taps, fast 8 (DESIGN taps8), turbo 4 (taps4), speech 16 at
divisor 2 (an 8-12 kHz source) or 4 (16-24 kHz); speech refuses 32-48 kHz.
"""
import argparse
import hashlib
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import x3sim as S      # noqa: E402
import x3xttab as XT   # noqa: E402
import x3xtmodel as XM  # noqa: E402
import x3xtgen as G     # noqa: E402

FAST_MIN = G.FAST_MIN
PROFILES = G.PROFILES
GOLDEN = os.path.join(S.FIXDIR, 'X3XT.txt')


def wrap(a):
    return ((np.asarray(a, dtype=np.int64) + 32768) & 0xFFFF) - 32768


def speech_div(rate):
    """XT Speech's divisor (SPEC.md 121.4): 2 at 8-12 kHz, 4 at 16-24 kHz."""
    if rate <= 12000:
        return 2
    if rate <= 24000:
        return 4
    return None


def stdd_div(rate):
    """XT Standard's divisor from its source (SPEC.md 121.4, 121.5): the
    output near 11 kHz and never under the 8 kHz floor - 2 at 16-24 kHz, 4 at
    32-48 kHz. An 8-12 kHz source is XT Standard at 1 ('std'), so 'stdd'
    names only the divided cases."""
    if 16000 <= rate <= 24000:
        return 2
    if rate >= 32000:
        return 4
    return None


def profile_div(profile, rate):
    if profile == 'speech':
        return speech_div(rate)
    if profile == 'stdd':
        return stdd_div(rate)
    return 1


def table_profile(profile):
    """The tables a profile decodes with. 'stdd' - XT Standard divided - IS
    XT Speech's: the same 16 window terms on the kept phases, the same Lee
    16/8 and byte-split stages (std at a divisor and speech at it are one
    output, bit for bit); only the divisor's rule differs."""
    return 'speech' if profile == 'stdd' else profile


# =============================================================================
# THE TABLES (x3xttab's recurrences: the bytes OPEN expands)
# =============================================================================

class Tabs:
    def __init__(self, profile):
        n, pack, aa = PROFILES[profile]
        self.n = n
        vm = XT.DESIGN['v_map']
        self.lvV = XT.v_levels()
        self.lvR = XT.r_levels()
        D = XT.iso_window_int()
        self.D = D
        rows = {}
        for ib in XT.win_row_values(n):
            rows[ib] = np.array(XT.win_row_recurrence(ib, vm['lens'], vm['s0']), dtype=np.int64)
        self.win = rows
        self.taps = XT.kept_taps(n)
        self.encv = np.array(XT.enc_v(), dtype=np.int64)
        self.encr = np.array(XT.enc_r(), dtype=np.int64)
        self.split = {}
        for c in list(XT.LEE_C16) + list(XT.AA_C16) + list(XT.WIN36_C16):
            H, L = XT.split_recurrence(c)
            self.split[c] = (np.array(H, dtype=np.int64), np.array(L, dtype=np.int64))
        self.imrow = np.array([XT.mul_row(c, self.lvR, 15) for c in XT.COS_C15], dtype=np.int64)
        self.aatab = np.array([XT.mul_row(c, self.lvR, 15) for c in XT.AA_C15], dtype=np.int64)
        self.rql = np.array(XT.rql_table(), dtype=np.int64)
        self.aa = aa

    def smul(self, v, c16):
        H, L = self.split[c16]
        v = np.asarray(v, dtype=np.int64) & 0xFFFF
        return wrap(H[(v >> 8) & 255] + L[v & 255])

    def enc_r(self, v):
        b = (np.asarray(v, dtype=np.int64) + 2) >> 2
        b = np.minimum(b, 8191)
        return self.encr[b + 8192]

    def enc_v(self, v, stats=None):
        v = np.asarray(v, dtype=np.int64)
        out = np.empty_like(v)
        small = (v >= -4096) & (v < 4096)
        out[small] = self.encv[v[small] + 4096]
        big = ~small
        if big.any():
            lv = np.array(self.lvV, dtype=np.int64)
            a = np.abs(v[big])
            # nearest level, ties toward zero: advance while 2a > lv[k] + lv[k+1]
            mids2 = lv[:-1] + lv[1:]
            k = np.searchsorted(mids2, 2 * a, side='left')
            out[big] = np.where(v[big] < 0, -k, k)
            if stats is not None:
                stats['vslow'] += int(big.sum())
        return out


# Lee's constants by the float the plan meets -> c16 (x3xttab.derive_consts)
def _c16(c):
    return int(round(c * 65536))


# the windows, the fast IMDCT's constants and the cos sign tables: one
# definition each, in tools/x3xtgen.py (standard library, the make path's)
WINDOWS, SHORT_WIN = G.WINDOWS, G.SHORT_WIN
TW1, TW0 = G.TW1, G.TW0
DCT9 = G.DCT9
R18, SG18 = np.array(G.R18), np.array(G.SG18)
R6, SG6 = np.array(G.R6), np.array(G.SG6)


def imq(a, c):
    """imul by a Q15 constant, rounded: (a c + 2^14) >> 15 (cannot overflow)."""
    return (np.asarray(a, dtype=np.int64) * c + 16384) >> 15


def ihalf(a):
    return (np.asarray(a, dtype=np.int64) + 1) >> 1


def dct3_9(y):
    """minimp3's 9-point DCT-III (x3sim.dct3_9_i's dataflow) with wrapping
    adds and rounded Q15 imuls.  y: 9 arrays."""
    C = DCT9
    s0, s2, s4, s6, s8 = y[0], y[2], y[4], y[6], y[8]
    t0 = wrap(s0 + ihalf(s6))
    s0 = wrap(s0 - s6)
    t4 = imq(wrap(s4 + s2), C['c2'])
    t2 = imq(wrap(s8 + s2), C['c1'])
    s6 = imq(wrap(s4 - s8), C['c3'])
    s4 = wrap(s4 + s8 - s2)
    s2 = wrap(s0 - ihalf(s4))
    o4 = wrap(s4 + s0)
    s8 = wrap(t0 - t2 + s6)
    s0 = wrap(t0 - t4 + t2)
    s4 = wrap(t0 + t4 - s6)
    s1, s3, s5, s7 = y[1], y[3], y[5], y[7]
    s3 = imq(s3, C['s3'])
    t0 = imq(wrap(s5 + s1), C['c4'])
    t4 = imq(wrap(s5 - s7), C['c5'])
    t2 = imq(wrap(s1 + s7), C['c6'])
    s1 = imq(wrap(s1 - s5 - s7), C['s3'])
    s5 = wrap(t0 - s3 - t2)
    s7 = wrap(t4 - s3 - t0)
    s3 = wrap(t4 + s3 - t2)
    return [wrap(s4 - s7), wrap(s2 + s1), wrap(s0 - s3), wrap(s8 + s5),
            o4, wrap(s8 - s5), wrap(s0 + s3), wrap(s2 - s1), wrap(s4 + s7)]


def dct4_18_fast(x):
    """x (nb, 18) Q13 -> z (nb, 18) = DCT-IV_18, the fast form."""
    co = [None] * 9
    si = [None] * 9
    co[0] = wrap(-x[:, 0])
    si[0] = x[:, 17]
    for i in range(4):
        si[8 - 2 * i] = wrap(x[:, 4 * i + 1] - x[:, 4 * i + 2])
        co[1 + 2 * i] = wrap(x[:, 4 * i + 1] + x[:, 4 * i + 2])
        si[7 - 2 * i] = wrap(x[:, 4 * i + 4] - x[:, 4 * i + 3])
        co[2 + 2 * i] = wrap(-(x[:, 4 * i + 3] + x[:, 4 * i + 4]))
    co = dct3_9(co)
    si = dct3_9(si)
    for k in (1, 3, 5, 7):
        si[k] = wrap(-si[k])
    z = np.empty_like(x)
    for i in range(9):
        z[:, 9 + i] = wrap((co[i] * -TW1[i] + si[i] * -TW0[i] + 16384) >> 15)
        z[:, 8 - i] = wrap((co[i] * -TW0[i] + si[i] * TW1[i] + 16384) >> 15)
    return z


def fold36(z):
    y = np.empty(z.shape[:-1] + (36,), dtype=np.int64)
    y[..., 0:9] = z[..., 9:18]
    for i in range(9, 27):
        y[..., i] = -z[..., 26 - i]
    for i in range(27, 36):
        y[..., i] = -z[..., i - 27]
    return wrap(y)


def fold12(z):
    y = np.empty(z.shape[:-1] + (12,), dtype=np.int64)
    y[..., 0:3] = z[..., 3:6]
    for i in range(3, 9):
        y[..., i] = -z[..., 8 - i]
    for i in range(9, 12):
        y[..., i] = -z[..., i - 9]
    return wrap(y)


# =============================================================================
# THE DECODER
# =============================================================================

class XTDecoder:
    def __init__(self, profile, div):
        self.profile = profile
        self.T = Tabs(profile)
        self.div = div
        self.nsb = 32 // div
        self.ovl = np.zeros((32, 18), dtype=np.int64)
        self.prev_bt = [0, 0]
        self.ring = np.zeros((15, 64), dtype=np.int64)     # V codes, ages 1..15
        self.stats = dict(clip=0, vslow=0, rqsat=0, fast=0, sparse=0, short=0,
                          granules=0, split=0)
        self.fast_min = FAST_MIN        # (a measurement may move it; the target's is X3X_FAST_MIN)
        self.sblog = None               # per long nonzero subband: its nonzero lines
        self.plan = XM.DctPlan(self.nsb)
        self.phases = list(range(0, 32, div))
        D = self.T.D
        # per kept phase: (tap i, |D| row, sign) for its kept taps
        self.wtaps = []
        for j in self.phases:
            t = []
            for i in self.T.taps[j]:
                d = D[32 * i + j]
                if d:
                    t.append((i, abs(d), 1 if d > 0 else -1))
            self.wtaps.append(t)

    # ---- 1: requantise ----
    def requant(self, ix, q):
        ix = np.asarray(ix, dtype=np.int64)
        q = np.asarray(q, dtype=np.int64)
        a = np.abs(ix)
        out = np.zeros(576, dtype=np.int64)
        small = (a > 0) & (a <= 15)
        if small.any():
            qs = q[small]
            v = self.T.rql[np.clip(qs - XT.RQ_G0, 0, XT.RQ_G1 - XT.RQ_G0), a[small]]
            v = np.where(qs < XT.RQ_G0, 0, v)
            v = np.where(qs > XT.RQ_G1, 32767, v)
            out[small] = v
        big = a > 15
        if big.any():
            m, e = S.pow43_i(a[big])
            qb = q[big]
            p = m * np.asarray(S.Q.P43_F, dtype=np.int64)[qb & 3]
            s = 15 - e - (qb >> 2)
            r = np.zeros_like(p)
            for k in range(len(p)):
                sk = int(s[k])
                if sk >= 31:
                    r[k] = 0
                elif sk >= 1:
                    r[k] = (int(p[k]) + (1 << (sk - 1))) >> sk
                else:
                    r[k] = int(p[k]) << min(-sk, 20)
            over = r > 32767
            self.stats['rqsat'] += int(over.sum())
            out[big] = np.minimum(r, 32767)
        return np.where(ix < 0, -out, out)

    # ---- 3..5: one channel's hybrid to windowed 36-point outputs ----
    def hybrid(self, h, gi, x):
        T = self.T
        nsb = self.nsb
        x = np.array(x, dtype=np.int64)
        if gi.block_type == 2:
            x = x[S.reorder_index(h, gi)]
        x[18 * nsb:] = 0
        if gi.block_type == 2:
            aa = (2 if gi.mixed else 0) - 1
        else:
            aa = 31
        aa = min(aa, nsb - 1)
        if aa >= 1:
            b = np.arange(1, aa + 1)
            for i in range(8):
                lo_i = 18 * b - 1 - i
                hi_i = 18 * b + i
                lo, hi = x[lo_i].copy(), x[hi_i].copy()
                if T.aa == 'split':
                    cs, ca = XT.AA_C16[i], XT.AA_C16[8 + i]
                    x[lo_i] = wrap(T.smul(lo, cs) + T.smul(hi, ca))
                    x[hi_i] = wrap(T.smul(hi, cs) - T.smul(lo, ca))
                else:
                    cl, ch = T.enc_r(lo) + 128, T.enc_r(hi) + 128
                    rcs, rca = T.aatab[i], T.aatab[8 + i]
                    x[lo_i] = wrap(rcs[cl] + rca[ch])
                    x[hi_i] = wrap(rcs[ch] - rca[cl])
        xb = x.reshape(32, 18)[:nsb]
        y = np.zeros((nsb, 36), dtype=np.int64)
        for sb in range(nsb):
            short = gi.block_type == 2 and not (gi.mixed and sb < 2)
            if short:
                if self.sblog is not None:
                    self.sblog.append(-1)
                self.stats['short'] += 1
                for w in range(3):
                    c = T.enc_r(xb[sb, w::3]) + 128          # 6 codes
                    z = np.zeros(6, dtype=np.int64)
                    for k in range(6):
                        z += SG6[:, k] * T.imrow[R6[:, k], c[k]]
                    yw = fold12(wrap(z))
                    for i in range(12):
                        y[sb, 6 + 6 * w + i] += T.smul(yw[i], XT.WIN36_C16[SHORT_WIN[i]])
                y[sb] = wrap(y[sb])
                continue
            bt = 0 if (gi.win_switch and gi.mixed and sb < 2) else gi.block_type
            if self.sblog is not None:
                self.sblog.append(int(np.count_nonzero(xb[sb])))
            if np.count_nonzero(xb[sb]) >= self.fast_min:
                self.stats['fast'] += 1
                z = dct4_18_fast(xb[sb:sb + 1])[0]
            else:
                self.stats['sparse'] += 1
                c = T.enc_r(xb[sb]) + 128
                z = np.zeros(18, dtype=np.int64)
                for k in range(18):
                    if c[k] != 128:
                        z += SG18[:, k] * T.imrow[R18[:, k], c[k]]
                z = wrap(z)
            yz = fold36(z)
            win = WINDOWS[bt]
            for i in range(36):
                if win[i] is not None:
                    y[sb, i] = T.smul(yz[i], XT.WIN36_C16[win[i]])
        return y

    # ---- the granule ----
    def granule(self, h, chans, plan):
        """chans: per channel (ok, gi, ix, q); plan: x3sim's stereo plan or
        None.  Returns PCM8 (uint8, 576/div)."""
        self.stats['granules'] += 1
        nch = len(chans)
        lin, eff = [], []
        for ch, (ok, gi, ix, q) in enumerate(chans):
            if ok:
                lin.append(self.requant(ix, q))
                g = gi
            else:
                lin.append(np.zeros(576, dtype=np.int64))
                g = S._silent_gi(self.prev_bt[ch])
            self.prev_bt[ch] = g.block_type
            eff.append(g)
        if nch == 1:
            y = self.hybrid(h, eff[0], lin[0])
        else:
            L, R = lin
            key = [(g.block_type, g.mixed) for g in eff]
            if key[0] == key[1]:
                m = (L + R + 1) >> 1
                if plan is not None:
                    for l, p in enumerate(plan):
                        if p == 'ms':
                            m[l] = L[l]
                        elif p != 'lr':
                            kl, kr = S.is_factors_q(h, eff[1], p[1])
                            v = (int(L[l]) * kl + int(L[l]) * kr + 16384) >> 15
                            m[l] = max(-32768, min(32767, v))
                y = self.hybrid(h, eff[0], m)
            else:
                self.stats['split'] += 1
                L2, R2 = L.copy(), R.copy()
                if plan is not None:
                    for l, p in enumerate(plan):
                        if p == 'ms':
                            L2[l] = max(-32768, min(32767, int(L[l] + R[l])))
                            R2[l] = max(-32768, min(32767, int(L[l] - R[l])))
                        elif p != 'lr':
                            kl, kr = S.is_factors_q(h, eff[1], p[1])
                            L2[l] = max(-32768, min(32767, (int(L[l]) * kl + 8192) >> 14))
                            R2[l] = max(-32768, min(32767, (int(L[l]) * kr + 8192) >> 14))
                ya = self.hybrid(h, eff[0], L2)
                yb = self.hybrid(h, eff[1], R2)
                y = (ya + yb + 1) >> 1
        return self.synth(self.overlap(y))

    def overlap(self, y):
        nsb = self.nsb
        cur = wrap(y[:, :18] + self.ovl[:nsb])
        self.ovl[:nsb] = y[:, 18:]
        cur[1::2, 1::2] = wrap(-cur[1::2, 1::2])
        return cur.T          # (18 slots, nsb)

    def synth(self, Sb):
        T = self.T
        div = self.div
        X = wrap(self.plan.run(Sb.astype(np.int64),
                               lambda v, c, n: T.smul(v, _c16(c))))
        # each kept X value is coded once; a V value is +-X, and its code
        # is +-code(X) (the target folds the sign into the add or sub)
        cx = T.enc_v(X.reshape(-1), self.stats).reshape(X.shape)
        Xf = np.zeros((18, 32), dtype=np.int64)
        Xf[:, ::div] = cx
        codes = XM.dct_to_V(Xf)
        hist = np.concatenate([self.ring[::-1], codes])    # rows: ages 15..1, then slots
        out = np.empty((18, len(self.phases)), dtype=np.int64)
        for m, j in enumerate(self.phases):
            acc = np.full(18, 32, dtype=np.int64)
            for i, ib, sg in self.wtaps[m]:
                col = j if i % 2 == 0 else 32 + j
                c = hist[15 - i:15 - i + 18, col]
                acc += sg * T.win[ib][c + 128]
            out[:, m] = wrap(acc)
        self.ring = hist[-15:][::-1].copy()
        acc = out.reshape(-1)
        clip = (acc >= 8192) | (acc < -8192)
        self.stats['clip'] += int(clip.sum())
        pcm = np.clip(acc >> 6, -128, 127) + 128
        return pcm.astype(np.uint8)


# =============================================================================
# THE RECORDS (x3sim's front end, decision for decision) AND THE DRIVER
# =============================================================================

def records(data):
    """Yield (h, chans, plan) per granule, as the front end hands a backend
    the granule record: chans per channel (ok, gi, ix, q).  A dropped frame's
    granules carry no ok channel."""
    st = S.Stream(data)
    if st.refused:
        raise S.Refused(st.refused)
    dec = S.Decoder(st, 'int')
    for h, side, md, verdict in st.frames():
        if verdict == S.F_TRUNC:
            break
        r = S.BitReader(md) if verdict == S.F_OK else None
        prev = [None, None]
        for gr in range(h.granules):
            if verdict != S.F_OK:
                yield h, [(False, None, None, None)] * h.nch, None
                continue
            sp = dec.granule_spectra(h, side, gr, r, prev)
            chans = []
            for gi, scf, ix, ok in sp:
                if ok:
                    q = S.quarter_exponents(h, gi, scf, h.ms and h.nch == 2)
                    chans.append((True, gi, np.array(ix, dtype=np.int64), q))
                else:
                    chans.append((False, gi, None, None))
            plan = None
            if h.nch == 2 and h.mode == 1 and sp[0][3] and sp[1][3]:
                plan = S.stereo_plan(h, sp[1][0], sp[1][1], sp[1][2])
            yield h, chans, plan
    return st


def decode(path_or_bytes, profile):
    data = path_or_bytes if isinstance(path_or_bytes, (bytes, bytearray)) else \
        open(path_or_bytes, 'rb').read()
    st = S.Stream(data)
    if st.refused:
        raise S.Refused(st.refused)
    div = profile_div(profile, st.h0.rate)
    if div is None:
        raise S.Refused('XT %s: SOURCE RATE' % profile.upper())
    dec = XTDecoder(table_profile(profile), div)
    out = [dec.granule(h, chans, plan) for h, chans, plan in records(data)]
    pcm = np.concatenate(out) if out else np.zeros(0, np.uint8)
    return pcm, st.h0.rate // div, dec


# =============================================================================
# GOLDEN OUTPUTS
# =============================================================================

GOLDEN_FIXTURES = (
    # the XT targets: MPEG-2.5 at 8, 11.025 and 12 kHz, mono
    'AFTERHRS.MP3', 'X8K16.MP3', 'X11K24.MP3', 'X12K32.MP3',
    # MPEG-2 16-24 kHz (speech at 4:1), stereo downmixed: joint, L/R, intensity
    'M16.MP3', 'NIGHTSIG.MP3', 'M22.MP3', 'M24.MP3', 'IS22.MP3',
    # 32-48 kHz: mono short blocks, joint stereo, intensity + M/S, L/R, damage
    'MONO44.MP3', 'NEONDRV.MP3', 'ISMS44.MP3', 'C128.MP3', 'BBADSIDE.MP3',
)


def golden_cases():
    out = []
    for name in GOLDEN_FIXTURES:
        rate = S.Stream(open(os.path.join(S.FIXDIR, name), 'rb').read()).h0.rate
        for p in ('std', 'fast', 'turbo', 'speech', 'stdd'):
            if profile_div(p, rate) is not None:
                out.append((name, p))
    return out


def golden_line(name, profile):
    pcm, rate, dec = decode(os.path.join(S.FIXDIR, name), profile)
    hsh = hashlib.sha256(pcm.tobytes()).hexdigest()
    return '%-13s %-6s d%d %7d %s clip %d vslow %d' % (
        name, profile, dec.div, len(pcm), hsh, dec.stats['clip'], dec.stats['vslow'])


def tables_crc():
    import zlib
    return zlib.crc32(XT.gen_input()) & 0xFFFFFFFF


def write_golden():
    lines = ['# XTMP3: the XT backend model\'s output (tools/x3simxt.py, SPEC.md 121.4).',
             '# Made by `tools/x3simxt.py --golden`; x3xt.asm is BIT-EXACT when its PCM',
             '# (unsigned 8-bit mono, untrimmed, from the first audio frame) hashes to',
             '# these.  fixture, profile, divisor, samples, SHA-256, output clips, V',
             '# codes on the slow path.  XTGEN.BIN CRC-32 %08X, FAST_MIN %d.' % (
                 tables_crc(), FAST_MIN),
             '']
    for c in golden_cases():
        lines.append(golden_line(*c))
        print(lines[-1])
    with open(GOLDEN, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print('wrote %s (%d cases)' % (GOLDEN, len(lines) - 6))


def read_golden():
    out = {}
    with open(GOLDEN) as f:
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            w = line.split()
            out[(w[0], w[1])] = line.rstrip('\n')
    return out


QUICK = (('X8K16.MP3', 'std'), ('X11K24.MP3', 'fast'), ('M16.MP3', 'speech'),
         ('IS22.MP3', 'turbo'), ('C128.MP3', 'stdd'))


def check(full=False):
    gold = read_golden()
    cases = list(gold.keys()) if full else QUICK
    bad = 0
    for c in cases:
        want = gold.get(c)
        got = golden_line(*c)
        if want != got:
            print('x3simxt: GOLDEN MISMATCH\n  want %s\n  got  %s' % (want, got))
            bad += 1
    print('x3simxt: %s, %d golden case(s), %d failure(s)' % (
        'ok' if not bad else 'FAILED', len(cases), bad))
    return bad


# =============================================================================
# QUALITY: against the float decode and against lame
# =============================================================================

def _err(a, ref):
    """a, ref: float arrays in 8-bit LSB units (signed, 128 = full scale)."""
    n = min(len(a), len(ref))
    a, ref = a[:n], ref[:n]
    e = a - ref
    ps = float(np.mean(ref * ref)) or 1e-30
    pe = float(np.mean(e * e)) or 1e-30
    return dict(snr=10 * np.log10(ps / pe), peak=float(np.max(np.abs(e))) if n else 0.0,
                dc=float(np.mean(e)) if n else 0.0, rms=float(np.sqrt(pe)))


def quality(profiles=('std', 'fast', 'turbo', 'speech', 'stdd'), names=GOLDEN_FIXTURES,
            out=None):
    rows = []
    cache = os.path.join(os.path.dirname(GOLDEN), '..', '..', 'build', 'x3lame')
    for name in names:
        path = os.path.join(S.FIXDIR, name)
        st = S.Stream(open(path, 'rb').read())
        rate = st.h0.rate
        lame = None
        if os.path.exists(S.LAME):
            try:
                os.makedirs(cache, exist_ok=True)
                lame = S.lame_decode(path, cache)
            except Exception:
                lame = None
        for p in profiles:
            div = profile_div(p, rate)
            if div is None:
                continue
            pcm, orate, dec = decode(path, p)
            a = pcm.astype(np.float64) - 128.0
            fl, _, _, _ = S.decode_file(path, 'float', div, True)
            ref = fl[:, 0].astype(np.float64) / 256.0
            r8 = np.clip(np.floor(ref + 0.5), -128, 127)
            row = dict(name=name, profile=p, div=div, rate=orate,
                       vs_float=_err(a, ref), float8=_err(r8, ref),
                       clips=dec.stats['clip'], fast=dec.stats['fast'],
                       sparse=dec.stats['sparse'], vslow=dec.stats['vslow'])
            if lame is not None:
                lm = lame[0].astype(np.float64)
                lm = lm.mean(axis=1, keepdims=True)
                head, _ = S.trim_counts(st.info)
                if div == 1:
                    ref_l = lm[:, 0] / 256.0
                    row['vs_lame'] = _err(a[head:], ref_l)
                else:
                    ref_l = S.ideal_decimate(lm, head, div, len(a))[:, 0] / 256.0
                    k = (head + div - 1) // div
                    row['vs_lame'] = _err(a[k:], ref_l[k:])
            rows.append(row)
            print('%-13s %-6s d%d  vs float %5.1f dB peak %5.1f dc %+.2f | float8 %5.1f dB | clips %d%s' % (
                name, p, div, row['vs_float']['snr'], row['vs_float']['peak'],
                row['vs_float']['dc'], row['float8']['snr'], row['clips'],
                ('  vs lame %5.1f dB' % row['vs_lame']['snr']) if 'vs_lame' in row else ''))
    if out:
        import json
        with open(out, 'w') as f:
            json.dump(rows, f, indent=1)
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--decode', metavar='FILE')
    ap.add_argument('--profile', default='std', choices=sorted(PROFILES) + ['stdd'])
    ap.add_argument('--out', metavar='WAV')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--golden', action='store_true')
    ap.add_argument('--quality', action='store_true')
    ap.add_argument('--json', metavar='FILE')
    ap.add_argument('--only', nargs='*')
    a = ap.parse_args()
    if a.decode:
        try:
            pcm, rate, dec = decode(a.decode, a.profile)
        except S.Refused as e:
            print('refused: %s' % e)
            return 2
        print('%s: %s d%d, %d Hz, %d samples, %s' % (a.decode, a.profile, dec.div, rate,
                                                     len(pcm), dec.stats))
        if a.out:
            import wave
            w = wave.open(a.out, 'wb')
            w.setnchannels(1)
            w.setsampwidth(1)
            w.setframerate(rate)
            w.writeframes(pcm.tobytes())
            w.close()
        return 0
    if a.golden:
        write_golden()
        return 0
    if a.check:
        bad = check(a.all)
        if a.all:
            bad += G.fullcheck(True)
        return 1 if bad else 0
    if a.quality:
        quality(names=tuple(a.only) if a.only else GOLDEN_FIXTURES, out=a.json)
        return 0
    ap.print_help()
    return 0


if __name__ == '__main__':
    sys.exit(main())
