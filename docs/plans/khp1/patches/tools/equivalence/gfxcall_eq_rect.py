"""unicorn: gfx_rect_setup base vs tip, over random rects and three geometries

    python3 gfxcall_eq_rect.py BASE_BUILD TIP_BUILD [N]

BASE_BUILD and TIP_BUILD are build/ directories each holding kernel-full.bin
and kernel.map (a `make` leaves both). N rects a geometry (default 20000).
"""
import sys, random, struct
def _os88_root():
    """The checkout this file sits in ($OS88_ROOT overrides): the nearest
    directory above holding kernel/kernel.asm."""
    import os
    r = os.environ.get("OS88_ROOT")
    if r:
        return r
    d = os.path.dirname(os.path.abspath(__file__))
    while d != os.path.dirname(d):
        if os.path.isfile(os.path.join(d, "kernel", "kernel.asm")):
            return d
        d = os.path.dirname(d)
    raise SystemExit("no os8088 checkout above " + __file__)
sys.path.insert(0, _os88_root() + "/tools")
from unicorn import *
from unicorn.x86_const import *
import os88sym

def parse(mapf):
    off, sect, equ = os88sym._parse_map(mapf)
    return off, sect, equ

def mk(binf, mapf):
    off, sect, equ = parse(mapf)
    full = open(binf, "rb").read()
    text = full[0x1A00:0x1A00 + 0x10000]
    return off, equ, text

def run(kern, regs, geom, table):
    off, equ, text = kern
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x100000)
    mu.mem_write(0x600, text[:0xA000])
    KS = 0x60; LS = equ["LOW_SEG"]
    def w16(o, v): mu.mem_write(KS*16 + off[o], struct.pack("<H", v & 0xFFFF))
    def w8(o, v): mu.mem_write(KS*16 + off[o], bytes([v & 0xFF]))
    ch, cw, rowmax, bmask, bshift, stride = geom
    w16("vid_ch", ch); w16("vid_chm1", ch-1); w16("vid_cw", cw); w16("vid_cwm1", cw-1)
    w16("vid_rowmax", rowmax); w16("vid_bmask", bmask); w8("vid_bshift", bshift); w16("vid_stride", stride)
    mu.mem_write(LS*16 + off["vid_rowtab"], table)
    for v in ("gfx_ry1","gfx_rrows","gfx_rspan","gfx_roff"): w16(v, 0x5A5A)
    w8("gfx_lmsk", 0x33); w8("gfx_rmsk", 0x44)
    HLT = 0xFFF0
    mu.mem_write(KS*16 + HLT, b"\xf4")
    sp = 0x2000
    mu.reg_write(UC_X86_REG_SS, LS); mu.reg_write(UC_X86_REG_SP, sp - 2)
    mu.mem_write(LS*16 + sp - 2, struct.pack("<H", HLT))
    mu.reg_write(UC_X86_REG_CS, KS); mu.reg_write(UC_X86_REG_DS, KS); mu.reg_write(UC_X86_REG_ES, KS)
    for r, v in zip((UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX), regs):
        mu.reg_write(r, v & 0xFFFF)
    mu.reg_write(UC_X86_REG_SI, 0x1111); mu.reg_write(UC_X86_REG_DI, 0x2222); mu.reg_write(UC_X86_REG_BP, 0x3333)
    mu.emu_start(KS*16 + off["gfx_rect_setup"], KS*16 + HLT, count=2000)
    cf = mu.reg_read(UC_X86_REG_EFLAGS) & 1
    r16 = lambda o: struct.unpack("<H", mu.mem_read(KS*16 + off[o], 2))[0]
    r8 = lambda o: mu.mem_read(KS*16 + off[o], 1)[0]
    keep = (mu.reg_read(UC_X86_REG_SI), mu.reg_read(UC_X86_REG_DI), mu.reg_read(UC_X86_REG_BP), mu.reg_read(UC_X86_REG_SP))
    if cf:
        return ("empty", keep)
    return (cf, r16("gfx_ry1"), r16("gfx_rrows"), r16("gfx_rspan"), r16("gfx_roff"), r8("gfx_lmsk"), r8("gfx_rmsk"), keep)

BASE, TIP = sys.argv[1], sys.argv[2]
base = mk(BASE + "/kernel-full.bin", BASE + "/kernel.map")
tip = mk(TIP + "/kernel-full.bin", TIP + "/kernel.map")
random.seed(1)
geoms = [(200, 640, 348, 1, 1, 80), (348, 720, 348, 3, 2, 90), (480, 640, 348, 0, 0, 80), (480, 640, 0, 0, 0, 80), (200, 640, 128, 1, 1, 80)]
n = bad = 0
table = bytes(random.randrange(256) for _ in range(348 * 2))
for g in geoms:
    for k in range(int(sys.argv[3]) if len(sys.argv) > 3 else 20000):
        def c(lo, hi): return random.randrange(lo, hi)
        if k % 10 == 0:
            regs = [random.randrange(-32768, 32768) for _ in range(4)]
        else:
            x1 = c(-40, g[1] + 40); x2 = c(x1 - 3, g[1] + 60); y1 = c(-40, g[0] + 20); y2 = c(y1 - 3, g[0] + 40)
            regs = [x1, y1, x2, y2]
        a = run(base, regs, g, table); b = run(tip, regs, g, table)
        n += 1
        if a != b:
            bad += 1
            if bad < 10: print("DIFF", g, regs, a, b)
print("cases", n, "differences", bad)
