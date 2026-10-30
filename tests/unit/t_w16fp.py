#!/usr/bin/env python3
"""WIN16's 8087 emulator against exact arithmetic (SPEC.md 124.5;
docs/plans/WIN16-PLAN.md S9).

    python3 tests/unit/t_w16fp.py [--cases N] [--seed S] [-v]

`apps/win16/w16fp.inc` is part W16P_FPU of WIN16.O88: the handlers for the
`int 34h`-`3Dh` a program's OS fixups become on a machine with no
coprocessor. It is self-contained on purpose, so this row assembles it
alone, loads it into unicorn and runs PATCHED instruction sequences through
it - the bytes the loader leaves behind - with nothing of WIN16 around.

THE ORACLE IS NOT AN FPU. Unicorn is QEMU 5's softfloat, whose x87
exception flags and FPREM are not to be trusted, and this host may have no
x87 at all. The reference is a model in `fractions`: every operation exact,
then rounded once to the precision, range and rounding mode of where the
answer goes, which is what the 8087 does. Values AND flags are compared:
the stored result's bits, the status word's six exception bits, C3/C2/C0
where the instruction sets them, and TOP.

The cases are random but seeded, and the operands are chosen to reach the
cases that matter: short mantissas (ties), every rounding mode at every
precision, extended exponents that overflow and underflow a double on the
way to memory, denormals, signed zeros, infinities and NaNs. Then the
fixed checks: the unmasked exception's call to the C runtime's handler with
WIN87EM's code, the stack's overflow and underflow, an instruction the
emulator lacks ending the program through the header's far pointer with
its name, int 3Ch's segment byte, and __FPMATH's thirteen functions.
"""
import argparse
import math
import os
import random
import struct
import subprocess
import sys
import tempfile
from fractions import Fraction as F

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, eq, done                       # noqa: E402
import os88ne                                              # noqa: E402

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX,
                                   UC_X86_REG_DX, UC_X86_REG_SP, UC_X86_REG_SS,
                                   UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_CS,
                                   UC_X86_REG_IP, UC_X86_REG_FLAGS, UC_X86_REG_BP)
except Exception:                                          # pragma: no cover
    print("t_w16fp: SKIP - unicorn is not importable here")
    sys.exit(77)

IE, DE, ZE, OE, UE, PE = 1, 2, 4, 8, 16, 32

# --- the machine ---------------------------------------------------------------
FPSEG = 0x2000          # the part
CSEG = 0x1000           # the test's code
DSEG = 0x3000           # its data
ESEG = 0x5000           # int 3Ch's ES operand
SSEG = 0x4000
END = 0xF000            # CSEG's last instruction's address: the stop
HAND = 0xE000           # the C runtime's handler: records AX, retf
ENDP = 0xE100           # WIN16's x87 stub: marks, then jumps to END
REC = 0xEFF0            # what HAND and ENDP record


def assemble():
    src = os.path.join(ROOT, "apps", "win16", "w16fp.inc")
    with tempfile.TemporaryDirectory() as td:
        wrap = os.path.join(td, "w.asm")
        out = os.path.join(td, "w16fp.bin")
        with open(wrap, "w") as f:
            # w16fp.inc's section FOLLOWS .w16dlg, the dialog part (124.18.7)
            f.write('cpu 8086\nbits 16\nsection .text\n'
                    'section .w16dlg vstart=0 align=1\n'
                    '%%include "%s"\n' % src)
        subprocess.run(["nasm", "-w+error", "-f", "bin", "-o", out, wrap], check=True)
        return open(out, "rb").read()


PART = assemble()
VEC = struct.unpack_from("<10H", PART, 10)


class Run:
    """One fresh machine: the part loaded clean, a program at CSEG:0."""

    def __init__(self, code, data=b"", esdata=b"", hand=False):
        mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        part = bytearray(PART)
        struct.pack_into("<HH", part, 30, ENDP, CSEG)
        if hand:
            struct.pack_into("<HH", part, 34, HAND, CSEG)
        mu.mem_write(FPSEG * 16, bytes(part))
        mu.mem_write(CSEG * 16, bytes(code))
        # HAND: mov [cs:REC], ax ; inc word [cs:REC+2] ; retf
        mu.mem_write(CSEG * 16 + HAND, bytes([0x2E, 0xA3]) + struct.pack("<H", REC)
                     + bytes([0x2E, 0xFF, 0x06]) + struct.pack("<H", REC + 2) + b"\xCB")
        # ENDP: mov byte [cs:REC+4], 1 ; jmp far CSEG:END
        mu.mem_write(CSEG * 16 + ENDP, bytes([0x2E, 0xC6, 0x06]) + struct.pack("<H", REC + 4)
                     + b"\x01\xEA" + struct.pack("<HH", END, CSEG))
        mu.mem_write(CSEG * 16 + END, b"\xF4")
        mu.mem_write(DSEG * 16, bytes(data))
        mu.mem_write(ESEG * 16, bytes(esdata))
        mu.reg_write(UC_X86_REG_DS, DSEG)
        mu.reg_write(UC_X86_REG_ES, ESEG)
        mu.reg_write(UC_X86_REG_SS, SSEG)
        mu.reg_write(UC_X86_REG_SP, 0xFFF0)
        mu.reg_write(UC_X86_REG_CS, CSEG)
        mu.hook_add(UC_HOOK_INTR, self._int)
        self.mu = mu
        self.pend = None

    def _int(self, uc, n, ud):
        self.pend = n
        uc.emu_stop()

    def push(self, w):
        sp = (self.mu.reg_read(UC_X86_REG_SP) - 2) & 0xFFFF
        self.mu.reg_write(UC_X86_REG_SP, sp)
        self.mu.mem_write(SSEG * 16 + sp, struct.pack("<H", w))

    def go(self, cs=CSEG, ip=0, limit=200000):
        mu = self.mu
        mu.reg_write(UC_X86_REG_CS, cs)
        lin = cs * 16 + ip
        for _ in range(limit):
            self.pend = None
            mu.emu_start(lin, CSEG * 16 + END, count=2000000)
            n = self.pend
            if n is None:
                return
            if not 0x34 <= n <= 0x3D:
                raise RuntimeError("int %02Xh" % n)
            # the CPU's interrupt: FLAGS, CS, IP (past the int), then the vector
            ip = mu.reg_read(UC_X86_REG_IP)
            self.push(mu.reg_read(UC_X86_REG_FLAGS))
            self.push(mu.reg_read(UC_X86_REG_CS))
            self.push(ip)
            mu.reg_write(UC_X86_REG_CS, FPSEG)
            lin = FPSEG * 16 + VEC[n - 0x34]
        raise RuntimeError("runaway")

    def far_call(self, off, **regs):
        """call far FPSEG:off from CSEG:END - __FPMATH"""
        mu = self.mu
        for k, v in regs.items():
            mu.reg_write({"ax": UC_X86_REG_AX, "bx": UC_X86_REG_BX, "cx": UC_X86_REG_CX,
                          "dx": UC_X86_REG_DX}[k], v)
        self.push(CSEG)
        self.push(END)
        mu.reg_write(UC_X86_REG_CS, FPSEG)
        mu.emu_start(FPSEG * 16 + off, CSEG * 16 + END, count=2000000)
        return (mu.reg_read(UC_X86_REG_AX), mu.reg_read(UC_X86_REG_DX),
                mu.reg_read(UC_X86_REG_FLAGS) & 1)

    def mem(self, seg, off, n):
        return bytes(self.mu.mem_read(seg * 16 + off, n))


# --- the patched instructions ------------------------------------------------------
def esc(e, reg, mem=None, i=0, seg=None):
    """An x87 instruction as the loader leaves it: `int 34h+esc` and its modrm
    (`int 3Ch` and a segment byte for an override; ES here)"""
    if mem is None:
        return bytes([0xCD, 0x34 + e, 0xC0 | reg << 3 | i])
    m = bytes([reg << 3 | 6]) + struct.pack("<H", mem)
    if seg == "es":
        return bytes([0xCD, 0x3C, 0xD8 + e]) + m
    return bytes([0xCD, 0x34 + e]) + m


FNINIT = esc(3, 4, i=3)
FWAIT = b"\xCD\x3D"
def FLDCW(a): return esc(1, 5, a)
def FNSTSW(a): return esc(5, 7, a)
def FLD80(a): return esc(3, 5, a)
def FSTP80(a): return esc(3, 7, a)
def FLD64(a): return esc(5, 0, a)
def FLD32(a): return esc(1, 0, a)

# data layout
A_CW, A_SW, A_X, A_Y, A_R, A_S = 0x00, 0x02, 0x10, 0x20, 0x40, 0x60

# --- values ------------------------------------------------------------------------------
EMIN, EMAX = -16382, 16383
INDEF80 = bytes.fromhex("00000000000000C0FFFF")


class V:
    """kind: 'z', 'n', 'i' (infinity), 'q' (NaN); s the sign (0/1); x the
    magnitude (Fraction) of a number; m the 64-bit significand of a NaN"""
    __slots__ = ("k", "s", "x", "m")

    def __init__(self, k, s=0, x=F(0), m=0):
        self.k, self.s, self.x, self.m = k, s, x, m

    def val(self):
        return -self.x if self.s else self.x

    def __repr__(self):
        if self.k == "n":
            e = ilog2(self.x)
            if abs(e) < 1000:
                return "%s%r" % ("-" if self.s else "", float(self.x))
            return "%s%.17gp%d" % ("-" if self.s else "", float(self.x / F(2) ** e), e)
        return "%s%s%s" % ("-" if self.s else "", self.k, "" if self.k != "q" else "%016X" % self.m)


def num(fr):
    if fr == 0:
        return V("z")
    return V("n", 1 if fr < 0 else 0, abs(fr))


def ilog2(x):
    """floor(log2 x) of a positive Fraction"""
    e = x.numerator.bit_length() - x.denominator.bit_length()
    if F(2) ** e > x:
        e -= 1
    return e


def dec80(b):
    lo, se = struct.unpack("<QH", b)
    s, e = se >> 15, se & 0x7FFF
    if e == 0x7FFF:
        if lo & ((1 << 63) - 1) == 0:
            return V("i", s)
        return V("q", s, m=lo)
    if lo == 0:
        return V("z", s)
    return V("n", s, F(lo) * F(2) ** ((e if e else 1) - 16383 - 63))


def enc80(v):
    """the register's bytes for a value already in the extended range"""
    if v.k == "z":
        return struct.pack("<QH", 0, v.s << 15)
    if v.k == "i":
        return struct.pack("<QH", 1 << 63, v.s << 15 | 0x7FFF)
    if v.k == "q":
        return struct.pack("<QH", v.m, v.s << 15 | 0x7FFF)
    e = ilog2(v.x)
    if e < EMIN:
        m = v.x / F(2) ** (EMIN - 63)
        assert m.denominator == 1
        return struct.pack("<QH", int(m), v.s << 15)
    m = v.x / F(2) ** (e - 63)
    assert m.denominator == 1 and m < 2 ** 64, v
    return struct.pack("<QH", int(m), v.s << 15 | (e + 16383))


def decmem(b, mb, eb):
    """an m64 (mb 52, eb 11) or m32 (23, 8): (value, flags)"""
    bits = int.from_bytes(b, "little")
    s = bits >> (mb + eb)
    e = (bits >> mb) & ((1 << eb) - 1)
    f = bits & ((1 << mb) - 1)
    bias = (1 << (eb - 1)) - 1
    if e == (1 << eb) - 1:
        if f == 0:
            return V("i", s), 0
        m = (1 << 63) | (f << (63 - mb))
        fl = 0
        if not m & (1 << 62):
            fl, m = IE, m | (1 << 62)
        return V("q", s, m=m), fl
    if e == 0:
        if f == 0:
            return V("z", s), 0
        return V("n", s, F(f) * F(2) ** (1 - bias - mb)), DE
    return V("n", s, F((1 << mb) | f) * F(2) ** (e - bias - mb)), 0


def encmem(v, mb, eb):
    """a value already rounded to the format -> its bytes"""
    bias = (1 << (eb - 1)) - 1
    n = (mb + eb + 1) // 8
    if v.k == "z":
        bits = 0
    elif v.k == "i":
        bits = ((1 << eb) - 1) << mb
    elif v.k == "q":
        bits = ((1 << eb) - 1) << mb | ((v.m >> (63 - mb)) & ((1 << mb) - 1))
    else:
        e = ilog2(v.x)
        if e < 1 - bias:
            m = v.x / F(2) ** (1 - bias - mb)
            assert m.denominator == 1
            bits = int(m)
        else:
            m = v.x / F(2) ** (e - mb)
            assert m.denominator == 1
            bits = (e + bias) << mb | (int(m) & ((1 << mb) - 1))
    return (bits | v.s << (mb + eb)).to_bytes(n, "little")


def rnd(v, P, emin, emax, rc, fl=0):
    """v exact -> (rounded, flags), every exception masked"""
    if v.k != "n":
        return v, fl
    a, neg = v.x, v.s
    e = ilog2(a)
    tiny = e < emin
    q = (emin if tiny else e) - (P - 1)
    m = a / F(2) ** q
    n = m.numerator // m.denominator
    rem = m - n
    if rem:
        fl |= PE
        if tiny:
            fl |= UE
        if (rc == 0 and (rem > F(1, 2) or (rem == F(1, 2) and n & 1))) or \
           (rc == 1 and neg) or (rc == 2 and not neg):
            n += 1
    if n == 0:
        return V("z", neg), fl
    if n == 1 << P:
        n, q = 1 << (P - 1), q + 1
    r = F(n) * F(2) ** q
    if ilog2(r) > emax:
        fl |= OE | PE
        if rc == 3 or (rc == 1 and not neg) or (rc == 2 and neg):
            return V("n", neg, F((1 << P) - 1) * F(2) ** (emax - P + 1)), fl
        return V("i", neg), fl
    return V("n", neg, r), fl


def quiet(v, fl=0):
    if v.k == "q" and not v.m & (1 << 62):
        return V("q", v.s, m=v.m | (1 << 62)), fl | IE
    return v, fl


def indef():
    return dec80(INDEF80)


def nan2(a, b):
    """the 8087's NaN answer, or None"""
    if a.k != "q" and b.k != "q":
        return None
    fl = 0
    for x in (a, b):
        if x.k == "q" and not x.m & (1 << 62):
            fl |= IE
    if a.k == "q" and b.k == "q":
        r = a if a.m >= b.m else b
    else:
        r = a if a.k == "q" else b
    return V("q", r.s, m=r.m | (1 << 62)), fl


def m_add(a, b, P, rc):
    n = nan2(a, b)
    if n:
        return n
    if a.k == "i" or b.k == "i":
        if a.k == "i" and b.k == "i" and a.s != b.s:
            return indef(), IE
        return (a if a.k == "i" else b), 0
    if a.k == "z" and b.k == "z":
        if a.s == b.s:
            return a, 0
        return V("z", 1 if rc == 1 else 0), 0
    if b.k == "z":
        return rnd(a, P, EMIN, EMAX, rc)
    if a.k == "z":
        return rnd(b, P, EMIN, EMAX, rc)
    t = a.val() + b.val()
    if t == 0:
        return V("z", 1 if rc == 1 else 0), 0
    return rnd(num(t), P, EMIN, EMAX, rc)


def neg(b):
    return b if b.k == "q" else V(b.k, b.s ^ 1, b.x, b.m)


def m_mul(a, b, P, rc):
    n = nan2(a, b)
    if n:
        return n
    s = a.s ^ b.s
    if a.k == "i" or b.k == "i":
        if a.k == "z" or b.k == "z":
            return indef(), IE
        return V("i", s), 0
    if a.k == "z" or b.k == "z":
        return V("z", s), 0
    return rnd(V("n", s, a.x * b.x), P, EMIN, EMAX, rc)


def m_div(a, b, P, rc):
    n = nan2(a, b)
    if n:
        return n
    s = a.s ^ b.s
    if a.k == "i":
        return (indef(), IE) if b.k == "i" else (V("i", s), 0)
    if b.k == "i":
        return V("z", s), 0
    if b.k == "z":
        return (indef(), IE) if a.k == "z" else (V("i", s), ZE)
    if a.k == "z":
        return V("z", s), 0
    return rnd(V("n", s, a.x / b.x), P, EMIN, EMAX, rc)


def m_sqrt(a, P, rc):
    if a.k == "q":
        return quiet(a)
    if a.k == "z":
        return a, 0
    if a.s:
        return indef(), IE
    if a.k == "i":
        return a, 0
    # exact enough: isqrt at 200 extra bits, a quarter below the next integer
    # when inexact so no tie can be manufactured
    k = 200 + max(0, -ilog2(a.x))
    N = a.x * F(4) ** k
    Ni = N.numerator // N.denominator
    r = math.isqrt(Ni)
    exact = r * r == N
    x = F(r) if exact else F(4 * r + 1, 4)
    return rnd(V("n", 0, x / F(2) ** k), P, EMIN, EMAX, rc)


def cmpcc(a, b):
    """C3 C2 C0 (the status word's high byte, bits 6 2 0), flags"""
    if a.k == "q" or b.k == "q":
        return 0x45, IE
    def key(v):
        if v.k == "z":
            return F(0), 0
        if v.k == "i":
            return None, (-1 if v.s else 1)
        return v.val(), 0
    ka, ia = key(a)
    kb, ib = key(b)
    if ia or ib:
        x, y = ia, ib
    else:
        x, y = ka, kb
    if x == y:
        return 0x40, 0
    return (0x01 if x < y else 0x00), 0


# --- the generators -------------------------------------------------------------------
def r80(rng, emin=-60, emax=60):
    """a random extended value: a short mantissa often, so ties happen"""
    roll = rng.random()
    if roll < 0.04:
        return V("z", rng.randrange(2))
    if roll < 0.07:
        return V("i", rng.randrange(2))
    if roll < 0.09:
        return V("q", rng.randrange(2), m=(1 << 63) | (rng.randrange(2) << 62) | rng.getrandbits(40) | 1)
    bits = rng.choice((1, 2, 3, 8, 23, 24, 25, 26, 52, 53, 54, 55, 63, 64))
    m = (1 << (bits - 1)) | rng.getrandbits(bits - 1) if bits > 1 else 1
    e = rng.randint(emin, emax)
    return V("n", rng.randrange(2), F(m) * F(2) ** (e - bits + 1))


def r64(rng):
    roll = rng.random()
    if roll < 0.05:
        return struct.pack("<d", rng.choice((0.0, -0.0)))
    if roll < 0.08:
        return struct.pack("<d", rng.choice((math.inf, -math.inf)))
    if roll < 0.10:
        return struct.pack("<Q", 0x7FF0000000000000 | rng.getrandbits(51) | rng.randrange(2) << 63 | 1)
    if roll < 0.14:
        return struct.pack("<Q", rng.getrandbits(52) | 1 | rng.randrange(2) << 63)  # a denormal
    v = r80(rng, -40, 40)
    if v.k != "n":
        v = V("n", 0, F(3, 2))
    v, _ = rnd(v, 53, -1022, 1023, 0)
    return encmem(v, 52, 11)


def r32(rng):
    roll = rng.random()
    if roll < 0.06:
        return struct.pack("<I", rng.getrandbits(23) | 1 | rng.randrange(2) << 31)
    v = r80(rng, -20, 20)
    if v.k == "q":
        return struct.pack("<I", 0x7FC00000 | rng.getrandbits(20))
    if v.k == "n":
        v, _ = rnd(v, 24, -126, 127, 0)
    return encmem(v, 23, 8)


def cw(P, rc, mask=0x3F):
    return mask | 0x40 | {24: 0, 53: 2, 64: 3}[P] << 8 | rc << 10


def flagstr(f):
    return "".join(n for b, n in ((IE, "I"), (DE, "D"), (ZE, "Z"), (OE, "O"), (UE, "U"), (PE, "P")) if f & b) or "-"


VERBOSE = [False]
FAILS = [0]


def compare(what, got_b, want_b, got_sw, want_fl, want_cc=None, top=None):
    ok = got_b == want_b and (got_sw & 0x3F) == want_fl
    if want_cc is not None:
        ok = ok and (got_sw >> 8) & 0x45 == want_cc
    if top is not None:
        ok = ok and (got_sw >> 11) & 7 == top
    if not ok and FAILS[0] < 25:
        FAILS[0] += 1
        check(False, what, got="%s sw=%04X(%s)" % (got_b.hex(), got_sw, flagstr(got_sw & 0x3F)),
              want="%s flags=%s cc=%s" % (want_b.hex(), flagstr(want_fl),
                                          None if want_cc is None else "%02X" % want_cc))
    elif ok:
        check(True, what)
    return ok


# --- the random rows ---------------------------------------------------------------------
ARITH = ("add", "mul", None, None, "sub", "subr", "div", "divr")


def model_op(op, a, b, P, rc):
    if op == "add":
        return m_add(a, b, P, rc)
    if op == "sub":
        return m_add(a, neg(b), P, rc)
    if op == "subr":
        return m_add(b, neg(a), P, rc)
    if op == "mul":
        return m_mul(a, b, P, rc)
    if op == "div":
        return m_div(a, b, P, rc)
    return m_div(b, a, P, rc)


def case_arith(rng):
    """st(0) op operand: the memory forms (m32, m64, m16/m32 integers) and the
    register forms (D8 st(i), DC st(i), DE st(i) popping)"""
    P, rc = rng.choice((24, 53, 64)), rng.randrange(4)
    reg = rng.choice((0, 1, 4, 5, 6, 7))
    op = ARITH[reg]
    form = rng.choice(("m32", "m64", "i16", "i32", "d8", "dc", "de"))
    a = r80(rng)
    data = bytearray(0x80)
    struct.pack_into("<H", data, A_CW, cw(P, rc))
    data[A_X:A_X + 10] = enc80(a)
    code = FNINIT + FLDCW(A_CW) + FLD80(A_X)
    fl = 0
    if form in ("m32", "m64", "i16", "i32"):
        if form == "m64":
            raw = r64(rng)
            b, fl = decmem(raw, 52, 11)
            e = 4
        elif form == "m32":
            raw = r32(rng)
            b, fl = decmem(raw, 23, 8)
            e = 0
        elif form == "i16":
            iv = rng.randint(-32768, 32767)
            raw, b, e = struct.pack("<h", iv), num(F(iv)), 6
        else:
            iv = rng.choice((rng.randint(-2 ** 31, 2 ** 31 - 1), rng.randint(-99, 99)))
            raw, b, e = struct.pack("<i", iv), num(F(iv)), 2
        data[A_Y:A_Y + len(raw)] = raw
        code += esc(e, reg, A_Y)
        r, f2 = model_op(op, a, b, P, rc)
        top = 7
    else:
        b = r80(rng)
        data[A_Y:A_Y + 10] = enc80(b)
        code += FLD80(A_Y)                         # st0 = b, st1 = a
        if form == "d8":                           # st0 = b op a
            code += esc(0, reg, i=1)
            r, f2 = model_op(op, b, a, P, rc)
            top = 6
        else:                                      # st1 = a op b (Intel's DC/DE)
            code += esc(4 if form == "dc" else 6, reg ^ 1 if reg >= 4 else reg, i=1)
            r, f2 = model_op(op, a, b, P, rc)
            if form == "dc":
                code += esc(3, 7, A_S)             # discard st0 into A_S
            top = 7
    code += FSTP80(A_R) + FNSTSW(A_SW)
    run = Run(code + b"\xEA" + struct.pack("<HH", END, CSEG), data)
    run.go()
    got = run.mem(DSEG, A_R, 10)
    sw = struct.unpack("<H", run.mem(DSEG, A_SW, 2))[0]
    top = 7 if form == "d8" else 0
    return compare("%s %s P%d rc%d %r %r -> %r" % (form, op, P, rc, a, b, r),
                   got, enc80(r), sw, fl | f2, top=top)


def case_store(rng):
    """fst/fstp m64 and m32 of an extended value: a double rounding when it
    came from arithmetic, one rounding here; overflow, underflow, denormals"""
    rc = rng.randrange(4)
    fmt = rng.choice(("m64", "m32"))
    mb, eb = (52, 11) if fmt == "m64" else (23, 8)
    bias = (1 << (eb - 1)) - 1
    v = r80(rng, -bias - mb - 4, bias + 3) if rng.random() < 0.6 else r80(rng)
    data = bytearray(0x80)
    struct.pack_into("<H", data, A_CW, cw(64, rc))
    data[A_X:A_X + 10] = enc80(v)
    e = 5 if fmt == "m64" else 1
    pop = rng.randrange(2)
    code = FNINIT + FLDCW(A_CW) + FLD80(A_X) + esc(e, 2 + pop, A_R) + FNSTSW(A_SW)
    run = Run(code + b"\xEA" + struct.pack("<HH", END, CSEG), data)
    run.go()
    n = (mb + eb + 1) // 8
    got = run.mem(DSEG, A_R, n)
    sw = struct.unpack("<H", run.mem(DSEG, A_SW, 2))[0]
    w, fl = quiet(v)
    if w.k == "n":
        w, fl = rnd(w, mb + 1, 1 - bias, bias, rc, fl)
    return compare("%s rc%d %r" % ("fst" + "p" * pop + " " + fmt, rc, v),
                   got, encmem(w, mb, eb), sw, fl, top=0 if pop else 7)


def case_int(rng):
    """fild m16/m32/m64 and fist/fistp: every rounding mode, the range's edge"""
    rc = rng.randrange(4)
    size = rng.choice((2, 4, 8))
    data = bytearray(0x80)
    struct.pack_into("<H", data, A_CW, cw(64, rc))
    lim = 1 << (size * 8 - 1)
    if rng.random() < 0.3:
        iv = rng.randint(-lim, lim - 1)
        raw = iv.to_bytes(size, "little", signed=True)
        data[A_Y:A_Y + size] = raw
        e, reg = {2: (7, 0), 4: (3, 0), 8: (7, 5)}[size]
        code = FNINIT + FLDCW(A_CW) + esc(e, reg, A_Y) + FSTP80(A_R) + FNSTSW(A_SW)
        run = Run(code + b"\xEA" + struct.pack("<HH", END, CSEG), data)
        run.go()
        return compare("fild m%d %d" % (size * 8, iv), run.mem(DSEG, A_R, 10), enc80(num(F(iv))),
                       struct.unpack("<H", run.mem(DSEG, A_SW, 2))[0], 0, top=0)
    roll = rng.random()
    if roll < 0.3:
        v = num(F(rng.choice((lim, -lim, lim - 1, -lim - 1))) + F(rng.choice((0, 1, 2, 3)), 4)
                * rng.choice((1, -1)))
        v, _ = rnd(v, 64, EMIN, EMAX, 0)
    elif roll < 0.4:
        v = r80(rng)
    else:
        v = num(F(rng.randint(-4 * lim, 4 * lim), rng.choice((1, 2, 4, 3, 8, 1024))))
        if abs(v.val()) > 2 * lim:
            v = num(v.val() / 4)
        v, _ = rnd(v, 64, EMIN, EMAX, 0)
    data[A_X:A_X + 10] = enc80(v)
    e, reg = {2: (7, 3), 4: (3, 3), 8: (7, 7)}[size]
    pop = True
    if size != 8 and rng.random() < 0.3:
        reg, pop = 2, False
    code = FNINIT + FLDCW(A_CW) + FLD80(A_X) + esc(e, reg, A_R) + FNSTSW(A_SW)
    run = Run(code + b"\xEA" + struct.pack("<HH", END, CSEG), data)
    run.go()
    fl = 0
    if v.k != "n" and v.k != "z":
        fl, iv = IE, -lim
    elif v.k == "z":
        iv = 0
    else:
        x = v.val()
        n = math.floor(x)
        rem = x - n
        if rem:
            fl = PE
            if (rc == 0 and (rem > F(1, 2) or (rem == F(1, 2) and n & 1))) or rc == 2 or \
               (rc == 3 and x < 0):
                n += 1
        iv = n
        if not -lim <= iv < lim:
            fl, iv = IE, -lim
    return compare("fist%s m%d rc%d %r" % ("p" if pop else "", size * 8, rc, v),
                   run.mem(DSEG, A_R, size), iv.to_bytes(size, "little", signed=True),
                   struct.unpack("<H", run.mem(DSEG, A_SW, 2))[0], fl, top=0 if pop else 7)


def case_unary(rng):
    """fsqrt, frndint, fchs, fabs, fscale, fxtract, fprem, fcom, ftst, fxam"""
    what = rng.choice(("sqrt", "rndint", "chs", "abs", "scale", "xtract", "prem",
                       "com", "tst", "xam"))
    P, rc = rng.choice((24, 53, 64)), rng.randrange(4)
    a = r80(rng)
    if what == "sqrt" and a.k == "n" and rng.random() < 0.7:
        a = V("n", 0, a.x)
    data = bytearray(0x80)
    struct.pack_into("<H", data, A_CW, cw(P, rc))
    data[A_X:A_X + 10] = enc80(a)
    b = None
    code = FNINIT + FLDCW(A_CW)
    cc = None
    fl = 0
    top = 7
    if what in ("scale", "prem", "com"):
        b = r80(rng, -8, 70) if what == "prem" else r80(rng)
        if what == "scale":
            b = num(F(rng.randint(-20000, 20000), rng.choice((1, 2, 4)))) if rng.random() < 0.8 else b
        data[A_Y:A_Y + 10] = enc80(b)
        code += FLD80(A_Y)
    code += FLD80(A_X)
    if what == "sqrt":
        code += esc(1, 7, i=2)
        r, fl = m_sqrt(a, P, rc)
    elif what == "rndint":
        code += esc(1, 7, i=4)
        r, fl = quiet(a)
        if r.k == "n":
            x = r.val()
            n = math.floor(x)
            rem = x - n
            if rem:
                fl = PE
                if (rc == 0 and (rem > F(1, 2) or (rem == F(1, 2) and n & 1))) or rc == 2 or \
                   (rc == 3 and x < 0):
                    n += 1
            r = V("z", a.s) if n == 0 else num(F(n))
    elif what == "chs":
        code += esc(1, 4, i=0)
        r = V(a.k, a.s ^ 1, a.x, a.m)
    elif what == "abs":
        code += esc(1, 4, i=1)
        r = V(a.k, 0, a.x, a.m)
    elif what == "scale":
        code += esc(1, 7, i=5)
        n = nan2(a, b)
        if n:
            r, fl = n
        elif b.k == "i":
            if (b.s == 0 and a.k == "z") or (b.s == 1 and a.k == "i"):
                r, fl = indef(), IE
            else:
                r = a if a.k in ("z", "i") else V("i" if b.s == 0 else "z", a.s)
        elif b.k == "z" or a.k != "n":
            r = a
        else:
            k = math.trunc(b.val())
            k = max(-32000, min(32000, k))
            r, fl = rnd(V("n", a.s, a.x * F(2) ** k), 64, EMIN, EMAX, rc)
        top = 6
    elif what == "xtract":
        code += esc(1, 6, i=4)
        r, fl = a, 0
        if a.k == "q":
            r, fl = quiet(a)
            ex = r
        elif a.k == "i":
            ex = V("i", 0)
        elif a.k == "z":
            ex, fl = V("i", 1), ZE
        else:
            e = ilog2(a.x)
            ex, r = num(F(e)), V("n", a.s, a.x / F(2) ** e)
        top = 6
        b = ex
    elif what == "prem":
        # fprem until C2 is clear, as a program does: `fprem / fnstsw ax /
        # sahf / jp` - here the loop is the test's, fifty times at most
        code += esc(1, 7, i=0) + FNSTSW(A_SW)
        n = nan2(a, b)
        if n:
            r, fl = n
            q = 0
        elif a.k == "i" or b.k == "z":
            r, fl, q = indef(), IE, 0
        elif a.k == "z" or b.k == "i":
            r, q = a, 0
        else:
            qq = math.trunc(a.x / b.x)
            rr = a.x - qq * b.x
            r = V("z", a.s) if rr == 0 else V("n", a.s, rr)
            q = qq
            r, _ = rnd(r, 64, EMIN, EMAX, rc)
        cc = (0x01 if q & 4 else 0) | (0x40 if q & 2 else 0)
        c1 = 0x02 if q & 1 else 0
        top = 6
    elif what == "com":
        pop = rng.randrange(2)
        code += esc(0, 2 + pop, i=1)
        cc, fl = cmpcc(a, b)
        r = b if pop else a
        top = 6
    elif what == "tst":
        code += esc(1, 4, i=4)
        cc, fl = cmpcc(a, V("z"))
        r = a
    elif what == "xam":
        code += esc(1, 4, i=5)
        r = a
        if a.k == "q":
            cc = 0x01
        elif a.k == "i":
            cc = 0x05
        elif a.k == "z":
            cc = 0x40
        else:
            cc = 0x44 if ilog2(a.x) < EMIN else 0x04
    if what == "prem":
        body = code
        loop = esc(1, 7, i=0) + FNSTSW(A_SW)
        code = body
        run = Run(code + b"\xEA" + struct.pack("<HH", END, CSEG), data)
        run.go()
        for _ in range(600):
            sw = struct.unpack("<H", run.mem(DSEG, A_SW, 2))[0]
            if not sw & 0x0400:
                break
            run.mu.mem_write(CSEG * 16, loop + b"\xEA" + struct.pack("<HH", END, CSEG))
            run.go()
        run.mu.mem_write(CSEG * 16, FSTP80(A_R) + FNSTSW(A_SW) + b"\xEA" + struct.pack("<HH", END, CSEG))
        run.go()
        sw = struct.unpack("<H", run.mem(DSEG, A_SW, 2))[0]
        ok = compare("fprem %r %r -> %r q%d" % (a, b, r, q if isinstance(q, int) else 0),
                     run.mem(DSEG, A_R, 10), enc80(r), sw, fl, want_cc=cc, top=7)
        if ok and r.k == "n" and not fl:
            eq((sw >> 8) & 2, c1, "fprem's C1 is the quotient's bit 0 (%r %r)" % (a, b))
        return ok
    code += FNSTSW(A_SW) + FSTP80(A_R)
    if b is not None and what in ("xtract",):
        code += FSTP80(A_S)
    run = Run(code + b"\xEA" + struct.pack("<HH", END, CSEG), data)
    run.go()
    sw = struct.unpack("<H", run.mem(DSEG, A_SW, 2))[0]
    if what == "xam":
        fl = 0
    ok = compare("f%s P%d rc%d %r %s-> %r" % (what, P, rc, a, "" if b is None else "%r " % b, r),
                 run.mem(DSEG, A_R, 10), enc80(r), sw, fl, want_cc=cc,
                 top=None if what == "com" else top)
    if ok and what == "xtract":
        eq(run.mem(DSEG, A_S, 10), enc80(b), "fxtract's exponent (%r)" % a)
    if ok and what == "xam":
        eq((sw >> 9) & 1, a.s, "fxam's C1 is the sign (%r)" % a)
    return ok


# --- the fixed rows ------------------------------------------------------------------------------
def tail():
    return b"\xEA" + struct.pack("<HH", END, CSEG)


def fixed():
    # 2.01 - 2 in a double: the Calculator's own question, and the answer the
    # host's double gives
    data = bytearray(0x80)
    data[A_X:A_X + 8] = struct.pack("<d", 2.01)
    data[A_Y:A_Y + 8] = struct.pack("<d", 2.0)
    run = Run(FNINIT + FLDCW(A_CW) + FLD64(A_X) + esc(4, 4, A_Y) + esc(5, 3, A_R) + tail(),
              bytes(data[:A_CW]) + struct.pack("<H", 0x027F) + bytes(data[2:]))
    run.go()
    eq(struct.unpack("<d", run.mem(DSEG, A_R, 8))[0], 2.01 - 2.0, "2.01 - 2 at PC 53 is the host's double")

    # the constants, bit for bit
    want = ["3FFF8000000000000000", "4000D49A784BCD1B8AFE", "3FFFB8AA3B295C17F0BC",
            "4000C90FDAA22168C235", "3FFD9A209A84FBCFF799", "3FFEB17217F7D1CF79AC",
            "00000000000000000000"]
    for i, w in enumerate(want):
        run = Run(FNINIT + esc(1, 5, i=i) + FSTP80(A_R) + tail())
        run.go()
        eq(run.mem(DSEG, A_R, 10)[::-1].hex().upper(), w, "fld constant D9 %02X" % (0xE8 + i))

    # the stack: nine pushes overflow (IE, the indefinite), a pop of an empty
    # register underflows
    run = Run(FNINIT + esc(1, 5, i=0) * 9 + FNSTSW(A_SW) + FSTP80(A_R) + tail())
    run.go()
    sw = struct.unpack("<H", run.mem(DSEG, A_SW, 2))[0]
    eq(sw & 0x3F, IE, "the ninth push is the stack's overflow, masked")
    eq(run.mem(DSEG, A_R, 10), INDEF80, "...and leaves the indefinite on top")
    run = Run(FNINIT + esc(1, 4, i=0) + FNSTSW(A_SW) + FSTP80(A_R) + tail())
    run.go()
    eq(struct.unpack("<H", run.mem(DSEG, A_SW, 2))[0] & 0x3F, IE, "fchs of an empty st(0) underflows")

    # fxch, fld st(i), fstp st(i), ffree, fdecstp/fincstp
    data = bytearray(0x80)
    data[A_X:A_X + 10] = enc80(num(F(3)))
    data[A_Y:A_Y + 10] = enc80(num(F(5)))
    code = (FNINIT + FLD80(A_X) + FLD80(A_Y) + esc(1, 1, i=1)    # fxch: 3 5
            + esc(1, 0, i=1)                                     # fld st1: 5 3 5
            + esc(5, 3, i=2)                                     # fstp st2: 3 5
            + FSTP80(A_R) + FSTP80(A_S) + FNSTSW(A_SW) + tail())
    run = Run(code, data)
    run.go()
    eq((dec80(run.mem(DSEG, A_R, 10)).val(), dec80(run.mem(DSEG, A_S, 10)).val()), (3, 5),
       "fxch, fld st(i) and fstp st(i) move registers")
    eq(struct.unpack("<H", run.mem(DSEG, A_SW, 2))[0] & 0x383F, 0, "...with TOP back at 0, nothing raised")

    # fld st(i) of a DENORMAL register copies it bit for bit
    den = struct.pack("<QH", 0x0000123456789ABC, 0)
    data = bytearray(0x80)
    data[A_X:A_X + 10] = den
    run = Run(FNINIT + FLD80(A_X) + esc(1, 0, i=0) + FSTP80(A_R) + tail(), data)
    run.go()
    eq(run.mem(DSEG, A_R, 10), den, "fld st(0) of a denormal register is a copy")

    # fstsw ax, fstcw, fldcw, fclex, fstenv/fldenv, fsave/frstor
    data = bytearray(0x80)
    struct.pack_into("<H", data, A_CW, 0x0B7F)
    data[A_X:A_X + 10] = enc80(num(F(7)))
    run = Run(FNINIT + FLDCW(A_CW) + FLD80(A_X) + esc(1, 5, i=0) + esc(7, 4, i=0)
              + esc(1, 7, A_S) + esc(1, 6, 0x50) + tail(), data)
    run.go()
    eq(run.mu.reg_read(UC_X86_REG_AX) & 0xFFFF, 6 << 11, "fstsw ax: TOP is 6 after two loads")
    eq(struct.unpack("<H", run.mem(DSEG, A_S, 2))[0], 0x0B7F, "fstcw stores what fldcw loaded")
    env = run.mem(DSEG, 0x50, 14)
    eq(struct.unpack("<3H", env[:6]), (0x0B7F, 6 << 11, 0x0FFF), "fstenv: CW, SW, TW (two valid at 6, 7)")
    sv = 0x50
    run = Run(FNINIT + FLD80(A_X) + FLD80(A_X) + esc(5, 6, sv) + esc(5, 4, sv) + FSTP80(A_R) + FSTP80(A_S) + FNSTSW(A_SW) + tail(), data)
    run.go()
    eq((dec80(run.mem(DSEG, A_R, 10)).val(), dec80(run.mem(DSEG, A_S, 10)).val()), (7, 7),
       "fsave then frstor gives the registers back")

    # ES: through int 3Ch's segment byte
    data = bytearray(0x80)
    esd = bytearray(0x80)
    esd[A_X:A_X + 8] = struct.pack("<d", 1.5)
    run = Run(FNINIT + esc(5, 0, A_X, seg="es") + esc(5, 3, A_R, seg="es") + tail(), data, esd)
    run.go()
    eq(struct.unpack("<d", run.mem(ESEG, A_R, 8))[0], 1.5, "int 3Ch's ES operand loads and stores")
    # SS by BP: mod 01 rm 110 [bp+disp8]
    run = Run(FNINIT + esc(1, 5, i=0) + bytes([0xCD, 0x39, 0x5E, 0x10]) + tail())
    run.mu.reg_write(UC_X86_REG_BP, 0x8000)
    run.go()
    eq(struct.unpack("<d", run.mem(SSEG, 0x8010, 8))[0], 1.0, "[bp+disp8] defaults to SS")

    # an unmasked exception: 1/0 with ZE unmasked calls the handler with
    # WIN87EM's code, the destination left as it was
    data = bytearray(0x80)
    struct.pack_into("<H", data, A_CW, 0x037B)
    data[A_X:A_X + 8] = struct.pack("<d", 1.0)
    data[A_Y:A_Y + 8] = struct.pack("<d", 0.0)
    run = Run(FNINIT + FLDCW(A_CW) + FLD64(A_X) + esc(4, 6, A_Y) + FNSTSW(A_SW)
              + esc(5, 3, A_R) + tail(), data, hand=True)
    run.go()
    eq(struct.unpack("<HH", run.mem(CSEG, REC, 4)), (0x83, 1), "1/0 unmasked: the handler, once, AX = 83h")
    eq(struct.unpack("<d", run.mem(DSEG, A_R, 8))[0], 1.0, "...and st(0) is still the dividend")
    sw = struct.unpack("<H", run.mem(DSEG, A_SW, 2))[0]
    eq(sw & 0x80BF, 0x8084, "...ZE, ES and B in the status word")
    # unmasked precision: the result IS stored, then the handler (86h)
    struct.pack_into("<H", data, A_CW, 0x035F)
    data[A_Y:A_Y + 8] = struct.pack("<d", 3.0)
    run = Run(FNINIT + FLDCW(A_CW) + FLD64(A_X) + esc(4, 6, A_Y) + esc(5, 3, A_R) + tail(),
              data, hand=True)
    run.go()
    eq(struct.unpack("<H", run.mem(CSEG, REC, 2))[0], 0x86, "1/3 with PE unmasked: AX = 86h")
    eq(struct.unpack("<d", run.mem(DSEG, A_R, 8))[0], 1 / 3, "...and the quotient was written first")
    # unmasked invalid by the stack: 8Bh underflow, 8Ah overflow, 81h plain
    struct.pack_into("<H", data, A_CW, 0x037E)
    for code, want, what in ((esc(1, 4, i=0), 0x8B, "an empty st(0)"),
                             (esc(1, 5, i=0) * 9, 0x8A, "a ninth push"),
                             (esc(1, 5, i=6) + esc(1, 5, i=6) + esc(6, 7, i=1), 0x81, "0/0")):
        run = Run(FNINIT + FLDCW(A_CW) + code + tail(), data, hand=True)
        run.go()
        eq(struct.unpack("<H", run.mem(CSEG, REC, 2))[0], want, "IE unmasked by %s: AX = %02Xh" % (what, want))

    # an instruction the emulator lacks ends the program through ENDP, named
    for code, name in ((esc(1, 6, i=2), b"FPTAN"), (esc(7, 4, A_X), b"FBLD"),
                       (esc(2, 5, i=1), b"FUCOMPP"), (esc(2, 0, i=3), b"DA C3")):
        run = Run(FNINIT + code + b"\x90" + tail())
        run.go()
        eq(run.mem(CSEG, REC + 4, 1), b"\x01", "%s ends the program" % name.decode())
        why = run.mem(FPSEG, 38, 12).split(b"\0")[0]
        eq(why, name, "...and names itself")
        eq(run.mu.reg_read(UC_X86_REG_SP), 0xFFF0 - 6, "...on the program's own stack, the "
           "int's frame on top: where it stopped")

    # __FPMATH: 0 start, 3 handler, 4/5 control word, 6 round, 7 fistp dword,
    # 8 status, 9 clear, 10 depth, 11 no coprocessor, 13 refused
    run = Run(tail())
    eq(run.far_call(0, bx=0), (0, run.mu.reg_read(UC_X86_REG_DX), 0), "__FPMATH 0 starts: AX 0, CF 0")
    eq(run.far_call(0, bx=5)[0], 0x1332, "...with WIN87EM's control word, 1332h")
    run.far_call(0, bx=4, ax=0x133F)
    eq(run.far_call(0, bx=5)[0], 0x133F, "__FPMATH 4 sets it, 5 reads it")
    eq(run.far_call(0, bx=11)[0], 0, "__FPMATH 11: no coprocessor here")
    eq(run.far_call(0, bx=13)[:2], (0xFFFF, 0xFFFF), "__FPMATH 13 is not a function: -1")
    data = bytearray(0x80)
    data[A_X:A_X + 10] = enc80(num(F(-5, 2)))
    run = Run(FLD80(A_X) + FLD80(A_X) + tail(), data)
    run.far_call(0, bx=0)
    run.go()
    eq(run.far_call(0, bx=10)[0], 2, "__FPMATH 10: two registers in use")
    eq(run.far_call(0, bx=7, ax=0x0400)[:2], (0xFFFD, 0xFFFF), "__FPMATH 7: -2.5 down is -3 in DX:AX")
    eq(run.far_call(0, bx=7, ax=0x0C00)[:2], (0xFFFE, 0xFFFF), "...and chopped -2, the mode its own")
    eq(run.far_call(0, bx=10)[0], 0, "...popping both")
    eq(run.far_call(0, bx=8)[0] & 0x3F, PE, "__FPMATH 8: the inexact it raised")
    eq(run.far_call(0, bx=9)[0], 0, "__FPMATH 9 clears")
    eq(run.far_call(0, bx=8)[0], 0, "...and the status is clean")


def census():
    """Every instruction the census can name, through the emulator: the ones
    that end the program are exactly os88ne.X87_EMU_LACKS, so the census's
    report about a program is the emulator's own answer"""
    names = {}
    for e, row in os88ne.X87_MEM.items():
        for reg, n in enumerate(row):
            if n:
                names[n] = esc(e - 0xD8, reg, A_X)
    for e, row in os88ne.X87_REG.items():
        for reg, n in enumerate(row):
            if n:
                names[n] = esc(e - 0xD8, reg, i=1)
    names["fld st(i)"] = esc(1, 0, i=1)
    names["fxch st(i)"] = esc(1, 1, i=1)
    for (e, m), n in os88ne.X87_ONE.items():
        names[n] = bytes([0xCD, 0x34 + e - 0xD8, m])
    data = bytearray(0x80)
    data[A_Y:A_Y + 10] = enc80(num(F(2)))
    ended = set()
    for n, code in sorted(names.items()):
        run = Run(FNINIT + FLD80(A_Y) + FLD80(A_Y) + code + tail(), data)
        run.go()
        if run.mem(CSEG, REC + 4, 1) == b"\x01":
            ended.add(n)
    eq(sorted(ended), sorted(os88ne.X87_EMU_LACKS), "the instructions the emulator ends a "
       "program on are os88ne.X87_EMU_LACKS", why="tools/os88ne.py reports a program's "
       "x87 against that set; a set that drifts from the emulator says a program will "
       "run when it ends, or refuses one in words that are wrong")
    # the reference programs, when they are here: nothing they use is lacking
    for f in ("build/win16/FPU.EXE", "win16ref/samples/w3-archive/WINDOWS/CALC.EXE",
              "win16ref/samples/w31/CALC.EXE"):
        path = os.path.join(ROOT, f)
        if not os.path.exists(path):
            print("  (no %s: skipped)" % f)
            continue
        r = os88ne.analyse(path)
        eq(r["x87_emu_lacks"], [], "%s uses nothing the emulator lacks (%d kinds)"
           % (f, len(r["x87"])))


def shipped():
    """The part WIN16.O88 carries is the part tested here: `make win16` cuts
    build/win16/w16fp.bin off WIN16's own assembly (os88ovl.py --second),
    and it must be these bytes, vstart 0 in both"""
    built = os.path.join(ROOT, "build", "win16", "w16fp.bin")
    if not os.path.exists(built):
        print("  (no build/win16/w16fp.bin: `make win16` has not run; skipped)")
        return
    if os.path.getmtime(built) < max(os.path.getmtime(os.path.join(ROOT, "apps", "win16", f))
                                     for f in ("w16fp.inc", "w16x87.inc", "win16.asm")):
        print("  (build/win16/w16fp.bin is older than its source: skipped)")
        return
    eq(open(built, "rb").read(), PART, "build/win16/w16fp.bin, cut from WIN16.O88's "
       "assembly, is the part this row tested", why="the oracle assembles "
       "w16fp.inc alone; a part that differs inside WIN16 was never tested")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", type=int, default=600)
    ap.add_argument("--seed", type=int, default=8087)
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    VERBOSE[0] = a.v
    rng = random.Random(a.seed)
    shipped()
    fixed()
    census()
    gens = (case_arith, case_arith, case_arith, case_store, case_int, case_unary, case_unary)
    for _ in range(a.cases):
        rng.choice(gens)(rng)
    done("t_w16fp (%d random cases, seed %d, part %d bytes)" % (a.cases, a.seed, len(PART)))


if __name__ == "__main__":
    main()
