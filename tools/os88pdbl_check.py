#!/usr/bin/env python3
"""os88pdbl_check - PDXI's bilevel decoders against their reference encoders (20.22.12).

    python3 tools/os88pdbl_check.py [--bin build-bl/pdbl] [--seed N] ccitt

Builds random bilevel images, codes them with tools/os88pdfx_bilevel.py in every CCITT
shape, runs the host decoder and compares to the source (and, reduced, to the model)."""
import sys, os, random, subprocess, tempfile
sys.path.insert(0, os.path.dirname(__file__))
import os88pdfx_bilevel as F

BIN = 'build-bl/pdbl'
BINS = ['build-bl/pdbl', 'build-bl/pdbl_b']

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


def bits_of(raw, rb, ow, oh, mode):
    got = []
    for r in range(oh):
        row = raw[r * rb:(r + 1) * rb]
        got.append([(row[x >> 3] >> (7 - (x & 7))) & 1 for x in range(ow)] if mode == 1 else list(row))
    return got

def run_jb2(binp, data, globals_, w, h, want, td):
    open(td + '/j', 'wb').write(data)
    g = '-'
    if globals_ is not None:
        open(td + '/g', 'wb').write(globals_); g = td + '/g'
    rc, out = run([binp, 'jbig2', td + '/j', g, h, want, td + '/jo']) if False else (None, None)
    r = subprocess.run([binp, 'jbig2', td + '/j', g, str(h), str(want), td + '/jo'], capture_output=True, text=True)
    if r.returncode: return None, r.stdout + r.stderr
    mode, ow, oh, rb, s = map(int, r.stdout.split()[:5])
    return (bits_of(open(td + '/jo', 'rb').read(), rb, ow, oh, mode), s), ''

def jb2_expect(page, w, h, s):
    """page: rows of 0/1 (1 = black).  delivered samples: 0 = black"""
    return model(page, w, h, s, False)

def compose(page, img, x, y, op):
    h = len(page); w = len(page[0])
    for j, row in enumerate(img):
        for i, v in enumerate(row):
            xx, yy = x + i, y + j
            if 0 <= xx < w and 0 <= yy < h:
                d = page[yy][xx]
                page[yy][xx] = (d | v, d & v, d ^ v, 1 - (d ^ v), v)[op]

def check_jb2_generic(rnd, bins):
    fails = total = 0
    with tempfile.TemporaryDirectory() as td:
        for case in range(60):
            w = rnd.choice([8, 33, 100, 257, 300]); h = rnd.choice([5, 31, 64, 90])
            nreg = rnd.choice([1, 1, 2, 3])
            default = rnd.choice([0, 0, 1])
            page = [[default] * w for _ in range(h)]
            segs = [F.segment(0, 48, F.page_info(w, h, default))]; desc = []
            for n in range(nreg):
                rw = rnd.randint(1, w); rh = rnd.randint(1, h)
                if n == 0 and rnd.random() < .6: rw, rh = w, h
                img = rimg(rw, rh, 0.2, rnd)
                x = rnd.randint(0, w - rw) if rw < w else 0
                y = rnd.randint(0, h - rh) if rh < h else 0
                op = rnd.choice([0, 0, 1, 2, 3, 4])
                tpl = rnd.randint(0, 3); tpgd = rnd.randint(0, 1)
                at = None
                if rnd.random() < .3:
                    at = list(F.NOMINAL_AT[tpl]); at[0] = (rnd.randint(-5, 4), rnd.choice([-1, -2])) if rnd.random() < .5 else at[0]
                    if tpl == 0 and rnd.random() < .5: at[2] = (rnd.randint(-8, 8), -2)
                mmr = rnd.random() < .25
                desc.append((rw, rh, x, y, op, tpl, tpgd, at, mmr, default))
                segs.append(F.segment(1 + n, rnd.choice([38, 39]), F.generic_segment(img, x, y, op, tpl, tpgd, at, mmr)))
                compose(page, img, x, y, op)
            data = b''.join(segs)
            for want in (0, w // 2, w // 4):
                for bn in bins:
                    res, err = run_jb2(bn, data, None, w, h, want, td)
                    total += 1
                    if res is None: print('FAIL run', case, w, h, err); fails += 1; continue
                    got, s = res
                    if got != jb2_expect(page, w, h, s):
                        open('/tmp/fail_%d.jb2' % case, 'wb').write(data); print('FAIL generic case', case, w, h, 'want', want, 'bin', bn, desc); fails += 1
    print('jbig2 generic: %d runs, %d failed' % (total, fails))
    return fails


def glyph(w, h, rnd):
    return [[1 if (x == 0 or y == 0 or x == w - 1 or (x + y) % 4 == 0 or rnd.random() < .15) else 0 for x in range(w)] for y in range(h)]

def blit(dst, img, x, y, op, defin=None):
    compose(dst, img, x, y, op)

def mutool_pgm(segs, globs, w, h, td):
    pdf = F.jbig2_pdf(segs, w, h, globs)
    open(td + '/m.pdf', 'wb').write(pdf)
    r = subprocess.run(['mutool', 'draw', '-q', '-r', '72', '-c', 'gray', '-o', td + '/m.pgm', td + '/m.pdf', '1'], capture_output=True)
    d = open(td + '/m.pgm', 'rb').read().split(b'\n', 3)[3]
    return [[1 if d[y * w + x] < 128 else 0 for x in range(w)] for y in range(h)]

def check_jb2_text(rnd, bins, use_mutool=False, refine_ok=True):
    fails = total = 0
    with tempfile.TemporaryDirectory() as td:
        for case in range(40):
            W = rnd.choice([60, 97, 130]); H = rnd.choice([40, 75, 110])
            nsym = rnd.randint(2, 9)
            hs = sorted(rnd.randint(4, 14) for _ in range(nsym))
            syms = [glyph(rnd.randint(2, 12), hh, rnd) for hh in hs]
            tpl = rnd.randint(0, 3)
            logs = rnd.randint(0, 3); corner = rnd.randint(0, 3); transp = rnd.randint(0, 1)
            comb = rnd.choice([0, 0, 1, 2, 3]); defpix = rnd.choice([0, 0, 1]); dsoff = rnd.randint(-3, 3)
            rop = rnd.choice([0, 0, 1, 2, 4]); refine = refine_ok and rnd.random() < .4; rtpl = rnd.randint(0, 1)
            insts = []
            for _ in range(rnd.randint(1, 24)):
                sid = rnd.randrange(nsym)
                X = rnd.randint(-3, W - 4); Y = rnd.randint(-3, H - 4)
                if refine and rnd.random() < .5:
                    base = syms[sid]; bh = len(base) + rnd.randint(-1, 2); bw = len(base[0]) + rnd.randint(-1, 2)
                    bm = [[(base[y][x] if y < len(base) and x < len(base[0]) else 0) ^ (1 if rnd.random() < .1 else 0) for x in range(bw)] for y in range(bh)]
                    insts.append((sid, X, Y, bm))
                else: insts.append((sid, X, Y))
            # model: region with default pixel, symbols combined by comb, region combined to page by rop
            region = [[defpix] * W for _ in range(H)]
            order = sorted(range(len(insts)), key=lambda i: i)  # draw order = decoder order (strip, s)
            # decoder order is by strip then S: reproduce the encoder's sort for non-commutative ops
            strips = 1 << logs
            def key(it):
                bm = it[3] if len(it) > 3 else syms[it[0]]
                bh = len(bm); bw = len(bm[0])
                if not transp:
                    S = it[1] + (bw - 1 if corner in (2, 3) else 0); T = it[2] + (bh - 1 if corner in (0, 2) else 0)
                else:
                    T = it[1] + (bw - 1 if corner in (2, 3) else 0); S = it[2] + (bh - 1 if corner in (0, 2) else 0)
                return (T // strips, S)
            for it in sorted(insts, key=key):
                bm = it[3] if len(it) > 3 else syms[it[0]]
                compose(region, bm, it[1], it[2], comb)
            page = [[0] * W for _ in range(H)]
            compose(page, region, 0, 0, rop)
            dseg = F.segment(0, 0, F.symdict_segment(syms, tpl), page=0)
            in_globals = rnd.random() < .5
            tseg = F.segment(2, 6, F.text_segment(syms, insts, W, H, 0, 0, rop, logs, corner, transp, comb, defpix, dsoff, refine, rtpl), refs=[0])
            pseg = F.segment(1, 48, F.page_info(W, H))
            if in_globals: globs, segs = [dseg], [pseg, tseg]
            else: globs, segs = None, [dseg, pseg, tseg]
            if use_mutool:
                mp = mutool_pgm(segs, globs, W, H, td)
                if mp != page: print('MUTOOL differs text case', case, dict(tpl=tpl, logs=logs, corner=corner, transp=transp, comb=comb, defpix=defpix, dsoff=dsoff, rop=rop, refine=refine, rtpl=rtpl)); fails += 1
                total += 1
                continue
            data = b''.join(segs)
            for want in (0, W // 2):
                for bn in bins:
                    res, err = run_jb2(bn, data, b''.join(globs) if globs else None, W, H, want, td)
                    total += 1
                    if res is None: print('FAIL run', case, err); fails += 1; continue
                    got, sc = res
                    if got != jb2_expect(page, W, H, sc):
                        open('/tmp/failt_%d.jb2' % case, 'wb').write(data)
                        print('FAIL text case', case, W, H, 'want', want, dict(tpl=tpl, logs=logs, corner=corner, transp=transp, comb=comb, defpix=defpix, dsoff=dsoff, rop=rop, refine=refine, rtpl=rtpl, glob=in_globals)); fails += 1
    print('jbig2 text%s: %d runs, %d failed' % (' (mutool)' if use_mutool else '', total, fails))
    return fails

if __name__ == '__main__':
    a = sys.argv[1:]
    seed = 1
    while a and a[0].startswith('--'):
        if a[0] == '--bin': BIN = a[1]; a = a[2:]
        elif a[0] == '--bins': BINS = a[1].split(','); a = a[2:]
        elif a[0] == '--seed': seed = int(a[1]); a = a[2:]
        else: break
    rnd = random.Random(seed)
    f = 0
    for what in a or ['ccitt']:
        if what == 'ccitt': f += check_ccitt(rnd)
        if what == 'jb2-generic': f += check_jb2_generic(rnd, BINS)
        if what == 'jb2-text': f += check_jb2_text(rnd, BINS)
        if what == 'mutool-jb2-text': f += check_jb2_text(rnd, BINS, True)
        if what == 'mutool-ccitt': f += check_mutool_ccitt(rnd)
    sys.exit(1 if f else 0)

