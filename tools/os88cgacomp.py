#!/usr/bin/env python3
"""os88cgacomp - what a CGA's 640 x 200 picture looks like on a COMPOSITE
monitor with the colour burst on (SPEC.md 98.2.2; the player's CGACOMP,
98.3.3).

    python3 tools/os88cgacomp.py --palette OUT.png     the 16 patterns
    python3 tools/os88cgacomp.py --render IN.pbm OUT.png

This is Andrew Jenner's (reenigne's) sampled chroma-multiplexer model, the
one MartyPC, 86Box and DOSBox all use - ported from MartyPC's
`marty_videocard_renderer/src/composite_new.rs` (UNLICENSE), at MartyPC's
default monitor settings (contrast, saturation and luma 1, hue 0, old CGA).
So what it renders is what those emulators show, pixel for pixel, and the
encoder can choose a frame's bits against it.

Mode register 1Ah (graphics, 640 wide, burst on) and the BIOS's colour
register (foreground 15): a lit pixel is RGBI 15, an unlit one 0, and the
border is 0.
"""
import argparse
import math
import sys

import numpy as np

CHROMA_MULTIPLEXER = np.array([
    2, 2, 2, 2, 114, 174, 4, 3, 2, 1, 133, 135, 2, 113, 150, 4,
    133, 2, 1, 99, 151, 152, 2, 1, 3, 2, 96, 136, 151, 152, 151, 152,
    2, 56, 62, 4, 111, 250, 118, 4, 0, 51, 207, 137, 1, 171, 209, 5,
    140, 50, 54, 100, 133, 202, 57, 4, 2, 50, 153, 149, 128, 198, 198, 135,
    32, 1, 36, 81, 147, 158, 1, 42, 33, 1, 210, 254, 34, 109, 169, 77,
    177, 2, 0, 165, 189, 154, 3, 44, 33, 0, 91, 197, 178, 142, 144, 192,
    4, 2, 61, 67, 117, 151, 112, 83, 4, 0, 249, 255, 3, 107, 249, 117,
    147, 1, 50, 162, 143, 141, 52, 54, 3, 0, 145, 206, 124, 123, 192, 193,
    72, 78, 2, 0, 159, 208, 4, 0, 53, 58, 164, 159, 37, 159, 171, 1,
    248, 117, 4, 98, 212, 218, 5, 2, 54, 59, 93, 121, 176, 181, 134, 130,
    1, 61, 31, 0, 160, 255, 34, 1, 1, 58, 197, 166, 0, 177, 194, 2,
    162, 111, 34, 96, 205, 253, 32, 1, 1, 57, 123, 125, 119, 188, 150, 112,
    78, 4, 0, 75, 166, 180, 20, 38, 78, 1, 143, 246, 42, 113, 156, 37,
    252, 4, 1, 188, 175, 129, 1, 37, 118, 4, 88, 249, 202, 150, 145, 200,
    61, 59, 60, 60, 228, 252, 117, 77, 60, 58, 248, 251, 81, 212, 254, 107,
    198, 59, 58, 169, 250, 251, 81, 80, 100, 58, 154, 250, 251, 252, 252,
    252], dtype=np.float64)
INTENSITY = (77.175381, 88.654656, 166.564623, 174.228438)
TAU = 6.28318531
FG = 15                         # the colour register's foreground
BORDER = 0


def _params(cgamode=0x1A, contrast=100.0, hue=0.0, sat=100.0,
            brightness=0.0):
    """composite_new.rs's recalculate(), old CGA: (the 1,024-entry table,
    ri, rq, gi, gq, bi, bq)"""
    min_v = CHROMA_MULTIPLEXER[0] + INTENSITY[0]
    max_v = CHROMA_MULTIPLEXER[255] + INTENSITY[3]
    mc = 256.0 / (max_v - min_v)
    mb = -min_v * mc
    mode_hue = 14.0 if (cgamode & 3) == 1 else 4.0
    mc *= contrast / 100.0
    mb += brightness * 5.0
    ms = 2.9 * sat / 100.0
    table = np.zeros(1024, np.int64)
    for x in range(1024):
        phase, right, left = x & 3, (x >> 2) & 15, (x >> 6) & 15
        rc, lc = right, left
        if cgamode & 4:
            rc = (right & 8) | (7 if right & 7 else 0)
            lc = (left & 8) | (7 if left & 7 else 0)
        c = CHROMA_MULTIPLEXER[((lc & 7) << 5) | ((rc & 7) << 2) | phase]
        i = INTENSITY[(left >> 3) | ((right >> 2) & 2)]
        table[x] = int(float(c + i) * mc + mb)      # `as i32` truncates
    i = float(table[6 * 68] - table[6 * 68 + 2])
    q = float(table[6 * 68 + 1] - table[6 * 68 + 3])
    a = TAU * (33.0 + 90.0 + hue + mode_hue) / 360.0
    c, s = math.cos(a), math.sin(a)
    r = 256.0 * ms / math.sqrt(i * i + q * q)
    iqi = -(i * c + q * s) * r
    iqq = (q * c - i * s) * r
    RI, RQ, GI, GQ, BI, BQ = 0.9563, 0.6210, -0.2721, -0.6474, -1.1069, \
        1.7046
    m = [int(RI * iqi + RQ * iqq), int(-RI * iqq + RQ * iqi),
         int(GI * iqi + GQ * iqq), int(-GI * iqq + GQ * iqi),
         int(BI * iqi + BQ * iqq), int(-BI * iqq + BQ * iqi)]
    return (table, *m)


_P = {}


def params():
    if not _P:
        _P["p"] = _params()
    return _P["p"]


def render(bits):
    """(h, w) of 0/1 hi-res pixels -> (h, w, 3) uint8 RGB, every row the way
    composite_process() does one scanline"""
    table, ri, rq, gi, gq, bi, bq = params()
    bits = np.asarray(bits, dtype=np.int64)
    h, w = bits.shape
    px = bits * FG
    t = np.zeros((h, w + 10), np.int64)
    bt = table[BORDER * 68:BORDER * 68 + 4]
    for x in range(4):
        t[:, x] = bt[(x + 3) & 3]
    t[:, 4] = table[(BORDER << 6) | (px[:, 0] << 2) | 3]
    ph = np.arange(w - 1) & 3
    t[:, 5:w + 4] = table[(px[:, :-1] << 6) | (px[:, 1:] << 2) | ph]
    t[:, w + 4] = table[(px[:, -1] << 6) | (BORDER << 2) | 3]
    for x in range(5):
        t[:, w + 5 + x] = bt[x & 3]
    xs = np.arange(w + 2)
    at = t[:, xs] - ((t[:, xs + 2] - t[:, xs + 4] + t[:, xs + 6]) << 1) + \
        t[:, xs + 8]
    bb = (t[:, xs + 1] - t[:, xs + 3] + t[:, xs + 5] - t[:, xs + 7]) << 1
    tt = t.copy()
    m = np.arange(4, w + 6)
    tt[:, m] = (t[:, m] << 3) - at[:, m - 4]
    k = np.arange(w)
    a, b = at[:, 1 + k], bb[:, 1 + k]
    c = tt[:, 5 + k] * 2
    d = tt[:, 4 + k] + tt[:, 6 + k]
    y = (c + d) << 8
    ph = k & 3                      # the four rotations of (a, b)
    A = np.where(ph == 0, a, np.where(ph == 1, -b, np.where(ph == 2, -a, b)))
    B = np.where(ph == 0, b, np.where(ph == 1, a, np.where(ph == 2, -b, -a)))
    out = np.empty((h, w, 3), np.uint8)
    for n, (ci, cq) in enumerate(((ri, rq), (gi, gq), (bi, bq))):
        out[:, :, n] = np.clip((y + ci * A + cq * B) >> 13, 0, 255)
    return out


def palette():
    """(16, 3) float: the colour a nibble shows repeated along a row - its
    four output pixels averaged, well inside a field of it"""
    rows = []
    for n in range(16):
        bits = [(n >> (3 - (x & 3))) & 1 for x in range(64)]
        rgb = render(np.array([bits]))[0, 24:40].astype(float)
        rows.append(rgb.mean(axis=0))
    return np.array(rows)


def cell_lut():
    """(16, 16, 16, 4, 3) float: the four output pixels of a nibble b set
    between a nibble a on its left and c on its right - the model rendered
    once for all 4,096, each in a row of seven cells (a a a b c c c), so a
    choice can be weighed by what it LOOKS like beside its neighbours and
    not by a flat field's colour"""
    a, b, c = np.meshgrid(np.arange(16), np.arange(16), np.arange(16),
                          indexing="ij")
    a, b, c = a.ravel(), b.ravel(), c.ravel()
    cells = np.stack([a, a, a, b, c, c, c], axis=1)
    bits = ((cells[:, :, None] >> (3 - np.arange(4))) & 1).reshape(len(a),
                                                                   28)
    out = render(bits).astype(float)
    return out[:, 12:16].reshape(16, 16, 16, 4, 3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--palette", metavar="OUT.png")
    ap.add_argument("--render", nargs=2, metavar=("IN", "OUT.png"))
    a = ap.parse_args()
    from PIL import Image
    if a.palette:
        p = palette()
        img = np.zeros((40, 16 * 40, 3), np.uint8)
        for n in range(16):
            img[:, n * 40:(n + 1) * 40] = p[n]
            print("%2d %s  %3d %3d %3d" % (n, format(n, "04b"), *p[n]))
        Image.fromarray(img).save(a.palette)
    if a.render:
        im = np.array(Image.open(a.render[0]).convert("1"), dtype=np.uint8)
        Image.fromarray(render(im)).save(a.render[1])
    return 0


if __name__ == "__main__":
    sys.exit(main())
