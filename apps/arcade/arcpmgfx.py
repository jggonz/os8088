#!/usr/bin/env python3
# =============================================================================
# os8088 - apps/arcade/arcpmgfx.py - Pac-Man's ROM part (ARCADE-SPEC 7.7)
#
#     python3 apps/arcade/arcpmgfx.py PACMAN.ROM OUT.prt
#
# The board image tools/getarcrom.py wrote, followed by what the renderer
# would otherwise work out on the guest every time - all of it DERIVED from
# the ROM, so like the ROM it is never committed. The renderer composes in
# PLANES, never a byte a pixel (ARCADE-SPEC 7.7), so the pictures are kept
# as MASKS: for each row, which pixels have value 1, which 2, which 3.
#
#   0000  6220  the board image: program, tiles, sprites, the PROMs
#   6400  1800  the 256 tiles, TURNED to the portrait screen: 8 rows of three
#               bytes - the pixels of value 1, 2, 3 (bit 7 = the left one)
#   7C00  3000  the 64 sprites likewise, 16 rows of three WORDS (bit 15 =
#               the left pixel), twice: as drawn, then flipped left-right
#   AC00   200  the 32 palettes' plane SELECTORS: byte (v x 4 + p) is FFh
#               when value v's colour has bit p set, else 00h
#   AE00    80  the 32 palettes' OPACITY: byte v is FFh when value v's
#               colour is not 0 (MAME's transparent pen for a sprite)
#   AE80    30  the 16 colours as VGA DAC triples (6 bits each)
#   AF00   7E0  screen tile (ty, tx) -> its video RAM index, 36 x 28 words
#   B700   800  video RAM index -> the screen tile, (ty << 8) | tx, or FFFF
#               for the 16 the screen does not show
#
# "Turned": MAME draws the board in landscape and rotates it 90 degrees
# clockwise (ROT90), so a landscape pixel (x, y) lands at portrait column
# 223 - y, row x. A tile's portrait (row r, column c) is its landscape
# (x = r, y = 7 - c); a sprite's is (x = r, y = 15 - c). A sprite's x flip
# (bit 1) is then the rows' order, and its y flip (bit 0) the columns'.
# =============================================================================
import sys

img = open(sys.argv[1], 'rb').read()
if len(img) != 0x6220:
    sys.exit('arcpmgfx: %s is not the Pac-Man board image' % sys.argv[1])
out = bytearray(0xBF00)
out[0:0x6220] = img


def tile_px(code, x, y):
    b = img[0x4000 + code * 16 + (8 if x < 4 else 0) + y]
    bit = x & 3
    return (((b >> (7 - bit)) & 1) << 1) | ((b >> (3 - bit)) & 1)


def sprite_px(code, x, y):
    b = img[0x5000 + code * 64 + (8, 16, 24, 0)[x >> 2] + (y & 7) + (32 if y >= 8 else 0)]
    bit = x & 3
    return (((b >> (7 - bit)) & 1) << 1) | ((b >> (3 - bit)) & 1)


for code in range(256):
    for r in range(8):
        m = [0, 0, 0, 0]
        for c in range(8):
            m[tile_px(code, r, 7 - c)] |= 0x80 >> c
        k = 0x6400 + (code * 8 + r) * 3
        out[k:k + 3] = bytes(m[1:4])
for code in range(64):
    for flip in range(2):
        for r in range(16):
            m = [0, 0, 0, 0]
            for c in range(16):
                cc = 15 - c if flip else c
                m[sprite_px(code, r, 15 - cc)] |= 0x8000 >> c
            k = 0x7C00 + ((code * 2 + flip) * 16 + r) * 6
            for v in range(3):
                out[k + 2 * v:k + 2 * v + 2] = bytes([m[v + 1] & 255, m[v + 1] >> 8])
lut = img[0x6020:0x6120]
for pal in range(32):
    for v in range(4):
        col = lut[pal * 4 + v] & 15
        for p in range(4):
            out[0xAC00 + pal * 16 + v * 4 + p] = 0xFF if col & (1 << p) else 0
        out[0xAE00 + pal * 4 + v] = 0xFF if col else 0
for i in range(16):
    c = img[0x6000 + i]
    r = 0x21 * (c & 1) + 0x47 * ((c >> 1) & 1) + 0x97 * ((c >> 2) & 1)
    g = 0x21 * ((c >> 3) & 1) + 0x47 * ((c >> 4) & 1) + 0x97 * ((c >> 5) & 1)
    b = 0x51 * ((c >> 6) & 1) + 0xAE * ((c >> 7) & 1)
    out[0xAE80 + 3 * i:0xAE83 + 3 * i] = bytes([r >> 2, g >> 2, b >> 2])
rev = [0xFFFF] * 1024
for ty in range(36):
    for tx in range(28):
        row, col = 27 - tx, ty                  # MAME's landscape tilemap
        r, c = row + 2, (col - 2) & 0xFF
        offs = (r + ((c & 0x1F) << 5)) if (c & 0x20) else (c + (r << 5))
        k = 0xAF00 + 2 * (ty * 28 + tx)
        out[k:k + 2] = bytes([offs & 255, offs >> 8])
        rev[offs] = (ty << 8) | tx
for i, v in enumerate(rev):
    out[0xB700 + 2 * i:0xB702 + 2 * i] = bytes([v & 255, v >> 8])
open(sys.argv[2], 'wb').write(bytes(out))
print('arcpmgfx: %s -> %s (%d bytes)' % (sys.argv[1], sys.argv[2], len(out)))
