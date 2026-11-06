#!/usr/bin/env python3
"""
os8088 - tests/pdmath/pdmath_diff.py        pdmath.inc against pdmath.c, bit for bit

The SHIPPING 8086 text (apps/pagedesk/pdmath.inc, assembled by
tests/pdmath/pdmharness.asm under `cpu 8086`, -w+error) is run instruction by
instruction in the Unicorn x86 engine in 16-bit real mode, with SS != DS (SPEC.md
73.5: [bp+..] addresses SS) and sentinels in ES, BP and DF; the HOST reference
apps/pagedesk/pdmath.c (-DPD_HOST) is run natively through ctypes.  Both get the
same inputs - edge values plus a seeded pseudo-random stream per function - and
must agree on: the return value, every byte of the argument memory the call may
write (and the guard bytes it must not), the sticky overflow flag, and, for the
far movers, the bytes of the far arena.  After every call the harness also
checks the calling convention: BP, DS, ES, SS and SP come back as they went in
and DF is clear.  Negative controls prove the checks can fail.

    python3 tests/pdmath/pdmath_diff.py [-n CASES] [--seed S] [-k NAME] [-v]

Exit status 0 only when every case of every function agrees.  Needs `nasm`,
`cc` and the `unicorn` Python package (`pip install unicorn`); it exits 77
(skipped) with a note when unicorn is missing, as the toolchain rows do.
"""
import argparse, ctypes, os, random, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.environ.get("PDM_BUILD", os.path.join(ROOT, "build", "pdmath"))

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BP, UC_X86_REG_SP,
        UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
        UC_X86_REG_EFLAGS, UC_X86_REG_IP, UC_X86_REG_BX, UC_X86_REG_CX,
        UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI)
except ImportError:
    print("pdmath_diff: SKIP - the `unicorn` Python package is not installed")
    sys.exit(77)


# ---- build the two sides ---------------------------------------------------
def build():
    os.makedirs(BUILD, exist_ok=True)
    asm_bin = os.path.join(BUILD, "pdmharness.bin")
    # PDM_INC=<dir> puts a MUTATED copy of pdmath.inc first (the mutation check)
    inc = ["-I", os.environ["PDM_INC"] + "/"] if os.environ.get("PDM_INC") else []
    subprocess.check_call(["nasm", "-f", "bin", "-w+error"] + inc + ["-I",
        os.path.join(ROOT, "apps/pagedesk/"), "-o", asm_bin,
        os.path.join(ROOT, "tests/pdmath/pdmharness.asm")])
    so = os.path.join(BUILD, "pdref.so")
    ad = os.path.join(ROOT, "apps/pagedesk")
    subprocess.check_call(["cc", "-O1", "-shared", "-fPIC", "-DPD_HOST", "-Wl,-w",
        "-I", ad, "-o", so, os.path.join(ad, "pdmath.c"),
        os.path.join(ad, "hosttest/pdhost.c")], stderr=subprocess.DEVNULL)
    return asm_bin, so


# ---- the Unicorn side ------------------------------------------------------
DSEG, SSEG, ESEG = 0x2000, 0x1000, 0x3000
SCR = 0x4000                    # scratch window inside DS
SCRN = 192
FAR_BASE = 0x50000              # far arena used by the movers (linear)
MEM = 0x120000

NAMES = ["ovfw", "pdm_ovf", "pdm_ovf_clear", "pdm_u32_set", "pdm_u32_add",
    "pdm_u32_sub", "pdm_u32_add16", "pdm_u32_cmp", "pdm_u32_mul16",
    "pdm_u32_divmod16", "pdm_u32_shr", "pdm_u32_shl", "pdm_u32_todec",
    "pdm_fx_int", "pdm_fx_floor", "pdm_fx_round", "pdm_fx_add", "pdm_fx_sub",
    "pdm_fx_mul", "pdm_fx_div", "pdm_fx_neg", "pdm_fx_cmp", "pdm_fx_parse",
    "pdm_fx_todec", "pdm_fx_to_pt16", "pdm_pt16_to_fx", "pdm_mat_ident",
    "pdm_mat_mul", "pdm_mat_apply", "pdm_view_fwd", "pdm_view_inv",
    "pdm_view_size", "pd_fcopy_in", "pd_fcopy_out", "pd_fcopy", "pd_fpeekb",
    "pd_fpeekw", "pd_fpokeb", "pd_fpokew", "pd_ffill", "pd_ds", "pdm_crc16",
    "pdm_crc16_far"]


class Emu:
    def __init__(self, image):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, MEM)
        self.uc.mem_write(DSEG << 4, image)
        words = [int.from_bytes(image[3 + 2 * i:5 + 2 * i], "little") for i in range(len(NAMES))]
        self.addr = dict(zip(NAMES, words))
        self.halt = 3 + 2 * len(NAMES)              # the `hlt` after the table
        assert image[self.halt] == 0xF4, "harness layout changed"
        self.ovfw = self.addr["ovfw"]

    def mem_w(self, lin, b): self.uc.mem_write(lin, bytes(b))
    def mem_r(self, lin, n): return bytes(self.uc.mem_read(lin, n))

    def call(self, name, args, sp0=0xFFF0, df=False, sentinel=True):
        """args: list of 16-bit values (pushed right to left)."""
        uc = self.uc
        uc.reg_write(UC_X86_REG_CS, DSEG)
        uc.reg_write(UC_X86_REG_DS, DSEG)
        uc.reg_write(UC_X86_REG_ES, ESEG)
        uc.reg_write(UC_X86_REG_SS, SSEG)
        uc.reg_write(UC_X86_REG_BP, 0xBEEF)
        for r in (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
                  UC_X86_REG_SI, UC_X86_REG_DI):
            uc.reg_write(r, 0xDEAD)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x2 | (0x400 if df else 0))
        sp = sp0
        for a in reversed(args):
            sp -= 2
            self.uc.mem_write((SSEG << 4) + sp, (a & 0xFFFF).to_bytes(2, "little"))
        argsp = sp
        sp -= 2
        self.uc.mem_write((SSEG << 4) + sp, self.halt.to_bytes(2, "little"))
        uc.reg_write(UC_X86_REG_SP, sp)
        start = (DSEG << 4) + self.addr[name]
        uc.emu_start(start, (DSEG << 4) + self.halt, count=2000000)
        r = lambda x: uc.reg_read(x)
        bad = []
        if r(UC_X86_REG_BP) != 0xBEEF: bad.append("BP")
        if r(UC_X86_REG_DS) != DSEG: bad.append("DS")
        if r(UC_X86_REG_ES) != ESEG: bad.append("ES")
        if r(UC_X86_REG_SS) != SSEG: bad.append("SS")
        if r(UC_X86_REG_SP) != argsp: bad.append("SP %04x!=%04x" % (r(UC_X86_REG_SP), argsp))
        if r(UC_X86_REG_EFLAGS) & 0x400: bad.append("DF set")
        return r(UC_X86_REG_AX), bad


# ---- the host reference through ctypes -------------------------------------
class Ref:
    def __init__(self, so):
        self.lib = ctypes.CDLL(so)
        self.mem = (ctypes.c_ubyte * 0x110000).in_dll(self.lib, "pd_host_mem")
        self.lib.pdm_ovf.restype = ctypes.c_ushort
        self.lib.pd_host_ds = ctypes.c_ushort.in_dll(self.lib, "pd_host_ds")
        self.lib.pd_host_ds.value = DSEG
        self.sig = {}

    def fn(self, name, spec, ret):
        f = getattr(self.lib, name)
        m = {"p": ctypes.c_void_p, "w": ctypes.c_ushort, "i": ctypes.c_short}
        f.argtypes = [m[c] for c in spec]
        f.restype = {"v": None, "w": ctypes.c_ushort, "i": ctypes.c_short}[ret]
        return f


# ---- case construction -----------------------------------------------------
class P(int):
    """a pointer argument: offset into the scratch window"""


def u16(v): return v & 0xFFFF
def pk32(v): return (v & 0xFFFFFFFF).to_bytes(4, "little")
def pk16(v): return (v & 0xFFFF).to_bytes(2, "little")


class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)

    U32 = [0, 1, 2, 9, 10, 0xFF, 0xFFFF, 0x10000, 0x10001, 0x7FFF, 0x8000,
           0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFF0000, 0xFFFFFFFE, 0xFFFFFFFF,
           0x00010000, 0x0000FFFF, 0x12345678, 3000000000, 1000000000]

    def u32(self):
        r = self.r; k = r.random()
        if k < .35: return r.choice(self.U32)
        if k < .5: return (r.choice(self.U32) + r.randint(-3, 3)) & 0xFFFFFFFF
        if k < .65: return r.getrandbits(r.randint(1, 32))
        return r.getrandbits(32)

    def fx(self):                       # raw signed 16.16 as a 32-bit pattern
        r = self.r; k = r.random()
        if k < .2: return r.choice([0, 1, -1, 0x10000, -0x10000, 0x7FFFFFFF,
                -0x7FFFFFFF, -0x80000000, 0x8000, -0x8000, 0xFFFF, 0x18000, -0x18000])
        if k < .4: return r.randint(-4 << 16, 4 << 16)
        if k < .6: return r.randint(-600 << 16, 600 << 16)
        if k < .8: return r.randint(-(1 << 30), 1 << 30)
        return r.randint(-(1 << 31), (1 << 31) - 1)

    def i16(self):
        r = self.r; k = r.random()
        if k < .25: return r.choice([0, 1, -1, 32767, -32767, -32768, 16, -16, 4096, -4096])
        if k < .6: return r.randint(-2000, 2000)
        return r.randint(-32768, 32767)

    def w16(self): return self.r.choice([0, 1, 0xFFFF, 0x8000, 0x7FFF, self.r.getrandbits(16), self.r.getrandbits(16)])


def scratch():
    return bytearray([0xA5] * SCRN)


# Each generator returns (args, scratch, mask) where mask is a function giving
# the byte ranges of scratch to compare (default: all of it).
def g_u32_bin(G):
    s = scratch(); a, b = G.u32(), G.u32()
    s[4:8] = pk32(a); s[8:12] = pk32(b)
    return [P(0), P(4), P(8)], s, None


def g_u32_set(G):
    s = scratch(); return [P(0), G.w16(), G.w16()], s, None


def g_add16(G):
    s = scratch(); s[0:4] = pk32(G.u32()); return [P(0), G.w16()], s, None


def g_cmp_u(G):
    s = scratch(); a = G.u32()
    b = a if G.r.random() < .2 else G.u32()
    if G.r.random() < .15: b = (a & 0xFFFF0000) | G.w16()
    s[0:4] = pk32(a); s[4:8] = pk32(b); return [P(0), P(4)], s, None


def g_mul16(G):
    s = scratch(); return [P(0), G.w16(), G.w16()], s, None


def g_divmod(G):
    s = scratch(); s[4:8] = pk32(G.u32())
    d = G.r.choice([0, 1, 2, 3, 10, 0xFFFF, 0x8000, G.w16(), G.r.randint(1, 1000)])
    return [P(0), P(4), d], s, None


def g_shift(G):
    s = scratch(); s[4:8] = pk32(G.u32())
    n = G.r.choice([0, 1, 15, 16, 17, 31, 32, 33, 0xFFFF, G.r.randint(0, 40), G.r.randint(0, 40)])
    return [P(0), P(4), n], s, None


def g_u32_todec(G):
    s = scratch(); s[64:68] = pk32(G.u32())
    w = G.r.choice([0, 0, 1, 5, 10, 11, 12, 0xFFFF, G.r.randint(0, 12)])
    return [P(0), P(64), w], s, ("todec", 12)


def g_fx_bin(G):
    s = scratch(); s[4:8] = pk32(G.fx()); s[8:12] = pk32(G.fx()); return [P(0), P(4), P(8)], s, None


def g_fx_mul(G):
    """a third of the cases sit on the +-0x7FFFFFFF saturation edge"""
    r = G.r; a, b = G.fx(), G.fx()
    if r.random() < .35:
        a = r.choice([1, -1]) * r.randint(1, 1 << 31)
        m = max(1, abs(a)); b = (0x7FFFFFFF << 16) // m + r.randint(-3, 3)
        b = r.choice([1, -1]) * max(0, min(b, 1 << 31))
        if r.random() < .5: a, b = b, a
    s = scratch(); s[4:8] = pk32(a); s[8:12] = pk32(b); return [P(0), P(4), P(8)], s, None


def g_fx_div(G):
    s = scratch(); a = G.fx(); b = G.fx()
    if G.r.random() < .5:                          # divisors under 2^16 (fast path)
        b = G.r.choice([G.r.randint(1, 65535), -G.r.randint(1, 65535), 0x10000 // 3, 7])
    if G.r.random() < .05: b = 0
    if G.r.random() < .35 and b:                   # quotient on the saturation edge
        a = (0x7FFFFFFF * abs(b)) >> 16
        a = max(0, min(a + G.r.randint(-3, 3), 1 << 31)) * G.r.choice([1, -1])
    s[4:8] = pk32(a); s[8:12] = pk32(b); return [P(0), P(4), P(8)], s, None


def g_fx_un(G):
    s = scratch(); s[4:8] = pk32(G.fx()); return [P(0), P(4)], s, None


def g_fx_one(G):
    s = scratch(); s[0:4] = pk32(G.fx()); return [P(0)], s, None


def g_fx_cmp(G):
    s = scratch(); a = G.fx(); b = a if G.r.random() < .2 else G.fx()
    if G.r.random() < .2: b = (a & ~0xFFFF) | (G.w16())
    s[0:4] = pk32(a); s[4:8] = pk32(b); return [P(0), P(4)], s, None


def g_fx_int(G):
    s = scratch(); return [P(0), G.i16()], s, None


def num_text(G):
    r = G.r; k = r.random()
    sign = r.choice(["", "", "-", "+"])
    if k < .1:
        t = r.choice(["", "-", "+", ".", "-.", "4.", ".5", "-.5", "0", "00", "+0", "12abc", "1.2.3", " 1", "1 ", "--1", "1e5", "- 1", "32767.99999", "32768", "32767.999985", "32767.5", "40000", "99999999999", "0.000007629", "0.000007630", "0.00001526", "1.0000000001"])
        return t
    ip = r.choice(["", "0", str(r.randint(0, 30)), str(r.randint(0, 9999)), str(r.randint(30000, 40000)), str(r.randint(0, 99999)), "9" * r.randint(1, 12), "32767", "32768", "3276" + str(r.randint(0, 9))])
    if r.random() < .75:
        fr = "".join(r.choice("0123456789") for _ in range(r.choice([0, 1, 2, 3, 4, 5, 8, 9, 12])))
        if r.random() < .15: fr = r.choice(["5", "50", "499999", "5000000", "99999999", "999999999", "000001", "0000076"])
        t = sign + ip + "." + fr
    else:
        t = sign + ip
    if r.random() < .06: t += r.choice(["x", ".", "-", " ", "5.", "\x00"])
    if r.random() < .04 and len(t) > 1:
        i = r.randrange(len(t)); t = t[:i] + r.choice("a.-+ ,") + t[i + 1:]
    return t


def g_fx_parse(G):
    s = scratch(); t = num_text(G).encode("latin1")[:100]
    s[16:16 + len(t)] = t
    return [P(0), P(16), len(t)], s, None


def g_fx_todec(G):
    s = scratch(); v = G.fx()
    if G.r.random() < .3: v = (G.r.randint(-3000, 3000) << 16) | G.r.choice([0, 1, 0x8000, 0x7FFF, 0xFFFF, 0x0CCD, 0x0001, 0x0003, 0x0148, 0x4000])
    s[64:68] = pk32(v)
    return [P(0), P(64)], s, ("todec", 14)


def g_to_pt16(G):
    s = scratch(); v = G.fx()
    if G.r.random() < .5: v = G.r.randint(-40, 40) * 4096 + G.r.choice([0, 2047, 2048, 2049, -2047, -2048, -2049, 1, -1])
    if G.r.random() < .3: v = G.r.choice([1, -1]) * (32767 * 4096 + G.r.choice([-2049, -2048, -2047, 0, 2047, 2048, 2049, 4096, 1 << 27, (1 << 27) + 2047]) )
    s[0:4] = pk32(v); return [P(0)], s, None


def g_pt16_to_fx(G):
    s = scratch(); return [P(0), G.i16()], s, None


def rmat(G, small=False):
    out = b""
    for _ in range(6):
        v = G.fx()
        if small or G.r.random() < .5: v = G.r.randint(-300 << 12, 300 << 12)
        out += pk32(v)
    return out


def g_mat_ident(G):
    s = scratch(); return [P(0)], s, None


def g_mat_mul(G):
    s = scratch(); s[24:48] = rmat(G); s[48:72] = rmat(G)
    k = G.r.random()
    if k < .15: return [P(24), P(24), P(48)], s, None       # r == a
    if k < .30: return [P(48), P(24), P(48)], s, None       # r == b
    return [P(0), P(24), P(48)], s, None


def g_mat_apply(G):
    s = scratch(); s[0:4] = pk32(G.fx()); s[4:8] = pk32(G.fx()); s[24:48] = rmat(G)
    return [P(0), P(4), P(24)], s, None


ASPECTS = [(1, 1), (5, 6), (6, 5), (1, 2), (2, 1), (200, 240), (480, 640), (7, 9), (40, 41), (41, 40), (1, 100), (100, 1)]


def mkview(G, flat=False):
    r = G.r
    def pt(): return r.choice([G.i16(), r.randint(-3000, 12000), r.randint(0, 9900)])
    x0, x1 = sorted([pt(), pt()]); y0, y1 = sorted([pt(), pt()])
    if r.random() < .1: x1 = x0
    rot = r.choice([0, 90, 180, 270])
    zoom = r.choice([25, 50, 75, 100, 150, 200, 400, r.randint(25, 400), r.randint(25, 400), r.randint(1, 32767) if r.random() < .15 else 100])
    an, ad = r.choice(ASPECTS) if r.random() < .6 else (r.randint(1, 4000), r.randint(1, 4000))
    if r.random() < .05: an, ad = r.randint(1, 65535), r.randint(1, 65535)
    ox, oy = G.i16(), G.i16()
    if r.random() < .5: ox, oy = r.randint(-200, 200), r.randint(-200, 200)
    if r.random() < .2: ox, oy = r.choice([32767, 32766, -32767, -32766, -32768, 32000, -32000]), r.choice([32767, 32766, -32767, -32768, 32500])
    v = b"".join(pk16(t) for t in (x0, y0, x1, y1, rot, zoom, an, ad, ox, oy, -1, -1, 1, 1))
    return v


def g_view_fwd(G):
    s = scratch(); s[16:44] = mkview(G)
    x = G.r.choice([G.i16(), G.r.randint(-3000, 12000)]); y = G.r.choice([G.i16(), G.r.randint(-3000, 12000)])
    return [P(0), P(2), P(16), x, y], s, None


def g_view_inv(G):
    s = scratch(); s[16:44] = mkview(G)
    px = G.r.choice([G.i16(), G.r.randint(-600, 800)]); py = G.r.choice([G.i16(), G.r.randint(-600, 800)])
    return [P(0), P(2), P(16), px, py], s, None


def g_view_size(G):
    s = scratch(); s[16:44] = mkview(G)
    return [P(0), P(2), P(16)], s, None


# ---- far memory --------------------------------------------------------------
def far_pick(G, n):
    r = G.r
    seg = r.choice([0x5000, 0x5000, 0x5010, 0x4FF8, 0x5100, 0x5000 + r.randint(0, 0x40)])
    k = r.random()
    if k < .3: off = 0x10000 - n - r.choice([0, 0, 1, 2, 7])        # at / near the segment's end
    elif k < .4: off = r.choice([0, 1, 2])
    else: off = r.randint(0, 200)
    return seg, off & 0xFFFF


def far_n(G):
    r = G.r; k = r.random()
    if k < .5: return r.randint(0, 40)
    if k < .75: return r.choice([0, 1, 2, 3, 47, 48, 64])
    return r.choice([0xFFF0, 0xFFF1, 0xFFFF, 0x8000, r.randint(0, 0xFFFF)])


class FarCase:
    """regions of the far arena (linear, length) to seed and compare"""
    def __init__(self): self.regions = []


def lin(seg, off): return (seg << 4) + off


def span_regions(seg, off, n, pad=24):
    n = min(n, 100)                     # refused big spans touch nothing: watch the start
    base = lin(seg, off)
    lo = max(0, base - pad)
    return [(lo, base + n + pad - lo)]


def refuse_big(G, n, off):
    """a big n must be one the mover REFUSES (it would overrun the scratch)"""
    if n <= 60 or n > 0xFFF0: return n, off
    return n, min(0xFFFF, 0x10000 - n + 1 + G.r.randint(0, min(100, n - 2)))


def g_fcopy_in(G):
    s = scratch(); n = far_n(G); seg, off = far_pick(G, min(n, 60))
    n, off = refuse_big(G, n, off)
    dst = 64 + G.r.randint(0, 3)
    return [seg, off, P(dst), n], s, ("far", span_regions(seg, off, n))


def g_fcopy_out(G):
    s = scratch(); n = far_n(G); seg, off = far_pick(G, min(n, 60))
    src = 64
    for i in range(64, SCRN): s[i] = G.r.getrandbits(8)
    n, off = refuse_big(G, n, off)
    return [P(src), seg, off, n], s, ("far", span_regions(seg, off, n))


def g_fcopy(G):
    s = scratch(); r = G.r
    n = r.choice([0, 1, 2, 3, 5, 16, 33, 64, r.randint(0, 80), 0xFFF1, r.randint(0, 0xFFFF)])
    seg, off = far_pick(G, min(n, 80))
    mode = r.random()
    if mode < .45:                                    # overlap in one segment
        dseg, doff = seg, (off + r.randint(-40, 40)) & 0xFFFF
        sseg, soff = seg, off
    elif mode < .6:                                   # same linear bytes, different segments
        sseg, soff = seg, off + 0x20
        dseg, doff = seg + 2, off
    elif mode < .8:
        dseg, doff = far_pick(G, min(n, 80)); sseg, soff = seg, off
    else:
        dseg, doff = seg + 1, (off + 8) & 0xFFFF; sseg, soff = seg, off
    rg = span_regions(dseg, doff, n) + span_regions(sseg, soff, n)
    return [dseg, doff & 0xFFFF, sseg, soff & 0xFFFF, n], s, ("far", rg)


def g_peekb(G):
    s = scratch(); seg, off = far_pick(G, 1); return [seg, off], s, ("far", span_regions(seg, off, 2))


def g_peekw(G):
    s = scratch(); seg, off = far_pick(G, 2)
    if off == 0xFFFF: off = 0xFFFE                  # off = FFFFh is outside the contract
    return [seg, off], s, ("far", span_regions(seg, off, 2))


def g_pokeb(G):
    s = scratch(); seg, off = far_pick(G, 1); return [seg, off, G.r.getrandbits(8) | (G.r.getrandbits(8) << 8 if G.r.random() < .3 else 0)], s, ("far", span_regions(seg, off, 2))


def g_pokew(G):
    s = scratch(); seg, off = far_pick(G, 2)
    if off == 0xFFFF: off = 0xFFFE
    return [seg, off, G.w16()], s, ("far", span_regions(seg, off, 2))


def g_ffill(G):
    s = scratch(); n = far_n(G); seg, off = far_pick(G, min(n, 80))
    return [seg, off, G.r.getrandbits(8) | (G.r.getrandbits(8) << 8 if G.r.random() < .3 else 0), n], s, ("far", span_regions(seg, off, n))


def g_ds(G):
    return [], scratch(), None


def g_crc16(G):
    s = scratch(); n = G.r.choice([0, 1, 2, 3, 7, 8, 9, 31, 64, G.r.randint(0, 100)])
    for i in range(64, SCRN): s[i] = G.r.getrandbits(8)
    n = min(n, SCRN - 64)
    return [G.w16(), P(64), n], s, None


def g_crc16_far(G):
    s = scratch(); n = far_n(G); seg, off = far_pick(G, min(n, 80))
    return [G.w16(), seg, off, n], s, ("far", span_regions(seg, off, n))


# name, spec, ret, generator.   spec: p pointer, w u16, i int16
FUNCS = [
    ("pdm_u32_set", "pww", "v", g_u32_set),
    ("pdm_u32_add", "ppp", "v", g_u32_bin),
    ("pdm_u32_sub", "ppp", "v", g_u32_bin),
    ("pdm_u32_add16", "pw", "v", g_add16),
    ("pdm_u32_cmp", "pp", "i", g_cmp_u),
    ("pdm_u32_mul16", "pww", "v", g_mul16),
    ("pdm_u32_divmod16", "ppw", "w", g_divmod),
    ("pdm_u32_shr", "ppw", "v", g_shift),
    ("pdm_u32_shl", "ppw", "v", g_shift),
    ("pdm_u32_todec", "ppw", "w", g_u32_todec),
    ("pdm_fx_int", "pi", "v", g_fx_int),
    ("pdm_fx_floor", "p", "i", g_fx_one),
    ("pdm_fx_round", "p", "i", g_fx_one),
    ("pdm_fx_add", "ppp", "v", g_fx_bin),
    ("pdm_fx_sub", "ppp", "v", g_fx_bin),
    ("pdm_fx_mul", "ppp", "v", g_fx_mul),
    ("pdm_fx_div", "ppp", "v", g_fx_div),
    ("pdm_fx_neg", "pp", "v", g_fx_un),
    ("pdm_fx_cmp", "pp", "i", g_fx_cmp),
    ("pdm_fx_parse", "ppw", "w", g_fx_parse),
    ("pdm_fx_todec", "pp", "w", g_fx_todec),
    ("pdm_fx_to_pt16", "p", "i", g_to_pt16),
    ("pdm_pt16_to_fx", "pi", "v", g_pt16_to_fx),
    ("pdm_mat_ident", "p", "v", g_mat_ident),
    ("pdm_mat_mul", "ppp", "v", g_mat_mul),
    ("pdm_mat_apply", "ppp", "v", g_mat_apply),
    ("pdm_view_fwd", "pppii", "v", g_view_fwd),
    ("pdm_view_inv", "pppii", "v", g_view_inv),
    ("pdm_view_size", "ppp", "v", g_view_size),
    ("pd_fcopy_in", "wwpw", "w", g_fcopy_in),
    ("pd_fcopy_out", "pwww", "w", g_fcopy_out),
    ("pd_fcopy", "wwwww", "w", g_fcopy),
    ("pd_fpeekb", "ww", "w", g_peekb),
    ("pd_fpeekw", "ww", "w", g_peekw),
    ("pd_fpokeb", "www", "v", g_pokeb),
    ("pd_fpokew", "www", "v", g_pokew),
    ("pd_ffill", "wwww", "w", g_ffill),
    ("pd_ds", "", "w", g_ds),
    ("pdm_crc16", "wpw", "w", g_crc16),
    ("pdm_crc16_far", "wwww", "w", g_crc16_far),
]
FUNCS_BY = {f[0]: f for f in FUNCS}


# ---- the runner --------------------------------------------------------------
class Harness:
    def __init__(self, asm_bin, so):
        self.emu = Emu(open(asm_bin, "rb").read())
        self.ref = Ref(so)
        self.cbuf = (ctypes.c_ubyte * SCRN)()
        self.fns = {}
        # a rolling pad so far-memory seeds are reproducible per case
        self.farrng = random.Random(1)

    def cfn(self, name, spec, ret):
        if name not in self.fns: self.fns[name] = self.ref.fn(name, spec, ret)
        return self.fns[name]

    def run_case(self, name, spec, ret, args, s, aux, far_ds_override=None):
        emu, ref = self.emu, self.ref
        regions = aux[1] if aux and aux[0] == "far" else []
        # seed memory identically on both sides
        pats = []
        for lo, ln in regions:
            pats.append((lo, bytes(self.farrng.getrandbits(8) for _ in range(ln))))
        for lo, p in pats:
            emu.mem_w(lo, p)
            ctypes.memmove(ctypes.addressof(ref.mem) + lo, p, len(p))
        emu.mem_w((DSEG << 4) + SCR, s)
        for i in range(SCRN): self.cbuf[i] = s[i]
        emu.mem_w((DSEG << 4) + emu.ovfw, b"\0\0")
        ref.lib.pdm_ovf_clear()
        # assembler side
        eargs = [u16(SCR + a) if isinstance(a, P) else u16(a) for a in args]
        ax, bad = emu.call(name, eargs)
        eovf = int.from_bytes(emu.mem_r((DSEG << 4) + emu.ovfw, 2), "little")
        escr = emu.mem_r((DSEG << 4) + SCR, SCRN)
        # reference side
        base = ctypes.addressof(self.cbuf)
        cargs = []
        for c, a in zip(spec, args):
            if c == "p": cargs.append(base + int(a))
            elif c == "w": cargs.append(u16(a))
            else: cargs.append(((a & 0xFFFF) ^ 0x8000) - 0x8000)
        cret = self.cfn(name, spec, ret)(*cargs)
        covf = ref.lib.pdm_ovf()
        cscr = bytes(self.cbuf)
        diffs = list(bad)
        # compare memory (todec: stale bytes after the NUL are unspecified)
        lo_skip = None
        if aux and aux[0] == "todec":
            n = (cret or 0) & 0xFFFF
            lo_skip = (n + 1, n + 4)
        for i in range(SCRN):
            if lo_skip and lo_skip[0] <= i - 0 < lo_skip[1]: continue
            if aux and aux[0] == "todec" and i < 64:   # the buffer is at 0
                pass
            if escr[i] != cscr[i]:
                diffs.append("scratch[%d] asm=%02x ref=%02x" % (i, escr[i], cscr[i])); break
        if ret != "v":
            m = lambda v: (v if v is not None else 0) & 0xFFFF
            if m(ax) != m(cret): diffs.append("ret asm=%04x ref=%04x" % (m(ax), m(cret)))
        if eovf != covf: diffs.append("ovf asm=%d ref=%d" % (eovf, covf))
        for lo, ln in regions:
            a = emu.mem_r(lo, ln)
            b = bytes(ref.mem[lo:lo + ln])
            if a != b:
                j = next(k for k in range(ln) if a[k] != b[k])
                diffs.append("far[%05x] asm=%02x ref=%02x" % (lo + j, a[j], b[j])); break
        return diffs


def describe(name, args, s):
    return "%s(%s)" % (name, ", ".join(("P%d" % a) if isinstance(a, P) else hex(a & 0xFFFF) for a in args))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-n", type=int, default=2500, help="random cases per function")
    ap.add_argument("--seed", type=int, default=20261105)
    ap.add_argument("-k", action="append", help="only functions whose name contains this")
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--no-control", action="store_true")
    a = ap.parse_args()
    asm_bin, so = build()
    H = Harness(asm_bin, so)
    total = fails = 0
    t, f = edge_pass(H, a.k)
    total += t; fails += f
    for name, spec, ret, gen in FUNCS:
        if a.k and not any(k in name for k in a.k): continue
        G = Gen(a.seed ^ (hash(name) & 0xFFFF) ^ sum(map(ord, name)) * 7919)
        bad = 0; first = None
        for i in range(a.n):
            args, s, aux = gen(G)
            d = H.run_case(name, spec, ret, args, s, aux)
            total += 1
            if d:
                bad += 1
                if first is None: first = (describe(name, args, s), bytes(s[:80]).hex(), d)
        fails += bad
        print("%-18s %5d cases  %s" % (name, a.n, "ok" if not bad else "FAIL x%d" % bad))
        if first:
            print("    first: %s\n    scratch: %s\n    diff: %s" % (first[0], first[1], "; ".join(first[2])))
    if not a.no_control and not a.k:
        fails += negative_controls(H)
        fails += roundtrip(H, a.seed, a.n)
        fails += strip_roundtrip(H)
        fails += crc_check_value(H)
        fails += asm_matrix()
    print("pdmath_diff: %d cases, %d failures - %s" % (total, fails, "PASS" if not fails else "FAIL"))
    return 1 if fails else 0


# ---- the fixed edge pass -----------------------------------------------------
# Every case below runs on every invocation, before the seeded stream: the
# values a random draw reaches only by luck (the most negative 16.16, the exact
# saturation and rounding ties, the shift counts 0/16/31/32+, a zero divisor,
# integral and non-integral divisors either side of 1.0, the parser's edge
# strings, the PageDesk aspects at every rotation and zoom edge, spans that end
# EXACTLY at the segment's end, 64KB moves in both overlap directions).
FXE = [0, 1, -1, 2, 3, 0x8000, -0x8000, 0xFFFF, 0x10000, -0x10000, 0x10001,
       0x18000, -0x18000, 0x20000, 0x3E80000, -0x3E80000, 0x640000, 0x9800C,
       0xB504F3, -0xB504F4, 0x7FFF0000, 0x7FFFFFFF, -0x7FFFFFFF, -0x80000000]
U32E = [0, 1, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF, 0x12345678]
PARSE_E = ["", "-", "+", ".", "-.", "+.", "+.5", "-.5", "5.", "-5.", "0", "-0", "-0.0",
    "-0.000001", "-0.0000076", "-0.0000077", "0.00000762939", "0.0000076293945",
    "0.000007629394531", "32767", "32767.99999", "32767.999992", "32767.9999924",
    "32767.99999237", "32767.999993", "-32767.99999", "-32767.999993", "32768",
    "-32768", "-32768.0", "-32767.5", "40000", "4000", "39999", "4001", "40001",
    "9" * 40, "." + "9" * 40, "-." + "0" * 30 + "1", "123.123456789", "1.000000005",
    "1.999999995", "1.99999999", "0.99999999", "0.999999999", "000000000000000000001.5",
    "-0000032767.5", "0000032768", "00000.50000000000", "1e5", "1E5", "1.5x", "1..5",
    "1.5.", "\x00", "1\x00", "1-", "+-1", "-+1", "--1", "++1", " 1", "1 ", "\t1",
    "1,5", "0x10", "inf", "nan", ".-1", "+0.", "-.0",
    # the 9th fraction digit is IGNORED (fd stops at 10^8): these round differently if it counted
    "0.000022889", "-0.000022889", "32767.000022889", "0.000022888"]
PD_ASPECTS = [(1, 1), (35, 48), (29, 45), (35, 54), (5, 12)]     # PAGEDESK-SPEC 5.4
PD_ZOOMS = [25, 26, 33, 50, 67, 75, 100, 125, 150, 200, 300, 399, 400]
PD_BOXES = [(-1600, -800, 8192, 12672), (160, 320, 9952, 12992)]  # nonzero origin, cropped


def pdview(box, rot, zoom, an, ad, ox, oy):
    return b"".join(pk16(t) for t in box + (rot, zoom, an, ad, ox, oy, -1, -1, 1, 1))


def edge_cases():
    """yields (name, args, scratch, aux)"""
    def s2(a, b=None, at=4):
        s = scratch(); s[at:at + 4] = pk32(a)
        if b is not None: s[at + 4:at + 8] = pk32(b)
        return s
    for a in FXE:
        for b in FXE:
            for f in ("pdm_fx_add", "pdm_fx_sub", "pdm_fx_mul", "pdm_fx_div"):
                yield f, [P(0), P(4), P(8)], s2(a, b), None
            yield "pdm_fx_cmp", [P(4), P(8)], s2(a, b), None
        yield "pdm_fx_div", [P(4), P(4), P(4)], s2(a), None          # r == a == b
        yield "pdm_fx_mul", [P(4), P(4), P(4)], s2(a), None
        for f in ("pdm_fx_neg",):
            yield f, [P(0), P(4)], s2(a), None
        for f in ("pdm_fx_floor", "pdm_fx_round", "pdm_fx_to_pt16"):
            yield f, [P(4)], s2(a), None
        yield "pdm_fx_todec", [P(0), P(64)], s2(a, at=64), ("todec", 14)
    for k in (-32768, -32767, -3, -2, -1, 0, 1, 2, 3, 32766, 32767):
        for d in (-32769, -32768, -32767, -1, 0, 1, 32767, 32768, 32769):
            v = k * 65536 + d
            if -0x80000000 <= v <= 0x7FFFFFFF:
                yield "pdm_fx_round", [P(4)], s2(v), None
                yield "pdm_fx_floor", [P(4)], s2(v), None
                yield "pdm_fx_todec", [P(0), P(64)], s2(v, at=64), ("todec", 14)
        yield "pdm_fx_int", [P(0), k], scratch(), None
        yield "pdm_pt16_to_fx", [P(0), k], scratch(), None
    for k in (-32768, -32767, -32766, -2, -1, 0, 1, 2, 32766, 32767, 32768):
        for d in (-2049, -2048, -2047, -1, 0, 1, 2047, 2048, 2049):
            v = k * 4096 + d
            if -0x80000000 <= v <= 0x7FFFFFFF:
                yield "pdm_fx_to_pt16", [P(4)], s2(v), None
    for raw in (1, 2, 3, 4, 5, 6, 7, 0xFFFD, 0xFFFE, 0x1FFFC, 0x1FFFD, 0x1FFFE, 0x199A, 0xCCD, 0x28F6):
        for sg in (1, -1):
            yield "pdm_fx_todec", [P(0), P(64)], s2(sg * raw, at=64), ("todec", 14)
    for t in PARSE_E:
        b = t.encode("latin1"); s = scratch(); s[16:16 + len(b)] = b
        yield "pdm_fx_parse", [P(0), P(16), len(b)], s, None
    for a in U32E:
        for b in U32E:
            for f in ("pdm_u32_add", "pdm_u32_sub"):
                yield f, [P(0), P(4), P(8)], s2(a, b), None
            yield "pdm_u32_cmp", [P(4), P(8)], s2(a, b), None
        for n in (0, 1, 15, 16, 17, 31, 32, 33, 0x8000, 0xFFFF):
            for f in ("pdm_u32_shl", "pdm_u32_shr"):
                yield f, [P(0), P(4), n], s2(a), None
        for d in (0, 1, 2, 10, 0x8000, 0xFFFF):
            yield "pdm_u32_divmod16", [P(0), P(4), d], s2(a), None
            yield "pdm_u32_divmod16", [P(4), P(4), d], s2(a), None    # q == a
        for w in (0, 1, 9, 10, 11, 0xFFFF):
            yield "pdm_u32_todec", [P(0), P(64), w], s2(a, at=64), ("todec", 12)
        for n in (0, 1, 0xFFFF):
            yield "pdm_u32_add16", [P(4), n], s2(a), None
    for a in (0, 1, 0x8000, 0xFFFF):
        for b in (0, 1, 0x8000, 0xFFFF):
            yield "pdm_u32_mul16", [P(0), a, b], scratch(), None
    # the view: PageDesk's aspects, every rotation, the zoom range's edges, negative coordinates
    pts = (-32768, -32767, -1601, -1, 0, 1, 17, 8192, 32767)
    pxs = (-32768, -32767, -1, 0, 1, 199, 32767)
    for box in PD_BOXES:
        for rot in (0, 90, 180, 270, 45):
            for an, ad in PD_ASPECTS:
                for zoom in PD_ZOOMS:
                    for ox, oy in ((17, -33), (-300, 400)):
                        v = pdview(box, rot, zoom, an, ad, ox, oy)
                        s = scratch(); s[16:44] = v
                        yield "pdm_view_size", [P(0), P(2), P(16)], s, None
                        for i, x in enumerate(pts):
                            y = pts[(i * 4 + zoom) % len(pts)]
                            s = scratch(); s[16:44] = v
                            yield "pdm_view_fwd", [P(0), P(2), P(16), x, y], s, None
                        for i, px in enumerate(pxs):
                            py = pxs[(i * 3 + zoom) % len(pxs)]
                            s = scratch(); s[16:44] = v
                            yield "pdm_view_inv", [P(0), P(2), P(16), px, py], s, None
    # zooms past 400 (the contract holds for 0..65535): the floor's -65536 edge,
    # |u| * zoom / 1600 just under 65536 with a remainder, and saturation
    for zoom, x0, x in ((1601, 32727, -32768), (1601, -32768, 32727), (1600, 32767, -32768),
                        (1600, -32768, 32767), (65535, 0, -1), (65535, 0, 1), (32767, -32768, 32767)):
        for rot in (0, 180):
            for an, ad in ((1, 1), (5, 12), (65535, 1)):
                v = pdview((x0, x0, x0, x0), rot, zoom, an, ad, 0, 0)
                s = scratch(); s[16:44] = v
                yield "pdm_view_fwd", [P(0), P(2), P(16), x, x], s, None
                s = scratch(); s[16:44] = v
                yield "pdm_view_size", [P(0), P(2), P(16)], s, None
    # far spans ending exactly at the segment's end, one past it, n = 0, the 0xFFF0 cap
    for off, n in ((0xFFFF, 1), (0xFFFF, 2), (0xFFC4, 60), (0xFFC5, 60), (0, 0), (0xFFFF, 0),
                   (0x10, 0xFFF0), (0x11, 0xFFF0), (0, 0xFFF1), (0, 0xFFFF)):
        rg = [(lin(0x5000, min(off, 0xFF00)) - 16, 0x140)] if n <= 60 else [(0x50000, 0x10020)]
        if n <= 60 or (off + n > 0x10000 or n > 0xFFF0):     # a near buffer cannot take 64KB
            yield "pd_fcopy_in", [0x5000, off, P(64), n], scratch(), ("far", rg)
            s = scratch(); s[64:] = bytes(range(SCRN - 64))
            yield "pd_fcopy_out", [P(64), 0x5000, off, n], s, ("far", rg)
        yield "pd_ffill", [0x5000, off, 0x5AA5, n], scratch(), ("far", rg)
        yield "pdm_crc16_far", [0x1D0F, 0x5000, off, n], scratch(), ("far", rg)
    # 64KB moves: disjoint, overlapping both ways in one segment and across segments
    big = [(0x50000, 0x20100)]
    for d, s_ in (((0x6000, 0x10), (0x5000, 0x10)), ((0x5001, 0x10), (0x5000, 0x10)),
                  ((0x5000, 0x10), (0x5001, 0x10)), ((0x5000, 0x21), (0x5002, 0x0)),
                  ((0x5002, 0x0), (0x5000, 0x21)), ((0x5001, 0x0), (0x5000, 0x10))):
        for n in (0xFFF0, 0x8001, 1, 2, 3):
            if d[1] + n <= 0x10000 and s_[1] + n <= 0x10000:
                yield "pd_fcopy", [d[0], d[1], s_[0], s_[1], n], scratch(), ("far", big)
    yield "pd_fcopy", [0x5000, 0x10, 0x5000, 0x11, 0xFFF0], scratch(), ("far", big)   # refused (src end)
    yield "pd_fcopy", [0x5000, 0x11, 0x5000, 0x10, 0xFFF0], scratch(), ("far", big)   # refused (dst end)


def edge_pass(H, ks):
    total = fails = 0; per = {}
    for name, args, s, aux in edge_cases():
        if ks and not any(k in name for k in ks): continue
        _, spec, ret, _g = FUNCS_BY[name]
        d = H.run_case(name, spec, ret, args, s, aux)
        total += 1; c = per.setdefault(name, [0, 0]); c[0] += 1
        if d:
            fails += 1; c[1] += 1
            if c[1] <= 2:
                print("    edge FAIL %s: %s" % (describe(name, args, s), "; ".join(d)))
    print("edge pass: %d fixed cases over %d functions, %d failures" % (total, len(per), fails))
    return total, fails


def strip_roundtrip(H):
    """PAGEDESK-SPEC 5.3's own claim, on the assembly: every pixel of a 200-pixel
    strip (a row and a column through the page's top-left) round-trips for every
    R, at each PageDesk aspect and zoom edge, on nonzero-origin cropped pages."""
    emu = H.emu; good = bad = 0; base = (DSEG << 4) + SCR
    rd = lambda o: int.from_bytes(emu.mem_r(base + o, 2), "little")
    for box in PD_BOXES:
        for rot in (0, 90, 180, 270):
            for an, ad in PD_ASPECTS:
                for zoom in (25, 33, 100, 400):
                    ox, oy = 17, -33
                    emu.mem_w(base + 16, pdview(box, rot, zoom, an, ad, ox, oy))
                    for i in range(200):
                        for px, py in ((ox + i, oy + 37), (ox + 41, oy + i)):
                            emu.mem_w((DSEG << 4) + emu.ovfw, b"\0\0")
                            emu.call("pdm_view_inv", [SCR, SCR + 2, SCR + 16, px, py])
                            emu.call("pdm_view_fwd", [SCR + 4, SCR + 6, SCR + 16, rd(0), rd(2)])
                            ok = (rd(4), rd(6)) == (px & 0xFFFF, py & 0xFFFF) and rd(-SCR + emu.ovfw) == 0
                            good += ok; bad += not ok
                            if not ok and bad < 4:
                                print("  strip: box %s R%d %d/%d z%d p=(%d,%d) -> (%d,%d)" % (box, rot, an, ad, zoom, px, py, rd(4), rd(6)))
    print("PAGEDESK-SPEC 5.3 strip: %d pixels round-tripped, %d broken" % (good, bad))
    return bad


def crc_check_value(H):
    """the published CRC-16/CCITT-FALSE (init FFFF) and XMODEM (init 0) check
    values of "123456789", on BOTH sides: the reference is held to the world too"""
    s = scratch(); s[64:73] = b"123456789"; miss = 0
    for init, want in ((0xFFFF, 0x29B1), (0x0000, 0x31C3)):
        _, spec, ret, _g = FUNCS_BY["pdm_crc16"]
        d = H.run_case("pdm_crc16", spec, ret, [init, P(64), 9], s, None)
        ax, _ = H.emu.call("pdm_crc16", [init, SCR + 64, 9])
        ok = not d and ax == want
        print("crc16 check value init %04x: asm %04x, want %04x %s" % (init, ax, want, "ok" if ok else "FAIL"))
        miss += not ok
    return miss


def asm_matrix():
    """pdmath.inc must assemble (cpu 8086, -w+error) included twice and with every
    PDM_SKIP_* group left out, alone and together; prints each group's bytes."""
    import tempfile
    groups = ["U32", "PARSE", "MAT", "VIEW", "FAR", "CRC"]
    ad = os.path.join(ROOT, "apps/pagedesk/")
    bad = 0; sizes = {}
    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, "t.asm"); out = os.path.join(td, "t.bin")
        open(src, "w").write('cpu 8086\nbits 16\norg 0\nsection .text\n%include "pdmath.inc"\n%include "pdmath.inc"\n')
        for name, skips in [("all", [])] + [("skip " + g, [g]) for g in groups] + [("core only", groups)]:
            cmd = ["nasm", "-f", "bin", "-w+error", "-I", ad, "-o", out, src] + ["-dPDM_SKIP_" + g for g in skips]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode:
                bad += 1; print("asm matrix: %s FAILED\n%s" % (name, r.stderr[:400])); continue
            sizes[name] = os.path.getsize(out)
    if not bad:
        print("asm matrix (included twice): " + ", ".join("%s %d" % kv for kv in sizes.items()) + " bytes")
    return bad


def roundtrip(H, seed, n):
    """PAGEDESK-SPEC 5.3 on the ASSEMBLY alone: fwd(inv(p)) == p for every pixel
    whose page point is representable (no clamp, no overflow) in a view that
    maps at most one pixel per pt16 unit - a coarser view cannot reach every pixel."""
    emu = H.emu; G = Gen(seed + 77); good = skipped = bad = 0
    for _ in range(n):
        v = mkview(G)
        zoom, an, ad = (int.from_bytes(v[o:o + 2], "little") for o in (10, 12, 14))
        if zoom > 1600 or zoom * an > 1600 * ad:        # more than one pixel per pt16 unit:
            skipped += 1; continue                      # not every pixel is reachable
        px = G.r.randint(-300, 700); py = G.r.randint(-300, 700)
        emu.mem_w((DSEG << 4) + SCR + 16, v)
        emu.mem_w((DSEG << 4) + emu.ovfw, b"\0\0")
        emu.call("pdm_view_inv", [SCR, SCR + 2, SCR + 16, px, py])
        if emu.mem_r((DSEG << 4) + emu.ovfw, 2) != b"\0\0":
            skipped += 1; continue
        x = int.from_bytes(emu.mem_r((DSEG << 4) + SCR, 2), "little")
        y = int.from_bytes(emu.mem_r((DSEG << 4) + SCR + 2, 2), "little")
        emu.call("pdm_view_fwd", [SCR + 4, SCR + 6, SCR + 16, x, y])
        qx = int.from_bytes(emu.mem_r((DSEG << 4) + SCR + 4, 2), "little")
        qy = int.from_bytes(emu.mem_r((DSEG << 4) + SCR + 6, 2), "little")
        if emu.mem_r((DSEG << 4) + emu.ovfw, 2) != b"\0\0":
            skipped += 1; continue
        if (qx, qy) == (px & 0xFFFF, py & 0xFFFF): good += 1
        else:
            bad += 1
            if bad < 4: print("  roundtrip: p=(%d,%d) -> pt=(%d,%d) -> (%d,%d)" % (px, py, x if x < 32768 else x - 65536, y if y < 32768 else y - 65536, qx, qy))
    print("property fwd(inv(p)) == p on the asm: %d held, %d unrepresentable (skipped), %d broken" % (good, skipped, bad))
    return bad


def negative_controls(H):
    """The checks must be able to fail.  Each control corrupts one thing the
    differential compares and must be CAUGHT; a harness that cannot see a
    broken routine has proved nothing."""
    emu = H.emu; miss = 0; slot = [0x5000]
    saved = emu.addr["pd_ds"]

    def stub(code, **kw):
        # a fresh address each time: the engine caches translated blocks
        slot[0] += 0x40; scratch_at = slot[0]
        emu.mem_w((DSEG << 4) + scratch_at, code)
        emu.addr["pd_ds"] = scratch_at
        try: return emu.call("pd_ds", [], **kw)
        finally: emu.addr["pd_ds"] = saved

    def report(label, ok):
        nonlocal miss
        print("control  %-38s %s" % (label, "caught" if ok else "MISSED")); miss += (not ok)

    _, bad = stub(bytes([0xC3]), df=True)                         # ret, DF set on entry
    report("DF left set", any("DF" in b for b in bad))
    _, bad = stub(bytes([0xB8, 0x00, 0x77, 0x8E, 0xC0, 0xC3]))    # mov ax,7700; mov es,ax; ret
    report("ES clobbered", "ES" in bad)
    _, bad = stub(bytes([0xB8, 0x00, 0x77, 0x8E, 0xD8, 0x8C, 0xC8, 0x8E, 0xD8, 0xC3]))
    report("DS changed and restored is NOT reported", not bad)
    _, bad = stub(bytes([0xBD, 0x34, 0x12, 0xC3]))                # mov bp,1234; ret
    report("BP clobbered", "BP" in bad)
    _, bad = stub(bytes([0xFC, 0x5B, 0x53, 0xC3]) )                     # cld; pop bx; push bx; ret (balanced)
    report("balanced stub is not reported", not bad)
    # a routine that is simply WRONG: asm fx_sub against the reference fx_add
    G = Gen(5); f_add = FUNCS_BY["pdm_fx_add"]; caught = 0
    orig = emu.addr["pdm_fx_add"]; emu.addr["pdm_fx_add"] = emu.addr["pdm_fx_sub"]
    for _ in range(50):
        args, s, aux = f_add[3](G)
        caught += bool(H.run_case("pdm_fx_add", f_add[1], f_add[2], args, s, aux))
    emu.addr["pdm_fx_add"] = orig
    report("asm fx_sub vs ref fx_add (%d/50 differ)" % caught, caught >= 40)
    # the far arena comparison: asm fcopy_out vs ref fcopy_in's arena effect
    f_o = FUNCS_BY["pd_fcopy_out"]; caught = 0
    orig = emu.addr["pd_fcopy_out"]; emu.addr["pd_fcopy_out"] = emu.addr["pd_ds"]
    for _ in range(50):
        args, s, aux = f_o[3](G)
        caught += bool(H.run_case("pd_fcopy_out", f_o[1], f_o[2], args, s, aux))
    emu.addr["pd_fcopy_out"] = orig
    report("asm that copies nothing (%d/50 differ)" % caught, caught >= 25)
    return miss


if __name__ == "__main__":
    sys.exit(main())
