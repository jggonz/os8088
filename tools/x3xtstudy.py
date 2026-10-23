#!/usr/bin/env python3
"""x3xtstudy.py - the XTMP3 XT approximation study (SPEC.md 121.4, story S3,
docs/xtmp3/XT-STUDY.md).

Builds and MEASURES on the host every approximation the XT backend could
use, so that phase 2 implements them in 8088 assembly with evidence:

  maps      code maps for the synthesis window's V codes (uniform, mu-law,
            G.711 segments, an optimised segment shape, Lloyd-Max), 7/8/10
            bits, and the encoder table's shift
  palette   coefficient palettes for the window rows against exact rows
  dct       the matrixing in 16-bit `imul` against product tables
  imdct     the IMDCT, its window and the antialias in tables
  terms     8- and 4-term synthesis, taps chosen PER OUTPUT PHASE (first-N,
            middle, magnitude, exhaustive least-error, least-error retuned)
            with alias, pre-echo, ringing and DC/phase-ripple checks
  rate      2:1 and 4:1 output-phase-only synthesis with pruned subbands
  profiles  the combinations: XT Standard / Fast / Turbo / Speech
  huff      requantisation and Huffman statistics and the 8088 cost model
  cost      every kernel costed by tools/x3cyc.py, end to end per profile
  listen    docs/xtmp3/listen/*.wav and their README

`python3 tools/x3xtstudy.py --all` runs everything and writes
docs/xtmp3/xtstudy-results.json and docs/xtmp3/XT-STUDY-TABLES.md (both
GENERATED; XT-STUDY.md is the written account that cites them).
`--selfcheck` is the quick structural check (filterbank, hybrid, Lee DCT,
IMDCT folds, the table formula). Needs numpy.

Every speed figure here is a MODEL (tools/x3cyc.py). Every quality figure is
a host measurement on synthesised signals through an MP3-LIKE quantiser
(x3xtdsp.quantise), not through LAME; phase 2 re-measures on real fixtures.
"""
import argparse
import itertools
import json
import math
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import x3xtdsp as dsp  # noqa: E402
import x3xtmodel as xm  # noqa: E402
import x3cyc  # noqa: E402

REPO = os.path.dirname(HERE)
OUTDIR = os.path.join(REPO, "docs", "xtmp3")
LISTEN = os.path.join(OUTDIR, "listen")
SR = 11025
KBPS = 16
DELAY = dsp.FB_DELAY + 576          # filterbank + one granule of hybrid


# ================================================================ corpus
class Item:
    def __init__(self, name, x, sr=SR, kbps=KBPS, role="eval"):
        n = (len(x) // 576) * 576
        self.name, self.sr, self.kbps, self.role = name, sr, kbps, role
        self.x = np.asarray(x[:n], dtype=np.float64)
        self.S = dsp.analysis(self.x)
        self.bt = dsp.block_types(self.x, sr)
        self.X = dsp.hybrid_analysis(self.S, self.bt)
        self.Xq, self.IX, self.g = dsp.quantise(self.X, self.bt, sr, kbps)
        self._ref = {}

    def ref(self, div=1):
        if div not in self._ref:
            self._ref[div] = xm.decode_chain(self.Xq, self.bt, {"div": div})[0]
        return self._ref[div]


def corpus(secs=5.0, sr=SR, kbps=KBPS):
    """Training material (role 'train') chooses maps, palettes and taps;
    every reported figure is on the held-out 'eval' items."""
    I = []
    I.append(Item("t_music", dsp.sig_music(sr, secs, seed=11, key=2), sr, kbps, "train"))
    I.append(Item("t_speech", dsp.sig_speech(sr, secs, seed=12, f0=105.0), sr, kbps, "train"))
    I.append(Item("t_pink", dsp.sig_noise(sr, secs, seed=14, amp=0.5), sr, kbps, "train"))
    I.append(Item("t_trans", dsp.sig_transient(sr, secs, seed=13), sr, kbps, "train"))
    I.append(Item("t_quiet", dsp.sig_music(sr, secs, seed=15, key=5) * 0.1, sr, kbps, "train"))
    I.append(Item("music", dsp.sig_music(sr, secs, seed=1, key=0), sr, kbps))
    I.append(Item("speech", dsp.sig_speech(sr, secs, seed=2, f0=140.0), sr, kbps))
    I.append(Item("transient", dsp.sig_transient(sr, secs, seed=3), sr, kbps))
    I.append(Item("bass", dsp.sig_bass(sr, 3.0), sr, kbps))
    I.append(Item("hitone", dsp.sig_tone(sr, 3.0, 4500.0, 0.7), sr, kbps))
    I.append(Item("noise", dsp.sig_noise(sr, 3.0, seed=5, pink=False, amp=0.4), sr, kbps))
    I.append(Item("clipstress", dsp.sig_clipstress(sr, 3.0), sr, kbps))
    I.append(Item("quiet", dsp.sig_music(sr, secs, seed=1, key=0) * 10 ** (-30 / 20), sr, kbps))
    I.append(Item("silence", dsp.sig_silence(sr, 2.0), sr, kbps))
    return I


EVAL_CORE = ("music", "speech", "transient")


# =============================================================== metrics
def compare(ref, y):
    """Waveform error of y against the float reference decode of the SAME
    quantised data (the approximation's own error, not the codec's)."""
    n = min(len(ref), len(y))
    ref, y = ref[:n], y[:n]
    e = y - ref
    den = float(np.dot(ref, ref))
    gain = float(np.dot(ref, y) / den) if den > 0 else 1.0
    p8r, cr = dsp.to_pcm8(ref)
    p8y, cy = dsp.to_pcm8(y)
    return {
        "snr": round(dsp.snr_db(ref, y), 2),
        "segsnr": round(dsp.seg_snr_db(ref, y), 2),
        "peak": round(float(np.max(np.abs(e))) if n else 0.0, 5),
        "peak_lsb8": round(float(np.max(np.abs(e))) * 128, 2) if n else 0.0,
        "dc": round(float(np.mean(e)) * 128, 3),         # in 8-bit LSBs
        "gain_db": round(20 * math.log10(abs(gain)), 3) if gain else None,
        "clips": int(cy),
        "clips_ref": int(cr),
        "pcm8_diff": round(float(np.mean(p8r != p8y)) * 100, 1),
    }


def band_esr(ref, y, sr):
    """Error-to-signal ratio in each octave band (dB), over the whole clip:
    where an approximation's error would be heard is the band where it
    comes nearest the signal. Returns (worst dB, its band's centre Hz)."""
    n = min(len(ref), len(y))
    if n < 1024:
        return None, None
    R_ = np.abs(np.fft.rfft(ref[:n])) ** 2
    E = np.abs(np.fft.rfft(y[:n] - ref[:n])) ** 2
    f = np.fft.rfftfreq(n, 1.0 / sr)
    worst, at = -200.0, None
    lo = 62.5
    while lo * 2 <= sr / 2:
        m = (f >= lo) & (f < lo * 2)
        if R_[m].sum() > 1e-9 * R_.sum() and E[m].sum() > 0:
            v = 10 * math.log10(E[m].sum() / R_[m].sum())
            if v > worst:
                worst, at = v, lo * math.sqrt(2)
        lo *= 2
    return round(worst, 1), (round(at) if at else None)


def pcm8_floor(ref):
    """What rounding the reference itself to 8 bits costs: the floor any XT
    output sits on."""
    p, _ = dsp.to_pcm8(ref)
    return round(dsp.snr_db(ref, (p.astype(np.float64) - 128) / 128.0), 2)


def band_stats(ref, y, sr, lo=125.0):
    """(worst, mean) octave-band error-to-signal ratio in dB, 125 Hz up."""
    n = min(len(ref), len(y))
    if n < 1024:
        return None, None
    Rf = np.abs(np.fft.rfft(ref[:n])) ** 2
    E = np.abs(np.fft.rfft(y[:n] - ref[:n])) ** 2
    f = np.fft.rfftfreq(n, 1.0 / sr)
    v = []
    while lo * 2 <= sr / 2:
        m = (f >= lo) & (f < lo * 2)
        if Rf[m].sum() > 1e-9 * Rf.sum() and Rf[m].sum() > 0:
            v.append(10 * math.log10(E[m].sum() / Rf[m].sum() + 1e-30))
        lo *= 2
    if not v:
        return None, None
    return round(max(v), 1), round(float(np.mean(v)), 1)


def run_cfg(items, cfg, names=None, div=1):
    res = {}
    for it in items:
        if it.role != "eval" or (names and it.name not in names):
            continue
        st = xm.Stats()
        y, st = xm.decode_chain(it.Xq, it.bt, dict(cfg, div=div), st)
        r = compare(it.ref(div), y)
        r["band_worst"], r["band_mean"] = band_stats(it.ref(div), y, it.sr / div)
        r["wraps"] = sum(st.wraps.values())
        r["sats"] = sum(st.sats.values())
        res[it.name] = r
    return res


def summary(res, names=EVAL_CORE, key="snr"):
    v = [res[n][key] for n in names if n in res and res[n].get(key) is not None
         and math.isfinite(res[n][key])]
    return round(float(np.mean(v)), 2) if v else None


# ================================================ stage training data
def stage_values(items, stage, roles=("train",)):
    """Float values that reach a stage's coder on the training items, each
    item weighted by the inverse of its energy (so a quiet excerpt counts
    by its relative error). stage: 'V' (window input), 'S' (matrixing
    input), 'R' (requantised lines), 'Z' (IMDCT outputs before the window),
    'D' (Lee multiply inputs)."""
    vals, wts = [], []
    for it in items:
        if it.role not in roles:
            continue
        if stage == "V":
            Sf = dsp.hybrid_synthesis(it.Xq, it.bt)
            v = dsp.synth_V(Sf).ravel()
        elif stage == "S":
            v = dsp.hybrid_synthesis(it.Xq, it.bt).ravel()
        elif stage == "R":
            v = it.Xq.ravel()
        elif stage == "Z":
            Xa = dsp.alias_reduce_dec(it.Xq, it.bt)
            v = (Xa.reshape(-1, 18) @ xm.DCT4_18.T).ravel()
        elif stage == "D":
            Sf = dsp.hybrid_synthesis(it.Xq, it.bt)
            acc = []
            xm.DctPlan(32).run(Sf, lambda v_, c, n: (acc.append(v_.copy()), v_ * c)[1])
            v = np.concatenate(acc)
        else:
            raise ValueError(stage)
        e = float(np.mean(v * v)) + 1e-12
        if len(v) > 60000:          # a fixed subsample keeps the searches quick
            v = v[np.random.default_rng(len(v)).choice(len(v), 60000, replace=False)]
        vals.append(v)
        wts.append(np.full(len(v), 1.0 / (e * len(v))))
    return np.concatenate(vals), np.concatenate(wts)


# ================================================================ maps
def build_maps(items, stage, q, vmax):
    data, w = stage_values(items, stage)
    maps = {}
    maps["lin8"] = xm.map_uniform(8, vmax, q)
    maps["lin10"] = xm.map_uniform(10, vmax, q)
    maps["mu8"] = xm.map_mu(8, vmax, q)
    maps["mu10"] = xm.map_mu(10, vmax, q)
    for bits in (7, 8, 10):
        maps["optseg%d" % bits] = xm.map_seg_opt(bits, data, w, q, vmax)
    maps["lloyd8"] = xm.map_lloyd(8, data, w, q, vmax=vmax)
    # the G.711 shape with its finest step at one LSB of q
    maps["g711_8"] = xm.map_g711(8, q, 0)
    return maps


def exp_maps(items, R):
    """The window's V code: which map, how many bits, which encoder shift.
    Everything else in float, so this is the window stage alone."""
    out = {"q_v": xm.Q_V, "rows": []}
    maps = build_maps(items, "V", xm.Q_V, 2.0)
    out["maps"] = {k: describe_map(m) for k, m in maps.items()}
    for name, cm in maps.items():
        bank = xm.make_win_bank(cm, dsp.DTAP)
        for s in (0, 1, 2, 3, 4):
            cfg = {"win": "tab", "win_bank": bank, "s_win": s}
            res = run_cfg(items, cfg)
            nrows = bank[0].rows.shape[0]
            out["rows"].append({
                "map": name, "bits": cm.bits, "s": s,
                "enc_bytes": 1 << (16 - s),
                "rows": nrows,
                "table_bytes": nrows * (1 << cm.bits) * 2,
                "snr_core": summary(res),
                "res": res})
    R["maps"] = out
    return maps


def describe_map(cm):
    d = {"name": cm.name, "bits": cm.bits, "levels": int(cm.L),
         "q": cm.q, "vmax": round(cm.vmax, 4),
         "finest": int(cm.steps()[0]), "coarsest": int(cm.steps().max())}
    if hasattr(cm, "seg"):
        d["seg"] = {"lens": list(cm.seg[0]), "s0": cm.seg[1]}
    return d


def exp_palette(items, R, cm, s_win):
    """Coefficient palettes for the window rows: K representatives chosen
    error-aware (importance = expected tap-input energy), against the exact
    distinct |D| rows."""
    out = []
    # importance of each tap: E[u^2] from training
    U2 = np.zeros((32, 16))
    for it in items:
        if it.role != "train":
            continue
        Sf = dsp.hybrid_synthesis(it.Xq, it.bt)
        U = dsp.synth_taps(dsp.synth_V(Sf))
        U2 += (U ** 2).mean(axis=0)
    for K in (None, 192, 128, 96, 64, 32):
        if K is None:
            coef = dsp.DTAP
            reps = np.unique(np.abs(coef)[np.abs(coef) > 0])
        else:
            coef, reps = xm.palette(dsp.DTAP, K, weight=U2 * 1.0)
        # float window alone: coefficient error only
        rf = run_cfg(items, {"coef": coef})
        bank = xm.make_win_bank(cm, coef)
        rt = run_cfg(items, {"coef": coef, "win": "tab", "win_bank": bank, "s_win": s_win})
        out.append({"K": K if K else len(reps), "exact": K is None,
                    "rows": int(bank[0].rows.shape[0]),
                    "table_bytes": int(bank[0].rows.shape[0]) * (1 << cm.bits) * 2,
                    "snr_coef_only": summary(rf), "snr_with_code": summary(rt),
                    "res": rt})
    R["palette"] = out


def exp_dct(items, R):
    """Matrixing: Lee's 80 multiplies in 16-bit imul vs product tables with
    a code at every multiply input (8 and 10 bits)."""
    out = []
    res = run_cfg(items, {"dct": "int"})
    out.append({"form": "imul Q11", "snr_core": summary(res), "res": res,
                "table_bytes": 0})
    maps = build_maps(items, "D", xm.Q_S, 12.0)
    for name in ("optseg8", "optseg10", "mu8", "mu10", "lin10"):
        cm = maps[name]
        for s in (0, 2):
            res = run_cfg(items, {"dct": "tab", "map_dct": cm, "s_dct": s})
            out.append({"form": "tab " + name, "s": s, "snr_core": summary(res),
                        "res": res,
                        "table_bytes": 31 * (1 << cm.bits) * 2,
                        "enc_bytes": 1 << (16 - s)})
    R["dct"] = out


def exp_imdct(items, R):
    """The hybrid stage in its XT forms, each alone."""
    out = []
    mR = build_maps(items, "R", xm.Q_R, 2.0)
    mZ = build_maps(items, "Z", xm.Q_R, 2.0)
    for label, cfg in (
            ("antialias imul", {"aa": "int"}),
            ("antialias skipped", {"aa": "skip"}),
            ("imdct imul + window imul", {"imdct": "int", "imdctwin": "int"})):
        res = run_cfg(items, cfg)
        out.append({"form": label, "snr_core": summary(res), "res": res})
    for name in ("optseg8", "optseg10", "mu8", "lloyd8"):
        res = run_cfg(items, {"aa": "tab", "map_aa": mR[name]})
        out.append({"form": "antialias tab " + name, "snr_core": summary(res), "res": res,
                    "table_bytes": 16 * (1 << mR[name].bits) * 2})
        res = run_cfg(items, {"imdct": "tab", "imdctwin": "int", "map_im": mR[name], "s_im": 0})
        out.append({"form": "imdct tab " + name + " (window imul)", "snr_core": summary(res),
                    "res": res, "table_bytes": 18 * (1 << mR[name].bits) * 2})
        res = run_cfg(items, {"imdct": "int", "imdctwin": "tab", "map_iw": mZ[name], "s_iw": 0})
        out.append({"form": "imdct imul, window tab " + name, "snr_core": summary(res),
                    "res": res, "table_bytes": 24 * (1 << mZ[name].bits) * 2})
        res = run_cfg(items, {"imdct": "tab", "imdctwin": "tab", "map_im": mR[name],
                              "map_iw": mZ[name]})
        out.append({"form": "imdct tab + window tab " + name, "snr_core": summary(res),
                    "res": res, "table_bytes": 42 * (1 << mR[name].bits) * 2})
    R["imdct"] = out
    R["imdct_maps"] = {"R": {k: describe_map(v) for k, v in mR.items()},
                       "Z": {k: describe_map(v) for k, v in mZ.items()}}
    return mR, mZ


# ============================================================ term choice
def mirror_of(j):
    """Output phase j's taps share |D| with phase j' tap i' (positions p and
    512 - p): j >= 1 -> (32 - j, 15 - i); j == 0 -> itself, i <-> 16 - i."""
    if j == 0:
        return 0, [None] + [16 - i for i in range(1, 16)]
    return 32 - j, [15 - i for i in range(16)]


def tap_corr(items, roles=("train",)):
    """R[j] = E[u u^T] of the 16 tap inputs of output phase j."""
    R = np.zeros((32, 16, 16))
    n = 0
    for it in items:
        if it.role not in roles:
            continue
        Sf = dsp.hybrid_synthesis(it.Xq, it.bt)
        U = dsp.synth_taps(dsp.synth_V(Sf))
        Un = U / (np.sqrt((U ** 2).mean()) + 1e-12)       # each item counts equally
        R += np.einsum("tji,tjk->jik", Un, Un) / U.shape[0]
        n += 1
    return R / max(n, 1)


def _subsets(n_taps, n):
    return np.array(list(itertools.combinations(range(n_taps), n)))


class BandObjective:
    """The 'optband' selection's objective, on the TRAINING items: the mean,
    over items and octave bands from 125 Hz to Nyquist, of the band's
    error-to-signal ratio in dB. Equal weight for every octave, so a
    selection cannot buy total SNR by pushing its error into the octaves
    where music is quiet (which the MSE-optimal 'opt' does)."""

    def __init__(self, items):
        self.U, self.Y, self.S, self.masks = [], [], [], []
        for it in items:
            if it.role != "train":
                continue
            Sf = dsp.hybrid_synthesis(it.Xq, it.bt)
            U = dsp.synth_taps(dsp.synth_V(Sf))
            y = np.einsum("tji,ji->tj", U, dsp.DTAP).reshape(-1)
            Yf = np.abs(np.fft.rfft(y)) ** 2
            f = np.fft.rfftfreq(len(y), 1.0 / it.sr)
            ms = []
            lo = 125.0
            while lo * 2 <= it.sr / 2:
                m = (f >= lo) & (f < lo * 2)
                if Yf[m].sum() > 1e-12:
                    ms.append(m)
                lo *= 2
            self.U.append(U)
            self.Y.append(y)
            self.S.append([Yf[m].sum() for m in ms])
            self.masks.append(ms)

    def __call__(self, coef):
        tot, n = 0.0, 0
        for U, y, S, ms in zip(self.U, self.Y, self.S, self.masks):
            e = np.einsum("tji,ji->tj", U, coef).reshape(-1) - y
            E = np.abs(np.fft.rfft(e)) ** 2
            for m, s in zip(ms, S):
                tot += 10 * math.log10(E[m].sum() / s + 1e-30)
                n += 1
        return tot / max(n, 1)


def choose_terms_band(R, n, obj, cand=12, passes=2):
    """'optband': start from 'opt', then coordinate descent over the mirror
    groups, each trying its `cand` least-MSE subsets, keeping the one that
    lowers BandObjective. Coefficients unchanged (no retuning)."""
    D = dsp.DTAP
    coef = choose_terms(R, n, "opt")
    groups = []
    seen = set()
    for j in range(32):
        jm, mp = mirror_of(j)
        if j in seen:
            continue
        seen |= {j, jm}
        groups.append((j, jm, mp))
    best = obj(coef)
    for _ in range(passes):
        for j, jm, mp in groups:
            live = [i for i in range(16) if D[j, i] != 0]
            Rj = R[j] if j == jm else R[j] + _mirror_R(R, j, jm, mp)
            subs = _subsets(len(live), n)
            L = np.array(live)[subs]
            drop = np.ones((len(L), 16), dtype=bool)
            np.put_along_axis(drop, L, False, axis=1)
            Dd = np.where(drop, D[j][None, :], 0.0)
            err = np.einsum("si,ik,sk->s", Dd, Rj, Dd)
            for si in np.argsort(err)[:cand]:
                trial = coef.copy()
                trial[j] = 0
                trial[j, L[si]] = D[j, L[si]]
                if jm != j:
                    trial[jm] = 0
                    for i in L[si]:
                        trial[jm, mp[i]] = D[jm, mp[i]]
                v = obj(trial)
                if v < best - 1e-9:
                    best, coef = v, trial
    return coef


def _mirror_R(R, j, jm, mp):
    D = dsp.DTAP
    P = np.zeros((16, 16))
    sg = np.zeros(16)
    for i in range(16):
        P[i, mp[i]] = 1
        sg[i] = np.sign(D[jm, mp[i]]) * np.sign(D[j, i]) if D[j, i] else 0
    Rm = P @ R[jm] @ P.T
    return sg[:, None] * Rm * sg[None, :]


def choose_terms(R, n, method):
    """Coefficients (32,16) keeping n taps a phase. methods:
    'first'  taps 0..n-1 (the strawman the brief rules out)
    'mid'    the n taps around the window's centre (plain truncation)
    'mag'    the n largest |D| of the phase
    'opt'    exhaustive least-error subset, coefficients unchanged
    'ls'     exhaustive least-error subset with the kept coefficients
             RETUNED by least squares on the training correlations
             (objective: E[(y_16 - y_n)^2], each training item weighted
             equally)
    'lsdc'   'ls' with each phase's DC gain held to the full filter's.
    'optdc'  'opt', then each phase's kept taps scaled by one factor so its
             DC gain is the full filter's (no retuning beyond that).
    Mirror phases (j, 32 - j) are chosen jointly with mirrored taps, so
    they share product rows; phases 0 and 16 are their own mirrors."""
    coef = np.zeros((32, 16))
    D = dsp.DTAP
    udc = dc_tap_inputs()
    done = set()
    for j in range(32):
        if j in done:
            continue
        jm, mp = mirror_of(j)
        live = [i for i in range(16) if D[j, i] != 0]
        if j == jm:
            Rj = R[j]
        else:
            # bring the mirror's statistics onto j's taps (with the sign
            # relation of the shared magnitudes)
            P = np.zeros((16, 16))
            sg = np.zeros(16)
            for i in range(16):
                P[i, mp[i]] = 1
                sg[i] = np.sign(D[jm, mp[i]]) * np.sign(D[j, i]) if D[j, i] else 0
            Rm = P @ R[jm] @ P.T
            Rj = R[j] + (sg[:, None] * Rm * sg[None, :])
        Dj = D[j]
        if method == "first":
            K = [i for i in range(16) if D[j, i] != 0][:n]
        elif method == "mid":
            c = 7.5 if j else 8.0
            K = sorted(sorted(live, key=lambda i: (abs(i - c), i))[:n])
        elif method == "mag":
            K = sorted(sorted(live, key=lambda i: -abs(Dj[i]))[:n])
        else:
            subs = _subsets(len(live), n)
            L = np.array(live)[subs]                       # (S, n)
            if method in ("opt", "optdc"):
                drop = np.ones((len(L), 16), dtype=bool)
                np.put_along_axis(drop, L, False, axis=1)
                Dd = np.where(drop, Dj[None, :], 0.0)
                err = np.einsum("si,ik,sk->s", Dd, Rj, Dd)
            else:
                RKK = Rj[L[:, :, None], L[:, None, :]]
                RKa = Rj[L, :] @ Dj
                sol = np.linalg.solve(RKK + 1e-12 * np.eye(n)[None], RKa[..., None])[..., 0]
                err = Dj @ Rj @ Dj - np.einsum("sk,sk->s", RKa, sol)
            K = list(L[int(np.argmin(err))])
        kc = np.zeros(16)
        if method == "optdc":
            a = udc[j]
            kc[K] = Dj[K]
            g = (a @ Dj) / (a[K] @ Dj[K]) if abs(a[K] @ Dj[K]) > 1e-9 else 1.0
            kc[K] *= g
        elif method in ("ls", "lsdc"):
            Ki = np.array(K)
            A = Rj[np.ix_(Ki, Ki)]
            b = Rj[Ki, :] @ Dj
            if method == "lsdc":
                # least squares with a_K . d = a . D (Lagrange), a = DC inputs
                a = udc[j]
                M = np.zeros((n + 1, n + 1))
                M[:n, :n] = A
                M[:n, n] = a[Ki]
                M[n, :n] = a[Ki]
                rhs = np.concatenate([b, [a @ Dj]])
                d = np.linalg.solve(M, rhs)[:n]
            else:
                d = np.linalg.solve(A, b)
            kc[Ki] = d
        else:
            kc[K] = Dj[K]
        coef[j] = kc
        done.add(j)
        if jm != j:
            for i in range(16):
                if kc[i] != 0:
                    coef[jm, mp[i]] = abs(kc[i]) * np.sign(D[jm, mp[i]]) * np.sign(kc[i]) * np.sign(D[j, i])
            done.add(jm)
    return coef


_UDC = None


def dc_tap_inputs():
    """The steady-state tap inputs for a constant input (u_dc[j, i])."""
    global _UDC
    if _UDC is None:
        S = dsp.analysis(np.full(32 * 200, 0.5))
        U = dsp.synth_taps(dsp.synth_V(S))
        _UDC = U[150] / 0.5
    return _UDC


def tone_spurs(coef=None, phases=None, sr=SR, ntones=40, div=1, prune=32):
    """Each tone through analysis then synthesis: the largest spurious
    component (anything outside +-8 bins of the tone, Blackman-Harris) relative to the tone,
    in dB. The full filter's own figure is the floor (its aliasing)."""
    out = []
    N = 8192 * 2
    fmax = sr / 2 / div * 0.97
    fr = np.exp(np.linspace(math.log(60), math.log(fmax), ntones))
    M = N // div
    nn = np.arange(M)
    win = (0.35875 - 0.48829 * np.cos(2 * np.pi * nn / M) + 0.14128 * np.cos(4 * np.pi * nn / M)
           - 0.01168 * np.cos(6 * np.pi * nn / M))          # Blackman-Harris: -92 dB sidelobes
    for f in fr:
        x = dsp.sig_tone(sr, (N + 2048) / sr, f, 0.5)
        S = dsp.analysis(x[:len(x) // 32 * 32])
        S[:, prune:] = 0
        y = dsp.synthesis(S, coef, phases)
        seg = y[2048 // div:2048 // div + N // div]
        Y = np.abs(np.fft.rfft(seg * win))
        k = int(round(f / (sr / div) * (N // div)))
        tone = Y[max(0, k - 8):k + 9].max()
        mask = np.ones(len(Y), dtype=bool)
        mask[max(0, k - 8):k + 9] = False
        spur = Y[mask].max()
        out.append(20 * math.log10(spur / tone + 1e-30))
    out = np.array(out)
    return {"worst": round(float(out.max()), 1), "median": round(float(np.median(out)), 1),
            "freqs": [round(float(f), 1) for f in fr], "spurs": [round(float(v), 1) for v in out]}


def click_test(coef=None, phases=None, div=1, prune=32):
    """A single click: energy before the peak (pre-echo) and after it
    (ringing), each relative to the whole response, in dB, and the error
    against the full filter's response."""
    x = np.zeros(32 * 400)
    p0 = 32 * 150 + 7
    x[p0] = 0.8
    S = dsp.analysis(x)
    S[:, prune:] = 0
    y = dsp.synthesis(S, coef, phases)
    yr = dsp.synthesis(S, None, phases)
    pk = int(np.argmax(np.abs(yr)))
    w = 256 // div
    tot = np.sum(yr ** 2)

    def band(v, a, b):
        return 10 * math.log10(np.sum(v[max(0, pk + a):pk + b] ** 2) / tot + 1e-30)
    return {"pre": round(band(y, -w, -8 // div), 1), "pre_ref": round(band(yr, -w, -8 // div), 1),
            "ring": round(band(y, 8 // div, w), 1), "ring_ref": round(band(yr, 8 // div, w), 1),
            "err": round(10 * math.log10(np.sum((y - yr) ** 2) / tot + 1e-30), 1)}


def dc_test(coef=None, phases=None, prune=32):
    """A constant 0.5 in: the output's gain error (dB) and the ripple of its
    32-sample pattern relative to the DC (dB) - the phase-gain mismatch
    that turns into a tone at k * fs/32 (344.5 Hz at 11,025)."""
    S = dsp.analysis(np.full(32 * 400, 0.5))
    S[:, prune:] = 0
    y = dsp.synthesis(S, coef, phases)
    seg = y[len(y) // 2:len(y) // 2 + 32 * 64]
    m = float(np.mean(seg))
    rip = float(np.std(seg))
    return {"gain_db": round(20 * math.log10(abs(m) / 0.5), 3),
            "ripple_db": round(20 * math.log10(rip / abs(m) + 1e-30), 1)}


def exp_terms(items, R_out, obj=None):
    R = tap_corr(items)
    obj = obj or BandObjective(items)
    out = []
    coefs = {}
    for n in (16, 8, 4):
        for method in (("full",) if n == 16 else ("first", "mid", "mag", "opt", "optband", "optdc", "ls", "lsdc")):
            if n == 16:
                coef = dsp.DTAP
            elif method == "optband":
                coef = choose_terms_band(R, n, obj)
            else:
                coef = choose_terms(R, n, method)
            coefs[(n, method)] = coef
            res = run_cfg(items, {"coef": coef})
            row = {"n": n, "method": method, "snr_core": summary(res), "res": res,
                   "band_worst_core": summary(res, key="band_worst"),
                   "band_mean_core": summary(res, key="band_mean"),
                   "alias": tone_spurs(coef), "click": click_test(coef), "dc": dc_test(coef),
                   "rows": int(len(np.unique(np.round(np.abs(coef[coef != 0]), 12)))),
                   "max_gain": round(float(np.abs(coef).sum(axis=1).max() / np.abs(dsp.DTAP).sum(axis=1).max()), 3)}
            out.append(row)
    R_out["terms"] = out
    return coefs


def tone_gain(f, div, prune, sr=SR):
    """A tone through analysis and the reduced-rate synthesis: its output
    amplitude against its input (dB), by a least-squares sinusoid fit. A
    PQMF alias at the new Nyquist folds back ONTO the tone, so near the band
    edge it shows here as a gain error and not as a spur."""
    x = dsp.sig_tone(sr, 1.5, f, 0.5)
    S = dsp.analysis(x[:len(x) // 32 * 32])
    S[:, prune:] = 0
    y = dsp.synthesis(S, None, list(range(0, 32, div)))
    t = np.arange(len(y)) * div / sr
    seg = slice(len(y) // 4, len(y) * 3 // 4)
    A = np.stack([np.sin(2 * np.pi * f * t[seg]), np.cos(2 * np.pi * f * t[seg])], 1)
    c, *_ = np.linalg.lstsq(A, y[seg], rcond=None)
    return 20 * math.log10(math.hypot(*c) / 0.5 + 1e-30)


def decimate_ref(ref_full, div, phase0=0):
    """The ideal reduced-rate output: the full-rate float decode, brick-wall
    low-passed at 0.9 x the new Nyquist, then sampled on the kept phases."""
    Y = np.fft.rfft(ref_full)
    f = np.fft.rfftfreq(len(ref_full))
    Y[f > 0.5 / div * 0.9] = 0
    return np.fft.irfft(Y, len(ref_full))[phase0::div]


def exp_rate(items, R_out):
    out = []
    for div in (2, 4):
        phases = list(range(0, 32, div))
        prune = 32 // div
        res = {}
        for it in items:
            if it.role != "eval":
                continue
            y, _ = xm.decode_chain(it.Xq, it.bt, {"div": div})
            ideal = decimate_ref(it.ref(1), div)
            res[it.name] = compare(ideal, y)
            res[it.name]["vs_lowpass"] = res[it.name]["snr"]
        out.append({"div": div, "out_rate_11025": round(SR / div, 1),
                    "bandwidth_hz": round(SR / 2 / div, 1),
                    "snr_core_vs_lowpassed_full": summary(res), "res": res,
                    "alias": tone_spurs(None, phases, div=div, prune=prune),
                    "click": click_test(None, phases, div=div, prune=prune),
                    "dc": dc_test(None, phases, prune=prune),
                    "edge_gain": [(f, round(tone_gain(f, div, prune), 2)) for f in
                                  [SR / 2 / div * r for r in (0.5, 0.8, 0.9, 0.95, 0.98)]]})
    R_out["rate"] = out


# ========================================================== stream stats
def imdct_costs():
    """(one nonzero line through the sparse table IMDCT, one subband through
    a fast factored IMDCT with byte-split multiplies) in model clocks."""
    import x3xtkern as K_
    c_line = K_.cost(K_.imdct_sparse_line())[0]
    L, lm, la = K_.lee_code(32, K_.mul_split())
    per = K_.cost(L, 2, 1)[0]
    # price the fast IMDCT's ops at Lee's own per-op mix: a multiply node
    # and an add node in the ratio the Lee code measured
    mul_c = K_.cost(["mov ax,[di+4]"] + K_.mul_split() + ["mov [di+6],dx"])[0]
    add_c = (per - lm * mul_c) / la
    m, a = K_.imdct_fast_split_ops()
    return c_line, m * mul_c + a * add_c


def granule_stats(items, roles=("eval",), names=None):
    """What a granule of THIS material asks of each stage: nonzero lines
    before and after the antialias, active subbands (anything nonzero
    after the antialias, or overlap left from the last granule), active
    antialias boundaries, the |ix| spread, short-block share, and the
    modelled Huffman codeword count (pairs in big_values, quads in
    count1)."""
    acc = {k: [] for k in ("nz", "nz_aa", "sb_active", "sb_active_ola", "aa_bound",
                           "ix_le1", "ix_le15", "ix_gt15", "short", "pairs", "quads",
                           "bv_end", "maxsb", "imdct_hybrid_clk")}
    c_line, c_fast = imdct_costs()
    for it in items:
        if it.role not in roles or (names and it.name not in names):
            continue
        Xa = dsp.alias_reduce_dec(it.Xq, it.bt)
        prev_active = np.zeros(32, dtype=bool)
        for g in range(len(it.bt)):
            ix = np.abs(it.IX[g].reshape(-1))
            nzm = ix != 0
            acc["nz"].append(int(nzm.sum()))
            nza = np.abs(Xa[g].reshape(-1)) > 1e-12
            acc["nz_aa"].append(int(nza.sum()))
            act = nza.reshape(32, 18).any(axis=1)
            nnz_sb = nza.reshape(32, 18).sum(axis=1)
            acc["imdct_hybrid_clk"].append(float(np.minimum(nnz_sb * c_line, np.where(nnz_sb > 0, c_fast, 0)).sum()))
            acc["sb_active"].append(int(act.sum()))
            acc["sb_active_ola"].append(int((act | prev_active).sum()))
            prev_active = act
            nb = 0
            if it.bt[g] != 2:
                for sb in range(1, 32):
                    if nzm[18 * sb - 8:18 * sb + 8].any():
                        nb += 1
            acc["aa_bound"].append(nb)
            nzv = ix[nzm]
            acc["ix_le1"].append(int((nzv <= 1).sum()))
            acc["ix_le15"].append(int(((nzv > 1) & (nzv <= 15)).sum()))
            acc["ix_gt15"].append(int((nzv > 15).sum()))
            acc["short"].append(int(it.bt[g] == 2))
            last = np.nonzero(nzm)[0]
            last = last[-1] + 1 if len(last) else 0
            big = np.nonzero(ix[:last] > 1)[0]
            bvend = (big[-1] + 2) // 2 * 2 if len(big) else 0
            acc["bv_end"].append(int(bvend))
            acc["pairs"].append(bvend // 2)
            acc["quads"].append(int(math.ceil((last - bvend) / 4.0)) if last > bvend else 0)
            acc["maxsb"].append(int((last + 17) // 18))
    return {k: round(float(np.mean(v)), 2) for k, v in acc.items() if v}


# ============================================================ cost model
import x3xtkern as K  # noqa: E402

CPU_HZ = x3cyc.CPU_HZ
PARSE_PER_GRANULE = 12000   # header, side info, scalefactors, reservoir
                            # copy: an ESTIMATE, not a kernel (phase 2)


def huff_lengths(items, names=None):
    """Ideal code lengths from this material's pair statistics: the
    fraction of big_values codewords longer than L bits (4, 6, 8), and of
    count1 quads longer than 4. A MODEL of Layer III's tables (whose
    lengths x3sim.py holds); phase 2 recounts with the real tables."""
    pairs = {}
    quads = {}
    for it in items:
        if it.role != "eval" or (names and it.name not in names):
            continue
        for g in range(len(it.bt)):
            ix = np.abs(it.IX[g].reshape(-1))
            nz = np.nonzero(ix)[0]
            if not len(nz):
                continue
            last = nz[-1] + 1
            big = np.nonzero(ix[:last] > 1)[0]
            bv = (big[-1] + 2) // 2 * 2 if len(big) else 0
            for i in range(0, bv, 2):
                k = (min(ix[i], 15), min(ix[i + 1], 15))
                pairs[k] = pairs.get(k, 0) + 1
            for i in range(bv, last, 4):
                k = tuple(int(v) for v in ix[i:i + 4])
                quads[k] = quads.get(k, 0) + 1
    out = {}
    tot = sum(pairs.values())
    lens = {k: max(1, math.ceil(-math.log2(v / tot))) for k, v in pairs.items()}
    for L in (4, 6, 8):
        out["pairs_over_%d" % L] = round(sum(pairs[k] for k in pairs if lens[k] > L) / tot, 4)
    out["pairs_mean_len"] = round(sum(pairs[k] * lens[k] for k in pairs) / tot, 2)
    qt = sum(quads.values()) or 1
    ql = {k: max(1, math.ceil(-math.log2(v / qt))) for k, v in quads.items()}
    out["quads_over_4"] = round(sum(quads[k] for k in quads if ql[k] > 4) / qt, 4)
    out["quads_mean_len"] = round(sum(quads[k] * ql[k] for k in quads) / qt, 2)
    return out


def huff_table_bytes(L):
    """15 distinct big_values tables and 2 count1 tables as word entries
    of an L-bit first level, plus bounded secondary tables for the longer
    codes (Layer III's longest is 19 bits): an estimate."""
    first = 15 * (1 << L) * 2 + 2 * (1 << min(L, 6)) * 2
    second = {4: 9000, 6: 6000, 8: 4000}[L]
    return first + second


PROFILES = {
    # name: numeric configuration and kernel choices. 'n' window taps an
    # output; 'div' the output phase divisor; the hybrid and matrixing forms.
    "XT Standard": dict(n=16, div=1, layout="lodsw", dct="split", aa="split",
                        imdct="hybrid", imdctwin="split"),
    "XT Fast": dict(n=8, div=1, layout="rel8", dct="split", aa="tab",
                    imdct="hybrid", imdctwin="tab"),
    "XT Turbo": dict(n=4, div=1, layout="rel8", dct="split", aa="tab",
                     imdct="hybrid", imdctwin="tab"),
    "XT Speech": dict(n=16, div=2, layout="lodsw", dct="split", aa="split",
                      imdct="hybrid", imdctwin="split"),
    "XT Speech 4:1": dict(n=16, div=4, layout="lodsw", dct="split", aa="split",
                          imdct="hybrid", imdctwin="split"),
}
# the STANDARD decoder's arithmetic (16-bit `imul` everywhere, Q15 rounded
# products, 32-bit window sums) priced on the same 8088: what the XT
# profiles are measured against on the slow machine
STANDARD_8088 = dict(n=16, div=1, layout="imul", dct="imul", aa="imul",
                     imdct="fastimul", imdctwin="imul")


def stage_cycles(p, gs, hl, huffL=8):
    """Cycles a granule by stage for profile p over granule stats gs.
    Every term is (units a granule) x (the kernel's modelled cost)."""
    div = p["div"]
    nout = 32 // div
    st = {}
    # Huffman: big_values pairs + count1 quads + sign bits + refill
    cw = K.cost(K.huff_codeword(huffL))[0]
    cw6 = K.cost(K.huff_codeword(6))[0]
    sec = K.cost(K.huff_second())[0]
    sign = K.cost(K.huff_sign())[0]
    refill = K.cost(K.huff_refill())[0]
    bits = gs["pairs"] * hl["pairs_mean_len"] + gs["quads"] * hl["quads_mean_len"] + gs["nz"]
    st["huffman"] = (gs["pairs"] * (cw + hl["pairs_over_%d" % huffL] * sec)
                     + gs["quads"] * (cw6 + (hl["quads_over_4"] * sec if huffL < 6 else 0))
                     + gs["nz"] * sign + bits / 8.0 * refill)
    st["parse"] = PARSE_PER_GRANULE
    # requantisation (only lines below the pruned subbands are rebuilt)
    frac = min(1.0, (32 // div) / max(gs["maxsb"], 1)) if div > 1 else 1.0
    nz = gs["nz"] * frac
    big = gs["ix_gt15"] * frac
    st["requant"] = (nz - big) * K.cost(K.requant_small())[0] + big * K.cost(K.requant_large())[0]
    # antialias
    nb = min(gs["aa_bound"], 32 // div - 1) if div > 1 else gs["aa_bound"]
    st["antialias"] = nb * 8 * K.cost(K.aa_butterfly(p["aa"]))[0]
    # IMDCT and its window / overlap
    sb = min(gs["sb_active_ola"], 32 // div)
    nza = gs["nz_aa"] * frac
    if p["imdct"] == "tab":
        st["imdct"] = nza * K.cost(K.imdct_sparse_line())[0]
    elif p["imdct"] == "hybrid":
        # per subband the cheaper of the sparse table form and the fast
        # factored form (both in the pass; a count picks)
        st["imdct"] = gs["imdct_hybrid_clk"] * frac
    elif p["imdct"] == "fastimul":
        m, a = K.imdct_fast_split_ops()
        mc = K.cost(["mov ax,[di+4]"] + K.mul_imul_q15(1) + ["mov [di+6],dx"])[0]
        ac = K.cost(["mov ax,[di+4]", "add ax,[di+6]", "mov [di+8],ax"])[0]
        st["imdct"] = sb * (m * mc + a * ac)
    else:
        m, a = K.imdct_fast_split_ops()
        L, lm, la = K.lee_code(32, K.mul_split())
        per_m = K.cost(L, 2, 1)[0] / (lm + la * 0.45)
        st["imdct"] = sb * (m * per_m + a * per_m * 0.45)
    if p["imdctwin"] == "imul":
        wl = ["mov cx,9", "xor ax,ax", "rep stosw ; 9"]
        for i in range(36):
            wl += ["mov ax,[es:di+%d]" % (2 * (i % 18))] + K.mul_imul_q15(0) + ["mov [es:di+%d],dx" % (40 + 2 * (i % 18))]
        st["imdct window"] = sb * K.cost(wl, 2, 1)[0]
    else:
        st["imdct window"] = sb * K.cost(K.imdct_subband_tab() if p["imdctwin"] == "tab"
                                         else K.imdct_subband_split())[0]
    # matrixing, codes, window: per slot
    mul = {"split": K.mul_split(), "imul": K.mul_imul_q15(1)}[p["dct"]]
    L, _, _ = K.lee_code(nout, mul, min(nout, int(round(gs["maxsb"]))))
    st["matrixing"] = 18 * K.cost(L, 2, 1)[0]
    if p["layout"] == "imul":
        st["V codes"] = 0
        st["window"] = 18 * nout * K.cost(K.win_output(p["n"], "imul"))[0]
    elif p.get("pair", True):
        # paired outputs: one code a unique V value, two ring stores
        st["V codes"] = 18 * nout * K.cost(K.vcode_fused(2))[0]
        st["window"] = 18 * (nout // 2) * K.cost(K.win_pair(p["n"], p["layout"]))[0]
    else:
        st["V codes"] = 18 * nout * K.cost(K.vcode("s0clamp", 4))[0]
        st["window"] = 18 * nout * K.cost(K.win_output(p["n"], p["layout"]))[0]
    return st


def exp_cost(items, R):
    out = {"kernels": {}, "profiles": {}, "rates": {}}
    ker = {
        "window tap row, 16 taps lodsw (one output)": K.win_output(16, "lodsw"),
        "window, 16 taps [si+d8] (one output)": K.win_output(16, "rel8"),
        "window, 16 taps standard ring [si+d16] (one output)": K.win_output(16, "abs16"),
        "window, 8 taps [si+d8] (one output)": K.win_output(8, "rel8"),
        "window, 4 taps [si+d8] (one output)": K.win_output(4, "rel8"),
        "window, 16 taps imul (one output)": K.win_output(16, "imul"),
        "window, 16 taps, outputs j and 32-j PAIRED (two outputs)": K.win_pair(16, "lodsw"),
        "window, 8 taps paired [si+d8] (two outputs)": K.win_pair(8, "rel8"),
        "window, 4 taps paired [si+d8] (two outputs)": K.win_pair(4, "rel8"),
        "V code fused into Lee's output, s=0 clamp, 2 stores": K.vcode_fused(2),
        "V code, s=0 clamp, 4 ring stores": K.vcode("s0clamp", 4),
        "V code, s=0 clamp, 2 ring stores": K.vcode("s0clamp", 2),
        "V code, s=2 round, 2 ring stores": K.vcode("s2round", 2),
        "multiply: byte-split tables (load, 2 lookups, store)": ["mov ax,[di+4]"] + K.mul_split() + ["mov [di+6],dx"],
        "multiply: imul Q15 rounded (load, store)": ["mov ax,[di+4]"] + K.mul_imul_q15(1) + ["mov [di+6],dx"],
        "multiply: imul high word only (load, store)": ["mov ax,[di+4]"] + K.mul_imul_hi() + ["mov [di+6],dx"],
        "multiply: 8-bit code table, ENC s=2 (load, store)": ["mov ax,[di+4]"] + K.mul_code_tab() + ["mov [di+6],dx"],
        "multiply: quarter-square, 17-bit, 4 segments": K.mul_qsq(),
        "multiply: imul 16x16->32 reference": K.mul_imul32(),
        "IMDCT: one nonzero line, 18 rows": K.imdct_sparse_line(),
        "IMDCT window+overlap, tables (one subband)": K.imdct_subband_tab(),
        "IMDCT window+overlap, byte-split (one subband)": K.imdct_subband_split(),
        "antialias butterfly, tables + recode": K.aa_butterfly("tab"),
        "antialias butterfly, byte-split": K.aa_butterfly("split"),
        "antialias butterfly, imul": K.aa_butterfly("imul"),
        "requantise |ix|<=15 (to a code word)": K.requant_small(),
        "requantise |ix|>15": K.requant_large(),
        "Huffman codeword, 4-bit first level": K.huff_codeword(4),
        "Huffman codeword, 6-bit first level": K.huff_codeword(6),
        "Huffman codeword, 8-bit first level": K.huff_codeword(8),
        "Huffman second level": K.huff_second(),
        "Huffman sign bit": K.huff_sign(),
        "Huffman refill (a byte)": K.huff_refill(),
        "table expansion at OPEN (one entry)": K.expand_entry(),
    }
    for name, lines in ker.items():
        c, b = K.cost(lines)
        out["kernels"][name] = {"clk": round(c, 1), "bytes": b, "asm": lines}
    lee = {}
    for N, Kk in ((32, 32), (32, 16), (32, 12), (16, 16), (8, 8)):
        for mname, mul in (("split", K.mul_split()), ("imul-hi", K.mul_imul_hi()),
                           ("code-tab", K.mul_code_tab())):
            if N != 32 and mname != "split":
                continue
            if Kk != N and mname != "split":
                continue
            L, m, a = K.lee_code(N, mul, Kk)
            c, b = K.cost(L, 2, 1)
            lee["Lee DCT-%d, %d live inputs, %s" % (N, Kk, mname)] = {"clk": round(c), "bytes": b, "mul": m, "add": a}
    out["lee"] = lee
    # loop vs unrolled window: a looped tap pays its row step and the loop
    looped = ["mov dx,64", "mov cx,16", "lodsw", "xchg bx,ax", "add dx,[bx+bp]",
              "add bp,512", "loop tap ; taken"]
    lp = ["lodsw", "xchg bx,ax", "add dx,[bx+di]", "add di,512", "loop tap ; taken"]
    c1 = K.cost(lp)[0]
    out["kernels"]["window tap, LOOPED (row pointer stepped, LOOP)"] = {"clk": round(c1, 1), "bytes": x3cyc.static_bytes(lp), "asm": lp}
    # segment switch: a pass that changes DS for a second row segment
    sw = ["mov ax,SEG2", "mov ds,ax"]
    out["kernels"]["segment switch (mov ax,imm / mov ds,ax)"] = {"clk": round(K.cost(sw)[0], 1), "bytes": x3cyc.static_bytes(sw), "asm": sw}
    ovr = ["lodsw", "xchg bx,ax", "es add dx,[bx+ROW0]"]
    out["kernels"]["window tap with a segment override (shared ring in another segment)"] = {"clk": round(K.cost(ovr)[0], 1), "bytes": x3cyc.static_bytes(ovr), "asm": ovr}
    # shift-add sequences for Lee's constants, against the split multiply
    plan = xm.DctPlan(32)
    sa = []
    for c in sorted(set(round(v, 9) for v in plan.consts)):
        row = {"c": c}
        for t in (2, 3, 4):
            d = xm.csd(c, t)
            row["t%d_err" % t] = round(abs(sum(s_ * 2.0 ** e for s_, e in d) - c) / c, 5)
            row["t%d_clk" % t] = round(K.cost(K.mul_shift_add(d))[0], 1)
        sa.append(row)
    out["shift_add"] = sa
    # granule statistics and Huffman lengths for each bit rate
    for kbps in (16, 24, 32):
        its = items if kbps == KBPS else corpus(secs=3.0, kbps=kbps)
        gs = granule_stats(its, names=EVAL_CORE)
        hl = huff_lengths(its, names=EVAL_CORE)
        out["rates"][kbps] = {"stats": gs, "huff": hl}
        allp = dict(PROFILES)
        allp["STANDARD arithmetic on the 8088 (reference)"] = STANDARD_8088
        allp["XT Standard, unpaired outputs"] = dict(PROFILES["XT Standard"], pair=False)
        for pname, p in allp.items():
            st = stage_cycles(p, gs, hl)
            tot = sum(st.values())
            gran_s = 576.0 / SR
            out["profiles"].setdefault(pname, {})[kbps] = {
                "stages": {k: round(v) for k, v in st.items()},
                "total": round(tot),
                "audio_s_per_wall_s": round(gran_s / (tot / CPU_HZ), 3),
                "wall_s_per_audio_s": round((tot / CPU_HZ) / gran_s, 2)}
        for L in (4, 6, 8):
            st = stage_cycles(PROFILES["XT Standard"], gs, hl, huffL=L)
            out["rates"][kbps]["huff_L%d" % L] = {"clk_per_granule": round(st["huffman"]),
                                                  "table_bytes": huff_table_bytes(L)}
    out["expand"] = expand_costs()
    R["cost"] = out


def expand_costs():
    """OPEN's expansion of each pack, MODELLED: entries a table times the
    per-entry kernel, plus the fl32 check over every word, plus a crc32 (a
    table-driven byte loop, priced for comparison only)."""
    import x3xttab
    ek = {k: K.cost(v)[0] for k, v in K.expand_kernels().items()}
    crc = K.cost(["lodsb", "xor al,bl", "mov bl,al", "shl bx,1", "mov ax,[bx+CRCT]",
                  "xor ax,dx", "mov dx,ax"])[0] + 20     # + the high word's xor
    res = {"kernels": {k: round(v, 1) for k, v in ek.items()}, "crc32_per_byte": round(crc, 1), "packs": {}}
    m = x3xttab.manifest()
    for pk, (n, div, names) in x3xttab.PACKS.items():
        cyc = {}
        rows = sum(w["rows"] for w in m["packs"][pk]["window_segments"])
        cyc["WIN"] = rows * 127 * (ek["prod_pos"] + ek["prod_neg"])
        nsplit = 0
        for nm, cnt in (("LEE", len(x3xttab.lee_consts(32 // div))), ("AASPL", 16), ("IMWIN", 19)):
            if nm in names:
                nsplit += cnt
        cyc["split"] = nsplit * ((256 + 129) * ek["split"] + 127 * ek["prod_neg"])
        mrows = 18 + (16 if "AATAB" in names else 0) + (19 if "IMWTAB" in names else 0)
        cyc["mul rows"] = mrows * 127 * (ek["mul_row"] + ek["prod_neg"])
        cyc["ENCV"] = 4097 * ek["enc_walk"] + 4095 * ek["enc_mirror"]
        cyc["ENCR"] = 8193 * ek["enc_walk"] + 8191 * ek["enc_mirror"]
        tot_bytes = m["packs"][pk]["total"]
        cyc["fl32 check"] = tot_bytes / 2 * ek["fl32"]
        total = sum(cyc.values())
        res["packs"][pk] = {"stages": {k: round(v) for k, v in cyc.items()},
                            "total_clk": round(total), "seconds": round(total / CPU_HZ, 2),
                            "crc32_seconds": round(tot_bytes * crc / CPU_HZ, 2),
                            "bytes": tot_bytes}
    return res


# ============================================================== profiles
class Design:
    """The study's chosen design inputs, built once from the TRAINING items:
    the window's V map, the hybrid stage's maps, the 8- and 4-term tap sets.
    x3xttab.py carries the same as literals; `check_tables` proves the two
    agree entry for entry."""

    def __init__(self, items):
        self.mV = build_maps(items, "V", xm.Q_V, 2.0)
        self.mR = build_maps(items, "R", xm.Q_R, 2.0)
        self.mZ = build_maps(items, "Z", xm.Q_R, 2.0)
        self.R = tap_corr(items)
        obj = BandObjective(items)
        self.coef = {16: dsp.DTAP, 8: choose_terms_band(self.R, 8, obj),
                     4: choose_terms_band(self.R, 4, obj)}
        self.vmap = self.mV["optseg8"]
        self.rmap = self.mR["lloyd8"]
        self.zmap = self.mZ["lloyd8"]
        self.banks = {}

    def bank(self, n, cm=None):
        cm = cm or self.vmap
        key = (n, cm.name, cm.bits)
        if key not in self.banks:
            self.banks[key] = xm.make_win_bank(cm, self.coef[n])
        return self.banks[key]

    def cfg(self, name):
        hyb = dict(aa="split", imdct="tab", imdctwin="split", dct="split",
                   map_im=self.rmap, s_im=2)
        if name == "XT Standard":
            return dict(hyb, win="tab", win_bank=self.bank(16), s_win=0)
        if name == "XT Fast":
            return dict(hyb, aa="tab", map_aa=self.rmap, imdctwin="tab", map_iw=self.zmap,
                        win="tab", win_bank=self.bank(8), s_win=0, coef=self.coef[8])
        if name == "XT Turbo":
            return dict(hyb, aa="tab", map_aa=self.rmap, imdctwin="tab", map_iw=self.zmap,
                        win="tab", win_bank=self.bank(4), s_win=0, coef=self.coef[4])
        if name == "XT Speech":
            return dict(hyb, win="tab", win_bank=self.bank(16), s_win=0, div=2)
        if name == "XT Speech 4:1":
            return dict(hyb, win="tab", win_bank=self.bank(16), s_win=0, div=4)
        if name == "XT Standard, 7-bit window codes":
            return dict(hyb, win="tab", win_bank=self.bank(16, self.mV["optseg7"]), s_win=0)
        if name == "XT Standard, Lloyd-Max window codes":
            return dict(hyb, win="tab", win_bank=self.bank(16, self.mV["lloyd8"]), s_win=0)
        if name == "XT Standard, 10-bit IMDCT codes":
            return dict(hyb, map_im=self.mR["optseg10"], win="tab", win_bank=self.bank(16), s_win=0)
        if name == "16-bit imul everywhere (no tables)":
            return dict(aa="int", imdct="int", imdctwin="int", dct="int", win="int")
        raise KeyError(name)


PROFILE_ORDER = ("XT Standard", "XT Fast", "XT Turbo", "XT Speech", "XT Speech 4:1",
                 "XT Standard, 7-bit window codes", "XT Standard, Lloyd-Max window codes",
                 "XT Standard, 10-bit IMDCT codes", "16-bit imul everywhere (no tables)")


def exp_profiles(items, R, des):
    out = {}
    for name in PROFILE_ORDER:
        cfg = des.cfg(name)
        div = cfg.pop("div", 1)
        res = run_cfg(items, cfg, div=div)
        row = {"snr_core": summary(res), "res": res, "div": div}
        if div > 1:
            ideal = {}
            for it in items:
                if it.role != "eval":
                    continue
                y, _ = xm.decode_chain(it.Xq, it.bt, dict(cfg, div=div))
                ideal[it.name] = compare(decimate_ref(it.ref(1), div), y)["snr"]
            row["snr_vs_ideal_lowpass"] = ideal
        out[name] = row
    out["_floor8"] = {it.name: pcm8_floor(it.ref()) for it in items if it.role == "eval"}
    out["_codec"] = {it.name: round(dsp.snr_db(it.x[:len(it.x) - 1200],
                                               it.ref()[DELAY:len(it.x) - 1200 + DELAY]), 2)
                     for it in items if it.role == "eval"}
    R["profiles"] = out


# ============================================================ listening
def write_wav(path, y, sr, bits):
    if bits == 8:
        data, clips = dsp.to_pcm8(y)
        raw = data.tobytes()
    else:
        v = np.clip(np.round(y * 32768.0), -32768, 32767).astype("<i2")
        raw = v.tobytes()
    w = wave.open(path, "wb")
    w.setnchannels(1)
    w.setsampwidth(bits // 8)
    w.setframerate(int(sr))
    w.writeframes(raw)
    w.close()


LISTEN_FIXTURES = (("music", "AFTERHRS.MP3", 5.0), ("speech", "X8K16.MP3", 3.2),
                   ("clicks", "X11K24.MP3", 3.1))


def exp_listen(des, R, x3sim=None, fdir=None, secs=3.5):
    """docs/xtmp3/listen/: three phrases, each as the float decode (16-bit),
    the float decode rounded to 8 bits (the fair baseline for an 8-bit XT
    output) and each approximation (8-bit), every file metered against the
    float decode. With x3sim present the phrases are REAL LAME streams (the
    numerics track's committed fixtures: an 11,025 Hz song at 24 kbps, the
    formant voice at 8,000 Hz and 16 kbps, clicks on a tone at 11,025 Hz and
    24 kbps); without it, the study's synthesised signals through the
    MP3-like quantiser."""
    os.makedirs(LISTEN, exist_ok=True)
    for f in os.listdir(LISTEN):
        if f.endswith(".wav"):
            os.remove(os.path.join(LISTEN, f))
    its = []
    if x3sim is not None:
        for sname, fn, cut_s in LISTEN_FIXTURES:
            it = FixtureItem(x3sim, os.path.join(fdir, fn))
            its.append((sname, it, cut_s, "%s (%d Hz, %d kbps, LAME)" % (fn, it.sr, it.kbps)))
    else:
        for sname, x in (("music", dsp.sig_music(SR, secs, seed=1, key=0)),
                         ("speech", dsp.sig_speech(SR, secs, seed=2, f0=140.0)),
                         ("clicks", dsp.sig_transient(SR, secs, seed=3))):
            its.append((sname, Item(sname, x), secs, "synthesised, MP3-like quantiser"))
    variants = [("xt-standard", "XT Standard"), ("xt-fast", "XT Fast"),
                ("xt-turbo", "XT Turbo"), ("xt-speech", "XT Speech")]
    rows = []
    for sname, it, cut_s, src in its:
        sr = it.sr
        ref = it.ref()
        lead = 1152                      # skip the decoder's start-up
        n = int(cut_s * sr)

        def cut(v, div=1):
            return v[lead // div:(lead + n) // div]
        write_wav(os.path.join(LISTEN, "%s-ref16.wav" % sname), cut(ref), sr, 16)
        rows.append((sname, src, "float decode (the reference)", "%s-ref16.wav" % sname, 16, sr, None))
        write_wav(os.path.join(LISTEN, "%s-ref8.wav" % sname), cut(ref), sr, 8)
        p8, _ = dsp.to_pcm8(cut(ref))
        m8 = compare(cut(ref), (p8.astype(float) - 128) / 128.0)
        m8["band_esr"], m8["band_hz"] = band_esr(cut(ref), (p8.astype(float) - 128) / 128.0, sr)
        rows.append((sname, src, "float decode rounded to 8 bits", "%s-ref8.wav" % sname, 8, sr, m8))
        for tag, pname in variants:
            cfg = des.cfg(pname)
            div = cfg.pop("div", 1)
            y, _ = xm.decode_chain(it.Xq, it.bt, dict(cfg, div=div))
            yc, rc = cut(y, div), cut(it.ref(div), div)
            fn = "%s-%s.wav" % (sname, tag)
            write_wav(os.path.join(LISTEN, fn), yc, sr / div, 8)
            p8, _ = dsp.to_pcm8(yc)
            m = compare(rc, (p8.astype(float) - 128) / 128.0)
            m["band_esr"], m["band_hz"] = band_esr(rc, (p8.astype(float) - 128) / 128.0, sr / div)
            rows.append((sname, src, pname, fn, 8, sr / div, m))
        for tag, label, cfg in (
                ("lin8-codes", "XT Standard with UNIFORM 8-bit codes (rejected)",
                 dict(des.cfg("XT Standard"), win_bank=xm.make_win_bank(des.mV["lin8"], dsp.DTAP))),
                ("4term-truncated", "4 terms by plain truncation, else exact (rejected)",
                 dict(coef=choose_terms(des.R, 4, "mid")))):
            y, _ = xm.decode_chain(it.Xq, it.bt, cfg)
            fn = "%s-%s.wav" % (sname, tag)
            write_wav(os.path.join(LISTEN, fn), cut(y), sr, 8)
            p8, _ = dsp.to_pcm8(cut(y))
            m = compare(cut(ref), (p8.astype(float) - 128) / 128.0)
            m["band_esr"], m["band_hz"] = band_esr(cut(ref), (p8.astype(float) - 128) / 128.0, sr)
            rows.append((sname, src, label, fn, 8, sr, m))
    total = sum(os.path.getsize(os.path.join(LISTEN, r[3])) for r in rows)
    R["listen"] = {"rows": [{"signal": r[0], "source": r[1], "variant": r[2], "file": r[3],
                             "bits": r[4], "rate": r[5], "metrics": r[6]} for r in rows],
                   "bytes": total, "from_fixtures": x3sim is not None}


# ===================================================== design <-> tables
def design_literals(des):
    """The DESIGN block x3xttab.py carries, from the study's choices."""
    vm = des.vmap
    taps = {}
    for n in (8, 4):
        taps[n] = [[int(i) for i in np.nonzero(des.coef[n][j])[0]] for j in range(32)]
    d = {"v_map": {"name": vm.name, "q": vm.q, "s0": vm.seg[1], "lens": list(vm.seg[0])},
         "r_map": {"name": des.rmap.name, "q": des.rmap.q, "levels": [int(v) for v in des.rmap.lv]},
         "z_map": {"name": des.zmap.name, "q": des.zmap.q, "levels": [int(v) for v in des.zmap.lv]},
         "taps8": taps[8], "taps4": taps[4]}
    return d


def write_design(des):
    """Rewrite x3xttab.py's DESIGN block and integer constants from this
    run (the generator then needs nothing of the study)."""
    import x3xttab
    path = os.path.join(HERE, "x3xttab.py")
    src = open(path).read()
    d = design_literals(des)
    lines = ["DESIGN = {"]
    lines.append('    # the window\'s V code: %s, %d segments, finest step 2^%d at Q%d'
                 % (d["v_map"]["name"], len(d["v_map"]["lens"]), d["v_map"]["s0"], d["v_map"]["q"]))
    lines.append('    "v_map": %r,' % d["v_map"])
    for key, what in (("r_map", "the IMDCT input code (Lloyd-Max on the training lines)"),
                      ("z_map", "XT Fast's and Turbo's IMDCT-window code (Lloyd-Max)")):
        lines.append("    # %s" % what)
        lines.append('    "%s": {"name": %r, "q": %d, "levels": [' % (key, d[key]["name"], d[key]["q"]))
        lv = d[key]["levels"]
        for i in range(0, len(lv), 12):
            lines.append("        " + ", ".join(str(v) for v in lv[i:i + 12]) + ",")
        lines.append("    ]},")
    for n in (8, 4):
        lines.append("    # %d taps an output phase: the least-error subset, mirrored pairs" % n)
        lines.append('    "taps%d": [' % n)
        for j in range(32):
            lines.append("        %r,  # phase %d" % (d["taps%d" % n][j], j))
        lines.append("    ],")
    lines.append("}")
    block = "\n".join(lines)
    a = src.index("#@@DESIGN-BEGIN\n") + len("#@@DESIGN-BEGIN\n")
    b = src.index("#@@DESIGN-END")
    src = src[:a] + block + "\n" + src[b:]
    dv = x3xttab.derive_consts()
    cl = []
    for k in ("LEE_C16", "AA_C16", "AA_C15", "COS_C15", "WIN36_C16", "WIN36_C13"):
        v = dv[k]
        cl.append("%s = (" % k)
        for i in range(0, len(v), 8):
            cl.append("    " + ", ".join(str(x) for x in v[i:i + 8]) + ",")
        cl.append(")")
    a = src.index("#@@CONSTS-BEGIN\n") + len("#@@CONSTS-BEGIN\n")
    b = src.index("#@@CONSTS-END")
    src = src[:a] + "\n".join(cl) + "\n" + src[b:]
    open(path, "w").write(src)


def check_tables(des):
    """The generator's bytes against the model the quality figures came
    from: the design literals, every window row, both encoders, the split
    rows, the IMDCT rows and the table-form antialias and IMDCT window.
    Returns a list of failures (empty is the pass)."""
    import importlib
    import x3xttab
    importlib.reload(x3xttab)
    fails = []
    lit = design_literals(des)
    for k in ("v_map", "r_map", "z_map", "taps8", "taps4"):
        if x3xttab.DESIGN[k] != lit[k]:
            fails.append("DESIGN[%s] differs from the study's choice" % k)
    if fails:
        return fails
    for n in (16, 8, 4):
        bank, _, _ = des.bank(n)
        vals = x3xttab.win_row_values(n)
        vm = x3xttab.DESIGN["v_map"]
        if len(vals) != bank.rows.shape[0]:
            fails.append("WIN%d row count" % n)
            continue
        for r, ib in enumerate(vals):
            gen = np.array(x3xttab.win_row_recurrence(ib, vm["lens"], vm["s0"]))[1:]
            if not np.array_equal(gen, bank.rows[r]):
                fails.append("WIN%d row %d" % (n, ib))
    _, look = des.vmap.enc_table(0, xm.Q_V)
    if not np.array_equal(np.array(x3xttab.enc_v()), look(np.arange(-4096, 4096))):
        fails.append("ENCV")
    tab, _ = des.rmap.enc_table(2, xm.Q_R, "round")
    if not np.array_equal(np.array(x3xttab.enc_r()), tab):
        fails.append("ENCR")
    for c16 in list(x3xttab.LEE_C16) + list(x3xttab.AA_C16) + list(x3xttab.WIN36_C16):
        H, L = x3xttab.split_recurrence(c16)
        Hm, Lm = xm.split_rows(c16 / 65536.0)
        hh = np.array([H[h & 255] for h in range(-128, 128)])
        if not (np.array_equal(hh, Hm) and np.array_equal(np.array(L), Lm)):
            fails.append("split %d" % c16)
    for m, c15 in enumerate(x3xttab.COS_C15):
        gen = np.array(x3xttab.mul_row(c15, x3xttab.r_levels(), 15))[1:]
        mod = xm.ptable(des.rmap, xm.COS_ROWS[m], xm.Q_R, xm.Q_R)[0]
        if not np.array_equal(gen, mod):
            fails.append("IMDCT row %d" % m)
    for k, c15 in enumerate(x3xttab.AA_C15):
        f = dsp.CS[k] if k < 8 else -dsp.CA[k - 8]
        gen = np.array(x3xttab.mul_row(c15, x3xttab.r_levels(), 15))[1:]
        mod = xm.ptable(des.rmap, f, xm.Q_R, xm.Q_R)[0]
        if not np.array_equal(gen, mod):
            fails.append("AATAB row %d" % k)
    wvals = sorted(set([round(v, 15) for v in dsp.SINE36[:18]] + [1.0]))
    for k, c13 in enumerate(x3xttab.WIN36_C13):
        gen = np.array(x3xttab.mul_row(c13, x3xttab.z_levels(), 15))[1:]
        mod = xm.ptable(des.zmap, wvals[k], xm.Q_R, xm.Q_S)[0]
        if not np.array_equal(gen, mod):
            fails.append("IMWTAB row %d" % k)
    return fails


# ================================================================ report
def fmt(v, nd=1):
    if v is None:
        return "-"
    if isinstance(v, float):
        if math.isinf(v):
            return "inf"
        return ("%%.%df" % nd) % v
    return str(v)


def write_tables(R, path):
    L = []
    w = L.append
    w("# XT study: generated tables")
    w("")
    w("GENERATED by `python3 tools/x3xtstudy.py --all`; do not edit. "
      "docs/xtmp3/XT-STUDY.md is the written account and cites these tables. "
      "Quality figures are host measurements: SNR in dB of each approximation "
      "against the FLOAT decode of the same MP3-like quantised data (so the "
      "codec's own noise is not in them), on held-out signals. Speed figures "
      "are the 8088 cycle MODEL (tools/x3cyc.py), not measurements.")
    w("")
    core = ", ".join(EVAL_CORE)
    # profiles
    P = R["profiles"]
    w("## 1. Profiles: quality")
    w("")
    w("SNR (dB) against the float decode; `core` is the mean of %s. "
      "`8-bit floor` is what rounding the float decode itself to 8 bits costs; "
      "`codec` is the float decode against the source (the MP3-like "
      "quantiser at %d kbps, %d Hz)." % (core, KBPS, SR))
    w("")
    names = [n for n in R["profiles"]["XT Standard"]["res"]]
    w("| profile | core | band ESR worst / mean | " + " | ".join(names) + " |")
    w("|---|---|---|" + "---|" * len(names))
    for pn in PROFILE_ORDER:
        r = P[pn]
        w("| %s | **%s** | %s / %s | " % (pn, fmt(r["snr_core"]), fmt(summary(r["res"], key="band_worst")),
                                       fmt(summary(r["res"], key="band_mean"))) +
          " | ".join(fmt(r["res"][n]["snr"]) for n in names) + " |")
    w("| *8-bit floor* | | | " + " | ".join(fmt(P["_floor8"][n]) for n in names) + " |")
    w("| *codec (float decode vs source)* | | | " + " | ".join(fmt(P["_codec"][n]) for n in names) + " |")
    w("")
    w("DC offset of the error (8-bit LSB), clip counts at 8 bits (profile / float decode), "
      "16-bit wraps and saturations, on every held-out item:")
    w("")
    w("| profile | worst DC (LSB) | worst gain (dB) | clips (XT / ref) | wraps | sats |")
    w("|---|---|---|---|---|---|")
    for pn in PROFILE_ORDER:
        r = P[pn]["res"]
        dcw = max(abs(v["dc"]) for v in r.values())
        gw = max(abs(v["gain_db"]) for v in r.values() if v["gain_db"] is not None)
        cl = sum(v["clips"] for v in r.values())
        cr = sum(v["clips_ref"] for v in r.values())
        w("| %s | %.3f | %.3f | %d / %d | %d | %d |" % (pn, dcw, gw, cl, cr,
                                                  sum(v["wraps"] for v in r.values()),
                                                  sum(v["sats"] for v in r.values())))
    for pn in PROFILE_ORDER:
        if "snr_vs_ideal_lowpass" in P[pn]:
            w("")
            w("%s against the IDEAL reduced-rate output (the full-rate float decode "
              "brick-wall low-passed at 0.9 x the new Nyquist and sampled on the kept "
              "phases): %s" % (pn, ", ".join("%s %s" % (k, fmt(v)) for k, v in P[pn]["snr_vs_ideal_lowpass"].items())))
    w("")
    # speed
    C = R["cost"]
    w("## 2. Profiles: speed (MODEL, 4.77 MHz 8088, 11,025 Hz mono, decode only)")
    w("")
    w("Audio seconds a wall second; wall seconds an audio second in brackets. "
      "Stage cycles a granule (576 samples) at 16 kbps beneath. Disk, UI and "
      "the feed are NOT in these (XT-STUDY.md 'End to end').")
    w("")
    w("| profile | 16 kbps | 24 kbps | 32 kbps |")
    w("|---|---|---|---|")
    for pn, v in C["profiles"].items():
        w("| %s | %s | %s | %s |" % (pn, *["%.3f (%.2f)" % (v[k]["audio_s_per_wall_s"], v[k]["wall_s_per_audio_s"]) for k in (16, 24, 32)]))
    w("")
    stages = list(next(iter(C["profiles"].values()))[16]["stages"].keys())
    w("| profile (16 kbps) | " + " | ".join(stages) + " | total |")
    w("|---|" + "---|" * (len(stages) + 1))
    for pn, v in C["profiles"].items():
        st = v[16]["stages"]
        tot = v[16]["total"]
        w("| %s | " % pn + " | ".join("%dk (%d%%)" % (round(st[s] / 1000), round(100 * st[s] / tot)) for s in stages) + " | %dk |" % round(tot / 1000))
    w("")
    w("## 3. Kernels (MODEL clocks, steady state, one unit)")
    w("")
    w("| kernel | clocks | bytes |")
    w("|---|---|---|")
    for k, v in C["kernels"].items():
        w("| %s | %.1f | %d |" % (k, v["clk"], v["bytes"]))
    for k, v in C["lee"].items():
        w("| %s (%d mul, %d add) | %d | %d |" % (k, v["mul"], v["add"], v["clk"], v["bytes"]))
    w("")
    w("Shift-and-add sequences for Lee's constants against the byte-split multiply "
      "(%.0f clocks with its load and store): relative coefficient error and clocks "
      "at 2, 3 and 4 signed powers of two." % C["kernels"]["multiply: byte-split tables (load, 2 lookups, store)"]["clk"])
    w("")
    w("| c | err 2 | clk 2 | err 3 | clk 3 | err 4 | clk 4 |")
    w("|---|---|---|---|---|---|---|")
    for r in C["shift_add"]:
        w("| %.6f | %.4f | %.0f | %.5f | %.0f | %.5f | %.0f |" % (r["c"], r["t2_err"], r["t2_clk"], r["t3_err"], r["t3_clk"], r["t4_err"], r["t4_clk"]))
    w("")
    E = C.get("expand")
    if E:
        w("OPEN's table expansion (MODEL): the recurrences of tools/x3xttab.py "
          "at their per-entry kernels (%s clocks: product row entry, its "
          "negation, a byte-split entry, an encoder-walk step, a `mul` row "
          "entry, a fl32 word), against a table-driven CRC-32 at %.0f clocks a "
          "byte." % (", ".join("%.0f" % E["kernels"][k] for k in ("prod_pos", "prod_neg", "split", "enc_walk", "mul_row", "fl32")),
                     E["crc32_per_byte"]))
        w("")
        w("| pack | bytes | " + " | ".join(next(iter(E["packs"].values()))["stages"].keys()) + " | total s | a CRC-32 instead of fl32 would add |")
        w("|---|---|" + "---|" * (len(next(iter(E["packs"].values()))["stages"]) + 2))
        for pk, v in E["packs"].items():
            w("| %s | %d | " % (pk, v["bytes"]) + " | ".join("%.2f s" % (x / CPU_HZ) for x in v["stages"].values()) +
              " | **%.2f** | %.2f s |" % (v["seconds"], v["crc32_seconds"]))
        w("")
    w("## 4. Granule statistics and Huffman")
    w("")
    w("Means a granule over the core items (MP3-like quantiser; real fixtures "
      "re-measure these in phase 2).")
    w("")
    ks = list(C["rates"][16]["stats"].keys())
    w("| kbps | " + " | ".join(ks) + " |")
    w("|---|" + "---|" * len(ks))
    for kb in (16, 24, 32):
        w("| %d | " % kb + " | ".join(fmt(C["rates"][kb]["stats"][k], 2) for k in ks) + " |")
    w("")
    w("| kbps | pairs > 4 bits | > 6 | > 8 | mean pair len | quads > 4 | L=4 clk/gr | L=6 | L=8 | table bytes L=4/6/8 |")
    w("|---|---|---|---|---|---|---|---|---|---|")
    for kb in (16, 24, 32):
        h = C["rates"][kb]["huff"]
        r = C["rates"][kb]
        w("| %d | %.3f | %.3f | %.3f | %.2f | %.3f | %d | %d | %d | %d / %d / %d |" % (
            kb, h["pairs_over_4"], h["pairs_over_6"], h["pairs_over_8"], h["pairs_mean_len"],
            h["quads_over_4"], r["huff_L4"]["clk_per_granule"], r["huff_L6"]["clk_per_granule"],
            r["huff_L8"]["clk_per_granule"], r["huff_L4"]["table_bytes"], r["huff_L6"]["table_bytes"],
            r["huff_L8"]["table_bytes"]))
    w("")
    # window maps
    w("## 5. The window's V code (the window stage alone)")
    w("")
    w("`s` is the encoder table's shift (ENC indexed by v >> s, rounded); "
      "s = 0 is reached with a clamp at +-2.0 FS (8 KB).")
    w("")
    w("| map | bits | s | ENC bytes | rows | row bytes | core SNR | quiet | noise |")
    w("|---|---|---|---|---|---|---|---|---|")
    for r in R["maps"]["rows"]:
        w("| %s | %d | %d | %d | %d | %d | %s | %s | %s |" % (
            r["map"], r["bits"], r["s"], r["enc_bytes"], r["rows"], r["table_bytes"],
            fmt(r["snr_core"]), fmt(r["res"]["quiet"]["snr"]), fmt(r["res"]["noise"]["snr"])))
    w("")
    w("| map | levels | finest | coarsest | top (FS) | segments |")
    w("|---|---|---|---|---|---|")
    for k, m in R["maps"]["maps"].items():
        w("| %s | %d | %d | %d | %.3f | %s |" % (k, m["levels"], m["finest"], m["coarsest"], m["vmax"],
                                             m.get("seg", "-")))
    w("")
    w("## 6. Coefficient palettes for the window rows")
    w("")
    w("| rows (K) | exact | row bytes | SNR, coefficients alone | SNR with the 8-bit code |")
    w("|---|---|---|---|---|")
    for r in R["palette"]:
        w("| %d | %s | %d | %s | %s |" % (r["K"], "yes" if r["exact"] else "palette", r["table_bytes"],
                                       fmt(r["snr_coef_only"]), fmt(r["snr_with_code"])))
    w("")
    w("## 7. Matrixing (32-point DCT, Lee) alone")
    w("")
    w("| form | s | core SNR | table bytes |")
    w("|---|---|---|---|")
    for r in R["dct"]:
        w("| %s | %s | %s | %d |" % (r["form"], r.get("s", "-"), fmt(r["snr_core"]), r.get("table_bytes", 0)))
    w("")
    w("## 8. The hybrid stage alone")
    w("")
    w("| form | core SNR | table bytes |")
    w("|---|---|---|")
    for r in R["imdct"]:
        w("| %s | %s | %s |" % (r["form"], fmt(r["snr_core"]), r.get("table_bytes", "-")))
    w("")
    w("## 9. Shortened synthesis: taps an output phase")
    w("")
    w("Float arithmetic, everything else exact, so this is the term choice alone. "
      "`alias` is the largest spurious component of a tone (40 tones, 60 Hz to "
      "5.3 kHz) relative to the tone; `pre`/`ring` the energy of a click's "
      "response before and after its peak relative to the whole (the full "
      "filter's own are -97 dB); `DC gain` and `ripple` from a constant input "
      "(ripple = the 32-sample pattern that becomes a tone at k x 344.5 Hz).")
    w("")
    w("| n | method | core SNR | band ESR worst / mean | hitone | alias worst / median | pre | ring | click err | DC gain | ripple | rows | max row gain |")
    w("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for r in R["terms"]:
        w("| %d | %s | %s | %s / %s | %s | %s / %s | %s | %s | %s | %s | %s | %d | %.3f |" % (
            r["n"], r["method"], fmt(r["snr_core"]), fmt(r.get("band_worst_core")), fmt(r.get("band_mean_core")),
            fmt(r["res"]["hitone"]["snr"]),
            fmt(r["alias"]["worst"]), fmt(r["alias"]["median"]), fmt(r["click"]["pre"]),
            fmt(r["click"]["ring"]), fmt(r["click"]["err"]), fmt(r["dc"]["gain_db"], 3),
            fmt(r["dc"]["ripple_db"]), r["rows"], r["max_gain"]))
    w("")
    w("## 10. Reduced rate (output phases only, pruned subbands)")
    w("")
    w("| divisor | out rate at 11,025 | bandwidth | core SNR vs ideal low-pass | " + " | ".join(names) + " |")
    w("|---|---|---|---|" + "---|" * len(names))
    for r in R["rate"]:
        w("| %d | %.1f | %.1f | %s | " % (r["div"], r["out_rate_11025"], r["bandwidth_hz"], fmt(r["snr_core_vs_lowpassed_full"])) +
          " | ".join(fmt(r["res"][n]["snr"]) for n in names) + " |")
    w("")
    for r in R["rate"]:
        g = r.get("edge_gain")
        if g:
            w("Divisor %d, tone gain near the band edge (dB): %s" % (r["div"], ", ".join("%.0f Hz %.2f" % (f, v) for f, v in g)))
            w("")
    w("## 11. Table packs (tools/x3xttab.py)")
    w("")
    T = R.get("tables")
    if T:
        w("Generator input: %d bytes, CRC-32 %s." % (T["gen_input_bytes"], T["gen_input_crc32"]))
        w("")
        w("| pack | window terms / divisor | table | bytes | CRC-32 | fl32 |")
        w("|---|---|---|---|---|---|")
        for pk, rec in T["packs"].items():
            for name, blobs in rec["tables"].items():
                for i, b in enumerate(blobs):
                    w("| %s | %d / %d | %s%s | %d | %s | %s |" % (pk, rec["window_terms"], rec["divisor"], name,
                                                               " seg %d" % i if len(blobs) > 1 else "",
                                                               b["bytes"], b["crc32"], b["fl32"]))
            w("| %s | | **total** | **%d** | | |" % (pk, rec["total"]))
        w("")
        for pk, rec in T["packs"].items():
            w("%s window segments: %s; table segments: %s" % (
                pk, "; ".join("pairs %s, %d rows, %d bytes" % (s["pairs"], s["rows"], s["bytes"]) for s in rec["window_segments"]),
                "; ".join("%s pass %s, %d bytes" % (s["pass"], "+".join(s["tables"]), s["bytes"]) for s in rec["table_segments"])))
            w("")
    F = R.get("fixtures", {})
    if F.get("items"):
        w("")
        w("## 12. Real fixtures (LAME streams parsed by tools/x3sim.py)")
        w("")
        w("The numerics track's committed fixtures, parsed and Huffman-decoded by "
          "x3sim.py, reconstructed by this study's chains. `ref vs x3sim` is this "
          "study's float decode against x3sim.py's own float decode (two "
          "independent ISO decoders; x3sim rounds to 16 bits). Huffman lengths are "
          "the REAL Layer III tables' (x3sim's `huff_decode`, bits a codeword "
          "without linbits or signs).")
        w("")
        w("| fixture | rate | kbps | granules | short | ref vs x3sim (dB) | nz | nz after AA | active sb | pairs | quads | pair len | > 4 | > 6 | > 8 | quads > 4 |")
        w("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
        for k, v in F["items"].items():
            if "error" in v:
                w("| %s | %s |" % (k, v["error"]))
                continue
            g = v["stats"]
            w("| %s | %d | %d | %d | %.2f | %s | %.0f | %.0f | %.1f | %.0f | %.0f | %s | %s | %s | %s | %s |" % (
                k, v["rate"], v["kbps"], v["granules"], v["short_share"], fmt(v["ref_vs_x3sim_float_snr"]),
                g["nz"], g["nz_aa"], g["sb_active_ola"], g["pairs"], g["quads"], fmt(v["pairs_mean_len"], 2),
                fmt(v["pairs_over_4"], 3), fmt(v["pairs_over_6"], 3), fmt(v["pairs_over_8"], 3),
                fmt(v["quads_over_4"], 3)))
        w("")
        good = [v for v in F["items"].values() if "error" not in v]
        pn = list(good[0]["profiles"].keys()) if good else []
        w("Quality, SNR (dB) against the float decode, and the 8-bit floor:")
        w("")
        w("| fixture | " + " | ".join(pn) + " | 8-bit floor |")
        w("|---|" + "---|" * (len(pn) + 1))
        for k, v in F["items"].items():
            if "error" in v:
                continue
            w("| %s | " % k + " | ".join("%s (%s)" % (fmt(v["profiles"][p]["snr"]), fmt(v["profiles"][p].get("band_worst")))
                                       for p in pn) + " | %s |" % fmt(v["floor8"]))
        w("")
        w("(In brackets, the worst octave band's error-to-signal ratio, dB.)")
        w("")
        sp = list(good[0]["speed"].keys()) if good else []
        w("Speed (MODEL), audio seconds a wall second, from each fixture's own "
          "granule statistics and Huffman lengths (XT Speech takes divisor 2 at "
          "8-12 kHz and 4 at 16 kHz):")
        w("")
        w("| fixture | " + " | ".join(sp) + " |")
        w("|---|" + "---|" * len(sp))
        for k, v in F["items"].items():
            if "error" in v:
                continue
            w("| %s | " % k + " | ".join("%.3f" % v["speed"][p]["audio_s_per_wall_s"] for p in sp) + " |")
        w("")
        a = F["items"].get("AFTERHRS.MP3")
        if a:
            st0 = list(a["speed"]["XT Standard"]["stages"].keys())
            w("AFTERHRS.MP3 (11,025 Hz mono, 24 kbps, a song) by stage, k clocks a granule:")
            w("")
            w("| profile | " + " | ".join(st0) + " | total | wall s an audio s |")
            w("|---|" + "---|" * (len(st0) + 2))
            for p, m in a["speed"].items():
                if "stages" not in m:
                    continue
                w("| %s | " % p + " | ".join("%d" % round(m["stages"][s_] / 1000) for s_ in st0) +
                  " | %d | %.2f |" % (round(m["total"] / 1000), m["wall_s_per_audio_s"]))
    open(path, "w").write("\n".join(L) + "\n")


def write_listen_readme(R, path):
    Lr = R["listen"]
    if Lr["from_fixtures"]:
        what = ("three committed MP3 fixtures (tests/xtmp3/, made by the numerics track "
                "with LAME from synthesised signals: no third-party audio), parsed and "
                "Huffman-decoded by tools/x3sim.py, then reconstructed by this study's "
                "float reference and by each XT approximation")
    else:
        what = ("three synthesised signals through the study's MP3-LIKE quantiser, then "
                "reconstructed by the float reference and each XT approximation")
    L = ["# XTMP3 XT study: listening material", "",
         "GENERATED by `python3 tools/x3xtstudy.py --all`: %s. Every metric is "
         "against the float decode (`*-ref16.wav`), measured on the 8-bit file as "
         "written. `*-ref8.wav` is the float decode rounded to 8 bits, the fair "
         "baseline for an 8-bit XT output. The listening notes in "
         "docs/xtmp3/XT-STUDY.md are reasoned from these metrics and spectra and are "
         "**for the owner to confirm by ear**." % what, "",
         "`worst band` is the octave band where the error comes nearest the "
         "signal (error-to-signal ratio, dB) and its centre frequency: where "
         "an approximation would be heard first.", "",
         "| file | source | variant | format | SNR (dB) | seg SNR | worst band | peak err (LSB8) | DC (LSB8) | clips |",
         "|---|---|---|---|---|---|---|---|---|---|"]
    for r in Lr["rows"]:
        m = r["metrics"] or {}
        wb = "%s dB @ %s Hz" % (fmt(m.get("band_esr")), m.get("band_hz")) if m.get("band_esr") is not None else "-"
        L.append("| `%s` | %s | %s | %d-bit %d Hz | %s | %s | %s | %s | %s | %s |" % (
            r["file"], r["source"], r["variant"], r["bits"], int(round(r["rate"])),
            fmt(m.get("snr")), fmt(m.get("segsnr")), wb, fmt(m.get("peak_lsb8")),
            fmt(m.get("dc"), 3), m.get("clips", "-")))
    L += ["", "Total %d bytes." % Lr["bytes"]]
    open(path, "w").write("\n".join(L) + "\n")


def strip(o):
    """Results JSON without the tone lists (they are summarised)."""
    if isinstance(o, dict):
        return {k: strip(v) for k, v in o.items() if k not in ("freqs", "spurs", "asm")}
    if isinstance(o, list):
        return [strip(v) for v in o]
    if isinstance(o, float) and not math.isfinite(o):
        return str(o)
    if isinstance(o, (np.floating,)):
        return float(o)
    if isinstance(o, (np.integer,)):
        return int(o)
    return o


def selfcheck():
    fails = []
    x = dsp.sig_noise(SR, 1.0, pink=False, amp=0.5)
    x = x[:len(x) // 32 * 32]
    y = dsp.synthesis(dsp.analysis(x))
    pr = dsp.snr_db(x[:-1000], y[dsp.FB_DELAY:len(x) - 1000 + dsp.FB_DELAY])
    if pr < 80:
        fails.append("filterbank reconstructs at %.1f dB" % pr)
    S = dsp.analysis(dsp.sig_music(SR, 2.0)[:576 * 38])
    bt = np.array([0, 1, 2, 2, 3] * 7 + [0, 0, 0])
    X = dsp.hybrid_analysis(S, bt)
    S2 = dsp.hybrid_synthesis(X, bt)
    h = dsp.snr_db(S[:-54], S2[18:-36])
    if h < 200:
        fails.append("hybrid round trip %.1f dB" % h)
    rng = np.random.default_rng(1)
    Sr = rng.standard_normal((8, 32))
    n = np.arange(32)
    if np.abs(xm.dct_float(Sr) - Sr @ np.cos(np.pi * n[:, None] * (2 * n[None, :] + 1) / 64).T).max() > 1e-9:
        fails.append("Lee DCT")
    if np.abs(xm.dct_to_V(xm.dct_float(Sr)) - dsp.synth_V(Sr)).max() > 1e-9:
        fails.append("V from the DCT")
    cm = xm.map_g711(8, 11, 0)
    row, cf = xm.ptable(cm, 0.7, 11, 13)
    want = np.array([round(0.7 * 4 * v) for v in cm.decode(np.arange(-127, 128))])
    if np.abs(row - want).max() > 1:
        fails.append("product table formula")
    for f in fails:
        print("x3xtstudy: FAIL", f)
    print("x3xtstudy: selfcheck %s" % ("ok" if not fails else "FAILED"))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description="XTMP3 XT approximation study")
    ap.add_argument("--all", action="store_true", help="run every experiment and write the outputs")
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("--secs", type=float, default=5.0)
    ap.add_argument("--write-design", action="store_true",
                    help="rewrite x3xttab.py's DESIGN and constants from this run")
    ap.add_argument("--check-design", action="store_true",
                    help="fail if x3xttab.py's tables differ from the study's")
    ap.add_argument("--render", action="store_true",
                    help="re-render the markdown from docs/xtmp3/xtstudy-results.json")
    ap.add_argument("--x3sim", metavar="DIR", default=None,
                    help="where tools/x3sim.py is (default: this tools/ directory, "
                         "then ../os8088-x3num/tools)")
    a = ap.parse_args()
    if a.selfcheck:
        return selfcheck()
    if a.render:
        R = json.load(open(os.path.join(OUTDIR, "xtstudy-results.json")))
        write_tables(R, os.path.join(OUTDIR, "XT-STUDY-TABLES.md"))
        write_listen_readme(R, os.path.join(LISTEN, "README.md"))
        return 0
    items = corpus(secs=a.secs)
    des = Design(items)
    if a.write_design:
        write_design(des)
        print("x3xtstudy: wrote x3xttab.py's DESIGN")
    if a.check_design or a.write_design:
        f = check_tables(des)
        for x in f:
            print("x3xtstudy: table mismatch:", x)
        print("x3xtstudy: tables %s the model" % ("MATCH" if not f else "DIFFER FROM"))
        if f:
            return 1
    if not a.all:
        return 0
    xdir = a.x3sim
    if xdir is None:
        for cand in (HERE, os.path.join(os.path.dirname(REPO), "os8088-x3num", "tools")):
            if os.path.exists(os.path.join(cand, "x3sim.py")):
                xdir = cand
                break
    R = {"meta": {"sr": SR, "kbps": KBPS, "secs": a.secs, "Q": {"R": xm.Q_R, "S": xm.Q_S, "V": xm.Q_V, "O": xm.Q_O},
                  "items": [(it.name, it.role) for it in items]}}
    steps = [("maps", lambda: exp_maps(items, R)),
             ("palette", lambda: exp_palette(items, R, des.vmap, 0)),
             ("dct", lambda: exp_dct(items, R)),
             ("imdct", lambda: exp_imdct(items, R)),
             ("terms", lambda: exp_terms(items, R)),
             ("rate", lambda: exp_rate(items, R)),
             ("profiles", lambda: exp_profiles(items, R, des)),
             ("cost", lambda: exp_cost(items, R)),
             ("fixtures", lambda: exp_fixtures(R, des, xdir)),
             ("listen", lambda: exp_listen(des, R, load_x3sim(xdir),
                                           os.path.join(os.path.dirname(xdir), "tests", "xtmp3")
                                           if xdir else None))]
    import time
    for name, fn in steps:
        t = time.time()
        fn()
        print("x3xtstudy: %-9s %.1f s" % (name, time.time() - t))
    import x3xttab
    R["tables"] = x3xttab.manifest()
    R["design"] = design_literals(des)
    os.makedirs(OUTDIR, exist_ok=True)
    with open(os.path.join(OUTDIR, "xtstudy-results.json"), "w") as f:
        json.dump(strip(R), f, indent=1, sort_keys=False)
    write_tables(R, os.path.join(OUTDIR, "XT-STUDY-TABLES.md"))
    write_listen_readme(R, os.path.join(LISTEN, "README.md"))
    print("x3xtstudy: wrote docs/xtmp3/xtstudy-results.json, XT-STUDY-TABLES.md, listen/")
    return 0



# ============================================== real fixtures through x3sim
X3SIM_FIXTURES = ("AFTERHRS.MP3", "X8K16.MP3", "X11K24.MP3", "X12K32.MP3", "M16.MP3")


def load_x3sim(path):
    """Import tools/x3sim.py (the numerics track's reference decoder) from
    `path`, read-only. Returns the module or None."""
    if not path or not os.path.exists(os.path.join(path, "x3sim.py")):
        return None
    sys.path.insert(0, path)
    try:
        import x3sim
        return x3sim
    except Exception as e:      # an in-progress reference may not import
        print("x3xtstudy: x3sim not usable (%s)" % e)
        return None


class FixtureItem(Item):
    """A committed MP3 fixture's requantised lines, block types and Huffman
    code lengths, taken from x3sim's parser and Huffman decoder (mono
    streams; a mixed block, which LAME does not write, is counted and
    decoded as a normal one), and x3sim's own float decode for the
    cross-check of this study's reference chain."""

    def __init__(self, x3sim, path):
        self.name = os.path.basename(path)
        self.role = "eval"
        data = open(path, "rb").read()
        TB = x3sim.TB
        lens = []
        orig = TB.huff_decode
        ht = x3sim.huff_tables()

        def tab_of(e):              # (table, first width) or a bare table
            return e[0] if isinstance(e, tuple) else e
        c1 = {id(tab_of(ht[32])), id(tab_of(ht[33]))}

        def spy(table, peek, *a, **k):
            v, used, lk = orig(table, peek, *a, **k)
            lens.append((used, id(table) in c1))
            return v, used, lk
        TB.huff_decode = spy
        try:
            st = x3sim.Stream(data)
            dec = x3sim.Decoder(st, "float", 1, True)
            self.sr = st.h0.rate
            Xs, bts, IXs = [], [], []
            self.mixed = 0
            prev_bt = 0
            for h, side, md, verdict in st.frames():
                if verdict == x3sim.F_TRUNC:
                    break
                r = x3sim.BitReader(md) if verdict == x3sim.F_OK else None
                prev = [None, None]
                for gr in range(h.granules):
                    sp = dec.granule_spectra(h, side, gr, r, prev) if r else None
                    X = np.zeros((32, 18))
                    ixg = np.zeros(576, dtype=np.int64)
                    if sp and sp[0][3]:
                        gi, scf, ix, _ = sp[0]
                        q = np.array(x3sim.quarter_exponents(h, gi, scf, False), dtype=np.float64)
                        ixa = np.array(ix, dtype=np.int64)
                        xr = np.sign(ixa) * np.abs(ixa) ** (4.0 / 3.0) * 2.0 ** (q / 4.0)
                        bt = gi.block_type if gi.win_switch else 0
                        if gi.win_switch and gi.mixed:
                            self.mixed += 1
                        if bt == 2:
                            xr = xr[x3sim.reorder_index(h, gi)]
                            ixa = ixa[x3sim.reorder_index(h, gi)]
                            Y = xr.reshape(32, 18)
                            for w in range(3):
                                X[:, 6 * w:6 * w + 6] = Y[:, w::3]
                        else:
                            X = xr.reshape(32, 18)
                        ixg = ixa
                    else:
                        bt = 3 if prev_bt in (1, 2) else 0
                    prev_bt = bt
                    Xs.append(X)
                    bts.append(bt)
                    IXs.append(ixg.reshape(32, 18))
            self.Xq = np.array(Xs)
            self.bt = np.array(bts)
            self.IX = np.array(IXs)
            self.kbps = st.h0.bitrate
        finally:
            TB.huff_decode = orig
        self.huff_lens = lens
        pcm, rate, _, _ = x3sim.decode_file(data, model="float", div=1, mono=True)
        self.x3sim_float = pcm[:, 0].astype(np.float64) / 32768.0
        self._ref = {}
        self.x = None


def exp_fixtures(R, des, x3sim_dir):
    x3sim = load_x3sim(x3sim_dir)
    if x3sim is None:
        R["fixtures"] = {"skipped": "x3sim.py not found at %s" % x3sim_dir}
        return
    fdir = os.path.join(os.path.dirname(x3sim_dir), "tests", "xtmp3")
    out = {"x3sim_dir": x3sim_dir, "items": {}}
    items = []
    for fn in X3SIM_FIXTURES:
        p = os.path.join(fdir, fn)
        if not os.path.exists(p):
            continue
        try:
            it = FixtureItem(x3sim, p)
        except Exception as e:      # x3sim is the numerics track's, and moves
            out["items"][fn] = {"error": "x3sim interface: %r" % e}
            continue
        items.append(it)
        ref = it.ref()
        n = min(len(ref), len(it.x3sim_float))
        xv = round(dsp.snr_db(it.x3sim_float[:n], ref[:n]), 1)
        lens = it.huff_lens
        pl = [u for u, c1 in lens if not c1]
        ql = [u for u, c1 in lens if c1]
        rec = {"rate": it.sr, "kbps": it.kbps, "granules": len(it.bt), "mixed": it.mixed,
               "short_share": round(float(np.mean(it.bt == 2)), 3),
               "ref_vs_x3sim_float_snr": xv,
               "pair_codewords": len(pl), "quad_codewords": len(ql),
               "pairs_mean_len": round(float(np.mean(pl)), 2) if pl else None,
               "quads_mean_len": round(float(np.mean(ql)), 2) if ql else None}
        for L in (4, 6, 8):
            rec["pairs_over_%d" % L] = round(float(np.mean(np.array(pl) > L)), 4) if pl else None
        rec["quads_over_4"] = round(float(np.mean(np.array(ql) > 4)), 4) if ql else None
        rec["stats"] = granule_stats([it])
        rec["profiles"] = {}
        for pname in ("XT Standard", "XT Fast", "XT Turbo", "XT Speech", "16-bit imul everywhere (no tables)"):
            cfg = des.cfg(pname)
            div = cfg.pop("div", 1)
            y, stx = xm.decode_chain(it.Xq, it.bt, dict(cfg, div=div))
            m = compare(it.ref(div), y)
            m["band_worst"], m["band_mean"] = band_stats(it.ref(div), y, it.sr / div)
            m["wraps"] = sum(stx.wraps.values())
            m["sats"] = sum(stx.sats.values())
            rec["profiles"][pname] = m
        rec["floor8"] = pcm8_floor(ref)
        hl = {"pairs_mean_len": rec["pairs_mean_len"] or 0, "quads_mean_len": rec["quads_mean_len"] or 0,
              "quads_over_4": rec["quads_over_4"] or 0}
        for L in (4, 6, 8):
            hl["pairs_over_%d" % L] = rec["pairs_over_%d" % L] or 0
        gs = rec["stats"]
        rec["speed"] = {}
        for pname, p in PROFILES.items():
            p = dict(p)
            if pname == "XT Speech":
                p["div"] = 2 if it.sr <= 12000 else 4
            stc = stage_cycles(p, gs, hl)
            tot = sum(stc.values())
            rec["speed"][pname] = {"audio_s_per_wall_s": round((576.0 / it.sr) / (tot / CPU_HZ), 3),
                                   "wall_s_per_audio_s": round((tot / CPU_HZ) / (576.0 / it.sr), 2),
                                   "total": round(tot), "div": p["div"],
                                   "stages": {k: round(v) for k, v in stc.items()}}
        stc = stage_cycles(STANDARD_8088, gs, hl)
        tot = sum(stc.values())
        rec["speed"]["STANDARD arithmetic on the 8088 (reference)"] = {
            "audio_s_per_wall_s": round((576.0 / it.sr) / (tot / CPU_HZ), 3),
            "wall_s_per_audio_s": round((tot / CPU_HZ) / (576.0 / it.sr), 2),
            "total": round(tot), "div": 1, "stages": {k: round(v) for k, v in stc.items()}}
        out["items"][it.name] = rec
    R["fixtures"] = out


if __name__ == "__main__":
    sys.exit(main())
