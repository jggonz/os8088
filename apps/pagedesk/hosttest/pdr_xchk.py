#!/usr/bin/env python3
"""PDREND's core of the any-PDF round (PAGEDESK-SPEC 20.22.7) held to MuPDF and
to itself, through the host harness's stand-in occupants (hosttest/pdr_xmod.c):

  * the ladder: a codec image needs its occupant, the harness loads it on
    PDE_NEEDMOD, the page says which modules it used and what they cost;
    PDR_NOSLOT refuses one and the page says PDF_INC_MODULE;
  * codec images: the stand-in decoder delivers the ramp the plain image on
    ramp.pdf's first page holds, so the DCT and JPX pages must draw it to the
    pixel, at every adapter; two codecs on one page swap the slot per band;
  * sample depths (smode 4): 1/2/4/16-bit grey, 8/16-bit RGB, CMYK and a
    /Decode [1 0] image against mutool;
  * the dash walker and the edit marks in PDXG: dash.pdf against mutool, and
    without PDXG the dashes are solid and noted;
  * with --hostile <asan binary>: the occupant answers IMG_OPEN and IMG_ROW
    with hostile sizes, modes and failures; every render must finish clean.

    pdr_xchk.py <pdr_test_real> <scratch> <resdir> [--hostile <pdr_test_real_asan>] [-n SEEDS]
"""
import argparse, os, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument('exe'); ap.add_argument('scratch'); ap.add_argument('resdir')
ap.add_argument('--hostile'); ap.add_argument('-n', type=int, default=40)
a = ap.parse_args()
exe, scratch, resdir = (os.path.abspath(x) for x in (a.exe, a.scratch, a.resdir))
try:
    from PIL import Image
    import numpy as np
except Exception as e:
    print('pdr_xchk: SKIP - Pillow/numpy missing (%s)' % e); sys.exit(0)
if not shutil.which('mutool'):
    print('pdr_xchk: SKIP - mutool missing'); sys.exit(0)
os.makedirs(scratch, exist_ok=True)
here = os.path.dirname(os.path.abspath(__file__))
fx = os.path.join(scratch, 'fx')
subprocess.run([sys.executable, os.path.join(here, '..', '..', '..', 'tools', 'os88pdfx_rcore.py'), fx], check=True, capture_output=True)
bank = os.path.join(scratch, 'fd'); os.makedirs(bank, exist_ok=True)
data = open(os.path.join(resdir, 'PDRES.BIN'), 'rb').read()
open(os.path.join(bank, 'PDRES.BIN'), 'wb').write(data)
open(os.path.join(bank, 'PAGEDESK.O88'), 'wb').write(b'\x55' * 1536 + data)
if os.path.exists(os.path.join(resdir, 'PDFONTX.RES')): shutil.copy(os.path.join(resdir, 'PDFONTX.RES'), bank)

INC_MODULE, INC_IMAGE, INC_COLOUR = 0x0200, 0x0004, 0x0008
MOD_GFX, MOD_IMG, MOD_JPX = 1 << 2, 1 << 4, 1 << 5
fails, nchecks = [], 0
def check(name, cond, why=''):
    global nchecks
    nchecks += 1
    if cond: print('  ok   ' + name)
    else: fails.append(name + ' ' + why); print('  FAIL ' + name + ' ' + why)

ADAPT = {'vga': (1, 1, 640, 4), 'herc': (29, 45, 720, 1), 'cga': (5, 12, 640, 1)}
def render(pdf, page=0, adapter='vga', zoom=100, env=None, binary=None, H=700):
    num, den, w, bpp = ADAPT[adapter]
    out = os.path.join(scratch, 'o.ppm')
    if os.path.exists(out): os.remove(out)
    e = dict(os.environ, PDR_GLYPHKB='24', ASAN_OPTIONS='detect_leaks=0')
    e.update(env or {})
    r = subprocess.run([binary or exe, 'render', pdf, str(page), str(zoom), '0', str(bpp), '%d/%d' % (num, den), str(w), str(H),
                        '0', '0', out, bank, '29'], capture_output=True, text=True, env=e, timeout=300)
    st = dict(kv.split('=', 1) for l in r.stdout.split('\n') if l.startswith('status') for kv in l.split()[1:] if '=' in kv)
    im = np.asarray(Image.open(out).convert('L')).astype(int) if r.returncode == 0 and os.path.exists(out) else None
    return r.returncode, im, st, r.stderr
def num(st, k):
    v = st.get(k, '0')
    return int(v, 16) if v.startswith('0x') else int(v)
def mutool(pdf, page, dpi=72):
    # -N: no ICC, so DeviceRGB to grey is PDF's own 0.3 / 0.59 / 0.11, as PDREND's
    ref = os.path.join(scratch, 'ref.png')
    subprocess.run(['mutool', 'draw', '-q', '-N', '-r', str(dpi), '-c', 'gray', '-o', ref, pdf, str(page + 1)], capture_output=True, check=True)
    return np.asarray(Image.open(ref).convert('L')).astype(int)
def cells(x, k=8):
    H, W = x.shape; H -= H % k; W -= W % k
    return x[:H, :W].reshape(H // k, k, W // k, k).mean(axis=(1, 3))
def cellmatch(a, b, thresh):
    """share of 8x8 cells differing by more than `thresh` grey levels"""
    h = min(a.shape[0], b.shape[0]); w = min(a.shape[1], b.shape[1])
    return float((np.abs(cells(a[:h, :w]) - cells(b[:h, :w])) > thresh).mean() * 100)

ramp, depth, dash, mask = (os.path.join(fx, n) for n in ('ramp.pdf', 'depth.pdf', 'dash.pdf', 'mask.pdf'))

print('-- codec images through the stand-in decoder (ramp.pdf)')
for ad in ('vga', 'herc', 'cga'):
    rc0, im0, st0, _ = render(ramp, 0, ad)
    check('ramp raw %s draws' % ad, rc0 == 0 and im0 is not None and num(st0, 'incomplete') == 0, str(st0))
    for pg, name, bit in ((1, 'DCT', MOD_IMG), (2, 'JPX', MOD_JPX)):
        rc, im, st, err = render(ramp, pg, ad)
        same = im is not None and im0 is not None and im.shape == im0.shape and int((im != im0).sum()) == 0
        check('ramp %s %s = the plain image to the pixel' % (name, ad), rc == 0 and same,
              'rc=%d diff=%s %s %s' % (rc, None if im is None or im0 is None else int((im != im0).sum()), st, err[-200:]))
        check('ramp %s %s used its occupant, clean' % (name, ad), num(st, 'mods') & bit and num(st, 'incomplete') == 0, str(st))
rc, im, st, _ = render(ramp, 0, 'vga')
ref = mutool(ramp, 0)
check('ramp raw vs mutool', rc == 0 and cellmatch(im, ref, 24) < 2.0, '%.2f%% cells' % (cellmatch(im, ref, 24) if im is not None else 100))
rc, im, st, _ = render(ramp, 3, 'vga')
check('a DCT and a JPX image apart: both used, one swap each, clean', rc == 0 and num(st, 'mods') & MOD_IMG and num(st, 'mods') & MOD_JPX
      and num(st, 'incomplete') == 0 and num(st, 'swaps') == 2, str(st))
rc, im, st, _ = render(ramp, 4, 'vga')
# KNOWN LIMIT (20.22.7.4): one slot, so a band whose images need both decoders draws
# with one; the other's rows there are not drawn and the page says PDF_INC_MODULE
check('a DCT and a JPX image sharing rows: one decoder a band, no ping-pong, noted', rc == 0
      and num(st, 'incomplete') == INC_MODULE and num(st, 'swaps') <= 2, str(st))
for ad in ('vga', 'cga'):
    rc, im, st, _ = render(ramp, 1, ad, env={'PDR_NOSLOT': '4'})
    rcw, imw, _, _ = render(ramp, 0, ad)
    check('DCT with the image module refused (%s): PDF_INC_MODULE, nothing drawn' % ad,
          rc == 0 and num(st, 'incomplete') & INC_MODULE and im is not None and imw is not None and int((im != imw).sum()) > 5000, str(st))
rc, im, st, _ = render(ramp, 1, 'vga', env={'PDR_XFAKE': 'n8'})
check('DCT delivering RGB (PDIM_N8): drawn, clean', rc == 0 and num(st, 'incomplete') == 0 and im is not None and int((im < 250).sum()) > 1000, str(st))
rc, im, st, _ = render(ramp, 1, 'vga', env={'PDR_XFAKE': 'bits'})
check('DCT delivering 1-bit rows: drawn, clean', rc == 0 and num(st, 'incomplete') == 0 and im is not None and int((im < 128).sum()) > 1000, str(st))
rc, im, st, _ = render(ramp, 1, 'vga', zoom=25)
check('DCT at 25%: drawn, clean', rc == 0 and num(st, 'incomplete') == 0, str(st))

print('-- sample depths and colour (smode 4): depth.pdf = its 8-bit grey equivalent, which = mutool')
depthg = os.path.join(fx, 'depthg.pdf')
for ad in ('vga', 'herc', 'cga'):
    rc, im, st, _ = render(depth, 0, ad)
    rcg, img, stg, _ = render(depthg, 0, ad)
    check('depth.pdf %s = depthg.pdf to the pixel, clean' % ad, rc == 0 and rcg == 0 and im is not None and img is not None
          and int((im != img).sum()) == 0 and num(st, 'incomplete') == 0, 'diff=%s %s' % (None if im is None or img is None else int((im != img).sum()), st))
rcg, img, stg, _ = render(depthg, 0, 'vga')
ref = mutool(depthg, 0)
pct = cellmatch(img, ref, 24) if img is not None else 100
check('depthg.pdf vga vs mutool (<= 2% of cells off by 24)', rcg == 0 and pct <= 2.0, '%.2f%%' % pct)

print('-- the dash walker in PDXG (dash.pdf)')
ref = mutool(dash, 0)
rc, im, st, _ = render(dash, 0, 'vga')
pct = cellmatch(im, ref, 90) if im is not None else 100
check('dash.pdf vs mutool (<= 3% of cells off by 90)', rc == 0 and pct <= 3.0, '%.2f%% %s' % (pct, st))
check('dash.pdf used PDXG, clean', num(st, 'mods') & MOD_GFX and num(st, 'incomplete') == 0, str(st))
rc, im2, st2, _ = render(dash, 0, 'vga', env={'PDR_NOSLOT': '2'})
check('dash.pdf without PDXG: solid, PDF_INC_MODULE', rc == 0 and num(st2, 'incomplete') & INC_MODULE and im2 is not None
      and int((im2 < 128).sum()) > int((im < 128).sum()), str(st2))

print('-- masks (mask.pdf): drawn, noted until blend lands')
rc, im, st, _ = render(mask, 0, 'vga')
check('mask.pdf finishes', rc == 0, str(st))

if a.hostile:
    print('-- hostile occupant answers (%d seeds, sanitized)' % a.n)
    bad = 0
    for seed in range(1, a.n + 1):
        for f, pg in ((ramp, 1), (ramp, 3), (ramp, 2)):
            for ad in ('vga', 'cga'):
                rc, im, st, err = render(f, pg, ad, env={'PDR_XHOSTILE': str(seed)}, binary=os.path.abspath(a.hostile))
                if rc != 0 or 'runtime error' in err or 'AddressSanitizer' in err:
                    bad += 1
                    if bad < 5: print('    seed %d p%d %s rc=%d %s' % (seed, pg, ad, rc, err[-300:]))
    check('hostile IMG_OPEN / IMG_ROW: every render finishes clean', bad == 0, '%d bad' % bad)

print('pdr_xchk: %d checks, %d FAILED' % (nchecks, len(fails)))
for f in fails: print('  FAILED ' + f)
sys.exit(1 if fails else 0)
