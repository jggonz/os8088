#!/usr/bin/env python3
"""apps/os88huge.inc, the huge claim's walker, run on the host against a model.

    python3 tests/os88huge.py [-v] [--seed N] [--break NAME]

The include (SPEC.md 50.3.8, docs/plans/MEGA-HUGE-PLAN.md F5) is assembled
with every arm into a stub and run in Unicorn's 8086 core. OSAPI_MEM_HUGE is
an `int F0h` at its cell (KERNEL_SEG:04C5h) that this script answers; the
two file routines' callees, os88_rseq and os88_wseq_put, are stubs of the
same kind over a Python file and a Python stream, so what is under test is
the WALK - where each piece starts and ends - and not the file doors, which
have rows of their own.

THE MACHINE IS SHAPED TO CATCH A REACH PAST A WINDOW. On kern_mega a bridge's
window k is DX + 8k and the cut lies between two windows; real mode has no
selectors, so the model gives the claim a step of 1800h: window k is 96KB
after window k-1, each 64KB long, with a 32KB GUARD between - so a piece
that runs one byte past its window's end, or an aim that adds the offset to
window 0's base, lands in a guard (or in the wrong window) instead of in the
claim's next byte, and the model sees it. The same cases are run on the
paragraph step every real-mode kernel answers (1000h, one contiguous run),
on a window-every-32KB claim (BX = 8000h asked, 0 answered, CX = 800h: the
walk must step two windows a 64KB) and on one every 16KB (4000h, CX = 400h).

Asserted, each case against the model and with EVERY register but the
documented outputs, the flags' DF aside, compared:

  hw_claim  the record: window 0, the step (doubled / quadrupled for a
            32KB / 16KB window given, not for one refused), KB x 1024, the
            cut (the out BX for BX = 0 asked, 0 for every other shape);
            refused: CF = 1, HW_SEG = 0
  hw_aim    ES:DI the model's byte, CX = min(wanted, the window's rest, the
            length's rest); at or past HW_LEN and CX = 0 refused, ES DI CX kept
  hw_get    random spans - across windows, from a window's last byte, odd
  hw_put    lengths, zero - out of and into the claim, to and from DS and a
            far segment: the bytes the model's, nothing outside the span
            written, every guard intact; a span past HW_LEN refused untouched
  hw_fread  the file's bytes at their offsets (HW_FOFF 0, a positive one -
            a file's tail at the claim's start - and a negative one - its
            head part-way in); ONE os88_rseq a window
            the span touches, none crossing a window, each a cluster
            multiple until HW_LEN cuts the last; a short file stops it;
            an error mid-span answers CF = 1 and that AX
  hw_fwrite the stream receives exactly the span's bytes, one put a window;
            a span past HW_LEN is FERR_BIG with nothing put; a refused put
            answers its AX

BROKEN ON PURPOSE (--break NAME, docs/WRITING-TESTS.md 1): `nowin` (the
window's-end cap taken out of hw_aim), `base0` (the aim adds the window's
index to nothing - the offset's low word in window 0, the naive sum), `nolen`
(the length's cap taken out), `nostep2` (hw_claim not doubling a 32KB
window's step), `nofoff` (hw_fread reading at the claim's offset, HW_FOFF's
low word ignored) - each must FAIL this row; the row's text names the
counts.

Milliseconds, where a package on an emulator would be minutes a case.
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX,
                                   UC_X86_REG_CX, UC_X86_REG_DX,
                                   UC_X86_REG_SI, UC_X86_REG_DI,
                                   UC_X86_REG_BP, UC_X86_REG_SP,
                                   UC_X86_REG_CS, UC_X86_REG_DS,
                                   UC_X86_REG_ES, UC_X86_REG_SS,
                                   UC_X86_REG_IP, UC_X86_REG_FLAGS)
except ImportError:                     # pragma: no cover
    print("os88huge: SKIP - unicorn is not installed (pip install unicorn)")
    sys.exit(77)

CSEG = 0xE000                           # the stub: code, its DS, the record
SSEG = 0xD000                           # a stack of its own (SS != DS)
FSEG = 0xC000                           # a FAR buffer
KSEG = 0x0060                           # KERNEL_SEG: OSAPI_MEM_HUGE's cell
HUGE_CELL = 0x04C5
REC = 0x8000                            # CSEG:REC the record
NAME = 0x8020                           # ...the name the file routines pass
CUR = 0x8040                            # ...the cursor / the stream
NBUF = 0x9000                           # ...a near buffer (to 0xFFFF)
CLAIM_PARA = 0x1000                     # window 0, at 64KB
GUARD = 0xEE
ENTRIES = ("hw_claim", "hw_aim", "hw_span", "hw_get", "hw_put", "hw_fread",
           "hw_fwrite")
FERR_IO, FERR_BIG = 2, 10
REGS = (("AX", UC_X86_REG_AX), ("BX", UC_X86_REG_BX), ("CX", UC_X86_REG_CX),
        ("DX", UC_X86_REG_DX), ("SI", UC_X86_REG_SI), ("DI", UC_X86_REG_DI),
        ("BP", UC_X86_REG_BP), ("DS", UC_X86_REG_DS), ("ES", UC_X86_REG_ES),
        ("SS", UC_X86_REG_SS), ("SP", UC_X86_REG_SP))

BREAKS = {
    "nowin": ("    jz .c2                          ; no 16-bit CX passes)\n"
              "    cmp cx, di\n    jbe .c2\n",
              "    jmp short .c2\n    cmp cx, di\n    jbe .c2\n"),
    "base0": ("    mul dx                          ; AX = the step x the "
              "window's index\n",
              "    xor ax, ax\n"),
    "nolen": ("    jnz .c1                         ; 64KB or more: no cap "
              "from the length\n",
              "    jmp short .c1\n"),
    "nostep2": ("    jnz .one\n    shl cx, 1\n", "    jnz .one\n    nop\n"),
    "nofoff": ("    add ax, [bx+HW_FOFF]            ; DX:AX = the file's offset\n",
               "    nop\n"),
}


def build(tmp, brk=None):
    src = open(os.path.join(ROOT, "apps", "os88huge.inc")).read()
    if brk:
        old, new = BREAKS[brk]
        if old not in src:
            sys.exit("os88huge: --break %s no longer matches the include" % brk)
        src = src.replace(old, new, 1)
    inc = os.path.join(tmp, "os88huge.inc")
    open(inc, "w").write(src)
    lines = ["cpu 8086", "bits 16", "org 0", '%include "os88api.inc"']
    lines += ["%define " + d for d in ("HW_CLAIM", "HW_GET", "HW_PUT",
                                       "HW_RSEQ", "HW_WSEQ")]
    for e in ENTRIES:                   # call (3) + hlt, a 4-byte stride
        lines += ["    call %s" % e, "    hlt"]
    lines += ["os88_rseq:", "    int 0xF1", "    ret",
              "os88_wseq_put:", "    int 0xF2", "    ret",
              '%%include "%s"' % inc]
    asm = os.path.join(tmp, "hwstub.asm")
    open(asm, "w").write("\n".join(lines) + "\n")
    out = os.path.join(tmp, "hwstub.bin")
    subprocess.run(["nasm", "-f", "bin", "-w+error",
                    "-I", os.path.join(ROOT, "apps") + os.sep,
                    "-o", out, asm], check=True)
    return open(out, "rb").read()


class Shape:
    """a claim as the model lays it out: window k at para + step x k"""

    def __init__(self, name, kb, step, asked, cx_out, bx_out, walk):
        self.name, self.kb, self.step = name, kb, step
        self.asked, self.cx_out, self.bx_out = asked, cx_out, bx_out
        self.walk = walk                # the 64KB step the record must hold
        self.len = kb * 1024

    def lin(self, x):                   # the linear byte of claim offset x
        return CLAIM_PARA * 16 + self.walk * 16 * (x >> 16) + (x & 0xFFFF)

    def windows(self):
        n = (self.len + 0xFFFF) >> 16
        return [(self.lin(k << 16), min(0x10000, self.len - (k << 16)))
                for k in range(n)]


SHAPES = [
    # a bridge's shape: windows 96KB apart, guards between (the cell's BX
    # out = 3, the pool piece's windows, for the record's HW_CUT)
    Shape("gapped", 300, 0x1800, 0, 0x1800, 3, 0x1800),
    # every real-mode kernel's: one run, a paragraph step
    Shape("paragraph", 260, 0x1000, 0, 0x1000, 0, 0x1000),
    # a window every 32KB given (BX = 8000h asked, 0 back), and 16KB
    Shape("win32", 200, 0x0800, 0x8000, 0x0800, 0, 0x1000),
    Shape("win16", 136, 0x0400, 0x4000, 0x0400, 0, 0x1000),
]


class Stub:
    def __init__(self, code):
        self.code = code
        mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 1 << 20)
        mu.mem_write(CSEG * 16, code)
        # the cell: int F0h / retf
        mu.mem_write(KSEG * 16 + HUGE_CELL, b"\xCD\xF0\xCB")
        mu.hook_add(UC_HOOK_INTR, self._int)
        self.mu = mu
        self.huge = None                # (cf, dx, cx, bx) for the next call
        self.file = b""                 # hw_fread's file
        self.reads = []                 # (offset, cap, linear) per os88_rseq
        self.rerr = None                # fail the n-th read with this AX
        self.puts = []                  # (linear, count) per os88_wseq_put
        self.stream = bytearray()
        self.perr = None
        self.bad = []

    # -- the kernel and the two file stubs ----------------------------------
    def _setcf(self, cf):
        f = self.mu.reg_read(UC_X86_REG_FLAGS)
        self.mu.reg_write(UC_X86_REG_FLAGS, (f | 1) if cf else (f & ~1))

    def _int(self, mu, intno, _):
        r = mu.reg_read
        if intno == 0xF0:
            cf, dx, cx, bx = self.huge
            self.huge_in = (r(UC_X86_REG_AX), r(UC_X86_REG_BX))
            if not cf:
                mu.reg_write(UC_X86_REG_DX, dx)
                mu.reg_write(UC_X86_REG_CX, cx)
                mu.reg_write(UC_X86_REG_BX, bx)
            mu.reg_write(UC_X86_REG_AX, 0x5A5A)     # the cell's AX: anything
            self._setcf(cf)
        elif intno == 0xF1:             # os88_rseq: READ_AT's registers
            off = r(UC_X86_REG_DX) << 16 | r(UC_X86_REG_AX)
            cap = r(UC_X86_REG_CX)
            lin = r(UC_X86_REG_ES) * 16 + r(UC_X86_REG_BX)
            if r(UC_X86_REG_SI) != NAME or r(UC_X86_REG_DI) != CUR or \
                    r(UC_X86_REG_DS) != CSEG:
                self.bad.append("os88_rseq called with SI %04X DI %04X DS %04X"
                                % (r(UC_X86_REG_SI), r(UC_X86_REG_DI),
                                   r(UC_X86_REG_DS)))
            self.reads.append((off, cap, lin))
            if self.rerr and len(self.reads) == self.rerr[0]:
                mu.reg_write(UC_X86_REG_AX, self.rerr[1])
                self._setcf(1)
                return
            data = self.file[off:off + cap]
            mu.mem_write(lin, data)
            mu.reg_write(UC_X86_REG_AX, len(data))
            mu.reg_write(UC_X86_REG_DX, 0)
            self._setcf(0)
        elif intno == 0xF2:             # os88_wseq_put: DS:DI, ES:SI, CX
            lin = r(UC_X86_REG_ES) * 16 + r(UC_X86_REG_SI)
            n = r(UC_X86_REG_CX)
            if r(UC_X86_REG_DI) != CUR or r(UC_X86_REG_DS) != CSEG:
                self.bad.append("os88_wseq_put's stream not DS:DI = CUR")
            self.puts.append((lin, n))
            if self.perr and len(self.puts) == self.perr[0]:
                mu.reg_write(UC_X86_REG_AX, self.perr[1])
                self._setcf(1)
                return
            self.stream += bytes(mu.mem_read(lin, n))
            mu.reg_write(UC_X86_REG_AX, 0x7777)     # everything but AX kept
            self._setcf(0)
        else:
            self.bad.append("int %02Xh" % intno)
            mu.emu_stop()

    # -- a call --------------------------------------------------------------
    def call(self, entry, **regs):
        """the entry with these registers; answers (out regs, CF)"""
        mu = self.mu
        want = {"AX": 0x1111, "BX": REC, "CX": 0x3333, "DX": 0x4444,
                "SI": 0x6666, "DI": 0x7777, "BP": 0x5555, "DS": CSEG,
                "ES": 0x0BAD, "SS": SSEG, "SP": 0xFFF0}
        want.update(regs)
        for nm, r in REGS:
            mu.reg_write(r, want[nm])
        mu.reg_write(UC_X86_REG_CS, CSEG)
        mu.reg_write(UC_X86_REG_FLAGS, 0x0402 | (regs.get("CF", 0)))  # DF set
        off = 4 * ENTRIES.index(entry)
        mu.reg_write(UC_X86_REG_IP, off)
        self.bad = []
        try:
            mu.emu_start(CSEG * 16 + off, CSEG * 16 + off + 3,
                         count=2000000)
        except Exception as e:          # noqa: BLE001 - a runaway
            return None, None, ["crashed: %s" % e]
        if mu.reg_read(UC_X86_REG_IP) != off + 3:
            self.bad.append("did not return (IP %04X)"
                            % mu.reg_read(UC_X86_REG_IP))
        out = {nm: mu.reg_read(r) for nm, r in REGS}
        return out, mu.reg_read(UC_X86_REG_FLAGS) & 1, (want, self.bad)

    def rec(self):
        b = bytes(self.mu.mem_read(CSEG * 16 + REC, 14))
        return (b[0] | b[1] << 8, b[2] | b[3] << 8,
                int.from_bytes(b[4:8], "little"), b[8] | b[9] << 8,
                int.from_bytes(b[10:14], "little"))

    def setrec(self, sh, foff=0):
        self.mu.mem_write(CSEG * 16 + REC,
                          CLAIM_PARA.to_bytes(2, "little") +
                          sh.walk.to_bytes(2, "little") +
                          sh.len.to_bytes(4, "little") +
                          sh.bx_out.to_bytes(2, "little") +
                          (foff & 0xFFFFFFFF).to_bytes(4, "little"))


class Check:
    def __init__(self, verbose):
        self.fails, self.n, self.v = [], 0, verbose

    def __call__(self, ok, what):
        self.n += 1
        if not ok:
            self.fails.append(what)
            if len(self.fails) <= 40:
                print("os88huge: FAIL " + what)
        elif self.v:
            print("os88huge: ok   " + what)
        return ok


def kept(chk, tag, out, want, keep):
    regs, bad = want
    for b in bad:
        chk(False, "%s: %s" % (tag, b))
    if out is None:
        return False
    ok = True
    for nm in keep:
        if out[nm] != regs[nm]:
            ok = chk(False, "%s: %s came back %04X, not %04X"
                     % (tag, nm, out[nm], regs[nm]))
    return ok


def fill_claim(st, sh, rng):
    """the whole map: guards everywhere, then the claim's windows random;
    answers the model (claim bytes)"""
    mu = st.mu
    mu.mem_write(CLAIM_PARA * 16, bytes([GUARD]) * (0xC0000 - CLAIM_PARA * 16))
    model = bytearray(rng.getrandbits(8) for _ in range(sh.len))
    for k, (lin, n) in enumerate(sh.windows()):
        mu.mem_write(lin, bytes(model[k << 16:(k << 16) + n]))
    return model


def read_claim(st, sh):
    out = bytearray()
    for lin, n in sh.windows():
        out += bytes(st.mu.mem_read(lin, n))
    return out


def guards_ok(st, sh):
    """every byte of the map outside the claim's windows is still GUARD"""
    mem = bytes(st.mu.mem_read(CLAIM_PARA * 16, 0xC0000 - CLAIM_PARA * 16))
    owned = bytearray(len(mem))
    for lin, n in sh.windows():
        a = lin - CLAIM_PARA * 16
        owned[a:a + n] = b"\x01" * n
    bad = [i for i in range(len(mem)) if not owned[i] and mem[i] != GUARD]
    return bad


def edges(sh, rng, n):
    """offsets that matter: every window's first and last bytes, the cut,
    the length's end, and random ones"""
    xs = set()
    for k in range((sh.len + 0xFFFF) >> 16):
        for d in (0, 1, 2, 0xFFFE, 0xFFFF, 0x8000, 0x7FFF):
            x = (k << 16) + d
            if x < sh.len:
                xs.add(x)
    xs |= {sh.len - 1, sh.len - 2, sh.len - 0x100}
    xs |= {rng.randrange(sh.len) for _ in range(n)}
    return sorted(xs)


def t_claim(st, chk, sh):
    tag = "%s hw_claim" % sh.name
    st.huge = (0, CLAIM_PARA, sh.cx_out, sh.bx_out if sh.asked == 0
               else (0 if sh.asked & 0xC000 else sh.asked))
    st.mu.mem_write(CSEG * 16 + REC, b"\xCC" * 14)
    out, cf, want = st.call("hw_claim", AX=sh.kb, CX=sh.asked)
    if not kept(chk, tag, out, want, ("AX", "BX", "CX", "SI", "DI", "BP",
                                      "DS", "ES", "SS", "SP")):
        return
    chk(st.huge_in == (sh.kb, sh.asked), "%s: the cell asked AX %d BX %04X "
        "(got %s)" % (tag, sh.kb, sh.asked, st.huge_in))
    chk(cf == 0 and out["DX"] == CLAIM_PARA, "%s: CF 0, DX = the claim" % tag)
    seg, step, ln, cut, foff = st.rec()
    chk((seg, step, ln, cut, foff) == (CLAIM_PARA, sh.walk, sh.len,
                                       sh.bx_out if sh.asked == 0 else 0, 0),
        "%s: the record %04X step %04X len %X cut %d foff %X, want %04X %04X "
        "%X %d 0" % (tag, seg, step, ln, cut, foff, CLAIM_PARA, sh.walk,
                     sh.len, sh.bx_out if sh.asked == 0 else 0))


def t_claim_more(st, chk):
    # refused: CF = 1, HW_SEG = 0
    st.huge = (1, 0, 0, 0)
    st.mu.mem_write(CSEG * 16 + REC, b"\xCC" * 14)
    out, cf, want = st.call("hw_claim", AX=500, CX=0)
    kept(chk, "refused hw_claim", out, want,
         ("AX", "BX", "CX", "SI", "DI", "BP", "DS", "ES", "SP"))
    chk(cf == 1 and st.rec()[0] == 0, "refused hw_claim: CF 1, HW_SEG 0")
    # HUGE_NOBRIDGE: BX back as it went, the cut 0
    st.huge = (0, 0x2345, 8, 2)
    out, cf, want = st.call("hw_claim", AX=1024, CX=2)
    r = st.rec()
    chk(cf == 0 and r == (0x2345, 8, 1024 * 1024, 0, 0),
        "HUGE_NOBRIDGE hw_claim: the record %s, the cut 0" % (r,))
    # 8000h asked, CX = 1000h and BX kept (conventional, no windows): no x2
    st.huge = (0, 0x3000, 0x1000, 0x8000)
    out, cf, want = st.call("hw_claim", AX=100, CX=0x8000)
    r = st.rec()
    chk(cf == 0 and r == (0x3000, 0x1000, 100 * 1024, 0, 0),
        "8000h asked, not given: the step 1000h as answered (%s)" % (r,))
    # 8000h given on kern_mega: CX = 8 -> the walk steps 16
    st.huge = (0, 0x0107, 8, 0)
    out, cf, want = st.call("hw_claim", AX=65535, CX=0x8000)
    r = st.rec()
    chk(cf == 0 and r == (0x0107, 16, 65535 * 1024, 0, 0),
        "8000h given (CX = 8): the walk's step 16, 65535 KB (%s)" % (r,))
    st.huge = (0, 0x0107, 8, 0)
    out, cf, want = st.call("hw_claim", AX=64, CX=0x4000)
    r = st.rec()
    chk(cf == 0 and r[1] == 32, "4000h given (CX = 8): the walk's step 32 "
        "(%s)" % (r,))


def t_aim(st, chk, sh, rng):
    st.setrec(sh)
    for x in edges(sh, rng, 60):
        for wantn in (1, 2, 0x100, 0x7FFF, 0xFFFF, rng.randrange(1, 0x10000)):
            out, cf, want = st.call("hw_aim", AX=x & 0xFFFF, DX=x >> 16,
                                    CX=wantn)
            tag = "%s hw_aim(%X, %X)" % (sh.name, x, wantn)
            kept(chk, tag, out, want, ("AX", "BX", "DX", "SI", "BP", "DS",
                                       "SS", "SP"))
            if out is None:
                continue
            exp = min(wantn, 0x10000 - (x & 0xFFFF), sh.len - x)
            got = out["ES"] * 16 + out["DI"]
            if not chk(cf == 0 and got == sh.lin(x) and out["CX"] == exp,
                       "%s: CF %d ES:DI %05X CX %X, want %05X CX %X"
                       % (tag, cf, got, out["CX"], sh.lin(x), exp)):
                return
    for x, n in ((sh.len, 1), (sh.len + 5, 9), (0x7FFFFFF, 1), (0, 0),
                 (sh.len // 2, 0)):
        out, cf, want = st.call("hw_aim", AX=x & 0xFFFF, DX=x >> 16, CX=n)
        tag = "%s hw_aim(%X, %X) refused" % (sh.name, x, n)
        kept(chk, tag, out, want, ("AX", "BX", "CX", "DX", "SI", "DI", "BP",
                                   "DS", "ES", "SP"))
        chk(cf == 1, tag + ": CF 1")


def spans(sh, rng, n):
    out = [(0, 0), (0, 1), (sh.len - 1, 1), (0, 0xFFFF), (sh.len - 0xFFFF,
                                                         0xFFFF)]
    for k in range(1, (sh.len + 0xFFFF) >> 16):
        b = k << 16
        out += [(b - 1, 2), (b - 0x1001, 0x2003), (b - 0x8000, 0x10000 - 1),
                (b, 0x7FFF), (b - 2, 4)]
    for _ in range(n):
        c = rng.choice((rng.randrange(1, 64), rng.randrange(1, 0x10000)))
        out.append((rng.randrange(0, sh.len - c + 1), c))
    return [(x, c) for x, c in out if x >= 0 and x + c <= sh.len and c < 0x10000]


def far(rng, c):
    """a buffer: near (DS:NBUF..) when it fits, else the far segment"""
    if c <= 0x10000 - NBUF and rng.random() < 0.5:
        return CSEG, NBUF
    return FSEG, rng.randrange(0, 0x10000 - c + 1) if c < 0x10000 else 0


def t_copy(st, chk, sh, rng):
    st.setrec(sh)
    model = fill_claim(st, sh, rng)
    for x, c in spans(sh, rng, 40):
        # GET: the claim's bytes into a buffer whose neighbours are marked
        es, di = far(rng, c)
        b0 = es * 16 + di
        lo, hi = max(b0 - 16, es * 16), min(b0 + c + 16, es * 16 + 0x10000)
        st.mu.mem_write(lo, b"\xA5" * (hi - lo))
        out, cf, want = st.call("hw_get", AX=x & 0xFFFF, DX=x >> 16, CX=c,
                                ES=es, DI=di)
        tag = "%s hw_get(%X, %X)" % (sh.name, x, c)
        kept(chk, tag, out, want, [nm for nm, _ in REGS])
        got = bytes(st.mu.mem_read(lo, hi - lo))
        exp = b"\xA5" * (b0 - lo) + bytes(model[x:x + c]) + \
            b"\xA5" * (hi - b0 - c)
        if not chk(cf == 0 and got == exp, "%s: CF %d, the buffer %s"
                   % (tag, cf, "the model's" if got == exp else "WRONG")):
            break
        # PUT: fresh bytes in, the model follows
        data = bytes(rng.getrandbits(8) for _ in range(c))
        es, di = far(rng, c)
        st.mu.mem_write(es * 16 + di, data)
        out, cf, want = st.call("hw_put", AX=x & 0xFFFF, DX=x >> 16, CX=c,
                                ES=es, DI=di)
        tag = "%s hw_put(%X, %X)" % (sh.name, x, c)
        kept(chk, tag, out, want, [nm for nm, _ in REGS])
        model[x:x + c] = data
        if not chk(cf == 0 and read_claim(st, sh) == model,
                   "%s: CF %d, the claim %s" % (
                       tag, cf, "the model's" if read_claim(st, sh) == model
                       else "WRONG")):
            break
    bad = guards_ok(st, sh)
    chk(not bad, "%s: every byte outside the windows untouched (%d written%s)"
        % (sh.name, len(bad), ", the first at %05X" % (bad[0] + CLAIM_PARA * 16)
           if bad else ""))
    # past the end: refused, nothing moved
    for x, c in ((sh.len - 1, 2), (sh.len, 1), (sh.len - 0x100, 0x1000)):
        for e in ("hw_get", "hw_put"):
            snap = read_claim(st, sh)
            st.mu.mem_write(FSEG * 16, b"\x5A" * 0x2000)
            out, cf, want = st.call(e, AX=x & 0xFFFF, DX=x >> 16, CX=c,
                                    ES=FSEG, DI=0)
            tag = "%s %s(%X, %X) past HW_LEN" % (sh.name, e, x, c)
            kept(chk, tag, out, want, [nm for nm, _ in REGS])
            chk(cf == 1 and read_claim(st, sh) == snap and
                bytes(st.mu.mem_read(FSEG * 16, 0x2000)) == b"\x5A" * 0x2000,
                tag + ": CF 1, nothing copied")


def t_fread(st, chk, sh, rng, foff=0):
    st.setrec(sh, foff)
    model = fill_claim(st, sh, rng)
    st.file = bytes(rng.getrandbits(8) for _ in range(sh.len + 0x3000))
    clu = 0x800
    cases = [(0, 0x8000), (0x10000 - 0x2000, 0x8000), (0, 0xF000),
             (sh.len - 0x4000, 0x8000)]
    for k in range(1, (sh.len + 0xFFFF) >> 16):
        cases.append(((k << 16) - 0x800, 0x1000))
    for _ in range(12):
        cases.append((rng.randrange(0, sh.len // clu) * clu,
                      rng.randrange(1, 0x20) * clu))
    if foff < 0:                    # the file starts part-way in
        cases = [(x, c) for x, c in cases if x + foff >= 0]
        cases += [(-foff, 0x8000), (-foff - 0x800 + 0x10000 if -foff < 0xF800
                                    else -foff, 0x2000)]
    for x, c in cases:
        st.reads = []
        out, cf, want = st.call("hw_fread", AX=x & 0xFFFF, DX=x >> 16, CX=c,
                                SI=NAME, DI=CUR, ES=0x0BAD)
        tag = "%s hw_fread(%X, %X)" % (sh.name, x, c)
        kept(chk, tag, out, want, ("BX", "CX", "SI", "DI", "BP", "DS", "ES",
                                   "SS", "SP"))
        n = min(c, sh.len - x)
        fo = x + foff
        n = min(n, max(0, len(st.file) - fo))
        model[x:x + n] = st.file[fo:fo + n]
        dl = out["DX"] << 16 | out["AX"]
        chk(cf == 0 and dl == n, "%s: CF %d, %X delivered, want %X"
            % (tag, cf, dl, n))
        # one read a window, none across one, each a cluster multiple but
        # the one HW_LEN cuts
        # the model's reads: the span cut at every window's end and at
        # HW_LEN, one read a piece, stopping after the first short one
        exp, at, left = [], x, min(c, sh.len - x)
        while left:
            cap = min(left, 0x10000 - (at & 0xFFFF))
            exp.append((at + foff, cap, sh.lin(at)))
            got = min(cap, max(0, len(st.file) - (at + foff)))
            if got < cap:
                break
            at, left = at + cap, left - cap
        ok = all(cap % clu == 0 or off - foff + cap == sh.len
                 for off, cap, _ in st.reads)
        chk(ok and st.reads == exp,
            "%s: os88_rseq %s, want %s" % (
                tag, [(hex(o), hex(c2)) for o, c2, _ in st.reads],
                [(hex(o), hex(c2)) for o, c2, _ in exp]))
        chk(read_claim(st, sh) == model, tag + ": the claim the file's bytes")
    if foff:
        return
    # the file's end mid-span: fewer delivered, the read stops
    st.file = st.file[:0x10000 + 0x123]
    st.reads = []
    out, cf, want = st.call("hw_fread", AX=0xC000, DX=0, CX=0x8000, SI=NAME,
                            DI=CUR)
    dl = out["DX"] << 16 | out["AX"]
    chk(cf == 0 and dl == 0x4123 and len(st.reads) == 2,
        "%s hw_fread across the file's end: %X delivered in %d reads"
        % (sh.name, dl, len(st.reads)))
    # an error in the second piece: CF = 1, its AX
    st.file = bytes(sh.len)
    st.reads, st.rerr = [], (2, FERR_IO)
    out, cf, want = st.call("hw_fread", AX=0xF800, DX=0, CX=0x1000, SI=NAME,
                            DI=CUR)
    st.rerr = None
    kept(chk, "%s hw_fread refused" % sh.name, out, want,
         ("BX", "CX", "SI", "DI", "BP", "DS", "ES", "SS", "SP"))
    chk(cf == 1 and out["AX"] == FERR_IO, "%s hw_fread: the second piece's "
        "FERR_IO answered (CF %d AX %X)" % (sh.name, cf, out["AX"]))


def t_fwrite(st, chk, sh, rng):
    st.setrec(sh)
    model = fill_claim(st, sh, rng)
    for x, c in spans(sh, rng, 12):
        st.puts, st.stream = [], bytearray()
        out, cf, want = st.call("hw_fwrite", AX=x & 0xFFFF, DX=x >> 16, CX=c,
                                DI=CUR)
        tag = "%s hw_fwrite(%X, %X)" % (sh.name, x, c)
        kept(chk, tag, out, want, [nm for nm, _ in REGS if nm != "AX"])
        wins = ((x + c - 1) >> 16) - (x >> 16) + 1 if c else 0
        ok = all((a - CLAIM_PARA * 16) >= 0 for a, _ in st.puts)
        chk(cf == 0 and bytes(st.stream) == bytes(model[x:x + c]) and
            len(st.puts) == wins and ok,
            "%s: CF %d, the stream the span's bytes in %d put(s) (want %d)"
            % (tag, cf, len(st.puts), wins))
    st.puts = []
    out, cf, want = st.call("hw_fwrite", AX=(sh.len - 4) & 0xFFFF,
                            DX=(sh.len - 4) >> 16, CX=5, DI=CUR)
    chk(cf == 1 and out["AX"] == FERR_BIG and not st.puts,
        "%s hw_fwrite past HW_LEN: FERR_BIG, nothing put" % sh.name)
    st.puts, st.stream, st.perr = [], bytearray(), (2, 6)
    out, cf, want = st.call("hw_fwrite", AX=0xF000, DX=0, CX=0x2000, DI=CUR)
    st.perr = None
    chk(cf == 1 and out["AX"] == 6, "%s hw_fwrite: the second put's refusal "
        "answered (CF %d AX %X)" % (sh.name, cf, out["AX"]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--seed", type=int, default=8088)
    ap.add_argument("--break", dest="brk", choices=sorted(BREAKS))
    a = ap.parse_args()
    bdir = os.path.join(ROOT, "build")
    os.makedirs(bdir, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=bdir, prefix="os88huge-") as tmp:
        code = build(tmp, a.brk)
    chk = Check(a.verbose)
    rng = random.Random(a.seed)
    st = Stub(code)
    t_claim_more(st, chk)
    for sh in SHAPES:
        t_claim(st, chk, sh)
        t_aim(st, chk, sh, rng)
        t_copy(st, chk, sh, rng)
        t_fread(st, chk, sh, rng)
        t_fread(st, chk, sh, rng, 44 * 0x800)       # a file's tail at 0
        t_fread(st, chk, sh, rng, -0x18000)         # its head part-way in
        t_fwrite(st, chk, sh, rng)
    print("os88huge: %d checks over %d shapes%s, %d failure(s)"
          % (chk.n, len(SHAPES), " (BROKEN: %s)" % a.brk if a.brk else "",
             len(chk.fails)))
    print("os88huge: %s" % ("FAILED" if chk.fails else "PASS"))
    return 1 if chk.fails else 0


if __name__ == "__main__":
    sys.exit(main())
