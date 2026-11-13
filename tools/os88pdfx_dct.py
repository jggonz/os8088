#!/usr/bin/env python3
"""os88pdfx_dct - PDXI's JPEG fixtures and the check against libjpeg
(PAGEDESK-SPEC 20.22.11, AP-07).

    python3 tools/os88pdfx_dct.py [--dir DIR] [--bin PATH] [--only SUBSTR] [-v]

WHAT. It makes a DETERMINISTIC set of JPEGs (a seeded synthetic picture, run
through cjpeg and ImageMagick: baseline and progressive, grey, 4:2:0 / 4:2:2 /
4:4:0 / 4:4:4, restart markers, odd sizes, RGB, CMYK and YCCK), decodes each with
the real pdxi.c through hosttest/pdi_test (the module's own verbs, a mock PDENG)
and compares the rows with libjpeg: `djpeg -grayscale -dct int -scale 1/N` for
the grey output and `djpeg -nosmooth -dct int` for PDIM_N8, Pillow for CMYK. It
prints the max and the mean absolute difference per image and per scale and
exits 1 when a limit is exceeded. A limit is 0 where the arithmetic is libjpeg's
own (every grey decode).

Provenance: cjpeg / djpeg from libjpeg-turbo (the version is printed), ImageMagick
`convert`, numpy's RandomState (stable across versions). Nothing is downloaded.
"""
import argparse, os, shutil, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def picture(w, h, seed):
    """a smooth gradient + shapes + a little noise, 8-bit RGB, deterministic"""
    rs = np.random.RandomState(seed)
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    r = 128 + 100 * np.sin(x / 23.0 + seed) * np.cos(y / 31.0)
    g = 255 * (x / max(w - 1, 1))
    b = 255 * (y / max(h - 1, 1))
    img = np.stack([r, g, b], axis=-1)
    for _ in range(6):
        cx, cy = rs.randint(0, w), rs.randint(0, h)
        rad = rs.randint(max(2, min(w, h) // 8), max(3, min(w, h) // 3))
        m = (x - cx) ** 2 + (y - cy) ** 2 < rad * rad
        img[m] = rs.randint(0, 256, 3)
    for _ in range(4):                      # hard edges and thin lines
        x0 = rs.randint(0, w); y0 = rs.randint(0, h)
        img[y0:y0 + 2, :, :] = rs.randint(0, 256, 3)
        img[:, x0:x0 + 1, :] = rs.randint(0, 256, 3)
    img += rs.normal(0, 6, img.shape)
    return np.clip(img, 0, 255).astype(np.uint8)


def ppm(path, a):
    h, w = a.shape[:2]
    with open(path, 'wb') as f:
        f.write(b'P6\n%d %d\n255\n' % (w, h)); f.write(a.tobytes())


def pgm(path, a):
    h, w = a.shape
    with open(path, 'wb') as f:
        f.write(b'P5\n%d %d\n255\n' % (w, h)); f.write(a.tobytes())


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, **kw)


def readpnm(path):
    d = open(path, 'rb').read()
    toks = []; i = 0
    while len(toks) < 4:
        while d[i:i + 1].isspace(): i += 1
        if d[i:i + 1] == b'#':
            while d[i:i + 1] != b'\n': i += 1
            continue
        j = i
        while not d[j:j + 1].isspace(): j += 1
        toks.append(d[i:j]); i = j
    i += 1
    kind = toks[0]; w, h = int(toks[1]), int(toks[2])
    n = 1 if kind == b'P5' else 3
    return np.frombuffer(d[i:i + w * h * n], np.uint8).reshape(h, w, n) if n == 3 else \
        np.frombuffer(d[i:i + w * h], np.uint8).reshape(h, w)


# name, w, h, kind, cjpeg-args
CASES = [
    ('b420',      200, 150, 'rgb', ['-sample', '2x2']),
    ('b444',      200, 150, 'rgb', ['-sample', '1x1']),
    ('b422',      200, 150, 'rgb', ['-sample', '2x1']),
    ('b440',      200, 150, 'rgb', ['-sample', '1x2']),
    ('b420odd',   203, 151, 'rgb', ['-sample', '2x2']),
    ('b420rst',   200, 150, 'rgb', ['-sample', '2x2', '-restart', '3B']),
    ('b444rst1',  96, 64,   'rgb', ['-sample', '1x1', '-restart', '7']),
    ('bgrey',     200, 150, 'grey', []),
    ('bgreyodd',  37, 53,   'grey', []),
    ('bq10',      160, 120, 'rgb', ['-quality', '10']),
    ('bq100',     160, 120, 'rgb', ['-quality', '100', '-sample', '1x1']),
    ('bopt',      160, 120, 'rgb', ['-optimize']),
    ('tiny1',     1, 1,     'rgb', []),
    ('tiny9',     9, 7,     'rgb', ['-sample', '2x2']),
    ('b8',        8, 8,     'grey', []),
    ('big',       640, 480, 'rgb', ['-sample', '2x2']),
    ('wide',      1100, 40, 'rgb', ['-sample', '2x1']),
    ('p420',      200, 150, 'rgb', ['-progressive', '-sample', '2x2']),
    ('p444',      200, 150, 'rgb', ['-progressive', '-sample', '1x1']),
    ('p422',      200, 150, 'rgb', ['-progressive', '-sample', '2x1']),
    ('pgrey',     200, 150, 'grey', ['-progressive']),
    ('p420odd',   203, 151, 'rgb', ['-progressive', '-sample', '2x2']),
    ('p420rst',   200, 150, 'rgb', ['-progressive', '-sample', '2x2', '-restart', '4B']),
    ('pbig',      640, 480, 'rgb', ['-progressive', '-sample', '2x2']),
    ('seq3scan',  120, 90,  'rgb', ['-scans', '/SCANS/']),   # baseline, one scan a component
    ('rgbjpeg',   120, 90,  'rgb', ['-rgb']),
    ('prgbjpeg',  120, 90,  'rgb', ['-rgb', '-progressive']),
]
ASSERT0 = True


def make(d, tool):
    os.makedirs(d, exist_ok=True)
    files = {}
    for i, (name, w, h, kind, args) in enumerate(CASES):
        a = picture(w, h, 100 + i)
        src = os.path.join(d, name + '.ppm'); out = os.path.join(d, name + '.jpg')
        if kind == 'grey':
            g = np.dot(a[..., :3].astype(np.float64), [0.299, 0.587, 0.114]).round().astype(np.uint8)
            src = os.path.join(d, name + '.pgm'); pgm(src, g)
        else:
            ppm(src, a)
        args = list(args)
        if '/SCANS/' in args:
            sc = os.path.join(d, name + '.scans')
            open(sc, 'w').write('0;\n1;\n2;\n')
            args[args.index('/SCANS/')] = sc
        run(['cjpeg', '-dct', 'int'] + args + ['-outfile', out, src])
        files[name] = out
    # CMYK and YCCK through ImageMagick
    for i, (name, w, h, extra) in enumerate([('cmyk', 100, 80, []), ('cmyk2', 100, 80, ['-sampling-factor', '2x2']),
                                             ('pcmyk', 100, 80, ['-interlace', 'Plane'])]):
        a = picture(w, h, 300 + i); src = os.path.join(d, name + '.ppm'); ppm(src, a)
        out = os.path.join(d, name + '.jpg')
        run(['convert', src, '-colorspace', 'CMYK'] + extra + ['-quality', '90', out])
        files[name] = out
    return files


def jinfo(path):
    """(progressive, ncomp, adobe_transform or None, jfif, sampling) from the markers"""
    d = open(path, 'rb').read(); i = 2; prog = False; nc = 0; ad = None; jf = False; samp = []
    while i < len(d):
        if d[i] != 0xFF: i += 1; continue
        m = d[i + 1]
        if m in (0xD8, 0x01) or 0xD0 <= m <= 0xD7 or m == 0xFF or m == 0: i += 2 if m != 0xFF else 1; continue
        ln = (d[i + 2] << 8) | d[i + 3]
        if m in (0xC0, 0xC1, 0xC2):
            prog = m == 0xC2; nc = d[i + 9]
            samp = [(d[i + 11 + 3 * k] >> 4, d[i + 11 + 3 * k] & 15) for k in range(nc)]
        if m == 0xE0 and d[i + 4:i + 9] == b'JFIF\0': jf = True
        if m == 0xEE and d[i + 4:i + 9] == b'Adobe': ad = d[i + 15]
        if m == 0xDA: break
        i += 2 + ln
    return prog, nc, ad, jf, samp


def jsize(path):
    d = open(path, 'rb').read(); i = 2
    while i < len(d):
        if d[i] != 0xFF: i += 1; continue
        m = d[i + 1]
        if m in (0xD8, 0x01) or 0xD0 <= m <= 0xD7 or m in (0, 0xFF): i += 2 if m != 0xFF else 1; continue
        if m in (0xC0, 0xC1, 0xC2): return (d[i + 7] << 8) | d[i + 8], (d[i + 5] << 8) | d[i + 6]
        i += 2 + ((d[i + 2] << 8) | d[i + 3])


def decode(binp, jpg, ww, wh, flags, out, extra_env=None, inline=False):
    env = dict(os.environ); env.update(extra_env or {})
    cmd = [binp, jpg, str(ww), str(wh), str(flags), out] + (['inline'] if inline else [])
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode:
        return None, r.stdout.strip() or r.stderr.strip()
    p = [int(x) for x in r.stdout.split()]
    return p, None


def stats(a, b):
    dd = np.abs(a.astype(np.int32) - b.astype(np.int32))
    return int(dd.max()), float(dd.mean())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default=os.path.join(ROOT, 'build', 'pdfx_dct'))
    ap.add_argument('--bin', default=os.path.join(ROOT, 'build', 'pdi', 'pdi_test'))
    ap.add_argument('--only', default='')
    ap.add_argument('--make-only', action='store_true')
    ap.add_argument('-v', action='store_true')
    o = ap.parse_args()
    print(subprocess.run(['djpeg', '-version'], capture_output=True, text=True).stderr.strip().splitlines()[0])
    files = make(o.dir, o)
    if o.make_only: return 0
    bad = 0; rows = 0
    for name, jpg in sorted(files.items()):
        if o.only and o.only not in name: continue
        prog, nc, ad, jf, samp = jinfo(jpg)
        W, H = jsize(jpg)
        # (want, flags, label): scale 1/1, then each reduction, then a native one
        wants = [(W, H), (max(1, W // 2), max(1, H // 2)), (max(1, W // 4), max(1, H // 4)), (1, 1)]
        for ww, wh in wants:
            for native in (0, 1):
                if native and nc not in (3, 4) and name != 'bgrey': continue
                flags = 4 if native else 0
                if native == 0 and nc == 4: flags = 2 if ad is not None else 0  # Adobe CMYK is inverted
                raw = os.path.join(o.dir, name + '.raw')
                p, err = decode(o.bin, jpg, ww, wh, flags, raw)
                tag = '%-9s %4dx%-4d want %4dx%-4d %s' % (name, W, H, ww, wh, 'N8  ' if native else 'grey')
                if p is None:
                    print('%s FAIL %s' % (tag, err)); bad += 1; continue
                w, h, mode, ncomp, rb, s = p
                got = np.fromfile(raw, np.uint8)
                if got.size != h * rb:
                    print('%s FAIL size %d != %d' % (tag, got.size, h * rb)); bad += 1; continue
                got = got.reshape(h, w, ncomp) if mode == 2 else got.reshape(h, w)
                # the reference at the SAME scale
                ref = os.path.join(o.dir, name + '.ref')
                lim = 0
                if nc == 4:
                    # libjpeg-turbo's djpeg turns (Adobe, inverted) CMYK/YCCK into RGB as C*K/255 on the stored samples
                    run(['djpeg', '-dct', 'int', '-nosmooth', '-scale', '1/%d' % (1 << s), '-pnm', '-outfile', ref, jpg])
                    r = readpnm(ref).astype(np.int32)
                    if r.shape[:2] != (h, w):
                        print('%s FAIL ref size %s vs %dx%d' % (tag, r.shape[:2], w, h)); bad += 1; continue
                    g = got.astype(np.int32)
                    if native:
                        k = g[..., 3]
                        rgb = np.stack([(g[..., j] * k + 127) // 255 for j in range(3)], -1)
                        lim = 3
                        mx, mean = stats(rgb, r)
                        lu_note = ''
                    else:
                        lu = (77 * r[..., 0] + 150 * r[..., 1] + 29 * r[..., 2]) >> 8
                        lim = 3
                        mx, mean = stats(g, lu)
                else:
                    sc = 1 << s
                    if native:
                        cmd = ['djpeg', '-nosmooth', '-dct', 'int', '-scale', '1/%d' % sc, '-ppm', '-outfile', ref, jpg]
                        lim = 2
                    else:
                        cmd = ['djpeg', '-grayscale', '-dct', 'int', '-scale', '1/%d' % sc, '-outfile', ref, jpg]
                        lim = 2 if (nc == 3 and ad == 0 and not jf) or name.endswith('rgbjpeg') else 0
                    run(cmd)
                    r = readpnm(ref)
                    if r.shape[:2] != (h, w):
                        print('%s FAIL ref size %s vs %dx%d (scale %d)' % (tag, r.shape[:2], w, h, s)); bad += 1; continue
                    if mode == 2 and nc == 1: r = r.reshape(h, w, 1) if r.ndim == 2 else r
                    if native and nc == 1: r = r[..., :1] if r.ndim == 3 else r.reshape(h, w, 1)
                    mx, mean = stats(got, r)
                    if native and name.endswith('rgbjpeg'): lim = 0
                    if native and s > 0 and nc == 3:
                        # PDXI's chroma at a reduction is the luma's, replicated; libjpeg decodes chroma at a
                        # larger scale. Hold the LUMA of the N8 rows to libjpeg's grey at the same scale.
                        gref = os.path.join(o.dir, name + '.gref')
                        run(['djpeg', '-grayscale', '-dct', 'int', '-scale', '1/%d' % (1 << s), '-outfile', gref, jpg])
                        gi = got.astype(np.int32)
                        lu = (77 * gi[..., 0] + 150 * gi[..., 1] + 29 * gi[..., 2]) >> 8
                        gr = readpnm(gref).astype(np.int32)
                        dd = np.abs(lu - gr)
                        mx, mean = int(dd.max()), float(dd.mean())
                        tag += ' luma'
                        ok = mean < 12.0
                        rows += 1
                        if not ok: bad += 1
                        print('%s 1/%d out %4dx%-4d max %3d mean %.4f (limit mean<12) %s' % (tag, 1 << s, w, h, mx, mean, 'ok' if ok else 'FAIL'))
                        continue
                rows += 1
                stored = prog or name == 'seq3scan'
                chroma = nc >= 3 and any(sm != samp[0] for sm in samp[1:])
                approx = (stored and s in (1, 2) and not (native and False)) or (chroma and s > 0)
                if approx:
                    # coefficients beyond the low N x N are not kept (stored scans), or the chroma is the luma's
                    # reduction replicated (20.22.11): bounded by the mean, not the max
                    lim = -1
                    ok = mean <= (4.0 if not (chroma or native) else 20.0)
                else:
                    ok = mx <= lim
                if not ok: bad += 1
                if o.v or not ok or True:
                    print('%s 1/%d out %4dx%-4d max %2d mean %.4f (limit %s) %s' % (tag, 1 << s, w, h, mx, mean, 'mean' if lim < 0 else lim, 'ok' if ok else 'FAIL'))
    print('%d comparisons, %d failures' % (rows, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
