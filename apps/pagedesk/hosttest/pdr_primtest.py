#!/usr/bin/env python3
"""PDREND's assembly primitives (apps/pagedesk/pdrprim.inc) against their C
twins (hosttest/pdr_host.c), instruction by instruction in Unicorn's 16-bit
real mode, with SS != DS and sentinels in BP/DS (PAGEDESK-SPEC 20.3.5):

    python3 hosttest/pdr_primtest.py [-n CASES] [--seed S]

Needs nasm, cc and the `unicorn` package (exits 77 = skipped without it).
Both sides get the same random inputs - edge values included - and must agree
on the return value and on EVERY byte of the band, glyph and image arenas
(guard bytes around each band row prove nothing outside the span is touched);
BP, DS, SS:SP are checked on return and DF must be clear. A built-in mutant
(a one-bit error injected into the asm image) proves the comparison can fail."""
import argparse, ctypes, os, random, subprocess, sys, tempfile
try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_CS, UC_X86_REG_DS,
        UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_EFLAGS, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
        UC_X86_REG_SI, UC_X86_REG_DI)
except ImportError:
    print("pdr_primtest: SKIP - the `unicorn` Python package is not installed"); sys.exit(77)

HERE = os.path.dirname(os.path.abspath(__file__)); AD = os.path.dirname(HERE)
tmp = tempfile.mkdtemp(prefix="pdrprim")
asm_bin = os.path.join(tmp, "h.bin")
subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-I", AD + "/", "-o", asm_bin, os.path.join(HERE, "pdr_primharness.asm")])
so = os.path.join(tmp, "twin.so")
subprocess.check_call(["cc", "-O1", "-shared", "-fPIC", "-DPD_HOST", "-Wl,-w", "-I", AD, "-o", so,
    os.path.join(HERE, "pdr_host.c"), os.path.join(HERE, "pdr_primlib.c"), os.path.join(HERE, "pdhost.c"),
    os.path.join(AD, "pdmath.c")], stderr=subprocess.DEVNULL)
image = open(asm_bin, "rb").read()

DSEG, SSEG, ESEG = 0x2000, 0x1000, 0x3000
BSEG, GSEG, ISEG = 0x4000, 0x5000, 0x6000          # band / glyph / image-row arenas
MEM = 0x120000
names = ["muldiv", "fill1", "fill4", "glyph", "imgrow", "gp", "ip", "act", "scan", "fcopy"]
tab = dict(zip(names, [int.from_bytes(image[3 + 2 * i:5 + 2 * i], "little") for i in range(len(names))]))
HALT = 3 + 2 * len(names)
assert image[HALT] == 0xF4

class Asm:
    def __init__(self, img):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16); self.uc.mem_map(0, MEM)
        self.uc.mem_write(DSEG << 4, img)
    def w(self, lin, b): self.uc.mem_write(lin, bytes(b))
    def r(self, lin, n): return bytes(self.uc.mem_read(lin, n))
    def call(self, addr, args, df=False):
        uc = self.uc
        for reg, v in ((UC_X86_REG_CS, DSEG), (UC_X86_REG_DS, DSEG), (UC_X86_REG_ES, ESEG), (UC_X86_REG_SS, SSEG), (UC_X86_REG_BP, 0xBEEF)):
            uc.reg_write(reg, v)
        for reg in (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI):
            uc.reg_write(reg, 0xDEAD)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x2 | (0x400 if df else 0))
        sp = 0xFFF0
        for a in reversed(args):
            sp -= 2; uc.mem_write((SSEG << 4) + sp, (a & 0xFFFF).to_bytes(2, "little"))
        argsp = sp; sp -= 2
        uc.mem_write((SSEG << 4) + sp, HALT.to_bytes(2, "little"))
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.emu_start((DSEG << 4) + addr, (DSEG << 4) + HALT, count=5000000)
        rd = uc.reg_read; bad = []
        if rd(UC_X86_REG_BP) != 0xBEEF: bad.append("BP")
        if rd(UC_X86_REG_DS) != DSEG: bad.append("DS")
        if rd(UC_X86_REG_SS) != SSEG: bad.append("SS")
        if rd(UC_X86_REG_SP) != argsp: bad.append("SP")
        if rd(UC_X86_REG_EFLAGS) & 0x400: bad.append("DF set")
        return rd(UC_X86_REG_AX), bad

lib = ctypes.CDLL(so)
hmem = (ctypes.c_ubyte * 0x110000).in_dll(lib, "pd_host_mem")
class GP(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ushort) for n in "bseg boff stride x nrow skip gseg goff wbytes wpx mode col shear rowstep".split()]
class IP(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ushort) for n in "bseg boff x0 x1 sseg soff sw xlo xhi slo shi smode inv dmode dyrow col".split()]
gp = GP.in_dll(lib, "pdr_gp"); ip = IP.in_dll(lib, "pdr_ip")
U16, S16 = ctypes.c_ushort, ctypes.c_short
lib.pdr_act.argtypes = [U16, U16, U16, S16, S16, ctypes.c_void_p]; lib.pdr_act.restype = U16
lib.pdr_scan.argtypes = [U16, U16, ctypes.c_void_p, U16, S16, ctypes.c_void_p, ctypes.c_void_p, U16]; lib.pdr_scan.restype = U16
lib.pdr_fcopy.argtypes = [U16, U16, U16, U16, U16]
lib.pdr_muldiv.argtypes = [ctypes.c_short] * 3; lib.pdr_muldiv.restype = ctypes.c_short
lib.pdr_fill1.argtypes = [ctypes.c_ushort, ctypes.c_ushort, ctypes.c_short, ctypes.c_short, ctypes.c_ushort]
lib.pdr_fill4.argtypes = [ctypes.c_ushort, ctypes.c_ushort, ctypes.c_short, ctypes.c_short, ctypes.c_ushort]

def s16(v): return v - 65536 if v & 0x8000 else v
fails = []
def agree(name, desc, a, b, extra=""):
    if a != b:
        if isinstance(a, bytes):
            d = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
            extra = "first byte offsets %s (asm %s twin %s)" % (d[:6], [hex(a[i]) for i in d[:4]], [hex(b[i]) for i in d[:4]])
        fails.append("%s %s: asm and twin differ %s" % (name, desc, extra))

def run(seed, n, img):
    R = random.Random(seed); A = Asm(img)
    st = {"muldiv": 0, "fill1": 0, "fill4": 0, "glyph": 0, "imgrow": 0, "act": 0, "scan": 0, "fcopy": 0}
    # ---- muldiv ---------------------------------------------------------------
    edge = [0, 1, -1, 2, -2, 100, -100, 16000, -16000, 32767, -32767, -32768]
    def rv(): return R.choice(edge) if R.random() < .35 else R.randint(-32768, 32767)
    for _ in range(n * 3):
        a, b, c = rv(), rv(), rv()
        got, bad = A.call(tab["muldiv"], [a, b, c])
        want = lib.pdr_muldiv(a, b, c)
        st["muldiv"] += 1
        if bad: fails.append("muldiv convention %s" % bad)
        agree("muldiv", "(%d,%d,%d)" % (a, b, c), s16(got), want, "%d vs %d" % (s16(got), want))
    # ---- fill1 / fill4: a 24-byte row with guards 4 either side ------------------
    for kind in ("fill1", "fill4"):
        for _ in range(n * 2):
            row = bytearray(R.getrandbits(8) for _ in range(40))
            width_px = 16 * 8 if kind == "fill1" else 16 * 2
            x0 = R.randint(0, width_px - 1); x1 = R.randint(x0, width_px - 1)
            if R.random() < .2: x1 = x0
            if R.random() < .15: x0 = x0 & ~7; x1 = min(width_px - 1, x0 | 7)
            off = 8
            val = R.getrandbits(8) if kind == "fill1" else R.randint(0, 15)
            lin = (BSEG << 4) + 0x100
            A.w(lin, row); hmem[lin:lin + 40] = (ctypes.c_ubyte * 40).from_buffer_copy(bytes(row))
            got, bad = A.call(tab[kind], [BSEG, 0x100 + off, x0, x1, val])
            getattr(lib, "pdr_" + kind)(BSEG, 0x100 + off, x0, x1, val)
            st[kind] += 1
            if bad: fails.append("%s convention %s" % (kind, bad))
            agree(kind, "x0=%d x1=%d v=%02x" % (x0, x1, val), A.r(lin, 40), bytes(hmem[lin:lin + 40]))
    # ---- glyph -------------------------------------------------------------------
    for _ in range(n * 3):
        mode = R.choice([0, 1, 2]); wb = R.randint(1, 4); wpx = R.randint((wb - 1) * 8 + 1, wb * 8)
        nrow = R.randint(1, 12); skip = R.randint(0, 6)
        stride = 24 if mode != 2 else 24
        # any shear, including rows more than 8 below the baseline, where (shear-1) - row + 8 is
        # NEGATIVE and the shift must be a floor (SAR, as the twin's signed >>), not a logical shift
        shear = R.choice([0, 0, R.randint(1, 22)])
        if shear:                                   # the C keeps every row's pixels inside the band row
            lo = min(0, ((shear - 1) - (skip + nrow - 1) + 8) >> 2); hi = ((shear - 1) - skip + 8) >> 2
            width_px = stride * (8 if mode != 2 else 2)
            x = R.randint(-lo, max(-lo, width_px - wpx - hi))
        else:
            x = R.randint(0, (stride * (8 if mode != 2 else 2)) - wpx - 12) if mode != 2 else R.randint(0, 48 - wpx - 12)
        x = max(0, x)
        bmp = bytearray(R.getrandbits(8) for _ in range(wb * 24))
        if R.random() < .3: bmp = bytearray(0xFF for _ in bmp)
        for rr in range(24):                                   # a strike's rows are zero padded to the byte
            for bb in range(wb):
                keep = max(0, min(8, wpx - bb * 8))
                bmp[rr * wb + bb] &= (0xFF << (8 - keep)) & 0xFF
        band = bytearray(R.getrandbits(8) for _ in range(stride * 16 + 16))
        BO, GO = 0x40, 0x10
        A.w((BSEG << 4) + BO, band); A.w((GSEG << 4) + GO, bmp)
        hmem[(BSEG << 4) + BO:(BSEG << 4) + BO + len(band)] = (ctypes.c_ubyte * len(band)).from_buffer_copy(bytes(band))
        hmem[(GSEG << 4) + GO:(GSEG << 4) + GO + len(bmp)] = (ctypes.c_ubyte * len(bmp)).from_buffer_copy(bytes(bmp))
        col = R.randint(0, 15); boff = R.randint(0, 3) * stride + BO
        fields = dict(bseg=BSEG, boff=boff, stride=stride, x=x, nrow=nrow, skip=skip, gseg=GSEG, goff=GO, wbytes=wb, wpx=wpx, mode=mode, col=col, shear=shear, rowstep=stride)
        base = DSEG << 4
        blk = b"".join(fields[k].to_bytes(2, "little") for k in "bseg boff stride x nrow skip gseg goff wbytes wpx mode col shear rowstep".split())
        A.w(base + tab["gp"], blk)
        for k, v in fields.items(): setattr(gp, k, v)
        got, bad = A.call(tab["glyph"], [])
        lib.pdr_glyph()
        st["glyph"] += 1
        if bad: fails.append("glyph convention %s" % bad)
        agree("glyph", "mode=%d wb=%d wpx=%d x=%d shear=%d nrow=%d skip=%d" % (mode, wb, wpx, x, shear, nrow, skip),
              A.r((BSEG << 4) + BO, len(band)), bytes(hmem[(BSEG << 4) + BO:(BSEG << 4) + BO + len(band)]))
    # ---- imgrow ------------------------------------------------------------------
    for _ in range(n * 3):
        smode = R.choice([0, 1, 2, 3]); sw = R.randint(1, 40)
        flip = R.random() < .3; inv = R.random() < .3; dmode = R.choice([1, 4]); dyrow = R.randint(0, 3)
        srcbytes = ((sw + 7) // 8) if smode in (0, 3) else (sw if smode == 1 else sw * 3)
        src = bytearray(R.getrandbits(8) for _ in range(srcbytes + 4))
        stride = 20 if dmode == 1 else 40
        pxw = stride * (8 if dmode == 1 else 2)
        x0 = R.randint(0, pxw - 2); x1 = R.randint(x0, pxw - 1)
        step = R.choice([0x10000, 0x8000, 0x4000, 0x20000, 0x18000, R.getrandbits(18) | 1, 0x1555])
        start = R.choice([0, 0, R.randint(0, sw * 2)])
        band = bytearray(R.getrandbits(8) for _ in range(stride + 8))
        BO, SO = 0x20, 0x08
        A.w((BSEG << 4) + BO, band); A.w((ISEG << 4) + SO, src)
        hmem[(BSEG << 4) + BO:(BSEG << 4) + BO + len(band)] = (ctypes.c_ubyte * len(band)).from_buffer_copy(bytes(band))
        hmem[(ISEG << 4) + SO:(ISEG << 4) + SO + len(src)] = (ctypes.c_ubyte * len(src)).from_buffer_copy(bytes(src))
        fields = dict(bseg=BSEG, boff=BO, x0=x0, x1=x1, sseg=ISEG, soff=SO, sw=sw, xlo=0, xhi=start, slo=step & 0xFFFF, shi=step >> 16,
                      smode=smode | (0x100 if flip else 0), inv=1 if inv else 0, dmode=dmode, dyrow=dyrow, col=R.randint(0, 15))
        order = "bseg boff x0 x1 sseg soff sw xlo xhi slo shi smode inv dmode dyrow col".split()
        A.w((DSEG << 4) + tab["ip"], b"".join((fields[k] & 0xFFFF).to_bytes(2, "little") for k in order))
        for k, v in fields.items(): setattr(ip, k, v)
        got, bad = A.call(tab["imgrow"], [])
        lib.pdr_imgrow()
        st["imgrow"] += 1
        if bad: fails.append("imgrow convention %s" % bad)
        agree("imgrow", "smode=%d flip=%d inv=%d dmode=%d sw=%d x=%d..%d step=%x" % (smode, flip, inv, dmode, sw, x0, x1, step),
              A.r((BSEG << 4) + BO, len(band)), bytes(hmem[(BSEG << 4) + BO:(BSEG << 4) + BO + len(band)]))
    # ---- act / scan / fcopy: edges in a far arena, near arrays in the module's own segment ----
    ESEG2 = 0x7000; EO = 0x30
    ACT, CXA, CDA = 0x6000, 0x6200, 0x6400          # near arrays: offsets inside DSEG (past the image)
    # edges are clamped to +/-PDR_QMAX (16000) by edge_add: that is pdr_scan's precondition (its
    # in-line IDIV cannot fault while |xb - xa| < 32768), so the extremes are +/-16000
    def rq(): return R.choice([0, 1, -1, 2, 3, 4, 5, 6, 7, 8, 100, -100, 16000, -16000, 15999, -15999]) if R.random() < .25 else R.randint(-60, 60) * 4 + R.choice([0, 0, 1, 2, 3])
    for _ in range(n):
        ne = R.randint(0, 24)
        raw = bytearray()
        for _e in range(ne):
            x0, y0, x1, y1 = rq(), rq(), rq(), rq()
            if R.random() < .1: y1 = y0
            if R.random() < .1: x1 = x0
            raw += b"".join((v & 0xFFFF).to_bytes(2, "little") for v in (x0, y0, x1, y1))
        raw = bytes(raw) + bytes(8)
        A.w((ESEG2 << 4) + EO, raw); hmem[(ESEG2 << 4) + EO:(ESEG2 << 4) + EO + len(raw)] = (ctypes.c_ubyte * len(raw)).from_buffer_copy(raw)
        qlo = R.randint(-80, 80) * 4; qhi = qlo + R.choice([3, 3, 3, 7, 63, 200])
        actbuf = (ctypes.c_ushort * 300)()
        got, bad = A.call(tab["act"], [ESEG2, EO, ne, qlo, qhi, ACT])
        want = lib.pdr_act(ESEG2, EO, ne, qlo, qhi, ctypes.addressof(actbuf))
        st["act"] += 1
        if bad: fails.append("act convention %s" % bad)
        agree("act", "ne=%d" % ne, got, want)
        agree("act", "list ne=%d" % ne, A.r((DSEG << 4) + ACT, want * 2), bytes(bytearray(actbuf)[:want * 2]))
        na = want
        if na == 0: continue
        for _row in range(3):
            ys = R.randint(qlo // 4, qhi // 4) * 4 + 2 if R.random() < .8 else R.randint(-300, 300)
            nspan = R.choice([64, 64, 3, 1, 0])
            cxb = (ctypes.c_short * 70)(); cdb = (ctypes.c_ubyte * 70)()
            A.w((DSEG << 4) + CXA, bytes(140)); A.w((DSEG << 4) + CDA, bytes(70))
            got, bad = A.call(tab["scan"], [ESEG2, EO, ACT, na, ys, CXA, CDA, nspan])
            want = lib.pdr_scan(ESEG2, EO, ctypes.addressof(actbuf), na, ys, ctypes.addressof(cxb), ctypes.addressof(cdb), nspan)
            st["scan"] += 1
            if bad: fails.append("scan convention %s" % bad)
            agree("scan", "ne=%d ys=%d nspan=%d" % (ne, ys, nspan), got, want)
            agree("scan", "x ne=%d ys=%d" % (ne, ys), A.r((DSEG << 4) + CXA, want * 2), bytes(bytearray(cxb)[:want * 2]))
            agree("scan", "dir ne=%d ys=%d" % (ne, ys), A.r((DSEG << 4) + CDA, want), bytes(bytearray(cdb)[:want]))
    for _ in range(n):
        cnt = R.randint(0, 61)
        src = bytes(R.getrandbits(8) for _ in range(cnt + 2)); dst0 = bytes(R.getrandbits(8) for _ in range(cnt + 4))
        A.w((ESEG2 << 4) + 0x100, src); A.w((0x7800 << 4) + 0x20, dst0)
        hmem[(ESEG2 << 4) + 0x100:(ESEG2 << 4) + 0x100 + len(src)] = (ctypes.c_ubyte * len(src)).from_buffer_copy(src)
        hmem[(0x7800 << 4) + 0x20:(0x7800 << 4) + 0x20 + len(dst0)] = (ctypes.c_ubyte * len(dst0)).from_buffer_copy(dst0)
        got, bad = A.call(tab["fcopy"], [0x7800, 0x20, ESEG2, 0x100, cnt])
        lib.pdr_fcopy(0x7800, 0x20, ESEG2, 0x100, cnt)
        st["fcopy"] += 1
        if bad: fails.append("fcopy convention %s" % bad)
        agree("fcopy", "n=%d" % cnt, A.r((0x7800 << 4) + 0x20, len(dst0)), bytes(hmem[(0x7800 << 4) + 0x20:(0x7800 << 4) + 0x20 + len(dst0)]))
    return st

ap = argparse.ArgumentParser(); ap.add_argument("-n", type=int, default=400); ap.add_argument("--seed", type=int, default=7)
args = ap.parse_args()
st = run(args.seed, args.n, image)
print("pdr_primtest: %s cases" % ", ".join("%s %d" % kv for kv in st.items()))
# the negative control: flip one operand byte of an instruction in pdr_glyph's mode-1 OR and the run must disagree
mut = bytearray(image)
k = mut.find(bytes([0x26, 0x08, 0x25]))          # or [es:di], ah
if k < 0: print("pdr_primtest: negative control skipped (pattern not found)")
else:
    mut[k + 1] = 0x20                              # or -> and
    before = len(fails)
    run(args.seed, 60, bytes(mut))
    caught = len(fails) > before
    del fails[before:]
    if not caught: fails.append("negative control: a mutated primitive was NOT caught")
    else: print("pdr_primtest: negative control caught (a mutated glyph OR disagrees)")
mut = bytearray(image)
k = mut.find(bytes([0x39, 0x47, 0xFE, 0x7E]))     # cmp [bx-2], ax ; jle .put  (pdr_scan's insertion)
if k < 0: print("pdr_primtest: scan negative control skipped (pattern not found)")
else:
    mut[k + 3] = 0x7D                              # jle -> jge: the crossings come out unsorted
    before = len(fails); run(args.seed, 120, bytes(mut)); caught = len(fails) > before; del fails[before:]
    if not caught: fails.append("negative control: a mutated pdr_scan was NOT caught")
    else: print("pdr_primtest: negative control caught (a mutated scan insertion disagrees)")
if fails:
    for f in fails[:25]: print("FAIL", f)
    print("pdr_primtest: %d FAILED" % len(fails)); sys.exit(1)
print("pdr_primtest: asm == twin, every byte of every arena, convention kept")
