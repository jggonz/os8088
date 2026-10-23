#!/usr/bin/env python3
"""x3xtdsp.py - the float reference and the XT arithmetic models for the
XTMP3 XT study (SPEC.md 121.4, docs/xtmp3/XT-STUDY.md).

Host-side only, numpy. Nothing here runs on the target; it is the study's
instrument. The parts:

* the ISO 11172-3 polyphase ANALYSIS filterbank (the encoder's) and the
  reference float SYNTHESIS, so a test signal becomes subband samples and
  back;
* the encoder's hybrid stage (frequency inversion, the 36/12-point MDCT
  with block switching, the alias-reduction butterflies) and the decoder's
  inverse, so the IMDCT stage is fed MDCT-domain data with long, start,
  short and stop blocks and their overlap;
* an MP3-LIKE quantiser (|x|^0.75, a global step a granule found by a bit
  budget), so that the requantised values have realistic sparsity and
  dynamic range. It is a model of an encoder, not LAME;
* code maps, product tables and the XT stage models, in numpy integer
  arithmetic that follows the target's word sizes, rounding and wrap.

Conventions: a signal is float in [-1, 1) at the source rate; a subband
array is (slots, 32); a granule is 18 slots (576 samples); an MDCT array is
(granules, 32, 18) with a block type per granule.
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from x3xttab import iso_window_int  # noqa: E402

D = np.array(iso_window_int(), dtype=np.float64) / 65536.0     # synthesis
C = D / 32.0                                                    # analysis

_k = np.arange(32)
_i64 = np.arange(64)
# analysis matrixing M[k][i] = cos((2k+1)(i-16) pi / 64)
M_AN = np.cos((2 * _k[:, None] + 1) * (_i64[None, :] - 16) * np.pi / 64)
# synthesis matrixing N[i][k] = cos((16+i)(2k+1) pi / 64)
N_SY = np.cos((16 + _i64[:, None]) * (2 * _k[None, :] + 1) * np.pi / 64)
FB_DELAY = 481          # samples, analysis + synthesis (checked in selfcheck)


def analysis(x):
    """ISO 11172-3 encoder polyphase analysis. x: float, len % 32 == 0.
    Returns S (slots, 32)."""
    x = np.asarray(x, dtype=np.float64)
    T = len(x) // 32
    xp = np.concatenate([np.zeros(512), x[:T * 32]])
    idx = 512 + 32 * np.arange(T)[:, None] + 31 - np.arange(512)[None, :]
    Z = xp[idx] * C[None, :]
    Y = Z.reshape(T, 8, 64).sum(axis=1)
    return Y @ M_AN.T


def synth_V(S):
    """The 64 V values a slot: V = N S."""
    return S @ N_SY.T


def synth_taps(V, j_list=range(32)):
    """For each slot t and output phase j, the 16 tap INPUTS u[t, j, i]
    (V of age i: V[j] at even ages, V[32+j] at odd), so that
    out[t, j] = sum_i D[32 i + j] u[t, j, i]."""
    T = V.shape[0]
    Vp = np.concatenate([np.zeros((16, 64)), V])
    j = np.asarray(list(j_list))
    U = np.empty((T, len(j), 16))
    for i in range(16):
        rows = Vp[16 - i:16 - i + T]
        U[:, :, i] = rows[:, j] if i % 2 == 0 else rows[:, 32 + j]
    return U


DTAP = np.array([[D[32 * i + j] for i in range(16)] for j in range(32)])


def synthesis(S, coef=None, phases=None):
    """Reference float synthesis. coef: optional (32, 16) tap coefficients
    replacing DTAP (zeros drop a tap). phases: output phases kept (a
    reduced-rate path keeps every 2nd or 4th). Returns the PCM stream."""
    coef = DTAP if coef is None else coef
    j = list(range(32)) if phases is None else list(phases)
    U = synth_taps(synth_V(S), j)
    out = np.einsum("tji,ji->tj", U, coef[j])
    return out.reshape(-1)


# ---------------------------------------------------------------- hybrid
SINE36 = np.sin(np.pi / 36 * (np.arange(36) + 0.5))
SINE12 = np.sin(np.pi / 12 * (np.arange(12) + 0.5))


def block_window(bt):
    w = np.zeros(36)
    if bt == 0:
        w[:] = SINE36
    elif bt == 1:
        w[:18] = SINE36[:18]
        w[18:24] = 1.0
        w[24:30] = SINE12[6:]
        w[30:] = 0.0
    elif bt == 3:
        w[:6] = 0.0
        w[6:12] = SINE12[:6]
        w[12:18] = 1.0
        w[18:] = SINE36[18:]
    else:
        raise ValueError(bt)
    return w


_n36 = np.arange(36)
_k18 = np.arange(18)
COS36 = np.cos(np.pi / 72 * (2 * _n36[:, None] + 1 + 18) * (2 * _k18[None, :] + 1))
_n12 = np.arange(12)
_k6 = np.arange(6)
COS12 = np.cos(np.pi / 24 * (2 * _n12[:, None] + 1 + 6) * (2 * _k6[None, :] + 1))

CA_C = np.array([-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037])
CS = 1.0 / np.sqrt(1.0 + CA_C ** 2)
CA = CA_C / np.sqrt(1.0 + CA_C ** 2)


def block_types(x, sr, thresh=10.0):
    """A transient detector (energy ratio of short high-passed windows), and
    the ISO sequencing rule: a short granule is preceded by a START and
    followed by a STOP. One block type a granule (no mixed blocks)."""
    G = len(x) // 576
    hp = np.diff(np.concatenate([[0.0], x]))
    short = np.zeros(G, dtype=bool)
    prev = 1e-9
    for g in range(G):
        seg = hp[g * 576:(g + 1) * 576]
        e = (seg.reshape(6, 96) ** 2).sum(1) + 1e-9
        short[g] = e.max() > thresh * max(prev, 1e-6) and e.max() > 1e-4
        prev = e.mean() * 0.5 + prev * 0.5
    bt = np.zeros(G, dtype=int)
    for g in range(G):
        if short[g]:
            bt[g] = 2
    for g in range(G):
        if bt[g] != 2:
            nxt = g + 1 < G and bt[g + 1] == 2
            prv = g > 0 and bt[g - 1] == 2
            if nxt and prv:
                bt[g] = 2           # no room for a start and a stop
    for g in range(G):
        if bt[g] != 2:
            nxt = g + 1 < G and bt[g + 1] == 2
            prv = g > 0 and bt[g - 1] == 2
            if nxt:
                bt[g] = 1
            elif prv:
                bt[g] = 3
    return bt


def hybrid_analysis(S, bt):
    """Encoder hybrid: S (slots, 32) -> X (granules, 32, 18), alias-reduced
    for long blocks; short blocks hold window w's six lines at [6w:6w+6].
    The MDCT of granule g covers slots of granules g-1 and g."""
    G = S.shape[0] // 18
    s = S[:G * 18].reshape(G, 18, 32).transpose(0, 2, 1).copy()  # (G,32,18)
    s[:, 1::2, 1::2] *= -1.0        # frequency inversion (its own inverse)
    prev = np.zeros((32, 18))
    X = np.zeros((G, 32, 18))
    for g in range(G):
        z = np.concatenate([prev, s[g]], axis=1)            # (32, 36)
        if bt[g] == 2:
            for w in range(3):
                seg = z[:, 6 + 6 * w:18 + 6 * w] * SINE12[None, :]
                X[g, :, 6 * w:6 * w + 6] = (seg @ COS12) * (4.0 / 12)
        else:
            X[g] = ((z * block_window(bt[g])[None, :]) @ COS36) * (4.0 / 36)
            flat = X[g].reshape(-1)
            for sb in range(1, 32):
                lo = 18 * sb - 1 - np.arange(8)
                hi = 18 * sb + np.arange(8)
                a, b = flat[lo].copy(), flat[hi].copy()
                flat[lo] = a * CS + b * CA
                flat[hi] = b * CS - a * CA
            X[g] = flat.reshape(32, 18)
        prev = s[g]
    return X


def alias_reduce_dec(X, bt):
    """Decoder antialias butterflies (long blocks)."""
    X = X.copy()
    for g in range(X.shape[0]):
        if bt[g] == 2:
            continue
        flat = X[g].reshape(-1)
        for sb in range(1, 32):
            lo = 18 * sb - 1 - np.arange(8)
            hi = 18 * sb + np.arange(8)
            a, b = flat[lo].copy(), flat[hi].copy()
            flat[lo] = a * CS - b * CA
            flat[hi] = b * CS + a * CA
    return X


def imdct_granule(Xg, btg):
    """(32, 18) -> (32, 36) windowed IMDCT output (float reference)."""
    if btg == 2:
        y = np.zeros((32, 36))
        for w in range(3):
            y[:, 6 + 6 * w:18 + 6 * w] += (Xg[:, 6 * w:6 * w + 6] @ COS12.T) * SINE12[None, :]
        return y
    return (Xg @ COS36.T) * block_window(btg)[None, :]


def hybrid_synthesis(X, bt, antialias=True):
    """Decoder hybrid (float): antialias, IMDCT, overlap-add, frequency
    inversion. Returns S (slots, 32)."""
    G = X.shape[0]
    Xa = alias_reduce_dec(X, bt) if antialias else X
    over = np.zeros((32, 18))
    out = np.zeros((G, 32, 18))
    for g in range(G):
        y = imdct_granule(Xa[g], bt[g])
        out[g] = y[:, :18] + over
        over = y[:, 18:]
    out[:, 1::2, 1::2] *= -1.0
    return out.transpose(0, 2, 1).reshape(G * 18, 32)


# --------------------------------------------------- MP3-like quantiser
def huff_bits_model(ix):
    """Approximate Layer III Huffman cost of one granule's |ix| (576,).
    Values above the last nonzero cost nothing (the zero region); in the
    count1 region (|v| <= 1 tail) a value costs ~1.3 bits plus sign; in
    big_values: 1 bit for a zero, 2*log2(v)+2 plus sign, plus linbits over 15.
    A MODEL, so that sparsity and |ix| statistics are plausible; phase 2
    re-takes every density from real fixtures."""
    nz = np.nonzero(ix)[0]
    if len(nz) == 0:
        return 0.0
    last = nz[-1] + 1
    v = ix[:last]
    big = np.nonzero(v > 1)[0]
    bvend = big[-1] + 1 if len(big) else 0
    bvv = v[:bvend]
    c1 = v[bvend:]
    bits = np.where(bvv == 0, 1.0,
                    2.0 * np.log2(np.maximum(bvv, 1)) + 2.0 + 1.0).sum()
    bits += np.where(bvv > 15, 4.0, 0.0).sum()
    bits += 1.3 * len(c1) + (c1 > 0).sum()
    return float(bits)


def quantise(X, bt, sr, kbps, mdct_quant=True):
    """MP3-like quantisation of the MDCT lines: ix = nint(|x/step|^0.75 -
    0.0946), step = 2^(g/4), the smallest g whose modelled cost fits the
    granule's main-data budget. Returns (Xq float, ix int, g per granule)."""
    G = X.shape[0]
    budget = kbps * 1000.0 * 576 / sr - 32 - 72 - 40   # header, side, sf
    Xq = np.zeros_like(X)
    IX = np.zeros(X.shape, dtype=np.int64)
    gs = np.zeros(G, dtype=int)
    for g in range(G):
        flat = X[g].reshape(-1)
        a = np.abs(flat)
        lo, hi = -120, 120
        while lo < hi:      # smallest step exponent that fits
            mid = (lo + hi) // 2
            step = 2.0 ** (mid / 4.0)
            ix = np.floor((a / step) ** 0.75 + 0.4054).astype(np.int64)
            ix = np.minimum(ix, 8191 + 15)
            if huff_bits_model(ix) <= budget:
                hi = mid
            else:
                lo = mid + 1
        step = 2.0 ** (lo / 4.0)
        ix = np.minimum(np.floor((a / step) ** 0.75 + 0.4054).astype(np.int64), 8206)
        IX[g] = (ix * np.sign(flat).astype(np.int64)).reshape(32, 18)
        Xq[g] = (np.sign(flat) * ix.astype(np.float64) ** (4.0 / 3.0) * step).reshape(32, 18)
        gs[g] = lo
    return Xq, IX, gs


# ---------------------------------------------------------------- signals
def _env(n, att, rel, sr):
    a = min(int(att * sr), n // 2)
    r = min(int(rel * sr), n // 2)
    e = np.ones(n)
    if a:
        e[:a] = np.linspace(0, 1, a)
    if r:
        e[-r:] *= np.linspace(1, 0, r)
    return e


def sig_music(sr, secs, seed=1, key=0):
    """A phrase: four chords of harmonic tones, a bass line and a soft
    kick/hat pattern."""
    rng = np.random.default_rng(seed)
    n = int(sr * secs)
    t = np.arange(n) / sr
    x = np.zeros(n)
    chords = [[0, 4, 7], [5, 9, 12], [7, 11, 14], [-3, 0, 4]]
    beat = n // 8
    for c in range(8):
        root = 220.0 * 2 ** ((key + [0, 5, 7, -3][c % 4]) / 12)
        seg = slice(c * beat, (c + 1) * beat)
        tt = t[seg] - t[seg][0]
        e = np.exp(-tt * 1.5) * _env(len(tt), 0.01, 0.05, sr)
        for semi in chords[c % 4]:
            f = 220.0 * 2 ** ((key + semi + [0, 5, 7, -3][c % 4]) / 12)
            for h in range(1, 7):
                if f * h < sr / 2 * 0.95:
                    x[seg] += 0.06 / h * e * np.sin(2 * np.pi * f * h * tt + rng.uniform(0, 6.28))
        fb = root / 4
        x[seg] += 0.22 * e * (np.sin(2 * np.pi * fb * tt) + 0.3 * np.sin(4 * np.pi * fb * tt))
        k = np.exp(-tt * 30) * np.sin(2 * np.pi * (60 + 80 * np.exp(-tt * 40)) * tt)
        x[seg] += 0.35 * k
        hat = rng.standard_normal(len(tt)) * np.exp(-((tt - 0.5 * len(tt) / sr) ** 2) * 2e4)
        x[seg] += 0.04 * np.diff(np.concatenate([[0], hat]))
    return 0.8 * x / np.max(np.abs(x))


def sig_speech(sr, secs, seed=2, f0=120.0):
    """Speech-like: a glottal pulse train with a wandering pitch through
    vowel formant resonators, unvoiced noise for fricatives, and pauses."""
    rng = np.random.default_rng(seed)
    n = int(sr * secs)
    vowels = [(730, 1090, 2440), (270, 2290, 3010), (530, 1840, 2480),
              (570, 840, 2410), (300, 870, 2240), (660, 1720, 2410)]
    x = np.zeros(n)
    pos = 0
    phase = 0.0
    while pos < n:
        kind = rng.choice(["v", "v", "v", "f", "p"])
        dur = int(sr * rng.uniform(0.08, 0.25))
        dur = min(dur, n - pos)
        if kind == "v":
            fmt = vowels[rng.integers(len(vowels))]
            src = np.zeros(dur)
            f = f0 * rng.uniform(0.85, 1.3)
            k = 0
            while k < dur:
                src[k] = 1.0
                k += int(sr / (f * (1 + 0.1 * math.sin(phase))))
                phase += 0.3
            src = np.convolve(src, np.exp(-np.arange(40) / 6.0))[:dur]
            y = src
            for F in fmt:
                if F < sr / 2 * 0.95:
                    y = _reson(y, F, 80 + F * 0.05, sr)
            y *= _env(dur, 0.02, 0.03, sr)
        elif kind == "f":
            y = rng.standard_normal(dur) * _env(dur, 0.01, 0.02, sr)
            y = np.diff(np.concatenate([[0], y]))
            y *= 0.15
        else:
            y = np.zeros(dur)
        x[pos:pos + dur] += y
        pos += dur
    return 0.7 * x / (np.max(np.abs(x)) + 1e-9)


def _reson(x, F, bw, sr):
    r = math.exp(-math.pi * bw / sr)
    a1 = -2 * r * math.cos(2 * math.pi * F / sr)
    a2 = r * r
    y = np.zeros_like(x)
    y1 = y2 = 0.0
    g = (1 - r)
    for i in range(len(x)):
        v = g * x[i] - a1 * y1 - a2 * y2
        y[i] = v
        y2, y1 = y1, v
    return y / (np.max(np.abs(y)) + 1e-9)


def sig_transient(sr, secs, seed=3):
    """A transient loop: clicks, castanet-like bursts, a snare and silence
    between, so a pre-echo has nowhere to hide."""
    rng = np.random.default_rng(seed)
    n = int(sr * secs)
    x = np.zeros(n)
    step = int(sr * 0.25)
    for k, p in enumerate(range(int(sr * 0.1), n - 2000, step)):
        kind = k % 4
        L = 1500
        tt = np.arange(L) / sr
        if kind == 0:
            x[p] += 0.9
        elif kind == 1:
            x[p:p + L] += 0.6 * rng.standard_normal(L) * np.exp(-tt * 120)
        elif kind == 2:
            x[p:p + L] += 0.7 * np.sin(2 * np.pi * 1800 * tt) * np.exp(-tt * 80)
        else:
            x[p:p + L] += 0.5 * (np.sin(2 * np.pi * 180 * tt) + 0.6 * rng.standard_normal(L)) * np.exp(-tt * 35)
    return np.clip(x, -0.95, 0.95)


def sig_tone(sr, secs, f, amp=0.7):
    t = np.arange(int(sr * secs)) / sr
    return amp * np.sin(2 * np.pi * f * t)


def sig_sweep(sr, secs, f0=30.0, f1=None, amp=0.7):
    f1 = f1 or sr / 2 * 0.98
    t = np.arange(int(sr * secs)) / sr
    k = math.log(f1 / f0) / secs
    return amp * np.sin(2 * np.pi * f0 * (np.exp(k * t) - 1) / k)


def sig_noise(sr, secs, seed=4, pink=True, amp=0.3):
    rng = np.random.default_rng(seed)
    n = int(sr * secs)
    w = rng.standard_normal(n)
    if pink:
        Wf = np.fft.rfft(w)
        f = np.arange(len(Wf)) + 1.0
        w = np.fft.irfft(Wf / np.sqrt(f), n)
    return amp * w / np.max(np.abs(w))


def sig_bass(sr, secs):
    t = np.arange(int(sr * secs)) / sr
    f = np.where((t % 1.0) < 0.5, 55.0, 82.4)
    return 0.8 * np.sin(2 * np.pi * np.cumsum(f) / sr)


def sig_clipstress(sr, secs):
    """Full-scale square-ish chords: what a loud master clips."""
    t = np.arange(int(sr * secs)) / sr
    x = np.zeros_like(t)
    for f in (110, 165, 220, 330):
        x += np.sign(np.sin(2 * np.pi * f * t)) * 0.35
    return np.clip(x * 1.0, -0.999, 0.999)


def sig_quiet(sr, secs, seed=6):
    """A quiet passage: the music phrase at -36 dB with a little noise."""
    return sig_music(sr, secs, seed=seed, key=3) * 10 ** (-36 / 20) + \
        sig_noise(sr, secs, seed=seed, amp=10 ** (-60 / 20))


def sig_silence(sr, secs):
    return np.zeros(int(sr * secs))


# ------------------------------------------------------------- metrics
def snr_db(ref, x):
    e = np.sum((ref - x) ** 2)
    s = np.sum(ref ** 2)
    if s == 0:
        return float("inf") if e == 0 else -float("inf")
    if e == 0:
        return float("inf")
    return 10 * math.log10(s / e)


def seg_snr_db(ref, x, n=256):
    """Segmental SNR over non-silent segments, each clamped to [-10, 60]."""
    L = (len(ref) // n) * n
    r = ref[:L].reshape(-1, n)
    d = (ref[:L] - x[:L]).reshape(-1, n)
    es = (r ** 2).sum(1)
    ed = (d ** 2).sum(1) + 1e-20
    keep = es > 1e-6 * n
    if not keep.any():
        return float("nan")
    v = np.clip(10 * np.log10(es[keep] / ed[keep] + 1e-20), -10, 60)
    return float(v.mean())


def to_pcm8(y):
    """The XT's sink: unsigned 8-bit, silence 128, saturated; returns
    (bytes as int array, clip count)."""
    v = np.round(y * 128.0)
    clips = int(np.sum((v > 127) | (v < -128)))
    v = np.clip(v, -128, 127)
    return (v + 128).astype(np.uint8), clips
