#!/usr/bin/env python3
"""PDREND against the REAL PDENG, over the committed samples (`make pdrend-real`).

  pdr_real.py <pdr_test_real> <scratch dir> <resource dir> <samples dir> [shots dir]

pdr_test_real is pdr_test.c linked with hosttest/pdr_hosteng.c and pdeng.c
(-DPDR_REAL_ENG): every verb PDREND calls is answered by PDENG's own C. Each
page of every sample is drawn as the three adapters see it - VGA 640 wide,
1/1 pixels, 4bpp; Hercules 720 wide, 29/45, 1bpp; CGA 640 wide, 5/12, 1bpp -
the whole page height, and compared with mutool at the same scale (mutool's
picture is resampled vertically by the pixel aspect, PAGEDESK-SPEC 5.3).

Per page it prints the share of 8x8 cells that differ by more than 90 grey
levels and the ink cells PDREND left white, and FAILS a page over the limits
(no cell comparison can see a font simplification, so the limits allow it).
Also asserted: the run is clean (no canary overrun, rc 0) and the Incomplete
bits equal the sample's documented capability (MANIFEST.TXT: a `supported`
file draws with no bit but the font one, which a non-base-14 name sets).

With a shots dir, writes <sample>-p<n>-<adapter>.png side by side with the
mutool reference (PDREND left, mutool right) for a human to LOOK at.
"""
import os, re, shutil, subprocess, sys

exe, scratch, resdir, samples = (os.path.abspath(a) for a in sys.argv[1:5])
shots = os.path.abspath(sys.argv[5]) if len(sys.argv) > 5 else None
try:
    from PIL import Image
    import numpy as np
except Exception as e:
    print('pdr_real: SKIP - Pillow/numpy missing (%s)' % e); sys.exit(0)
if not shutil.which('mutool'):
    print('pdr_real: SKIP - mutool missing'); sys.exit(0)
os.makedirs(scratch, exist_ok=True)
if shots: os.makedirs(shots, exist_ok=True)
res = os.path.join(resdir, 'PDRES.BIN'); resx = os.path.join(resdir, 'PDFONTX.RES')
bank = os.path.join(scratch, 'fd'); os.makedirs(bank, exist_ok=True)
data = open(res, 'rb').read()
open(os.path.join(bank, 'PDRES.BIN'), 'wb').write(data)
open(os.path.join(bank, 'PAGEDESK.O88'), 'wb').write(b'\x55' * 1536 + data)
if os.path.exists(resx): shutil.copy(resx, os.path.join(bank, 'PDFONTX.RES'))

man = open(os.path.join(samples, 'MANIFEST.TXT'), encoding='latin-1').read()
cap = dict(re.findall(r'^file (\S+)\n(?:  .*\n)*?  capability (\S+)', man, re.M))
pages = {f: int(n) for f, n in re.findall(r'^file (\S+)\n(?:  .*\n)*?  pages (\d+)', man, re.M)}
#          adapter: (aspect num, den, width, bpp)
ADAPT = {'vga': (1, 1, 640, 4), 'herc': (29, 45, 720, 1), 'cga': (5, 12, 640, 1)}
fails, n = [], 0

def cells(x, k=8):
    H, W = x.shape; H -= H % k; W -= W % k
    return x[:H, :W].reshape(H // k, k, W // k, k).mean(axis=(1, 3))

for f in sorted(cap):
    pdf = os.path.join(samples, f)
    np_ = min(pages.get(f, 1), 4)
    for page in range(np_):
        ref = os.path.join(scratch, 'ref.png')
        r = subprocess.run(['mutool', 'draw', '-q', '-r', '72', '-o', ref, pdf, str(page + 1)], capture_output=True)
        if r.returncode: fails.append('%s p%d: mutool failed' % (f, page)); continue
        b0 = Image.open(ref).convert('L')
        for an, (num, den, W, bpp) in ADAPT.items():
            zoom = 100
            # the page at zoom 100 is 1 px a point across; down the page, num/den of that
            Hh = int(b0.size[1] * num / den) + 2
            out = os.path.join(scratch, 'o.ppm')
            r = subprocess.run([str(x) for x in (exe, 'render', pdf, page, zoom, 0, bpp, '%d/%d' % (num, den), W, min(Hh, 1000), 0, 0, out, bank, 29)],
                               capture_output=True, text=True, env=dict(os.environ, PDR_GLYPHKB='24'))
            st = dict(kv.split('=', 1) for l in r.stdout.split('\n') if l.startswith('status') for kv in l.split()[1:] if '=' in kv)
            n += 1
            if r.returncode == 3 and cap[f] == 'refused':
                print('  ok   %-12s p%d %-4s refused cleanly (%s), as MANIFEST.TXT says' % (f, page, an, r.stderr.strip()[:40])); continue
            if r.returncode:
                fails.append('%s p%d %s: rc=%d %s' % (f, page, an, r.returncode, r.stderr.strip()[:160])); continue
            inc = int(st.get('incomplete', '0x0'), 16)
            b = np.asarray(b0.resize((b0.size[0], max(1, int(b0.size[1] * num / den))), Image.BOX), dtype=float)
            a = np.asarray(Image.open(out).convert('L'), dtype=float)[:b.shape[0], :b.shape[1]]
            b = b[:a.shape[0], :a.shape[1]]
            if bpp == 1:                               # a 1bpp band dithers grey: compare at the cell scale only
                pass
            ca, cb = cells(a), cells(b)
            bad = (np.abs(ca - cb) > 90).mean() * 100
            miss = int(((cb < 160) & (ca > 235)).sum())
            msg = '%-12s p%d %-4s inc=0x%04x %-28s %5.1f%% cells differ, %4d ink cells missing' % (
                f, page, an, inc, "'%s'" % st.get('first', '').strip("'")[:26], bad, miss)
            supported = cap[f] == 'supported'
            lim_bad, lim_miss = (6.0, 60) if supported else (100.0, 100000)
            # a supported file draws with no Incomplete bit but the font one - and the dash
            # one, while dash patterns are wave 2 (PAGEDESK-SPEC 20.3.7): said, not hidden
            allowed = 0x0001 | (0x0040 if 'dash' in st.get('first', '') else 0)
            okinc = (inc & ~allowed) == 0 if supported else True
            if bad > lim_bad or miss > lim_miss or not okinc:
                fails.append(msg); print('  FAIL ' + msg)
            else:
                print('  ok   ' + msg)
            if shots:
                A = Image.open(out).convert('RGB').crop((0, 0, int(b.shape[1]), int(b.shape[0])))
                B = Image.fromarray(b.astype('uint8')).convert('RGB')
                S = Image.new('RGB', (A.size[0] + B.size[0] + 8, max(A.size[1], B.size[1])), (255, 0, 255))
                S.paste(A, (0, 0)); S.paste(B, (A.size[0] + 8, 0))
                S.save(os.path.join(shots, '%s-p%d-%s.png' % (f.split('.')[0], page, an)))
# ---- contract checks through the real engine: what the fixes of verify-rend are about ----------
def mkpdf(content, font=None):
    """a one-page 400x300 PDF; F1 is Helvetica unless `font` gives its dictionary"""
    objs = ["<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Count 1 /Kids [5 0 R] >>",
            font or "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
            b"<< /Length %d >>\nstream\n" % len(content) + content + b"\nendstream",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] /Contents 4 0 R /Resources << /Font << /F1 3 0 R >> >> >>"]
    out = bytearray(b"%PDF-1.4\n"); offs = []
    for i, o in enumerate(objs):
        offs.append(len(out)); out += b"%d 0 obj\n" % (i + 1) + (o if isinstance(o, bytes) else o.encode("latin-1")) + b"\nendobj\n"
    x = len(out); out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
    for o in offs: out += b"%010d 00000 n \n" % o
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, x)
    return bytes(out)
def draw(content, font=None):
    p = os.path.join(scratch, "c.pdf"); open(p, "wb").write(mkpdf(content, font)); out = os.path.join(scratch, "c.ppm")
    r = subprocess.run([exe, "render", p, "0", "100", "0", "4", "1/1", "400", "300", "0", "0", out, bank, "29"],
                       capture_output=True, text=True, env=dict(os.environ, PDR_GLYPHKB="24"))
    st = dict(kv.split("=", 1) for l in r.stdout.split("\n") if l.startswith("status") for kv in l.split()[1:] if "=" in kv)
    return r.returncode, np.asarray(Image.open(out).convert("RGB")).astype(int) if r.returncode == 0 else None, st
def check(name, cond, why=""):
    global n
    n += 1
    if cond: print("  ok   " + name)
    else: fails.append("check: " + name + " " + why)
def dark(im, x0, y0, x1, y1): return int((im[y0:y1, x0:x1].sum(axis=2) < 200).sum())   # page y is 300 - y
# 1. an inline image whose data holds " EI " is read EXACTLY (W x H bytes): the rectangles either side draw
# (the data is itself content: read wrongly, it paints the page red; /DP 7 is a key whose number
# value must not swallow /H, which a byte scan for EI would then stop inside the data)
data = b" EI 1 0 0 rg 0 0 400 300 re f".ljust(32); assert len(data) == 32
rc, im, st = draw(b"0 g 10 250 20 20 re f BI /W 8 /DP 7 /H 4 /BPC 8 /CS /G ID " + data +
                  b" EI 100 250 20 20 re f BI /IM true /W 16 /H 2 ID " + bytes(4) + b" EI 200 250 20 20 re f")
check("inline images got past exactly (data containing EI; an /IM mask): every rectangle after them draws",
      rc == 0 and dark(im, 12, 32, 28, 48) > 200 and dark(im, 102, 32, 118, 48) > 200 and dark(im, 202, 32, 218, 48) > 200
      and im[150, 300].min() > 200
      and int(st.get("incomplete", "0"), 16) & 0x04, "rc=%d st=%r" % (rc, st))
# 2. 40 q's past the 16-deep stack and 40 Q's: the colour after them is the one before them (red, not blue)
rc, im, st = draw(b"q 1 0 0 rg " + b"q " * 40 + b"0 0 1 rg " + b"Q " * 40 + b"10 10 40 40 re f Q 60 10 40 40 re f")
px = im[300 - 30, 30] if rc == 0 else None; px2 = im[300 - 30, 80] if rc == 0 else None
check("q/Q stay balanced past the graphics-state depth: red after the 40 pairs, black after the outer Q",
      rc == 0 and px[0] > 150 and px[2] < 100 and px2.sum() < 100 and int(st.get("incomplete", "0"), 16) & 0x80, "rc=%d px=%r %r" % (rc, px, px2))
# 3. a TJ of numbers only moves the pen: (X) lands 60 pt right of the Td
rc, im, st = draw(b"BT /F1 12 Tf 10 100 Td [-5000] TJ (X) Tj ET")
check("a TJ with no string still moves the pen ([-5000] at 12 pt = 60 pt)",
      rc == 0 and dark(im, 0, 185, 60, 202) == 0 and dark(im, 68, 185, 82, 202) > 10, "rc=%d" % rc)
# 4. control codes advance by /Widths: two codes 1 at 1000 units each put A 24 pt right at 12 pt
rc, im, st = draw(b"BT /F1 12 Tf 10 100 Td (\001\001A) Tj ET",
                  "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /FirstChar 0 /LastChar 65 /Widths [%s] >>" % " ".join(["1000"] * 66))
check("codes below 32 advance by their /Widths entry (two at 1000 = 24 pt)",
      rc == 0 and dark(im, 0, 185, 32, 202) == 0 and dark(im, 33, 185, 46, 202) > 10, "rc=%d" % rc)
# 5. text far larger than any strike is drawn at its size (a scaled strike), not at 24 px
rc, im, st = draw(b"BT /F1 120 Tf 20 100 Td (H) Tj ET")
ys = np.where((im[:, :, 0] < 100).any(axis=1))[0] if rc == 0 else []
check("120 pt text is ~86 px tall (a scaled strike), not a 24 px one", rc == 0 and len(ys) and 70 < ys.max() - ys.min() < 100,
      "rc=%d height=%s" % (rc, (ys.max() - ys.min()) if len(ys) else None))

# 6. polygon clips are a pool for the PASS: a later W must not change what an earlier fill was cut by
rc, im, st = draw(b"q 0 0 m 100 0 l 50 100 l h W n 0 g 0 0 400 300 re f Q "
                  b"q 200 0 m 300 0 l 250 100 l h W n 0 g 0 0 400 300 re f Q")
check("two triangle clips in turn: each fill is cut by its own triangle (both draw)",
      rc == 0 and dark(im, 40, 260, 60, 290) > 300 and dark(im, 240, 260, 260, 290) > 300
      and dark(im, 140, 260, 160, 290) == 0, "rc=%d left=%s right=%s" % (rc, rc == 0 and dark(im, 40, 260, 60, 290), rc == 0 and dark(im, 240, 260, 260, 290)))

rc, im, st = draw(b"q 0 0 m 200 0 l 100 200 l h W n q 100 0 m 300 0 l 200 200 l h W n 0 g 0 0 400 300 re f Q Q 0 g 350 0 20 20 re f")
check("nested triangle clips intersect (the chain), and a fill after both Q's is unclipped",
      rc == 0 and dark(im, 145, 270, 155, 280) == 100 and dark(im, 45, 270, 55, 280) == 0 and dark(im, 245, 270, 255, 280) == 0
      and dark(im, 352, 282, 368, 298) > 200, "rc=%d" % rc)

for f in fails:
    if not f.startswith(tuple(cap)) or 'cells differ' not in f: print('  FAIL ' + f)
print('pdr_real: %d renders, %d FAILED' % (n, len(fails)))
sys.exit(1 if fails else 0)
