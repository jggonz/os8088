"""The shipping 8086 text of pdc*.inc under Unicorn, as a pdc_vec backend.

Calling convention checks on EVERY call: BP, SI, DI, DS, ES, SS and SP come back
as they went in and DF is clear. Pointers are offsets into the harness segment;
the Python side allocates scratch there from BUF upward. Also counts the
instructions and the 8088 clocks a call took (pdcycles' costing rule)."""
import os, re, subprocess, struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn.x86_const import *

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.environ.get("PDC_BUILD", os.path.join(ROOT, "build", "pdcrypt"))
DSEG, SSEG, ESEG = 0x2000, 0x1000, 0x3000
BUF = 0x6000
MEM = 0x120000

def cost(b, size):                       # tests/pdmath/pdcycles.py's rule
    base = 4.34 * size
    op = b[0]; i = 0
    while op in (0x26, 0x2E, 0x36, 0x3E, 0xF2, 0xF3) and i + 1 < len(b):
        i += 1; op = b[i]
    modrm = b[i + 1] if i + 1 < len(b) else 0
    mem = (modrm >> 6) != 3
    ea = 9 if mem else 0
    if op in (0xF6, 0xF7):
        reg = (modrm >> 3) & 7
        if reg == 4: return max(base, (70 if op == 0xF6 else 125) + (12 if mem else 0))
        if reg == 6: return max(base, 155 + (12 if mem else 0))
        if reg in (2, 3): return max(base, 3 + ea + (16 if mem else 0))
    if 0x50 <= op <= 0x57: return max(base, 15)
    if op in (0x58, 0x59, 0x5A, 0x5B, 0x5D, 0x5E, 0x5F): return max(base, 12)
    if op == 0xE8: return max(base, 23)
    if op in (0xC3, 0xC2): return max(base, 20)
    if op == 0xE2: return max(base, 17)
    if 0x70 <= op <= 0x7F or op in (0xEB, 0xE9, 0xE3): return max(base, 16)
    if op in (0xD0, 0xD1): return max(base, 2 + ea + (15 if mem else 0))
    if op in (0xD2, 0xD3): return max(base, 8 + 4 * 4)
    if op in (0xA4, 0xA5, 0xAC, 0xAD): return max(base, 16)
    if op == 0xD7: return max(base, 11)
    if op in (0xAA, 0xAB): return max(base, 11)
    return max(base, 3 + ea + (6 if mem and op in (0x88, 0x89, 0x8A, 0x8B, 0x01, 0x03, 0x29, 0x2B, 0x39, 0x3B) else 0))

def build(defs):
    os.makedirs(BUILD, exist_ok=True)
    out = os.path.join(BUILD, "pdchar.bin")
    # the table is generated from the .tab so the Python and the asm cannot disagree
    cmd = ["nasm", "-f", "bin", "-w+error", "-I", os.path.join(ROOT, "apps/"), "-I", HERE + "/", "-o", out] + \
          ["-D" + d for d in defs] + [os.path.join(HERE, "pdchar.asm")]
    subprocess.check_call(cmd)
    names = [l.split()[1] for l in open(os.path.join(HERE, "pdchar.tab")) if l.strip().startswith("T ")]
    return open(out, "rb").read(), names

class Asm:
    clocks = 0; insns = 0
    def __init__(self, defs=(), measure=False):
        image, names = build([d for d in defs])
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, MEM)
        self.uc.mem_write(DSEG << 4, image)
        self.addr = {n: struct.unpack_from("<H", image, 3 + 2 * i)[0] for i, n in enumerate(names)}
        self.halt = 3 + 2 * len(names)
        assert image[self.halt] == 0xF4
        self.size = len(image)
        assert self.size < BUF, "harness image outgrew BUF"
        self.tot = [0.0, 0]
        def hook(u, addr, size, ud):
            self.tot[0] += cost(bytes(u.mem_read(addr, min(size, 4))), size); self.tot[1] += 1
        if measure: self.uc.hook_add(UC_HOOK_CODE, hook)
    def w(self, off, data): self.uc.mem_write((DSEG << 4) + off, bytes(data))
    def r(self, off, n): return bytes(self.uc.mem_read((DSEG << 4) + off, n))
    def call(self, name, args, max_insn=50_000_000):
        uc = self.uc
        uc.reg_write(UC_X86_REG_CS, DSEG); uc.reg_write(UC_X86_REG_DS, DSEG)
        uc.reg_write(UC_X86_REG_ES, ESEG); uc.reg_write(UC_X86_REG_SS, SSEG)
        uc.reg_write(UC_X86_REG_BP, 0xBEEF); uc.reg_write(UC_X86_REG_SI, 0x5151); uc.reg_write(UC_X86_REG_DI, 0xD1D1)
        for r in (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX): uc.reg_write(r, 0xDEAD)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x2)
        sp = 0xFFF0
        for a in reversed(args):
            sp -= 2; uc.mem_write((SSEG << 4) + sp, (a & 0xFFFF).to_bytes(2, "little"))
        argsp = sp
        sp -= 2; uc.mem_write((SSEG << 4) + sp, self.halt.to_bytes(2, "little"))
        uc.reg_write(UC_X86_REG_SP, sp)
        self.tot = [0.0, 0]
        uc.emu_start((DSEG << 4) + self.addr[name], (DSEG << 4) + self.halt, count=max_insn)
        self.clocks, self.insns = self.tot
        rd = uc.reg_read
        bad = [n for n, v, e in (("BP", rd(UC_X86_REG_BP), 0xBEEF), ("SI", rd(UC_X86_REG_SI), 0x5151), ("DI", rd(UC_X86_REG_DI), 0xD1D1),
               ("DS", rd(UC_X86_REG_DS), DSEG), ("ES", rd(UC_X86_REG_ES), ESEG), ("SS", rd(UC_X86_REG_SS), SSEG),
               ("SP", rd(UC_X86_REG_SP), argsp)) if v != e]
        assert not bad, (name, "clobbered", bad)
        assert not rd(UC_X86_REG_EFLAGS) & 0x400, (name, "DF set")
        return rd(UC_X86_REG_AX)

    # ---- the backend interface of pdc_vec ----
    def _words(self, off, n, per): return [int.from_bytes(self.r(off + 2 * per * i, 2 * per), "little") for i in range(n)]
    def _putw(self, off, vals, per): self.w(off, b"".join(v.to_bytes(2 * per, "little") for v in vals))
    def md5_init(self):
        self.call("_pdc_md5_init", [BUF]); return self._words(BUF, 4, 2)
    def md5_block(self, st, blk):
        self._putw(BUF, st, 2); self.w(BUF + 64, blk); self.call("_pdc_md5_block", [BUF, BUF + 64]); return self._words(BUF, 4, 2)
    def rc4(self, key, data):
        S, B = BUF, BUF + 512
        self.w(S + 300, key); self.call("_pdc_rc4_ks", [S, S + 300, len(key)])
        # run in chunks of 255 to exercise the carried state
        out = b""
        for i in range(0, len(data), 255):
            c = data[i:i + 255]; self.w(B, c); self.call("_pdc_rc4", [S, B, len(c)]); out += self.r(B, len(c))
        return out
    def aes_ks(self, key, nk):
        RK = BUF + 1024; self.w(RK + 400, key); self.call("_pdc_aes_ks", [RK, RK + 400, nk]); return self.r(RK, 16 * (nk + 7))
    def aes_enc(self, rk, nr, blk):
        RK = BUF + 1024; self.w(RK, rk); self.w(RK + 400, blk); self.call("_pdc_aes_enc", [RK, nr, RK + 400, RK + 420]); return self.r(RK + 420, 16)
    def aes_dec(self, rk, nr, blk):
        RK = BUF + 1024; self.w(RK, rk); self.w(RK + 400, blk); self.call("_pdc_aes_dec", [RK, nr, RK + 400, RK + 420]); return self.r(RK + 420, 16)
    def _cbc(self, name, rk, nr, iv, data):
        RK = BUF + 1024; self.w(RK, rk); self.w(RK + 400, iv); self.w(RK + 500, data)
        self.call(name, [RK, nr, RK + 400, RK + 500, len(data) // 16]); return self.r(RK + 500, len(data)), self.r(RK + 400, 16)
    def cbc_enc(self, rk, nr, iv, data): return self._cbc("_pdc_cbc_enc", rk, nr, iv, data)
    def cbc_dec(self, rk, nr, iv, data): return self._cbc("_pdc_cbc_dec", rk, nr, iv, data)
    def sha256_init(self):
        return [0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19]
    def sha256_block(self, st, blk):
        self._putw(BUF, st, 2); self.w(BUF + 64, blk); self.call("_pdc_sha256_block", [BUF, BUF + 64]); return self._words(BUF, 8, 2)
    def sha512_init(self):
        return [0x6a09e667f3bcc908,0xbb67ae8584caa73b,0x3c6ef372fe94f82b,0xa54ff53a5f1d36f1,0x510e527fade682d1,0x9b05688c2b3e6c1f,0x1f83d9abfb41bd6b,0x5be0cd19137e2179]
    def sha384_init(self):
        return [0xcbbb9d5dc1059ed8,0x629a292a367cd507,0x9159015a3070dd17,0x152fecd8f70e5939,0x67332667ffc00b31,0x8eb44a8768581511,0xdb0c2e0d64f98fa7,0x47b5481dbefa4fa4]
    def sha512_block(self, st, blk):
        self._putw(BUF, st, 4); self.w(BUF + 160, blk); self.call("_pdc_sha512_block", [BUF, BUF + 160]); return self._words(BUF, 8, 4)
