#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/m6502.py - ARCADE's 6502 core (apps/arcade/arc6502.inc),
# run under Unicorn, held against tools/qbsnd.py's CPU6502
#
#     python3 tests/m6502.py [--n N] [--seed S] [--codes 1,2,...] [--quick]
#
# tools/qbsnd.py is the behavioural reference for Q*bert's sound CPU, and the
# 8086 core is meant to run its ROMs LIVE, so the two must agree to the cycle.
# The core is assembled with tests/m6502t.asm (a header of offsets at 0 and
# two hooks the harness answers) into a temp dir and run in a segment of its
# own; the 6502's memory is another. Both CPUs get THE SAME bus: plain bytes
# at 0000-01FF and 6000-7FFF, A15 ignored, everything else I/O through a
# handler that logs (cycle, address, value) - the map arc6502.inc's header
# states - so a difference is the core's, never the bus's.
#
#   a. ONE INSTRUCTION, FUZZED: every opcode in CPU6502's table, N random
#      states each (A X Y S P - decimal included - the IRQ line, operands and
#      pointers aimed at plain memory, at I/O and across pages, A15 set or
#      not), one m6_run of budget 1 against one step(). Compared: A X Y S P
#      PC, the cycles (and the debt that budget 1 leaves), all plain memory,
#      every I/O access with the cycle it saw, and the 8086 registers the
#      core must keep. Plus: every undocumented opcode is a counted 2-cycle
#      NOP of length 1, and a budget the debt swallows runs nothing.
#   b. THE SOUND ROMS: qb-snd1/2.bin out of build/arcade-rom/qbert.zip at
#      7000h/7800h, both CPUs from reset in chunks of 112 cycles (the line
#      refreshed from each side's own qbsnd.Riot between chunks, the same
#      way on both), 0.2 s of power-on, a command strobed as Board.command
#      does, then 200,000 cycles. Compared: every DAC write (cycle, value)
#      and the end state. No zip = SKIP, never a pass.
#
# It also prints the core's size and, from part b's first command, x86
# instructions per 6502 step and a rough 286 cycle estimate from the NASM
# listing (a table, not a measurement: Unicorn has no clock).
#
# Needs `pip install unicorn`. Exit 0 = every part that ran matched and none
# was skipped silently.
# =============================================================================
import argparse
import os
import random
import re
import struct
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import qbsnd                        # noqa: E402

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_BLOCK  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX,  # noqa: E402
                               UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI,
                               UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_CS,
                               UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS)

PSEG, MSEG, SSEG = 0x1000, 0x3000, 0x5000
PB, MB = PSEG * 16, MSEG * 16
ESX = 0x7123                        # ES on the way in: the core must give it back
PLAIN = ((0x0000, 0x0200), (0x6000, 0x8000))
KEEP = {UC_X86_REG_AX: 0x1357, UC_X86_REG_BX: 0x2468, UC_X86_REG_CX: 0x369C,
        UC_X86_REG_DX: 0x48D1, UC_X86_REG_SI: 0x5AE2, UC_X86_REG_DI: 0x6BF3,
        UC_X86_REG_BP: 0x7C04}
NAMES = {UC_X86_REG_AX: 'AX', UC_X86_REG_BX: 'BX', UC_X86_REG_CX: 'CX',
         UC_X86_REG_DX: 'DX', UC_X86_REG_SI: 'SI', UC_X86_REG_DI: 'DI',
         UC_X86_REG_BP: 'BP', UC_X86_REG_ES: 'ES', UC_X86_REG_SP: 'SP'}
MODES = {}                          # opcode -> qbsnd.py's mode name


def assemble(tmp, defs=(), inc=None):
    out = os.path.join(tmp, 'm6502t.bin')
    lst = os.path.join(tmp, 'm6502t.lst')
    subprocess.run(['nasm', '-f', 'bin', '-w+error'] + ['-D' + d for d in defs] +
                   (['-I', os.path.join(os.path.abspath(inc), '')] if inc else []) + [
                    '-I', os.path.join(ROOT, 'apps', 'arcade') + '/',
                    '-I', HERE + '/', os.path.join(HERE, 'm6502t.asm'),
                    '-o', out, '-l', lst], check=True)
    return open(out, 'rb').read(), open(lst).read()


class MendedCPU(qbsnd.CPU6502):
    """CPU6502 with the cycles its branch() adds kept: `self.cyc += f()`
    reads self.cyc BEFORE f() runs, so the taken and page-cross cycles
    branch() adds are overwritten (arc6502.inc's M6_BR). The reference for
    a core built -DM6_BRANCH_PENALTY."""

    def step(self):
        if self.irq_line:
            if self.irq():
                return
        op = self.rd(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        f = self.ops.get(op)
        if f is None:
            raise RuntimeError('6502: opcode %02X at %04X' % (op, (self.pc - 1) & 0xFFFF))
        c = f()
        self.cyc += c
        self.pc &= 0xFFFF


# --- the bus both CPUs see ---------------------------------------------------
class FuzzIO:
    """I/O for part a: reads answer a fixed random sequence, every access is
    logged with the cycle the CPU said it happened at."""

    def __init__(self, vals):
        self.vals = vals
        self.log = []

    def read(self, a, cyc):
        v = self.vals[len(self.log) % len(self.vals)]
        self.log.append(('r', a, v, cyc & 0xFFFFFFFF))
        return v

    def write(self, a, v, cyc):
        self.log.append(('w', a, v, cyc & 0xFFFFFFFF))


class BoardIO:
    """I/O for part b: what qbsnd.Board decodes in 0200-5FFF - the 6532's
    registers (and its RAM where the board aliases it), the DAC at 1000h -
    with a qbsnd.Riot of its own whose clock is `cyc`."""

    def __init__(self, cyc):
        self.riot = qbsnd.Riot(cyc)
        self.dac = []
        self.speech = 0

    def read(self, a, cyc):
        b = a & 0x0FFF
        if a < 0x1000:
            if b & 0x0280 == 0:
                return self.riot.ram[b & 0x7F]
            if b & 0x0280 == 0x0200:
                return self.riot.read(b & 0x1F)
        return 0xFF

    def write(self, a, v, cyc):
        b = a & 0x0FFF
        if a < 0x1000:
            if b & 0x0280 == 0:
                self.riot.ram[b & 0x7F] = v
            elif b & 0x0280 == 0x0200:
                self.riot.write(b & 0x1F, v)
        elif a < 0x2000:
            self.dac.append((cyc, v))
        elif a < 0x3000:
            self.speech += 1


class FlatBus:
    """qbsnd.CPU6502's bus, with arc6502.inc's map."""

    def __init__(self, mem, io_):
        self.mem = mem
        self.io = io_
        self.cpu = None

    def read(self, a):
        a &= 0x7FFF
        if a < 0x200 or a >= 0x6000:
            return self.mem[a]
        return self.io.read(a, self.cpu.cyc)

    def write(self, a, v):
        a &= 0x7FFF
        if a < 0x200:
            self.mem[a] = v
        elif a < 0x6000:
            self.io.write(a, v, self.cpu.cyc)


# --- the 8086 side -----------------------------------------------------------
class Core:
    def __init__(self, img, lst):
        assert img[:4] == b'M6T1', 'm6502t.bin: no header'
        h = struct.unpack_from('<19H', img, 4)
        (self.e_reset, self.e_run, self.e_stop, self.h_ior, self.h_iow, self.ioval,
         self.v_a, self.v_x, self.v_y, self.v_s, self.v_p, self.v_pc, self.v_cyc,
         self.v_seg, self.v_irq, self.v_debt, self.v_bad, self.core0, self.core1) = h
        self.size = self.core1 - self.core0
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x100000)
        uc.mem_write(PB, img)
        self.w16(self.v_seg, MSEG)
        self.io = None
        self.bad = []
        uc.hook_add(UC_HOOK_CODE, self._ior, None, PB + self.h_ior, PB + self.h_ior)
        uc.hook_add(UC_HOOK_CODE, self._iow, None, PB + self.h_iow, PB + self.h_iow)
        self.lst = listing(lst, self.core0, self.core1)
        self.counting = False
        self.n_ins = 0
        self.n_cyc = 0
        self._last = None
        self._memo = {}
        uc.hook_add(UC_HOOK_BLOCK, self._block, None, PB + self.core0, PB + self.core1 - 1)

    def r8(self, o):
        return self.uc.mem_read(PB + o, 1)[0]

    def w8(self, o, v):
        self.uc.mem_write(PB + o, bytes([v & 0xFF]))

    def r16(self, o):
        return struct.unpack('<H', self.uc.mem_read(PB + o, 2))[0]

    def w16(self, o, v):
        self.uc.mem_write(PB + o, struct.pack('<H', v & 0xFFFF))

    def r32(self, o):
        return struct.unpack('<I', self.uc.mem_read(PB + o, 4))[0]

    def w32(self, o, v):
        self.uc.mem_write(PB + o, struct.pack('<I', v & 0xFFFFFFFF))

    def _ior(self, uc, addr, size, _):
        bx = uc.reg_read(UC_X86_REG_BX)
        if not 0x0200 <= bx < 0x6000:
            self.bad.append('m6_ior with BX = %04X' % bx)
        v = self.io.read(bx, self.r32(self.v_cyc))
        uc.mem_write(PB + self.ioval, bytes([v]))

    def _iow(self, uc, addr, size, _):
        bx = uc.reg_read(UC_X86_REG_BX)
        if not 0x0200 <= bx < 0x6000:
            self.bad.append('m6_iow with BX = %04X' % bx)
        self.io.write(bx, uc.reg_read(UC_X86_REG_AX) & 0xFF, self.r32(self.v_cyc))

    def _block(self, uc, addr, size, _):
        if not self.counting:
            return
        if self._last is not None and self._last[0] == 'jcc' and addr != self._last[1]:
            self.n_cyc += 4                 # the last block's jcc was taken
        k = (addr, size)
        m = self._memo.get(k)
        if m is None:
            n = c = 0
            kind = None
            for o in range(addr - PB, addr - PB + size):
                ins = self.lst.get(o)
                if ins:
                    n += 1
                    c += ins[0]
                    kind = ins[1]
            m = self._memo[k] = (n, c, kind, addr + size)
        self.n_ins += m[0]
        self.n_cyc += m[1]
        self._last = (m[2], m[3])

    def call(self, entry, ax=0):
        uc = self.uc
        for r, v in KEEP.items():
            uc.reg_write(r, v)
        uc.reg_write(UC_X86_REG_AX, ax)
        for r in (UC_X86_REG_CS, UC_X86_REG_DS):
            uc.reg_write(r, PSEG)
        uc.reg_write(UC_X86_REG_SS, SSEG)
        uc.reg_write(UC_X86_REG_SP, 0xFFF0)
        uc.reg_write(UC_X86_REG_ES, ESX)
        self._last = None
        uc.emu_start(PB + entry, PB + self.e_stop, count=5_000_000)
        want = dict(KEEP)
        want[UC_X86_REG_AX] = ax
        want[UC_X86_REG_ES] = ESX
        want[UC_X86_REG_SP] = 0xFFF0
        for r, v in want.items():
            got = uc.reg_read(r)
            if got != v:
                self.bad.append('%s %04X, was %04X' % (NAMES[r], got, v))

    def reset(self):
        self.call(self.e_reset)

    def run(self, budget):
        self.call(self.e_run, budget)

    def state(self):
        return dict(a=self.r8(self.v_a), x=self.r8(self.v_x), y=self.r8(self.v_y),
                    s=self.r8(self.v_s), p=self.r8(self.v_p), pc=self.r16(self.v_pc),
                    cyc=self.r32(self.v_cyc))

    def set_state(self, a, x, y, s, p, pc, cyc):
        self.w8(self.v_a, a)
        self.w8(self.v_x, x)
        self.w8(self.v_y, y)
        self.w8(self.v_s, s)
        self.w8(self.v_p, p)
        self.w16(self.v_pc, pc)
        self.w32(self.v_cyc, cyc)


# A rough 80286 cost table, real mode, the queue assumed full: what a
# listing line costs. Enough to say "about N cycles a 6502 instruction".
def cost286(text):
    t = text.split(';')[0].strip().lower()
    if not t:
        return None
    m = t.split()[0]
    ops = t[len(m):].strip()
    mem = '[' in ops
    parts = [p.strip() for p in ops.split(',')] if ops else []
    dst_mem = bool(parts) and '[' in parts[0]
    imm = len(parts) == 2 and re.match(r'^(0x[0-9a-f]+|\d+|[0-9a-f]+h)$', parts[1]) is not None
    if m in ('db', 'dw', 'dd', 'hlt'):
        return None
    if m == 'mov':
        return (3 if dst_mem else 5) if mem else 2, None
    if m in ('add', 'sub', 'and', 'or', 'xor', 'adc', 'sbb'):
        return (7 if mem else (3 if imm else 2)), None
    if m in ('cmp', 'test'):
        return (6 if mem else (3 if imm else 2)), None
    if m in ('inc', 'dec', 'neg', 'not'):
        return (7 if mem else 2), None
    if m in ('shl', 'shr', 'rcl', 'rcr', 'sal', 'sar', 'rol', 'ror'):
        return 2, None
    if m in ('lahf', 'sahf', 'cbw', 'cmc', 'clc', 'stc', 'nop'):
        return 2, None
    if m == 'xchg':
        return 3, None
    if m == 'push':
        return 3, None
    if m == 'pop':
        return 5, None
    if m == 'call':
        return 7, None
    if m == 'ret':
        return 11, None
    if m == 'jmp':
        return (11 if mem else 7), None
    if m.startswith('j'):
        return 3, 'jcc'                 # + 4 when taken (Core._block)
    return 5, None


def listing(text, lo, hi):
    """offset -> (286 cycles, kind) for every instruction the listing puts
    in [lo, hi)."""
    out = {}
    pat = re.compile(r'^\s*\d+\s+([0-9A-F]{8})\s+[0-9A-F\[\]()]+-?\s+(?:<\d+>\s*)?(.*)$')
    for line in text.splitlines():
        m = pat.match(line)
        if not m:
            continue
        o = int(m.group(1), 16)
        if not lo <= o < hi:
            continue
        src = m.group(2)
        if re.match(r'^\s*[\w.%@$]+:', src):
            src = src.split(':', 1)[1]
        c = cost286(src)
        if c:
            out[o] = c
    return out


# --- part a ------------------------------------------------------------------
def rand_addr(rng):
    r = rng.random()
    if r < 0.25:
        a = rng.randrange(0x0000, 0x0100)
    elif r < 0.40:
        a = rng.randrange(0x0100, 0x0200)
    elif r < 0.65:
        a = rng.randrange(0x0200, 0x6000)
    else:
        a = rng.randrange(0x6000, 0x8000)
    if rng.random() < 0.3:
        a = (a & 0xFF00) | rng.randrange(0xF0, 0x100)
    if rng.random() < 0.3:
        a |= 0x8000
    return a


def mode_of(cpu):
    """opcode -> mode, read off qbsnd.py's own table so the fuzz follows it."""
    names = {cpu.imm: 'imm', cpu.zp: 'zp', cpu.zpx: 'zpx', cpu.zpy: 'zpy',
             cpu.ab: 'ab', cpu.abx: 'abx', cpu.aby: 'aby', cpu.izx: 'izx',
             cpu.izy: 'izy'}
    out = {}
    for op, f in cpu.ops.items():
        mode = None
        for cell in (f.__closure__ or ()):
            v = cell.cell_contents
            if callable(v) and v in names:
                mode = names[v]
        out[op] = mode
    out[0x4C] = out[0x20] = 'ab'
    out[0x6C] = 'ind'
    return out


def one(core, cpu, rng, op, mode):
    mem = bytearray(0x10000)
    mem[0x0000:0x0200] = rng.randbytes(0x200)
    mem[0x6000:0x8000] = rng.randbytes(0x2000)
    if rng.random() < 0.8:
        pc = rng.randrange(0x6000, 0x7FFD)
    else:
        pc = rng.randrange(0x0000, 0x01FD)
    if rng.random() < 0.3:
        pc = (pc & 0xFF00) | rng.randrange(0x80, 0xFD)
    a, x, y, s, p = (rng.randrange(256) for _ in range(5))
    if mode in ('ab', 'abx', 'aby', 'ind'):
        ea = rand_addr(rng)
        if mode == 'ind' and rng.random() < 0.3:
            ea = (ea & 0xFF00) | 0xFF   # the page-wrap bug
        opnd = [ea & 0xFF, ea >> 8]
    else:
        opnd = [rng.randrange(256), rng.randrange(256)]
    if mode in ('izx', 'izy'):
        z = (opnd[0] + (x if mode == 'izx' else 0)) & 0xFF
        ea = rand_addr(rng)
        mem[z] = ea & 0xFF
        mem[(z + 1) & 0xFF] = ea >> 8
    if mode in ('zp', 'zpx', 'zpy') and rng.random() < 0.2:
        opnd[0] = 0xFF                  # zero-page index wrap
    mem[pc] = op
    mem[pc + 1] = opnd[0]
    mem[pc + 2] = opnd[1]
    irq = rng.random() < 0.1
    cyc = rng.choice((rng.randrange(1 << 32), 0xFFFFFFFF - rng.randrange(8),
                      rng.randrange(0x10000)))
    pcx = pc | (0x8000 if rng.random() < 0.3 else 0)
    vals = list(rng.randbytes(8))

    # the reference
    io_p = FuzzIO(vals)
    bus = FlatBus(bytearray(mem), io_p)
    cpu.bus = bus
    bus.cpu = cpu
    cpu.a, cpu.x, cpu.y, cpu.s, cpu.p, cpu.pc, cpu.cyc = a, x, y, s, p, pcx, cyc
    cpu.irq_line = irq
    cpu.step()
    ref = dict(a=cpu.a, x=cpu.x, y=cpu.y, s=cpu.s, p=cpu.p & 0xFF, pc=cpu.pc & 0xFFFF,
               cyc=cpu.cyc & 0xFFFFFFFF)
    spent = cpu.cyc - cyc

    # the core
    io_c = FuzzIO(vals)
    core.io = io_c
    for lo, hi in PLAIN:
        core.uc.mem_write(MB + lo, bytes(mem[lo:hi]))
    core.set_state(a, x, y, s, p, pcx, cyc)
    core.w8(core.v_irq, 1 if irq else 0)
    core.w16(core.v_debt, 0)
    core.run(1)
    got = core.state()
    why = []
    for k in ('a', 'x', 'y', 's', 'p', 'pc', 'cyc'):
        if got[k] != ref[k]:
            why.append('%s %X want %X' % (k, got[k], ref[k]))
    if core.r16(core.v_debt) != spent - 1:
        why.append('debt %d want %d' % (core.r16(core.v_debt), spent - 1))
    for lo, hi in PLAIN:
        g = bytes(core.uc.mem_read(MB + lo, hi - lo))
        if g != bytes(bus.mem[lo:hi]):
            d = next(i for i in range(hi - lo) if g[i] != bus.mem[lo + i])
            why.append('mem %04X = %02X want %02X' % (lo + d, g[d], bus.mem[lo + d]))
    if io_c.log != io_p.log:
        why.append('io %s want %s' % (fmt_log(io_c.log), fmt_log(io_p.log)))
    if core.bad:
        why += core.bad
        core.bad = []
    # A budget the debt swallows runs nothing and shrinks the debt.
    if not why and spent > 1 and rng.random() < 0.1:
        core.run(1)
        if core.state() != got or core.r16(core.v_debt) != spent - 2:
            why.append('a swallowed budget ran something (debt %d)' % core.r16(core.v_debt))
    if why:
        return 'A%02X X%02X Y%02X S%02X P%02X PC%04X irq%d op %02X %02X %02X: %s' % (
            a, x, y, s, p, pcx, irq, op, opnd[0], opnd[1], '; '.join(why))
    return None


def fmt_st(st):
    return ' '.join('%s=%X' % kv for kv in st.items())


def fmt_log(log):
    return ' '.join('%s%04X=%02X@%d' % e for e in log) or '-'


def part_a(core, n, seed, CPU, tag):
    cpu = CPU(None)
    modes = mode_of(cpu)
    rng = random.Random(seed)
    sentinel = bytes(rng.randbytes(0x10000))
    core.uc.mem_write(MB, sentinel)
    fails = {}
    for op in sorted(cpu.ops):
        for _ in range(n):
            r = one(core, cpu, rng, op, modes[op])
            if r:
                fails.setdefault(op, []).append(r)
                if len(fails[op]) >= 3:
                    break
    # undocumented: a counted one-byte 2-cycle NOP
    undoc = [op for op in range(256) if op not in cpu.ops]
    core.w16(core.v_bad, 0)
    for op in undoc:
        pc = 0x6100
        core.uc.mem_write(MB + pc, bytes([op, 0xA9, 0x55]))
        core.set_state(0x11, 0x22, 0x33, 0xF0, 0x24, pc, 1000)
        core.w8(core.v_irq, 0)
        core.w16(core.v_debt, 0)
        core.run(1)
        st = core.state()
        want = dict(a=0x11, x=0x22, y=0x33, s=0xF0, p=0x24, pc=pc + 1, cyc=1002)
        if st != want or core.r16(core.v_debt) != 1:
            fails.setdefault(op, []).append('undocumented: %s' % st)
    if core.r16(core.v_bad) != len(undoc):
        fails.setdefault(-1, []).append('m6_bad = %d after %d undocumented' %
                                        (core.r16(core.v_bad), len(undoc)))
    # nothing the core was not asked to touch: I/O space and 8000-FFFF
    after = bytes(core.uc.mem_read(MB, 0x10000))
    for lo, hi in ((0x0200, 0x6000), (0x8000, 0x10000)):
        if after[lo:hi] != sentinel[lo:hi]:
            d = next(i for i in range(lo, hi) if after[i] != sentinel[i])
            fails.setdefault(-1, []).append('a stray write at %04X' % d)
    for op, rs in sorted(fails.items()):
        for r in rs[:3]:
            print('m6502: a%s: %s %s' % (tag, '--' if op < 0 else '%02X' % op, r))
    tot = len(cpu.ops) * n
    if fails:
        print('m6502: a%s FAIL - %d opcode(s) differ from %s: %s' % (
            tag, len(fails), CPU.__name__, ' '.join('%02X' % o for o in sorted(fails) if o >= 0)))
        return False
    print('m6502: a%s ok - %d opcodes x %d random states, one instruction each, '
          'match %s to the cycle; %d undocumented are counted NOPs'
          % (tag, len(cpu.ops), n, CPU.__name__, len(undoc)))
    return True


# --- part b ------------------------------------------------------------------
def sound_rom():
    from os88build import at
    z = at('build/arcade-rom/qbert.zip')
    if not os.path.exists(z):
        return None
    with zipfile.ZipFile(z) as f:
        return f.read('qb-snd1.bin') + f.read('qb-snd2.bin')


def session(core, rom, code, count, CPU, boot=qbsnd.CLOCK // 5, after=200_000, chunk=112):
    img = bytearray(0x10000)
    img[0x6000:0x7000] = b'\xFF' * 0x1000   # what the board reads at 6000-6FFF
    img[0x7000:0x8000] = rom
    # the reference
    io_p = BoardIO(None)
    bus = FlatBus(bytearray(img), io_p)
    cpu = CPU(bus)
    bus.cpu = cpu
    io_p.riot.cyc = lambda: cpu.cyc
    cpu.reset()
    # the core
    io_c = BoardIO(None)
    io_c.riot.cyc = lambda: core.r32(core.v_cyc)
    core.io = io_c
    core.uc.mem_write(MB, bytes(img))
    core.set_state(0, 0, 0, 0, 0, 0, 0)
    core.w16(core.v_bad, 0)
    core.reset()
    target = 0
    steps = 0
    end = boot + after
    commanded = False
    while target < end:
        if not commanded and target >= boot:
            for io_ in (io_p, io_c):
                io_.riot.strobe((code & 0x3F) | 0xC0)
                io_.riot.strobe((code & 0x3F) | 0x40)
            commanded = True
        target += chunk
        cpu.irq_line = io_p.riot.irq()
        while cpu.cyc < target:
            cpu.step()
            steps += 1
        core.w8(core.v_irq, 1 if io_c.riot.irq() else 0)
        core.counting = count
        core.run(chunk)
        core.counting = False
        st = core.state()
        ref = dict(a=cpu.a, x=cpu.x, y=cpu.y, s=cpu.s, p=cpu.p & 0xFF, pc=cpu.pc,
                   cyc=cpu.cyc)
        if st != ref or core.bad:
            return ('the states part at cycle %d: %s want %s %s'
                    % (target, fmt_st(st), fmt_st(ref), core.bad)), io_p, io_c, steps
    if io_c.dac != io_p.dac:
        k = next((i for i in range(min(len(io_c.dac), len(io_p.dac)))
                  if io_c.dac[i] != io_p.dac[i]), min(len(io_c.dac), len(io_p.dac)))
        return ('DAC write %d: %s want %s' % (k, io_c.dac[k:k + 1], io_p.dac[k:k + 1]),
                io_p, io_c, steps)
    return None, io_p, io_c, steps


def part_b(core, codes, CPU, tag):
    rom = sound_rom()
    if rom is None:
        print('m6502: b SKIP - no build/arcade-rom/qbert.zip, so the sound ROMs were not run')
        return None
    ok = True
    first = True
    for code in codes:
        core.n_ins = core.n_cyc = 0
        err, io_p, io_c, steps = session(core, rom, code, first and not tag, CPU)
        if err:
            print('m6502: b%s FAIL - command %02X: %s' % (tag, code, err))
            ok = False
            continue
        print('m6502: b%s command %02X: %d DAC writes, %d 6502 steps, identical'
              % (tag, code, len(io_p.dac), steps))
        if first and steps and not tag:
            ipi = core.n_ins / steps
            cpi = core.n_cyc / steps
            rate = steps / ((qbsnd.CLOCK // 5 + 200_000) / qbsnd.CLOCK)
            print('m6502: speed - %.1f x86 instructions and ~%.0f 286 cycles (a table, '
                  'not a measurement) a 6502 step; at %.0f steps/s of real time that is '
                  '~%.1f MHz of 286' % (ipi, cpi, rate, cpi * rate / 1e6))
            first = False
    print('m6502: b%s %s - the sound ROMs, %d command(s), every DAC write compared to '
          'the cycle against %s' % (tag, 'ok' if ok else 'FAIL', len(codes), CPU.__name__))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--n', type=int, default=300, help='random states an opcode (part a)')
    ap.add_argument('--seed', type=int, default=6502)
    ap.add_argument('--inc', help='a directory whose arc6502.inc is used instead '
                    '(an A/B, or a core broken on purpose)')
    ap.add_argument('--codes', default='1,4,9,E,F,17,1A,21,25,28',
                    help='sound commands for part b (hex)')
    ap.add_argument('--quick', action='store_true', help='part a only (says so: not a pass)')
    a = ap.parse_args()
    codes = [int(c, 16) for c in a.codes.split(',')]
    # The core as shipped, against qbsnd.CPU6502 as it runs; then the core
    # -DM6_BRANCH_PENALTY against MendedCPU (a line of M6_BR's comment).
    variants = (((), qbsnd.CPU6502, ''), (('M6_BRANCH_PENALTY',), MendedCPU, '+'))
    ok, skipped = True, False
    for defs, CPU, tag in variants:
        with tempfile.TemporaryDirectory() as tmp:
            img, lst = assemble(tmp, defs, a.inc)
        core = Core(img, lst)
        print('m6502: the core%s is %d bytes (arc6502.inc%s, the 512-byte dispatch '
              'table included)' % (tag, core.size, ' -D' + defs[0] if defs else ''))
        ok &= part_a(core, a.n, a.seed, CPU, tag)
        if a.quick:
            skipped = True
            continue
        rb = part_b(core, codes if not tag else codes[:3], CPU, tag)
        if rb is None:
            skipped = True
        else:
            ok &= rb
    if not ok:
        print('m6502: FAIL')
        return 1
    if skipped:
        print('m6502: part b did not run - not a pass')
        return 1
    print('m6502: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
