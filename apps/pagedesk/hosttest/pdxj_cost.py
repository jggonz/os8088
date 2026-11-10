#!/usr/bin/env python3
"""pdxj_cost - what PDXJ costs on a 4.77 MHz 8088, from instruction counts
(PAGEDESK-SPEC 20.22.13.10). Taken ONCE (docs/TESTING: a measurement that decides
or documents a design is taken once and written down).

    python3 apps/pagedesk/hosttest/pdxj_cost.py [fixture.j2k want_w want_h ...]

Runs the real build/PDXJ.MOD in Unicorn with a code hook, buckets every executed
instruction by the function that owns it (labels from a nasm listing), and prices
each with a coarse 8088 table: the fetch floor 4 clocks a byte (the project's
4.34), a register op 3, a memory operand 14-25 by kind, push 15, pop 12, call 23,
ret 20, a taken branch 16, loop 17, mul/imul 130, div 165, a shift by CL 8 + 4/bit.
It is an ESTIMATE, good to about +-30%: MartyPC is the arbiter and the field
numbers (PERFORMANCE.md) the truth. What matters here is the SHAPE: where the time
goes and how it scales.
"""
import bisect, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pdxj_uc as U
from unicorn import UC_HOOK_CODE

ROOT = U.ROOT
LST = os.path.join(ROOT, 'build', 'pdxj', 'pdxj.lst')
subprocess.check_call(['nasm', '-f', 'bin', '-w+error', '-I', os.path.join(ROOT, 'apps') + '/', '-I', os.path.join(ROOT, 'build') + '/',
                       '-l', LST, '-o', os.path.join(ROOT, 'build', 'pdxj', 'cost.bin'), os.path.join(ROOT, 'apps', 'pagedesk', 'pdxj.asm')])
labels = []
pend = None
for ln in open(LST):
    m = re.match(r'\s*\d+\s+([0-9A-F]{8})\s+[0-9A-F]+\s+(.*)$', ln)
    t = ln[ln.index('<1>') + 4:] if '<1>' in ln else (ln[8:] if len(ln) > 8 else '')
    lm = re.match(r'\s*(?:\d+\s+)?(?:<\d>\s*)?([A-Za-z_][A-Za-z0-9_]*):', ln[8:] if not m else m.group(2))
    if m and pend is not None:
        labels.append((int(m.group(1), 16), pend)); pend = None
    if lm and not m:
        pend = lm.group(1)
    elif lm and m:
        labels.append((int(m.group(1), 16), lm.group(1)))
labels.sort()
addrs = [a for a, _ in labels]
names = [n for _, n in labels if True]
# keep global-looking names only (C functions and asm routines)
keep = [(a, n) for a, n in labels if (n.startswith('_') or n.startswith('xj_')) and not n.startswith('__')]
kaddr = [a for a, _ in keep]


def owner(off):
    i = bisect.bisect_right(kaddr, off) - 1
    return keep[i][1] if i >= 0 else '?'


PRE = {0x26, 0x2E, 0x36, 0x3E, 0xF0, 0xF2, 0xF3}


def price(b, cl):
    i = 0
    while i < len(b) and b[i] in PRE: i += 1
    op = b[i]; size = len(b)
    modrm = b[i + 1] if i + 1 < len(b) else 0
    mem = (modrm >> 6) != 3
    reg = (modrm >> 3) & 7
    fetch = 4 * size
    eu = 3
    if op in (0xF7, 0xF6) and reg in (4, 5): eu = 130 + (8 if mem else 0)
    elif op in (0xF7, 0xF6) and reg in (6, 7): eu = 165
    elif op in (0xD0, 0xD1): eu = 15 + 8 if mem else 2
    elif op in (0xD2, 0xD3): eu = (20 if mem else 8) + 4 * (cl & 0xFF)
    elif op in (0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x06, 0x0E, 0x16, 0x1E, 0x9C): eu = 15
    elif op in (0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x07, 0x1F, 0x17, 0x9D): eu = 12
    elif op == 0xE8: eu = 23
    elif op in (0xC3, 0xC2): eu = 20
    elif op in (0xCB, 0xCA): eu = 34
    elif op == 0x9A: eu = 36
    elif op in (0xEB, 0xE9): eu = 15
    elif 0x70 <= op <= 0x7F: eu = 10
    elif op in (0xE0, 0xE1, 0xE2, 0xE3): eu = 14
    elif op in (0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8E, 0x8D): eu = 14 + (4 if op in (0x89, 0x8B) else 0) if mem else 2
    elif op in (0xC6, 0xC7): eu = 16 + (4 if op == 0xC7 else 0) if mem else 4
    elif op in (0xA0, 0xA1, 0xA2, 0xA3): eu = 14
    elif op < 0x40 and (op & 7) < 6:
        if (op & 7) < 4: eu = (19 if (op & 2) == 0 and mem else 14) if mem else 3
        else: eu = 4
    elif op in (0x80, 0x81, 0x83): eu = (23 if reg != 7 else 14) if mem else 4
    elif op in (0xFE, 0xFF): eu = (23 if reg < 2 else 20) if mem else 3
    elif op in (0x84, 0x85): eu = 14 if mem else 3
    elif 0xB0 <= op <= 0xBF: eu = 4
    elif op in (0xF6, 0xF7): eu = 20 if mem else 3
    return max(fetch, eu)


class Cost(U.Mod):
    def __init__(self):
        super().__init__(60)
        self.b = {}
        self.stage = 0
        self.stg = {}
        self.calls = {}

    def hook_fn(self, uc, addr, size, user):
        off = addr - (U.SEG_MOD << 4)
        n = owner(off)
        b = bytes(uc.mem_read(addr, size))
        cl = uc.reg_read(U.UC_X86_REG_CX) & 0xFF
        c = price(b, cl)
        e = self.b.setdefault(n, [0, 0])
        e[0] += 1; e[1] += c
        if b[0] == 0xE8 or True:
            pass
        if n in ('_hrow', '_vcol', '_vstep', '_prow', 'xj_lift_core', 'xj_l53_core', 'xj_scal_core', '_xj_inter',
                 '_xj_lift_n', '_xj_lift_f', '_xj_l53_n', '_xj_l53_f', '_xj_scal_n', '_xj_scal_f', '_idwt_stage'):
            s = self.stg.setdefault(self.stage, [0, 0]); s[0] += 1; s[1] += c
        if off in self.entry_idwt: self.stage += 1; self.calls['stage'] = self.stage
        for k in ('_t1_spp', '_t1_mrp', '_t1_cup', '_xj_mqdec', '_t1_block'):
            if off == self.entry[k]: self.calls[k] = self.calls.get(k, 0) + 1

    def measure(self, path, ww, wh):
        self.entry = {n: a for a, n in keep}
        self.entry_idwt = {self.entry['_idwt_stage']}
        data = open(path, 'rb').read()
        self.uc.mem_write(U.SEG_SRC << 4, data)
        import struct
        w = [0] * 20; w[2] = U.SEG_SRC; w[4] = len(data); w[5] = 4; w[10] = ww; w[11] = wh
        self.uc.mem_write(U.SEG_XB << 4, struct.pack('<20H', *w))
        ax, cf = self.call(0xE8, U.SEG_XB, 0, 0)
        o = struct.unpack('<20H', bytes(self.uc.mem_read(U.SEG_XB << 4, 40)))
        if cf: return None
        self.b = {}; self.stg = {}; self.stage = 0; self.calls = {}
        h = self.uc.hook_add(UC_HOOK_CODE, self.hook_fn)
        oh, rb = o[14], o[17]
        for y in range(oh):
            self.call(0xE9, U.SEG_ROW, 0, y)
        self.uc.hook_del(h)
        self.call(0xEA, 0, 0, 0)
        return o[18], o[13], o[14]


def report(c, path, sc):
    tot_i = sum(v[0] for v in c.b.values()); tot_c = sum(v[1] for v in c.b.values())
    print('\n%s: scale %s, %d instructions, ~%.2f M clocks = %.1f s at 4.77 MHz' % (os.path.basename(path), sc, tot_i, tot_c / 1e6, tot_c / 4.77e6))
    grp = {'tier-2 (packets, tag trees, header bits)': ('_packet', '_tt_dec', '_hb_get', '_hb_bits', '_hb_align', '_td_byte', '_td_skip', '_td_copy', '_arena_put', '_blk_geom', '_pb_geom', '_tnodes', '_hit', '_tile_packets', '_tile_layout', '_tp_next', '_tp_begin', '_s_byte', '_s_fill', '_s_copy', '_s_skip32', '_ilog2', '_cdiv', '_np_of', '_step_sh', '_rd16', '_sot_read'),
           'tier-1 MQ decoder': ('_xj_mqdec', 'xj_bytein'),
           'tier-1 pass logic': ('_t1_spp', '_t1_mrp', '_t1_cup', '_t1_sig', '_t1_sign', '_t1_block'),
           'dequantise': ('_xj_deq', '_decode_comp'),
           'inverse DWT': ('_hrow', '_vcol', '_vstep', '_prow', 'xj_lift_core', 'xj_l53_core', 'xj_scal_core', '_xj_inter', '_xj_lift_n', '_xj_lift_f', '_xj_l53_n', '_xj_l53_f', '_xj_scal_n', '_xj_scal_f', '_idwt_stage', '_rowmap_id'),
           'grey conversion and rows': ('_xj_grey', '_v_row', '_decode_trow', '_decode_tile', '_grey_params')}
    seen = set()
    for g, fl in grp.items():
        i = sum(c.b.get(f, [0, 0])[0] for f in fl); k = sum(c.b.get(f, [0, 0])[1] for f in fl)
        seen.update(fl)
        print('  %-44s %10d instr %9.2f M clk %5.1f%%' % (g, i, k / 1e6, 100.0 * k / tot_c))
    rest = sum(v[1] for n, v in c.b.items() if n not in seen)
    print('  %-44s %10s       %9.2f M clk %5.1f%%' % ('other (pd_f* movers, door, ...)', '', rest / 1e6, 100.0 * rest / tot_c))
    print('  MQ decisions %d, passes: cleanup %d sigprop %d magref %d, code-blocks %d' % (
        c.calls.get('_xj_mqdec', 0), c.calls.get('_t1_cup', 0), c.calls.get('_t1_spp', 0), c.calls.get('_t1_mrp', 0), c.calls.get('_t1_block', 0)))
    m = c.b.get('_xj_mqdec', [0, 0]); n = c.calls.get('_xj_mqdec', 1)
    print('  per MQ decision: %.1f instr incl. byte-in, ~%.0f clk  (xj_mqdec alone %.1f instr)' % ((m[0] + c.b.get('xj_bytein', [0, 0])[0]) / n,
          (m[1] + c.b.get('xj_bytein', [0, 0])[1]) / n, m[0] / n))
    t1i = sum(c.b.get(f, [0, 0])[0] for f in grp['tier-1 pass logic'] + grp['tier-1 MQ decoder'])
    t1c = sum(c.b.get(f, [0, 0])[1] for f in grp['tier-1 pass logic'] + grp['tier-1 MQ decoder'])
    passes = c.calls.get('_t1_cup', 0) + c.calls.get('_t1_spp', 0) + c.calls.get('_t1_mrp', 0)
    if passes: print('  per code-block pass (all three kinds, MQ included): %.0f instr, ~%.1f k clk' % (t1i / passes, t1c / passes / 1000))
    for s in sorted(c.stg):
        if s: print('  wavelet stage %d: %8d instr, ~%.2f M clk' % (s, c.stg[s][0], c.stg[s][1] / 1e6))


def main():
    jobs = [('g_ll.j2k', 160, 120), ('g_ll.j2k', 40, 30), ('g_ly.j2k', 160, 120), ('g_ly.j2k', 40, 30), ('odd_ll.j2k', 157, 113)]
    a = sys.argv[1:]
    if a: jobs = [(a[i], int(a[i + 1]), int(a[i + 2])) for i in range(0, len(a), 3)]
    for f, w, h in jobs:
        path = f if os.path.isabs(f) else os.path.join(U.FIX, f)
        c = Cost()
        r = c.measure(path, w, h)
        report(c, path, r[0] if r else 'refused')


if __name__ == '__main__':
    main()
