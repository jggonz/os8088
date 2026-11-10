#!/usr/bin/env python3
"""pdxj_asm - pdxjasm.inc (8086, run in Unicorn's real-mode x86) against its host
twin pdxjtw.c, bit for bit (PAGEDESK-SPEC 20.22.13.8).

    python3 apps/pagedesk/hosttest/pdxj_asm.py [-n ROUNDS]

Exits 77 (skipped) without the `unicorn` package, like pdr_primtest.py. The
twins are built into build/pdxj/libpdxjtw.dylib|so with the host cc.
"""
import ctypes, os, random, struct, subprocess, sys
try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_CS, UC_X86_REG_DS,
                                   UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_IP, UC_X86_REG_BX, UC_X86_REG_CX,
                                   UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI)
except ImportError:
    print("pdxj_asm: SKIP - the `unicorn` Python package is not installed"); sys.exit(77)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
BUILD = os.path.join(ROOT, 'build', 'pdxj')
os.makedirs(BUILD, exist_ok=True)
rng = random.Random(8088)

# ---- build the harness image and the twin library
binp = os.path.join(BUILD, 'pdxj_asmtest.bin')
subprocess.check_call(['nasm', '-f', 'bin', '-w+error', '-I', os.path.join(ROOT, 'apps') + '/', '-o', binp,
                       os.path.join(HERE, 'pdxj_asmtest.asm')])
img = open(binp, 'rb').read()
lib = os.path.join(BUILD, 'libpdxjtw.so')
subprocess.check_call(['cc', '-std=gnu99', '-O1', '-shared', '-fPIC', '-w', '-I', os.path.join(ROOT, 'apps', 'pagedesk'),
                       '-I', os.path.join(ROOT, 'apps', 'pagedesk', 'hosttest'), '-o', lib,
                       os.path.join(HERE, 'pdxj_twlib.c')])
tw = ctypes.CDLL(lib)
for n in ('tw_a', 'tw_chi', 'tw_clo', 'tw_ct'): getattr(tw, n).restype = ctypes.c_uint
tw.tw_bp.restype = ctypes.c_ulong
tw.xj_mqdec.restype = ctypes.c_uint
HM = (ctypes.c_ubyte * 0x110000).in_dll(tw, 'pd_host_mem')

CODESEG, STKSEG, FARSEG = 0x1000, 0x2000, 0x3000
NFN = 12
names = ['mqinit', 'mqdec', 'cxreset', 'lift_n', 'lift_f', 'l53_n', 'l53_f', 'scal_n', 'scal_f', 'inter', 'deq', 'grey']
tabw = struct.unpack_from('<%dH' % (NFN + 6), img, 0)
FN = dict(zip(names, tabw[:NFN]))
VAR = dict(zip(['cx', 'a', 'chi', 'clo', 'ct', 'bp'], tabw[NFN:]))
RET = (NFN + 6) * 2            # the `jmp short $`
DATA = 0x4000                  # scratch arrays in the code segment (past the image and its bss)

uc = Uc(UC_ARCH_X86, UC_MODE_16)
uc.mem_map(0, 0x110000)
uc.mem_write((CODESEG << 4), img)


def rd(seg, off, n): return bytes(uc.mem_read((seg << 4) + off, n))
def wr(seg, off, b): uc.mem_write((seg << 4) + off, bytes(b))


def call(fn, *args, es=0x5000, ds=None):
    sp = 0xF000
    uc.reg_write(UC_X86_REG_SS, STKSEG); uc.reg_write(UC_X86_REG_CS, CODESEG)
    uc.reg_write(UC_X86_REG_DS, CODESEG if ds is None else ds); uc.reg_write(UC_X86_REG_ES, es)
    uc.reg_write(UC_X86_REG_BP, 0x1234)
    for a in reversed(args):
        sp -= 2; wr(STKSEG, sp, struct.pack('<H', a & 0xFFFF))
    sp -= 2; wr(STKSEG, sp, struct.pack('<H', RET))
    sp0 = sp + 2 + 2 * len(args)
    uc.reg_write(UC_X86_REG_SP, sp)
    uc.reg_write(UC_X86_REG_DS, CODESEG)
    uc.emu_start((CODESEG << 4) + FN[fn], (CODESEG << 4) + RET, count=5000000)
    assert uc.reg_read(UC_X86_REG_BP) == 0x1234, fn + ': BP not preserved'
    assert uc.reg_read(UC_X86_REG_SP) == sp + 2, fn + ': SP'
    assert uc.reg_read(UC_X86_REG_DS) == CODESEG, fn + ': DS not preserved'
    assert uc.reg_read(UC_X86_REG_ES) == es, fn + ': ES not preserved'
    return uc.reg_read(UC_X86_REG_AX)


def s16(v): return ((v + 0x8000) & 0xFFFF) - 0x8000
fails = 0
def check(name, cond, info=''):
    global fails
    if not cond:
        fails += 1
        print('FAIL', name, info)


# ---- MQ
def mq_state_asm():
    return (struct.unpack('<H', rd(CODESEG, VAR['a'], 2))[0], struct.unpack('<H', rd(CODESEG, VAR['chi'], 2))[0],
            struct.unpack('<H', rd(CODESEG, VAR['clo'], 2))[0], rd(CODESEG, VAR['ct'], 1)[0])
def mq_state_tw(): return (tw.tw_a(), tw.tw_chi(), tw.tw_clo(), tw.tw_ct())

def test_mq(rounds):
    buf = (ctypes.c_ubyte * 4200)()
    for rnd in range(rounds):
        n = rng.choice([4, 30, 300, 2000])
        data = bytearray(rng.getrandbits(8) for _ in range(n))
        for _ in range(n // 20):                     # stuffing and marker-like bytes
            i = rng.randrange(n - 1); data[i] = 0xFF
            if rng.random() < 0.5: data[i + 1] = rng.choice([0x00, 0x7F, 0x8F, 0x90, 0xFF])
        data += b'\xff\xff'
        for i, b in enumerate(data): buf[i] = b
        wr(CODESEG, DATA, data)
        call('cxreset'); tw.xj_cxreset()
        call('mqinit', DATA); tw.xj_mqinit(buf)
        check('mqinit', mq_state_asm() == mq_state_tw(), '%s %s' % (mq_state_asm(), mq_state_tw()))
        for k in range(n * 3):
            cx = rng.choice([0, 0, 1, 2, 3, 9, 10, 14, 15, 16, 17, 18, rng.randrange(19)])
            a = call('mqdec', cx); b = tw.xj_mqdec(cx)
            if a != b or mq_state_asm() != mq_state_tw():
                check('mqdec', False, 'round %d step %d cx %d asm %d tw %d %s %s' % (rnd, k, cx, a, b, mq_state_asm(), mq_state_tw()))
                return
        check('mqcx', rd(CODESEG, VAR['cx'], 19) == bytes(ctypes.string_at(ctypes.addressof(ctypes.c_ubyte.in_dll(tw, 'xj_cx')), 19)))
        a0 = struct.unpack('<H', rd(CODESEG, VAR['bp'], 2))[0] - DATA
        check('mqbp', a0 == tw.tw_bp() - ctypes.addressof(buf), '%d vs %d' % (a0, tw.tw_bp() - ctypes.addressof(buf)))


def rand16(n, ext=True):
    out = []
    for _ in range(n):
        r = rng.random()
        if ext and r < 0.1: out.append(rng.choice([-32768, 32767, 0, 1, -1, 16383, -16384]))
        elif r < 0.6: out.append(rng.randint(-3000, 3000))
        else: out.append(rng.randint(-32768, 32767))
    return out
def pack16(v): return struct.pack('<%dh' % len(v), *v)


def test_lift(rounds):
    ks = [7266, 14466, -868, -25987, 20155, 13318, -32768, 32767, 0, 1, -1]
    for _ in range(rounds):
        n = rng.choice([0, 1, 2, 3, 17, 100, 513])
        d, a, b = rand16(n), rand16(n), rand16(n)
        k = rng.choice(ks)
        for kind in ('lift', 'l53', 'scal'):
            for far in (0, 1):
                nm = kind + ('_f' if far else '_n')
                ca = (ctypes.c_short * (n + 1))(*d); cb = (ctypes.c_short * (n + 1))(*a); cc = (ctypes.c_short * (n + 1))(*b)
                mode = rng.randrange(2)
                if far:
                    wr(FARSEG, 0, pack16(d)); wr(FARSEG, 0x2000, pack16(a)); wr(FARSEG, 0x4000, pack16(b))
                    for i, v in enumerate(d): HM[(FARSEG << 4) + i * 2] = v & 255; HM[(FARSEG << 4) + i * 2 + 1] = (v >> 8) & 255
                    for i, v in enumerate(a): HM[(FARSEG << 4) + 0x2000 + i * 2] = v & 255; HM[(FARSEG << 4) + 0x2000 + i * 2 + 1] = (v >> 8) & 255
                    for i, v in enumerate(b): HM[(FARSEG << 4) + 0x4000 + i * 2] = v & 255; HM[(FARSEG << 4) + 0x4000 + i * 2 + 1] = (v >> 8) & 255
                    if kind == 'lift': call(nm, FARSEG, 0, 0x2000, 0x4000, n, k); getattr(tw, 'xj_lift_f')(FARSEG, 0, 0x2000, 0x4000, n, k)
                    elif kind == 'l53': call(nm, FARSEG, 0, 0x2000, 0x4000, n, mode); tw.xj_l53_f(FARSEG, 0, 0x2000, 0x4000, n, mode)
                    else: call(nm, FARSEG, 0, n, k); tw.xj_scal_f(FARSEG, 0, n, k)
                    got = rd(FARSEG, 0, 2 * n); exp = bytes(HM[(FARSEG << 4):(FARSEG << 4) + 2 * n])
                else:
                    wr(CODESEG, DATA, pack16(d)); wr(CODESEG, DATA + 0x1000, pack16(a)); wr(CODESEG, DATA + 0x2000, pack16(b))
                    if kind == 'lift': call(nm, DATA, DATA + 0x1000, DATA + 0x2000, n, k); tw.xj_lift_n(ca, cb, cc, n, k)
                    elif kind == 'l53': call(nm, DATA, DATA + 0x1000, DATA + 0x2000, n, mode); tw.xj_l53_n(ca, cb, cc, n, mode)
                    else: call(nm, DATA, n, k); tw.xj_scal_n(ca, n, k)
                    got = rd(CODESEG, DATA, 2 * n); exp = ctypes.string_at(ctypes.addressof(ca), 2 * n)
                check(nm, got == exp, 'n=%d k=%d mode=%d' % (n, k, mode))


def test_misc(rounds):
    for _ in range(rounds):
        # inter
        nh = rng.choice([0, 1, 5, 40]); lw = nh + rng.randrange(2)
        e = rand16(lw + 1); o = rand16(nh + 1)
        wr(CODESEG, DATA + 0x1000, pack16(e)); wr(CODESEG, DATA + 0x2000, pack16(o))
        ce = (ctypes.c_short * (lw + 1))(*e[:lw + 1]); co = (ctypes.c_short * (nh + 1))(*o[:nh + 1])
        out = (ctypes.c_short * (lw + nh + 4))()
        call('inter', DATA, DATA + 0x1000, DATA + 0x2000, lw, nh)
        tw.xj_inter(out, ce, co, lw, nh)
        check('inter', rd(CODESEG, DATA, 2 * (lw + nh)) == ctypes.string_at(ctypes.addressof(out), 2 * (lw + nh)), 'lw %d nh %d' % (lw, nh))
        # deq
        n = rng.choice([0, 1, 4, 64]); mag = [rng.choice([0, 1, 2, 3, 100, 4095, 65535, rng.getrandbits(16)]) for _ in range(n)]
        flg = [rng.choice([0, 0x8000, 0x1234, 0x9234]) for _ in range(n)]
        m = rng.choice([1, 2048, 2048 + 100, 4095]); sh = rng.choice([1, 5, 9, 12, 15, 16, 17, 25, 31, 40]); rnd = (1 << (sh - 1)) if rng.random() < .5 and sh < 17 else 0
        wr(CODESEG, DATA, struct.pack('<%dH' % n, *mag)); wr(CODESEG, DATA + 0x1000, struct.pack('<%dH' % n, *flg))
        cm = (ctypes.c_ushort * (n + 1))(*mag); cf = (ctypes.c_ushort * (n + 1))(*flg)
        call('deq', DATA, DATA + 0x1000, FARSEG, 0x100, n, m, sh, rnd)
        tw.xj_deq(cm, cf, FARSEG, 0x100, n, m, sh, rnd)
        check('deq', rd(FARSEG, 0x100, 2 * n) == bytes(HM[(FARSEG << 4) + 0x100:(FARSEG << 4) + 0x100 + 2 * n]), 'm %d sh %d rnd %d' % (m, sh, rnd))
        # grey
        n = rng.choice([0, 1, 9, 200]); src = rand16(n)
        add = rng.choice([0, 128 << 3, 2048 + 8, 1 << 10, 4]); sh = rng.choice([0, 3, 4, 5, 8]); mx = rng.choice([255, 255, 15, 7, 1]); inv = rng.randrange(2)
        tab = bytes(min(255, (i * 255 + mx // 2) // mx) for i in range(128)) if mx < 255 and rng.random() < 0.8 else None
        wr(FARSEG, 0x6000, pack16(src))
        for i, v in enumerate(src): HM[(FARSEG << 4) + 0x6000 + i * 2] = v & 255; HM[(FARSEG << 4) + 0x6000 + i * 2 + 1] = (v >> 8) & 255
        ctab = ctypes.create_string_buffer(tab if tab else b'\0' * 128, 128)
        if tab: wr(CODESEG, DATA + 0x3000, tab)
        call('grey', FARSEG, 0x6000, FARSEG, 0x7000, n, add, sh, mx, DATA + 0x3000 if tab else 0, inv)
        tw.xj_grey(FARSEG, 0x6000, FARSEG, 0x7800, n, add, sh, mx, ctab if tab else None, inv)
        check('grey', rd(FARSEG, 0x7000, n) == bytes(HM[(FARSEG << 4) + 0x7800:(FARSEG << 4) + 0x7800 + n]), 'n %d add %d sh %d mx %d inv %d' % (n, add, sh, mx, inv))


if __name__ == '__main__':
    rounds = 40
    if '-n' in sys.argv: rounds = int(sys.argv[sys.argv.index('-n') + 1])
    test_mq(rounds // 4 + 2); test_lift(rounds); test_misc(rounds * 3)
    print('pdxj_asm: %s' % ('FAIL (%d)' % fails if fails else 'ok - asm == twin'))
    sys.exit(1 if fails else 0)
