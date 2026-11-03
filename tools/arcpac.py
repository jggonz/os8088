#!/usr/bin/env python3
# =============================================================================
# os8088 - tools/arcpac.py - the Namco Pac-Man board in Python: ARCADE's
# reference model for its Z80 boards (docs/ARCADE-SPEC.md section 7)
#
# tools/arcz80.py's Z80 at 3.072 MHz, with the board as MAME's pacman.cpp
# maps it: ROM 0000-3FFF, video RAM 4000-43FF, colour RAM 4400-47FF, RAM
# 4C00-4FEF, sprite attributes 4FF0-4FFF, the latches at 5000-5007 (bit 0 of
# 5000 enables the VBlank interrupt), the sound at 5040-505F, sprite
# positions at 5060-506F, the watchdog at 50C0; IN0, IN1 and the DIPs read at
# 5000, 5040, 5080 and 50C0; A15 ignored. OUT (00),A latches the IM 2 vector.
# One interrupt a frame, held until taken or until 5000 bit 0 is cleared.
#
#     python3 -I tools/arcpac.py stats  ROM [--frames N] [--png OUT]
#     python3 -I tools/arcpac.py seeds  ROM [--frames N] [-o FILE]
#     python3 -I tools/arcpac.py digest ROM --at F[,F...] [--png-prefix P]
#
# ROM is the board image tools/getarcrom.py writes (build/arcade-rom/
# PACMAN.ROM, the owner's set): program 0000, tiles 4000, sprites 5000, the
# colour PROM 6000, the palette PROM 6020, the waveforms 6120.
# =============================================================================
import argparse
import collections
import hashlib
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from arcz80 import Z80          # noqa: E402

CPU_HZ = 3072000
FRAME_T = 50688                 # 3.072 MHz / 60.606 Hz (MAME: 384 x 264 at 6.144 MHz)

IN0_IDLE = 0xFF                 # active low: up left right down rack coin1 coin2 service
IN1_IDLE = 0xFF                 # bit 4 = the test switch off, bit 7 = upright
DSW1 = 0xC9                     # 1 coin 1 credit, 3 lives, bonus at 10000, normal


class Board:
    def __init__(self, img):
        self.img = img
        self.rom = img[0:0x4000]
        self.m = bytearray(0x10000)
        self.m[0:0x4000] = self.rom
        self.in0 = IN0_IDLE
        self.in1 = IN1_IDLE
        self.irq_en = 0
        self.vec = 0
        self.pending = False
        self.snd = bytearray(32)
        self.vw = 0
        self.vchg = 0
        self.dirty = set()
        self.ios = collections.Counter()
        self.cpu = Z80(self)

    def rd(self, a):
        a &= 0x7FFF
        if a < 0x4800 or 0x4C00 <= a < 0x5000:
            return self.m[a]
        if a < 0x4C00:
            return 0xBF
        a &= 0x50FF
        if a & 0xC0 == 0x00: return self.in0
        if a & 0xC0 == 0x40: return self.in1
        if a & 0xC0 == 0x80: return DSW1
        return 0xFF

    def wr(self, a, v):
        a &= 0x7FFF
        if a < 0x4000:
            return
        if a < 0x4800:
            self.vw += 1
            if self.m[a] != v:
                self.vchg += 1
                self.dirty.add(a & 0x3FF)
            self.m[a] = v
            return
        if a < 0x4C00:
            return
        if a < 0x5000:
            self.m[a] = v
            return
        a &= 0x50FF
        self.ios['w%02X' % (a & 0xF0)] += 1
        if a & 0xF8 == 0x00:
            if a & 7 == 0:
                self.irq_en = v & 1
                if not self.irq_en:
                    self.pending = False
            return
        if 0x40 <= a & 0xFF < 0x60:
            self.snd[a & 0x1F] = v & 15
            return
        if 0x60 <= a & 0xFF < 0x70:
            self.m[0x5000 | (a & 0xFF)] = v       # sprite positions
            return

    def io_in(self, port):
        return 0xFF

    def io_out(self, port, v):
        if port & 0xFF == 0:
            self.vec = v
            self.ios['vec'] += 1

    def frame(self, each=None):
        """One frame: FRAME_T T-states, then VBlank."""
        cpu = self.cpu
        end = cpu.cyc + FRAME_T
        n = 0
        while cpu.cyc < end:
            if self.pending:
                t = cpu.irq(self.vec)
                if t:
                    self.pending = False
                    cpu.cyc += t
                    if each:
                        each(self, None, 'irq')
                    continue
            pc = cpu.pc
            cpu.cyc += cpu.step()
            n += 1
            if each:
                each(self, pc, None)
        if self.irq_en:
            self.pending = True
        return n

    # --- the picture, as MAME's pacman.cpp draws it ---------------------------
    def palette(self):
        out = []
        for i in range(32):
            c = self.img[0x6000 + i]
            r = 0x21 * (c & 1) + 0x47 * ((c >> 1) & 1) + 0x97 * ((c >> 2) & 1)
            g = 0x21 * ((c >> 3) & 1) + 0x47 * ((c >> 4) & 1) + 0x97 * ((c >> 5) & 1)
            b = 0x51 * ((c >> 6) & 1) + 0xAE * ((c >> 7) & 1)
            out.append((r, g, b))
        return out

    def tile_pixel(self, base, code, x, y, big=False):
        """A 2bpp pixel of a tile (8x8) or sprite (16x16) in MAME's layout."""
        if not big:
            byte = base + code * 16 + (8 if x < 4 else 0) + y
            bit = x & 3
        else:
            col_off = (8, 16, 24, 0)[x >> 2]
            byte = base + code * 64 + col_off + (y & 7) + (32 if y >= 8 else 0)
            bit = x & 3
        v = self.img[byte]
        return (((v >> (7 - bit)) & 1) << 1) | ((v >> (3 - bit)) & 1)

    def screen(self):
        """The 224x288 picture, portrait, as rows of (r,g,b)."""
        pal = self.palette()
        lut = self.img[0x6020:0x6120]
        W, H = 288, 224                       # MAME's own (unrotated) space
        land = [[(0, 0, 0)] * W for _ in range(H)]
        for row in range(28):
            for col in range(36):
                r, c = row + 2, col - 2
                offs = (r + ((c & 0x1F) << 5)) if (c & 0x20) else (c + (r << 5))
                code = self.m[0x4000 + offs]
                color = self.m[0x4400 + offs] & 0x1F
                for y in range(8):
                    for x in range(8):
                        p = self.tile_pixel(0x4000, code, x, y)
                        land[row * 8 + y][col * 8 + x] = pal[lut[color * 4 + p] & 15]
        for s in range(7, -1, -1):
            attr = self.m[0x4FF0 + 2 * s]
            color = self.m[0x4FF1 + 2 * s] & 0x1F
            code, fx, fy = attr >> 2, attr & 2, attr & 1
            sx = 272 - self.m[0x5060 + 2 * s + 1]
            sy = self.m[0x5060 + 2 * s] - 31
            for y in range(16):
                for x in range(16):
                    px = 15 - x if fx else x
                    py = 15 - y if fy else y
                    p = self.tile_pixel(0x5000, code, px, py, True)
                    c = lut[color * 4 + p] & 15
                    if c == 0:
                        continue
                    X, Y = sx + x, sy + y
                    if 16 <= X < W - 16 and 0 <= Y < H:      # sprites clip to the
                        land[Y][X] = pal[c]                    #   playfield's columns
        # ROT90: portrait (x', y') = (H-1-y, x)
        return [[land[H - 1 - xp][yp] for xp in range(H)] for yp in range(W)]


def write_png(path, rows):
    h, w = len(rows), len(rows[0])
    raw = b''.join(b'\0' + bytes(c for px in r for c in px) for r in rows)

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' +
                           chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def script(fr):
    """IN0, IN1 for frame fr: a coin at 400, a start at 500, then steer."""
    in0, in1 = IN0_IDLE, IN1_IDLE
    if 400 <= fr < 405:
        in0 &= ~0x20
    if 500 <= fr < 505:
        in1 &= ~0x20
    if fr >= 600:
        in0 &= ~(0x02, 0x08, 0x04, 0x01)[(fr // 70) % 4]
    return in0, in1


def run(b, frames, each=None, perframe=None):
    for fr in range(frames):
        b.in0, b.in1 = script(fr)
        n = b.frame(each)
        if perframe:
            perframe(fr, b, n)


def cmd_stats(img, a):
    b = Board(img)
    per = []
    irq = [0, 0]
    hot = collections.Counter()
    state = {'in': False, 'sp': 0}

    def each(b, pc, ev):
        if ev == 'irq':
            state['in'] = True
            state['sp'] = b.cpu.sp + 2
            return
        if state['in'] and b.cpu.sp >= state['sp']:
            state['in'] = False
        irq[0 if state['in'] else 1] += 1
        hot[pc] += 1

    def perframe(fr, b, n):
        per.append((fr, n, b.vw, b.vchg, len(b.dirty), b.cpu.halted))
        b.vw = b.vchg = 0
        b.dirty = set()
    run(b, a.frames, each, perframe)
    for name, lo, hi in (('attract', 60, 400), ('play', 650, a.frames)):
        rows = [p for p in per if lo <= p[0] < hi]
        if not rows:
            continue
        print(name, len(rows), 'frames')
        for i, what in ((1, 'instructions/frame'), (2, 'video writes/frame'),
                        (3, 'video changed     '), (4, 'tiles changed     ')):
            v = sorted(r[i] for r in rows)
            print('  %s avg %.0f p50 %d p90 %d max %d' % (
                what, sum(v) / len(v), v[len(v) // 2], v[len(v) * 9 // 10], v[-1]))
    tot = sum(irq)
    print('interrupt handler: %.1f%% of instructions' % (100.0 * irq[0] / max(1, tot)))
    for pc, n in hot.most_common(16):
        print('  %04X %5.1f%%' % (pc, 100.0 * n / tot))
    print('io', dict(b.ios), 'im', b.cpu.im, 'i %02X' % b.cpu.i, 'vec %02X' % b.vec)
    if a.png:
        write_png(a.png, b.screen())


def cmd_seeds(img, a):
    b = Board(img)
    targets = set()
    after = set()
    intpc = set()
    st = {'prev': None}

    rst = {}                            # vector -> [where it came back]
    ran = {}                            # vector -> how often it ran
    pending = []                        # (SP before, RST's PC, vector)

    def each(b, pc, ev):
        cpu = b.cpu
        if ev == 'irq':
            intpc.add(b.cpu.rd16(cpu.sp))
            targets.add(cpu.pc)
            return
        while pending and cpu.sp > pending[-1][0]:
            pending.pop()               # unwound past it: it never came back
        if pending and cpu.sp == pending[-1][0] and \
                pending[-1][1] < cpu.pc <= pending[-1][1] + 5 and \
                (b.m[pc] in (0xC9, 0xE9) or (b.m[pc] & 0xC7) == 0xC0 or
                 (b.m[pc] in (0xDD, 0xFD) and b.m[pc + 1] == 0xE9)):
            # by a RET, or the POP / JP (HL) a dispatcher returns with
            sp0, at, vec = pending.pop()
            rst.setdefault(vec, []).append(cpu.pc - at)
        op = b.m[pc]
        if (op & 0xC7) == 0xC7:
            pending.append(((cpu.sp + 2) & 0xFFFF, pc, op & 0x38))
            ran[op & 0x38] = ran.get(op & 0x38, 0) + 1
        if op == 0xCD or (op & 0xC7) == 0xC4:       # a CALL's return site is
            after.add(pc + 3)                      #   walked; an RST's is not
                                                   #   (RST 28h, 30h carry data)
        if op in (0xC9, 0xE9) or (op & 0xC7) == 0xC0 or \
                (op in (0xDD, 0xFD) and b.m[pc + 1] == 0xE9) or \
                (op == 0xED and b.m[pc + 1] in (0x4D, 0x45)):
            targets.add(cpu.pc)
    run(b, a.frames, each)
    lines = ['# Indirect targets (RET, JP (HL), the IM 2 vector) tools/arcpac.py',
             '# reached in %d frames of its script that are not the instruction'
             % a.frames,
             '# after a CALL or an interrupt\'s return address. Addresses only.']
    lines.append('# Where each RST came back, after its inline data (apps/arcade/'
                 'arcz80t.py walks it):')
    for vec in sorted(rst):             # a call, if nearly every run of it
        back = rst[vec]                 #   came back, and always as far on
        if len(set(back)) == 1 and len(back) >= 0.9 * ran[vec]:
            lines.append('RST %02X +%d' % (vec, back[0]))
    lines += ['%04X' % p for p in sorted(targets - after - intpc) if p < 0x4000]
    out = '\n'.join(lines) + '\n'
    if a.output:
        open(a.output, 'w').write(out)
    else:
        sys.stdout.write(out)


def cmd_digest(img, a):
    at = sorted(int(x) for x in a.at.split(','))
    b = Board(img)

    def perframe(fr, b, n):
        if fr + 1 in at:
            print('%d %s' % (fr + 1, hashlib.sha1(bytes(b.m[0x4000:0x4800])).hexdigest()))
            if a.png_prefix:
                write_png('%s%d.png' % (a.png_prefix, fr + 1), b.screen())
    run(b, at[-1], None, perframe)


def main():
    ap = argparse.ArgumentParser(description='ARCADE reference model: the Pac-Man board')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('stats')
    p.add_argument('rom')
    p.add_argument('--frames', type=int, default=1200)
    p.add_argument('--png')
    p = sub.add_parser('seeds')
    p.add_argument('rom')
    p.add_argument('--frames', type=int, default=4000)
    p.add_argument('-o', '--output')
    p = sub.add_parser('digest')
    p.add_argument('rom')
    p.add_argument('--at', required=True)
    p.add_argument('--png-prefix')
    a = ap.parse_args()
    img = open(a.rom, 'rb').read()
    if len(img) != 0x6220:
        sys.exit('arcpac: %s is %d bytes, not the Pac-Man board image' % (a.rom, len(img)))
    {'stats': cmd_stats, 'seeds': cmd_seeds, 'digest': cmd_digest}[a.cmd](img, a)


if __name__ == '__main__':
    main()
