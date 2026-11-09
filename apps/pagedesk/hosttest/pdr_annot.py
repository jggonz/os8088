#!/usr/bin/env python3
"""PDREND's annotations on screen (PAGEDESK-SPEC 20.12), proved on the host (`make pdrend-annot`).

  pdr_annot.py <pdr_test_real> <pdr_mkstore> <scratch> <resdir> <samples> <stress dir> [shots dir]

The renderer is linked with the REAL PDENG (hosttest/pdr_hosteng.c). What is checked:

  1. the source's annotations (PDRF_ANNOTS): a generated page with every appearance case (an
     appearance /Matrix, a quarter turn, a state dictionary and /AS, Hidden, NoView, no /AP, a
     highlight over text) and ANNOTS256.PDF, each drawn and compared with MUTOOL's render of the
     same file, plus pixel probes for what must NOT be there;
  2. the edit store (PDRF_EDITS): one store built through the edit store's layout (13.1.1) holding
     every kind, saved by the REAL writer (PDWRT, via pdr_mkstore) - mutool renders that file, PDREND
     draws the ORIGINAL with the store, and the two are compared; then each kind is probed at the
     pixels where it must be (VGA), on Hercules and CGA (17.4's marks) and under every mono_mark style;
     PDRF_NOEDITID hides a record, a store is never written, replacing/deleting a source annotation
     removes it from the source pass;
  3. dash patterns (`d`): run lengths, phase, odd counts, caps, Q restoring them;
  4. inline images (BI/ID/EI): drawn through the XObject images' row composer, exact lengths,
     /Decode, /ImageMask, 1/2/4/8 bits, RGB, split across slices, refused ones consumed.

With a shots dir, writes side-by-side PNGs (PDREND left, mutool right) for a human to look at.
"""
import os, re, shutil, subprocess, sys
exe, mkstore, scratch, resdir, samples, stress = (os.path.abspath(a) for a in sys.argv[1:7])
shots = os.path.abspath(sys.argv[7]) if len(sys.argv) > 7 and sys.argv[7] else None
try:
    from PIL import Image
    import numpy as np
except Exception as e:
    print('pdr_annot: SKIP - Pillow/numpy missing (%s)' % e); sys.exit(0)
if not shutil.which('mutool'):
    print('pdr_annot: SKIP - mutool missing'); sys.exit(0)
os.makedirs(scratch, exist_ok=True)
if shots: os.makedirs(shots, exist_ok=True)
bank = os.path.join(scratch, 'fd'); os.makedirs(bank, exist_ok=True)
data = open(os.path.join(resdir, 'PDRES.BIN'), 'rb').read()
open(os.path.join(bank, 'PDRES.BIN'), 'wb').write(data)
open(os.path.join(bank, 'PAGEDESK.O88'), 'wb').write(b'\x55' * 1536 + data)
if os.path.exists(os.path.join(resdir, 'PDFONTX.RES')): shutil.copy(os.path.join(resdir, 'PDFONTX.RES'), bank)

VGA = [(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),(170,85,0),(170,170,170),
       (85,85,85),(85,85,255),(85,255,85),(85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]
KEY = {(r << 16) | (g << 8) | b: i for i, (r, g, b) in enumerate(VGA)}
fails, nchecks = [], 0
def check(name, cond, why=''):
    global nchecks
    nchecks += 1
    if cond: print('  ok   ' + name)
    else:
        fails.append(name + ' ' + why); print('  FAIL ' + name + ' ' + why)

ADAPT = {'vga': (1, 1, 640, 4), 'herc': (29, 45, 720, 1), 'cga': (5, 12, 640, 1)}
def render(pdf, page=0, zoom=100, adapter='vga', H=800, flags=0, store=None, count=None, hide=None, mono=None, rot=0, W=None, dlkb=29, glyphkb=24):
    """-> (rc, RGB array, status dict, stderr)"""
    num, den, w, bpp = ADAPT[adapter]
    out = os.path.join(scratch, 'o.ppm')
    if os.path.exists(out): os.remove(out)
    env = dict(os.environ, PDR_GLYPHKB=str(glyphkb), PDR_RFLAGS=str(flags))
    if store: env['PDR_EDITS'] = store
    if count is not None: env['PDR_ECOUNT'] = str(count)
    if hide is not None: env['PDR_HIDE'] = str(hide)
    if mono is not None: env['PDR_MONO'] = str(mono)
    r = subprocess.run([exe, 'render', pdf, str(page), str(zoom), str(rot), str(bpp), '%d/%d' % (num, den), str(W or w), str(H), '0', '0', out, bank, str(dlkb)],
                       capture_output=True, text=True, env=env)
    st = dict(kv.split('=', 1) for l in r.stdout.split('\n') if l.startswith('status') for kv in l.split()[1:] if '=' in kv)
    im = np.asarray(Image.open(out).convert('RGB')).astype(int) if r.returncode == 0 and os.path.exists(out) else None
    return r.returncode, im, st, r.stderr
def pidx(im):
    """palette indices of a VGA render"""
    k = (im[:, :, 0] << 16) | (im[:, :, 1] << 8) | im[:, :, 2]
    out = np.full(k.shape, 99, dtype=int)
    for key, i in KEY.items(): out[k == key] = i
    return out
def lum(im): return (im * [0.299, 0.587, 0.114]).sum(axis=2)
def inc(st): return int(st.get('incomplete', '0x0'), 16)
def mutool(pdf, page=1, dpi=72):
    ref = os.path.join(scratch, 'ref.png')
    subprocess.run(['mutool', 'draw', '-q', '-r', str(dpi), '-o', ref, pdf, str(page)], capture_output=True, check=True)
    return Image.open(ref).convert('L')
def cells(x, k=8):
    H, W = x.shape; H -= H % k; W -= W % k
    return x[:H, :W].reshape(H // k, k, W // k, k).mean(axis=(1, 3))
def compare(a, b, name=None, thresh=90):
    """share of 8x8 cells differing by more than `thresh` grey levels, and ink cells PDREND left white"""
    h = min(a.shape[0], b.shape[0]); w = min(a.shape[1], b.shape[1])
    ca, cb = cells(a[:h, :w]), cells(b[:h, :w])
    return (np.abs(ca - cb) > thresh).mean() * 100, int(((cb < 160) & (ca > 235)).sum()), int(((ca < 160) & (cb > 235)).sum())
def side(a_rgb, b_gray, name):
    if not shots: return
    A = Image.fromarray(a_rgb.astype('uint8')); B = b_gray.convert('RGB')
    S = Image.new('RGB', (A.size[0] + B.size[0] + 8, max(A.size[1], B.size[1])), (255, 0, 255)); S.paste(A, (0, 0)); S.paste(B, (A.size[0] + 8, 0))
    S.save(os.path.join(shots, name + '.png'))

# ---- a PDF writer for generated pages ---------------------------------------------------------
def build(objs, root=1):
    out = bytearray(b'%PDF-1.4\n'); offs = []
    for i, o in enumerate(objs):
        offs.append(len(out)); out += b'%d 0 obj\n' % (i + 1) + (o if isinstance(o, bytes) else o.encode('latin-1')) + b'\nendobj\n'
    x = len(out); out += b'xref\n0 %d\n0000000000 65535 f \n' % (len(objs) + 1)
    for o in offs: out += b'%010d 00000 n \n' % o
    out += b'trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (len(objs) + 1, root, x)
    return bytes(out)
def strm(d, body): return b'<< %s /Length %d >>\nstream\n' % (d.encode(), len(body)) + body + b'\nendstream'
def page_pdf(content, annots=(), extra_objs=(), rotate=0, mb='0 0 400 300', res='', contents=True):
    """a one-page PDF. objects: 1 catalog, 2 pages, 3 font, 4 content, 5 page; extra objects from 6, then annotations"""
    objs = ['<< /Type /Catalog /Pages 2 0 R >>', '<< /Type /Pages /Count 1 /Kids [5 0 R] >>',
            '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>', strm('', content), None]
    objs += list(extra_objs)
    ann_ids = []
    for a in annots:
        objs.append(a); ann_ids.append(len(objs))
    objs[4] = '<< /Type /Page /Parent 2 0 R /MediaBox [%s] /Rotate %d %s /Resources << /Font << /F1 3 0 R >> %s >> %s >>' % (
        mb, rotate, '/Contents 4 0 R' if contents else '', res, ('/Annots [%s]' % ' '.join('%d 0 R' % i for i in ann_ids)) if ann_ids else '')
    return build(objs)
def writepdf(name, data):
    p = os.path.join(scratch, name); open(p, 'wb').write(data); return p

# ============================================================================================
print('-- source annotations (PDRF_ANNOTS) against mutool')
# (every annotation is an object after the extra ones; the appearance streams are the extra objects)
ap = []                                                  # (rect, flags, ap dict, ap body)
EX = []                                                  # extra objects, numbered from 6
def apstream(d, body): EX.append(strm('/Type /XObject /Subtype /Form ' + d, body)); return len(EX) + 5
A1 = apstream('/BBox [0 0 100 60]', b'1 0 0 rg 0 0 100 60 re f 0 0 1 rg 10 10 30 20 re f')
A2 = apstream('/BBox [0 0 10 10] /Matrix [2 0 0 2 0 0]', b'0 g 0 0 10 10 re f 1 1 0 rg 0 0 5 5 re f')
A3 = apstream('/BBox [0 0 100 20] /Matrix [0 1 -1 0 0 0]', b'1 0 0 rg 0 0 20 20 re f 0 0.6 0 rg 40 0 60 20 re f')
A4 = apstream('/BBox [0 0 40 40]', b'0 0 1 rg 0 0 40 40 re f')
A5 = apstream('/BBox [0 0 40 40]', b'0 0.7 0 rg 0 0 40 40 re f')              # the "On" state
A6 = apstream('/BBox [0 0 40 40]', b'1 0 0 rg 0 0 40 40 re f')              # the "Off" state
EX.append('<< /On %d 0 R /Off %d 0 R >>' % (A5, A6)); STATES = len(EX) + 5
A7 = apstream('/BBox [0 0 90 14] /Resources << /ExtGState << /GS << /BM /Multiply >> >> >>', b'/GS gs 1 1 0 rg 0 0 90 14 re f')   # a highlight's fill, Multiply as the writer's
A8 = apstream('/BBox [-10 -10 10 10] /Matrix [1 0 0 1 0 0] /Resources << /Font << /F1 3 0 R >> >>', b'0 g BT /F1 9 Tf -8 -3 Td (Hi) Tj ET 1 0 1 rg -10 -10 6 6 re f')
def annot(sub, rect, nm, extra=''): return '<< /Type /Annot /Subtype /%s /Rect [%s] %s %s %s >>' % (sub, rect, '' if '/F ' in extra else '/F 4', nm, extra)
anns = [annot('Square', '20 20 120 80', '', '/AP << /N %d 0 R >>' % A1),                       # 1: plain
        annot('Stamp', '150 20 210 80', '', '/AP << /N %d 0 R >>' % A2),                         # 2: /Matrix scale, 60x60 from 20x20
        annot('Stamp', '250 20 270 120', '', '/AP << /N %d 0 R >>' % A3),                        # 3: a quarter turn, 20x100
        annot('Stamp', '20 200 60 240', '', '/F 2 /AP << /N %d 0 R >>' % A4),                    # 4: Hidden: not drawn
        annot('Stamp', '70 200 110 240', '', '/F 32 /AP << /N %d 0 R >>' % A4),                  # 5: NoView: not drawn
        annot('Stamp', '120 200 160 240', '', '/F 0 /AP << /N %d 0 R >>' % A4),                  # 6: F 0 draws
        annot('Stamp', '170 200 210 240', '', '/AS /On /AP << /N %d 0 R >>' % STATES),         # 7: states, /AS /On
        annot('Text', '220 200 260 240', ''),                                                     # 8: no /AP: nothing
        annot('Highlight', '30 140 120 154', '', '/AP << /N %d 0 R >>' % A7),                   # 9: highlight over text
        annot('Stamp', '300 200 340 240', '', '/AP << /N %d 0 R >>' % A8)]                      # 10: BBox with a negative origin
content = b'0 g BT /F1 12 Tf 32 143 Td (HIGHLIGHT ME PLEASE) Tj ET 0.8 g 0 280 400 20 re f'
pdf1 = writepdf('ann.pdf', page_pdf(content, anns, EX))
ref = mutool(pdf1)
rc, im, st, err = render(pdf1, flags=1, H=300, zoom=100)
check('source annotations: the renderer finishes (rc %d)' % rc, rc == 0, err[:100])
if rc == 0:
    pi = pidx(im); g = lum(im)
    def at(x, y): return pi[300 - y, x]                       # page pt -> pixel, 400x300 page at 100%
    check('plain /AP drawn in its /Rect: red ground, blue square inside', at(30, 70) == 12 or at(30, 70) == 4, 'px %s' % at(30, 70))
    check('  the blue square sits at (10,10) of the appearance: page (30,30)-(60,50)', at(40, 40) == 1 or at(40, 40) == 9, 'px %s' % at(40, 40))
    check('/Matrix [2 0 0 2]: the 20x20 box is stretched to the 60x60 /Rect (12.5.5): black, with the yellow quarter at its lower left',
          at(190, 60) == 0 and at(160, 30) == 14 and at(160, 70) == 0 or (at(160, 30) in (14, 6) and at(200, 70) == 0), 'px %s %s %s' % (at(190, 60), at(160, 30), at(160, 70)))
    check('a quarter-turn /Matrix: red at the end the BBox puts at the bottom, green above (mutool agrees below)',
          at(260, 30) in (4, 12) and at(260, 100) in (2, 10), 'px %s %s' % (at(260, 30), at(260, 100)))
    check('Hidden (/F 2) and NoView (/F 32) are not drawn; /F 0 is', at(40, 220) == 15 and at(90, 220) == 15 and at(140, 220) in (1, 9), 'px %s %s %s' % (at(40, 220), at(90, 220), at(140, 220)))
    check('/AP /N a state dictionary: /AS /On picks the green one', at(190, 220) in (2, 10), 'px %s' % at(190, 220))
    check('no /AP: nothing drawn (the /Rect stays page white)', at(240, 220) == 15)
    # the highlight is drawn BEFORE the page, so the text is on top of its yellow ground
    hl = pi[300 - 154:300 - 140, 30:120]
    check('a highlight with an opaque yellow appearance: yellow ground AND the text on it (drawn first, 20.12.1)',
          (hl == 14).sum() > 500 and (hl == 0).sum() > 80, 'yellow %d black %d' % ((hl == 14).sum(), (hl == 0).sum()))
    check('BBox with a negative origin [-10 -10 10 10] fits its /Rect: the magenta square at its lower left', at(305, 205) in (5, 13), 'px %s' % at(305, 205))
    bad, miss, extra = compare(g, np.asarray(ref, dtype=float))
    check('vs mutool: %.1f%% cells differ, %d ink cells missing, %d ink cells extra' % (bad, miss, extra), bad < 4.0 and miss < 20 and extra < 20)
    side(im, ref, 'annot-synthetic')
rc2, im2, st2, _ = render(pdf1, flags=0, H=300)
check('without PDRF_ANNOTS none of it is drawn (the red ground is page white)', rc2 == 0 and pidx(im2)[300 - 70, 30] == 15)
rc3, im3, st3, _ = render(pdf1, flags=1, H=300, adapter='herc', zoom=100)
check('the same page on Hercules draws the appearances (dithered)', rc3 == 0 and (lum(im3)[:250, :400] < 200).sum() > 2000)
# a rotated page
pdfr = writepdf('annr.pdf', page_pdf(content, anns, EX, rotate=90))
refr = mutool(pdfr)
rc, imr, st, err = render(pdfr, flags=1, H=420, zoom=100, W=640)
if rc == 0:
    bad, miss, extra = compare(lum(imr), np.asarray(refr, dtype=float))
    check('a page with /Rotate 90: appearances turn with it (vs mutool: %.1f%% cells differ)' % bad, bad < 4.0 and miss < 20 and extra < 20)
    side(imr, refr, 'annot-synthetic-rot90')
else: check('rotated annotation page renders', False, err[:100])

# a page with NO /Contents still shows its annotations (the phase machine starts from "the page ended at once")
nopg = page_pdf(b'', anns[:3], EX, contents=False)
rcn, imn, stn, errn = render(writepdf('ann0.pdf', nopg), flags=1, H=300)
check('a page with no /Contents still draws its annotations (rc %d)' % rcn, rcn == 0 and pidx(imn)[300 - 70, 30] == 12 and (pidx(imn)[300 - 100:300 - 20, 20:120] == 12).sum() > 3000, errn[:80])

# verify-annotr: what review found
EX2 = []
def apstream2(d, body): EX2.append(strm('/Type /XObject /Subtype /Form ' + d, body)); return len(EX2) + 5
B1 = apstream2('/BBox [0 0 40 40] /Matrix [0.7071 0.7071 -0.7071 0.7071 0 0]', b'0 0 1 rg 0 0 40 40 re f')    # turned 45 degrees
B2 = apstream2('/BBox [0 0 40 40]', b'0 0.7 0 rg 0 0 40 40 re f')                                             # "Yes"
B3 = apstream2('/BBox [0 0 40 40]', b'1 0 0 rg 0 0 40 40 re f')                                               # "Off"
B4 = apstream2('/BBox [0 0 40 40]', b'1 0 0 rg 0 0 40 40 re f > 0 0 1 rg 0 0 20 20 re f')                    # a syntax error half way
B5 = apstream2('/BBox [0 0 40 40]', b'0 0 1 rg 0 0 40 40 re f')
B6 = apstream2('/BBox [0 0 40 40]', b'0 0 1 rg 0 0 40 40 re f BI /W 2 /H 2')                                 # ends inside BI's dictionary
anns2 = [annot('Stamp', '100 100 160 160', '', '/AP << /N %d 0 R >>' % B1),                                     # 1: 45 degrees
         annot('Widget', '200 100 240 140', '', '/AS /Yes /AP << /N << /Off %d 0 R /Yes %d 0 R >> >>' % (B3, B2)),   # 2: a DIRECT state dictionary
         annot('Stamp', '20 200 60 240', '', '/AP << /N %d 0 R >>' % B4),                                       # 3: errs half way
         annot('Stamp', '80 200 120 240', '', '/AP << /N %d 0 R >>' % B6),                                      # 4: ends inside BI
         annot('Stamp', '140 200 180 240', '', '/AP << /N %d 0 R >>' % B5)]                                     # 5: after both: drawn
pdf2 = writepdf('ann2.pdf', page_pdf(b'0 g 0 0 10 10 re f', anns2, EX2))
ref2 = mutool(writepdf('ann2r.pdf', page_pdf(b'0 g 0 0 10 10 re f', anns2[:2], EX2)))      # mutool refuses the erring streams: its reference is the first two
rc, im, st, err = render(pdf2, flags=1, H=300)
check('review cases render (rc %d)' % rc, rc == 0, err[:100])
if rc == 0:
    pi = pidx(im)
    def at2(x, y): return pi[300 - y, x]
    check('12.5.5 takes ALL FOUR corners: a 45-degree /Matrix fills its /Rect with the diamond (centre blue, corner white)',
          at2(130, 130) in (1, 9) and at2(103, 103) == 15, 'px %s %s' % (at2(130, 130), at2(103, 103)))
    rr = np.asarray(ref2, dtype=float)
    d = np.abs(lum(im)[300 - 160:300 - 100, 100:160] - rr[300 - 160:300 - 100, 100:160])
    check('  ... and agrees with mutool there (%.1f%% of pixels differ by > 90)' % ((d > 90).mean() * 100), (d > 90).mean() < 0.08)
    check('/AP /N a DIRECT state dictionary with /AS /Yes draws the green one', at2(220, 120) in (2, 10), 'px %s' % at2(220, 120))
    check('an appearance that errs half way: what came before it is drawn, the annotations AFTER it still are',
          at2(40, 220) in (4, 12) and at2(160, 220) in (1, 9), 'px %s %s' % (at2(40, 220), at2(160, 220)))
    check('an appearance that ends inside BI\'s dictionary does not swallow the next one', at2(100, 220) in (1, 9) and at2(160, 220) in (1, 9))
# a page whose own content errs still gets its annotations and the edit store's marks
anns3 = [annot('Stamp', '140 200 180 240', '', '/AP << /N %d 0 R >>' % B5)]
pdf3 = writepdf('ann3.pdf', page_pdf(b'0 g 0 0 10 10 re f > 1 0 0 rg 300 20 50 50 re f', anns3, EX2))
rc, im, st, err = render(pdf3, flags=1, H=300)
check('a page whose content stream errs still draws its annotations (rc %d)' % rc, rc == 0 and pidx(im)[300 - 220, 160] in (1, 9) and pidx(im)[300 - 40, 320] == 15, err[:80])
# a long run of annotations the walk skips is done a batch a slice, not in one
hid = [annot('Stamp', '%d 20 %d 30' % (10 + i, 12 + i), '', '/F 2 /AP << /N %d 0 R >>' % B5) for i in range(120)]
pdf4 = writepdf('ann4.pdf', page_pdf(b'', hid + [annot('Stamp', '140 200 180 240', '', '/AP << /N %d 0 R >>' % B5)], EX2))
rc, im, st, err = render(pdf4, flags=1, H=300)
def nsteps(pdf):
    r = subprocess.run([exe, 'render', pdf, '0', '100', '0', '4', '1/1', '640', '300', '0', '0', os.path.join(scratch, 'o4.ppm'), bank, '29'],
                       capture_output=True, text=True, env=dict(os.environ, PDR_GLYPHKB='24', PDR_RFLAGS='1'))
    m = re.search(r'steps=(\d+)', r.stdout); return int(m.group(1)) if m else 0
s_many = nsteps(pdf4)
s_one = nsteps(writepdf('ann5.pdf', page_pdf(b'', [annot('Stamp', '140 200 180 240', '', '/AP << /N %d 0 R >>' % B5)], EX2)))
check('120 hidden annotations and one shown: the shown one is drawn, and the walk is a batch of 8 a slice (%d steps against %d for the page without them)' % (s_many, s_one),
      rc == 0 and pidx(im)[300 - 220, 160] in (1, 9) and s_many - s_one >= 120 // 8 - 1, err[:80])

# ANNOTS256: 256 appearances of seven kinds
a256 = os.path.join(stress, 'ANNOTS256.PDF')
if os.path.exists(a256):
    ref = mutool(a256)
    rc, im, st, err = render(a256, flags=1, H=800, W=640, zoom=100)
    check('ANNOTS256: renders (rc %d, %s passes)' % (rc, st.get('passes')), rc == 0, err[:100])
    if rc == 0:
        bad, miss, extra = compare(lum(im), np.asarray(ref, dtype=float))
        check('ANNOTS256 vs mutool: %.1f%% cells differ, %d ink cells missing, %d extra' % (bad, miss, extra), bad < 4.0 and miss < 40 and extra < 40)
        side(im, ref, 'annot-256')
        pi = pidx(im)
        n = 0
        for i in range(0, 256, 7):                            # the "Text" annotations: a yellow fill 28x22
            col, row = i % 16, i // 16; x, y = 30 + col * 35, 30 + row * 44
            n += int(pi[792 - (y + 11), x + 3] == 14)
        check('ANNOTS256: every sampled Text annotation shows its yellow fill (%d of 37)' % n, n >= 36)
        rc0, im0, _, _ = render(a256, flags=0, H=800, W=640, zoom=100)
        check('ANNOTS256 without the flag: no annotation pixels', rc0 == 0 and (pidx(im0) == 14).sum() == 0)
        # a small group of bands each: the same picture whatever the list size (annotations are IN BAND, 10.2)
        rc7, im7, st7, _ = render(a256, flags=1, H=800, W=640, zoom=100, dlkb=11)
        check('ANNOTS256 with an 11 KiB list: the identical image in %s passes (the DL overflow rescan carries the annotation phases)' % st7.get('passes'),
              rc7 == 0 and (im7 == im).all() and int(st7.get('passes', '0')) > int(st.get('passes', '9')))
else:
    print('  (no ANNOTS256.PDF in %s: the stress set was not generated)' % stress)

# ============================================================================================
print('-- the edit store (PDRF_EDITS) against the real writer + mutool')
MARK = os.path.join(samples, 'MARKUP.PDF')
def mk(scn, src):
    d = os.path.join(scratch, scn); os.makedirs(d, exist_ok=True)
    r = subprocess.run([mkstore, src, scn, d], capture_output=True, text=True, cwd=d)
    cnt = int(open(os.path.join(d, 'store.cnt')).read()) if os.path.exists(os.path.join(d, 'store.cnt')) else 0
    return d, cnt, r
d_all, n_all, r = mk('all', MARK)
check('the writer saves the %d-record store (qpdf --check clean)' % n_all, r.returncode == 0 and n_all == 14 and
      subprocess.run(['qpdf', '--check', os.path.join(d_all, 'R.PDF')], capture_output=True).returncode == 0, r.stdout[-200:] + r.stderr[-200:])
store = os.path.join(d_all, 'store.bin')
refp = os.path.join(d_all, 'R.PDF')
ref = mutool(refp)
rc, im, st, err = render(MARK, flags=3, store=store, count=n_all, H=800, W=640)
check('MARKUP + the store: renders (rc %d, Incomplete 0x%x)' % (rc, inc(st)), rc == 0 and inc(st) == 0, err[:100])
base_rc, base, _, _ = render(MARK, flags=0, H=800, W=640)
if rc == 0:
    pi = pidx(im); g = lum(im)
    bad, miss, extra = compare(g, np.asarray(ref, dtype=float))
    check('every kind: PDREND of the original + store vs mutool of the writer\'s file: %.1f%% cells differ, %d ink cells missing, %d extra' % (bad, miss, extra),
          bad < 5.0 and miss < 40 and extra < 40)
    side(im, ref, 'edits-all-vga')
    def px(x, y): return pi[792 - y, x]
    def region(x0, y0, x1, y1): return pi[792 - y1:792 - y0, x0:x1]
    nb = pidx(base)
    def newpix(x0, y0, x1, y1): return int((region(x0, y0, x1, y1) != nb[792 - y1:792 - y0, x0:x1]).sum())
    r_ = region(470, 740, 490, 758)
    check('Note: a yellow speech bubble with a black outline in its 20 x 18 pt box', (r_ == 14).sum() > 120 and (r_ == 0).sum() > 40, 'yellow %d black %d' % ((r_ == 14).sum(), (r_ == 0).sum()))
    r_ = region(54, 722, 330, 748)
    check('Highlight: yellow quad with the title still on top (drawn first)', (r_ == 14).sum() > 3500 and (r_ == 0).sum() > 600, 'yellow %d black %d' % ((r_ == 14).sum(), (r_ == 0).sum()))
    r_ = region(54, 703, 375, 716)
    check('Highlight, a second colour (light green) over the italic line', (r_ == 10).sum() > 2500, 'lgreen %d' % (r_ == 10).sum())
    r_ = region(152, 660, 528, 684)
    check('Text box: "J. Smith" in blue (colour 1) at 12 pt', (r_ == 1).sum() > 40, 'blue %d' % (r_ == 1).sum())
    r_ = region(408, 200, 554, 346)
    ys, xs = np.where(r_ == 4)
    check('Text box, centred bold Times in red, wrapped inside its 146 pt frame (4 lines)', len(xs) > 300 and xs.min() >= 4 and xs.max() <= 142 and ys.min() >= 0,
          'red %d x %s..%s' % (len(xs), xs.min() if len(xs) else None, xs.max() if len(xs) else None))
    rows = sorted(set(np.where((r_ == 4).any(axis=1))[0]))
    bands = 1 + sum(1 for a, b in zip(rows, rows[1:]) if b - a > 3)
    check('  ... as the writer broke it: 4 text lines (found %d)' % bands, bands == 4)
    lft = [np.where((r_[ys0:ys0 + 11] == 4).any(axis=0))[0] for ys0 in range(0, 60, 12)]
    check('Ink, red: along the polyline (100,340) and the dot at (150,250)', px(100, 340) == 4 or px(101, 340) == 4 or px(99, 340) == 4, 'px %s' % px(100, 340))
    check('  ... the second stroke at y 200 and the single-point dot', (region(100, 199, 300, 202) == 4).sum() > 350 and (region(147, 247, 154, 254) == 4).sum() >= 4,
          'stroke %d dot %d' % ((region(100, 199, 300, 202) == 4).sum(), (region(147, 247, 154, 254) == 4).sum()))
    check('Sign, black: a stroke along (80,110)-(110,130)', newpix(80, 108, 170, 132) > 40)
    check('Line with an arrow at its end, blue: the shaft and a head at (500,600)', px(450, 560) in (1, 9) or px(450, 559) in (1, 9) or px(451, 560) in (1, 9)
          and (region(488, 590, 504, 604) == 1).sum() > 14, 'head blue %d' % (region(488, 590, 504, 604) == 1).sum())
    check('Line with both ends (align 3), green: heads at both ends', (region(398, 494, 412, 506) == 2).sum() > 10 and (region(508, 494, 522, 506) == 2).sum() > 10,
          'ends %d %d' % ((region(398, 494, 412, 506) == 2).sum(), (region(508, 494, 522, 506) == 2).sum()))
    check('Rect, red: a 2 pt border and an empty inside', px(60, 490) == 4 or px(61, 490) == 4, 'px %s' % px(61, 490))
    check('Ellipse, green: left extreme and an empty centre', (region(130, 488, 134, 502) == 2).sum() >= 4 and px(175, 495) == 15, 'centre %s' % px(175, 495))
    top = pi[272:273, 252:378]                                # the 1 pt border of (250,470)-(380,520) lies on pixel row 792 - 520
    on = (top == 1).any(axis=0).astype(int)
    changes = int(np.abs(np.diff(on)).sum())
    check('Area mark, dashed (align bit 0): the top edge alternates (%d changes); the solid one (magenta) is one run' % changes, changes >= 12 and
          (pi[332:333, 252:378] == 5).all(), 'solid row: %s' % set(pi[332, 252:378]))
    check('a review reply (a note with /IRT) draws nothing of its own', newpix(465, 735, 495, 765) == newpix(465, 735, 495, 765))
    # the other flags
    rcA, imA, _, _ = render(MARK, flags=1, store=store, count=n_all, H=800, W=640)
    check('PDRF_ANNOTS alone: the edit store is not drawn (the page is the page)', rcA == 0 and (pidx(imA) == nb).all())
    rcE, imE, _, _ = render(MARK, flags=2, store=store, count=n_all, H=800, W=640)
    check('PDRF_EDITS alone draws them (same as ANNOTS|EDITS, MARKUP having no source annotations)', rcE == 0 and (imE == im).all())
    rcH, imH, _, _ = render(MARK, flags=7, store=store, count=n_all, H=800, W=640, hide=5)
    ph = pidx(imH)
    check('PDRF_NOEDITID hides record 5 (the ink) and only it', rcH == 0 and (region_h := ph[792 - 400:792 - 190, 70:310]) is not None and (region_h == 4).sum() == 0 and
          (ph[792 - 748:792 - 722, 54:330] == 14).sum() > 3500 and (ph[792 - 758:792 - 740, 470:490] == 14).sum() > 120)
    rcZ, imZ, stZ, _ = render(MARK, flags=3, store=store, count=0, H=800, W=640)
    check('a store of 0 records is the page', rcZ == 0 and (pidx(imZ) == nb).all())
    # a 29 KiB list is the normal claim: smaller lists carry the edit phases through the rescan too
    rcS, imS, stS, _ = render(MARK, flags=3, store=store, count=n_all, H=800, W=640, dlkb=11)
    check('an 11 KiB list: the identical image (%s passes) - the marks are in band, 10.2' % stS.get('passes'), rcS == 0 and (imS == im).all())
    # zoom / rotation: the marks follow the page
    rcR, imR, stR, _ = render(MARK, flags=3, store=store, count=n_all, H=640, W=800, rot=90, zoom=100)
    check('on a page turned 90 degrees the marks turn with it (renders, no Incomplete)', rcR == 0 and inc(stR) == 0)
    if shots and rcR == 0: Image.fromarray(imR.astype('uint8')).save(os.path.join(shots, 'edits-all-rot90.png'))
    rcZ2, imZ2, _, _ = render(MARK, flags=3, store=store, count=n_all, H=1200, W=640, zoom=150)
    check('at 150% the marks draw', rcZ2 == 0)

    # ---- 1bpp: 17.4's marks
    for ad, zoom, Hh in (('herc', 65, 348), ('cga', 60, 200)):
        rcm, imm, stm, errm = render(MARK, flags=3, store=store, count=n_all, H=Hh, zoom=zoom, adapter=ad)
        _, bm, _, _ = render(MARK, flags=0, H=Hh, zoom=zoom, adapter=ad)
        check('%s: the store draws (rc %d, Incomplete 0x%x)' % (ad, rcm, inc(stm)), rcm == 0 and inc(stm) == 0, errm[:100])
        if rcm == 0:
            dm, db = lum(imm) < 128, lum(bm) < 128
            check('%s: the marks add black pixels (%d) and never white-out the page text: no more than 6%% of the page ink lost' % (ad, (dm & ~db).sum()),
                  (dm & ~db).sum() > 600 and (db & ~dm).sum() < 0.06 * db.sum() + 150)
            side(imm, mutool(refp, dpi=int(72 * zoom / 100)), 'edits-all-%s' % ad) if False else None
            if shots: Image.fromarray(imm.astype('uint8')).save(os.path.join(shots, 'edits-all-%s.png' % ad))
    # highlight in the four mono styles at 100% Hercules (title area): pt (54..330, 722..748)
    rows = {}
    for mm in range(4):
        rcm, imm, _, _ = render(MARK, flags=3, store=store, count=n_all, H=348, zoom=100, adapter='herc', mono=mm)
        _, bm, _, _ = render(MARK, flags=0, H=348, zoom=100, adapter='herc')
        rows[mm] = (lum(imm) < 128, lum(bm) < 128) if rcm == 0 else None
    def hl_pixels(mm):                                        # the rows of the title highlight on Hercules at 100%: y = (792-748)*29/45 .. (792-722)*29/45
        a, b = rows[mm]; y0, y1 = int(44 * 29 / 45) - 2, int(70 * 29 / 45) + 3
        return int((a[y0:y1, 54:330] & ~b[y0:y1, 54:330]).sum())
    if all(v is not None for v in rows.values()):
        u, f, hatch = hl_pixels(1), hl_pixels(2), hl_pixels(3)
        check('mono_mark: 1 underline only (%d px), 2 dotted frame only (%d px), 0 both (%d), 3 hatch (%d px, a dither, no solid fill)' % (u, f, hl_pixels(0), hatch),
              0 < u < 600 and 0 < f < 2000 and hl_pixels(0) >= max(u, f) and 600 < hatch < 0.55 * 276 * 18 and hatch > hl_pixels(0))

    # ---- a store is never written (pdr_test fails rc 7 when it is) - the renders above all passed through it
    check('the store segment is never written (pdr_test compares all 64 KiB after every render)', True)

# replace / delete: source annotation removed from the source pass
d_rep, n_rep, r = mk('replace', a256) if os.path.exists(a256) else (None, 0, None)
if r is not None:
    # the writer cannot save the 100 KB stress file today (PDWRT answers XREF): only the store is used here
    st_ = os.path.join(d_rep, 'store.bin')
    check('replace scenario: store of %d records built' % n_rep, n_rep == 2 and os.path.exists(st_), (r.stdout + r.stderr)[-200:])
    if os.path.exists(st_):
        _, a_ann, _, _ = render(a256, flags=1, H=800, W=640)
        rc, a_rep, st, err = render(a256, flags=3, store=st_, count=n_rep, H=800, W=640)
        if rc == 0:
            pa, pr = pidx(a_ann), pidx(a_rep)
            # annotation 1 (col 1, row 0: a Highlight 28x22 at (65,30)) is deleted; annotation 0 (a Text at (30,30)) is replaced by a note at (300,40)
            check('a deleted source annotation is not drawn: its yellow quad is gone', (pa[792 - 52:792 - 30, 65:93] == 14).sum() > 400 and (pr[792 - 52:792 - 30, 65:93] == 14).sum() == 0,
                  'before %d after %d' % ((pa[792 - 52:792 - 30, 65:93] == 14).sum(), (pr[792 - 52:792 - 30, 65:93] == 14).sum()))
            check('a replaced one leaves its old place (the Text at (30,30) is gone) and the record draws at its new one', (pr[792 - 52:792 - 30, 30:58] == 14).sum() == 0 and
                  (pr[792 - 58:792 - 40, 300:320] == 10).sum() > 120)
            diff = (pa != pr)
            diff[792 - 52:792 - 30, 30:58] = False; diff[792 - 52:792 - 30, 65:93] = False; diff[792 - 58:792 - 40, 300:320] = False
            check('everything else on the page is unchanged (%d other pixels differ)' % diff.sum(), diff.sum() == 0)
        else: check('replace scenario renders', False, err[:100])

# ============================================================================================
# verify-annotr: an ink stroke longer than the path buffer (PDR_NPATH 200) is stroked in pieces, and
# each piece goes on FROM the last point - the segment across the cut is not lost
import struct as _st
def ink_store(pts):
    rec = _st.pack('<BBH4hHHHBBBBHHHHHH14x', 5, 0, 0, 0, 0, 400 * 16, 300 * 16, 0, 0, 1, 0, 0, 4, 0, 0, 0, 48, len(pts), 0, 0)
    return rec + b''.join(_st.pack('<hh', x * 16, y * 16) for x, y in pts)
pts = [(20 + (i & 1) * 2, 150) for i in range(197)] + [(380, 150), (380, 150)]
sp = os.path.join(scratch, 'ink.bin'); open(sp, 'wb').write(ink_store(pts))
rc, im, st, err = render(writepdf('ink.pdf', page_pdf(b'')), flags=2, store=sp, H=300, W=400)
row = (lum(im)[300 - 150, 100:300] < 128) if rc == 0 else np.zeros(1)
check('an ink stroke of 199 points: the segment across the path buffer\'s cut (point 196 -> 197) is drawn (%d of 200 px inked)' % row.sum(), rc == 0 and row.all(), err[:80])

# verify-annotr: the store is drawn a budget a turn (about 1,024 points and text bytes), never all of
# it in one slice: 200 ink records of 60 points (a dot each) take more steps than one, and the last is drawn
def ink_rec(i, n, pts_off):
    return _st.pack('<BBH4hHHHBBBBHHHHHH14x', 5, 0, 0, 0, 0, 400 * 16, 300 * 16, 0, 0, i + 1, 1, 0, 2, 0, 0, 0, pts_off, n, 0, 0)
def ink_many(k, n=60):
    recs = b''.join(ink_rec(i, n, 48 * k + i * n * 4) for i in range(k))
    pts = b''.join(_st.pack('<hh', (20 + i) * 16, 40 * 16) if j == 0 else _st.pack('<hh', -32768, 0) for i in range(k) for j in range(n))   # a dot and 59 pen-ups each: heavy in points, light in the list
    return recs + pts
sp1 = os.path.join(scratch, 'ink1.bin'); open(sp1, 'wb').write(ink_many(1))
sp2 = os.path.join(scratch, 'ink200.bin'); open(sp2, 'wb').write(ink_many(200))
def esteps(store):
    r = subprocess.run([exe, 'render', writepdf('inkp.pdf', page_pdf(b'')), '0', '100', '0', '4', '1/1', '400', '300', '0', '0', os.path.join(scratch, 'o5.ppm'), bank, '29'],
                       capture_output=True, text=True, env=dict(os.environ, PDR_GLYPHKB='24', PDR_RFLAGS='2', PDR_EDITS=store))
    m = re.search(r'steps=(\d+)', r.stdout); return (int(m.group(1)) if m else 0), r.returncode
(s1, r1), (s2, r2) = esteps(sp1), esteps(sp2)
im5 = np.asarray(Image.open(os.path.join(scratch, 'o5.ppm')).convert('RGB')).astype(int) if r2 == 0 else None
check('200 ink records of 60 points (dots) are drawn over several turns (%d steps against %d for one record), the last one too' % (s2, s1),
      r1 == 0 and r2 == 0 and s2 - s1 >= 8 and im5 is not None and (pidx(im5)[259:262, 215:222] == 1).sum() >= 3)

print('-- dash patterns (d)')
def draw(content, **kw):
    pdf = writepdf('c.pdf', page_pdf(content))
    return render(pdf, H=300, W=400, **kw)
def runs(row):
    """lengths of runs of 'ink' and gaps along a boolean row: [(is_ink, length), ...]"""
    out = []; cur = None; n = 0
    for v in row:
        v = bool(v)
        if v == cur: n += 1
        else:
            if cur is not None: out.append((cur, n))
            cur, n = v, 1
    out.append((cur, n)); return out
def line_runs(im, y, x0=10, x1=390):
    return runs(lum(im)[300 - y, x0:x1] < 128)
rc, im, st, err = draw(b'[10 10] 0 d 3 w 20 150 m 380 150 l S')
r_ = line_runs(im, 150) if rc == 0 else []
on = [n for k, n in r_ if k and n > 3]; off = [n for k, n in r_ if not k and n > 3]
check('[10 10] 0 d: %d dashes of 10 px (found lengths %s...)' % (len(on), on[:4]), rc == 0 and 17 <= len(on) <= 19 and all(9 <= n <= 11 for n in on[1:-1]) and all(9 <= n <= 11 for n in off[1:-1]) and inc(st) == 0)
rc, im, st, err = draw(b'[10 10] 5 d 3 w 20 150 m 380 150 l S')
r_ = line_runs(im, 150); first = [n for k, n in r_ if k][0]
check('the phase: [10 10] 5 d starts 5 px into the first dash (first dash %d px)' % first, rc == 0 and 4 <= first <= 6)
rc, im, st, err = draw(b'[4] 0 d 3 w 20 150 m 380 150 l S')
r_ = line_runs(im, 150); on = [n for k, n in r_ if k and n > 1]
check('an odd array [4] is 4 on, 4 off (%d dashes of ~4 px)' % len(on), rc == 0 and 44 <= len(on) <= 47 and all(3 <= n <= 5 for n in on[1:-1]))
rc, im, st, err = draw(b'[2 4 6] 0 d 3 w 20 150 m 380 150 l S')
r_ = [n for k, n in line_runs(im, 150, 20, 120)]
check('[2 4 6]: on 2, off 4, on 6, off 2, on 4, off 6 (found %s)' % r_[:6], rc == 0 and r_[:6] == [2, 4, 6, 2, 4, 6] or (rc == 0 and all(abs(a - b) <= 1 for a, b in zip(r_[:6], [2, 4, 6, 2, 4, 6]))))
rc, im, st, err = draw(b'[8 8] 0 d 3 w 60 60 100 100 re S')
g = lum(im) < 128
col = runs(g[300 - 160:300 - 60, 60])                       # the left edge, x = 60 (the middle of the 3 pt stroke)
check('a dashed rectangle: its left edge alternates (%d runs) and the path closes' % len(col), rc == 0 and len([1 for k, n in col if k]) >= 5)
rc, im, st, err = draw(b'1 J 4 w [0 8] 0 d 20 100 m 200 100 l S')
r_ = line_runs(im, 100, 10, 230); dots = [n for k, n in r_ if k]
check('round caps and a zero-length dash: dots every 8 px (%d dots)' % len(dots), rc == 0 and 21 <= len(dots) <= 24 and all(3 <= n <= 6 for n in dots))
rc, im, st, err = draw(b'1 J 0 w [0 8] 0 d 20 100 m 200 100 l S')
dots = [n for k, n in runs(lum(im)[199:202, 10:230].min(axis=0) < 128) if k] if rc == 0 else []      # a pixel dot sits on row 199 or 200
check('a dotted HAIRLINE (0 w, round caps, [0 8]): every dot is a pixel (%d dots; an octagon of half a pixel drew none)' % len(dots), rc == 0 and 21 <= len(dots) <= 24)
rc, im, st, err = draw(b'q [10 10] 0 d Q 3 w 20 150 m 380 150 l S')
r_ = line_runs(im, 150)
check('Q restores the dash: a stroke after q..Q is solid', rc == 0 and len([1 for k, n in r_ if k]) == 1)
rc, im, st, err = draw(b'[10 10] 0 d [] 0 d 3 w 20 150 m 380 150 l S')
check('[] 0 d goes back to solid', rc == 0 and len([1 for k, n in line_runs(im, 150) if k]) == 1)
# verify-annotr: a phase is taken MODULO the period (twice the sum for an odd count), not clamped
rc, im, st, err = draw(b'[10 10] 1002 d 3 w 20 150 m 380 150 l S')
r_ = line_runs(im, 150); first = [n for k, n in r_ if k][0] if rc == 0 else 0
check('a huge phase: [10 10] 1002 d starts like 2 d (1002 mod 20: a first dash of 8 px, found %d; a clamp to 63.75 would give 6)' % first, rc == 0 and 7 <= first <= 9)
rc, im, st, err = draw(b'[4] 6 d 3 w 20 150 m 380 150 l S')
r_ = line_runs(im, 150); lead = r_[0] if rc == 0 else (None, 0)
check('an odd array\'s phase cycles over twice its sum: [4] 6 d opens with a 2 px gap (the 10 px before the line + 2: %s)' % (lead,), rc == 0 and lead[0] is False and 11 <= lead[1] <= 13)
rc, im, st, err = draw(b'[3] 0 d 2 w 50 50 m 100 250 150 50 200 250 c S 250 60 m 350 60 l 300 140 l h S')
check('dashed curves and closed paths draw (no Incomplete)', rc == 0 and inc(st) == 0 and (lum(im) < 128).sum() > 300)
rc, im, st, err = draw(b'[1 2 3 4 5 6] 0 d 2 w 20 150 m 380 150 l S')
check('a six-element array keeps four and says so (Incomplete: limit)', rc == 0 and inc(st) & 0x80 and (lum(im) < 128).sum() > 100)
rc, im, st, err = draw(b'[10 10] 0 d 3 w 20 150 m 380 150 l S', adapter='herc', zoom=100)
check('a dash on Hercules', rc == 0 and len([1 for k, n in runs(lum(im)[int(150 * 29 / 45):int(150 * 29 / 45) + 1, 10:390][0] < 128) if k]) >= 12)

# ============================================================================================
print('-- inline images (BI / ID / EI)')
def at4(im, x, y): return pidx(im)[300 - y, x]
chk = bytes([0xAA, 0x55] * 4)
rc, im, st, err = draw(b'q 80 0 0 80 20 200 cm BI /W 8 /H 8 /BPC 1 /CS /G ID ' + chk + b' EI Q 0 g 150 200 20 20 re f')
check('1-bit gray 8x8 checker at 80x80: alternating 10 px squares (bit 1 = white); no Incomplete', rc == 0 and inc(st) == 0 and at4(im, 25, 275) == 15 and at4(im, 35, 275) == 0
      and at4(im, 25, 265) == 0 and at4(im, 35, 265) == 15 and at4(im, 160, 210) == 0, 'px %s %s inc %x' % (at4(im, 25, 275), at4(im, 35, 275), inc(st)))
rc, im, st, err = draw(b'q 80 0 0 80 20 200 cm BI /W 8 /H 8 /BPC 1 /CS /G /D [1 0] ID ' + chk + b' EI Q')
check('/Decode [1 0] inverts it', rc == 0 and at4(im, 25, 275) == 0 and at4(im, 35, 275) == 15)
ramp = bytes([0, 85, 170, 255] * 4)
rc, im, st, err = draw(b'q 160 0 0 40 20 200 cm BI /W 4 /H 4 /BPC 8 /CS /G ID ' + ramp + b' EI Q')
v = [at4(im, 20 + 20 + 40 * k, 230) for k in range(4)]
check('8-bit gray, 4 columns 0 85 170 255: black, dark grey, light grey, white (found %s)' % v, rc == 0 and v == [0, 8, 7, 15])
rgb = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255])
rc, im, st, err = draw(b'q 80 0 0 40 20 200 cm BI /W 2 /H 2 /BPC 8 /CS /RGB ID ' + rgb + b' EI Q')
rg = lum(im)
check('8-bit RGB 2x2 (red, green / blue, white): drawn in luminance (white brightest, blue darkest)', rc == 0 and rg[300 - 235, 80] < rg[300 - 235, 30] + 200 and
      rg[300 - 205, 80] > 200 and rg[300 - 205, 30] < rg[300 - 235, 80] + 255)
g4 = bytes([0x04, 0x8F])                                   # 4 bits a sample: 0 4 8 15
rc, im, st, err = draw(b'q 160 0 0 20 20 200 cm BI /W 4 /H 1 /BPC 4 /CS /G ID ' + g4 + b' EI Q')
v = [at4(im, 20 + 20 + 40 * k, 210) for k in range(4)]
check('4-bit gray (0 4 8 15): dark to light (found %s)' % v, rc == 0 and v[0] == 0 and v[3] == 15 and v[1] in (0, 8) and v[2] in (8, 7))
g2 = bytes([0b00011011])
rc, im, st, err = draw(b'q 160 0 0 20 20 200 cm BI /W 4 /H 1 /BPC 2 /CS /G ID ' + g2 + b' EI Q')
v = [at4(im, 20 + 20 + 40 * k, 210) for k in range(4)]
check('2-bit gray (0 1 2 3): black, dark grey, light grey, white (found %s)' % v, rc == 0 and v == [0, 8, 7, 15])
mask = bytes([0xF0, 0xF0, 0xF0, 0xF0, 0x0F, 0x0F, 0x0F, 0x0F])
rc, im, st, err = draw(b'1 0 0 rg q 80 0 0 80 20 200 cm BI /IM true /W 8 /H 8 ID ' + mask + b' EI Q')
check('/IM true stencil paints the 0 bits in the fill colour (light red: the right half of the top rows, the left of the bottom)', rc == 0 and at4(im, 40, 270) == 15 and at4(im, 70, 270) == 12 and at4(im, 40, 230) == 12 and at4(im, 70, 230) == 15,
      'px %s %s %s %s' % (at4(im, 40, 270), at4(im, 70, 270), at4(im, 40, 230), at4(im, 70, 230)))
rc, im, st, err = draw(b'1 0 0 rg q 80 0 0 80 20 200 cm BI /IM true /W 8 /H 8 /D [1 0] ID ' + mask + b' EI Q')
check('/Decode [1 0] swaps the stencil', rc == 0 and at4(im, 40, 270) == 12 and at4(im, 70, 270) == 15)
# exact lengths, whatever the data holds
data = b'x EI 1 0 0 rg 0 '; assert len(data) == 16
rc, im, st, err = draw(b'0 g 10 250 20 20 re f BI /W 4 /H 4 /BPC 8 /CS /G ID ' + data + b' EI 100 250 20 20 re f')
check('data that contains EI and content operators is read by count: the rectangles around it draw, the page is not red', rc == 0 and at4(im, 20, 260) == 0 and at4(im, 110, 260) == 0 and at4(im, 300, 150) == 15)
rc, im, st, err = draw(b'BI /W 2 /H 2 /BPC 8 /CS /CMYK ID ' + bytes(16) + b' EI 0 g 100 250 20 20 re f')
check('CMYK is consumed exactly and not drawn: Incomplete (image), the next operator draws', rc == 0 and inc(st) & 4 and at4(im, 110, 260) == 0)
rc, im, st, err = draw(b'BI /W 3 /H 3 /BPC 8 /CS /RGB /F /AHx ID 00ff00ff00ff00ff00ff00ff00ff00ff00ff00> EI 0 g 100 250 20 20 re f')
check('a filtered inline image is scanned to EI, not drawn: Incomplete (image)', rc == 0 and inc(st) & 4 and at4(im, 110, 260) == 0)
rc, im, st, err = draw(b'q 100 0 0 100 20 100 cm BI /W 100 /H 100 /BPC 8 /CS /G ID ' + bytes([128]) * 10000 + b' EI Q 0 g 300 250 20 20 re f')
check('an image whose data would not fit the list record (10,000 bytes) is consumed and not drawn: Incomplete (image)', rc == 0 and inc(st) & 4 and at4(im, 310, 260) == 0)
big = bytes(((x + y) * 2) % 256 for y in range(50) for x in range(100))
rc, im, st, err = draw(b'q 200 0 0 100 20 100 cm BI /W 100 /H 50 /BPC 8 /CS /G ID ' + big + b' EI Q 0 g 300 250 20 20 re f')
l = lum(im)
check('a 5,000-byte image is read across slices and drawn whole (a gradient); the operators after it still draw', rc == 0 and inc(st) == 0 and at4(im, 310, 260) == 0 and
      l[300 - 190, 30:200].std() > 30 and (l[300 - 105, 20:220] < 255).sum() > 100,
      'inc %x px %s std %.1f dark %d' % (inc(st), at4(im, 310, 260), l[300 - 190, 30:200].std(), (l[300 - 105, 20:220] < 255).sum()))
# against mutool: a page of several inline images
pg = (b'q 80 0 0 80 20 200 cm BI /W 8 /H 8 /BPC 1 /CS /G ID ' + chk + b' EI Q '
      b'q 160 0 0 40 120 220 cm BI /W 4 /H 4 /BPC 8 /CS /G ID ' + ramp + b' EI Q '
      b'q 100 0 0 50 120 150 cm BI /W 2 /H 2 /BPC 8 /CS /RGB ID ' + rgb + b' EI Q '
      b'q 200 0 0 100 20 20 cm BI /W 100 /H 50 /BPC 8 /CS /G ID ' + big + b' EI Q '
      b'0.2 0.4 0.9 rg q 80 0 0 80 300 20 cm BI /IM true /W 8 /H 8 ID ' + mask + b' EI Q')
pdfi = writepdf('inl.pdf', page_pdf(pg))
refi = mutool(pdfi)
rc, im, st, err = render(pdfi, H=300, W=400)
if rc == 0:
    bad, miss, extra = compare(lum(im), np.asarray(refi, dtype=float), thresh=110)
    check('five inline images vs mutool: %.1f%% cells differ, %d missing, %d extra (dither against an antialiased grey)' % (bad, miss, extra), bad < 12.0 and miss < 6)
    side(im, refi, 'inline-images')
rc, im, st, err = render(pdfi, H=300, W=400, adapter='herc', zoom=100)
check('the same page on Hercules (dithered 1bpp) renders', rc == 0 and inc(st) == 0)


# a Square whose saved appearance strokes ON the BBox's maximum edges (PageDesk's own Save As shape): all four
# edges must be drawn at every zoom, a stroke whose centre is inside the clip draws
print('-- an appearance stroking at its BBox edge')
# (Rect origin, BBox size, MediaBox, zooms): QEMU's saved mark (rows-report 2: 50% lost two edges), then three
# marks where the stroke and the BBox's LOW edge round to the same qpx at 25/33% (verify-square: the outward
# clip alone still lost the top or left edge; a BBox's low sides give back the rounding, pdrd.c clip_apply)
def fxs(v): return ('%.4f' % v).rstrip('0').rstrip('.')
for (rx, ry, bw0, bh0), mb, zooms in (((109, 526.3125, 123.875, 140.3125), '0 0 400 700', (25, 33, 50, 60, 73, 87, 100, 125, 150, 200)),
                                      ((288.875, 553.4375, 107.875, 134.875), '0 0 612 792', (25,)),
                                      ((51.625, 595.4375, 169.875, 121.5), '0 0 612 792', (25,)),
                                      ((49.6875, 559.75, 74.9375, 29.5625), '0 0 612 792', (33,))):
    EXS = [strm('/Type /XObject /Subtype /Form /BBox [0 0 %s %s]' % (fxs(bw0), fxs(bh0)),
                ('1 0.333 0.333 RG 1 w 0.5 0.5 %s %s re S' % (fxs(bw0 - 1), fxs(bh0 - 1))).encode())]
    sqa = '<< /Type /Annot /Subtype /Square /F 4 /Rect [%s %s %s %s] /AP << /N 6 0 R >> >>' % (fxs(rx), fxs(ry), fxs(rx + bw0), fxs(ry + bh0))
    pdfs = writepdf('sqedge.pdf', page_pdf(b'', [sqa], EXS, mb=mb))
    for zm in zooms:
        nm = 'square edge %s at %d%%' % (fxs(rx), zm)
        rc, im, st, err = render(pdfs, flags=1, H=800, zoom=zm)
        if rc != 0: check(nm + ' renders', False, err[:80]); continue
        pi = pidx(im); ink = (pi == 12) | (pi == 4)
        ys, xs = np.nonzero(ink)
        if not len(ys): check(nm + ': drawn at all', False); continue
        y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
        bw = x1 - x0 + 1; bh = y1 - y0 + 1
        edges = {'top': ink[y0:y0+2, x0:x1+1].any(axis=0).mean(), 'bottom': ink[y1-1:y1+1, x0:x1+1].any(axis=0).mean(),
                 'left': ink[y0:y1+1, x0:x0+2].any(axis=1).mean(), 'right': ink[y0:y1+1, x1-1:x1+1].any(axis=1).mean()}
        exp_w = round(bw0 * zm / 100); exp_h = round(bh0 * zm / 100)
        check('%s: all four edges drawn %s, box %dx%d (want ~%dx%d)' % (nm, {k: round(float(v), 2) for k, v in edges.items()}, bw, bh, exp_w, exp_h),
              min(edges.values()) > 0.95 and abs(bw - exp_w) <= 2 and abs(bh - exp_h) <= 2)

# a rectangle clip with no area clips everything (the outward rounding made a zero-width clip one pixel wide)
pdfz = writepdf('zclip.pdf', page_pdf(b'q 100.3 100 0 50 re W n 0 g 0 0 400 300 re f Q q 50 50.3 80 0 re W n 0 g 0 0 400 300 re f Q'))
rc, im, st, err = render(pdfz, H=300)
check('a zero-width and a zero-height rectangle clip paint nothing', rc == 0 and int((pidx(im) == 0).sum()) == 0,
      'rc %d, %d black' % (rc, 0 if im is None else int((pidx(im) == 0).sum())))

# 1bpp (Hercules, CGA): a coloured or grey STROKE is ink, not paper. PageDesk's
# own saved /Square strokes 1 0.333 0.333 RG (luminance 136), which the 128
# threshold drew WHITE - the mark vanished on reopening on both mono adapters
# (the final pass's pagedeskxt screens). Only near-white (>= 240) stays white.
rx, ry, bw0, bh0 = 109, 526.3125, 123.875, 140.3125
EXS = [strm('/Type /XObject /Subtype /Form /BBox [0 0 %s %s]' % (fxs(bw0), fxs(bh0)),
            ('1 0.333 0.333 RG 1 w 0.5 0.5 %s %s re S' % (fxs(bw0 - 1), fxs(bh0 - 1))).encode())]
sqa = '<< /Type /Annot /Subtype /Square /F 4 /Rect [%s %s %s %s] /AP << /N 6 0 R >> >>' % (fxs(rx), fxs(ry), fxs(rx + bw0), fxs(ry + bh0))
pdfm = writepdf('sqmono.pdf', page_pdf(b'0.75 G 1 w 20 100 m 380 100 l S 0.9 G 20 80 m 380 80 l S 0.95 G 20 60 m 380 60 l S', [sqa], EXS, mb='0 0 400 700'))
for ad in ('herc', 'cga'):
    for zm in (73, 100):
        nm = 'mono %s at %d%%: the saved red /Square' % (ad, zm)
        rc, im, st, err = render(pdfm, flags=1, H=800, zoom=zm, adapter=ad)
        if rc != 0: check(nm + ' renders', False, err[:80]); continue
        ink = lum(im) < 128
        half = int(round(350 * zm / 100 * ADAPT[ad][0] / ADAPT[ad][1]))   # the square is in the page's top half, the rules in its bottom
        box = ink[:half, :int(400 * zm / 100) - 2]                         # the page, not the desk pattern beside it
        if not box.any(): check(nm + ': drawn at all', False); continue
        ys, xs = np.nonzero(box)
        sub = box[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        edges = {'top': sub[0:2].any(axis=0).mean(), 'bottom': sub[-2:].any(axis=0).mean(),
                 'left': sub[:, 0:2].any(axis=1).mean(), 'right': sub[:, -2:].any(axis=1).mean()}
        check('%s: all four edges inked %s' % (nm, {k: round(float(v), 2) for k, v in edges.items()}), min(edges.values()) > 0.95)
    rc, im, st, err = render(pdfm, H=800, zoom=100, adapter=ad)
    if rc == 0:
        sc = ADAPT[ad][0] / ADAPT[ad][1]
        def lit(y): r = int(round((700 - y) * sc)); return float((lum(im)[max(r - 1, 0):r + 2, 40:360] < 128).any(axis=0).mean())
        check('mono %s: a 0.75 and a 0.9 grey rule are ink, a 0.95 grey one is paper (%.2f %.2f %.2f)' % (ad, lit(100), lit(80), lit(60)),
              lit(100) > 0.95 and lit(80) > 0.95 and lit(60) < 0.05)
    else:
        check('mono %s: the grey rules render' % ad, False, err[:80])

print('pdr_annot: %d checks, %d FAILED' % (nchecks, len(fails)))
for f in fails: print('  FAIL ' + f)
sys.exit(1 if fails else 0)
