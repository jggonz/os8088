#!/usr/bin/env python3
# =============================================================================
# os8088 - apps/arcade/arcqbgfx.py - Q*bert's two parts (ARCADE-SPEC 10)
#
#     python3 apps/arcade/arcqbgfx.py QBERT.ROM qbert.patch QBROM.PRT QBSPR.PRT
#
# From the board image tools/getarcrom.py wrote (the program at 0000h, the
# tiles at 6000h, the sprites' four planes from 8000h), everything the host
# would otherwise work out on the guest - DERIVED from the ROM, so like the
# ROM it is never committed.
#
# QBROM.PRT, the ROM part:
#   0000  6000  the program, A000-FFFFh as the 8088 sees it, with
#               apps/arcade/qbert.patch applied (ARCADE-SPEC 10.2)
#   6000  2800  the 256 tiles TURNED to the portrait screen, 40 bytes each:
#               plane p's 8 rows at p x 8 (bit 7 = the left pixel), then the
#               8 rows' OPAQUE bytes (a pixel not 0) for the background's
#               priority over the sprites
#   8800    40  the yields: a count, then each `call far`'s address in the
#               guest - the host writes the N88 core's entry after the 9Ah
#   8840   100  a sprite code's byte: 1 when it has a pixel (the board parks
#               its unused sprites with a blank one)
#
# QBSPR.PRT, the aux part: the 256 sprites turned, 160 bytes each - 16 rows
# of five words, bit 15 the left pixel: the OPAQUE mask, then planes 0-3.
#
# "Turned": the cabinet's monitor is on its side and MAME rotates the board's
# 256 x 240 picture 270 degrees (ROT270), so the board's pixel (x, y) lands
# at portrait column y, row 255 - x. A tile's portrait (column c, row r) is
# its own (x = 7 - r, y = c); a sprite's is (x = 15 - r, y = c).
# =============================================================================
import struct
import sys

PROG = 0xA000                       # where the program sits for the 8088
WAIT = bytes([0x3B, 0x06, 0x80, 0x00, 0x74, 0xFA])   # cmp ax,[0080h] / jz $-4


def die(msg):
    sys.exit('arcqbgfx: %s' % msg)


def patch(prog, path):
    """Apply the patch file; out: the yields' guest addresses."""
    yields = []
    for n, line in enumerate(open(path), 1):
        line = line.split('#')[0].split()
        if not line:
            continue
        kind, at = line[0], int(line[1], 16)
        o = at - PROG
        if not 0 <= o < len(prog):
            die('line %d: %04X is not in the program' % (n, at))
        if kind == 'nop':
            k = int(line[2])
            # only a MOV sreg,r16 (8E /r) may be dropped
            for i in range(0, k, 2):
                if prog[o + i] != 0x8E:
                    die('line %d: %04X is not a segment load' % (n, at + i))
            prog[o:o + k] = b'\x90' * k
        elif kind == 'port':
            # mov [disp16],r8 (88 06 / A2) or mov byte [disp16],imm8 (C6 06)
            op = prog[o]
            if op == 0xA2:
                d = o + 1
            elif op in (0x88, 0xC6) and (prog[o + 1] & 0xC7) == 0x06:
                d = o + 2
            else:
                die('line %d: %04X is not a byte store to an address' % (n, at))
            w = struct.unpack_from('<H', prog, d)[0]
            if not 0x5800 <= w <= 0x5807:
                die('line %d: %04X stores to %04X, not 5800-5807h' % (n, at, w))
            struct.pack_into('<H', prog, d, w + 8)
        elif kind == 'yield':
            if bytes(prog[o:o + 6]) != WAIT:
                die('line %d: %04X is not the wait for the NMI' % (n, at))
            prog[o:o + 6] = b'\x9A\x00\x00\x00\x00\x90'
            yields.append(at)
        else:
            die('line %d: no kind %r' % (n, kind))
    return yields


def tile_px(img, t, x, y):
    b = img[0x6000 + t * 32 + y * 4 + (x >> 1)]
    return (b >> 4) if (x & 1) == 0 else (b & 15)


def sprite_px(img, code, x, y):
    v = 0
    for pl in range(4):             # MSB first: the first quarter is bit 3
        b = img[0x8000 + pl * 0x2000 + code * 32 + y * 2 + (x >> 3)]
        v = v << 1 | (b >> (7 - (x & 7))) & 1
    return v


def main():
    if len(sys.argv) != 5:
        die('usage: arcqbgfx.py QBERT.ROM qbert.patch QBROM.PRT QBSPR.PRT')
    img = open(sys.argv[1], 'rb').read()
    if len(img) != 0x10000:
        die('%s is not the Q*bert board image' % sys.argv[1])
    prog = bytearray(img[0:0x6000])
    yields = patch(prog, sys.argv[2])
    rom = bytearray(0x8940)
    rom[0:0x6000] = prog
    for t in range(256):
        k = 0x6000 + t * 40
        for r in range(8):
            opq = 0
            for c in range(8):
                v = tile_px(img, t, 7 - r, c)
                for p in range(4):
                    if (v >> p) & 1:
                        rom[k + p * 8 + r] |= 0x80 >> c
                if v:
                    opq |= 0x80 >> c
            rom[k + 32 + r] = opq
    struct.pack_into('<H', rom, 0x8800, len(yields))
    for i, a in enumerate(yields):
        struct.pack_into('<H', rom, 0x8802 + 2 * i, a)
    spr = bytearray(256 * 160)
    for code in range(256):
        for r in range(16):
            w = [0] * 5
            for c in range(16):
                v = sprite_px(img, code, 15 - r, c)
                if v:
                    w[0] |= 0x8000 >> c
                for p in range(4):
                    if (v >> p) & 1:
                        w[1 + p] |= 0x8000 >> c
            struct.pack_into('<5H', spr, code * 160 + r * 10, *w)
            if w[0]:
                rom[0x8840 + code] = 1
    open(sys.argv[3], 'wb').write(rom)
    open(sys.argv[4], 'wb').write(spr)
    print('arcqbgfx: %d yields, %s (%d bytes), %s (%d bytes)'
          % (len(yields), sys.argv[3], len(rom), sys.argv[4], len(spr)))


if __name__ == '__main__':
    main()
