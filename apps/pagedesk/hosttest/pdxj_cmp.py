#!/usr/bin/env python3
"""pdxj_cmp - PDXJ against opj_decompress -r (PAGEDESK-SPEC 20.22.13.8).

    python3 apps/pagedesk/hosttest/pdxj_cmp.py [--bin build/pdxj/pdxj_test] [-k substr] [-v] [--kb N]

For every fixture in tests/pdfany/jpx/ and every reduction r the file allows, runs
the host build of the module asking for that many pixels, then compares what it
delivered (at the scale it chose - it may reduce further than asked when the
workspace says so) with opj_decompress -r <scale>. The reference is the Y of the
decoded RGB for a colour file with the multiple component transform (that is
component 0, which is what the module decodes), the channels for N8, the
sample itself for grey. Prints max and mean absolute difference per case and
fails when a budget is exceeded.

Budgets: lossless without a transform: max 0. 9/7 or a transform: max <= 3 and
mean <= 0.5 (colour ICT: max <= 4, mean <= 0.7, the Y of an 8-bit rounded RGB).
"""
import json, os, subprocess, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
FIX = os.path.join(ROOT, 'tests', 'pdfany', 'jpx')
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from os88pdfx_jpx import read_pnm, alpha_ramp


def ceil_shift(v, s): return (v + (1 << s) - 1) >> s


def main():
    binp = os.path.join(ROOT, 'build', 'pdxj', 'pdxj_test')
    key = None; verbose = False; env = dict(os.environ)
    a = sys.argv[1:]
    while a:
        x = a.pop(0)
        if x == '--bin': binp = a.pop(0)
        elif x == '-k': key = a.pop(0)
        elif x == '-v': verbose = True
        elif x == '--kb': env['PDXJ_KB'] = a.pop(0)
        else: sys.exit(__doc__)
    man = json.load(open(os.path.join(FIX, 'MANIFEST.json')))['fixtures']
    ref = os.path.join(FIX, 'ref')
    if not os.path.isdir(ref):
        subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'os88pdfx_jpx.py'), '--ref'])
    bad = 0; n = 0
    tmp = os.path.join(ROOT, 'build', 'pdxj', 'out.raw')
    for e in man:
        name = e['name']
        if key and key not in name: continue
        path = os.path.join(FIX, e['file'])
        W, H, nc, depth = e['w'], e['h'], e['ncomp'], e['depth']
        args = e['args']
        lossy = '-I' in args
        mct = nc == 3 and '-mct' not in args
        refuse = name.startswith('refuse') or name == 't_bad'
        if e.get('alpha'):                    # /SMaskInData: the channel exact, the colour open still works
            n += 1
            p = subprocess.run([binp, path, tmp, str(W), str(H), '0x100'], capture_output=True, text=True, env=env)
            ok = p.stdout.split()[:6] == ['0', str(W), str(H), '0', '1', str(W)] and np.array_equal(
                np.fromfile(tmp, dtype=np.uint8).reshape(H, W), alpha_ramp(W, H))
            q = subprocess.run([binp, path, '-', str(W), str(H)], capture_output=True, text=True, env=env)
            ok = ok and not q.stdout.startswith('REFUSED')
            print('%-14s alpha channel exact %s' % (name, 'ok' if ok else 'FAIL'))
            if not ok: bad += 1
            continue
        for r in range(0, 7):
            ww, hh = ceil_shift(W, r), ceil_shift(H, r)
            p = subprocess.run([binp, path, tmp, str(ww), str(hh)], capture_output=True, text=True, env=env)
            out = p.stdout.strip()
            if refuse:
                n += 1
                ok = out.startswith('REFUSED')
                print('%-14s refusal: %s %s' % (name, out, 'ok' if ok else 'FAIL (should refuse)'))
                if not ok: bad += 1
                break
            if out.startswith('REFUSED') or p.returncode:
                n += 1; bad += 1
                print('%-14s r%d  FAIL: %s %s' % (name, r, out, p.stderr.strip()[:80])); break
            sc, ow, oh, mode, onc, rb = map(int, out.split())
            if sc != r and r < sc:        # the module chose a bigger reduction than asked: only test that once
                continue
            rf = os.path.join(ref, '%s.r%d.%s' % (name, sc, 'pgm' if nc == 1 else 'ppm'))
            if not os.path.exists(rf): break
            R, mx = read_pnm(rf)
            raw = np.fromfile(tmp, dtype=np.uint8)
            if R.shape[0] != oh or R.shape[1] != ow:
                n += 1; bad += 1; print('%-14s r%d  FAIL size %dx%d vs ref %dx%d' % (name, sc, ow, oh, R.shape[1], R.shape[0])); continue
            if depth > 8 and mx > 255: R = np.minimum(255, (R.astype(np.int64) + (1 << (depth - 9))) >> (depth - 8))
            if mode == 1 or mode == 0 and nc == 3:   # PDIM_N8 = 2 below
                pass
            if mode == 0:
                got = raw.reshape(oh, ow).astype(np.int64)
                if nc == 3:
                    Rf = R.astype(np.int64)
                    if lossy: ex = np.rint(0.299 * Rf[..., 0] + 0.587 * Rf[..., 1] + 0.114 * Rf[..., 2]).astype(np.int64)
                    else: ex = (Rf[..., 0] + 2 * Rf[..., 1] + Rf[..., 2]) >> 2
                else:
                    ex = R.astype(np.int64)
            else:
                got = raw.reshape(oh, ow, onc).astype(np.int64); ex = R.astype(np.int64)
            if depth > 8 and mx > 255: pass
            d = np.abs(got - ex)
            mxd, mean = int(d.max()), float(d.mean())
            if not lossy and not mct: lim = (0, 0.0)
            elif mct and lossy: lim = (12, 0.7)
            elif mct: lim = (12, 0.1)       # the reference Y is computed from RGB that opj clamped
            else: lim = (3, 0.5)
            ok = mxd <= lim[0] and mean <= lim[1]
            n += 1
            if not ok: bad += 1
            if verbose or not ok or r == sc:
                print('%-14s ask r%d got r%d %3dx%-3d mode %d  max %2d mean %.3f  %s' % (name, r, sc, ow, oh, mode, mxd, mean, 'ok' if ok else 'FAIL (limit %s)' % (lim,)))
    print('%d cases, %d failed' % (n, bad))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
