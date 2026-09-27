#!/usr/bin/env python3
"""The TEXT video format's picture of a character (SPEC.md 98.1.3.6).

A TEXT file (pixel format 8) is the 80 x 25 text screen as it is - a cell's
character, then its attribute - and what a cell LOOKS like is whatever the
machine's character generator draws for that code. Nothing on the host can
know that: a CGA draws 8 x 8 cells out of its own ROM, an MDA or a Hercules
9 x 14, an EGA 8 x 14 and a VGA 9 x 16, and a clone is free to redraw any of
them. So the encoder matches against a MODEL of a PC's 8 x 8 face, and this
module is that model:

  32..126    fonts/tallx.f8, the tree's own 8 x 8 face - clean-room, PC-shaped
             (two-pixel stems, seven columns of ink), and the same letters a
             `make FONT=tallx` kernel draws;
  the rest   tools/cp437font.py's CP437 glyphs, drawn by arithmetic - which
             for the shades and the half blocks the encoder leans on is not
             an approximation at all: a quarter, a half, three quarters, a
             whole cell, and its top, bottom, left or right half, on every
             ROM ever made.

The model is only ever the ENCODER's reference and the host's picture of a
file (a preview, `os88vid decode --png`). The player draws nothing: it writes
the codes and the card draws them. The encoder is built so a ROM that
differs from the model costs little - it compares cells through a blur as
well as dot for dot (os88venc's TextMatcher) - and the default glyph set
(`blocks`) is the part of CP437 that no ROM draws differently.

No numpy here: os88vid imports this for the Preview's reference, and the
player's gates run without it. `render` is numpy's and says so.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# the tree's face: tools/../fonts/, or beside this file in a bundle
# (tools/os88vbundle.py carries it as data, 98.2.13)
FACE = "tallx.f8"

SHADES = (0xB0, 0xB1, 0xB2, 0xDB)           # a quarter, a half, 3/4, all
HALVES = (0xDC, 0xDF, 0xDD, 0xDE)           # lower, upper, left, right
DOTS = (0x27, 0x2C, 0x2E, 0x60)           # ' , . `: a dot high or low
MOREDOTS = (0x22, 0x2A, 0x3A, 0x3B)       # " * : ;: a pair, a star, two
GLYPH_SETS = {
    "ascii": tuple(range(32, 127)),
    "shades": tuple(range(32, 127)) + SHADES,
    "blocks": tuple(range(32, 127)) + SHADES + HALVES,
    # the other classic style: solid blocks and half blocks for the shapes,
    # and the four dots for the edges and dithers a block is too coarse for
    "dots": (0x20, 0xDB) + HALVES + DOTS,
    # ...and the same with four more: a pair of dots high, a small star,
    # and the two stacked pairs - more marks to shade a middle tone with
    "dots-plus": (0x20, 0xDB) + HALVES + DOTS + MOREDOTS,
}
GLYPH_SET_DEFAULT = "blocks"

# The Preview's quarters of a cell (98.4.6): each 4 x 4 quadrant's lit dots,
# 0..16 - top-left, top-right, bottom-left, bottom-right. The player takes
# 32..126 off the machine's own glyphs (OSAPI_FONT_GLYPHS) and these off a
# table; any other code but 0 is half lit, which is what an unknown shape is
# on average. apps/video/video.asm's vp_tquad is this table
BLOCK_QUADS = {
    0x00: (0, 0, 0, 0),
    0xB0: (4, 4, 4, 4), 0xB1: (8, 8, 8, 8), 0xB2: (12, 12, 12, 12),
    0xDB: (16, 16, 16, 16),
    0xDC: (0, 0, 16, 16), 0xDF: (16, 16, 0, 0),
    0xDD: (16, 0, 16, 0), 0xDE: (0, 16, 0, 16),
}
UNKNOWN_QUADS = (8, 8, 8, 8)


def quads_of(rows):
    """Eight glyph rows (bit 7 leftmost) -> the four quadrants' lit dots"""
    out = []
    for qy in (0, 1):
        for qx in (0, 1):
            n = 0
            for r in rows[qy * 4:qy * 4 + 4]:
                nib = (r >> 4) if qx == 0 else (r & 15)
                n += bin(nib).count("1")
            out.append(n)
    return tuple(out)


def quads(code, ascii_font=None):
    """A code's four quadrants as the PLAYER reckons them: 32..126 from
    `ascii_font` ({code: 8 rows}, the machine's; the model's when None),
    the blocks and 0 from BLOCK_QUADS, anything else UNKNOWN_QUADS"""
    if 32 <= code <= 126:
        return quads_of((ascii_font or model())[code])
    return BLOCK_QUADS.get(code, UNKNOWN_QUADS)


_MODEL = None


def _face_path():
    for p in (os.path.join(HERE, FACE),
              os.path.join(os.path.dirname(HERE), "fonts", FACE)):
        if os.path.exists(p):
            return p
    raise FileNotFoundError("the text model's face, %s, is not beside %s "
                            "nor in fonts/" % (FACE, HERE))


def model():
    """{code: 8 rows} for every code 0..255: the model face described above.
    0 and 127 are blank"""
    global _MODEL
    if _MODEL is None:
        import os88font
        import cp437font
        g = {c: list(r) for c, r in os88font.parse(_face_path()).items()}
        for c in cp437font.CODES:
            g[c] = list(cp437font.glyph(c))
        g[0] = [0] * 8
        g[127] = [0] * 8
        _MODEL = g
    return _MODEL


def bitmaps(codes):
    """numpy (len(codes), 8, 8) float32 of 0/1: the model's glyphs"""
    import numpy as np
    m = model()
    rows = np.array([m[c] for c in codes], np.uint8)          # (n, 8)
    return np.unpackbits(rows[..., None], axis=2).astype(np.float32)


def render(cv, wb, h, rgb16):
    """numpy (h * 8, wb / 2 * 8, 3) uint8: a TEXT canvas (character, then
    attribute) drawn in the model face at 8 x 8 a cell, blink off - the
    screen a CGA shows, before its 5:12 dot is squared. `rgb16` is the
    sixteen colours, (16, 3)"""
    import numpy as np
    cells = wb // 2
    a = np.frombuffer(bytes(cv), np.uint8).reshape(h, cells, 2)
    ch, at = a[..., 0], a[..., 1]
    m = model()
    font = np.array([m[c] for c in range(256)], np.uint8)
    bits = np.unpackbits(font[..., None], axis=2).astype(bool)  # (256, 8, 8)
    g = bits[ch]                                        # (h, cells, 8, 8)
    fg = np.asarray(rgb16, np.uint8)[at & 15]           # (h, cells, 3)
    bg = np.asarray(rgb16, np.uint8)[at >> 4]
    px = np.where(g[..., None], fg[:, :, None, None, :],
                  bg[:, :, None, None, :])              # (h, cells, 8, 8, 3)
    return px.transpose(0, 2, 1, 3, 4).reshape(h * 8, cells * 8, 3)


if __name__ == "__main__":
    m = model()
    for name, codes in GLYPH_SETS.items():
        print("%-7s %3d glyphs" % (name, len(codes)))
    bad = [c for c in BLOCK_QUADS if c and quads_of(m[c]) != BLOCK_QUADS[c]]
    if bad:
        sys.exit("os88txtfont: the model's %s disagree with BLOCK_QUADS"
                 % ", ".join("%02Xh" % c for c in bad))
    print("os88txtfont: the model's blocks and shades are BLOCK_QUADS'")
