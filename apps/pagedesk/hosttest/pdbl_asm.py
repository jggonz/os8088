#!/usr/bin/env python3
"""pdbl_asm.py - runs pdxijb2.inc (the MQ decoder and the template-0 row) under Unicorn
and compares it with the Python encoder's input, bit for bit, and counts cycles.

  python3 apps/pagedesk/hosttest/pdbl_asm.py [--cases N]

The test links the .inc to a stub (a byte source, the variables pdxijb2.c owns) and
drives _mq_bit directly (random contexts) and _gb_row0 (generic regions). The cycle
figure is an 8088 estimate: 4 clocks per instruction byte fetched plus 4 per memory
operand word (the fetch floor of PERFORMANCE.md Part 2), summed over the run."""
import os, random, subprocess, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..', 'tools'))
import os88pdfx_bilevel as B
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn.x86_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
INC = os.path.join(HERE, '..', 'pdxijb2.inc')

STUB = r'''
cpu 8086
bits 16
org 0
        jmp  short start
        times 8-($-$$) db 0
hdr:    dw _gb_row0, _mq_bit, t_init, _mq_chi, _mq_clo, _mq_a, _mq_ct, _gb_c, _gb_p1, _gb_p2, _gb_w, _gb_fail, _inp, _inend, _mq_cur, _mq_nxt, _g_wseg, _mq_eofn
start:  hlt
_mq_chi: dw 0
_mq_clo: dw 0
_mq_a:   dw 0
_mq_ct:  dw 0
_mq_cur: dw 0
_mq_nxt: dw 0
_mq_eofn: dw 0
_gb_c:   dw 0
_gb_p1:  dw 0
_gb_p2:  dw 0
_gb_w:   dw 0
_gb_fail: dw 0
_g_wseg: dw 0
_inp:    dw 0
_inend:  dw 0
_mq_fetch:
        mov  bx, [_inp]
        cmp  bx, [_inend]
        jae  .eof
        mov  al, [bx]
        inc  word [_inp]
        xor  ah, ah
        ret
.eof:   inc  word [_mq_eofn]
        mov  ax, 0xFF
        ret
t_init: call _mq_fetch
        mov  [_mq_cur], ax
        call _mq_fetch
        mov  [_mq_nxt], ax
        mov  ax, [_mq_cur]
        mov  [_mq_chi], ax
        mov  word [_mq_clo], 0
        call _mq_bytein
        mov  ax, [_mq_chi]
        mov  cl, 7
        shl  ax, cl
        mov  bx, [_mq_clo]
        mov  cl, 9
        shr  bx, cl
        or   ax, bx
        mov  [_mq_chi], ax
        mov  ax, [_mq_clo]
        mov  cl, 7
        shl  ax, cl
        mov  [_mq_clo], ax
        sub  word [_mq_ct], 7
        mov  word [_mq_a], 0x8000
        ret
%include "INC"
'''

class M:
    CS = 0x1000; WS = 0x3000; SS = 0x2000
    def __init__(s):
        src = STUB.replace('INC', INC)
        d = tempfile.mkdtemp()
        a = os.path.join(d, 't.asm'); b = os.path.join(d, 't.bin')
        open(a, 'w').write(src)
        subprocess.check_call(['nasm', '-f', 'bin', '-w-all', '-o', b, a])
        s.code = open(b, 'rb').read()
        h = [int.from_bytes(s.code[8 + 2 * i:10 + 2 * i], 'little') for i in range(18)]
        (s.row0, s.mqbit, s.init, s.chi, s.clo, s.a, s.ct, s.gbc, s.p1, s.p2, s.w, s.fail,
         s.inp, s.inend, s.cur, s.nxt, s.wseg, s.eofn) = h
        s.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        s.uc.mem_map(0x10000, 0x10000); s.uc.mem_map(0x20000, 0x10000); s.uc.mem_map(0x30000, 0x10000)
        s.uc.mem_write(0x10000, s.code)
        s.uc.mem_write(0x10000 + 0x8000, b'\xF4')   # hlt: the return point
        s.uc.reg_write(UC_X86_REG_DS, s.CS); s.uc.reg_write(UC_X86_REG_ES, s.CS)
        s.uc.reg_write(UC_X86_REG_CS, s.CS); s.uc.reg_write(UC_X86_REG_SS, s.SS)
        s.cyc = 0; s.ins = 0
        s.uc.hook_add(UC_HOOK_CODE, s.hook)
        s.setw(s.wseg, s.WS)
    def hook(s, uc, addr, size, ud):
        s.ins += 1; s.cyc += max(4 * size, 8) + 4  # fetch floor, plus ~a memory operand
    def setw(s, off, v): s.uc.mem_write(0x10000 + off, v.to_bytes(2, 'little'))
    def getw(s, off): return int.from_bytes(s.uc.mem_read(0x10000 + off, 2), 'little')
    def call(s, off, ax=None, bx=None, stack=()):
        u = s.uc
        sp = 0xFFF0
        for v in reversed(stack):
            sp -= 2; u.mem_write(0x20000 + sp, v.to_bytes(2, 'little'))
        sp -= 2; u.mem_write(0x20000 + sp, (0x8000).to_bytes(2, 'little'))  # return -> hlt
        u.reg_write(UC_X86_REG_SP, sp)
        u.reg_write(UC_X86_REG_BP, 0)
        u.reg_write(UC_X86_REG_DS, s.CS); u.reg_write(UC_X86_REG_ES, s.CS)
        u.emu_start(off, 0x8001, count=20_000_000)  # stops after hlt executes (past it)
        return u.reg_read(UC_X86_REG_AX)
    def feed(s, data):
        base = 0x4000
        s.uc.mem_write(0x10000 + base, bytes(data))
        s.setw(s.inp, base); s.setw(s.inend, base + len(data)); s.setw(s.eofn, 0)

def test_bits(m, rng, n):
    """random contexts and biased bits through _mq_bit"""
    e = B.MQEnc(); seq = []
    for i in range(n):
        c = rng.randrange(64 if i % 3 else 5000)
        p = rng.choice((0.02, 0.5, 0.9))
        b = 1 if rng.random() < p else 0
        e.bit(c, b); seq.append((c, b))
    data = e.flush()
    m.uc.mem_write(0x30000, bytes(0x10000))
    m.feed(data); m.call(m.init)
    # contexts live at workspace offsets 0x1000+c (one state byte each)
    bad = 0
    for c, b in seq:
        r = m.call(m.mqbit, stack=(0x1000 + c,)) & 0xFF
        if r != b: bad += 1
    return bad, len(seq)

KIND = None
KINDS = ('text', 'noise', 'blank', 'lines', 'scan')

def rand_img(rng, w, h):
    kind = rng.choice(KINDS) if KIND is None else KIND
    img = [[0] * w for _ in range(h)]
    if kind == 'scan':      # white paper with sparse glyph-like strokes (~5% black), the shape of a scanned text page
        for _ in range(w * h // 260):
            x0 = rng.randrange(w); y0 = rng.randrange(h); L = rng.randint(3, 14); horiz = rng.random() < .5
            for k in range(L):
                for t in range(2):
                    xx, yy = (x0 + k, y0 + t) if horiz else (x0 + t, y0 + k)
                    if xx < w and yy < h: img[yy][xx] = 1
        return img
    for y in range(h):
        for x in range(w):
            if kind == 'noise': img[y][x] = 1 if rng.random() < 0.3 else 0
            elif kind == 'lines': img[y][x] = 1 if (y // 3 + x // 7) % 5 == 0 else 0
            elif kind == 'text': img[y][x] = 1 if ((x // 4) * 7 + (y // 5) * 3 + rng.randrange(2)) % 6 < 2 else 0
    return img

def test_rows(m, rng, w, h):
    img = rand_img(rng, w, h)
    e = B.MQEnc(); B.enc_generic(e, img, 0, 0)
    data = e.flush()
    m.uc.mem_write(0x30000, bytes(0x10000))
    m.uc.mem_write(0x32000, b'\x80')   # context 0 owns slot 0
    m.feed(data); m.call(m.init)
    nb = (w + 7) >> 3
    R0, R1, R2 = 0x6000, 0x6400, 0x6800
    for r in (R0, R1, R2): m.uc.mem_write(0x10000 + r, bytes(0x400))
    c, p1, p2 = R0, R1, R2
    m.setw(s_w := m.w, w); m.setw(m.fail, 0)
    bad = 0; ins0 = m.ins; cyc0 = m.cyc
    for y in range(h):
        m.uc.mem_write(0x10000 + c, bytes(nb))
        m.setw(m.gbc, c); m.setw(m.p1, p1); m.setw(m.p2, p2)
        m.call(m.row0)
        if m.getw(m.fail): return 0, h, 0, 1   # the 4,096-context table filled (random noise): the decoder reports it, as gb_cx does
        row = bytes(m.uc.mem_read(0x10000 + c, nb))
        exp = bytearray(nb)
        for x in range(w):
            if img[y][x]: exp[x >> 3] |= 0x80 >> (x & 7)
        if row != bytes(exp): bad += 1
        c, p1, p2 = p2, c, p1
    return bad, h, (m.cyc - cyc0), w * h

if __name__ == '__main__':
    n = 12
    if '--cases' in sys.argv: n = int(sys.argv[sys.argv.index('--cases') + 1])
    rng = random.Random(88)
    m = M(); fails = 0
    for i in range(n):
        bad, tot = test_bits(m, rng, rng.choice((200, 2000, 6000)))
        if bad: fails += 1; print('mq_bit case', i, 'bad', bad, 'of', tot)
    cyc = pix = 0
    for i in range(n):
        w = rng.choice((8, 13, 64, 200, 333, 640)); h = rng.choice((3, 10, 30))
        bad, hh, c, p = test_rows(m, rng, w, h)
        cyc += c; pix += p
        if bad: fails += 1; print('row case', i, 'w', w, 'h', h, 'bad rows', bad)
    for k in KINDS:
        globals()['KIND'] = k
        bad, hh, c, p = test_rows(m, rng, 640, 30)
        print('  %-6s %6.0f est cycles/pixel' % (k, c / p))
    print('asm: %d failed; est %.0f cycles/pixel (8088 fetch floor)' % (fails, cyc / pix))
    sys.exit(1 if fails else 0)
