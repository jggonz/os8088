#!/usr/bin/env python3
"""PDREND's core of the any-PDF round (PAGEDESK-SPEC 20.22.7) held to MuPDF and
to itself, through the host harness's stand-in occupants (hosttest/pdr_xmod.c):

  * the ladder: a codec image needs its occupant, the harness loads it on
    PDE_NEEDMOD, the page says which modules it used and what they cost;
    PDR_NOSLOT refuses one and the page says PDF_INC_MODULE;
  * codec images: the stand-in decoder delivers the ramp the plain image on
    ramp.pdf's first page holds, so the DCT and JPX pages must draw it to the
    pixel, at every adapter; two codecs on one page swap the slot per band,
    and two on ONE band are drawn through the stash (20.22.7.10) exactly as
    their plain twins, in the list's order;
  * sample depths (smode 4): 1/2/4/16-bit grey, 8/16-bit RGB, CMYK and a
    /Decode [1 0] image against mutool;
  * the dash walker and the edit marks in PDXG: dash.pdf against mutool, and
    without PDXG the dashes are solid and noted;
  * with --hostile <asan binary>: the occupant answers IMG_OPEN and IMG_ROW
    with hostile sizes, modes and failures; every render must finish clean.

    pdr_xchk.py <pdr_test_real> <scratch> <resdir> [--hostile <pdr_test_real_asan>] [-n SEEDS]
"""
import argparse, os, re, shutil, subprocess, sys

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
def render(pdf, page=0, adapter='vga', zoom=100, env=None, binary=None, H=700, dl=29):
    num, den, w, bpp = ADAPT[adapter]
    out = os.path.join(scratch, 'o.ppm')
    if os.path.exists(out): os.remove(out)
    e = dict(os.environ, PDR_GLYPHKB='24', ASAN_OPTIONS='detect_leaks=0')
    e.update(env or {})
    r = subprocess.run([binary or exe, 'render', pdf, str(page), str(zoom), '0', str(bpp), '%d/%d' % (num, den), str(w), str(H),
                        '0', '0', out, bank, str(dl)], capture_output=True, text=True, env=e, timeout=300)
    st = dict(kv.split('=', 1) for l in r.stdout.split('\n') if l.startswith('status') for kv in l.split()[1:] if '=' in kv)
    m = re.search(r"^xshade calls=(\d+) stepmax=(\d+)", r.stdout, re.M)
    if m: st['xcalls'], st['xstepmax'] = m.group(1), m.group(2)
    m = re.search(r"^status .*first='([^']*)'", r.stdout, re.M)
    if m: st['first'] = m.group(1)
    im = np.asarray(Image.open(out).convert('L')).astype(int) if r.returncode == 0 and os.path.exists(out) else None
    if int(st.get('incomplete', '0'), 0) and not st.get('first'):    # QA R16: the status line would say nothing
        unnamed.append('%s p%d %s' % (os.path.basename(pdf), page, st))
    return r.returncode, im, st, r.stderr
unnamed = []
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
# one slot, a band needing both decoders (20.22.7.10): the resident one's images are
# stashed in the XC and the other is loaded - every image drawn, in the list's order
for pg, what in ((4, 'side by side'), (6, 'overlapping, a rectangle between, the JPX twice')):
    for ad in ('vga', 'herc', 'cga'):
        rc, im, st, err = render(ramp, pg, ad)
        rcp, imp, stp, _ = render(ramp, pg + 1, ad)
        same = im is not None and imp is not None and im.shape == imp.shape and int((im != imp).sum()) == 0
        check('DCT + JPX sharing bands, %s (%s): = the plain images to the pixel, clean' % (what, ad),
              rc == 0 and rcp == 0 and same and num(st, 'incomplete') == 0 and num(st, 'mods') & MOD_IMG and num(st, 'mods') & MOD_JPX,
              'rc=%d diff=%s %s %s' % (rc, None if im is None or imp is None else int((im != imp).sum()), st, err[-200:]))
    if pg == 4:
        check('... one decoder load a shared band (swaps bounded by the bands, not PDR_XASKS)', 2 <= num(st, 'swaps') <= 12, str(st))
rc, im, st, _ = render(ramp, 6, 'vga', env={'PDR_NOXC': '1'})
rcp, imp, _, _ = render(ramp, 7, 'vga')
check('both decoders and no XC for the stash: drawn with one, PDF_INC_LIMIT (not MODULE)', rc == 0
      and num(st, "incomplete") & 0x0080 and not num(st, 'incomplete') & INC_MODULE, str(st))
check('negative control: without the stash page 7 is NOT its plain twin (the to-the-pixel checks can fail)',
      im is not None and imp is not None and im.shape == imp.shape and int((im != imp).sum()) > 2000,
      'diff=%s' % (None if im is None or imp is None else int((im != imp).sum())))
# the primary releases the XC under a NEEDMOD answer, mid-view (20.22.7.11): the
# view goes on - the pass runs again without it - and finishes, every band blitted
# (before the fix: the view ended at the re-INIT, blits 1-5 and nothing noted)
for ad in ('vga', 'herc'):
    rc, im, st, err = render(ramp, 6, ad, env={'PDR_XCDROP': '1'})
    check('XC released under a NEEDMOD answer (%s): the view finishes, PDF_INC_LIMIT, no MODULE' % ad,
          rc == 0 and num(st, 'blits') >= 40 and num(st, 'incomplete') & 0x0080 and not num(st, 'incomplete') & INC_MODULE,
          '%s %s' % (st, err[-200:]))
# PDXF's GLYPH grant moves the glyph claim under a ladder answer, mid-view (20.22.7.11):
# the caches start again from the new claim and the pass re-runs - the page is unchanged
for ad in ('vga', 'herc'):
    rc0, im0, st0, _ = render(ramp, 8, ad)
    rc, im, st, err = render(ramp, 8, ad, env={'PDR_GLYMOVE': '1'})
    same = im is not None and im0 is not None and im.shape == im0.shape and int((im != im0).sum()) == 0
    check('glyph claim moved under a NEEDMOD (%s): the pass re-ran, the page is the same to the pixel, clean' % ad,
          rc == 0 and rc0 == 0 and same and num(st, 'passes') > num(st0, 'passes') and num(st, 'incomplete') == 0
          and im0 is not None and int((im0 < 128).sum()) > 500,
          'rc=%d diff=%s %s %s' % (rc, None if im is None or im0 is None else int((im != im0).sum()), st, err[-200:]))
# Flate Forms in a Flate page with ONE Flate slot (QA R14, 20.22.7.12): each parent is
# parked and replayed, and the page draws as with two slots (where the nesting of three
# Flate streams parks too) - and as mutool draws it
forms = os.path.join(fx, 'forms.pdf')
r1 = render(forms, 0, 'vga', env={'PDR_FLATE': '1'}); r2 = render(forms, 0, 'vga', env={'PDR_FLATE': '2'})
same = r1[1] is not None and r2[1] is not None and int((r1[1] != r2[1]).sum()) == 0
check('Flate Forms nested in a Flate page, ONE slot: = two slots to the pixel, clean', r1[0] == 0 and same
      and num(r1[2], 'incomplete') == 0 and num(r2[2], 'incomplete') == 0, '%s %s' % (r1[2], r2[2]))
ref = mutool(forms, 0)
pct = cellmatch(r1[1], ref, 40) if r1[1] is not None else 100
check('forms.pdf (one slot) vs mutool (<= 3% of cells off by 40)', pct <= 3.0, '%.2f%%' % pct)
r1 = render(forms, 1, 'vga', env={'PDR_FLATE': '1'}); r2 = render(forms, 1, 'vga', env={'PDR_FLATE': '2'})
check('an inline image before the Do, ONE slot: not replayable, noted (form stream); two slots clean',
      num(r1[2], 'incomplete') & 0x0002 and num(r2[2], 'incomplete') == 0, '%s %s' % (r1[2], r2[2]))
rc, im, st, _ = render(ramp, 6, 'vga', env={'PDR_XCKB': '2'})
check('both decoders and a 2 KiB XC: what fits is stashed, the rest PDF_INC_LIMIT, nothing MODULE', rc == 0
      and not num(st, 'incomplete') & INC_MODULE, str(st))
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

print('-- one-pixel strokes (hair.pdf, QA R11)')
import math
rc, im, st, _ = render(os.path.join(fx, 'hair.pdf'), 0, 'vga')
gaps = steps = cover = 0
if im is not None:
    dark = im < 128
    for k in range(0, 360, 7):
        t = math.radians(k + 0.37); c, s_ = math.cos(t), math.sin(t)
        x0, y0, x1, y1 = 200 + 30 * c, 400 - (200 + 30 * s_), 200 + 160 * c, 400 - (200 + 160 * s_)
        steep = abs(s_) >= abs(c)
        a0, a1 = (y0, y1) if steep else (x0, x1)
        for v in range(int(min(a0, a1)) + 1, int(max(a0, a1))):
            if steep:
                u = int(math.floor(x0 + (v + 0.5 - y0) * (x1 - x0) / (y1 - y0)))
                n, m = int(dark[v, u - 1:u + 2].sum()), int(dark[v, u - 2:u + 3].sum())
            else:
                u = int(math.floor(y0 + (v + 0.5 - x0) * (y1 - y0) / (x1 - x0)))
                n, m = int(dark[u - 1:u + 2, v].sum()), int(dark[u - 2:u + 3, v].sum())
            steps += 1; cover += m; gaps += n == 0
check('a fan of 1 w lines at every 7 degrees: a pixel on every row (steep) or column (shallow)',
      rc == 0 and steps > 5000 and gaps == 0, '%d gaps of %d' % (gaps, steps))
check('... and no thicker than 1.5 pixels on the average (%.2f)' % (cover / max(steps, 1)), steps and cover / steps <= 1.5)
check('an axis-aligned 1 w square keeps one-pixel sides', im is not None and
      int((im[2:28, 2:28] < 128).sum()) == 80, None if im is None else str(int((im[2:28, 2:28] < 128).sum())))
print('-- the group plan and the shading cull (small.pdf, QA R15)')
small = os.path.join(fx, 'small.pdf')
for ad in ('vga', 'herc', 'cga'):
    rc, im, st, _ = render(small, 0, ad)
    rc8, im8, st8, _ = render(small, 0, ad, dl=12)
    check('hundreds of shapes a few rows tall (%s): a 12 KiB list draws what a 29 KiB one does, clean (%s and %s passes)'
          % (ad, st.get('passes'), st8.get('passes')),
          rc == 0 and rc8 == 0 and im is not None and im8 is not None and int((im != im8).sum()) == 0
          and num(st, 'incomplete') == 0 and num(st8, 'incomplete') == 0)
# page 2: 96 knobs, each a shading in a clip 3 to 9 pixels across (SYNTH.PDF's buttons), PDXG stood in for by PDR_XSHADE
xs = {'PDR_XSHADE': '1'}
rc, im, st, _ = render(small, 1, 'vga', env=xs)
rcn, imn, stn, _ = render(small, 1, 'vga', env=dict(xs, PDR_NOSHCULL='1'))
check('96 knobs: one PDXG call each in one pass (%s calls, %s passes; %s without the cull)' % (st.get('xcalls'), st.get('passes'), stn.get('xcalls')),
      rc == 0 and num(st, 'incomplete') == 0 and num(st, 'xcalls') == 96 and num(st, 'passes') <= 2 and num(stn, 'xcalls') > 5 * 96)
check('... each cut into no more bands than its clip has pixels (%s, against %s)' % (st.get('xstepmax'), stn.get('xstepmax')),
      num(st, 'xstepmax') <= 10 and num(stn, 'xstepmax') == 32)
for dlk in (16, 12):
    rck, imk, stk, _ = render(small, 1, 'vga', env=xs, dl=dlk)
    check('... a %d KiB list draws them to the pixel, asking PDXG only for a pass\'s own rows (%s calls, %s passes)' % (dlk, stk.get('xcalls'), stk.get('passes')),
          rck == 0 and imk is not None and im is not None and int((imk != im).sum()) == 0 and num(stk, 'incomplete') == 0
          and num(stk, 'xcalls') <= 2 * 96 and num(stk, 'passes') <= 8)
rc, im, st, _ = render(forms, 2, 'vga')
check('a number past 16.16: drawn, PDF_INC_RANGE named (first=number range)', rc == 0 and num(st, 'incomplete') & 0x0100
      and st.get('first') == 'number range', str(st))
check('every Incomplete render names its first reason (QA R16)', not unnamed, '; '.join(unnamed[:4]))
print('pdr_xchk: %d checks, %d FAILED' % (nchecks, len(fails)))
for f in fails: print('  FAILED ' + f)
sys.exit(1 if fails else 0)
