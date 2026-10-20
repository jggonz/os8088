#!/usr/bin/env python3
"""os88palsim - what a palette that changes mid-play would buy the Video
Player (docs/plans/VIDEO-PALETTE-PLAN.md, measured in
docs/reports/VIDEO-PALETTE-2026-10-12.md). An instrument: nothing in `make`
runs it, and it writes nothing.

    os88palsim.py CLIP [--start S] [--secs T] [--k 256|16] [--p P]
                       [--vbl N] [--size WxH] [--fps F]

The clip is run through the encoder's OWN ditherers (os88venc's
Vga8Ditherer at --k 256, KnollDitherer at --k 16, each at its defaults)
under each policy below, and every frame is scored as the PSNR of a 3 x 3
box blur of the source against the same blur of what the screen would show -
the blur is what an eye does to a dither. It models the PICTURE and not the
stream: the budget, the cuts and the bytes are the encoder's to say.

Shots are found as an encoder would find them - PySceneDetect's content
rule, mean HSV change between frames over 27, shots under MINSHOT frames
merged into the one before - and each shot's palette is ffmpeg's
palettegen over that shot alone, as os88venc's vga8_palette is over a clip.

  EGA  (--k 16 only) the EGA's sixteen: VGA4's `auto`
  G    one palette for the clip: VGA8's `auto`, VGA4's `clip16`
  S    a palette per shot, swapped at the cut. The ideal: on a screen that
       is not flipped it FLASHES; on a flipped VGA4 screen it is exactly the
       Color Select bank switch (VIDEO-PALETTE-PLAN 2.2)
  SP   P colours a shot, swapped at the cut (the ideal at P)
  RP   P a shot RECYCLED (VIDEO-PALETTE-PLAN 2.1): only DAC slots no pixel
       on the glass names are ever written - asserted - so it is invisible on
       any screen; the next shot's colours are preloaded the frame before
       its cut
  FP   RP, plus up to VBL slots the outgoing page still shows written in the
       blank that latches the cut's page (flipped screens only)

and CGA4: cga4_pick for the whole clip and for each shot.
"""
import argparse, collections, os, subprocess, sys, tempfile
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88venc as ve
import os88vid as vid

ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
ap.add_argument("clip")
ap.add_argument("--start", type=float, default=0)
ap.add_argument("--secs", type=float, default=60)
ap.add_argument("--k", type=int, default=256)
ap.add_argument("--p", type=int, default=160)
ap.add_argument("--vbl", type=int, default=48)
ap.add_argument("--size", default="320x180")
ap.add_argument("--fps", type=int, default=12)
ap.add_argument("--fence", action="store_true",
                help="RP/FP dither over the shot's OWN slots only (and 0):"
                     " without it a shot spreads over every slot near its"
                     " colours, pins them all, and leaves no room")
A = ap.parse_args()
W, H = map(int, A.size.split("x"))
FPS = A.fps
CLIP, SECS, P, VBL, K = A.clip, A.secs, A.p, A.vbl, A.k
MINSHOT = 6


def decode():
    vf = ("fps=%d,scale=%d:%d:force_original_aspect_ratio=increase,"
          "crop=%d:%d,format=rgb24" % (FPS, W, H, W, H))
    p = subprocess.run(["ffmpeg", "-v", "error", "-ss", str(A.start),
                        "-t", str(SECS), "-i", CLIP, "-vf", vf, "-an",
                        "-f", "rawvideo", "-"],
                       capture_output=True, check=True).stdout
    return np.frombuffer(p, np.uint8).reshape(-1, H, W, 3)


def hsv(f):
    f = f.astype(np.float32) / 255
    mx, mn = f.max(2), f.min(2)
    v, c = mx, mx - mn
    s = np.where(mx > 0, c / np.maximum(mx, 1e-6), 0)
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    cc = np.maximum(c, 1e-6)
    h = np.where(mx == r, ((g - b) / cc) % 6,
                 np.where(mx == g, (b - r) / cc + 2, (r - g) / cc + 4)) / 6
    h = np.where(c > 0, h, 0)
    return np.stack([h, s, v], -1) * 255


def shots(fr):
    hv = [hsv(f[::2, ::2]) for f in fr]
    cut = [0]
    for i in range(1, len(fr)):
        d = np.abs(hv[i] - hv[i - 1]).mean()
        if d > 27 and i - cut[-1] >= MINSHOT:
            cut.append(i)
    return cut


def palgen(fs, n):
    with tempfile.TemporaryDirectory() as t:
        raw, out = os.path.join(t, "f.raw"), os.path.join(t, "p.png")
        fs.tofile(raw)
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo",
                        "-pix_fmt", "rgb24", "-s", "%dx%d" % (W, H), "-i", raw,
                        "-vf", "palettegen=max_colors=%d:stats_mode=full:"
                        "reserve_transparent=0" % max(n, 4), "-frames:v", "1",
                        out], check=True)
        from PIL import Image
        rgb = np.array(Image.open(out).convert("RGB")).reshape(-1, 3)
    cols = sorted({tuple((int(v) * 63 + 127) // 255 for v in c) for c in rgb})
    if (0, 0, 0) not in cols:
        cols = [(0, 0, 0)] + cols[:n - 1]
    else:
        cols.remove((0, 0, 0))
        cols = [(0, 0, 0)] + cols
    cols = np.array(cols[:n], np.float32) * (255 / 63)
    return cols


def full(cols):
    return np.concatenate([cols, np.repeat(cols[-1:], 256 - len(cols), 0)])


def to6(d):
    return np.rint(d * 63 / 255).astype(np.uint8)


def mk(dac, active=None):
    """The ditherer for `dac`. With `active` (a mask of slots), the
    nearest-colour table sees only those - every other slot is made slot 0's
    black, and a tie goes to the lowest index - while what a held pixel is
    measured against stays the DAC's real colours"""
    real = dac.copy()
    real[K:] = real[K - 1]
    pal = real.copy()
    if active is not None:
        pal[:K][~active[:K]] = pal[0]
        pal[K:] = pal[0]
    if K == 256:
        d = ve.Vga8Ditherer(W, H, to6(pal).tobytes(), 24.0, 18.0, 16.0)
    else:
        d = ve.KnollDitherer(W, H, to6(pal).tobytes(), 24.0, 16.0)
    if active is not None:
        d.pal = np.rint(real * 63 / 255) * (255 / 63)
    return d


def blur(a):
    a = a.astype(np.float32)
    p = np.pad(a, ((1, 1), (1, 1), (0, 0)), mode="edge")
    return sum(p[y:y + H, x:x + W] for y in range(3) for x in range(3)) / 9


def psnr(src, out):
    mse = ((blur(src) - blur(out)) ** 2).mean()
    return 10 * np.log10(255 ** 2 / max(mse, 1e-6))


def used(i):
    return np.bincount(i.ravel(), minlength=256)[:256]


fr = decode()
cut = shots(fr)
bounds = list(zip(cut, cut[1:] + [len(fr)]))
print("%s: %d frames at %d fps, %d shots (median %d frames)" % (
    os.path.basename(CLIP), len(fr), FPS, len(bounds),
    int(np.median([b - a for a, b in bounds]))))
sys.stdout.flush()

res = collections.OrderedDict()


def plain(pals, mk):
    out, prev = [], None
    for (a, b), p in zip(bounds, pals):
        d = mk(p)
        for f in fr[a:b]:
            i = d(f)
            out.append((psnr(f, p[i]), 0 if prev is None else
                        int((i != prev).sum())))
            prev = i
    return out


if K == 16:
    res["EGA"] = plain([full(np.frombuffer(vid.STD16, np.uint8).astype(
        np.float32).reshape(16, 3) * (255 / 63))] * len(bounds), mk)
res["G"] = plain([full(palgen(fr, K))] * len(bounds), mk)
res["S"] = plain([full(palgen(fr[a:b], K)) for a, b in bounds], mk)


def ranked(cols, a, b):
    d = mk(full(cols))
    c = np.zeros(256, np.int64)
    for f in fr[a:b:3]:
        c += used(d(f))
    return cols[np.argsort(-c[:len(cols)], kind="stable")]


sP = [ranked(palgen(fr[a:b], P), a, b) for a, b in bounds]
res["SP"] = plain([full(c) for c in sP], mk)
NEAR = 255 / 63 * 1.0


def recycle(flip):
    dac = np.zeros((256, 3), np.float32)
    first = [c for c in sP[0] if c.max() > 0][:K - 1]
    if first:                       # (a shot of black has no others)
        dac[1:1 + len(first)] = first

    def fence(si):
        if not A.fence:
            return None
        act = np.zeros(256, bool)
        act[0] = True
        for c in sP[si]:
            act[:K] |= np.abs(dac[:K] - c).max(1) <= NEAR
        return act
    act = fence(0)
    d = mk(dac, act)
    out, prev, ev = [], None, []
    for si, (a, b) in enumerate(bounds):
        nxt = list(sP[si + 1]) if si + 1 < len(bounds) else []
        for fi in range(a, b):
            f = fr[fi]
            gl = used(prev) > 0 if prev is not None else np.zeros(256, bool)
            gl[0] = True
            want = list(sP[si]) + (nxt if fi == b - 1 else [])
            miss = [c for c in want if np.abs(dac[:K] - c).max(1).min() > NEAR]
            if miss:
                wa = np.array(want)
                keep = lambda k: -np.abs(wa - dac[k]).max(1).min()
                free = sorted((k for k in range(1, K) if not gl[k]), key=keep)
                n = min(len(miss), len(free))
                for k, c in zip(free[:n], miss[:n]):
                    assert not gl[k]
                    dac[k] = c
                miss = miss[n:]
                vb = 0
                if flip and fi == a and miss:
                    live = sorted((k for k in range(1, K) if gl[k]),
                                  key=keep)[:VBL]
                    for k, c in zip(live, miss):
                        dac[k] = c
                        vb += 1
                if n or vb:
                    ev.append((si, fi - a, n, vb))
            na = fence(si)
            if ev and ev[-1][:2] == (si, fi - a) or (
                    na is not None and not np.array_equal(na, act)):
                act = na
                nd = mk(dac, act)
                nd.prev = d.prev
                if hasattr(d, "at"):
                    nd.at = d.at
                d = nd
            i = d(f)
            out.append((psnr(f, full(dac[:K])[i]), 0 if prev is None else
                        int((i != prev).sum())))
            prev = i
    return out, ev


res["RP"], evR = recycle(False)
res["FP"], evF = recycle(True)
print("K %d, P %d, VBL %d.  PSNR of a 3x3 blur, dB: mean / worst frame / "
      "mean of each shot's cut frame; pixels changed a frame" % (K, P, VBL))
for k, v in res.items():
    v = np.array(v)
    cf = [v[a, 0] for a, _ in bounds[1:]]
    print("  %-4s %6.2f  %6.2f  %6.2f   %7.0f" % (
        k, v[:, 0].mean(), v[:, 0].min(), np.mean(cf), v[1:, 1].mean()))
gain = np.array([np.mean([res["S"][i][0] - res["G"][i][0]
                          for i in range(a, b)]) for a, b in bounds])
print("S over G a shot: median %+.2f, best %+.2f, worst %+.2f dB; "
      "shots >= +2 dB: %d of %d" % (np.median(gain), gain.max(), gain.min(),
                                    (gain >= 2).sum(), len(gain)))
nb = sum(n for _, _, n, _ in evR)
print("RP: %d DAC events, %d slots written (%d bytes), max %d in one, "
      "%d in the frame before a cut" % (
          len(evR), nb, 3 * nb, max([n for _, _, n, _ in evR] or [0]),
          sum(n for s, f, n, _ in evR if f == bounds[s][1] - bounds[s][0]
              - 1)))
vbs = [v for _, _, _, v in evF if v]
print("FP: blank writes at %d cuts, max %d" % (len(vbs), max(vbs or [0])))
# CGA4 picks
whole = ve.cga4_pick(list(fr[::4]))
picks = collections.Counter(ve.cga4_pick(list(fr[a:b])) for a, b in bounds)


def name(sel):
    s = "m5 cyan/red/white" if sel & 0x40 else \
        ("cyan/magenta/white" if sel & 0x20 else "green/red/brown")
    return "%02Xh %s%s bg %d" % (sel, "bright " if sel & 16 else "", s,
                                 sel & 15)


print("CGA4 whole clip: %s; per shot: %s" % (name(whole), ", ".join(
    "%s x%d" % (name(k), n) for k, n in picks.most_common(6))))
