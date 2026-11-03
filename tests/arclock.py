#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/arclock.py - ARCADE's translated code, in LOCKSTEP with the
# reference model (docs/ARCADE-SPEC.md section 6.1)
#
#     python3 tests/arclock.py CODE.bin ROM [--frames N]
#
# CODE.bin is the code part apps/arcade/arcaot.py's output assembles to; ROM
# is the board image it was translated from. The code part runs under
# Unicorn's 8086 with the view, aux and stack laid out as the package lays
# them out, and tools/arcsim.py runs beside it. At every 8080 instruction
# boundary - every entry of the PC table the code reaches - the two must
# agree on the PC, A, BC, DE, HL and SP; on the flags wherever the 8080 is
# about to READ them (a conditional, ADC/SBB, RAL/RAR, DAA, PUSH PSW, CMC);
# and once a frame on every byte of RAM and VRAM. Interrupts go in on the
# same instruction in both, at arcsim's T-states: the translated code does
# not count cycles (the package's clock is the PC's, ARCADE-SPEC 5), so the
# harness takes arcsim's word for WHEN, which is what makes the two
# comparable at all.
#
# THE ONE KNOWN DIFFERENCE IS NOT COMPARED: AC, the 8080's half-carry, after
# a logical op or a subtract (ARCADE-SPEC 3.5). Only DAA reads it, and DAA
# here follows an ADD.
#
# Needs `pip install unicorn`. Exit 0 = every boundary agreed.
# =============================================================================
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'apps', 'arcade'))
import arcsim                       # noqa: E402
import arcaot                       # noqa: E402

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE   # noqa: E402
from unicorn import x86_const as X  # noqa: E402

CSEG, VSEG, ASEG = 0x1000, 0x2000, 0x3000
V_IN1, V_IN2, ST_INTE = 0x4019, 0x401A, 0x4011


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('code')
    ap.add_argument('rom')
    ap.add_argument('--frames', type=int, default=600)
    ap.add_argument('--seeds', default=os.path.join(ROOT, 'apps', 'arcade', 'invaders.seeds'))
    a = ap.parse_args()
    code = open(a.code, 'rb').read()
    rom = open(a.rom, 'rb').read()
    table = [code[2 * p] | (code[2 * p + 1] << 8) for p in range(0x4000)]
    miss = table[0x3FFF]
    at = {}
    for pc, off in enumerate(table):     # several PCs share an offset when
        if off != miss:                  #   the first ones translate to
            at.setdefault(off, []).append(pc)   # nothing (NOP)
    reads = {}
    for pcs in at.values():
        for pc in pcs:
            reads[pc] = arcaot.Ins(rom, pc).use

    seeds = [int(x.split('#')[0], 16) for x in open(a.seeds)
             if x.split('#')[0].strip()]
    flow = arcaot.walk(rom, seeds)
    arcaot.liveness(flow)
    arcaot.mark_polls(flow)
    idioms = arcaot.find_idioms(rom, flow)   # one 8086 loop each: the model
                                             #   runs the whole 8080 loop at
                                             #   once and interrupts at its exit

    u = Uc(UC_ARCH_X86, UC_MODE_16)
    u.mem_map(0, 0x100000)
    u.mem_write(CSEG * 16, code)
    u.mem_write(VSEG * 16, rom)
    u.mem_write(ASEG * 16, b'\x02\x00')
    u.reg_write(X.UC_X86_REG_CS, CSEG)
    for r in (X.UC_X86_REG_DS, X.UC_X86_REG_SS):
        u.reg_write(r, VSEG)
    u.reg_write(X.UC_X86_REG_ES, ASEG)
    u.reg_write(X.UC_X86_REG_SP, 0x2400)
    u.reg_write(X.UC_X86_REG_BP, 0x7FFF)
    u.reg_write(X.UC_X86_REG_DI, 2)         # the write list's next free word
    u.reg_write(X.UC_X86_REG_EFLAGS, 0x0002)
    u.mem_write(VSEG * 16 + V_IN1, bytes([0x08]))

    m = arcsim.M(rom)
    m.sp = 0x2400
    st = {'n': 0, 'fail': None, 'frame': 0, 'half': 1, 'end': arcsim.HALF,
          'redirect': None, 'check': False, 'done': False}

    def regs():
        g = u.reg_read
        ax, bx, cx, dx = (g(X.UC_X86_REG_AX), g(X.UC_X86_REG_BX),
                          g(X.UC_X86_REG_CX), g(X.UC_X86_REG_DX))
        return ax & 255, (ax >> 8) & 255, cx, dx, bx, g(X.UC_X86_REG_SP), \
            g(X.UC_X86_REG_EFLAGS) & 0xD5

    def fail(msg):
        st['fail'] = msg
        u.emu_stop()
        return True

    def hook(uc, addr, size, _):
        off = addr - CSEG * 16
        pcs = at.get(off)
        if pcs is None:
            return
        for pc in pcs:
            if pc == m.pc or st['redirect']:
                if one(uc, pc):
                    return

    def one(uc, pc):
        if st['redirect']:                  # the instruction before has run
            if pc != st['redirect'][0]:     #   on the 8086 too: deliver now
                return fail('interrupted at %04X, the model at %04X'
                            % (pc, st['redirect'][0]))
            uc.emu_stop()
            return True
        if st['check']:                     # both sides at one boundary
            st['check'] = False
            ram = bytearray(uc.mem_read(VSEG * 16 + 0x2000, 0x2000))
            ref = bytearray(m.m[0x2000:0x4000])
            for k in range(0x2300, m.sp):       # under the SP is dead, and
                if 0x2000 <= k < 0x4000:        #   our own CALLs push there
                                                #   (no data lives in 2300-
                                                #   23DF: tools/arcsim.py)
                    ram[k - 0x2000] = ref[k - 0x2000] = 0
            for k in range(m.sp, 0x2400):       # a PUSH PSW's F: AC is the
                if ram[k - 0x2000] ^ ref[k - 0x2000] == 0x10:   # one known
                    ram[k - 0x2000] = ref[k - 0x2000]           # difference
            if ram != ref:
                k = next(i for i in range(0x2000) if ram[i] != ref[i])
                return fail('frame %d: RAM/VRAM differ first at %04X: %02X, the model %02X'
                            % (st['frame'], 0x2000 + k, ram[k], ref[k]))
        uc.reg_write(X.UC_X86_REG_BP, 0x7FFF)       # no poll ever exits
        A, F, BC, DE, HL, SP, FL = regs()
        if pc != m.pc:
            return fail('PC %04X, the model is at %04X' % (pc, m.pc))
        want = (m.a, (m.b << 8) | m.c, (m.d << 8) | m.e, (m.h << 8) | m.l, m.sp)
        got = (A, BC, DE, HL, SP)
        if want != got:
            return fail('at %04X: A BC DE HL SP %s, the model %s' % (
                pc, ' '.join('%X' % v for v in got), ' '.join('%X' % v for v in want)))
        use = reads.get(pc, 0)
        if use:
            mask = (0x01 if use & arcaot.C else 0) | (0xC4 if use & arcaot.Z else 0)
            if m.op_at(pc) == 0x27:
                mask |= 0x10
            if (FL & mask) != (m.f() & mask):
                return fail('at %04X (op %02X): flags %02X, the model %02X (mask %02X)'
                            % (pc, rom[pc], FL, m.f(), mask))
        st['n'] += 1
        # the model's step, and its interrupt on the same boundary
        m.step()
        if pc in idioms:
            lo, hi = pc, pc + idioms[pc][1]
            while lo <= m.pc < hi:
                m.step()
                st['n'] += 1
        uc.reg_write(X.UC_X86_REG_DI, 2)    # the list: the harness never draws
        if m.cyc >= st['end']:
            n = st['half']
            if m.ie:
                ret = m.pc
                m.interrupt(n)
                st['redirect'] = (ret, n * 8)
            else:
                m.interrupt(n)
            st['half'] = 3 - n
            if n == 2:
                st['frame'] += 1
                m.in1 = arcsim.script(st['frame'])
                uc.mem_write(VSEG * 16 + V_IN1, bytes([m.in1]))
                st['check'] = True
                if st['frame'] >= a.frames:
                    st['done'] = True
                    uc.emu_stop()
                    return True
            st['end'] += arcsim.HALF

    arcsim.M.op_at = lambda self, pc: self.m[pc]
    u.hook_add(UC_HOOK_CODE, hook, begin=CSEG * 16, end=CSEG * 16 + len(code))
    ip = table[0]
    while True:
        u.emu_start(CSEG * 16 + ip, 0x100000, count=0)
        if st['fail']:
            print('arclock: FAIL after %d instructions, frame %d: %s'
                  % (st['n'], st['frame'], st['fail']))
            return 1
        if st['done']:
            break
        if st['redirect']:
            ret, vec = st['redirect']
            st['redirect'] = None
            # flags DEAD at the interrupted PC are garbage the translation
            # may leave (a poll's DEC, a VRAM test): legal, since nothing
            # reads them before they are set again - the handler's PUSH PSW
            # and POP PSW carry the garbage through. The model takes ours.
            live = flow[ret].lin if ret in flow else arcaot.ALL
            fl = u.reg_read(X.UC_X86_REG_EFLAGS) & 0xD5
            mf = m.f()
            keep = (0x01 if live & arcaot.C else 0) | (0xD4 if live & arcaot.Z else 0)
            m.setf((mf & keep) | (fl & ~keep & 0xD5) | 2)
            sp = (u.reg_read(X.UC_X86_REG_SP) - 2) & 0xFFFF
            u.reg_write(X.UC_X86_REG_SP, sp)
            u.mem_write(VSEG * 16 + sp, bytes([ret & 255, ret >> 8]))
            u.mem_write(VSEG * 16 + ST_INTE, b'\x00')
            ip = table[vec]
            continue
        print('arclock: the 8086 side stopped at %04X:%04X with nothing to say'
              % (u.reg_read(X.UC_X86_REG_CS), u.reg_read(X.UC_X86_REG_IP)))
        return 1
    print('arclock: %d 8080 instructions in lockstep over %d frames, all agree'
          % (st['n'], st['frame']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
