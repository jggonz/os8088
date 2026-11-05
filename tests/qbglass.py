#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/qbglass.py - Q*bert's picture IS its board's memory
# (ARCADE-SPEC 10.4)
#
#     python3 tests/qbglass.py [--machine os8088_xt_vga]
#
# Q*bert picked from Game on MartyPC's VGA XT, then left in its attract mode,
# which plays the game itself - the pyramid, the sprites, and a stretch with
# the BACKGROUND IN FRONT of them (5803h bit 0). At chosen points the machine
# is stopped the instant ar_loop's frame hook returns having DRAWN (qb_skips
# 0: an 8088 lets the picture skip frames, ARCADE-SPEC 10.5), and the glass
# is compared - every portrait pixel but those of a box of changed tiles
# still waiting its turn (qb_dirty, ARCADE-SPEC 10.4: an 8088 repaints a
# change of scene over many frames) - with the picture MAME's gottlieb driver
# makes of the same memory: the game's own segment G (found through the view,
# NV_GSEG) - video RAM 3800h, sprite RAM 3000h, the palette 5000h, the
# priority at 580Bh - and the tiles and sprites out of the ROM image. None
# may differ. A tile the host never redrew because the game's store went past
# the copy, a sprite box left stale, a priority pass wrong: each is a pixel
# here.
# =============================================================================
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, HERE)
os.chdir(ROOT)
import os88build                    # noqa: E402
import os88marty                    # noqa: E402
import os88ui                       # noqa: E402
from arcspeed import find_view      # noqa: E402

V_NFRAME = 0x6044
NV_GSEG = 0x6080
XB = 10                             # the picture's left byte (arcqb.inc QB_XB)
# frames of the attract mode to look at, and whether the background must be
# in front there (tools/qbnative.py's run: priority 1 from about 2120 to 2260)
# (frame, priority, how many drawn frames from there - the priority stretch
# is looked at across its length, at least three times and up to eight,
# so that a sprite crosses a tile in front of it in some frame)
LOOKS = ((700, None, 1), (1300, None, 1), (2130, 1, 8))


def skips_addr():
    """qb_skips' offset (a bss cell, so no label): out of the listing's
    encoding of `mov byte [qb_skips], 0` (C6 06 lo hi 00)."""
    import re
    for ln in open(os88build.at('build/arcade.lst')):
        m = re.match(r'\s*\d+\s+[0-9A-F]{8}\s+C606\[([0-9A-F]{4})\]00\s.*mov byte \[qb_skips\], 0', ln)
        if m:
            h = m.group(1)
            return int(h[2:4] + h[0:2], 16)
    raise SystemExit('qbglass: FAIL - qb_skips is not in the listing')


def dirty_addr():
    """qb_dirty's offset, out of `mov byte [qb_dirty+bx], 1` (C6 87 lo hi 01)."""
    import re
    for ln in open(os88build.at('build/arcade.lst')):
        m = re.match(r'\s*\d+\s+[0-9A-F]{8}\s+C687\[([0-9A-F]{4})\]01\s.*\[qb_dirty\+bx\]', ln)
        if m:
            h = m.group(1)
            return int(h[2:4] + h[0:2], 16)
    raise SystemExit('qbglass: FAIL - qb_dirty is not in the listing')


def after_frame():
    """ar_loop's instruction after `call [bx+BD_FRAME]`, from the listing."""
    import re
    for ln in open(os88build.at('build/arcade.lst')):
        m = re.match(r'\s*\d+\s+([0-9A-F]{8})\s+(\S+)\s+call \[bx\+BD_FRAME\]', ln)
        if m:
            return int(m.group(1), 16) + len(m.group(2)) // 2
    raise SystemExit('qbglass: FAIL - no BD_FRAME call in the listing')


def reference(rom, g):
    """MAME's picture of board memory G, turned: rows of 240 pixel values."""
    vram = g[0x3800:0x3C00]
    spr = g[0x3000:0x3100]
    pri = g[0x580B] & 1
    land = [[0] * 256 for _ in range(240)]

    def tile(x, y):
        t = vram[(y >> 3) * 32 + (x >> 3)]
        b = rom[0x6000 + t * 32 + (y & 7) * 4 + ((x & 7) >> 1)]
        return (b >> 4) if (x & 1) == 0 else (b & 15)
    if not pri:
        for y in range(240):
            for x in range(256):
                land[y][x] = tile(x, y)
    for offs in range(0, 256, 4):
        sx = spr[offs + 1] - 4
        sy = spr[offs] - 13
        code = 255 ^ spr[offs + 2]
        for r in range(16):
            for k in range(16):
                v = 0
                for pl in range(4):
                    b = rom[0x8000 + pl * 0x2000 + code * 32 + r * 2 + (k >> 3)]
                    v = v << 1 | (b >> (7 - (k & 7))) & 1
                x, y = sx + k, sy + r
                if v and 8 <= x < 256 and 0 <= y < 240:
                    land[y][x] = v
    if pri:
        for y in range(240):
            for x in range(256):
                v = tile(x, y)
                if v:
                    land[y][x] = v
    # ROT270: the board's (x, y) is the portrait column y, row 255 - x
    return [[land[c][255 - r] for c in range(240)] for r in range(256)]


def compare(rom, g, planes, pending=bytes(64)):
    """Every screen row of the picture - both of a doubled one - read out of
    the card's four planes, against the reference's pixel VALUES (the DAC is
    the palette's, so a value is a colour)."""
    want = reference(rom, g)
    bad = 0
    first = None
    for r in range(256):
        for y in range((r * 15) // 8, ((r + 1) * 15) // 8):
            for c in range(240):
                if pending[(r >> 5) * 8 + (c >> 5)]:
                    continue                # a box still waiting its turn
                exp = want[r][c]
                for x in (XB * 8 + 2 * c, XB * 8 + 2 * c + 1):
                    o = y * 80 + (x >> 3)
                    bit = 7 - (x & 7)
                    got = sum(((planes[p][o] >> bit) & 1) << p for p in range(4))
                    if got != exp:
                        bad += 1
                        if first is None:
                            first = (c, r, got, exp)
    return bad, first


def dump(where, frame, rom, g, fb, w, h):
    os.makedirs(where, exist_ok=True)
    os88marty.write_png_rgb(os.path.join(where, 'glass%d.png' % frame), w, h, fb)
    pal = g[0x5000:0x5020]
    want = reference(rom, g)
    out = bytearray()
    for r in range(256):
        for c in range(240):
            i = want[r][c]
            e, o = pal[2 * i], pal[2 * i + 1]
            out += bytes(((o & 15) * 17, (e >> 4) * 17, (e & 15) * 17))
    os88marty.write_png_rgb(os.path.join(where, 'want%d.png' % frame), 240, 256, bytes(out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--machine', default='os8088_xt_vga')
    ap.add_argument('--dump', help='a directory: each look as the glass and as wanted (PNG)')
    a = ap.parse_args()
    rom = open(os88build.at('build/arcade-rom/QBERT.ROM'), 'rb').read()
    hook = after_frame()
    skips = skips_addr()
    dirty = dirty_addr()
    fails = []
    with os88ui.boot('build/os8088-360.img', apps='build/arcade360.img',
                     machine=a.machine, quick=True) as ui:
        m = ui.m
        ui.path('B:/ARCADE.O88')
        m.run()
        os88marty.guest_sleep(m, 1)
        ui.menu_pick('Game', 'Q*bert')
        m.pause()
        img = open(os88build.at('build/arcade.bin'), 'rb').read()
        mem = bytes(m.read(0, 0xA0000))
        base = mem.find(img[0xE00:0xE80]) - 0xE00
        v = find_view(m)

        def word(at):
            return struct.unpack('<H', bytes(m.read(at, 2)))[0]
        for frame, pri, count in LOOKS:
            while word(v + V_NFRAME) < frame:
                m.advance(frames=20)
            gseg = word(v + NV_GSEG)
            done = 0
            for k in range(count):
                if k:
                    m.advance(frames=8)
                for _ in range(200):        # the next frame that DREW
                    m.bp_exec(base + hook)
                    m.run()
                    if m.wait_stop(20) != 'breakpoint':
                        raise SystemExit('qbglass: FAIL - the frame hook never returned')
                    m.bp_exec()
                    if bytes(m.read(base + skips, 1))[0] == 0 and \
                            sum(1 for b in m.read(base + dirty, 64) if b) <= 62:
                        g = bytes(m.read(gseg * 16, 0x10000))
                        if pri is None or (g[0x580B] & 1) == pri:
                            break
                else:
                    if done < min(count, 3):    # the stretch may end first
                        fails.append('frame %d: %d drawn frames with priority %s, not %d'
                                     % (frame, done, pri, min(count, 3)))
                    break
                done += 1
                planes = m.planes(0, 38400)
                pending = bytes(m.read(base + dirty, 64))
                bad, first = compare(rom, g, planes, pending)
                if a.dump:
                    w, h, fb = m.fbuf()
                    dump(a.dump, frame, rom, g, fb, w, h)
                print('qbglass: frame %d (priority %d, %d boxes pending): %d pixels differ%s'
                      % (word(v + V_NFRAME), g[0x580B] & 1, sum(1 for b in pending if b), bad,
                         '' if not first else ', first at column %d row %d: %d, not %d' % first))
                if bad:
                    fails.append('frame %d: %d pixels are not the board memory\'s picture'
                                 % (word(v + V_NFRAME), bad))
    for f in fails:
        print('qbglass: FAIL - ' + f)
    if fails:
        return 1
    print('qbglass: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
