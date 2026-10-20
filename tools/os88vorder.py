#!/usr/bin/env python3
"""os88vorder - what the ORDER of a sixteen-colour file's colours costs, and
what a scene's ditherer that starts from the last scene's screen buys
(docs/reports/VIDEO-ORDER-2026-10-13.md). An instrument: nothing in `make`
runs it, and it changes no shipped byte.

    os88vorder.py decode FILE.V88 OUT.npz
    os88vorder.py choose IN.npz OUT.json [--guard old|target] [--lam L]
    os88vorder.py encode [--order OUT.json] [--hold-across] -- ARGS...
    os88vorder.py copies IN.npz [--us US] [--profile P]

A VGA4 pixel's value is its colour's SLOT, and its four bits are its four
planes, so which slot each colour takes decides how many planes a change
between two colours writes - 7 to 8 is all four, 7 to 5 is one. The
encoder sorts each scene's sixteen darkest first (colour_kmeans); this asks
what an order chosen for the planes would save, and whether one can also
keep the switch at a scene cut from showing the old picture in colours far
from its own.

`decode` keeps every frame's four planes of a VGA4 file (SPEC.md 98.1.3.2)
and its palette table (98.1.3.2.3). `choose` picks an order a scene, slot 0
kept black: a local search over pair swaps on the planes the scene's own
changes write (each change a -> b costs popcount(slot a ^ slot b)), plus the
cut from the last scene's screen, plus `--lam` times the CIELAB distance,
pixel by pixel, between the last scene's screen shown in the new colours and
`--guard`: the colour it was (`old`) or the new scene's own (`target`). It
prints, against darkest first, the stores (vga4_subs' rule) on sampled frame
pairs and both distances. `encode` runs os88venc with each scene's sixteen
put in that order - the dither is the same picture in other slots, since
every choice it makes is by colour - and with `--hold-across`, each scene's
ditherer starting from the last one's screen, so the colour hold
(--vga4-hold) keeps a pixel whose slot's NEW colour is already near the
source. The order is read off a DECODED file, so a choose made from one
encode is the order for the next encode of the same clip and options.

`copies` prices VRAM LATCH COPIES (docs/plans/VIDEO-LATCH-PLAN.md) on a
decoded file: one frame pair in twelve, 8 x 8-byte blocks, every shift that
keeps the 4 x 4 dither cell (a whole byte across, a multiple of 4 down, up
to 16), each block copied when its stores at STORE_US beat the copy's
`--us` a byte plus its set-up; then the residual record through the shipped
writer and the profile's own table, against the record with no copies."""
import json, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SETTLE = 12         # frames after a cut its picture is taken as whole
POP = np.array([bin(i).count("1") for i in range(16)])


def lab_of(c6):
    """the DAC's six bits -> CIELAB (D65)"""
    c = np.asarray(c6, float) / 63
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722],
                  [0.0193, 0.1192, 0.9505]])
    xyz = c @ m.T / np.array([0.95047, 1, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]),
                     200 * (f[..., 1] - f[..., 2])], -1)


def planes_of(ix):
    return np.stack([np.packbits((ix >> p) & 1, axis=-1) for p in range(4)])


def stores(tp, sp):
    """vga4_subs' stores: at a byte where a plane changes, the planes that
    want the same value are one store"""
    ch = tp != sp
    tot = 0
    for p in range(4):
        dup = np.zeros(tp.shape[1:], bool)
        for q in range(p):
            dup |= ch[q] & (tp[q] == tp[p])
        tot += int((ch[p] & ~dup).sum())
    return tot


def cmd_decode(src, out):
    import os88vid as vid
    r = vid.Reader(src)
    g = r.g
    if not g.bitplanes or not r.pev:
        sys.exit("os88vorder: %s is not a VGA4 file with a palette a scene"
                 % src)
    at = np.array(g.base)[:, None] + np.arange(g.wb)[None, :]
    planes = np.zeros((r.frames, 4, g.h, g.wb), np.uint8)
    surf = g.surface()
    for f, (rec, _, _) in enumerate(r.records()):
        r.apply(surf, rec, check=False)
        planes[f] = np.frombuffer(bytes(surf), np.uint8).reshape(4, -1)[:, at]
    np.savez(out, planes=planes,
             pev=np.array([[f] + list(p) for f, p in r.pev], np.int32))


def load(path):
    z = np.load(path)
    b = np.unpackbits(z["planes"], axis=-1)
    idx = (b[:, 0] | b[:, 1] << 1 | b[:, 2] << 2 | b[:, 3] << 3)
    return idx.astype(np.uint8), z["pev"][:, 0], \
        z["pev"][:, 1:].reshape(-1, 16, 3)


def cmd_choose(npz, out, guard="old", lam=0.2):
    idx, pf, pals = load(npz)
    F = len(idx)
    ends = list(pf[1:]) + [F]
    LAB = lab_of(pals)
    ident = np.arange(16)

    def search(cost):
        sig = ident.copy()
        c = cost(sig)
        while True:
            best = None
            for a in range(1, 16):
                for b in range(a + 1, 16):
                    q = sig.copy()
                    q[a], q[b] = q[b], q[a]
                    cq = cost(q)
                    if cq < c:
                        c, best = cq, q
            if best is None:
                return sig
            sig = best

    def intra(q, ts):
        return sum(stores(planes_of(q[idx[t]].astype(np.uint8)),
                          planes_of(q[idx[t - 1]].astype(np.uint8)))
                   for t in ts)

    sigs, prev = [], ident
    tot = np.zeros(4)
    dist = [[], [], [], []]
    for s in range(len(pf)):
        f, e = pf[s], ends[s]
        a0 = f + SETTLE + 1 if s else 1
        T = np.zeros((16, 16), np.int64)
        for t in range(a0, e):
            m = idx[t] != idx[t - 1]
            np.add.at(T, (idx[t - 1][m], idx[t][m]), 1)
        C = np.zeros((16, 16), np.int64)
        D = np.zeros((16, 16))
        if s:
            old, tg = idx[f - 1], idx[min(f + SETTLE, e - 1)]
            np.add.at(C, (old.ravel(), tg.ravel()), 1)
            for i in range(16):
                for j in range(16):
                    if guard == "target":
                        D[i, j] = (C[i] * np.sqrt(((LAB[s][j] - LAB[s]) ** 2)
                                                  .sum(-1))).sum()
                    else:
                        D[i, j] = C[i].sum() * np.sqrt(
                            ((LAB[s][j] - LAB[s - 1][i]) ** 2).sum())
        oslot = prev

        def cost(q):
            c = (T * POP[q[:, None] ^ q[None, :]]).sum()
            c += (C * POP[oslot[:, None] ^ q[None, :]]).sum()
            return c + lam * D[np.arange(16), np.argsort(q)[oslot]].sum()

        sig = search(cost)
        ts = list(range(a0, e, 6))[:12]
        k = (e - a0) / len(ts) if ts else 0
        for n, (q, qp) in enumerate(((ident, ident), (sig, prev))):
            tot[n] += intra(q, ts) * k
            if s:
                o = qp[idx[f - 1]].astype(np.uint8)
                tg = idx[min(f + SETTLE, e - 1)]
                tot[2 + n] += stores(planes_of(q[tg].astype(np.uint8)),
                                     planes_of(o))
                shown = LAB[s][np.argsort(q)[o]]
                dist[2 * n].append(np.sqrt(((shown - LAB[s][tg]) ** 2)
                                           .sum(-1)).mean())
                dist[2 * n + 1].append(np.sqrt(
                    ((shown - LAB[s - 1][idx[f - 1]]) ** 2).sum(-1)).mean())
        sigs.append([int(v) for v in sig])
        prev = sig
    json.dump(sigs, open(out, "w"))
    pc = lambda a, b: 100 * (a / b - 1)
    print("stores: within scenes %+.1f%%, at the cuts %+.1f%%, all %+.1f%%"
          % (pc(tot[1], tot[0]), pc(tot[3], tot[2]),
             pc(tot[1] + tot[3], tot[0] + tot[2])))
    print("the last screen in the new colours: %.1f dE from the new scene's "
          "(darkest first %.1f), %.1f from its own (%.1f)"
          % (np.mean(dist[2]), np.mean(dist[0]), np.mean(dist[3]),
             np.mean(dist[1])))


def cmd_encode(order, hold_across, args):
    import os88venc
    if order:
        sigs = json.load(open(order))
        made = os88venc.colour_kmeans
        n = [0]

        def ordered(px, colours=16):
            p = made(px, colours)
            sig = sigs[n[0]] if n[0] < len(sigs) else list(range(16))
            n[0] += 1
            if sig[0] != 0 or sorted(sig) != list(range(16)):
                raise SystemExit("os88vorder: scene %d's order" % (n[0] - 1))
            out = [None] * 16
            for j in range(16):
                out[sig[j]] = p[3 * j:3 * j + 3]
            return b"".join(out) + p[48:]
        os88venc.colour_kmeans = ordered
    if hold_across:
        def settle(self, item, r):
            n, f = item
            k = self.which(n)
            last = getattr(self, "_last", None)
            if last is not None and k != last:
                a, b = self.ds[last], self.ds[k]
                if b.prev is None and a.prev is not None:
                    b.prev, b.at, b.score = a.prev, a.at, None
            self._last = k
            return self.ds[k].settle(f, r)
        os88venc.SceneDitherer.settle = settle
    sys.argv = ["os88venc.py"] + args
    return os88venc.main()


def cmd_copies(npz, cus=2.3, profile="286-fast"):
    import os88vid as vid, os88venc as venc
    BLK_US, ROW_US, STORE_US, BH, BW = 15.0, 1.5, 1.6, 8, 8
    idx, pf, pals = load(npz)
    F = len(idx)
    ends = list(pf[1:]) + [F]
    h, w = idx.shape[1:]
    g = vid.Geom(vid.LAY_LIN80, w // 8, h, bitplanes=True)
    g.sub2 = True
    prof = venc.PROFILES[profile]
    tab, sub = venc.profile_table(prof, vid.LAY_LIN80)
    k = vid.HZ / 1e6 * (prof.get("speed") or 1)

    def us(new, old):
        rec = vid.record(vid.vga4_subs(bytes(new.ravel()), bytes(old.ravel()),
                                       g), g, limit=None)
        return vid.cycles_of(rec, True, table=tab, sub=sub, sub2=True) / k, \
            len(rec)

    def smap(tp, sp):
        ch = tp != sp
        tot = np.zeros(tp.shape[1:], np.int32)
        for p in range(4):
            dup = np.zeros(tp.shape[1:], bool)
            for q in range(p):
                dup |= ch[q] & (tp[q] == tp[p])
            tot += ch[p] & ~dup
        return tot
    nby, nbx = h // BH, (w // 8) // BW
    blk = BLK_US + BH * ROW_US + BH * BW * cus
    pairs = [t for s in range(len(pf))
             for t in range(pf[s] + SETTLE + 1 if s else 1, ends[s], 12)]
    T0 = T1 = B0 = B1 = nb = 0
    for t in pairs:
        cur, prev = idx[t], idx[t - 1]
        cp, pp = planes_of(cur), planes_of(prev)
        cut = lambda m: m[:nby * BH, :nbx * BW].reshape(
            nby, BH, nbx, BW).sum((1, 3))
        base = cut(smap(cp, pp)) * STORE_US
        best, sh = base.copy(), np.zeros(base.shape + (2,), int)
        for dy in range(-16, 17, 4):
            for dx in range(-2, 3):
                if not (dy or dx):
                    continue
                m = smap(cp, np.roll(pp, (dy, dx), axis=(1, 2))).astype(float)
                if dy > 0:
                    m[:dy] = 1e9
                if dy < 0:
                    m[dy:] = 1e9
                if dx > 0:
                    m[:, :dx] = 1e9
                if dx < 0:
                    m[:, dx:] = 1e9
                c = cut(m) * STORE_US + blk
                b = c < best
                best, sh[b] = np.where(b, c, best), (dy, dx)
        npp, n = pp.copy(), 0
        for by in range(nby):
            for bx in range(nbx):
                if best[by, bx] < base[by, bx]:
                    dy, dx = sh[by, bx]
                    y0, x0 = by * BH, bx * BW
                    npp[:, y0:y0 + BH, x0:x0 + BW] = \
                        pp[:, y0 - dy:y0 - dy + BH, x0 - dx:x0 - dx + BW]
                    n += 1
        b = np.unpackbits(npp, axis=-1)
        scr = (b[0] | b[1] << 1 | b[2] << 2 | b[3] << 3).astype(np.uint8)
        u0, b0 = us(cur, prev)
        u1, b1 = us(cur, scr)
        T0, T1 = T0 + u0, T1 + u1 + n * blk
        B0, B1 = B0 + b0, B1 + b1 + n * 6 + (2 if n else 0)
        nb += n
    print("%s, %.2f us a copied byte: %d pairs, %.1f blocks a frame; decode "
          "%+.1f%%, bytes %+.1f%%" % (profile, cus, len(pairs),
                                      nb / len(pairs), 100 * (T1 / T0 - 1),
                                      100 * (B1 / B0 - 1)))


def main():
    a = sys.argv[1:]
    if a[:1] == ["decode"] and len(a) == 3:
        return cmd_decode(a[1], a[2])
    if a[:1] == ["choose"] and len(a) >= 3:
        g = a[a.index("--guard") + 1] if "--guard" in a else "old"
        lam = float(a[a.index("--lam") + 1]) if "--lam" in a else 0.2
        return cmd_choose(a[1], a[2], g, lam)
    if a[:1] == ["encode"] and "--" in a:
        pre, args = a[1:a.index("--")], a[a.index("--") + 1:]
        order = pre[pre.index("--order") + 1] if "--order" in pre else None
        return cmd_encode(order, "--hold-across" in pre, args)
    if a[:1] == ["copies"] and len(a) >= 2:
        cu = float(a[a.index("--us") + 1]) if "--us" in a else 2.3
        pr = a[a.index("--profile") + 1] if "--profile" in a else "286-fast"
        return cmd_copies(a[1], cu, pr)
    sys.exit(__doc__)


if __name__ == "__main__":
    # the encoder's pool spawns workers that import this file again; the
    # patches above are the parent's alone, and the ditherers' settle runs
    # in the parent
    sys.exit(main())
