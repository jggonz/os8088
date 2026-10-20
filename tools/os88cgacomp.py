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

C512 (SPEC.md 98.1.3.5) is the same model at 80-column TEXT (3D8h 09h) with
RGBI pixels, on either CARD: `render_rgbi(px, new)` takes any pixel colours,
`new=True` is composite_new.rs's new-CGA arm (IBM's 1985 output network,
where R, G and B reach the luma too), and `c512_palette(new)` is the colour
each of the 512 codes shows as a flat field.
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


def _newv(c, i, r, g, b):
    """composite_new.rs's new_cga! macro"""
    return (c / 0.72) * 0.29 + (i / 0.28) * 0.32 + (r / 0.28) * 0.1 + \
        (g / 0.28) * 0.22 + (b / 0.28) * 0.07


def _params(cgamode=0x1A, contrast=100.0, hue=0.0, sat=100.0,
            brightness=0.0, new=False):
    """composite_new.rs's recalculate(): (the 1,024-entry table, ri, rq, gi,
    gq, bi, bq) - the old CGA's, or with `new` the new one's"""
    if not new:
        min_v = CHROMA_MULTIPLEXER[0] + INTENSITY[0]
        max_v = CHROMA_MULTIPLEXER[255] + INTENSITY[3]
    else:
        i0, i3 = INTENSITY[0], INTENSITY[3]
        min_v = _newv(CHROMA_MULTIPLEXER[0], i0, i0, i0, i0)
        max_v = _newv(CHROMA_MULTIPLEXER[255], i3, i3, i3, i3)
    mc = 256.0 / (max_v - min_v)
    mb = -min_v * mc
    mode_hue = 14.0 if (cgamode & 3) == 1 else 4.0
    mc *= contrast / 100.0 * (1.2 if new else 1.0)     # new CGA: 120%
    mb += (brightness - (10.0 if new else 0.0)) * 5.0   # ...and -10
    ms = (4.35 if new else 2.9) * sat / 100.0           # ...and 150%
    table = np.zeros(1024, np.int64)
    for x in range(1024):
        phase, right, left = x & 3, (x >> 2) & 15, (x >> 6) & 15
        rc, lc = right, left
        if cgamode & 4:
            rc = (right & 8) | (7 if right & 7 else 0)
            lc = (left & 8) | (7 if left & 7 else 0)
        c = CHROMA_MULTIPLEXER[((lc & 7) << 5) | ((rc & 7) << 2) | phase]
        i = INTENSITY[(left >> 3) | ((right >> 2) & 2)]
        if not new:
            v = c + i
        else:
            r = INTENSITY[((left >> 2) & 1) | ((right >> 1) & 2)]
            g = INTENSITY[((left >> 1) & 1) | (right & 2)]
            b = INTENSITY[(left & 1) | ((right << 1) & 2)]
            v = _newv(c, i, r, g, b)
        table[x] = int(float(v) * mc + mb)          # `as i32` truncates
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


def params(cgamode=0x1A, new=False):
    k = (cgamode, bool(new))
    if k not in _P:
        _P[k] = _params(cgamode, new=bool(new))
    return _P[k]


def render(bits):
    """(h, w) of 0/1 hi-res pixels -> (h, w, 3) uint8 RGB, every row the way
    composite_process() does one scanline"""
    bits = np.asarray(bits, dtype=np.int64)
    return _render(bits * FG, params(), BORDER)


def render_rgbi(px, new=False, cgamode=0x09, border=0):
    """(h, w) of RGBI 0..15 hi-res pixels -> (h, w, 3) uint8 RGB, at the
    mode `cgamode` sets (80-column text by default) on the old or new card"""
    return _render(np.asarray(px, dtype=np.int64), params(cgamode, new),
                   border)


def _render(px, prm, border):
    table, ri, rq, gi, gq, bi, bq = prm
    h, w = px.shape
    t = np.zeros((h, w + 10), np.int64)
    bt = table[border * 68:border * 68 + 4]
    for x in range(4):
        t[:, x] = bt[(x + 3) & 3]
    t[:, 4] = table[(border << 6) | (px[:, 0] << 2) | 3]
    ph = np.arange(w - 1) & 3
    t[:, 5:w + 4] = table[(px[:, :-1] << 6) | (px[:, 1:] << 2) | ph]
    t[:, w + 4] = table[(px[:, -1] << 6) | (border << 2) | 3]
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


# --- C512: composite colour on the text hack (SPEC.md 98.1.3.5) -------------
# the two characters C512 uses and their top two glyph rows (the same byte
# in both), then the few a C512 screen can otherwise hold: 0 and space are
# a keyframe's black, and 0DEh is the full screen's text (98.3.12.2)
C512_CHARS = (0x13, 0x55)
GLYPH01 = {0x13: 0x66, 0x55: 0xCC, 0x00: 0x00, 0x20: 0x00, 0xDB: 0xFF,
           0xDE: 0x0F, 0xDD: 0xF0}
BURST_BORDER = 6                # 3D9h: IBM's 80-column burst (98.3.12.2)


def c512_code(k):
    """code k (0..511) -> (character, attribute): 13h for 0-255, 55h after"""
    return C512_CHARS[k >> 8], k & 255


def c512_cells(chars, attrs):
    """arrays of characters and attributes -> (..., 8) RGBI pixels, a
    cell's row: a glyph bit lit is the foreground (the low nibble)"""
    chars = np.asarray(chars, dtype=np.int64)
    attrs = np.asarray(attrs, dtype=np.int64)
    rows = np.vectorize(lambda c: GLYPH01.get(int(c), 0))(chars) \
        if chars.size else chars
    bits = (rows[..., None] >> (7 - np.arange(8))) & 1
    return np.where(bits == 1, (attrs & 15)[..., None], (attrs >> 4)[..., None])


_C512 = {}


def c512_palette(new=False):
    """(512, 3) float RGB: what each code shows as a flat field - a row of
    nine of its cells rendered at 80-column text, the fifth one averaged
    (a cell's pattern repeats every colour clock, so it is flat there)"""
    k = bool(new)
    if k not in _C512:
        codes = np.arange(512)
        ch = np.array([c512_code(c)[0] for c in codes])
        at = codes & 255
        row = np.tile(c512_cells(ch, at), (1, 9))
        out = render_rgbi(row, new=k).astype(float)
        _C512[k] = out[:, 32:40].mean(axis=1)
    return _C512[k]


def srgb2lab(rgb):
    """(..., 3) sRGB 0..255 -> CIELAB, D65"""
    c = np.asarray(rgb, dtype=float) / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722],
                  [0.0193, 0.1192, 0.9505]])
    xyz = c @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]),
                     200 * (f[..., 1] - f[..., 2])], -1)


def render_c512(cv, wb, h, new=False, border=BURST_BORDER):
    """a C512 canvas (h rows of wb bytes, character then attribute) ->
    (h, wb / 2 x 8, 3) RGB: one row a canvas row (two scan lines on the
    glass), what the old or new card's composite monitor shows"""
    a = np.frombuffer(bytes(cv), np.uint8).reshape(h, wb)
    px = c512_cells(a[:, 0::2], a[:, 1::2]).reshape(h, -1)
    return render_rgbi(px, new=new, border=border)


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
