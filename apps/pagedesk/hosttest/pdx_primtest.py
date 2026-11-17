#!/usr/bin/env python3
"""PDXF's rasteriser assembly (apps/pagedesk/pdxfrs.inc: pdx_muldiv, pdx_accline, pdx_pack)
against its C twin (pdxfol.c), instruction by instruction in Unicorn's 16-bit real mode, SS != DS
with sentinels in BP/DS (PAGEDESK-SPEC 20.22.8):

    python3 apps/pagedesk/hosttest/pdx_primtest.py [-n CASES] [--seed S]

Both sides get the same random edges - out-of-range, flat, steep, reversed, one-cell and many-cell
rows - and must leave the same ACCUMULATOR (every word) and the same packed bytes; BP, DS, SS:SP are
checked on return. Exits 77 (skipped) without the `unicorn` package."""
import argparse, ctypes, os, random, subprocess, sys, tempfile
try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_CS, UC_X86_REG_DS,
        UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_EFLAGS, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI)
except ImportError:
    print("pdx_primtest: SKIP - the `unicorn` Python package is not installed"); sys.exit(77)

HERE = os.path.dirname(os.path.abspath(__file__)); AD = os.path.dirname(HERE)
ap = argparse.ArgumentParser(); ap.add_argument("-n", type=int, default=3000); ap.add_argument("--seed", type=int, default=1)
a = ap.parse_args()
tmp = tempfile.mkdtemp(prefix="pdxprim")
asm_bin = os.path.join(tmp, "h.bin")
subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-I", os.path.dirname(AD) + "/", "-o", asm_bin, os.path.join(HERE, "pdx_primharness.asm")])
so = os.path.join(tmp, "twin.so")
subprocess.check_call(["cc", "-O1", "-shared", "-fPIC", "-w", "-DPD_HOST", "-I", AD, "-o", so, os.path.join(HERE, "pdx_primlib.c"), os.path.join(AD, "pdmath.c")])
image = open(asm_bin, "rb").read()
names = ["muldiv", "accline", "pack", "acc", "acw", "ach"]
tab = dict(zip(names, [int.from_bytes(image[3 + 2 * i:5 + 2 * i], "little") for i in range(len(names))]))
HALT = 3 + 2 * len(names)
assert image[HALT] == 0xF4
DSEG, SSEG, ESEG = 0x2000, 0x1000, 0x3000
OUTSEG = 0x5000
MEM = 0x120000
ACW, ACH = 34, 48

class Asm:
    def __init__(self, img):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16); self.uc.mem_map(0, MEM); self.uc.mem_write(DSEG << 4, img)
    def w(self, lin, b): self.uc.mem_write(lin, bytes(b))
    def r(self, lin, n): return bytes(self.uc.mem_read(lin, n))
    def call(self, addr, args):
        uc = self.uc
        for reg, v in ((UC_X86_REG_CS, DSEG), (UC_X86_REG_DS, DSEG), (UC_X86_REG_ES, ESEG), (UC_X86_REG_SS, SSEG), (UC_X86_REG_BP, 0xBEEF)):
            uc.reg_write(reg, v)
        for reg in (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI): uc.reg_write(reg, 0xDEAD)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x2)
        sp = 0xFFF0
        for v in reversed(args): sp -= 2; uc.mem_write((SSEG << 4) + sp, (v & 0xFFFF).to_bytes(2, "little"))
        argsp = sp; sp -= 2
        uc.mem_write((SSEG << 4) + sp, HALT.to_bytes(2, "little"))
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.emu_start((DSEG << 4) + addr, (DSEG << 4) + HALT, count=5000000)
        rd = uc.reg_read; bad = []
        if rd(UC_X86_REG_BP) != 0xBEEF: bad.append("BP")
        if rd(UC_X86_REG_DS) != DSEG: bad.append("DS")
        if rd(UC_X86_REG_SS) != SSEG: bad.append("SS")
        if rd(UC_X86_REG_SP) != argsp: bad.append("SP")
        return rd(UC_X86_REG_AX), bad

lib = ctypes.CDLL(so)
acc = (ctypes.c_short * (ACW * ACH)).in_dll(lib, "pdx_acc")
cw = ctypes.c_ushort.in_dll(lib, "pdx_acw"); ch = ctypes.c_ushort.in_dll(lib, "pdx_ach")
hmem = (ctypes.c_ubyte * 0x110000).in_dll(lib, "pd_host_mem")
S16 = ctypes.c_short; U16 = ctypes.c_ushort
lib.twin_accline.argtypes = [S16] * 4
lib.twin_pack.argtypes = [U16, U16]; lib.twin_pack.restype = U16
lib.pdx_muldiv.argtypes = [S16] * 3; lib.pdx_muldiv.restype = S16
def s16(v): return v - 65536 if v & 0x8000 else v
R = random.Random(a.seed); A = Asm(image); fails = []; nm = na = npk = 0

def rv(lim):
    return R.choice([0, 1, -1, 63, 64, 65, lim, lim - 1, -lim, 32767, -32767, -32768]) if R.random() < .2 else R.randint(-lim, lim)
# ---- muldiv
for _ in range(a.n * 3):
    x, y, z = rv(32767), rv(32767), rv(32767)
    got, bad = A.call(tab["muldiv"], [x, y, z]); want = lib.pdx_muldiv(x, y, z); nm += 1
    if bad or s16(got) != want: fails.append("muldiv(%d,%d,%d): asm %d twin %d %s" % (x, y, z, s16(got), want, bad)); break
# ---- accline: the same edges on both sides, the whole accumulator compared after each
for case in range(a.n):
    w = R.randint(1, 32); h = R.randint(1, 48)
    cw.value = w; ch.value = h
    A.w((DSEG << 4) + tab["acw"], w.to_bytes(2, "little")); A.w((DSEG << 4) + tab["ach"], h.to_bytes(2, "little"))
    base = [R.randint(-200, 200) if R.random() < .3 else 0 for _ in range(ACW * ACH)]
    for i, v in enumerate(base): acc[i] = v
    A.w((DSEG << 4) + tab["acc"], b"".join((v & 0xFFFF).to_bytes(2, "little") for v in base))
    lim = max(w, h) * 64 + 200
    for _ in range(R.randint(1, 4)):
        kind = R.random()
        x0, y0, x1, y1 = rv(lim), rv(lim), rv(lim), rv(lim)
        if kind < .15: y1 = y0                               # flat
        elif kind < .3: x1 = x0                              # vertical
        elif kind < .45: x1 = x0 + R.randint(-80, 80); y1 = y0 + R.randint(-300, 300)     # short, steep
        elif kind < .6: x0 = R.randint(0, w * 64); x1 = R.randint(0, w * 64); y0 = R.randint(0, h * 64); y1 = R.randint(0, h * 64)   # inside
        lib.twin_accline(x0, y0, x1, y1)
        got, bad = A.call(tab["accline"], [x0, y0, x1, y1]); na += 1
        if bad: fails.append("accline convention %s" % bad)
    mine = A.r((DSEG << 4) + tab["acc"], ACW * ACH * 2)
    want = b"".join((acc[i] & 0xFFFF).to_bytes(2, "little") for i in range(ACW * ACH))
    if mine != want:
        d = [i // 2 for i in range(0, len(mine), 2) if mine[i:i + 2] != want[i:i + 2]]
        fails.append("accline case %d (w=%d h=%d): %d cells differ, first %s (asm %s twin %s)" % (case, w, h, len(d), d[:4],
            [s16(int.from_bytes(mine[2 * i:2 * i + 2], 'little')) for i in d[:3]], [s16(int.from_bytes(want[2 * i:2 * i + 2], 'little')) for i in d[:3]]))
        break
# ---- pack: random accumulators, the destination bytes compared
for case in range(a.n // 2):
    w = R.randint(1, 32); h = R.randint(1, 48)
    cw.value = w; ch.value = h
    A.w((DSEG << 4) + tab["acw"], w.to_bytes(2, "little")); A.w((DSEG << 4) + tab["ach"], h.to_bytes(2, "little"))
    vals = [R.choice([0, 128, -128, 127, -127, 256, -256, 64, 192]) if R.random() < .4 else R.randint(-300, 300) for _ in range(ACW * ACH)]
    for i, v in enumerate(vals): acc[i] = v
    A.w((DSEG << 4) + tab["acc"], b"".join((v & 0xFFFF).to_bytes(2, "little") for v in vals))
    junk = bytes(R.getrandbits(8) for _ in range(200))
    A.w((OUTSEG << 4) + 0x100, junk); hmem[(OUTSEG << 4) + 0x100:(OUTSEG << 4) + 0x100 + 200] = (ctypes.c_ubyte * 200).from_buffer_copy(junk)
    nb_t = lib.twin_pack(OUTSEG, 0x100)
    got, bad = A.call(tab["pack"], [OUTSEG, 0x100]); npk += 1
    wb = (w + 7) // 8
    if bad or got != nb_t or A.r((OUTSEG << 4) + 0x100, 200) != bytes(hmem[(OUTSEG << 4) + 0x100:(OUTSEG << 4) + 0x100 + 200]):
        fails.append("pack case %d (w=%d h=%d): ret %d twin %d %s" % (case, w, h, got, nb_t, bad)); break
print("pdx_primtest: muldiv %d, accline %d, pack %d cases: %s" % (nm, na, npk, "OK, asm == twin" if not fails else "FAILED"))
for f in fails[:6]: print("  " + f)
sys.exit(1 if fails else 0)
