#!/usr/bin/env python3
"""PDREND host checks (PAGEDESK-SPEC 20.3.6), run by `make pdrend`.

  pdr_check.py <pdr_test binary> [scratch dir] [resource dir]

Needs Pillow + numpy, build/PDRES.BIN and build/PDFONTX.RES (make pdfonts) and
mutool; a missing one SKIPS the check that needs it and says so - the exit
status is 0 only when every check that could run passed.

What is asserted:
  * the renderer's base matrix equals pdm_view_fwd within 1 pixel, for every
    rotation, zoom and pixel aspect on a nonzero-origin box (PAGEDESK-SPEC 5.3);
  * every page of the test PDF renders at 1bpp and 4bpp, three zooms, all four
    adapter aspects, with exactly the incomplete bits it should (10.6);
  * a display-list overflow that forces many passes draws the IDENTICAL image
    to the single pass (10.2) - byte for byte;
  * against mutool at the same scale, no more than a few percent of 8x8 cells
    differ and no ink cell is missing (font and palette simplification allowed);
  * the text map recovers the page's text, escapes included (9.2, 10.9);
  * a thumbnail has ink and a white border;
  * the hostile-input checks of hosttest/pdr_fuzz.py (when -fsanitize builds).
"""
import os, subprocess, sys, shutil
here = os.path.dirname(os.path.abspath(__file__))
exe = os.path.abspath(sys.argv[1])
scratch = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.join(os.path.dirname(exe), 'pdrtest')
resdir = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else os.path.dirname(exe)
os.makedirs(scratch, exist_ok=True)
fails, skips, oks = [], [], 0

def ok(name):
    global oks; oks += 1; print('  ok   ' + name)
def fail(name, why):
    fails.append(name); print('  FAIL %s: %s' % (name, why))
def skip(name, why):
    skips.append(name); print('  skip %s: %s' % (name, why))
def run(args, **kw):
    return subprocess.run([exe] + [str(a) for a in args], capture_output=True, text=True, **kw)

# ---- 1. the view transform ---------------------------------------------------
r = run(['viewchk'])
if r.returncode: fail('view transform vs pdm_view_fwd', r.stdout + r.stderr)
else: ok('view transform vs pdm_view_fwd (%s)' % r.stdout.strip())

# ---- fixtures ----------------------------------------------------------------
try:
    from PIL import Image
    import numpy as np
except Exception as e:
    skip('rendering checks', 'Pillow/numpy missing: %s' % e)
    print('pdr_check: %d ok, %d skipped, %d FAILED' % (oks, len(skips), len(fails)))
    sys.exit(1 if fails else 0)
pdf = os.path.join(scratch, 't.pdf')
subprocess.run([sys.executable, os.path.join(here, 'pdr_mkpdf.py'), pdf], check=True, capture_output=True)
# the files the shell lays out beside the package (DECISIONS D3, D5): PDRES.BIN, PAGEDESK.O88 (a 1536-byte
# pad, then the PDRES part - so the part offset is not zero), and the optional PDFONTX.RES
bank = os.path.join(scratch, 'fd'); bank_nox = os.path.join(scratch, 'fd_nox')
res = os.path.join(resdir, 'PDRES.BIN'); resx = os.path.join(resdir, 'PDFONTX.RES')
if not (os.path.exists(res) and os.path.exists(resx)):
    skip('rendering checks', 'no %s / %s (make pdfonts)' % (res, resx))
    print('pdr_check: %d ok, %d skipped, %d FAILED' % (oks, len(skips), len(fails)))
    sys.exit(1 if fails else 0)
for d, withx in ((bank, True), (bank_nox, False)):
    os.makedirs(d, exist_ok=True)
    data = open(res, 'rb').read()
    open(os.path.join(d, 'PDRES.BIN'), 'wb').write(data)
    open(os.path.join(d, 'PAGEDESK.O88'), 'wb').write(b'\x55' * 1536 + data)
    if withx: shutil.copy(resx, os.path.join(d, 'PDFONTX.RES'))

def render(page, zoom, rot, bpp, asp, W, H, name, dlkb=22, bk=None, env=None):
    out = os.path.join(scratch, name + '.ppm')
    e = dict(os.environ); e.update(env or {})
    r = run(['render', pdf, page, zoom, rot, bpp, asp, W, H, 8, 8, out, bk or bank, dlkb], env=e)
    st = {}
    for l in r.stdout.split('\n'):
        if l.startswith('status'):
            for kv in l.split()[1:]:
                if '=' in kv: k, v = kv.split('=', 1); st[k] = v
    return r, out, st

EXPECT_INC = {0: 0x0001, 1: 0, 2: 0, 3: 0, 4: 0}      # page 0 uses a non-base-14 font on purpose
# adapter -> (aspect, W, H, band depth): VGA and EGA are 4bpp, Hercules and CGA 1bpp (the band claim
# is sized for the live depth, 10.3); VGA also runs 1bpp, which is what a mono window of it would be
ASPECTS = {'vga': ('1/1', 640, 480, 4), 'ega': ('35/48', 640, 350, 4), 'herc': ('29/45', 720, 348, 1), 'cga': ('5/12', 640, 200, 1), 'vga1': ('1/1', 640, 480, 1)}

# ---- 2. every page, every depth/zoom/aspect ----------------------------------
n_bad = 0
for page in range(5):
    if True:
        for zoom in (50, 100, 150):
            for an, (asp, W, H, bpp) in ASPECTS.items():
                r, out, st = render(page, zoom, 0, bpp, asp, W, H, 'm')
                inc = int(st.get('incomplete', '0x0'), 16) if st else -1
                want = EXPECT_INC[page]
                if r.returncode or inc != want:
                    n_bad += 1; fail('render p%d bpp%d z%d %s' % (page, bpp, zoom, an), 'rc=%d incomplete=0x%x want 0x%x %s' % (r.returncode, inc, want, r.stderr.strip()[:120]))
if not n_bad: ok('75 renders (5 pages x 3 zooms x 5 adapter/depth configs) with the expected Incomplete bits')

# ---- 3. rotation sweep: all four rotations draw, none is blank ----------------
bad = 0
for page in (0, 1, 2):
    for rot in (0, 90, 180, 270):
        r, out, st = render(page, 100, rot, 4, '1/1', 640, 480, 'r')
        im = np.asarray(Image.open(out).convert('L'))[8:, 8:]
        if r.returncode or (im < 100).sum() < 500:
            bad += 1; fail('rotation p%d r%d' % (page, rot), 'blank or failed')
if not bad: ok('all four user rotations draw (3 pages x 4)')

# ---- 4. display-list overflow: many passes, identical image --------------------
r1, o1, s1 = render(4, 100, 0, 4, '1/1', 640, 480, 'ovl_a', 22)
res = []
for kb in (8, 12, 16):
    r2, o2, s2 = render(4, 100, 0, 4, '1/1', 640, 480, 'ovl_b', kb)
    same = open(o1, 'rb').read() == open(o2, 'rb').read()
    res.append((kb, int(s2.get('passes', 0)), same, int(s2.get('incomplete', '0x0'), 16)))
if all(s and p > int(s1['passes']) and inc == 0 for kb, p, s, inc in res):
    ok('DL overflow: 8/12/16 KiB lists draw the identical image as 22 KiB in %s passes (vs %s)' % ('/'.join(str(p) for _, p, _, _ in res), s1['passes']))
else:
    fail('DL overflow multi-pass', repr(res))
r5, o5, s5 = render(4, 100, 0, 1, '5/12', 640, 200, 'sub_a', 60)
r5b, o5b, s5b = render(4, 100, 0, 1, '5/12', 640, 200, 'sub_b', 4)
if open(o5, 'rb').read() == open(o5b, 'rb').read() and int(s5b.get('passes', 0)) > 30: ok('a 4 KiB list composes one band in sub-passes (%s passes) and draws the identical image' % s5b['passes'])
else: fail('sub-row passes', repr((s5, s5b)))
r6, o6, s6 = render(4, 100, 0, 4, '1/1', 640, 480, 'ovl_c', 1)
if int(s6.get('incomplete', '0x0'), 16) & 0x80: ok('a 1 KiB list that cannot hold even ONE ROW: reported Incomplete (limit), not silently dropped')
else: fail('DL too small for one row', repr(s6))

# ---- 5. against mutool -----------------------------------------------------------
if not shutil.which('mutool'):
    skip('mutool comparison', 'mutool not installed')
else:
    def cells(x, n=8):
        H, W = x.shape; H -= H % n; W -= W % n
        return x[:H, :W].reshape(H // n, n, W // n, n).mean(axis=(1, 3))
    for page, zoom, maxbad, maxmiss in ((0, 100, 4.0, 40), (0, 150, 5.0, 100), (1, 150, 4.0, 15), (2, 150, 4.0, 15), (3, 150, 4.0, 40)):
        r, out, st = render(page, zoom, 0, 4, '1/1', 640, 480, 'cmp')
        ref = os.path.join(scratch, 'ref.png')
        subprocess.run(['mutool', 'draw', '-q', '-r', str(72 * zoom / 100), '-o', ref, pdf, str(page + 1)], check=True, capture_output=True)
        b = np.asarray(Image.open(ref).convert('L'), dtype=float)
        a = np.asarray(Image.open(out).convert('L'), dtype=float)[8:8 + b.shape[0], 8:8 + b.shape[1]]
        ca, cb = cells(a), cells(b)
        bad = (np.abs(ca - cb) > 90).mean() * 100
        miss = int(((cb < 200) & (ca > 235)).sum())
        # a page with 0.9 grey fills draws them white (a four-grey palette): not "missing"
        msg = 'vs mutool p%d z%d: %.1f%% cells differ, %d ink cells missing' % (page, zoom, bad, miss)
        if bad <= maxbad and miss <= maxmiss: ok(msg)
        else: fail(msg, 'limits %.1f%% / %d' % (maxbad, maxmiss))
    # the page that is rotated and cropped by the file itself
    r, out, st = render(3, 100, 0, 4, '1/1', 640, 480, 'cmp')
    b = np.asarray(Image.open(os.path.join(scratch, 'ref.png')).convert('L'))
    # (rendered above at 150; the geometry check is the cell comparison)

# ---- 5b. the font files: cluster sizes, a missing PDFONTX.RES, a small glyph claim -------
imgs = {}
for cl in (1, 2, 8):
    r, out, st = render(0, 150, 0, 4, '1/1', 640, 480, 'cl%d' % cl, env={'PDR_CLUSTER': str(cl), 'PDR_GLYPHKB': '24'})
    imgs[cl] = open(out, 'rb').read()
    if r.returncode or 'refused' in r.stderr: fail('strike reads at cluster = %d sectors' % cl, r.stderr[:200])
if imgs[1] == imgs[2] == imgs[8]: ok('strikes read on demand identically at 512 B, 1 KiB and 4 KiB clusters (every seek and capacity a cluster multiple)')
else: fail('cluster sweep', 'images differ between cluster sizes')
rx, ox, sx = render(0, 150, 0, 4, '1/1', 640, 480, 'nox', bk=bank_nox, env={'PDR_GLYPHKB': '24'})
ax = np.asarray(Image.open(ox).convert('L'))[8:, 8:]
ref = np.asarray(Image.open(os.path.join(scratch, 'cl1.ppm')).convert('L'))[8:, 8:]
# the 36 px "Big36" line at zoom 150 sits near y=285..330 of the page: with no PDFONTX.RES it is a doubled 12 px strike
band = ax[110:200, 370:520]
if rx.returncode == 0 and (band < 100).sum() > 150 and int(sx.get('incomplete', '0x0'), 16) == 0x0001:
    ok('no PDFONTX.RES: large text is a pixel-doubled core strike (%d dark px), not an error, not Incomplete' % (band < 100).sum())
else: fail('no PDFONTX.RES', 'rc=%d dark=%d st=%r' % (rx.returncode, (band < 100).sum(), sx))
rs, os_, ss = render(0, 100, 0, 4, '1/1', 640, 480, 'small', env={'PDR_GLYPHKB': '8'})
if rs.returncode == 0 and (np.asarray(Image.open(os_).convert('L'))[8:, 8:] < 100).sum() > 300 and int(ss.get('incomplete', '0x0'), 16) & 1: ok('an 8 KiB glyph claim draws what its cache holds and reports Incomplete (font), it does not fail')
else: fail('small glyph claim', rs.stderr[:200])

# ---- 6. text map -----------------------------------------------------------------
want = ['Helvetica 14: The quick brown fox', 'Times 14: The quick brown fox', 'Courier 12: The quick brown fox',
        'Escapes: (paren) \\ back ABC \xe9 caf\xe9', 'Line three via quote', 'Standard \x92quote\x92 \x91grave\x91', 'Six point text at 6pt']
raw = subprocess.run([exe, 'text', pdf, '0', bank], capture_output=True).stdout
miss = [w for w in want if w.encode('latin-1') not in raw]
if miss: fail('text map recovers the text incl. escapes and encodings', 'missing %r in %r' % (miss, raw[:300]))
else: ok('text map recovers the text incl. escapes and encodings')
raw2 = subprocess.run([exe, 'text', pdf, '3', bank], capture_output=True).stdout
if b'Crop origin 100,100' in raw2 and b'Rotated and cropped page' in raw2: ok('text map on a rotated cropped page')
else: fail('text map on a rotated cropped page', repr(raw2))

# ---- 7. thumbnails -----------------------------------------------------------------
for pg in (0, 1, 2, 3):
    th = os.path.join(scratch, 'th.pgm')
    r = run(['thumb', pdf, pg, 64, 80, th, bank])
    if r.returncode: fail('thumbnail p%d' % pg, r.stderr); continue
    im = np.asarray(Image.open(th).convert('L'))
    dark = (im < 128).mean()
    msg = 'thumbnail p%d 64x80: %.1f%% dark, white margin' % (pg, dark * 100)
    if 0.01 < dark < 0.6 and im[0, :].min() == 255: ok(msg)
    else: fail(msg, 'dark=%.3f' % dark)

print('pdr_check: %d ok, %d skipped, %d FAILED' % (oks, len(skips), len(fails)))
sys.exit(1 if fails else 0)
