#!/usr/bin/env python3
"""pdxj_uc - the REAL PDXJ.MOD (cc8086's output, nasm'd) run in Unicorn's real-mode
x86 and held to the host build, row for row (PAGEDESK-SPEC 20.22.13.8).

    python3 apps/pagedesk/hosttest/pdxj_uc.py [-k substr] [--count] [--kb N] [--eng]

The host build proves the algorithm; this proves the 16-bit module: the compiler's
arithmetic, the segment discipline (SS != DS, far workspace) and the door. The
codestream is passed inline, or with --eng through a stand-in PDENG door
(hosttest/pdxj_engstub.asm: STRM_OPEN with accept = PDSA_CODEC, STRM_READ into the
module's buffer, STRM_CLOSE), which exercises the module's engine path and its
register contract; the real PDENG is the lead's integration test.
--count hooks every instruction and prints, per fixture, instructions, bytes
fetched and the 8088 cycle estimate (docs in PAGEDESK-SPEC 20.22.13.10).
Exits 77 without Unicorn, and 77 when build/PDXJ.MOD is not built (make pdxj).
"""
import json, os, struct, subprocess, sys
try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_CS,
                                   UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_SP, UC_X86_REG_BP,
                                   UC_X86_REG_EFLAGS, UC_X86_REG_IP)
except ImportError:
    print("pdxj_uc: SKIP - the `unicorn` Python package is not installed"); sys.exit(77)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
MOD = os.path.join(ROOT, 'build', 'PDXJ.MOD')
if not os.path.exists(MOD):
    print("pdxj_uc: SKIP - build/PDXJ.MOD is not built (make build/PDXJ.MOD)"); sys.exit(77)
FIX = os.path.join(ROOT, 'tests', 'pdfany', 'jpx')
HOST = os.path.join(ROOT, 'build', 'pdxj', 'pdxj_test')

SEG_MOD, SEG_STK, SEG_XB, SEG_WS, SEG_SRC, SEG_ROW = 0x1000, 0x2000, 0x2400, 0x3000, 0x4000, 0x5F00
SEG_ENG = 0x7000
ENG_SRCLEN, ENG_OPENS, ENG_CLOSES = None, None, None
RETSEG, RETOFF = 0x0050, 0x0000
img = open(MOD, 'rb').read()
magic, abi, isz, bsz = struct.unpack_from('<HHHH', img)
assert isz == len(img), 'PDXJ.MOD header'


class Mod:
    def __init__(self, kb=60):
        global ENG_SRCLEN, ENG_OPENS, ENG_CLOSES
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, 0x110000)
        self.uc.mem_write(SEG_MOD << 4, img)
        eng = open(os.path.join(ROOT, 'build', 'pdxj', 'engstub.bin'), 'rb').read()
        self.uc.mem_write(SEG_ENG << 4, eng)
        ENG_SRCLEN = len(eng) - 10; ENG_OPENS = len(eng) - 4; ENG_CLOSES = len(eng) - 2
        self.uc.mem_write((RETSEG << 4) + RETOFF, b'\xf4')
        self.kb = kb
        self.hook = None
        self.ins = self.byt = self.mul = 0
        self.call(0xC0, SEG_XB, 0, 0, pre=self._init)

    def _init(self):
        w = [0] * 64; w[1] = 0; w[2] = SEG_ENG; w[24] = SEG_WS; w[25] = self.kb
        self.uc.mem_write(SEG_XB << 4, struct.pack('<64H', *w))

    def _count(self, uc, addr, size, user):
        self.ins += 1; self.byt += size
        b = uc.mem_read(addr, 2)
        if b[0] == 0xF7 and ((b[1] >> 3) & 7) in (4, 5, 6, 7): self.mul += 1

    def call(self, verb, bx, cx, dx, pre=None):
        uc = self.uc
        if pre: pre()
        sp = 0xF000
        uc.reg_write(UC_X86_REG_SS, SEG_STK)
        for v in (RETOFF, RETSEG):     # far return address: IP pushed last
            pass
        uc.mem_write((SEG_STK << 4) + sp - 4, struct.pack('<HH', RETOFF, RETSEG))
        sp -= 4
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_DS, 0x7777); uc.reg_write(UC_X86_REG_ES, 0x6666); uc.reg_write(UC_X86_REG_BP, 0x1357)
        uc.reg_write(UC_X86_REG_AX, verb); uc.reg_write(UC_X86_REG_BX, bx)
        uc.reg_write(UC_X86_REG_CX, cx); uc.reg_write(UC_X86_REG_DX, dx)
        uc.reg_write(UC_X86_REG_CS, SEG_MOD)
        uc.emu_start((SEG_MOD << 4) + 8, (RETSEG << 4) + RETOFF, count=400000000)
        assert uc.reg_read(UC_X86_REG_DS) == 0x7777 and uc.reg_read(UC_X86_REG_ES) == 0x6666, 'DS/ES not preserved'
        assert uc.reg_read(UC_X86_REG_BP) == 0x1357, 'BP'
        ax = uc.reg_read(UC_X86_REG_AX)
        cf = uc.reg_read(UC_X86_REG_EFLAGS) & 1
        return ax, cf

    def run(self, path, ww, wh, flags=0, ncomp=0, count=False, eng=False):
        data = open(path, 'rb').read()
        assert len(data) < 0xF000
        self.uc.mem_write(SEG_SRC << 4, data)
        w = [0] * 20; w[2] = SEG_SRC; w[3] = 0; w[4] = len(data); w[5] = 4; w[9] = ncomp; w[10] = ww; w[11] = wh; w[12] = flags
        if eng:                                    # the stream comes from the stand-in PDENG door
            w[0] = 5; w[1] = 0; w[2] = w[3] = w[4] = 0
            self.uc.mem_write((SEG_ENG << 4) + ENG_SRCLEN, struct.pack('<H', len(data)))
        self.uc.mem_write(SEG_XB << 4, struct.pack('<20H', *w))
        ax, cf = self.call(0xE8, SEG_XB, 0, 0)
        o = struct.unpack('<20H', bytes(self.uc.mem_read(SEG_XB << 4, 40)))
        if cf: return ('REFUSED', o[13]), None
        scale, ow, oh, mode, onc, rb = o[18], o[13], o[14], o[15], o[16], o[17]
        if count:
            self.ins = self.byt = self.mul = 0
            self.hook = self.uc.hook_add(UC_HOOK_CODE, self._count)
        rows = bytearray()
        for y in range(oh):
            ax, cf = self.call(0xE9, SEG_ROW, 0, y)
            assert not cf, 'row %d: rc %d' % (y, ax)
            rows += bytes(self.uc.mem_read(SEG_ROW << 4, rb))
        if self.hook is not None:
            self.uc.hook_del(self.hook); self.hook = None
        self.call(0xEA, 0, 0, 0)
        if eng:
            op, cl = struct.unpack('<HH', bytes(self.uc.mem_read((SEG_ENG << 4) + ENG_OPENS, 4)))
            assert cl >= 1, 'the module never closed the engine stream (opens %d closes %d)' % (op, cl)
        return (scale, ow, oh, mode, onc, rb), bytes(rows)


def main():
    key = None; count = False; kb = 60; eng = False
    a = sys.argv[1:]
    while a:
        x = a.pop(0)
        if x == '-k': key = a.pop(0)
        elif x == '--count': count = True
        elif x == '--kb': kb = int(a.pop(0))
        elif x == '--eng': eng = True
        else: sys.exit(__doc__)
    man = json.load(open(os.path.join(FIX, 'MANIFEST.json')))['fixtures']
    m = Mod(kb)
    bad = n = 0
    for e in man:
        if key and key not in e['name']: continue
        path = os.path.join(FIX, e['file'])
        for r in (0, 2):
            ww, hh = (e['w'] + (1 << r) - 1) >> r, (e['h'] + (1 << r) - 1) >> r
            res, rows = m.run(path, ww, hh, count=count, eng=eng)
            p = subprocess.run([HOST, path, os.path.join(ROOT, 'build', 'pdxj', 'uc.raw'), str(ww), str(hh)],
                               capture_output=True, text=True, env=dict(os.environ, PDXJ_KB=str(kb)))
            hout = p.stdout.strip()
            n += 1
            if res[0] == 'REFUSED':
                ok = hout.startswith('REFUSED %d ' % res[1])
                print('%-14s r%d refused (%d)  %s' % (e['name'], r, res[1], 'ok' if ok else 'FAIL host says ' + hout))
                bad += 0 if ok else 1
                continue
            hdr = ' '.join(map(str, res))
            same = hout == hdr and open(os.path.join(ROOT, 'build', 'pdxj', 'uc.raw'), 'rb').read() == rows
            cost = ''
            if count:
                cyc = int(m.byt * 4.34 + m.mul * 120)
                cost = '  %d instr, %d bytes, %d mul/div => ~%d cycles (%.1f s at 4.77 MHz)' % (m.ins, m.byt, m.mul, cyc, cyc / 4.77e6)
            print('%-14s r%d %s  %s%s' % (e['name'], r, hdr, 'ok' if same else 'FAIL (host: %s)' % hout, cost))
            if not same: bad += 1
    print('pdxj_uc: %d cases, %d failed' % (n, bad))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
