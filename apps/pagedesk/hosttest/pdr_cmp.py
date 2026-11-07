#!/usr/bin/env python3
"""Compare a harness rendering with mutool at the same scale (PAGEDESK-SPEC
20.3.6).  Usage: pdr_cmp.py ours.ppm pdf page zoom%% ox oy out.png
Reports the share of 8x8 cells whose ink coverage differs by more than 35
points (font and palette simplification make small differences normal) and
writes ours | reference | difference side by side."""
import sys, subprocess, tempfile, os
from PIL import Image
import numpy as np
ours, pdf, page, zoom, ox, oy, out = sys.argv[1:8]
zoom = int(zoom)
ref = tempfile.mktemp(suffix='.png')
subprocess.run(['mutool', 'draw', '-q', '-r', str(72 * zoom / 100), '-o', ref, pdf, str(int(page) + 1)], check=True)
r = Image.open(ref).convert('L'); o = Image.open(ours).convert('L')
w, h = r.size
o = o.crop((int(ox), int(oy), int(ox) + w, int(oy) + h))
a = np.asarray(o, dtype=float); b = np.asarray(r, dtype=float)
def cells(x, n=8):
    H, W = x.shape; H -= H % n; W -= W % n
    return x[:H, :W].reshape(H // n, n, W // n, n).mean(axis=(1, 3))
ca, cb = cells(a), cells(b)
bad = np.abs(ca - cb) > 90          # 35% of 255
inkb = (cb < 200)
print('cells=%d differing=%d (%.1f%%)  ref-ink cells missing in ours=%d' % (bad.size, bad.sum(), 100.0 * bad.mean(), int((inkb & (ca > 235)).sum())))
d = np.abs(a - b[:a.shape[0], :a.shape[1]]).clip(0, 255)
side = np.concatenate([a, np.full((a.shape[0], 4), 128), b[:a.shape[0], :a.shape[1]], np.full((a.shape[0], 4), 128), 255 - d], axis=1)
Image.fromarray(side.astype('uint8')).save(out)
os.unlink(ref)
