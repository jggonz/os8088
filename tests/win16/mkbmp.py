#!/usr/bin/env python3
"""mkbmp.py - the two bitmaps bitmap.rc binds (WIN16 fixture, S4/S8),
written deterministically so `make win16fixtures` needs no committed binary.

    python3 tests/win16/mkbmp.py OUTDIR

w16color.bmp is 16x16 at 4 bits a pixel in the standard Windows 16-colour
palette: the sixteen colours as a 4x4 grid of 4x4 squares, colour index =
row * 4 + column, so a misplaced palette entry or a nibble swap is visible
at a glance. w16mono.bmp is 16x16 at 1 bit: a checkerboard of 4x4 squares
with the top-left square white.
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


def main(out):
    os.makedirs(out, exist_ok=True)
    colour = [[(y // 4) * 4 + x // 4 for x in range(16)] for y in range(16)]
    mono = [[1 - ((x // 4 + y // 4) & 1) for x in range(16)] for y in range(16)]
    with open(os.path.join(out, "w16color.bmp"), "wb") as f:
        f.write(bmp(16, 16, 4, PAL, colour))
    with open(os.path.join(out, "w16mono.bmp"), "wb") as f:
        f.write(bmp(16, 16, 1, [(0, 0, 0), (255, 255, 255)], mono))


if __name__ == "__main__":
    main(sys.argv[1])
