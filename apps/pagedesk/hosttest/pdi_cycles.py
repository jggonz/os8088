#!/usr/bin/env python3
"""pdi_cycles - the 8088 cost of PDXI's inner loops, from instruction counts
(PAGEDESK-SPEC 20.22.11). Unicorn runs each proc on typical data and counts how
often every instruction executes; ndisasm names the instructions; a model of the
8088's clocks prices them: the 8086 manual's base times plus the effective-address
term, +4 clocks per extra word transfer (the 8088's 8-bit bus), floored at 4.34
clocks per instruction byte (the fetch floor, CLAUDE.md / PERFORMANCE.md Part 2).

    python3 apps/pagedesk/hosttest/pdi_cycles.py
"""
import os, re, struct, subprocess, sys, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pdi_asm as A

EA = {'bx': 5, 'si': 5, 'di': 5, 'bp': 5, 'bx+si': 7, 'bx+di': 8, 'bp+si': 8, 'bp+di': 7}

def ea_cost(op):
    m = re.search(r'\[([^\]]*)\]', op)
    if not m: return 0
    t = m.group(1).replace(' ', '').replace('word', '').replace('byte', '')
    t = re.sub(r'^(?:cs|ds|es|ss):', '', t)
    regs = [x for x in re.split(r'[+-]', t) if x in ('bx', 'si', 'di', 'bp')]
    disp = bool(re.search(r'[+-]0x[0-9a-f]+|[+-]\d+', t)) or not regs
    if not regs: return 6
    base = EA.get('+'.join(regs), 5)
    return base + (4 if disp and len(regs) == 1 else (4 if disp else 0))

def cost(mn, ops, nbytes):
    o = [x.strip() for x in ops.split(',')] if ops else []
    mem = [x for x in o if '[' in x]
    wide = ' byte ' not in (' ' + ops + ' ') and not any(r in o for r in ('al', 'ah', 'bl', 'bh', 'cl', 'ch', 'dl', 'dh'))
    ea = ea_cost(mem[0]) if mem else 0
    xfer = 4 if (mem and wide) else 0
    if mn in ('imul', 'mul'):
        return (150 if mn == 'imul' else 124) + (ea + xfer if mem else 0)
    if mn == 'mov':
        if mem: return (9 if o[0].startswith('[') or ' [' in o[0] else 8) + ea + xfer + (4 if len(o) > 1 and o[1][0].isdigit() and o[0].find('[') >= 0 else 0)
        return 4 if (len(o) > 1 and re.match(r'-?(0x)?[0-9a-f]+$', o[1])) else 2
    if mn in ('add', 'sub', 'adc', 'sbb', 'and', 'or', 'xor', 'cmp', 'test'):
        if mem:
            dstmem = o[0].find('[') >= 0 and mn not in ('cmp', 'test')
            imm = len(o) > 1 and re.match(r'-?(0x)?[0-9a-f]+$', o[1])
            return (17 if imm else 16) + ea + (8 if wide else 0) if dstmem else 9 + ea + xfer
        return 4 if len(o) > 1 and re.match(r'-?(0x)?[0-9a-f]+$', o[1]) else 3
    if mn in ('inc', 'dec', 'neg', 'not'): return (15 + ea + (8 if wide else 0)) if mem else 3
    if mn in ('shl', 'shr', 'sar', 'rol', 'ror', 'rcl', 'rcr', 'sal'):
        return 2 if len(o) > 1 and o[1] == '1' else 8 + 4 * 8        # cl: 8 + 4n, n ~ 8 on average here
    if mn == 'push': return 15
    if mn == 'pop': return 12
    if mn == 'call': return 23
    if mn in ('ret', 'retf'): return 20
    if mn == 'jmp': return 15
    if mn == 'jcxz': return 18
    if mn == 'loop': return 17
    if mn.startswith('j'): return 16          # taken; not-taken is 4 - priced as taken
    if mn in ('lodsw', 'stosw', 'movsw'): return 16 if mn != 'movsw' else 26
    if mn == 'cwd': return 5
    return 6

def main():
    asmbin = os.path.join(A.BUILD, 'asmtest.bin')
    subprocess.run(['nasm', '-f', 'bin', '-w+error', '-I', os.path.join(A.HERE, '..'), '-I', os.path.join(A.ROOT, 'apps'),
                    '-o', asmbin, os.path.join(A.HERE, 'pdi_asmtest.asm')], check=True)
    dis = subprocess.run(['ndisasm', '-b', '16', asmbin], capture_output=True, text=True).stdout
    ins = {}
    for ln in dis.splitlines():
        m = re.match(r'([0-9A-F]{8})\s+([0-9A-F]+)\s+(\S+)\s*(.*)', ln)
        if m:
            a = int(m.group(1), 16); nb = len(m.group(2)) // 2
            ins[a] = (m.group(3).lower(), m.group(4).lower(), nb)
    m = A.Machine(asmbin)
    f_hsym, f_bits, f_block, f_idct, a_ebuf, a_pos, a_zz, _ = m.tab

    def price(hist):
        tot = 0
        for a, n in hist.items():
            if a not in ins: continue
            mn, ops, nb = ins[a]
            c = max(cost(mn, ops, nb), 4.34 * nb)
            tot += c * n
        return int(tot)

    rs = A.random.Random(8088)
    out = {}
    for mode, name in ((0, '8x8 (1/1)'), (1, '4x4 (1/2)'), (2, '2x2 (1/4)')):
        res = []
        for kind in (1, 1, 1, 3, 0, 2):
            coef = A.rnd_coef(rs, kind); qt = [rs.choice((2, 5, 8, 16)) for _ in range(64)]
            m.w(0x6000, struct.pack('<64h', *coef)); m.w(0x6100, struct.pack('<64H', *qt)); m.w(0x7000, b'\0' * 88)
            m.hist = {}; m.capture = True
            m.call(f_idct, [0x6000, 0x6100, mode, A.SEG, 0x7000, 11]); m.capture = False
            res.append((kind, price(m.hist)))
        out[name] = res
    print('IDCT, 8088 clocks a block (kind 1 = a typical sparse block, 3 = a few full columns, 0 = dense, 2 = DC only):')
    for k, v in out.items():
        sp = [c for kd, c in v if kd == 1]
        print('  %-10s sparse %7d  columns %7d  dense %7d  dc-only %7d   (%.1f ms at 4.77 MHz sparse)' % (
            k, sum(sp) // len(sp), [c for kd, c in v if kd == 3][0], [c for kd, c in v if kd == 0][0], [c for kd, c in v if kd == 2][0],
            sum(sp) / len(sp) / 4772.7))
    # baseline blocks from a real file's tables and entropy data
    jpg = A.FIXB420
    if jpg:
        tabs = A.real_tables(jpg)
        d = open(jpg, 'rb').read(); i = d.index(b'\xff\xda'); i += 2 + ((d[i + 2] << 8) | d[i + 3])
        # unstuff
        ent = bytearray(); j = i
        while j < len(d) - 2 and len(ent) < 1300:
            if d[j] == 0xFF:
                if d[j + 1] == 0: ent.append(0xFF); j += 2; continue
                break
            ent.append(d[j]); j += 1
        eb = bytes(ent[:1300]) + bytes(1420 - min(len(ent), 1300))
        # the tables through the twin's builder: reuse pdi_twin to make the images
        (cd, vd), (ca, va) = tabs[0x00], tabs[0x10]
        pad = lambda v: bytes(v) + bytes(256 - len(v))
        rec = b'B' + bytes(cd) + struct.pack('<H', len(vd)) + pad(vd) + bytes(ca) + struct.pack('<H', len(va)) + pad(va) + eb + struct.pack('<H', 0)
        r = subprocess.run([os.path.join(A.BUILD, 'pdi_twin')], input=rec, capture_output=True).stdout
        tdc, tac = r[1:839], r[839:1677]
        m.w(0x2000, tdc); m.w(0x2400, tac); m.w(a_ebuf, eb)
        pos = 0; costs = []; nz = []
        for blk in range(40):
            m.w(a_pos, struct.pack('<H', pos)); m.hist = {}; m.capture = True
            m.call(f_block, [0x2000, 0x2400, 0x6000]); m.capture = False
            costs.append(price(m.hist)); pos = struct.unpack('<H', m.r(a_pos, 2))[0]
            nz.append(sum(1 for x in struct.unpack('<64h', m.r(0x6000, 128)) if x))
        # the same coefficients through every transform: what a real photograph's blocks cost
        pos = 0; real = {0: [], 1: [], 2: []}
        for blk in range(40):
            m.w(a_pos, struct.pack('<H', pos)); m.call(f_block, [0x2000, 0x2400, 0x6000])
            pos = struct.unpack('<H', m.r(a_pos, 2))[0]
            cf = m.r(0x6000, 128)
            for mode in (0, 1, 2):
                m.w(0x6000 + 0x200, cf); m.w(0x6100, struct.pack('<64H', *([8] * 64))); m.hist = {}; m.capture = True
                m.call(f_idct, [0x6200, 0x6100, mode, A.SEG, 0x7000, 11]); m.capture = False
                real[mode].append(price(m.hist))
        print('IDCT of the 40 real blocks of that file: mean clocks 8x8 %d, 4x4 %d, 2x2 %d (%.1f / %.1f / %.1f ms at 4.77 MHz)' % (
            tuple(sum(real[k]) // 40 for k in (0, 1, 2)) + tuple(sum(real[k]) / 40 / 4772.7 for k in (0, 1, 2))))
        m.w(a_pos, struct.pack('<H', 0))
        print('Baseline block (Huffman + dezigzag), %d consecutive blocks of a real 4:2:0 file at quality 75:' % len(costs))
        print('  clocks a block: min %d mean %d max %d (mean %d nonzero coefficients; %.2f ms mean at 4.77 MHz)' % (
            min(costs), sum(costs) // len(costs), max(costs), sum(nz) // len(nz), sum(costs) / len(costs) / 4772.7))

main()
