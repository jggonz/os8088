#!/usr/bin/env python3
"""os88vcomp - would another CODER make a .V88 smaller, and at what decode?

The instrument behind docs/reports/VIDEO-COMPRESSION-2026-10-20.md. Host-side
and read-only: it reads .V88 files through tools/os88vid.py's own Reader and
writer, and never writes one.

    os88vcomp.py bytes FILE.V88...           where a stream's bytes go, and
                                             what LZ4 / LZB / zlib take off
    os88vcomp.py overhead FILE.V88...        picture, sound, keys, padding
    os88vcomp.py tiles LOSSLESS.V88          one bit a pixel: an 8 x 4 tile
                                             CODEBOOK beside today's lists
    os88vcomp.py video1 SRC LOSSLESS.V88 [OTHER.V88...]
                                             VGA8 / VGA4: Microsoft Video-1's
                                             block coder from the SOURCE,
                                             against .V88s of the same clip
    os88vcomp.py budget1 LOSSLESS.V88        one bit UNDER A BUDGET (default
                                             5150-st225's): today's lists
                                             against tile codebooks
    os88vcomp.py budgetc SRC LOSSLESS.V88    colour under a budget: today's
                                             changes against block modes -
                                             pfill (a DITHERED fill), two,
                                             fill, eight, cb (a codebook)

Every byte count of today's format is the real writer's (`spans`, `record`)
and every 8088 cycle figure for it is `cycles_of`, wave 0's model. The NEW
coders' cycle figures are ESTIMATES by instruction count against the
8088's fetch floor - TILE_CYC and V1_CYC below - and say so in the report.

`video1` scores against the source clip (ffmpeg reads it), aligned by
search and TONE-MATCHED onto the lossless .V88 (--tone 0 to skip): the
encoder chooses its own levels, and a darker look must not score as
compression loss. Three readings: plain PSNR, and 2 x 2 and 4 x 4
box-blurred ("seen" from further back; the 4 x 4 one cannot see a 4 x 4
block coder's own edges, which is why it is never quoted alone).
"""
import argparse
import os
import struct
import subprocess
import sys
import zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88vid as V             # noqa: E402
import os88lz as L              # noqa: E402

SECTOR = 512
HZ = 4.77e6
TILE_CYC = 225.0    # an 8 x 4 one-bit tile from a four-plane codebook, ESTIMATE
V1_CYC = {          # 8088 cycles a Video-1 block, ESTIMATES
    "lin320": dict(skip=40, fill=200, two=500, det=700),
    "modex": dict(skip=40, fill=120, two=380, det=620),
    "vga4": dict(skip=30, fill=170, two=400, det=560),
}


# ---- pictures and scores ------------------------------------------------

def blur(a, k=4):
    pad = np.pad(a, ((k // 2, k - 1 - k // 2), (k // 2, k - 1 - k // 2)) +
                 ((0, 0),) * (a.ndim - 2), mode="edge")
    c = np.concatenate([np.zeros_like(pad[:1]), pad.cumsum(0)], 0)
    a1 = (c[k:] - c[:-k]) / k
    c = np.concatenate([np.zeros_like(a1[:, :1]), a1.cumsum(1)], 1)
    return (c[:, k:] - c[:, :-k]) / k


def psnr(mse):
    return 99.0 if mse <= 1e-9 else 10 * np.log10(255 * 255 / mse)


def scores(A, B):
    A, B = list(A), list(B)
    out = []
    for k in (1, 2, 4):
        f = (lambda x: x) if k == 1 else (lambda x, k=k: blur(x, k))
        out.append(psnr(np.mean([((f(a) - f(b)) ** 2).mean()
                                 for a, b in zip(A, B)])))
    return "plain %5.2f  2x2 %5.2f  4x4 %5.2f dB" % tuple(out)


def v88_rgb(path):
    """[(rgb float32 0..255, palette)] for every frame as the glass shows it"""
    r = V.Reader(path)
    g = r.g
    out = []
    for f, surf, *_ in V.v88_frames(r, check=False):
        if r.pixfmt in (V.PF_VGA8, V.PF_VGA4):
            cv = np.frombuffer(g.canvas(surf), np.uint8)[:g.w * g.h] \
                .reshape(g.h, g.w)
            p = V.palette_at(r, f) or V.STD16
            pal = np.frombuffer(bytes(p), np.uint8).astype(np.float32) \
                .reshape(-1, 3) * 255 / 63
            if r.pixfmt == V.PF_VGA4:
                cv, pal = cv & 15, pal[:16]
            out.append((pal[cv], pal))
        else:
            rgb = V.canvas_rgb(r.pixfmt, g.canvas(surf), g) * 255 / 63
            out.append((rgb.astype(np.float32), None))
    return r, out


def stream_stats(path):
    """(KB/s of records less their sound, % of a 4.77 MHz 8088 by cycles_of)"""
    r = V.Reader(path)
    g = r.g
    byt = cyc = n = 0
    for rec, *_ in r.records():
        byt += len(rec) - r.abytes
        n += 1
        try:
            cyc += V.cycles_of(rec, planar=g.planes > 1, layout=g.layout,
                               sub2=g.sub2, bitplanes=g.bitplanes,
                               blocks=("onebit" if g.layout in
                                       V.ONEBIT_LAYOUTS else True)
                               if getattr(g, "v2", False) else False)
        except V.V88Error:
            pass
    return byt / n * r.fps / 1024, 100 * cyc / n * r.fps / HZ


# ---- bytes and overhead -------------------------------------------------

def super_packets(r):
    at, nsec = r.sp0, r.sp0n
    while nsec:
        sp = r.d[at:at + nsec * SECTOR]
        nf, nxt = struct.unpack_from("<HH", sp, 0)
        o, recs = 4, []
        for _ in range(nf):
            n = struct.unpack_from("<H", sp, o)[0]
            recs.append(sp[o:o + n])
            o += n
        yield nsec * SECTOR, recs
        at, nsec = at + nsec * SECTOR, nxt


def cmd_bytes(a):
    for path in a.files:
        r = V.Reader(path)
        if r.resident:
            print("%-14s RESIDENT, skipped" % os.path.basename(path))
            continue
        g = r.g
        planar = g.planes > 1
        sps = list(super_packets(r))
        tot = sum(s for s, _ in sps)
        step = max(1, int(tot / a.budget))
        pb = lz4 = lzb = zl = 0
        for _, rs in sps[::step]:
            P = b"".join(x[:len(x) - r.abytes] if r.abytes else x for x in rs)
            pb += len(P)
            lz4 += len(L.lz4_compress(P, tail=False))
            lzb += len(L.lzb_compress(P))
            zl += len(zlib.compress(P, 9))
        # where a record's bytes go: the writer's grammar, entry by entry
        C = dict.fromkeys(("head", "segs", "P addr", "P data", "slice", "run"), 0)
        nP = [0] * 7
        buf = bytearray(65536)
        recs = [x for _, rs in sps for x in rs]
        for rec in recs[::max(1, len(recs) // a.records)]:
            st = {}

            def seg(ab):
                C["segs"] += 1 if ab else 3
                st["abs"] = ab

            def wr(k, di, m):
                ab = st.get("abs")
                if k <= V.L_P6:
                    C["P addr"] += 2 if ab else 1
                    C["P data"] += m
                    nP[m] += 1
                elif k in (V.L_SLICE, V.L_SLICEL):
                    C["slice"] += (2 if ab else 1) + (1 if k == V.L_SLICE
                                                      else 2) + m
                else:
                    C["run"] += (2 if ab else 1) + (1 if k == V.L_RUN
                                                    else 2) + 1
            C["head"] += 6
            if planar:
                for lead, s, e in V.subs_of(rec, g):
                    C["head"] += 1 + (2 if g.sub2 else 0)
                    V.walk_sub(buf, rec, s, g, wr, seg)
            else:
                C["head"] += 10
                V.walk_lists(buf, rec, 6, wr, seg)
        t = sum(C.values())
        print("%-14s %-7s LZ4 %5.1f%%  LZB %5.1f%%  zlib %5.1f%% of the "
              "picture bytes | %s | P1 %.0f%% of entries" % (
                  os.path.basename(path), V.PF_NAMES[r.pixfmt],
                  100 * lz4 / pb, 100 * lzb / pb, 100 * zl / pb,
                  "  ".join("%s %.1f%%" % (k, 100 * v / t)
                            for k, v in C.items()),
                  100 * nP[1] / max(1, sum(nP))))
        sys.stdout.flush()


def cmd_overhead(a):
    for path in a.files:
        r = V.Reader(path)
        if r.resident:
            continue
        fs = os.path.getsize(path)
        sp = recb = nrec = 0
        for s, rs in super_packets(r):
            sp += s
            recb += sum(len(x) for x in rs)
            nrec += len(rs)
        keyb = sum(e[2] for e in r.keys)
        aud = nrec * r.abytes
        print("%-14s %5.1f MB  picture %5.1f%%  sound %5.1f%%  keys %4.1f%%  "
              "padding %4.1f%%  rest %4.1f%% (key parts, pool, layer)" % (
                  os.path.basename(path), fs / 1e6, 100 * (recb - aud) / fs,
                  100 * aud / fs, 100 * keyb / fs, 100 * (sp - recb) / fs,
                  100 * (fs - sp - keyb) / fs))


# ---- one bit: a tile codebook ------------------------------------------

POP = np.array([bin(i).count("1") for i in range(256)], np.uint8)


def popc(x):
    return POP[x.view(np.uint8).reshape(x.shape + (8,))].sum(-1) \
        .astype(np.int32)


def tiles_of(bits, th):
    h, w = bits.shape
    h2 = h - h % th
    by = np.packbits(bits[:h2].astype(np.uint8), axis=1)
    t = by.reshape(h2 // th, th, -1).transpose(0, 2, 1)
    pad = np.zeros(t.shape[:2] + (8,), np.uint8)
    pad[..., :th] = t
    return pad.view(np.uint64)[..., 0]


def untile(codes, th, h2, wb):
    b = codes[..., None].view(np.uint8)[..., :th]
    return np.unpackbits(b.transpose(0, 2, 1).reshape(h2, wb), axis=1)


def lists_cost(g, prev_cv, cv):
    """today's record taking canvas prev_cv to cv: (bytes, 8088 cycles)"""
    wb, h = g.wb, g.h
    a = np.frombuffer(prev_cv, np.uint8).reshape(h, wb)
    b = np.frombuffer(cv, np.uint8).reshape(h, wb)
    ys, xs = np.nonzero(a != b)
    surf = bytearray(65536)
    for y in np.unique(ys):
        surf[g.base[y]:g.base[y] + wb] = b[y].tobytes()
    ops = V.spans((np.array(g.base)[ys] + xs).tolist(), surf, g)
    rec = V.record(ops, g, limit=None)
    return len(rec), V.cycles_of(rec, layout=g.layout)


def cmd_tiles(a):
    th, ncb, addr = a.rows, a.codebook, 1.1
    r, F = v88_rgb(a.file)
    g = r.g
    if g.planes > 1 or r.pixfmt not in (V.PF_MONO1, V.PF_CGACOMP):
        sys.exit("tiles: a one-bit .V88")
    F = [(rgb[..., 0] > 127).astype(np.uint8) for rgb, _ in F]
    h2 = g.h - g.h % th
    T = np.stack([tiles_of(b, th) for b in F])
    nf, ty, wb = T.shape
    rb = sum(len(rec) - r.abytes for rec, *_ in r.records())
    # the codebook: the clip's most frequent CHANGED tiles
    ch = np.concatenate([T[0].ravel()] +
                        [T[i][T[i] != T[i - 1]] for i in range(1, nf)])
    u, c = np.unique(ch, return_counts=True)
    o = np.argsort(-c)
    cb, cover = u[o][:ncb], c[o][:ncb].sum() / c.sum()
    allu, inv = np.unique(T, return_inverse=True)
    inv = inv.reshape(T.shape)
    best = np.zeros(len(allu), np.int64)
    bd = np.full(len(allu), 99, np.int32)
    for i in range(len(cb)):
        d = popc(allu ^ cb[i])
        m = d < bd
        bd[m], best[m] = d[m], i
    near, nd = cb[best[inv]], bd[inv]
    print("%s %s %dx%d, %d frames at %.2f fps: the .V88 is %.1f KB/s of "
          "picture; a codebook of %d 8x%d tiles holds %.1f%% of the changed "
          "tiles exactly" % (os.path.basename(a.file), g.name, wb * 8, g.h,
                             nf, r.fps, rb / nf * r.fps / 1024, ncb, th,
                             100 * cover))

    def canv(Dt, full):
        out = full.copy()
        out[:h2] = untile(Dt, th, h2, wb)
        return np.packbits(out, axis=1).tobytes()
    for k in a.k:
        for vq in (False, True):
            D = np.zeros((ty, wb), np.uint64)
            byt = cyc = seen = 0.0
            nidx = 0
            for i in range(nf):
                tgt = T[i]
                need = popc(D ^ tgt) > k        # within k bits: left stale
                Dv = D.copy()
                n_i = 0
                rawm = need
                if vq:
                    v = (D ^ tgt)[..., None].view(np.uint8)[..., :th]
                    use = need & (nd[i] <= k) & ((v != 0).sum(-1) >= 2)
                    Dv[use] = near[i][use]
                    n_i = int(use.sum())
                    rawm = need & ~use
                Dn = Dv.copy()
                Dn[rawm] = tgt[rawm]
                lb, lc = lists_cost(g, canv(Dv, F[i]), canv(Dn, F[i]))
                byt += lb + n_i * (1 + addr)
                cyc += lc + n_i * TILE_CYC
                nidx += n_i
                D = Dn
                p = F[i][:h2].astype(float) * 255
                q = untile(D, th, h2, wb).astype(float) * 255
                seen += ((blur(p) - blur(q)) ** 2).mean()
            if vq:
                byt += ncb * th
            print("  k=%d %-9s %6.1f KB/s  8088 %5.1f%%  tiles a frame %4.0f  "
                  "seen %5.1f dB" % (k, "codebook" if vq else "lists",
                                     byt / nf * r.fps / 1024,
                                     100 * cyc / nf * r.fps / HZ, nidx / nf,
                                     psnr(seen / nf)))
            sys.stdout.flush()


# ---- colour: Microsoft Video-1 -----------------------------------------

def src_frames(src, w, h, fps, n, start, secs):
    cmd = ["ffmpeg", "-loglevel", "error", "-ss", str(start), "-i", src,
           "-t", str(secs), "-vf", "fps=%.6f,scale=%d:%d:flags=area"
           % (fps, w, h), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    b = subprocess.run(cmd, capture_output=True, check=True).stdout
    return np.frombuffer(b, np.uint8).reshape(-1, h, w, 3) \
        .astype(np.float32)[:n]


class Nearest:
    """RGB -> the palette's nearest entry, through a 32 x 32 x 32 table"""

    def __init__(self, pal):
        q = (np.arange(32) * 8 + 4).astype(np.float32)
        grid = np.stack(np.meshgrid(q, q, q, indexing="ij"), -1) \
            .reshape(-1, 3)
        self.t = ((grid[:, None, :] - pal[None]) ** 2).sum(-1).argmin(-1)
        self.pal = pal

    def __call__(self, rgb):
        i = np.clip(rgb, 0, 255).astype(np.int32) >> 3
        return self.pal[self.t[(i[..., 0] << 10) | (i[..., 1] << 5) |
                               i[..., 2]]]


def split2(px, near):
    """Video-1's two colours: the pixels split about the block's mean
    luminance, each side's mean taken to the palette"""
    lum = px @ np.array([0.299, 0.587, 0.114], np.float32)
    m = lum > lum.mean(1, keepdims=True)
    c1 = m.sum(1, keepdims=True)
    c0 = px.shape[1] - c1
    m1 = (px * m[..., None]).sum(1) / np.maximum(c1, 1)
    m0 = (px * ~m[..., None]).sum(1) / np.maximum(c0, 1)
    m1 = np.where(c1 == 0, m0, m1)
    m0 = np.where(c0 == 0, m1, m0)
    return np.where(m[..., None], near(m1)[:, None], near(m0)[:, None])


def video1(S, pals, bw, fps, kind, t):
    """Video-1 on frames S. bw 4 (VGA8, CRAM8's own): SKIP (a run 2 bytes),
    FILL 2, 2-COLOUR 4 (a 16-bit mask and two), 8-COLOUR 10 (a pair a 2 x 2
    quadrant). bw 8 (VGA4, a byte column of four rows on mode 12h's planes):
    FILL, 2-COLOUR (four mask bytes and a byte of two colours, write mode 3),
    4-COLOUR (a pair a 4 x 4 half), 2 bits of mode a block. A block within
    `t` (mean squared RGB error a pixel) of the source takes the cheapest
    mode that is; one that none is takes the most detailed."""
    nf, h, w, _ = S.shape
    hb, wb = h // 4, w // bw
    cost = dict(fill=2, two=4, det=10) if bw == 4 else \
        dict(fill=1.25, two=5.25, det=6.25)
    cyc = V1_CYC[kind]
    nears = {}
    D = np.zeros((hb * 4, wb * bw, 3), np.float32)
    byt = cy = 0.0
    outs = []

    def blk(x):
        return x.reshape(hb, 4, wb, bw, 3).transpose(0, 2, 1, 3, 4) \
            .reshape(-1, 4 * bw, 3)
    for i in range(nf):
        key = pals[i].tobytes()
        if key not in nears:
            nears[key] = Nearest(pals[i])
        near = nears[key]
        Rb, Db = blk(S[i][:hb * 4, :wb * bw]), blk(D)

        def e(X):
            return ((Rb - X) ** 2).mean((1, 2))
        fill = np.repeat(near(Rb.mean(1))[:, None], 4 * bw, 1)
        two = split2(Rb, near)
        if bw == 4:
            q = Rb.reshape(-1, 2, 2, 2, 2, 3).transpose(0, 1, 3, 2, 4, 5) \
                .reshape(-1, 4, 3)
            det = split2(q, near).reshape(-1, 2, 2, 2, 2, 3) \
                .transpose(0, 1, 3, 2, 4, 5).reshape(-1, 16, 3)
        else:
            q = Rb.reshape(-1, 4, 2, 4, 3).transpose(0, 2, 1, 3, 4) \
                .reshape(-1, 16, 3)
            det = split2(q, near).reshape(-1, 2, 4, 4, 3) \
                .transpose(0, 2, 1, 3, 4).reshape(-1, 32, 3)
        mode = np.full(len(Rb), 3)
        mode[e(two) <= t] = 2
        mode[e(fill) <= t] = 1
        if t > 0:
            mode[e(Db) <= t] = 0
        out = np.where((mode == 0)[:, None, None], Db,
                       np.where((mode == 1)[:, None, None], fill,
                                np.where((mode == 2)[:, None, None], two,
                                         det)))
        sk = (mode == 0).astype(int)
        runs = (np.diff(np.concatenate([[0], sk])) == 1).sum()
        nb = np.bincount(mode, minlength=4)
        byt += runs * 2 + nb[1] * cost["fill"] + nb[2] * cost["two"] + \
            nb[3] * cost["det"] + 2
        cy += runs * cyc["skip"] + nb[1] * cyc["fill"] + \
            nb[2] * cyc["two"] + nb[3] * cyc["det"] + 1000
        D = out.reshape(hb, wb, 4, bw, 3).transpose(0, 2, 1, 3, 4) \
            .reshape(hb * 4, wb * bw, 3)
        outs.append(D)
    return byt / nf * fps / 1024, 100 * cy / nf * fps / HZ, outs


def cmd_video1(a):
    r, F = v88_rgb(a.lossless)
    g = r.g
    if r.pixfmt not in (V.PF_VGA8, V.PF_VGA4):
        sys.exit("video1: a VGA8 or VGA4 .V88")
    S = src_frames(a.src, g.w, g.h, r.fps, len(F) + 8, a.start, a.secs)
    # the .V88's frame i is the source's frame i + off: found, not assumed
    samp = list(range(5, len(F) - 5, max(1, len(F) // 12)))

    def miss(off):
        return np.mean([((blur(F[i][0]) - blur(S[i + off])) ** 2).mean()
                        for i in samp if 0 <= i + off < len(S)])
    off = min(range(-3, 4), key=miss)
    F, S = (F[-off:], S) if off < 0 else (F, S[off:])
    n = min(len(S), len(F))
    S, F = S[:n], F[:n]
    if a.tone:
        idx = [i for i in samp if i < n]
        A = np.stack([blur(S[i]) for i in idx])
        B = np.stack([blur(F[i][0]) for i in idx])
        curves = []
        for c in range(3):
            x, y = A[..., c].ravel(), B[..., c].ravel()
            bins = np.clip(x.astype(int) >> 3, 0, 31)
            cnt = np.bincount(bins, None, 32)
            cur = np.bincount(bins, y, 32) / np.maximum(cnt, 1)
            ok = cnt > 0
            cur = np.interp(np.arange(32), np.arange(32)[ok], cur[ok])
            curves.append(np.interp(np.arange(256), np.arange(32) * 8 + 4,
                                    cur))
        S = np.stack([np.take(curves[c], np.clip(S[..., c], 0, 255)
                              .astype(int)) for c in range(3)], -1) \
            .astype(np.float32)
    print("%s: %s %dx%d %s, %d frames at %.2f fps, against %s (offset %d%s)"
          % (os.path.basename(a.lossless), g.name, g.w, g.h,
             V.PF_NAMES[r.pixfmt], n, r.fps, os.path.basename(a.src), off,
             ", tone-matched" if a.tone else ""))
    for p in [a.lossless] + a.others:
        FF = F if p == a.lossless else v88_rgb(p)[1]
        if p != a.lossless and off < 0:
            FF = FF[-off:]
        kb, cpu = stream_stats(p)
        print("  %-16s %6.1f KB/s  8088 %4.0f%%  %s" % (
            os.path.basename(p), kb, cpu, scores(S, [x for x, _ in FF[:n]])))
        sys.stdout.flush()
    bw = 8 if r.pixfmt == V.PF_VGA4 else 4
    kind = "vga4" if bw == 8 else ("modex" if g.name == "modex"
                                   else "lin320")
    for t in a.t:
        kb, cpu, outs = video1(S, [p for _, p in F], bw, r.fps, kind, t)
        h, w = outs[0].shape[:2]
        print("  Video-1 t=%-6g %6.1f KB/s  8088 %4.0f%%  %s" % (
            t, kb, cpu, scores(S[:, :h, :w], outs)))
        sys.stdout.flush()


# ---- under a budget: what the encoder would choose ---------------------
#
# Both models below are the encoder's own shape (SPEC.md 98.2.1) reduced to
# what matters for a coder: a disk bucket (bytes a second, `depth` deep,
# started half full) and a CPU bucket (an average share, one second deep)
# with a per-frame ceiling. Every candidate competes on error fixed per
# byte, aged as the encoder ages a wrong pixel (1 + n/8); what does not
# fit stays wrong, as a cut frame leaves it. Today's own changes are priced
# per changed unit off the LOSSLESS file's own records and cycles_of, so
# the lists-only rows are a model of today's encoder - checked against its
# real budgeted files in the report - and the coder rows differ from them
# by the coder alone.

def tiles2(bits, th, tw):
    h = bits.shape[0]
    h2 = h - h % th
    by = np.packbits(bits[:h2].astype(np.uint8), axis=1)
    wb = by.shape[1] - by.shape[1] % tw
    t = by[:, :wb].reshape(h2 // th, th, wb // tw, tw) \
        .transpose(0, 2, 1, 3).reshape(h2 // th, wb // tw, th * tw)
    pad = np.zeros(t.shape[:2] + (8,), np.uint8)
    pad[..., :th * tw] = t
    return pad.view(np.uint64)[..., 0]


def untiles2(codes, th, tw, h2, wb):
    b = codes[..., None].view(np.uint8)[..., :th * tw] \
        .reshape(codes.shape[0], codes.shape[1], th, tw)
    return np.unpackbits(b.transpose(0, 2, 1, 3).reshape(h2, wb), axis=1)


def budget_bits(path, disk, avg, peak, depth=None, th=4, tw=1, kmax=2,
                tile_cyc=TILE_CYC, use_cb=True, keysecs=2.0, ncb=256):
    """one bit: today's entries, or a codebook index, under the budget"""
    r, F = v88_rgb(path)
    g, fps = r.g, r.fps
    F = [(rgb[..., 0] > 127).astype(np.uint8) for rgb, _ in F]
    byt = cyc = n = 0
    for rec, *_ in r.records():
        byt += len(rec) - r.abytes
        cyc += V.cycles_of(rec, layout=g.layout)
        n += 1
    ch, prev = 0, np.zeros_like(np.packbits(F[0], axis=1))
    for b in F:
        p = np.packbits(b, axis=1)
        ch += int((p != prev).sum())
        prev = p
    bpc, cpc = byt / ch, (cyc - 1214 * n) / ch
    T = np.stack([tiles2(b, th, tw) for b in F])
    nf, ty, tx = T.shape
    h2, wbu = ty * th, tx * tw
    per = int(round(keysecs * fps))
    near, nd = np.zeros_like(T), np.full(T.shape, 99, np.int32)
    if use_cb:              # a codebook a key interval: its most frequent
        for s in range(0, nf, per):         # changed tiles
            e = min(nf, s + per)
            chg = np.concatenate([T[s].ravel()] + [T[i][T[i] != T[i - 1]]
                                                   for i in range(s + 1, e)])
            u, c = np.unique(chg, return_counts=True)
            cb = u[np.argsort(-c)][:ncb]
            seg = T[s:e]
            allu, inv = np.unique(seg, return_inverse=True)
            inv = inv.reshape(seg.shape)
            best = np.zeros(len(allu), np.int64)
            bd = np.full(len(allu), 99, np.int32)
            for i in range(len(cb)):
                d = popc(allu ^ cb[i])
                m = d < bd
                bd[m], best[m] = d[m], i
            near[s:e], nd[s:e] = cb[best[inv]], bd[inv]
    period = HZ / fps
    bb, cb_ = depth or disk, avg * HZ
    bbuck, cbuck = bb / 2, cb_ / 2
    D = np.zeros((ty, tx), np.uint64)
    age = np.zeros((ty, tx), np.int32)
    spent = cyc = seen = 0.0
    nidx = 0
    for i in range(nf):
        bbuck = min(bb, bbuck + disk / fps)
        cbuck = min(cb_, cbuck + avg * period)
        if use_cb and i % per == 0:
            bbuck -= ncb * th * tw
            spent += ncb * th * tw
        tgt = T[i]
        x = D ^ tgt
        wrong = popc(x)
        ys, xs = np.nonzero(wrong > 0)
        nb = (x[..., None].view(np.uint8)[..., :th * tw] != 0).sum(-1)[ys, xs]
        wt = 1 + age[ys, xs] / 8.0
        w = wrong[ys, xs] * wt
        raw_b, raw_c = nb * bpc, nb * cpc
        bud_b = max(1.0, bbuck)
        bud_c = max(1.0, min(max(0.0, cbuck), peak * period) - 1214)

        def share(cb_, cc_):    # the fraction of the scarcer budget spent
            return np.maximum(cb_ / bud_b, cc_ / bud_c)
        gain, cost_b, cost_c = w, raw_b, raw_c
        useidx = np.zeros(len(ys), bool)
        if use_cb:
            ndd = nd[i][ys, xs]
            ig = (wrong[ys, xs] - ndd) * wt
            useidx = (ndd <= kmax) & (ig / share(2.1, tile_cyc) >
                                      w / share(raw_b, raw_c))
            gain = np.where(useidx, ig, w)
            cost_b = np.where(useidx, 2.1, raw_b)
            cost_c = np.where(useidx, tile_cyc, raw_c)
        order = np.argsort(-(gain / share(cost_b, cost_c)))
        take = order[(np.cumsum(cost_b[order]) <= bud_b) &
                     (np.cumsum(cost_c[order]) <= bud_c)]
        ti, tr = take[useidx[take]], take[~useidx[take]]
        D[ys[ti], xs[ti]] = near[i][ys[ti], xs[ti]]
        D[ys[tr], xs[tr]] = tgt[ys[tr], xs[tr]]
        bu, cu = cost_b[take].sum(), cost_c[take].sum() + 1214
        bbuck -= bu
        cbuck -= cu
        spent += bu
        cyc += cu
        nidx += len(ti)
        age = np.where(popc(D ^ tgt) > 0, age + 1, 0)
        a = F[i][:h2, :wbu * 8].astype(float) * 255
        b = untiles2(D, th, tw, h2, wbu).astype(float) * 255
        seen += ((blur(a) - blur(b)) ** 2).mean()
    return (spent / nf * fps / 1024, 100 * cyc / nf / period, nidx / nf,
            psnr(seen / nf))


def cmd_budget1(a):
    print("%s: %d B/s for the picture, a %d-byte bucket, %.0f%% / %.0f%%"
          % (os.path.basename(a.file), a.disk, a.depth or a.disk,
             100 * a.avg, 100 * a.peak))
    shapes = [("today's lists", dict(use_cb=False)),
              ("8 x 4 codebook", dict(th=4, tw=1, tile_cyc=TILE_CYC)),
              ("16 x 4 codebook", dict(th=4, tw=2, tile_cyc=260.0))]
    for name, kw in shapes:
        kb, cpu, idx, seen = budget_bits(a.file, a.disk, a.avg, a.peak,
                                         a.depth, kmax=a.kmax, **kw)
        print("  %-16s %6.1f KB/s  8088 %5.1f%%  tiles a frame %4.0f  "
              "seen %5.2f dB" % (name, kb, cpu, idx, seen))
        sys.stdout.flush()


BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9],
                   [15, 7, 13, 5]])
BLK_CYC = {         # 8088 cycles a block, ESTIMATES (the report's loops)
    "vga4": dict(pfill=400, two=400, cb=450, fixed=1500),
    "lin320": dict(fill=200, two=500, eight=700, cb=420, fixed=1214),
    "modex": dict(fill=120, two=380, eight=620, cb=520, fixed=1500)}


def changed_units(a, b, kind):
    """VGA4: per row-byte, the planes that differ; else a pixel each"""
    if kind == "vga4":
        x = (a ^ b).astype(np.uint8)
        h, w = x.shape
        planes = np.bitwise_or.reduce(x[:, :w - w % 8].reshape(h, -1, 8),
                                      axis=2)
        return np.unpackbits(planes[..., None], axis=2)[..., 4:].sum(-1)
    return (a != b).astype(np.int32)


def blocks(a, bw, bh=4):
    h, w = a.shape[:2]
    hb, wb = h // bh, w // bw
    a = a[:hb * bh, :wb * bw]
    return a.reshape((hb, bh, wb, bw) + a.shape[2:]).swapaxes(1, 2) \
        .reshape((hb * wb, bh * bw) + a.shape[2:]), hb, wb


def unblocks(b, hb, wb, bw, bh=4):
    return b.reshape((hb, wb, bh, bw) + b.shape[2:]).swapaxes(1, 2) \
        .reshape((hb * bh, wb * bw) + b.shape[2:])


def cells(rgb, bw, bh=4):
    """a block's 2 x 2-cell means: what its error is measured on"""
    x = rgb.reshape(-1, bh // 2, 2, bw // 2, 2, 3)
    return x.mean((2, 4)).reshape(len(rgb), -1, 3)


class Mixes:
    """VGA4's DITHERED fill: the (a, b, m) whose 4 x 4 Bayer mix is
    nearest an RGB, through a 32 x 32 x 32 table"""

    def __init__(self, pal):
        A, B = np.triu_indices(len(pal))
        m = np.arange(17)
        mix = (pal[A][:, None] * m[None, :, None] +
               pal[B][:, None] * (16 - m)[None, :, None]) / 16
        self.cand = np.stack([np.repeat(A, 17), np.repeat(B, 17),
                              np.tile(m, len(A))], 1)
        mix = mix.reshape(-1, 3)
        q = (np.arange(32) * 8 + 4).astype(np.float32)
        grid = np.stack(np.meshgrid(q, q, q, indexing="ij"), -1) \
            .reshape(-1, 3)
        self.t = np.empty(len(grid), np.int64)
        for s in range(0, len(grid), 4096):
            self.t[s:s + 4096] = ((grid[s:s + 4096, None] - mix[None]) ** 2) \
                .sum(-1).argmin(-1)

    def __call__(self, rgb):
        i = np.clip(rgb, 0, 255).astype(np.int32) >> 3
        return self.cand[self.t[(i[:, 0] << 10) | (i[:, 1] << 5) | i[:, 2]]]


def aligned_source(a, F):
    """the source at the .V88's frames: aligned, and tone-matched onto it"""
    r = a.r
    g = r.g
    S = src_frames(a.src, g.w, g.h, r.fps, len(F) + 8, a.start, a.secs)
    samp = list(range(5, len(F) - 5, max(1, len(F) // 12)))

    def miss(off):
        return np.mean([((blur(F[i]) - blur(S[i + off])) ** 2).mean()
                        for i in samp if 0 <= i + off < len(S)])
    off = min(range(-3, 4), key=miss)
    lo = -off if off < 0 else 0
    S = S if off < 0 else S[off:]
    n = min(len(S), len(F) - lo)
    S = S[:n]
    idx = [i for i in samp if i < n]
    A = np.stack([blur(S[i]) for i in idx])
    B = np.stack([blur(F[lo + i]) for i in idx])
    curves = []
    for c in range(3):
        x, y = A[..., c].ravel(), B[..., c].ravel()
        bins = np.clip(x.astype(int) >> 3, 0, 31)
        cnt = np.bincount(bins, None, 32)
        cur = np.bincount(bins, y, 32) / np.maximum(cnt, 1)
        ok = cnt > 0
        cur = np.interp(np.arange(32), np.arange(32)[ok], cur[ok])
        curves.append(np.interp(np.arange(256), np.arange(32) * 8 + 4, cur))
    S = np.stack([np.take(curves[c], np.clip(S[..., c], 0, 255)
                          .astype(int)) for c in range(3)], -1) \
        .astype(np.float32)
    return S, lo, n


def budget_colour(a, modes, disk):
    """colour: today's change, or a block mode, under the budget"""
    r = a.r
    g, fps = r.g, r.fps
    kind = "vga4" if r.pixfmt == V.PF_VGA4 else \
        ("modex" if g.name == "modex" else "lin320")
    bw = 8 if kind == "vga4" else 4
    cyc_t = BLK_CYC[kind]
    idx, pals = a.idx[a.lo:a.lo + a.n], a.pals[a.lo:a.lo + a.n]
    S = a.S
    nf = len(idx)
    byt = cyc = n = 0
    for rec, *_ in r.records():
        byt += len(rec) - r.abytes
        n += 1
        try:
            cyc += V.cycles_of(rec, planar=g.planes > 1, layout=g.layout,
                               sub2=g.sub2)
        except V.V88Error:
            pass
    units, prev = 0, np.zeros_like(a.idx[0])
    for x in a.idx:
        units += changed_units(prev, x, kind).sum()
        prev = x
    bpc, cpc = byt / units, (cyc - cyc_t["fixed"] * n) / units
    Sb = [blocks(s, bw)[0] for s in S]
    Lb = [blocks(x, bw)[0] for x in idx]
    _, hb, wb = blocks(idx[0], bw)
    nbk = hb * wb
    per = int(round(2.0 * fps))
    by, bx = np.divmod(np.arange(nbk), wb)
    pat = BAYER4[(by[:, None] * 4 + np.repeat(np.arange(4), bw)[None]) % 4,
                 (bx[:, None] * bw + np.tile(np.arange(bw), 4)[None]) % 4]
    D = np.zeros((nbk, 4 * bw), np.int64)
    age = np.zeros(nbk)
    period = HZ / fps
    bb, cbk = disk, a.avg * HZ
    bbuck, cbuck = bb / 2, cbk / 2
    spent = cyc = 0.0
    outs, used, mixes, nears = [], {}, {}, {}
    cb = None
    for i in range(nf):
        pal = pals[i]
        key = pal.tobytes()
        bbuck = min(bb, bbuck + disk / fps)
        cbuck = min(cbk, cbuck + a.avg * period)
        Sc = cells(Sb[i], bw)

        def err(ix):
            return ((cells(pal[ix], bw) - Sc) ** 2).sum((1, 2))
        e_now = err(D)
        L = Lb[i]
        cu = changed_units(unblocks(D, hb, wb, bw), unblocks(L, hb, wb, bw),
                           kind)
        u = blocks(cu, 1 if kind == "vga4" else bw)[0].sum(1)
        opts = [("lists", L, u * bpc, u * cpc)]
        if key not in nears:
            nears[key] = Nearest(pal)
        near = nears[key]

        def toidx(rgb):
            q = np.clip(rgb, 0, 255).astype(np.int32) >> 3
            return near.t[(q[..., 0] << 10) | (q[..., 1] << 5) | q[..., 2]]
        if "cb" in modes:
            if i % per == 0:        # the interval's most frequent blocks
                e = min(nf, i + per)
                chg = np.concatenate([Lb[j][(Lb[j] != (Lb[j - 1] if j else -1))
                                             .any(1)] for j in range(i, e)])
                v, c = np.unique(chg, axis=0, return_counts=True)
                cb = v[np.argsort(-c)][:256]
                cost = len(cb) * (bw * 4 // (2 if kind == "vga4" else 1))
                bbuck -= cost
                spent += cost
            cand = np.nonzero((L != D).any(1))[0]
            ci = np.zeros(nbk, np.int64)
            if len(cand):
                cc = cells(pal[cb], bw)
                ci[cand] = ((Sc[cand][:, None] - cc[None]) ** 2) \
                    .sum((2, 3)).argmin(1)
            opts.append(("cb", cb[ci], np.full(nbk, 2.1),
                         np.full(nbk, cyc_t["cb"])))
        if "pfill" in modes:
            if key not in mixes:
                mixes[key] = Mixes(pal)
            abm = mixes[key](Sb[i].mean(1))
            opts.append(("pfill", np.where(pat < abm[:, 2:3], abm[:, 0:1],
                                           abm[:, 1:2]),
                         np.full(nbk, 2.5), np.full(nbk, cyc_t["pfill"])))
        if "fill" in modes:
            opts.append(("fill", np.repeat(toidx(Sb[i].mean(1))[:, None],
                                           4 * bw, 1),
                         np.full(nbk, 2.5), np.full(nbk, cyc_t["fill"])))
        if "two" in modes:
            opts.append(("two", toidx(split2(Sb[i], near)),
                         np.full(nbk, 4.5 if bw == 4 else 5.75),
                         np.full(nbk, cyc_t["two"])))
        if "eight" in modes:
            q = Sb[i].reshape(-1, 2, 2, 2, 2, 3).transpose(0, 1, 3, 2, 4, 5) \
                .reshape(-1, 4, 3)
            det = split2(q, near).reshape(-1, 2, 2, 2, 2, 3) \
                .transpose(0, 1, 3, 2, 4, 5).reshape(-1, 16, 3)
            opts.append(("eight", toidx(det), np.full(nbk, 10.5),
                         np.full(nbk, cyc_t["eight"])))
        bud_b = max(1.0, bbuck)
        bud_c = max(1.0, min(max(0.0, cbuck), a.peak * period) -
                    cyc_t["fixed"])

        def share(cb_, cc_):    # the fraction of the scarcer budget spent
            return np.maximum(np.maximum(cb_, 1e-3) / bud_b, cc_ / bud_c)
        bg, bo = np.full(nbk, -1.0), np.full(nbk, -1)
        bbs, bcs, bsh = np.zeros(nbk), np.zeros(nbk), np.ones(nbk)
        for k, (name, blk, b_, c_) in enumerate(opts):
            gain = (e_now - err(blk)) * (1 + age / 8)
            sh = share(b_, c_)
            better = (gain > 0) & (gain / sh > np.where(bo >= 0, bg / bsh, 0))
            bg[better], bo[better], bsh[better] = gain[better], k, sh[better]
            bbs[better], bcs[better] = b_[better], c_[better]
        cand = np.nonzero(bo >= 0)[0]
        order = cand[np.argsort(-(bg[cand] / bsh[cand]))]
        take = order[(np.cumsum(bbs[order]) <= bud_b) &
                     (np.cumsum(bcs[order]) <= bud_c)]
        for k, (name, blk, b_, c_) in enumerate(opts):
            t = take[bo[take] == k]
            D[t] = blk[t]
            used[name] = used.get(name, 0) + len(t)
        bu, cu_ = bbs[take].sum(), bcs[take].sum() + cyc_t["fixed"]
        bbuck -= bu
        cbuck -= cu_
        spent += bu
        cyc += cu_
        fixed = np.zeros(nbk, bool)
        fixed[take] = True
        age = np.where(fixed, 0, np.where(err(D) > 0, age + 1, 0))
        outs.append(pal[unblocks(D, hb, wb, bw)])
    tot = max(1, sum(used.values()))
    return "%6.1f KB/s  8088 %4.0f%%  %s  | %s" % (
        spent / nf * fps / 1024, 100 * cyc / nf / period,
        scores(S[:, :hb * 4, :wb * bw], outs),
        " ".join("%s %.0f%%" % (k, 100 * v / tot) for k, v in used.items()))


def cmd_budgetc(a):
    a.r, F = v88_rgb(a.lossless)
    rr = V.Reader(a.lossless)
    g = rr.g
    a.idx, a.pals = [], []
    for f, surf, *_ in V.v88_frames(rr, check=False):
        cv = np.frombuffer(g.canvas(surf), np.uint8)[:g.w * g.h] \
            .reshape(g.h, g.w)
        if rr.pixfmt == V.PF_VGA4:
            cv = cv & 15
        a.idx.append(cv.copy())
        a.pals.append(F[f][1])
    a.S, a.lo, a.n = aligned_source(a, [x for x, _ in F])
    print("%s: budget %.0f%% / %.0f%% of an 8088, against %s, tone-matched"
          % (os.path.basename(a.lossless), 100 * a.avg, 100 * a.peak,
             os.path.basename(a.src)))
    for disk in a.disk:
        for m in a.modes:
            modes = [x for x in m.split("+") if x and x != "lists"]
            print("  disk %7d  %-24s %s" % (disk, m, budget_colour(a, modes,
                                                                   disk)))
            sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("bytes")
    p.add_argument("files", nargs="+")
    p.add_argument("--budget", type=int, default=1_500_000,
                   help="stream bytes sampled for the LZ columns")
    p.add_argument("--records", type=int, default=2500)
    p.set_defaults(fn=cmd_bytes)
    p = sub.add_parser("overhead")
    p.add_argument("files", nargs="+")
    p.set_defaults(fn=cmd_overhead)
    p = sub.add_parser("tiles")
    p.add_argument("file")
    p.add_argument("--rows", type=int, default=4)
    p.add_argument("--codebook", type=int, default=256)
    p.add_argument("--k", type=int, nargs="+", default=[0, 1, 2, 3],
                   help="bits a tile may be wrong and stay")
    p.set_defaults(fn=cmd_tiles)
    p = sub.add_parser("video1")
    p.add_argument("src")
    p.add_argument("lossless")
    p.add_argument("others", nargs="*")
    p.add_argument("--start", type=float, default=30)
    p.add_argument("--secs", type=float, default=15)
    p.add_argument("--tone", type=int, default=1)
    p.add_argument("--t", type=float, nargs="+",
                   default=[50, 100, 200, 400, 800])
    p.set_defaults(fn=cmd_video1)
    p = sub.add_parser("budget1")
    p.add_argument("file", help="a LOSSLESS one-bit .V88")
    p.add_argument("--disk", type=int, default=106500,
                   help="bytes a second for the picture (5150-st225's 116 "
                   "KB/s less PCM8 11 kHz and padding)")
    p.add_argument("--depth", type=int, default=294912,
                   help="the disk bucket: 96 KB and the 192 KB reserve")
    p.add_argument("--avg", type=float, default=0.85,
                   help="the average share; 5150-st225's --avg AUTO is its "
                   "per-frame ceiling")
    p.add_argument("--peak", type=float, default=0.85)
    p.add_argument("--kmax", type=int, default=2)
    p.set_defaults(fn=cmd_budget1)
    p = sub.add_parser("budgetc")
    p.add_argument("src")
    p.add_argument("lossless", help="a LOSSLESS VGA4 or VGA8 .V88")
    p.add_argument("--disk", type=int, nargs="+", default=[100000, 50000])
    p.add_argument("--avg", type=float, default=2.25)
    p.add_argument("--peak", type=float, default=3.75)
    p.add_argument("--modes", nargs="+", default=["lists", "pfill+two",
                                                  "cb", "fill+two+eight"])
    p.add_argument("--start", type=float, default=30)
    p.add_argument("--secs", type=float, default=15)
    p.set_defaults(fn=cmd_budgetc)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
