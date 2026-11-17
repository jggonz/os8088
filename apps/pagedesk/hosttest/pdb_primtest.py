#!/usr/bin/env python3
"""PDREND's 8-bit band primitives (apps/pagedesk/pdrx8.inc) against their C twins
(hosttest/pdr_host.c, pdr_x8quant) in Unicorn's 16-bit real mode, SS != DS, sentinels
in BP/DS (PAGEDESK-SPEC 20.3.5, 20.22.10):

    python3 hosttest/pdb_primtest.py [-n CASES] [--seed S]

The quantiser is compared byte for byte over random rows, ends and parities with
guard bytes around the band row; pdr_x8xlat against the table. Also prints the
instruction count a pixel (what a 640 x 16 band costs) from Unicorn's code hook.
Exits 77 (skipped) without the `unicorn` package."""
import argparse, ctypes, os, random, subprocess, sys, tempfile
try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_CS, UC_X86_REG_DS,
        UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_EFLAGS, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
        UC_X86_REG_SI, UC_X86_REG_DI)
except ImportError:
    print("pdb_primtest: SKIP - the `unicorn` Python package is not installed"); sys.exit(77)

HERE = os.path.dirname(os.path.abspath(__file__)); AD = os.path.dirname(HERE)
tmp = tempfile.mkdtemp(prefix="pdbprim")
asm_bin = os.path.join(tmp, "h.bin")
subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-I", AD + "/", "-o", asm_bin, os.path.join(HERE, "pdb_primharness.asm")])
so = os.path.join(tmp, "twin.so")
subprocess.check_call(["cc", "-O1", "-shared", "-fPIC", "-DPD_HOST", "-Wl,-w", "-I", AD, "-o", so,
    os.path.join(HERE, "pdr_host.c"), os.path.join(HERE, "pdr_primlib.c"), os.path.join(HERE, "pdhost.c"),
    os.path.join(AD, "pdmath.c")], stderr=subprocess.DEVNULL)
image = open(asm_bin, "rb").read()
DSEG, SSEG, ESEG, SRC, DST = 0x2000, 0x1000, 0x3000, 0x4000, 0x5000
MEM = 0x120000
names = ["xlat", "quant", "xq"]
tab = dict(zip(names, [int.from_bytes(image[3 + 2 * i:5 + 2 * i], "little") for i in range(len(names))]))
HALT = 3 + 2 * len(names)
assert image[HALT] == 0xF4

class Asm:
    def __init__(self):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16); self.uc.mem_map(0, MEM)
        self.uc.mem_write(DSEG << 4, image)
        self.n = 0
        def hook(uc, addr, size, ud): self.n += 1
        self.uc.hook_add(UC_HOOK_CODE, hook)
    def w(self, lin, b): self.uc.mem_write(lin, bytes(b))
    def r(self, lin, n): return bytes(self.uc.mem_read(lin, n))
    def call(self, addr, args):
        uc = self.uc
        for reg, v in ((UC_X86_REG_CS, DSEG), (UC_X86_REG_DS, DSEG), (UC_X86_REG_ES, ESEG), (UC_X86_REG_SS, SSEG), (UC_X86_REG_BP, 0xBEEF)):
            uc.reg_write(reg, v)
        for reg in (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI):
            uc.reg_write(reg, 0xDEAD)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x2)
        sp = 0xFFF0
        for a in reversed(args):
            sp -= 2; uc.mem_write((SSEG << 4) + sp, (a & 0xFFFF).to_bytes(2, "little"))
        argsp = sp; sp -= 2
        uc.mem_write((SSEG << 4) + sp, HALT.to_bytes(2, "little"))
        uc.reg_write(UC_X86_REG_SP, sp)
        self.n = 0
        uc.emu_start((DSEG << 4) + addr, (DSEG << 4) + HALT, count=5000000)
        rd = uc.reg_read; bad = []
        if rd(UC_X86_REG_BP) != 0xBEEF: bad.append("BP")
        if rd(UC_X86_REG_DS) != DSEG: bad.append("DS")
        if rd(UC_X86_REG_SS) != SSEG: bad.append("SS")
        if rd(UC_X86_REG_SP) != argsp: bad.append("SP")
        if rd(UC_X86_REG_EFLAGS) & 0x400: bad.append("DF set")
        return bad

ap = argparse.ArgumentParser(); ap.add_argument("-n", type=int, default=300); ap.add_argument("--seed", type=int, default=7)
a = ap.parse_args()
lib = ctypes.CDLL(so)
hmem = (ctypes.c_ubyte * 0x110000).in_dll(lib, "pd_host_mem")
class XQ(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ushort) for n in "sseg soff dseg doff x0 n par bpp".split()]
xq = XQ.in_dll(lib, "pdr_xq")
R = random.Random(a.seed); A = Asm(); fails = []
cost = {}
for case in range(a.n):
    bpp = R.choice([4, 1]); n = R.randint(1, 80) if R.random() < .8 else R.randint(1, 640)
    x0 = R.randint(0, 40) if R.random() < .6 else R.randint(0, 7)
    src = bytes(R.choice([R.getrandbits(8), 0, 255, 20, 21, 63, 64, 105, 106, 148, 149, 190, 191, 233, 234]) for _ in range(n))
    span = ((x0 & 1) + n + 1) // 2 + 1 if bpp == 4 else ((x0 & 7) + n + 7) // 8 + 1
    par = R.getrandbits(1) if bpp == 4 else (R.getrandbits(2) << 2)
    band = bytes(R.getrandbits(8) for _ in range(span + 16))
    doff = 8                                                    # guards of 8 bytes either side
    for lin, b in ((SRC << 4, src), (DST << 4, band)):
        A.w(lin, b); ctypes.memmove(ctypes.addressof(hmem) + lin, b, len(b))
    xq.sseg, xq.soff, xq.dseg, xq.doff, xq.x0, xq.n, xq.par, xq.bpp = SRC, 0, DST, doff, x0, n, par, bpp
    lib.pdr_x8quant()
    want = bytes(hmem[DST << 4:(DST << 4) + len(band)])
    for i, v in enumerate((SRC, 0, DST, doff, x0, n, par, bpp)): A.w((DSEG << 4) + tab["xq"] + 2 * i, v.to_bytes(2, "little"))
    bad = A.call(tab["quant"], [])
    got = A.r(DST << 4, len(band))
    c = cost.setdefault(bpp, [0, 0]); c[0] += A.n; c[1] += n
    if bad: fails.append("convention %s" % bad)
    if got != want:
        d = [i for i in range(len(band)) if got[i] != want[i]]
        fails.append("bpp %d x0 %d n %d par %d: bytes %s differ (asm %s twin %s)" % (bpp, x0, n, par, d[:5], [hex(got[i]) for i in d[:3]], [hex(want[i]) for i in d[:3]]))
# xlat
for case in range(a.n):
    lut = bytes(R.getrandbits(8) for _ in range(256)); n = R.randint(0, 300); data = bytes(R.getrandbits(8) for _ in range(n + 4))
    A.w((SRC << 4), data); A.w((DSEG << 4) + 0x8000, lut)
    bad = A.call(tab["xlat"], [SRC, 1, n, 0x8000])
    got = A.r(SRC << 4, n + 4)
    want = data[:1] + bytes(lut[b] for b in data[1:1 + n]) + data[1 + n:]
    if bad: fails.append("xlat convention %s" % bad)
    if got != want: fails.append("xlat n=%d differs" % n)
if fails:
    print("pdb_primtest: FAIL\n  " + "\n  ".join(fails[:12])); sys.exit(1)
for bpp, (ins, px) in sorted(cost.items()):
    print("pdb_primtest: quant %dbpp %.1f instructions a pixel (a 640 x 16 band: %d)" % (bpp, ins / px, int(ins / px * 640 * 16)))
print("pdb_primtest: ok (%d quantiser + %d xlat cases)" % (a.n, a.n))
