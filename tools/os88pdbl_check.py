#!/usr/bin/env python3
"""os88pdbl_check - PDXI's bilevel decoders against their reference encoders (20.22.12).

    python3 tools/os88pdbl_check.py [--bin build-bl/pdbl] [--seed N] ccitt

Builds random bilevel images, codes them with tools/os88pdfx_bilevel.py in every CCITT
shape, runs the host decoder and compares to the source (and, reduced, to the model)."""
import sys, os, random, subprocess, tempfile
sys.path.insert(0, os.path.dirname(__file__))
import os88pdfx_bilevel as F

BIN = 'build-bl/pdbl'

def rimg(w, h, p, rnd):
    img = []
    for y in range(h):
        if img and rnd.random() < 0.5:        # correlate with the row above, as scans do
            row = list(img[-1])
            for _ in range(rnd.randint(0, 6)):
                i = rnd.randrange(w); row[i] ^= 1
        else:
            row = []; c = 0
            while len(row) < w:
                r = rnd.choice([1, 2, 3, 5, 9, 17, 70, 200, 1000, 2700]) if rnd.random() < p else rnd.randint(1, 12)
                row += [c] * r; c ^= 1
            row = row[:w]
        img.append(row)
    return img

def model(img, w, h, s, black1):
    """expected delivery (list of rows of ints) for scale s"""
    if s == 0:
        return [[(v if black1 else 1 - v) for v in r] for r in img]
    n = 1 << s; ow = (w + n - 1) >> s; oh = (h + n - 1) >> s
    out = []
    for g in range(oh):
        row = []
        for j in range(ow):
            cnt = 0
            for y in range(g * n, g * n + n):
                for x in range(j * n, j * n + n):
                    if y < h and x < w: cnt += (img[y][x] if black1 else 1 - img[y][x])
                    else: cnt += 1
            row.append(((cnt << 8) - cnt) >> (2 * s))
        out.append(row)
    return out

def run(args):
    r = subprocess.run([BIN] + [str(a) for a in args], capture_output=True, text=True)
    return r.returncode, r.stdout.strip()

def check_ccitt(rnd, nproc=[0]):
    fails = 0; total = 0
    cases = [(-1, False, False), (-1, False, True), (0, False, False), (0, True, False), (0, True, True), (0, False, True),
             (3, True, False), (3, True, True), (1, True, False)]
    shapes = [(1728, 24), (100, 17), (2600, 9), (7, 5), (8, 4), (64, 33), (1, 6), (2480, 16)]
    with tempfile.TemporaryDirectory() as td:
        for (w, h) in shapes:
            for (k, eol, align) in cases:
                img = rimg(w, h, 0.25, rnd)
                data = F.enc_ccitt(img, k, eol=eol, align=align, eob=rnd.random() < 0.7)
                fl = 4 | (1 if eol else 0) | (2 if align else 0)
                for black1 in (False, True):
                    for want in (0, w // 2 if w >= 16 else 0, w // 4 if w >= 32 else 0, w // 8 if w >= 64 else 0):
                        open(td + '/c', 'wb').write(data)
                        rc, out = run(['ccitt', td + '/c', k & 0xFFFF, w, h, fl | (8 if black1 else 0), h, want, td + '/o'])
                        total += 1
                        if rc: print('FAIL run', w, h, k, eol, align, want, out); fails += 1; continue
                        mode, ow, oh, rb, s = map(int, out.split()[:5])
                        raw = open(td + '/o', 'rb').read()
                        exp = model(img, w, h, s, black1)
                        got = []
                        for r in range(oh):
                            row = raw[r * rb:(r + 1) * rb]
                            if mode == 1: got.append([(row[x >> 3] >> (7 - (x & 7))) & 1 for x in range(ow)])
                            else: got.append(list(row))
                        if got != exp:
                            bad = sum(1 for a, b in zip(got, exp) for x, y in zip(a, b) if x != y)
                            print('FAIL', 'w%d h%d k%d eol%d align%d black1=%d want=%d scale=%d diff=%d' % (w, h, k, eol, align, black1, want, s, bad)); fails += 1
    print('ccitt: %d cases, %d failed' % (total, fails))
    return fails

def mutool_grey(pdf, w, h, td):
    open(td + '/m.pdf', 'wb').write(pdf)
    subprocess.run(['mutool', 'draw', '-q', '-r', '72', '-c', 'gray', '-o', td + '/m.pgm', td + '/m.pdf', '1'], check=True)
    d = open(td + '/m.pgm', 'rb').read()
    # P5 w h 255 \n
    parts = d.split(b'\n', 3)
    return [list(parts[3][y * w:(y + 1) * w]) for y in range(h)], parts[1]

def check_mutool_ccitt(rnd):
    fails = total = 0
    with tempfile.TemporaryDirectory() as td:
        for (w, h) in ((1728, 20), (100, 17), (2480, 12), (33, 9), (800, 40)):
            for (k, eol, align) in ((-1, False, False), (-1, False, True), (0, False, False), (0, True, False), (0, True, True), (3, True, False)):
                img = rimg(w, h, 0.25, rnd)
                for black1 in (False, True):
                    pdf = F.ccitt_pdf(img, k, eol, align, True, black1)
                    got, hdr = mutool_grey(pdf, w, h, td)
                    exp = [[0 if v else 255 for v in r] for r in img]   # black=0 on the page either way (Decode)
                    total += 1
                    bad = sum(1 for a, b in zip(got, exp) for x, y in zip(a, b) if abs(x - y) > 0)
                    if bad: print('MUTOOL differs', w, h, k, eol, align, black1, 'diff', bad); fails += 1
    print('mutool ccitt: %d cases, %d differ' % (total, fails))
    return fails

if __name__ == '__main__':
    a = sys.argv[1:]
    seed = 1
    while a and a[0].startswith('--'):
        if a[0] == '--bin': BIN = a[1]; a = a[2:]
        elif a[0] == '--seed': seed = int(a[1]); a = a[2:]
        else: break
    rnd = random.Random(seed)
    f = 0
    for what in a or ['ccitt']:
        if what == 'ccitt': f += check_ccitt(rnd)
        if what == 'mutool-ccitt': f += check_mutool_ccitt(rnd)
    sys.exit(1 if f else 0)

