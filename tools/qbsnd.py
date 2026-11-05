#!/usr/bin/env python3
# =============================================================================
# os8088 - tools/qbsnd.py - Gottlieb's rev. 1 sound board (Q*bert's), in
# Python: the reference and the measuring stick for ARCADE-SPEC 13
#
#     python3 -I tools/qbsnd.py QBERT.ROM --measure
#     python3 -I tools/qbsnd.py QBERT.ROM --wav DIR      (each command, 8-bit)
#
# The board as MAME's gottlieb_a.cpp has it: an M6503 at 3.579545 MHz / 4,
# a 6532 RIOT (128 bytes of RAM, two ports, a timer), an MC1408 DAC written
# at 1000h, the Votrax at 2000h (not sounded), the two sound ROMs at
# 7000-7FFFh. The main board's command lands on the RIOT's port A, inverted
# in its low six bits, and its bit 7 strobes the RIOT's edge detector, whose
# interrupt is the 6503's IRQ.
#
# The ROMs come from the user's <SET>.ROM (ARCADE-SPEC 1.4) or straight from
# the two chips; nothing here holds a ROM byte.
# =============================================================================
import argparse
import os
import struct
import sys

CLOCK = 3579545 // 4        # the 6503's clock, Hz


class CPU6502:
    """An NMOS 6502: every documented opcode, decimal mode, the cycles a
    real one takes (page crossings and taken branches included)."""

    def __init__(self, bus):
        self.bus = bus
        self.a = self.x = self.y = 0
        self.s = 0xFD
        self.p = 0x24
        self.pc = 0
        self.cyc = 0
        self.irq_line = False
        self.build()

    def rd(self, a):
        return self.bus.read(a & 0xFFFF)

    def wr(self, a, v):
        self.bus.write(a & 0xFFFF, v & 0xFF)

    def rd16(self, a):
        return self.rd(a) | (self.rd(a + 1) << 8)

    def push(self, v):
        self.wr(0x100 | self.s, v)
        self.s = (self.s - 1) & 0xFF

    def pop(self):
        self.s = (self.s + 1) & 0xFF
        return self.rd(0x100 | self.s)

    def reset(self):
        self.pc = self.rd16(0xFFFC)
        self.p |= 0x04

    def nz(self, v):
        self.p = (self.p & 0x7D) | (v & 0x80) | (0 if v else 2)
        return v

    def irq(self):
        if self.p & 0x04:
            return False
        self.push(self.pc >> 8)
        self.push(self.pc & 0xFF)
        self.push((self.p & ~0x10) | 0x20)
        self.p |= 0x04
        self.pc = self.rd16(0xFFFE)
        self.cyc += 7
        return True

    # --- addressing: each returns (address, extra cycle for a page cross)
    def imm(self):
        a = self.pc
        self.pc += 1
        return a, 0

    def zp(self):
        a = self.rd(self.pc)
        self.pc += 1
        return a, 0

    def zpx(self):
        a = (self.rd(self.pc) + self.x) & 0xFF
        self.pc += 1
        return a, 0

    def zpy(self):
        a = (self.rd(self.pc) + self.y) & 0xFF
        self.pc += 1
        return a, 0

    def ab(self):
        a = self.rd16(self.pc)
        self.pc += 2
        return a, 0

    def abx(self):
        b = self.rd16(self.pc)
        self.pc += 2
        a = (b + self.x) & 0xFFFF
        return a, int((a ^ b) > 0xFF)

    def aby(self):
        b = self.rd16(self.pc)
        self.pc += 2
        a = (b + self.y) & 0xFFFF
        return a, int((a ^ b) > 0xFF)

    def izx(self):
        z = (self.rd(self.pc) + self.x) & 0xFF
        self.pc += 1
        return self.rd(z) | (self.rd((z + 1) & 0xFF) << 8), 0

    def izy(self):
        z = self.rd(self.pc)
        self.pc += 1
        b = self.rd(z) | (self.rd((z + 1) & 0xFF) << 8)
        a = (b + self.y) & 0xFFFF
        return a, int((a ^ b) > 0xFF)

    # --- operations
    def adc(self, v):
        c = self.p & 1
        if self.p & 0x08:
            lo = (self.a & 0x0F) + (v & 0x0F) + c
            hi = (self.a >> 4) + (v >> 4)
            if lo > 9:
                lo += 6
            if lo > 0x0F:
                hi += 1
            z = ((self.a + v + c) & 0xFF) == 0
            n = hi & 8
            vflag = (~(self.a ^ v) & (self.a ^ (hi << 4)) & 0x80)
            if hi > 9:
                hi += 6
            r = ((hi << 4) | (lo & 0x0F)) & 0xFF
            self.p = (self.p & 0x3C) | (1 if hi > 0x0F else 0) | (2 if z else 0) \
                | (0x80 if n else 0) | (0x40 if vflag else 0)
            self.a = r
            return
        r = self.a + v + c
        self.p = (self.p & 0xBE) | (1 if r > 0xFF else 0) | \
            (0x40 if (~(self.a ^ v) & (self.a ^ r) & 0x80) else 0)
        self.a = self.nz(r & 0xFF)

    def sbc(self, v):
        if self.p & 0x08:
            c = self.p & 1
            r = self.a - v - (1 - c)
            lo = (self.a & 0x0F) - (v & 0x0F) - (1 - c)
            hi = (self.a >> 4) - (v >> 4)
            if lo < 0:
                lo -= 6
                hi -= 1
            if hi < 0:
                hi -= 6
            self.p = (self.p & 0x3C) | (0 if r < 0 else 1) | \
                (0x40 if ((self.a ^ v) & (self.a ^ r) & 0x80) else 0)
            self.nz(r & 0xFF)
            self.a = ((hi << 4) | (lo & 0x0F)) & 0xFF
            return
        self.adc(v ^ 0xFF)

    def cmp(self, r, v):
        d = r - v
        self.p = (self.p & 0xFE) | (1 if d >= 0 else 0)
        self.nz(d & 0xFF)

    def branch(self, cond):
        off = self.rd(self.pc)
        self.pc += 1
        if cond:
            if off & 0x80:
                off -= 256
            t = (self.pc + off) & 0xFFFF
            self.cyc += 1 + int((t ^ self.pc) > 0xFF)
            self.pc = t

    def build(self):
        t = {}
        modes = {'imm': self.imm, 'zp': self.zp, 'zpx': self.zpx, 'zpy': self.zpy,
                 'ab': self.ab, 'abx': self.abx, 'aby': self.aby, 'izx': self.izx,
                 'izy': self.izy}

        def ld(reg):
            def f(m):
                a, x = m()
                v = self.nz(self.rd(a))
                setattr(self, reg, v)
                return x
            return f

        def st(reg):
            def f(m):
                a, _ = m()
                self.wr(a, getattr(self, reg))
                return 0
            return f

        def alu(fn):
            def f(m):
                a, x = m()
                fn(self.rd(a))
                return x
            return f

        def rmw(fn):
            def f(m):
                a, _ = m()
                self.wr(a, fn(self.rd(a)))
                return 0
            return f

        def asl(v):
            self.p = (self.p & 0xFE) | (v >> 7)
            return self.nz((v << 1) & 0xFF)

        def lsr(v):
            self.p = (self.p & 0xFE) | (v & 1)
            return self.nz(v >> 1)

        def rol(v):
            c = self.p & 1
            self.p = (self.p & 0xFE) | (v >> 7)
            return self.nz(((v << 1) | c) & 0xFF)

        def ror(v):
            c = self.p & 1
            self.p = (self.p & 0xFE) | (v & 1)
            return self.nz((v >> 1) | (c << 7))

        def inc(v):
            return self.nz((v + 1) & 0xFF)

        def dec(v):
            return self.nz((v - 1) & 0xFF)

        def ora(v):
            self.a = self.nz(self.a | v)

        def and_(v):
            self.a = self.nz(self.a & v)

        def eor(v):
            self.a = self.nz(self.a ^ v)

        def bit(v):
            self.p = (self.p & 0x3D) | (v & 0xC0) | (0 if (self.a & v) else 2)

        groups = [
            # (opcode, mode, op, cycles)
            (0xA9, 'imm', ld('a'), 2), (0xA5, 'zp', ld('a'), 3), (0xB5, 'zpx', ld('a'), 4),
            (0xAD, 'ab', ld('a'), 4), (0xBD, 'abx', ld('a'), 4), (0xB9, 'aby', ld('a'), 4),
            (0xA1, 'izx', ld('a'), 6), (0xB1, 'izy', ld('a'), 5),
            (0xA2, 'imm', ld('x'), 2), (0xA6, 'zp', ld('x'), 3), (0xB6, 'zpy', ld('x'), 4),
            (0xAE, 'ab', ld('x'), 4), (0xBE, 'aby', ld('x'), 4),
            (0xA0, 'imm', ld('y'), 2), (0xA4, 'zp', ld('y'), 3), (0xB4, 'zpx', ld('y'), 4),
            (0xAC, 'ab', ld('y'), 4), (0xBC, 'abx', ld('y'), 4),
            (0x85, 'zp', st('a'), 3), (0x95, 'zpx', st('a'), 4), (0x8D, 'ab', st('a'), 4),
            (0x9D, 'abx', st('a'), 5), (0x99, 'aby', st('a'), 5), (0x81, 'izx', st('a'), 6),
            (0x91, 'izy', st('a'), 6),
            (0x86, 'zp', st('x'), 3), (0x96, 'zpy', st('x'), 4), (0x8E, 'ab', st('x'), 4),
            (0x84, 'zp', st('y'), 3), (0x94, 'zpx', st('y'), 4), (0x8C, 'ab', st('y'), 4),
        ]
        for base, fn in ((0x00, ora), (0x20, and_), (0x40, eor), (0x60, self.adc),
                         (0xE0, self.sbc)):
            groups += [(base | 0x09, 'imm', alu(fn), 2), (base | 0x05, 'zp', alu(fn), 3),
                       (base | 0x15, 'zpx', alu(fn), 4), (base | 0x0D, 'ab', alu(fn), 4),
                       (base | 0x1D, 'abx', alu(fn), 4), (base | 0x19, 'aby', alu(fn), 4),
                       (base | 0x01, 'izx', alu(fn), 6), (base | 0x11, 'izy', alu(fn), 5)]
        cmpa = (lambda v: self.cmp(self.a, v))
        groups += [(0xC9, 'imm', alu(cmpa), 2), (0xC5, 'zp', alu(cmpa), 3),
                   (0xD5, 'zpx', alu(cmpa), 4), (0xCD, 'ab', alu(cmpa), 4),
                   (0xDD, 'abx', alu(cmpa), 4), (0xD9, 'aby', alu(cmpa), 4),
                   (0xC1, 'izx', alu(cmpa), 6), (0xD1, 'izy', alu(cmpa), 5)]
        cmpx = (lambda v: self.cmp(self.x, v))
        cmpy = (lambda v: self.cmp(self.y, v))
        groups += [(0xE0, 'imm', alu(cmpx), 2), (0xE4, 'zp', alu(cmpx), 3),
                   (0xEC, 'ab', alu(cmpx), 4), (0xC0, 'imm', alu(cmpy), 2),
                   (0xC4, 'zp', alu(cmpy), 3), (0xCC, 'ab', alu(cmpy), 4),
                   (0x24, 'zp', alu(bit), 3), (0x2C, 'ab', alu(bit), 4)]
        for base, fn in ((0x00, asl), (0x20, rol), (0x40, lsr), (0x60, ror)):
            groups += [(base | 0x06, 'zp', rmw(fn), 5), (base | 0x16, 'zpx', rmw(fn), 6),
                       (base | 0x0E, 'ab', rmw(fn), 6), (base | 0x1E, 'abx', rmw(fn), 7)]
        groups += [(0xE6, 'zp', rmw(inc), 5), (0xF6, 'zpx', rmw(inc), 6),
                   (0xEE, 'ab', rmw(inc), 6), (0xFE, 'abx', rmw(inc), 7),
                   (0xC6, 'zp', rmw(dec), 5), (0xD6, 'zpx', rmw(dec), 6),
                   (0xCE, 'ab', rmw(dec), 6), (0xDE, 'abx', rmw(dec), 7)]
        for op, mode, fn, cyc in groups:
            m = modes[mode]
            t[op] = (lambda f, m, c: (lambda: c + f(m)))(fn, m, cyc)
        s = self

        def imp(fn, c):
            def g():
                fn()
                return c
            return g

        def acc(fn):
            def g():
                s.a = fn(s.a)
                return 2
            return g
        t[0x0A] = acc(asl)
        t[0x2A] = acc(rol)
        t[0x4A] = acc(lsr)
        t[0x6A] = acc(ror)

        def setr(r, v):
            setattr(s, r, v)
        t[0xAA] = imp(lambda: setr('x', s.nz(s.a)), 2)
        t[0xA8] = imp(lambda: setr('y', s.nz(s.a)), 2)
        t[0x8A] = imp(lambda: setr('a', s.nz(s.x)), 2)
        t[0x98] = imp(lambda: setr('a', s.nz(s.y)), 2)
        t[0xBA] = imp(lambda: setr('x', s.nz(s.s)), 2)
        t[0x9A] = imp(lambda: setr('s', s.x), 2)
        t[0xE8] = imp(lambda: setr('x', s.nz((s.x + 1) & 0xFF)), 2)
        t[0xC8] = imp(lambda: setr('y', s.nz((s.y + 1) & 0xFF)), 2)
        t[0xCA] = imp(lambda: setr('x', s.nz((s.x - 1) & 0xFF)), 2)
        t[0x88] = imp(lambda: setr('y', s.nz((s.y - 1) & 0xFF)), 2)
        t[0xEA] = imp(lambda: None, 2)
        t[0x18] = imp(lambda: setr('p', s.p & ~0x01), 2)
        t[0x38] = imp(lambda: setr('p', s.p | 0x01), 2)
        t[0x58] = imp(lambda: setr('p', s.p & ~0x04), 2)
        t[0x78] = imp(lambda: setr('p', s.p | 0x04), 2)
        t[0xB8] = imp(lambda: setr('p', s.p & ~0x40), 2)
        t[0xD8] = imp(lambda: setr('p', s.p & ~0x08), 2)
        t[0xF8] = imp(lambda: setr('p', s.p | 0x08), 2)
        t[0x48] = imp(lambda: s.push(s.a), 3)
        t[0x08] = imp(lambda: s.push(s.p | 0x30), 3)
        t[0x68] = imp(lambda: setr('a', s.nz(s.pop())), 4)
        t[0x28] = imp(lambda: setr('p', (s.pop() & 0xEF) | 0x20), 4)
        for op, mask, want in ((0x10, 0x80, 0), (0x30, 0x80, 1), (0x50, 0x40, 0),
                               (0x70, 0x40, 1), (0x90, 0x01, 0), (0xB0, 0x01, 1),
                               (0xD0, 0x02, 0), (0xF0, 0x02, 1)):
            t[op] = (lambda mask, want: lambda: (s.branch(bool(s.p & mask) == bool(want)), 2)[1])(mask, want)

        def jmp():
            s.pc = s.rd16(s.pc)
            return 3

        def jmpi():
            a = s.rd16(s.pc)
            s.pc = s.rd(a) | (s.rd((a & 0xFF00) | ((a + 1) & 0xFF)) << 8)
            return 5

        def jsr():
            a = s.rd16(s.pc)
            r = (s.pc + 1) & 0xFFFF
            s.push(r >> 8)
            s.push(r & 0xFF)
            s.pc = a
            return 6

        def rts():
            lo = s.pop()
            s.pc = ((s.pop() << 8) | lo) + 1
            return 6

        def rti():
            s.p = (s.pop() & 0xEF) | 0x20
            lo = s.pop()
            s.pc = (s.pop() << 8) | lo
            return 6

        def brk():
            s.pc += 1
            s.push(s.pc >> 8)
            s.push(s.pc & 0xFF)
            s.push(s.p | 0x30)
            s.p |= 0x04
            s.pc = s.rd16(0xFFFE)
            return 7
        t[0x4C] = jmp
        t[0x6C] = jmpi
        t[0x20] = jsr
        t[0x60] = rts
        t[0x40] = rti
        t[0x00] = brk
        self.ops = t

    def step(self):
        if self.irq_line:
            if self.irq():
                return
        op = self.rd(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        f = self.ops.get(op)
        if f is None:
            raise RuntimeError('6502: opcode %02X at %04X' % (op, (self.pc - 1) & 0xFFFF))
        self.cyc += f()
        self.pc &= 0xFFFF


class Riot:
    """The 6532: 128 bytes of RAM, port A (the command), port B, the timer,
    the PA7 edge detector."""

    def __init__(self, cpu_cycles):
        self.cyc = cpu_cycles           # () -> the CPU's cycle count
        self.ram = bytearray(128)
        self.pa_in = 0x40               # (idle: no command, PA7 low)
        self.pb_in = 0xFF
        self.ddra = self.ddrb = 0
        self.ora = self.orb = 0
        self.t_at = 0                   # the cycle the timer was written
        self.t_val = 0                  # the value written
        self.t_div = 1024
        self.t_ie = False
        self.t_flag = False
        self.pa7_flag = False
        self.pa7_ie = False
        self.pa7_pos = False

    def timer(self):
        """(the count now, has it passed zero)."""
        el = self.cyc() - self.t_at
        n = el // self.t_div
        if n <= self.t_val:
            return self.t_val - n, False
        past = el - (self.t_val + 1) * self.t_div
        return (0xFF - past) & 0xFF, True

    def irq(self):
        _, z = self.timer()
        if z:
            self.t_flag = True
        return (self.t_flag and self.t_ie) or (self.pa7_flag and self.pa7_ie)

    def strobe(self, pa):
        """The main board writes port A: an edge on PA7 the detector watches."""
        old = self.pa_in & 0x80
        self.pa_in = pa
        new = pa & 0x80
        if old != new and bool(new) == self.pa7_pos:
            self.pa7_flag = True

    def read(self, a):
        if a & 0x04 == 0:
            r = a & 3
            if r == 0:
                return (self.pa_in & ~self.ddra | self.ora & self.ddra) & 0xFF
            if r == 1:
                return self.ddra
            if r == 2:
                return (self.pb_in & ~self.ddrb | self.orb & self.ddrb) & 0xFF
            return self.ddrb
        if a & 1 == 0:
            v, z = self.timer()
            self.t_ie = bool(a & 8)
            if z:
                self.t_flag = False         # (a read after zero clears it)
            return v
        _, z = self.timer()
        if z:
            self.t_flag = True
        f = (0x80 if self.t_flag else 0) | (0x40 if self.pa7_flag else 0)
        self.pa7_flag = False
        return f

    def write(self, a, v):
        if a & 0x04 == 0:
            r = a & 3
            if r == 0:
                self.ora = v
            elif r == 1:
                self.ddra = v
            elif r == 2:
                self.orb = v
            else:
                self.ddrb = v
            return
        if a & 0x10:
            self.t_div = (1, 8, 64, 1024)[a & 3]
            self.t_val = v
            self.t_at = self.cyc()
            self.t_ie = bool(a & 8)
            self.t_flag = False
        else:
            self.pa7_pos = bool(a & 1)
            self.pa7_ie = bool(a & 2)


class Board:
    def __init__(self, rom7000):
        assert len(rom7000) == 0x1000
        self.rom = bytes(rom7000)
        self.cpu = CPU6502(self)
        self.riot = Riot(lambda: self.cpu.cyc)
        self.dac = []                   # (cycle, value)
        self.speech = 0

    def read(self, a):
        a &= 0x7FFF
        if a >= 0x6000:
            return self.rom[a & 0x0FFF] if a >= 0x7000 else 0xFF
        b = a & 0x0FFF
        if a < 0x1000:
            if b & 0x0280 == 0:             # 0000-007F, mirrored
                return self.riot.ram[b & 0x7F]
            if b & 0x0280 == 0x0200:        # the RIOT's registers
                return self.riot.read(b & 0x1F)
        return 0xFF

    def write(self, a, v):
        a &= 0x7FFF
        b = a & 0x0FFF
        if a < 0x1000:
            if b & 0x0280 == 0:
                self.riot.ram[b & 0x7F] = v
            elif b & 0x0280 == 0x0200:
                self.riot.write(b & 0x1F, v)
        elif a < 0x2000:
            self.dac.append((self.cpu.cyc, v))
        elif a < 0x3000:
            self.speech += 1

    def run(self, cycles):
        end = self.cpu.cyc + cycles
        while self.cpu.cyc < end:
            self.cpu.irq_line = self.riot.irq()
            self.cpu.step()

    def command(self, code):
        """A sound CODE onto port A (bits 0-5 as the 6503 reads them, so the
        main board's byte INVERTED: code = ~byte & 3Fh, MAME's
        gottlieb_sound_r1), PA7 strobed up and back down."""
        self.riot.strobe((code & 0x3F) | 0xC0)
        self.riot.strobe((code & 0x3F) | 0x40)


def rom_of(path):
    d = open(path, 'rb').read()
    if d[:6] == b'ARCROM':
        d = d[32:]
    if len(d) == 0x11000:                        # QBERT.ROM with its sound chips
        return d[0x10000:]
    if len(d) == 0x1000:
        return d
    raise SystemExit('qbsnd: %s: not the sound chips (%d bytes)' % (path, len(d)))


def render(rom, cmd, limit_s=8.0, quiet_s=0.4):
    b = Board(rom)
    b.cpu.reset()
    b.run(CLOCK // 5)                            # its power-on
    b.dac = []
    t0 = b.cpu.cyc
    b.command(cmd)
    last = t0
    while b.cpu.cyc - t0 < limit_s * CLOCK:
        n = len(b.dac)
        b.run(CLOCK // 50)
        if len(b.dac) != n:
            last = b.dac[-1][0]
        elif b.cpu.cyc - last > quiet_s * CLOCK and b.dac:
            break
    return [(c - t0, v) for c, v in b.dac], (last - t0) / CLOCK


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom', help='QBERT.ROM with its sound chips, or the 4KB of them')
    ap.add_argument('--measure', action='store_true')
    ap.add_argument('--wav')
    ap.add_argument('--rate', type=int, default=11025)
    a = ap.parse_args()
    rom = rom_of(a.rom)
    total = 0.0
    for cmd in range(1, 0x40):
        dac, secs = render(rom, cmd)
        if not dac:
            continue
        vals = len(set(v for _, v in dac))
        total += secs
        if a.measure:
            print('qbsnd: %02X  %6.2f s  %6d DAC writes  %3d values%s'
                  % (cmd, secs, len(dac), vals, '  (to the limit)' if secs > 7.9 else ''))
        if a.wav:
            os.makedirs(a.wav, exist_ok=True)
            n = int(secs * a.rate) + 1
            out = bytearray(n)
            k, v = 0, 0x80
            for i in range(n):
                c = i * CLOCK // a.rate
                while k < len(dac) and dac[k][0] <= c:
                    v = dac[k][1]
                    k += 1
                out[i] = v
            import wave
            with wave.open(os.path.join(a.wav, 'cmd%02X.wav' % cmd), 'wb') as w:
                w.setnchannels(1)
                w.setsampwidth(1)
                w.setframerate(a.rate)
                w.writeframes(bytes(out))
    print('qbsnd: %.1f s of sound in all' % total)
    return 0


if __name__ == '__main__':
    sys.exit(main())
