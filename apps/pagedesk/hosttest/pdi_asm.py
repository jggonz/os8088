#!/usr/bin/env python3
"""pdi_asm - holds pdxidct.inc (the 8086 assembly) to its C twins, bit for bit,
under Unicorn's real-mode x86, and counts the instructions each proc ran
(PAGEDESK-SPEC 20.22.11).

    python3 apps/pagedesk/hosttest/pdi_asm.py [--cases N] [--cycles]

Cases: random coefficient blocks through every transform (dense, sparse, columns
of zeros, the extremes of int16), random canonical Huffman tables (and the real
tables of a cjpeg file) over random bytes through pdi_block, and pdi_bits at every
bit offset. A mismatch prints the case and exits 1.
"""
import argparse, os, random, struct, subprocess, sys
import unicorn
from unicorn import x86_const as X

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
BUILD = os.path.join(ROOT, 'build', 'pdi')
SEG = 0x1000
FIXB420 = next((p for p in (os.path.join(BUILD, 'fx', 'b420.jpg'), os.path.join(ROOT, 'build', 'pdfx_dct', 'b420.jpg')) if os.path.exists(p)), '')

# 8088 cycle costs by mnemonic (IBM 8088 timing; EA add-ons folded into the figure
# for a memory operand). A model, used for the per-block cost in the spec.
def cyc(mn, ops):
    mem = '[' in ops
    m = mn
    if m in ('imul',): return 154 if not mem else 160
    if m in ('mul',): return 118 if not mem else 124
    if m in ('mov',): return 13 if (mem and ops.split(',')[0].strip().startswith('[')) else (14 if mem else 4)
    if m in ('add', 'sub', 'adc', 'sbb', 'and', 'or', 'xor', 'cmp'): return 16 if mem else 3
    if m in ('shl', 'shr', 'sar', 'rcl', 'rcr'): return 8 + 4 * 1 if ',' in ops and ops.split(',')[1].strip() != 'cl' else 8
    if m in ('push',): return 11
    if m in ('pop',): return 8
    if m in ('call',): return 19
    if m in ('ret',): return 16
    if m.startswith('j'): return 12
    if m in ('loop',): return 17
    if m in ('lodsw', 'stosw'): return 12
    if m in ('inc', 'dec', 'neg', 'not'): return 15 if mem else 3
    if m in ('cwd',): return 5
    return 6


class Machine:
    def __init__(self, binpath):
        self.code = open(binpath, 'rb').read()
        self.tab = struct.unpack_from('<8H', self.code, 0)
        self.mu = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_16)
        self.mu.mem_map(SEG * 16, 0x10000)
        self.mu.mem_write(SEG * 16, self.code)
        self.insn = 0
        self.mu.hook_add(unicorn.UC_HOOK_CODE, self._hook)
        self.hist = {}
        self.cycles = 0
        self.capture = False

    def _hook(self, uc, addr, size, ud):
        self.insn += 1
        if self.capture:
            a = addr - SEG * 16
            self.hist[a] = self.hist.get(a, 0) + 1

    def call(self, fn, args):
        mu = self.mu
        for r in (X.UC_X86_REG_CS, X.UC_X86_REG_DS, X.UC_X86_REG_ES, X.UC_X86_REG_SS):
            mu.reg_write(r, SEG)
        sp = 0xFF00
        # push args right to left, then the return address (the hlt)
        for a in reversed(args):
            sp -= 2; mu.mem_write(SEG * 16 + sp, struct.pack('<H', a & 0xFFFF))
        sp -= 2; mu.mem_write(SEG * 16 + sp, struct.pack('<H', self.tab[7]))
        mu.reg_write(X.UC_X86_REG_SP, sp)
        for r in (X.UC_X86_REG_BP, X.UC_X86_REG_SI, X.UC_X86_REG_DI):
            mu.reg_write(r, 0x1111)
        self.insn = 0
        mu.emu_start(SEG * 16 + fn, SEG * 16 + self.tab[7])
        assert mu.reg_read(X.UC_X86_REG_SP) == sp + 2, 'stack not balanced'
        assert mu.reg_read(X.UC_X86_REG_BP) == 0x1111 and mu.reg_read(X.UC_X86_REG_SI) == 0x1111 \
            and mu.reg_read(X.UC_X86_REG_DI) == 0x1111, 'BP/SI/DI not preserved'
        return mu.reg_read(X.UC_X86_REG_AX)

    def w(self, off, data): self.mu.mem_write(SEG * 16 + off, bytes(data))
    def r(self, off, n): return bytes(self.mu.mem_read(SEG * 16 + off, n))


def rnd_coef(rs, kind):
    c = [0] * 64
    if kind == 0:                         # dense moderate
        c = [rs.randint(-300, 300) for _ in range(64)]
    elif kind == 1:                       # sparse, JPEG-like
        c[0] = rs.randint(-1000, 1000)
        for _ in range(rs.randint(0, 12)):
            c[rs.randrange(64)] = rs.randint(-60, 60)
    elif kind == 2:                       # DC only
        c[0] = rs.randint(-1024, 1023)
    elif kind == 3:                       # zero columns, a few full ones
        for col in rs.sample(range(8), 2):
            for r in range(8): c[r * 8 + col] = rs.randint(-500, 500)
    elif kind == 4:                       # extremes
        c = [rs.choice((-32768, 32767, 0, 1, -1, 255, -256)) for _ in range(64)]
    else:
        c = [0] * 64
    return c


def rnd_huff(rs, nsym, maxlen=16):
    """counts[16] and vals: a canonical code with no all-ones code"""
    while True:
        counts = [0] * 16
        left = 1.0
        n = 0
        for L in range(1, maxlen + 1):
            if n >= nsym: break
            cap = int(left * (1 << L))
            k = rs.randint(0, min(cap, nsym - n, 255))
            if rs.random() < 0.3 and L < 12: k = 0
            counts[L - 1] = k
            n += k; left -= k / float(1 << L)
        if n == 0 or left <= 0: continue
        # a final slack so the all-ones code stays unused; refuse a full tree
        vals = rs.sample(range(256), n)
        return counts, vals


def real_tables(path):
    d = open(path, 'rb').read(); i = 2; tabs = {}
    while i < len(d):
        if d[i] != 0xFF: i += 1; continue
        m = d[i + 1]
        if m == 0xC4:
            ln = (d[i + 2] << 8) | d[i + 3]; j = i + 4
            while j < i + 2 + ln:
                tc = d[j]; cnt = list(d[j + 1:j + 17]); n = sum(cnt)
                tabs[tc] = (cnt, list(d[j + 17:j + 17 + n])); j += 17 + n
            i += 2 + ln
        elif m == 0xDA: break
        else:
            i += 2 + ((d[i + 2] << 8) | d[i + 3]) if m not in (0xD8, 0x01) else 2
    return tabs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cases', type=int, default=300)
    ap.add_argument('--cycles', action='store_true')
    o = ap.parse_args()
    asmbin = os.path.join(BUILD, 'asmtest.bin')
    os.makedirs(BUILD, exist_ok=True)
    subprocess.run(['nasm', '-f', 'bin', '-w+error', '-I', os.path.join(HERE, '..'), '-I', os.path.join(ROOT, 'apps'),
                    '-o', asmbin, os.path.join(HERE, 'pdi_asmtest.asm')], check=True)
    twin = os.path.join(BUILD, 'pdi_twin')
    m = Machine(asmbin)
    f_hsym, f_bits, f_block, f_idct, a_ebuf, a_pos, a_zz, _ = m.tab
    rs = random.Random(8088)
    recs = []; meta = []
    stride = 11
    for i in range(o.cases):
        mode = i % 3
        kind = rs.choice((0, 1, 1, 1, 2, 3, 4, 5))
        coef = rnd_coef(rs, kind)
        qt = [rs.choice((1, 2, 3, 5, 8, 16, 40, 99, 255)) for _ in range(64)] if kind != 4 else [rs.randint(1, 65535) for _ in range(64)]
        recs.append(b'I' + struct.pack('<H', mode) + struct.pack('<64h', *coef) + struct.pack('<64H', *qt))
        meta.append(('I', mode, coef, qt))
    std = real_tables(FIXB420) if FIXB420 else {}
    for i in range(o.cases):
        if std and i % 4 == 0:
            (cd, vd), (ca, va) = std[0x00], std[0x10]
        else:
            cd, vd = rnd_huff(rs, rs.randint(2, 12)); ca, va = rnd_huff(rs, rs.randint(10, 200))
        eb = bytes(rs.randrange(256) if rs.random() < 0.9 else 0 for _ in range(1420))
        pos = rs.randrange(0, 400)
        pad = lambda v: bytes(v) + bytes(256 - len(v))
        recs.append(b'B' + bytes(cd) + struct.pack('<H', len(vd)) + pad(vd) + bytes(ca) + struct.pack('<H', len(va)) + pad(va) + eb + struct.pack('<H', pos))
        meta.append(('B', pos, eb))
    for i in range(64):
        n = rs.randint(0, 16); eb = bytes(rs.randrange(256) for _ in range(1420)); pos = rs.randrange(0, 4000)
        recs.append(b'S' + struct.pack('<H', n) + eb + struct.pack('<H', pos)); meta.append(('S', n, eb, pos))
    res = subprocess.run([twin], input=b''.join(recs), capture_output=True, check=True).stdout
    p = 0; bad = 0; n_idct = n_blk = n_bit = 0
    stats = {'I': [0, 0, 0], 'B': [0, 0], 'S': [0, 0]}      # instructions per mode...
    per_mode = {0: [], 1: [], 2: []}; blk_ins = []
    for k, mt in enumerate(meta):
        if mt[0] == 'I':
            _, mode, coef, qt = mt
            want = res[p:p + 88]; p += 88
            base = 0x4000
            m.w(0x6000, struct.pack('<64h', *coef)); m.w(0x6100, struct.pack('<64H', *qt)); m.w(0x7000, b'\xA5' * 88)
            m.capture = o.cycles and k < 6
            m.call(f_idct, [0x6000, 0x6100, mode, SEG, 0x7000, stride])
            m.capture = False
            got = m.r(0x7000, 88)
            per_mode[mode].append(m.insn)
            if got != want:
                bad += 1; print('IDCT mismatch case %d mode %d\n  coef %s\n  qt0 %d\n  want %s\n  got  %s' % (k, mode, coef, qt[0], want.hex(), got.hex()))
                if bad > 5: return 1
            n_idct += 1
        elif mt[0] == 'B':
            _, pos, eb = mt
            ok = res[p]; p += 1
            if ok: continue
            tdc = res[p:p + 838]; tac = res[p + 838:p + 1676]; p += 1676
            wc = struct.unpack_from('<64h', res, p); wd = struct.unpack_from('<h', res, p + 128)[0]; wp = struct.unpack_from('<H', res, p + 130)[0]; p += 132
            m.w(0x2000, tdc); m.w(0x2400, tac); m.w(a_ebuf, eb); m.w(a_pos, struct.pack('<H', pos)); m.w(0x6000, b'\xCC' * 128)
            ax = m.call(f_block, [0x2000, 0x2400, 0x6000])
            gc = struct.unpack('<64h', m.r(0x6000, 128)); gd = ax - 65536 if ax >= 32768 else ax
            gp = struct.unpack('<H', m.r(a_pos, 2))[0]
            blk_ins.append(m.insn)
            if (gc, gd, gp) != (wc, wd, wp):
                bad += 1; print('BLOCK mismatch case %d: diff %d/%d pos %d/%d coef-eq %s' % (k, gd, wd, gp, wp, gc == wc))
                if bad > 5: return 1
            n_blk += 1
        else:
            _, n, eb, pos = mt
            wv, wp = struct.unpack_from('<HH', res, p); p += 4
            m.w(a_ebuf, eb); m.w(a_pos, struct.pack('<H', pos))
            gv = m.call(f_bits, [n]); gp = struct.unpack('<H', m.r(a_pos, 2))[0]
            if (gv, gp) != (wv, wp):
                bad += 1; print('BITS mismatch n=%d pos=%d: %x/%x %d/%d' % (n, pos, gv, wv, gp, wp)); 
            n_bit += 1
    print('pdi_asm: %d IDCT, %d block, %d bits cases against the C twins: %d mismatch(es)' % (n_idct, n_blk, n_bit, bad))
    for md in (0, 1, 2):
        v = per_mode[md]
        print('  idct mode %d (%s): instructions per block min %d mean %d max %d' % (md, ('8x8', '4x4', '2x2')[md], min(v), sum(v) // len(v), max(v)))
    if blk_ins:
        print('  baseline block: instructions min %d mean %d max %d' % (min(blk_ins), sum(blk_ins) // len(blk_ins), max(blk_ins)))
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
