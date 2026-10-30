#!/usr/bin/env python3
"""mkbmp.py - the two bitmaps bitmap.rc binds (WIN16 fixture, S4/S8), and
the icon icons.rc binds (S12), written deterministically so `make
win16fixtures` needs no committed binary.

    python3 tests/win16/mkbmp.py OUTDIR

w16color.bmp is 16x16 at 4 bits a pixel in the standard Windows 16-colour
palette: the sixteen colours as a 4x4 grid of 4x4 squares, colour index =
row * 4 + column, so a misplaced palette entry or a nibble swap is visible
at a glance. w16mono.bmp is 16x16 at 1 bit: a checkerboard of 4x4 squares
with the top-left square white.

w16icon.ico is a 32x32 icon in the two images a Windows 3.0 icon carries, 16
colours and 2 (SPEC.md 124.18.4): a diamond, red edged and yellow inside,
with a blue square at its middle and a see-through hole in that, so its four
corners and its centre show what it is drawn on. The 2-colour image is the
same shape black on white.
"""
import os
import struct
import sys

# The Windows 3.x default 16-colour palette (index order of the VGA driver)
PAL = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128),
       (128, 0, 128), (0, 128, 128), (192, 192, 192), (128, 128, 128),
       (255, 0, 0), (0, 255, 0), (255, 255, 0), (0, 0, 255), (255, 0, 255),
       (0, 255, 255), (255, 255, 255)]


def bmp(w, h, bpp, pal, rows):
    """rows top-down, each a list of palette indices."""
    stride = ((w * bpp + 31) // 32) * 4
    pix = b""
    for row in reversed(rows):                  # a DIB is stored bottom-up
        bits, acc, n = bytearray(), 0, 0
        for v in row:
            acc = (acc << bpp) | v
            n += bpp
            if n == 8:
                bits.append(acc)
                acc, n = 0, 0
        pix += bytes(bits).ljust(stride, b"\0")
    ct = b"".join(struct.pack("<BBBB", b_, g, r, 0) for r, g, b_ in pal)
    off = 14 + 40 + len(ct)
    return (b"BM" + struct.pack("<IHHI", off + len(pix), 0, 0, off) +
            struct.pack("<IiiHHIIiiII", 40, w, h, 1, bpp, 0, len(pix), 0, 0,
                        len(pal), 0) + ct + pix)


def dib(w, h, bpp, pal, rows, mask):
    """an icon image: the header (height doubled), colours, XOR bits, AND
    mask; rows and mask top-down, mask 1 = see-through"""
    xs = ((w * bpp + 31) // 32) * 4
    ms = ((w + 31) // 32) * 4
    xor = b""
    for row in reversed(rows):
        bits, acc, n = bytearray(), 0, 0
        for v in row:
            acc = (acc << bpp) | v
            n += bpp
            if n == 8:
                bits.append(acc)
                acc, n = 0, 0
        xor += bytes(bits).ljust(xs, b"\0")
    andm = b""
    for row in reversed(mask):
        bits = bytearray((w + 7) // 8)
        for x, v in enumerate(row):
            if v:
                bits[x >> 3] |= 0x80 >> (x & 7)
        andm += bytes(bits).ljust(ms, b"\0")
    ct = b"".join(struct.pack("<BBBB", b_, g, r, 0) for r, g, b_ in pal)
    return (struct.pack("<IiiHHIIiiII", 40, w, 2 * h, 1, bpp, 0,
                        len(xor) + len(andm), 0, 0, 0, 0) + ct + xor + andm)


def diamond():
    """the diamond's 16-colour drawing, None where it is see-through: what
    tests/win16desk.py looks for on the glass"""
    rows16, _, mask = _diamond()
    return [[None if m else c for c, m in zip(r, k)] for r, k in zip(rows16, mask)]


def icon():
    """the diamond, as w16icon.ico: 16 colours, then 2"""
    rows16, rows2, mask = _diamond()
    imgs = [(16, 4, dib(32, 32, 4, PAL, rows16, mask)),
            (2, 1, dib(32, 32, 1, [(0, 0, 0), (255, 255, 255)], rows2, mask))]
    out = struct.pack("<HHH", 0, 1, len(imgs))
    off = 6 + 16 * len(imgs)
    for cc, bpp, img in imgs:
        out += struct.pack("<BBBBHHII", 32, 32, cc, 0, 1, bpp, len(img), off)
        off += len(img)
    return out + b"".join(img for _, _, img in imgs)


def _diamond():
    rows16, rows2, mask = [], [], []
    for y in range(32):
        r16, r2, m = [], [], []
        for x in range(32):
            d = abs(x - 15.5) + abs(y - 15.5)
            c = max(abs(x - 15.5), abs(y - 15.5))
            if d > 15.5 or c < 2.5:             # the corners, the hole
                r16.append(0); r2.append(0); m.append(1)
            elif d > 13.5:                      # the edge
                r16.append(1); r2.append(0); m.append(0)
            elif c < 6.5:                       # the square
                r16.append(12); r2.append(0); m.append(0)
            else:
                r16.append(11); r2.append(1); m.append(0)
        rows16.append(r16); rows2.append(r2); mask.append(m)
    return rows16, rows2, mask


def main(out):
    os.makedirs(out, exist_ok=True)
    colour = [[(y // 4) * 4 + x // 4 for x in range(16)] for y in range(16)]
    mono = [[1 - ((x // 4 + y // 4) & 1) for x in range(16)] for y in range(16)]
    with open(os.path.join(out, "w16color.bmp"), "wb") as f:
        f.write(bmp(16, 16, 4, PAL, colour))
    with open(os.path.join(out, "w16mono.bmp"), "wb") as f:
        f.write(bmp(16, 16, 1, [(0, 0, 0), (255, 255, 255)], mono))
    with open(os.path.join(out, "w16icon.ico"), "wb") as f:
        f.write(icon())


if __name__ == "__main__":
    main(sys.argv[1])
