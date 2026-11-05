#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/inflate86.py - apps/os88inflate.inc, run under Unicorn's 8086
# against Python's zlib (docs/plans/ARCADE-RELEASE-PLAN.md 2.2)
#
#     python3 tests/inflate86.py [ZIP ...]
#
# A stub assembles the include alone, and every stream here goes through it:
# zlib's raw DEFLATE at levels 0 (stored), 1 and 9 (fixed and dynamic codes),
# of text, of zeros, of random bytes, at 1 byte to 64KB; and every member of
# each ZIP named (the ROM sets ARCADE imports). The output must be zlib's
# byte for byte. Then the refusals: a stream cut short, one with a bad block
# type, and one asked for a byte more or less than it makes - each must come
# back CF=1, with nothing past the output written.
# All of it TWICE: as the include is by default (its work space through CS)
# and with INF_DSWS (docs/plans/ARCADE-MEGA-PLAN.md M2: through DS, the
# stream's segment in AX - ARCADE's build), where the work space is put in a
# segment of its own and any write into the code's segment is a failure:
# kern_mega faults every store through CS.
# Needs `pip install unicorn`. Exit 0 = all of it.
# =============================================================================
import os
import random
import subprocess
import sys
import tempfile
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

STUB = """
    cpu 8086
    org 0
    ; the stream at 2000:0000, the output at 3000:0000, CX in [cs:cnt]
    mov cx, [cs:cnt]
    mov ax, 0x3000
    mov es, ax
    xor di, di
%ifdef INF_DSWS
    mov ax, 0x4000                  ; the work space: a segment of its own
    mov ds, ax
    mov ax, 0x2000                  ; AX:SI = the stream
%else
    mov ax, 0x2000
    mov ds, ax
%endif
    xor si, si
    call inflate
    mov ax, 0
    adc ax, 0
    mov bx, 0x4000                  ; the results, outside the code's segment
    mov ds, bx
    mov [res], ax
    mov [resdi], di
    hlt
cnt:    dw 0
res:    dw 0
resdi:  dw 0
%include "os88inflate.inc"
INF_WS:
    times INF_WS_SZ db 0
"""


def build(dsws=False):
    td = tempfile.mkdtemp(prefix='inflate86-')
    src = os.path.join(td, 's.asm')
    open(src, 'w').write(STUB)
    out = os.path.join(td, 's.bin')
    mapf = os.path.join(td, 's.map')
    open(src, 'w').write('[map symbols %s]\n' % mapf + STUB)
    subprocess.run(['nasm', '-f', 'bin', '-w+error', '-I', os.path.join(ROOT, 'apps') + '/']
                   + (['-DINF_DSWS'] if dsws else []) + ['-o', out, src], check=True)
    syms = {}
    for ln in open(mapf):
        f = ln.split()
        if len(f) == 3 and all(c in '0123456789ABCDEFabcdef' for c in f[0] + f[1]):
            syms[f[2]] = int(f[1], 16)
    return open(out, 'rb').read(), syms


CSW = []                            # writes into the code's segment


def run(code, syms, stream, n):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import UC_X86_REG_CS, UC_X86_REG_SS, UC_X86_REG_SP, UC_X86_REG_DS
    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(0, 0x100000)
    uc.mem_write(0x10000, code)
    uc.hook_add(UC_HOOK_MEM_WRITE, lambda u, a, ad, sz, v, d: CSW.append(ad),
                begin=0x10000, end=0x1FFFF)
    uc.mem_write(0x10000 + syms['cnt'], (n & 0xFFFF).to_bytes(2, 'little'))
    uc.mem_write(0x20000, stream[:0x10000])
    guard = bytes([0xA5]) * 0x10010
    uc.mem_write(0x30000, guard)
    uc.reg_write(UC_X86_REG_CS, 0x1000)
    uc.reg_write(UC_X86_REG_DS, 0x1000)
    uc.reg_write(UC_X86_REG_SS, 0x9000)
    uc.reg_write(UC_X86_REG_SP, 0xFFFE)
    hlt = code.index(b'\xf4', 0)
    uc.emu_start(0x10000, 0x10000 + hlt, count=200_000_000)
    cf = int.from_bytes(uc.mem_read(0x40000 + syms['res'], 2), 'little')
    out = bytes(uc.mem_read(0x30000, 0x10010))
    return cf, out


def raw(data, level):
    c = zlib.compressobj(level, zlib.DEFLATED, -15)
    return c.compress(data) + c.flush()


def main():
    bad = []
    for dsws in (False, True):
        code, syms = build(dsws)
        del CSW[:]
        mode = 'INF_DSWS' if dsws else 'through CS'
        one(code, syms, bad, mode)
        if dsws and CSW:
            bad.append('%s: %d writes into the code\'s segment (first at %05Xh)'
                       % (mode, len(CSW), CSW[0]))
        elif dsws:
            print('inflate86: %s: nothing written into the code\'s segment' % mode)
    for b in bad[:20]:
        print('inflate86: FAIL - ' + b)
    if bad:
        return 1
    print('inflate86: ok')
    return 0


def one(code, syms, bad, mode):
    rnd = random.Random(8088)
    cases = []
    text = (open(os.path.join(ROOT, 'SPEC.md'), 'rb').read()[:70000])
    for size in (1, 2, 100, 4096, 8192, 32768, 65535, 65536):
        for name, data in (('text', text[:size]), ('zeros', bytes(size)),
                           ('random', bytes(rnd.getrandbits(8) for _ in range(min(size, 9000))))):
            for level in (0, 1, 9):
                cases.append(('%s %d level %d' % (name, len(data), level), data, raw(data, level)))
    for z in sys.argv[1:]:
        zf = zipfile.ZipFile(z)
        for i in zf.infolist():
            if i.compress_type == zipfile.ZIP_DEFLATED and 0 < i.file_size <= 0x10000:
                zf.fp.seek(i.header_offset)
                h = zf.fp.read(30)
                n, e = int.from_bytes(h[26:28], 'little'), int.from_bytes(h[28:30], 'little')
                zf.fp.seek(i.header_offset + 30 + n + e)
                stream = zf.fp.read(i.compress_size)
                cases.append(('%s:%s' % (os.path.basename(z), i.filename), zf.read(i), stream))
    for name, data, stream in cases:
        if len(stream) > 0x10000:
            continue
        cf, out = run(code, syms, stream, len(data))
        if cf or out[:len(data)] != data or (len(data) < 0x10000 and out[len(data)] != 0xA5):
            bad.append('%s: %s' % (mode, name))
    print('inflate86: %s: %d streams, %d wrong' % (mode, len(cases), len(bad)))
    # the refusals
    data = text[:5000]
    s = raw(data, 9)
    for why, stream, n in (('cut short', s[:len(s) // 2] + bytes(64), len(data)),
                           ('one byte more asked', s, len(data) + 1),
                           ('one byte fewer asked', s, len(data) - 1),
                           ('block type 3', bytes([0x07]) + s, len(data))):
        cf, out = run(code, syms, stream, n)
        ok = cf == 1 and all(b == 0xA5 for b in out[n:n + 16])
        if not ok:
            bad.append('%s: refusal: %s' % (mode, why))
        print('inflate86: %s: %s -> %s' % (mode, why, 'refused' if cf else 'ACCEPTED'))


if __name__ == '__main__':
    sys.exit(main())
