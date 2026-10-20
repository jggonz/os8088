#!/usr/bin/env python3
"""os88paldrift - how far a SHOT's best sixteen drift inside it, and how
much shifting a few palette slots mid-shot could win back
(docs/plans/VIDEO-PALETTE-PLAN.md 11.2).

    python3 tools/os88paldrift.py CLIP START END [WINDOW] [tail]

Each shot (os88venc.scene_pass at 15 fps, 640x360, every fourth row and
column) gets its own sixteen, k-means in CIELAB weighted 1 + C*/8 as
colour_kmeans weights; each WINDOW-frame stretch of it (default 30, two
seconds) is then scored against the shot's sixteen, the window's OWN
sixteen (the ceiling of any shifting), and the shot's with the 2 and the 4
slots that cost least dropped and refitted to the window. The score is the
weighted mean nearest-colour dE, or with `tail` the weighted share of
pixels more than 10 dE from every colour - a colour a dither cannot reach.
An instrument: nothing in the build runs it."""
import os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import os88venc as ve
import os88cgacomp as cc
rng = np.random.default_rng(1)
TAIL = 0

def km(lab, wt, fixed, k, it=10):
    C = [c for c in fixed]
    near = ((lab[:, None] - np.array(C)[None]) ** 2).sum(2).min(1) if C else np.full(len(lab), 1e9)
    for _ in range(k):
        d = near * wt
        C.append(lab[rng.choice(len(lab), p=d / d.sum())] if d.sum() else C[0])
        near = np.minimum(near, ((lab - C[-1]) ** 2).sum(1))
    C = np.array(C, float); nf = len(fixed)
    for _ in range(it):
        a = np.argmin(((lab[:, None] - C[None]) ** 2).sum(2), 1)
        for j in range(nf, len(C)):
            m = a == j
            if m.any():
                C[j] = (lab[m] * wt[m, None]).sum(0) / wt[m].sum()
    return C

def err(lab, wt, C):
    d = np.sqrt(((lab[:, None] - C[None]) ** 2).sum(2).min(1))
    if TAIL:                        # weighted share a colour 10 dE off
        return 100 * (wt * (d > 10)).sum() / wt.sum()
    return (d * wt).sum() / wt.sum()

def shift(lab, wt, P, k):
    """drop the k slots whose loss costs least (greedy), refit k free"""
    keep = list(range(1, 16))       # slot 0 black stays
    for _ in range(k):
        best = None
        for j in keep:
            C = P[[0] + [x for x in keep if x != j]]
            e = err(lab, wt, C)
            if best is None or e < best[0]:
                best = (e, j)
        keep.remove(best[1])
    return km(lab, wt, list(P[[0] + keep]), k)

def main():
    global TAIL
    src = sys.argv[1]; start, end = float(sys.argv[2]), float(sys.argv[3])
    W = int(sys.argv[4]) if len(sys.argv) > 4 else 30
    TAILARG = len(sys.argv) > 5
    fr = [f[::4, ::4].reshape(-1, 3) for f in
          ve.ffmpeg_video(src, 640, 360, None, "15", start, end, None, pix="rgb24")]
    sc = ve.scene_pass(src, 640, 360, None, "15", start, end, None)
    starts = [s for s, _ in sc] + [len(fr)]
    tot = {}
    TAIL = 0
    print("scene  frames   shot  own   s2    s4    (window dE, frame-weighted)")
    for i in range(len(starts) - 1):
        a, b = starts[i], starts[i + 1]
        allpx = np.concatenate(fr[a:b])[::max(1, (b - a) // 4)]
        lab = cc.srgb2lab(allpx.astype(float)); wt = 1 + np.hypot(lab[:, 1], lab[:, 2]) / 8
        TAIL = 0
        P = km(lab, wt, [np.zeros(3)], 15, 12)
        row = np.zeros(4); n = 0
        for w0 in range(a, b, W):
            w1 = min(b, w0 + W)
            px = np.concatenate(fr[w0:w1])[::max(1, (w1 - w0) // 2)][::3]
            l = cc.srgb2lab(px.astype(float)); t = 1 + np.hypot(l[:, 1], l[:, 2]) / 8
            own = km(l, t, [np.zeros(3)], 15, 10)
            TAIL = 0
            s2, s4 = shift(l, t, P, 2), shift(l, t, P, 4)
            TAIL = TAILARG
            e = [err(l, t, P), err(l, t, own), err(l, t, s2), err(l, t, s4)]
            row += np.array(e) * (w1 - w0); n += w1 - w0
        row /= n
        for j, v in enumerate(row): tot[j] = tot.get(j, 0) + v * (b - a)
        print("%5d %7d  %5.2f %5.2f %5.2f %5.2f  at %.1fs" % (i, b - a, *row, start + a / 15))
    N = len(fr)
    print("all   %7d  %5.2f %5.2f %5.2f %5.2f" % (N, *(tot[j] / N for j in range(4))))


if __name__ == "__main__":
    main()
