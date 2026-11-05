#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/qbnative.py - Q*bert's own 8088 code, run the way ARCADE's N88
# core runs it, under Unicorn (ARCADE-SPEC 10)
#
#     python3 tests/qbnative.py [--frames N] [--png DIR --at F,F,...]
#                               [--script F:IN1:IN4 ...] [--cycles]
#
# The program is the ROM part the package ships (build/arcade/QBROM.PRT: the
# ROM with apps/arcade/qbert.patch applied), put at A000h of a segment G that
# is not 0, with every segment register G. A run goes until the game reaches
# one of its yields (the `call far` the patch left in place of its wait for
# the frame); the host then delivers the NMI as n88_enter does - FLAGS, CS,
# IP pushed, the handler at the game's own vector [0008h] - and the next run
# goes on from the yield's return. The inputs are filled at 5800h, 5801h and
# 5804h before each run, as qb_keys fills them.
#
# What it CHECKS - the claims qbert.patch makes, which nothing else can see:
#   * no store reaches 5800-5807h, where the host keeps the inputs: every
#     write the board's I/O takes was moved to 5808h and up;
#   * the segment registers are G at every yield: the three loads were all;
#   * every frame ends at a yield within the budget (no wait was missed).
# By default it plays its own attract mode, then a coin, a start and a few
# hundred frames of moves. --png writes the board's picture as MAME draws
# it, turned, at the frames named. --cycles adds an 8088 cycle estimate a
# frame (it needs capstone; the estimate is a table, not a measurement -
# MartyPC is the measurement, tests/arcspeed.py).
#
# This was the native probe (plan section 8.2): it booted the attract mode,
# found the yields, and measured the game at 6-11k cycles a frame of the
# 77.7k a 4.77 MHz 8088 has at 61.42 Hz.
#
# Needs `pip install unicorn`. Exit 0 = every check held.
# =============================================================================
import argparse
import collections
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
os.chdir(ROOT)
import os88build                    # noqa: E402

G = 0x1000
GB = G * 16
DSW, IN1REST = 0x02, 0x40           # as apps/arcade/arcqb.inc's QB_DSW, QB_IN1REST
BUDGET = 3_000_000                  # instructions a run may take


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=3000)
    ap.add_argument('--sndlog', action='store_true',
                    help="list the bytes the game writes to its sound latch (5802h, at 580Ah)")
    ap.add_argument('--png')
    ap.add_argument('--at', default='')
    ap.add_argument('--script', nargs='*', default=None,
                    help='F:IN1:IN4 - from frame F on, those inputs')
    ap.add_argument('--cycles', action='store_true')
    a = ap.parse_args()
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_CS, UC_X86_REG_DS,
                                   UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_SP,
                                   UC_X86_REG_IP, UC_X86_REG_FLAGS)
    prt = open(os88build.at('build/arcade/QBROM.PRT'), 'rb').read()
    rom = open(os88build.at('build/arcade-rom/QBERT.ROM'), 'rb').read()
    n = struct.unpack_from('<H', prt, 0x8800)[0]
    yields = list(struct.unpack_from('<%dH' % n, prt, 0x8802))
    if a.script is None:                # the attract mode, then a game
        a.script = ['%d:0:0' % (a.frames - 600), '%d:4:0' % (a.frames - 600),
                    '%d:0:0' % (a.frames - 594), '%d:1:0' % (a.frames - 530),
                    '%d:0:0' % (a.frames - 524)]
        for i, f in enumerate(range(a.frames - 300, a.frames, 40)):
            a.script += ['%d:0:%d' % (f, (1, 8, 4, 2)[i % 4]), '%d:0:0' % (f + 8)]
    script = {}
    for s in a.script:
        f, i1, i4 = s.split(':')
        script[int(f)] = (int(i1, 0), int(i4, 0))
    shots = set(int(x) for x in a.at.split(',') if x)

    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(0, 0x100000)
    uc.mem_write(GB + 0xA000, prt[0:0x6000])
    for r in (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
        uc.reg_write(r, G)
    st = {'stop': None, 'bad': []}

    def at_yield(uc_, addr, size, _):
        st['stop'] = addr - GB
        uc_.emu_stop()

    def stray(uc_, access, addr, size, value, _):
        o = addr - GB
        if 0x5800 <= o <= 0x5807:
            ip = uc_.reg_read(UC_X86_REG_IP)
            st['bad'].append('a store of %02X at %04X, from %04X' % (value & 0xFF, o, ip))
    for y in yields:
        uc.hook_add(UC_HOOK_CODE, at_yield, None, GB + y, GB + y)
    uc.hook_add(UC_HOOK_MEM_WRITE, stray, None, GB + 0x5800, GB + 0x5807)
    snd = []

    def sound(uc_, access, addr, size, value, _):
        snd.append((st.get('frame', 0), value & 0xFF))
    if a.sndlog:
        uc.hook_add(UC_HOOK_MEM_WRITE, sound, None, GB + 0x580A, GB + 0x580A)

    cyc = None
    if a.cycles:
        cyc = Cycles(uc, GB)

    def inputs(i1, i4):
        uc.mem_write(GB + 0x5800, bytes([DSW, i1 | IN1REST, 0, 0, i4]))

    ip = 0xA000
    i1 = i4 = 0
    per = []
    for f in range(a.frames):
        st['frame'] = f
        if f in script:
            i1, i4 = script[f]
        inputs(i1, i4)
        if f:                               # the NMI, as n88_enter delivers it
            sp = uc.reg_read(UC_X86_REG_SP) - 6
            fl = uc.reg_read(UC_X86_REG_FLAGS)
            uc.mem_write(GB + sp, struct.pack('<HHH', ip, G, fl))
            uc.reg_write(UC_X86_REG_SP, sp)
            uc.reg_write(UC_X86_REG_FLAGS, fl & ~0x0300)
            ip = struct.unpack('<H', bytes(uc.mem_read(GB + 8, 2)))[0]
        st['stop'] = None
        c0 = cyc.total if cyc else 0
        uc.emu_start(GB + ip, GB + 0x10000, count=BUDGET)
        if st['stop'] is None:
            print('qbnative: FAIL - frame %d ran %d instructions and reached no '
                  'yield (IP %04X)' % (f, BUDGET, uc.reg_read(UC_X86_REG_IP)))
            return 1
        for r, name in ((UC_X86_REG_CS, 'CS'), (UC_X86_REG_DS, 'DS'),
                        (UC_X86_REG_ES, 'ES'), (UC_X86_REG_SS, 'SS')):
            if uc.reg_read(r) != G:
                st['bad'].append('frame %d: %s is %04X at the yield' % (f, name, uc.reg_read(r)))
        ip = st['stop'] + 6                 # the yield returns past its NOP
        if cyc:
            per.append(cyc.total - c0)
        if f in shots and a.png:
            picture(uc, rom, os.path.join(a.png, 'qb%04d.png' % f))
        if st['bad']:
            break
    if a.sndlog:
        from collections import Counter
        cpf = Counter(fr for fr, v in snd if v & 0x0F != 0x0F)
        print('qbnative: %d writes to the sound latch; the commands (low nibble not F): %s'
              % (len(snd), ' '.join('%02X' % v for v in sorted(set(v for _, v in snd
                                                              if v & 0x0F != 0x0F)))))
        print('qbnative: frames with more than one command: %d of %d'
              % (sum(1 for n in cpf.values() if n > 1), len(cpf)))
        print('qbnative: the first 40: %s' % ' '.join('%d:%02X' % fv for fv in snd[:40]))
    for b in st['bad'][:10]:
        print('qbnative: FAIL - ' + b)
    if st['bad']:
        return 1
    print('qbnative: %d frames, every one to a yield, no store on the inputs, '
          'the segments G throughout' % a.frames)
    if per:
        srt = sorted(per[1:])
        print('qbnative: 8088 cycles a frame (a table\'s estimate): median %d, 90%% %d, '
              'max %d; a 4.77 MHz 8088 has %d at 61.42 Hz'
              % (srt[len(srt) // 2], srt[len(srt) * 9 // 10], srt[-1], 4772727 // 61.42))
    print('qbnative: ok')
    return 0


class Cycles:
    """An 8088 cycle ESTIMATE per basic block (capstone, a table): the
    probe's instrument, not the package's measurement."""

    def __init__(self, uc, gb):
        import capstone
        from unicorn import UC_HOOK_BLOCK
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
        self.md.detail = True
        self.cap = capstone
        self.total = 0
        self.memo = {}
        self.gb = gb
        uc.hook_add(UC_HOOK_BLOCK, self.block)

    REP = {'movsb': 17, 'movsw': 25, 'stosb': 10, 'stosw': 14, 'lodsb': 13,
           'lodsw': 17, 'cmpsb': 22, 'cmpsw': 30, 'scasb': 15, 'scasw': 19}

    def block(self, uc, addr, size, _):
        c = self.memo.get((addr, size))
        if c is None:
            c = 0
            for ins in self.md.disasm(bytes(uc.mem_read(addr, size)), addr):
                c += self.cost(ins)
            self.memo[(addr, size)] = c
        if size and uc.mem_read(addr, 1)[0] in (0xF2, 0xF3):
            ins = next(self.md.disasm(bytes(uc.mem_read(addr, size)), addr))
            c += self.REP.get(ins.mnemonic.split()[-1], 20)    # one a pass
        self.total += c

    def cost(self, ins):
        X = self.cap.x86
        m = ins.mnemonic
        ops = ins.operands
        w = any(o.size == 2 for o in ops)
        mem = [o for o in ops if o.type == X.X86_OP_MEM]
        if mem:
            mm = mem[0].mem
            e = 6 if not mm.base and not mm.index else \
                ((7 if not mm.disp else 11) if mm.base and mm.index else (5 if not mm.disp else 9))
            e += 2 if mm.segment else 0
        else:
            e = 0
        wp = 4 if w else 0
        dst = bool(ops) and ops[0].type == X.X86_OP_MEM
        imm = any(o.type == X.X86_OP_IMM for o in ops)
        if m == 'mov':
            c = (4 if imm else 2) if not mem else ((10 if imm else 9) + e + wp if dst else 8 + e + wp)
        elif m in ('add', 'sub', 'and', 'or', 'xor', 'adc', 'sbb'):
            c = (4 if imm else 3) if not mem else ((17 if imm else 16) + e + 2 * wp if dst else 9 + e + wp)
        elif m in ('cmp', 'test'):
            c = (4 if imm else 3) if not mem else 10 + e + wp
        elif m in ('inc', 'dec', 'not', 'neg'):
            c = (2 if w else 3) if not mem else 15 + e + 2 * wp
        elif m in ('shl', 'shr', 'sar', 'rol', 'ror', 'rcl', 'rcr', 'sal'):
            c = 2 if not mem else 15 + e + 2 * wp
        elif m == 'push':
            c = 15 if not mem else 24 + e
        elif m == 'pop':
            c = 12 if not mem else 25 + e
        elif m == 'call':
            c = 23 if not mem else 29 + e
        elif m == 'ret':
            c = 20
        elif m == 'iret':
            c = 44
        elif m == 'jmp':
            c = 15 if not mem else 22 + e
        elif m.startswith('j') or m.startswith('loop'):
            c = 11
        elif m in ('mul', 'imul'):
            c = 118 if w else 77
        elif m in ('div', 'idiv'):
            c = 162 if w else 90
        elif m == 'lea':
            c = 2 + e
        elif m.startswith('rep'):
            c = 9
        else:
            c = 4
        return max(c, 4 * ins.size)


def picture(uc, rom, path):
    """The board's picture as MAME's gottlieb driver draws it, turned."""
    sys.path.insert(0, os.path.join(ROOT, 'tests'))
    from qbglass import reference
    g = bytes(uc.mem_read(GB, 0x10000))
    want = reference(rom, g)
    pal = g[0x5000:0x5020]
    out = bytearray()
    for r in range(256):
        for c in range(240):
            i = want[r][c]
            e, o = pal[2 * i], pal[2 * i + 1]
            out += bytes(((o & 15) * 17, (e >> 4) * 17, (e & 15) * 17))
    import os88marty
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    os88marty.write_png_rgb(path, 240, 256, bytes(out))


if __name__ == '__main__':
    sys.exit(main())
