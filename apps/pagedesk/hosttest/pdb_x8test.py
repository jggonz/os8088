#!/usr/bin/env python3
"""PDX8.MOD, the shipping part, against its host twin (apps/pagedesk/pdx8.c compiled
-DPD_HOST), through the door, in Unicorn's 16-bit real mode (PAGEDESK-SPEC 20.22.10.11):

    python3 hosttest/pdb_x8test.py [-n SEQUENCES] [--seed S] [PDX8.MOD]

The part is laid where plat lays it: its image at the base of the region, the strip's
planes at seg:PDX8_RES. A random sequence of the verbs PDREND sends in a strip (BAND,
BEGIN, LUM, SPAN, SINK, REC for XSTATE and GROUP records of a display list the test writes,
END) runs on both; after each strip the 8 x width planes and the quantised band row must be
identical byte for byte, and the registers the door promises to preserve must be preserved
(BP, SI, DI, DS, ES, SS:SP, DF; CF and CX/DX are the answer's). Exits 77 without `unicorn`
or without a built PDX8.MOD (make pdx8)."""
import argparse, ctypes, os, random, struct, subprocess, sys, tempfile
try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_CS, UC_X86_REG_DS,
        UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_EFLAGS, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
        UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_IP)
except ImportError:
    print("pdb_x8test: SKIP - the `unicorn` Python package is not installed"); sys.exit(77)

HERE = os.path.dirname(os.path.abspath(__file__)); AD = os.path.dirname(HERE)
ap = argparse.ArgumentParser(); ap.add_argument("-n", type=int, default=40); ap.add_argument("--seed", type=int, default=11)
ap.add_argument("mod", nargs="?", default=os.path.join(AD, "..", "..", "build", "PDX8.MOD"))
a = ap.parse_args()
if not os.path.exists(a.mod):
    print("pdb_x8test: SKIP - %s is not built (make pdx8)" % a.mod); sys.exit(77)
image = open(a.mod, "rb").read()
magic, abi, isz, bsz = struct.unpack_from("<HHHH", image)
assert magic == 0x3850 and isz == len(image), (hex(magic), isz, len(image))
tmp = tempfile.mkdtemp(prefix="pdbx8")
so = os.path.join(tmp, "twin.so")
# pdr_host.c defines pdr_eng under PDR_REAL_ENG only; the shim stands in without it
subprocess.check_call(["cc", "-O1", "-shared", "-fPIC", "-DPD_HOST", "-Wl,-w", "-w", "-I", AD, "-o", so,
    os.path.join(AD, "pdx8.c"), os.path.join(HERE, "pdr_host.c"), os.path.join(HERE, "pdr_primlib.c"),
    os.path.join(HERE, "pdhost.c"), os.path.join(AD, "pdmath.c")])

import re
PDX8_RES = int(re.search(r'#define PDX8_RES\s+(\d+)', open(os.path.join(AD, 'pdabi.h')).read()).group(1))
RSEG, SSEG, XBSEG, DLSEG, BSEG, HSEG = 0x2000, 0x1000, 0x4000, 0x5000, 0x6000, 0x3000
MEM = 0x120000
V_INIT, V_CTL, V_SPAN, V_GS, V_REC, V_IMG, V_DEF = 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7
C_BAND, C_BEGIN, C_END, C_LUM, C_MKEND, C_PASS, C_WANT = 0, 1, 2, 3, 4, 5, 6
D_STATE, D_MARK, D_GROUP, D_GEND, D_NOMASK = 0, 1, 2, 3, 4

class Part:
    def __init__(self):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16); self.uc.mem_map(0, MEM)
        self.uc.mem_write(RSEG << 4, image)
        self.uc.mem_write(HSEG << 4, b"\xf4")           # hlt: where the door returns
        self.n = 0
        def hook(uc, addr, size, ud): self.n += 1
        self.uc.hook_add(UC_HOOK_CODE, hook)
    def w(self, seg, off, b): self.uc.mem_write((seg << 4) + off, bytes(b))
    def r(self, seg, off, n): return bytes(self.uc.mem_read((seg << 4) + off, n))
    def call(self, verb, bx=0, cx=0, dx=0):
        uc = self.uc
        for reg, v in ((UC_X86_REG_CS, RSEG), (UC_X86_REG_DS, 0x7777), (UC_X86_REG_ES, 0x6666), (UC_X86_REG_SS, SSEG), (UC_X86_REG_BP, 0xBEEF),
                       (UC_X86_REG_SI, 0x1234), (UC_X86_REG_DI, 0x5678)):
            uc.reg_write(reg, v)
        uc.reg_write(UC_X86_REG_AX, verb); uc.reg_write(UC_X86_REG_BX, bx)
        uc.reg_write(UC_X86_REG_CX, cx); uc.reg_write(UC_X86_REG_DX, dx)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x2)
        sp = 0xFFF0
        sp -= 2; uc.mem_write((SSEG << 4) + sp, (HSEG).to_bytes(2, "little"))      # far return: CS, IP
        sp -= 2; uc.mem_write((SSEG << 4) + sp, (0).to_bytes(2, "little"))
        uc.reg_write(UC_X86_REG_SP, sp)
        self.n = 0
        uc.emu_start((RSEG << 4) + 8, (HSEG << 4), count=20000000)
        rd = uc.reg_read; bad = []
        for nm, reg, v in (("BP", UC_X86_REG_BP, 0xBEEF), ("SI", UC_X86_REG_SI, 0x1234), ("DI", UC_X86_REG_DI, 0x5678),
                           ("DS", UC_X86_REG_DS, 0x7777), ("ES", UC_X86_REG_ES, 0x6666), ("SS", UC_X86_REG_SS, SSEG),
                           ("SP", UC_X86_REG_SP, sp + 4)):
            if rd(reg) != v: bad.append(nm)
        if rd(UC_X86_REG_EFLAGS) & 0x400: bad.append("DF")
        return rd(UC_X86_REG_AX), rd(UC_X86_REG_CX), rd(UC_X86_REG_DX), rd(UC_X86_REG_EFLAGS) & 1, bad

lib = ctypes.CDLL(so)
hmem = (ctypes.c_ubyte * 0x110000).in_dll(lib, "pd_host_mem")
lib.pdx8_verb.restype = ctypes.c_uint; lib.pdx8_verb.argtypes = [ctypes.c_uint] * 4
def hw(seg, off, b):
    for i, v in enumerate(bytes(b)): hmem[(seg << 4) + off + i] = v
def hr(seg, off, n): return bytes(hmem[(seg << 4) + off + i] for i in range(n))
def hcall(verb, bx=0, cx=0, dx=0):
    ax = lib.pdx8_verb(verb, bx, cx, dx)
    g = lambda n: ctypes.c_uint.in_dll(lib, n).value & 0xFFFF
    return ax & 0xFFFF, g("x8_ocx"), g("x8_odx"), g("x8_cf") & 1, []

def both(P, verb, bx=0, cx=0, dx=0):
    t = P.call(verb, bx, cx, dx); h = hcall(verb, bx, cx, dx)
    return t, h

def setup(P, BW, bpp, dl):
    """XB, DL, band and region identically on both sides"""
    for wr in (P.w, hw):
        wr(DLSEG, 0, dl); wr(BSEG, 0, bytes([0xAB]) * 4096)
    band = struct.pack("<HHhhHHHHhhhh", DLSEG, len(dl), 16, 0, BW, BSEG, (BW + 7) // 8 if bpp == 1 else (BW + 1) // 2, bpp,
                       0, 0, BW + 16 - 1, 7)
    for wr in (P.w, hw): wr(XBSEG, 0, band)
    return band

def rec(typ, aux, body, y0=0, y1=0):
    return struct.pack("<BBHhh", typ, aux, 8 + len(body), y0, y1) + bytes(body)

R = random.Random(a.seed); P = Part(); fails = 0; ninst = 0
for case in range(a.n):
    BW = R.choice([64, 96, 200, 256]); bpp = R.choice([4, 1])
    # a display list of transparency records the walker would meet in order
    recs = []; offs = []; off = 0; depth = 0
    for k in range(R.randint(3, 9)):
        c = R.random()
        if c < .55:
            r = rec(6, 0, [R.choice([255, 255, 200, 128, 60, 10]), R.choice([0, 0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12]), 0, 0])
        elif depth < 2 and c < .8:
            r = rec(7, R.choice([0, 1, 2, 3]), [R.choice([255, 180, 90]), R.choice([0, 0, 1, 2]), 0, 0], 0, 7); depth += 1
        elif depth:
            r = rec(7, 0x80, [], 0, 7); depth -= 1
        else:
            r = rec(6, 0, [255, 0, 0, 0])
        offs.append(off); off += len(r); recs.append(r)
    while depth: recs.append(rec(7, 0x80, [], 0, 7)); depth -= 1
    offs = []; off = 0
    for r in recs: offs.append(off); off += len(r)
    dl = b"".join(recs)
    setup(P, BW, bpp, dl)
    kb = (PDX8_RES + 6 * 8 * BW + 1024 + 1023) // 1024
    xbs = 0
    for (t, h) in [both(P, V_INIT, RSEG, kb, XBSEG), both(P, V_CTL, C_BAND)]:
        if t[:4] != h[:4] or t[4]: fails += 1; print("case", case, "INIT/BAND", t, h)
    y0 = 0
    both(P, V_CTL, C_PASS, 0)
    t, h = both(P, V_CTL, C_BEGIN, y0, y0 + 7)
    nops = R.randint(4, 30); ri = 0
    for op in range(nops):
        c = R.random()
        if c < .15:
            t, h = both(P, V_CTL, C_LUM, R.randint(0, 255))
        elif c < .3 and ri < len(recs):
            t, h = both(P, V_REC, offs[ri]); ri += 1
        elif c < .85:
            xa = R.randint(0, BW - 2); xb = R.randint(xa, min(BW - 1, xa + R.choice([3, 20, 100])))
            t, h = both(P, V_SPAN, y0 + R.randint(0, 7), xa, xb)
        elif c < .93:
            t, h = both(P, V_DEF, D_STATE, R.randint(0, 1), 0)
            xb_t = P.r(XBSEG, 0, 12); xb_h = hr(XBSEG, 0, 12)
            if xb_t != xb_h: fails += 1; print("case", case, "STATE record differs", xb_t.hex(), xb_h.hex())
        else:
            lo = R.randint(0, 600)
            both(P, V_DEF, D_MARK, lo, lo + R.randint(0, 40))
            t, h = both(P, V_CTL, C_WANT, R.randint(0, 600), R.randint(0, 600) + 15)
        ninst += P.n
        if t[:4] != h[:4] or t[4]:
            fails += 1; print("case", case, "op", op, "door", t, "twin", h)
    while ri < len(recs):
        t, h = both(P, V_REC, offs[ri]); ri += 1
        if t[:4] != h[:4] or t[4]: fails += 1; print("case", case, "tail REC", t, h)
    for wr in (P.w, hw): pass
    t, h = both(P, V_CTL, C_END)
    if t[:4] != h[:4] or t[4]: fails += 1; print("case", case, "END", t, h)
    pl = 8 * BW
    pt = P.r(RSEG, PDX8_RES, 6 * pl); ph = hr(RSEG, PDX8_RES, 6 * pl)
    if pt != ph:
        fails += 1; i = next(i for i in range(len(pt)) if pt[i] != ph[i])
        print("case", case, "planes differ at", i, "(plane %d offset %d): part %02x twin %02x" % (i // pl, i % pl, pt[i], ph[i]))
    bt = P.r(BSEG, 0, 4096); bh = hr(BSEG, 0, 4096)
    if bt != bh:
        fails += 1; i = next(i for i in range(len(bt)) if bt[i] != bh[i])
        print("case", case, "band differs at", i, "part %02x twin %02x" % (bt[i], bh[i]))
print("pdb_x8test: %d sequences, %d instructions in the part, %s" % (a.n, ninst, "ok" if not fails else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
