#!/usr/bin/env python3
# =============================================================================
# os8088 - tools/ccverify.py     differential check of the C peephole pass
#
#     python3 tools/ccverify.py build/cword.raw.asm [build/loom.raw.asm ...]
#     python3 tools/ccverify.py --self-test build/cword.raw.asm
#
# WHAT IT PROVES, AND HOW MUCH. tools/ccpeep.py rewrites tools/cc8086.py's
# output; this file runs the program BEFORE and AFTER the rewrite side by side
# on the same inputs and fails on the first observable difference. It is a
# CONCRETE differential interpreter, not a proof: every C function the pass
# changed is entered with randomised registers, stack and memory (biased toward
# 0, small numbers and -1, which is where C's branches turn), the two versions
# are stepped instruction by instruction over SmallerC's emitted subset of the
# 8086, and what each one does that the rest of the machine can see is
# recorded as a trace:
#
#   W  every BYTE written to memory, with its segment (SS for a [bp...]
#      address, DS otherwise - SS != DS in this OS), in order
#   C  every call: its target, and SP at the call (the arguments are memory,
#      and memory is already in the trace as the writes that put it there)
#   J  a jump out of the function (a tail jump into a shim): all registers
#   R  the return: AX unless SmallerC's own `; glb` comment says void, SP,
#      BP, SI and DI
#   X  a trap: a divide by zero, a step cap, an instruction outside the subset
#
# and the two traces must be IDENTICAL (a prefix of each other when a run hit
# the step cap). A call is modelled the way the pass's own liveness models it:
# it answers in AX and FLAGS, leaves BX, CX, DX, SI and DI as they were (the
# conservative direction - a register the pass wrongly thought dead stays
# visible after the call) and scribbles over the 64 bytes below SP, the frame
# it would really have built there. All of those answers are a function of
# the call's ORDINAL, so both versions are handed the same callee.
#
# What it does NOT see: a path no random input reached (the report gives the
# share of rewritten sites that were executed, per package - read that number
# with the verdict), and anything that rests on the ABI rather than on the
# instruction (a void function's AX at `ret`, a callee reading BX) - those are
# the same assumptions the pass makes, and they are stated in tools/ccpeep.py.
#
# --self-test is the check of the checker: it plants deliberately WRONG
# rewrites (a setcc whose condition is not negated, a `dead` that deletes a
# live write, a fold that ignores liveness, a shift one place short, a jccjmp
# that keeps its condition) and fails unless every one is caught. The pass's
# own first defect - two flow rules rewriting one branch in one sweep - is
# NOT one of them: with today's rules, taking the per-sweep ownership out
# changes one jump in cword and one function in loom and both are still
# correct (verified), so it cannot be planted to be caught.
# =============================================================================
import argparse
import difflib
import os
import re
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cc8086   # noqa: E402
import ccpeep   # noqa: E402

M16 = 0xFFFF
REG16 = {"ax": 0, "cx": 1, "dx": 2, "bx": 3, "sp": 4, "bp": 5, "si": 6,
         "di": 7}
REG8 = {"al": (0, 0), "cl": (1, 0), "dl": (2, 0), "bl": (3, 0),
        "ah": (0, 8), "ch": (1, 8), "dh": (2, 8), "bh": (3, 8)}
SEGOV = re.compile(r"\b(ss|ds|es|cs)\s*:", re.I)
TOKEN = re.compile(r"[A-Za-z_.?$@][\w.?$@#~]*|0[xX][0-9a-fA-F]+|[0-9][0-9a-fA-F]*[hH]\b|\d+|'.'|[-+*/()~&|^<>]+|\s+")
STEP_CAP = 30000
EVENT_CAP = 3000


class Unsupported(Exception):
    pass


def symval(name):
    """A symbol's address: the same in both versions, spread over 64KB."""
    return (zlib.crc32(name.encode()) * 2) & 0xFFFE | 0x0100


def parse_num(t):
    tl = t.lower()
    if tl.startswith("0x"):
        return int(tl, 16)
    if tl.endswith("h") and re.match(r"^[0-9][0-9a-f]*h$", tl):
        return int(tl[:-1], 16)
    if len(t) == 3 and t[0] == "'" and t[2] == "'":
        return ord(t[1])
    return int(t)


def expr(text, allow_regs):
    """Compile an operand expression into (regs, const-fn source). Returns a
    Python expression over R (the register file) and a set of registers."""
    out, regs = [], []
    pos = 0
    text = text.strip()
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if not m:
            raise Unsupported("expression %r" % text)
        t = m.group(0)
        pos = m.end()
        if t.isspace():
            continue
        tl = t.lower()
        if tl in REG16 and allow_regs:
            out.append("R[%d]" % REG16[tl])
            regs.append(tl)
        elif tl in REG16 or tl in REG8:
            raise Unsupported("register in an immediate %r" % text)
        elif re.match(r"^[A-Za-z_.?$@]", t):
            out.append(str(symval(t)))
        elif re.match(r"^[0-9']", t):
            out.append(str(parse_num(t)))
        elif t == "/":
            out.append("//")
        else:
            out.append(t)
    src = " ".join(out) or "0"
    return src, regs


class Machine:
    __slots__ = ("R", "cf", "zf", "sf", "of", "pf", "mem", "seed", "ev",
                 "ncall", "steps", "cover", "pool", "sp0")

    def __init__(self, seed, regs, pool=()):
        self.pool = pool
        self.R = list(regs)
        self.sp0 = self.R[4]
        self.cf = self.zf = self.sf = self.of = self.pf = 0
        self.mem = {}
        self.seed = seed
        self.ev = []
        self.ncall = 0
        self.steps = 0


def mix(a, b):
    x = (a * 0x9E3779B1 + b * 0x85EBCA6B + 0x632BE5AB) & 0xFFFFFFFF
    x ^= x >> 15
    x = (x * 0x2C1B3C6D) & 0xFFFFFFFF
    x ^= x >> 12
    x = (x * 0x297A2D39) & 0xFFFFFFFF
    x ^= x >> 15
    return x


def biased_byte(h):
    k = h & 0xFF
    v = (h >> 8) & 0xFF
    if k < 110:
        return 0
    if k < 180:
        return v & 0x0F
    if k < 200:
        return 0xFF
    return v


def biased_word(h, pool=()):
    """A 16-bit input value. `pool` is the function's own constants - the
    `cmp ax, 13` it is going to make - so `if (x == 13)` is reached by some
    trials and not by one in 65,536."""
    k = h & 0xFF
    v = (h >> 8) & 0xFFFF
    if pool and k >= 200:
        c = pool[v % len(pool)]
        return (c + ((h >> 24) % 3) - 1) & M16
    if k < 50:
        return 0
    if k < 90:
        return 1
    if k < 150:
        return v & 0x1F
    if k < 175:
        return 0xFFFF
    if k < 190:
        return 0x8000 | (v & 0xFF)
    return v


def rd8(st, seg, a):
    k = (seg << 16) | (a & M16)
    v = st.mem.get(k)
    if v is None:
        # a pure function of the ADDRESS, never of the order or width the
        # program reads it in: the two versions read different sets of bytes
        # and must still find the same machine
        w = biased_word(mix(st.seed, k & ~1), st.pool)
        v = (w >> 8) & 0xFF if a & 1 else w & 0xFF
        st.mem[k] = v
    return v


def wr8(st, seg, a, v):
    a &= M16
    st.mem[(seg << 16) | a] = v & 0xFF
    if seg == 1 and a < st.sp0:
        return                  # the function's own stack: see frame()
    st.ev.append(("W", seg, a, v & 0xFF))


def frame(st):
    """The stack between SP and the entry SP, as it stands: what a callee
    (or a tail-called shim) can read of this function's stack. A write
    there is not an event when it is made - a push the pass deleted, or a
    slot a later push reuses, is invisible to everything else - only what
    is in place when control leaves the function is."""
    sp = st.R[4]
    return tuple(rd8(st, 1, a) for a in range(sp, min(st.sp0, sp + 256)))


def rd16(st, seg, a):
    return rd8(st, seg, a) | (rd8(st, seg, a + 1) << 8)


def wr16(st, seg, a, v):
    wr8(st, seg, a, v)
    wr8(st, seg, a + 1, v >> 8)


PARITY = [bin(i).count("1") % 2 == 0 for i in range(256)]


def szp(st, r, bits):
    mask = (1 << bits) - 1
    r &= mask
    st.zf = int(r == 0)
    st.sf = (r >> (bits - 1)) & 1
    st.pf = int(PARITY[r & 0xFF])
    return r


COND = {
    "o": lambda s: s.of, "no": lambda s: not s.of,
    "b": lambda s: s.cf, "c": lambda s: s.cf, "nae": lambda s: s.cf,
    "ae": lambda s: not s.cf, "nb": lambda s: not s.cf,
    "nc": lambda s: not s.cf,
    "e": lambda s: s.zf, "z": lambda s: s.zf,
    "ne": lambda s: not s.zf, "nz": lambda s: not s.zf,
    "be": lambda s: s.cf or s.zf, "na": lambda s: s.cf or s.zf,
    "a": lambda s: not (s.cf or s.zf), "nbe": lambda s: not (s.cf or s.zf),
    "s": lambda s: s.sf, "ns": lambda s: not s.sf,
    "p": lambda s: s.pf, "pe": lambda s: s.pf,
    "np": lambda s: not s.pf, "po": lambda s: not s.pf,
    "l": lambda s: s.sf != s.of, "nge": lambda s: s.sf != s.of,
    "ge": lambda s: s.sf == s.of, "nl": lambda s: s.sf == s.of,
    "le": lambda s: s.zf or s.sf != s.of, "ng": lambda s: s.zf or s.sf != s.of,
    "g": lambda s: not s.zf and s.sf == s.of,
    "nle": lambda s: not s.zf and s.sf == s.of,
}


class Operand:
    """A compiled operand: get(st) / put(st, v) at a fixed width."""
    __slots__ = ("kind", "bits", "get", "put", "addr", "seg", "text")


def compile_operand(op, other_bits=None):
    o = Operand()
    o.text = op
    s = op.strip()
    size = None
    m = re.match(r"^(byte|word|dword|near|far|short)\s+(.*)$", s, re.I)
    if m:
        size = m.group(1).lower()
        s = m.group(2).strip()
    if size == "dword":
        raise Unsupported("dword operand %r" % op)
    sl = s.lower()
    if "[" in s:
        seg = None
        so = SEGOV.search(s)
        if so:
            seg = so.group(1).lower()
            s = SEGOV.sub("", s)
        inner = re.search(r"\[([^\]]*)\]", s)
        if not inner or s[:inner.start()].strip() or s[inner.end():].strip():
            raise Unsupported("memory operand %r" % op)
        src, regs = expr(inner.group(1), True)
        if seg is None:
            seg = "ss" if "bp" in regs else "ds"
        segno = {"ss": 1, "ds": 2, "es": 3, "cs": 4}[seg]
        fn = eval("lambda R: (%s) & 0xFFFF" % src)
        o.kind = "mem"
        o.addr = fn
        o.seg = segno
        o.bits = 8 if size == "byte" else 16 if size == "word" else other_bits
        if o.bits == 8:
            o.get = lambda st, fn=fn, g=segno: rd8(st, g, fn(st.R))
            o.put = lambda st, v, fn=fn, g=segno: wr8(st, g, fn(st.R), v)
        else:
            o.get = lambda st, fn=fn, g=segno: rd16(st, g, fn(st.R))
            o.put = lambda st, v, fn=fn, g=segno: wr16(st, g, fn(st.R), v)
        return o
    if sl in REG16:
        i = REG16[sl]
        o.kind, o.bits = "reg", 16
        o.get = lambda st, i=i: st.R[i]
        def put(st, v, i=i):
            st.R[i] = v & M16
        o.put = put
        return o
    if sl in REG8:
        i, sh = REG8[sl]
        o.kind, o.bits = "reg", 8
        o.get = lambda st, i=i, sh=sh: (st.R[i] >> sh) & 0xFF
        def put8(st, v, i=i, sh=sh):
            st.R[i] = (st.R[i] & ~(0xFF << sh) & M16) | ((v & 0xFF) << sh)
        o.put = put8
        return o
    if sl in ("cs", "ds", "es", "ss"):
        raise Unsupported("segment register %r" % op)
    src, _ = expr(s, False)
    val = eval(src) & M16
    o.kind, o.bits = "imm", None
    o.get = lambda st, val=val: val
    o.put = None
    return o


def operands(it):
    ops = [compile_operand(x) for x in it.ops]
    known = [x.bits for x in ops if x.bits]
    w = known[0] if known else 16
    for x in ops:
        if x.kind == "mem" and x.bits is None:
            # re-compile at the width the other operand says
            y = compile_operand(x.text, w)
            x.bits, x.get, x.put = y.bits, y.get, y.put
    if any(x.kind != "imm" and x.bits != w for x in ops):
        raise Unsupported("mixed widths %r" % it.code)
    return ops, w


def alu(st, m, a, b, bits):
    mask = (1 << bits) - 1
    top = 1 << (bits - 1)
    if m in ("add", "adc"):
        c = st.cf if m == "adc" else 0
        r = a + b + c
        st.cf = int(r > mask)
        st.of = int(((a ^ r) & (b ^ r) & top) != 0)
        return szp(st, r, bits)
    if m in ("sub", "sbb", "cmp"):
        c = st.cf if m == "sbb" else 0
        r = a - b - c
        st.cf = int(r < 0)
        st.of = int(((a ^ b) & (a ^ r) & top) != 0)
        return szp(st, r, bits)
    if m in ("and", "test"):
        st.cf = st.of = 0
        return szp(st, a & b, bits)
    if m == "or":
        st.cf = st.of = 0
        return szp(st, a | b, bits)
    if m == "xor":
        st.cf = st.of = 0
        return szp(st, a ^ b, bits)
    raise Unsupported(m)


def shift(st, m, v, n, bits):
    mask = (1 << bits) - 1
    top = 1 << (bits - 1)
    n &= 0x1F                     # an 8086 does NOT mask; SmallerC never > 15
    if n == 0:
        return v
    for _ in range(n):
        if m in ("shl", "sal"):
            st.cf = (v >> (bits - 1)) & 1
            v = (v << 1) & mask
            st.of = int(((v & top) != 0) != st.cf)
        elif m == "shr":
            st.of = (v >> (bits - 1)) & 1
            st.cf = v & 1
            v >>= 1
        elif m == "sar":
            st.of = 0
            st.cf = v & 1
            v = (v >> 1) | (v & top)
        elif m == "rol":
            st.cf = (v >> (bits - 1)) & 1
            v = ((v << 1) | st.cf) & mask
            st.of = int(((v & top) != 0) != st.cf)
        elif m == "ror":
            st.cf = v & 1
            v = (v >> 1) | (st.cf << (bits - 1))
            st.of = int(((v >> (bits - 1)) ^ (v >> (bits - 2))) & 1)
        elif m == "rcl":
            c = st.cf
            st.cf = (v >> (bits - 1)) & 1
            v = ((v << 1) | c) & mask
            st.of = int(((v & top) != 0) != st.cf)
        elif m == "rcr":
            c = st.cf
            st.of = int(((v >> (bits - 1)) & 1) != c)
            st.cf = v & 1
            v = (v >> 1) | (c << (bits - 1))
        else:
            raise Unsupported(m)
    if m in ("shl", "sal", "shr", "sar"):
        szp(st, v, bits)
    return v


def is_func(label):
    """A function (or shim) entry: a label that is neither a compiler-local
    one nor one of cc8086's `..@` labels inside a function."""
    return not ccpeep.LOCAL.match(label) and not label.startswith("..@")


class Program:
    """One version of the file, compiled into closures per code node."""

    def __init__(self, text):
        self.voids = ccpeep.void_functions(text)
        nodes = ccpeep.parse(text)
        self.seq = {s: [] for s in ccpeep.CODE_SECTIONS}
        for n in nodes:
            if n.sec in self.seq and n.kind in ("ins", "label", "barrier"):
                self.seq[n.sec].append(n)
        self.lab = {}
        self.funcs = {}             # name -> (section, index)
        for s, lst in self.seq.items():
            fn = None
            for k, n in enumerate(lst):
                if n.kind == "label":
                    self.lab[n.label] = (s, k)
                    if is_func(n.label):
                        fn = n.label
                        self.funcs[fn] = (s, k)
        self.owner = {}
        self.code = {s: [None] * len(lst) for s, lst in self.seq.items()}

    def function_text(self, name):
        """The instruction lines of one function, for the diff."""
        s, k = self.funcs[name]
        lst = self.seq[s]
        out = []
        for j in range(k, len(lst)):
            n = lst[j]
            if j > k and n.kind == "label" and is_func(n.label):
                break
            out.append((j, n.text.strip() if n.kind != "ins"
                        else re.sub(r"\s+", " ", n.it.code.strip())))
        return out

    def step_fn(self, s, k, fname):
        f = self.code[s][k]
        if f is None:
            f = self.code[s][k] = self.compile(s, k, fname)
        return f

    def compile(self, s, k, fname):
        """A closure st -> next index, or None to stop."""
        n = self.seq[s][k]
        nxt = k + 1 if k + 1 < len(self.seq[s]) else None
        if n.kind == "label":
            return lambda st: nxt
        if n.kind == "barrier":
            def bar(st):
                st.ev.append(("X", "barrier"))
                return None
            return bar
        it = n.it
        m = it.mnem
        try:
            return self.compile_ins(s, k, it, m, nxt, fname)
        except Unsupported as e:
            msg = str(e)
            def uns(st, msg=msg):
                st.ev.append(("X", "unsupported", msg))
                return None
            return uns

    def compile_ins(self, s, k, it, m, nxt, fname):
        R = None  # noqa: F841
        if m == "jmp" or cc8086.JCC.match(m):
            t = ccpeep.target(it)
            local = t is not None and t in self.lab and \
                self.lab[t][0] == s and not is_func(t)
            if m == "jmp":
                if local:
                    tk = self.lab[t][1]
                    return lambda st: tk
                tv = t if t else it.code
                def tail(st, tv=tv):
                    st.ev.append(("J", tv, tuple(st.R), frame(st)))
                    return None
                return tail
            cond = COND[m[1:]]
            if not local:
                raise Unsupported("jcc out of the function %r" % it.code)
            tk = self.lab[t][1]
            return lambda st: tk if cond(st) else nxt
        if m == "call":
            op = it.ops[0].strip()
            far = bool(re.match(r"^far\b", op, re.I))
            if far:
                tgt = compile_operand(op[3:].strip(), 16)
                if tgt.kind != "mem":
                    raise Unsupported("far call %r" % op)
                addr, seg = tgt.addr, tgt.seg
                tv = lambda st: ("far", addr(st.R), rd16(st, seg, addr(st.R)))
            else:
                t = ccpeep.target(it)
                if not t:
                    o = compile_operand(op, 16)
                    tv = lambda st, o=o: ("ind", o.get(st))
                else:
                    tv = lambda st, t=t: t
            def call(st):
                st.ncall += 1
                st.ev.append(("C", tv(st), st.R[4], frame(st)))
                h = mix(st.seed ^ 0x5A5A, st.ncall)
                st.R[0] = biased_word(h, st.pool)
                st.cf, st.zf = h >> 16 & 1, h >> 17 & 1
                st.sf, st.of, st.pf = h >> 18 & 1, h >> 19 & 1, h >> 20 & 1
                sp = st.R[4]
                for j in range(1, 65):
                    st.mem[(1 << 16) | ((sp - j) & M16)] = \
                        mix(h, j) & 0xFF
                return nxt
            return call
        if m in ("ret", "retn", "retf", "iret"):
            void = fname in self.voids
            def ret(st):
                st.ev.append(("R", None if void else st.R[0], st.R[4],
                              st.R[5], st.R[6], st.R[7]))
                return None
            return ret
        if m in ("clc", "stc", "cmc", "cld", "nop"):
            def flag(st):
                if m == "clc":
                    st.cf = 0
                elif m == "stc":
                    st.cf = 1
                elif m == "cmc":
                    st.cf ^= 1
                return nxt
            return flag
        if m == "cbw":
            def cbw(st):
                al = st.R[0] & 0xFF
                st.R[0] = al | (0xFF00 if al & 0x80 else 0)
                return nxt
            return cbw
        if m == "cwd":
            def cwd(st):
                st.R[2] = 0xFFFF if st.R[0] & 0x8000 else 0
                return nxt
            return cwd
        if m == "push":
            o = compile_operand(it.ops[0], 16)
            if o.kind == "imm" or o.bits != 16:
                raise Unsupported("push %r" % it.code)
            def push(st, o=o):
                v = o.get(st)
                st.R[4] = (st.R[4] - 2) & M16
                wr16(st, 1, st.R[4], v)
                return nxt
            return push
        if m == "pop":
            o = compile_operand(it.ops[0], 16)
            if o.kind == "imm" or o.bits != 16:
                raise Unsupported("pop %r" % it.code)
            def pop(st, o=o):
                v = rd16(st, 1, st.R[4])
                st.R[4] = (st.R[4] + 2) & M16
                o.put(st, v)
                return nxt
            return pop
        if m == "lea":
            d = compile_operand(it.ops[0], 16)
            sr = compile_operand(it.ops[1], 16)
            if sr.kind != "mem" or d.kind != "reg":
                raise Unsupported(it.code)
            def lea(st, d=d, a=sr.addr):
                d.put(st, a(st.R))
                return nxt
            return lea
        if m in cc8086.SHIFTS:
            if len(it.ops) != 2:
                raise Unsupported(it.code)
            d = compile_operand(it.ops[0])
            if d.bits is None:
                raise Unsupported("shift of unsized memory %r" % it.code)
            w = d.bits
            c = compile_operand(it.ops[1])
            if c.kind == "imm":
                cnt = c.get(None)
                if cnt != 1:
                    raise Unsupported("shift by %d" % cnt)
                cfn = lambda st: 1
            elif c.text.strip().lower() == "cl":
                cfn = lambda st: st.R[1] & 0xFF
            else:
                raise Unsupported(it.code)
            def sh(st, d=d):
                d.put(st, shift(st, m, d.get(st), cfn(st), w))
                return nxt
            return sh
        ops, w = operands(it)
        if m == "mov":
            d, sr = ops
            if d.kind == "imm":
                raise Unsupported(it.code)
            def mov(st, d=d, sr=sr):
                d.put(st, sr.get(st))
                return nxt
            return mov
        if m == "xchg":
            a, b = ops
            def xchg(st, a=a, b=b):
                x, y = a.get(st), b.get(st)
                a.put(st, y)
                b.put(st, x)
                return nxt
            return xchg
        if m in ("add", "adc", "sub", "sbb", "and", "or", "xor"):
            d, sr = ops
            def arith(st, d=d, sr=sr):
                d.put(st, alu(st, m, d.get(st), sr.get(st) & ((1 << w) - 1),
                              w))
                return nxt
            return arith
        if m in ("cmp", "test"):
            a, b = ops
            def cmp(st, a=a, b=b):
                alu(st, m, a.get(st), b.get(st) & ((1 << w) - 1), w)
                return nxt
            return cmp
        if m in ("inc", "dec"):
            d = ops[0]
            if d.bits is None:
                raise Unsupported(it.code)
            def incdec(st, d=d):
                c = st.cf
                d.put(st, alu(st, "add" if m == "inc" else "sub",
                              d.get(st), 1, w))
                st.cf = c
                return nxt
            return incdec
        if m == "neg":
            d = ops[0]
            def neg(st, d=d):
                v = d.get(st)
                d.put(st, alu(st, "sub", 0, v, w))
                st.cf = int(v != 0)
                return nxt
            return neg
        if m == "not":
            d = ops[0]
            def nt(st, d=d):
                d.put(st, ~d.get(st))
                return nxt
            return nt
        if m in ("mul", "imul", "div", "idiv"):
            if len(ops) != 1:
                raise Unsupported(it.code)
            sr = ops[0]
            if w != 16:
                raise Unsupported("byte %s" % m)
            def md(st, sr=sr):
                v = sr.get(st)
                if m == "mul":
                    r = st.R[0] * v
                    st.R[0], st.R[2] = r & M16, (r >> 16) & M16
                    st.cf = st.of = int(st.R[2] != 0)
                    return nxt
                if m == "imul":
                    a = st.R[0] - 0x10000 if st.R[0] & 0x8000 else st.R[0]
                    b = v - 0x10000 if v & 0x8000 else v
                    r = (a * b) & 0xFFFFFFFF
                    st.R[0], st.R[2] = r & M16, r >> 16
                    sx = 0xFFFF if r & 0x8000 else 0
                    st.cf = st.of = int(st.R[2] != sx)
                    return nxt
                num = (st.R[2] << 16) | st.R[0]
                if v == 0:
                    st.ev.append(("X", "div0"))
                    return None
                if m == "div":
                    q, r = divmod(num, v)
                    if q > M16:
                        st.ev.append(("X", "div-overflow"))
                        return None
                else:
                    a = num - (1 << 32) if num & 0x80000000 else num
                    b = v - 0x10000 if v & 0x8000 else v
                    q = abs(a) // abs(b)
                    if (a < 0) != (b < 0):
                        q = -q
                    r = a - q * b
                    if not -0x8000 <= q <= 0x7FFF:
                        st.ev.append(("X", "div-overflow"))
                        return None
                st.R[0], st.R[2] = q & M16, r & M16
                # flags are undefined after a divide: scramble them the same
                # way in both versions so nothing can come to rely on them
                st.cf = st.zf = st.sf = st.of = st.pf = 0
                return nxt
            return md
        raise Unsupported("mnemonic %s" % m)

    def consts(self, name):
        """Every literal the function names, for the input pool."""
        out = set()
        for j, _ in self.function_text(name):
            n = self.seq[self.funcs[name][0]][j]
            if n.kind != "ins":
                continue
            for o in n.it.ops:
                v = cc8086.imm_value(o)
                if v is None:
                    inner = re.search(r"[-+]\s*(\d+)\s*\]", o)
                    v = int(inner.group(1)) if inner else None
                if v is not None:
                    out.add(v & M16)
        return tuple(sorted(out))

    def run(self, fname, seed, regs, cover=None, pool=(), start=None):
        s, k = self.lab[start] if start else self.funcs[fname]
        st = Machine(seed, regs, pool)
        lst = self.code[s]
        steps = 0
        while k is not None:
            if cover is not None:
                cover.add(k)
            f = lst[k]
            if f is None:
                f = self.step_fn(s, k, fname)
            k = f(st)
            if st.R[4] > st.sp0:
                st.sp0 = st.R[4]        # a mid-function entry's pops
            steps += 1
            if steps >= STEP_CAP or len(st.ev) >= EVENT_CAP:
                st.ev.append(("X", "cap"))
                return st.ev, True
        return st.ev, False


def initial_regs(seed, pool=()):
    regs = [biased_word(mix(seed, 100 + i), pool) for i in range(8)]
    regs[4] = 0xE000 + (mix(seed, 7) & 0x0FFE)        # SP: even, roomy
    regs[5] = mix(seed, 8) & 0xFFFE                    # BP: the caller's
    return regs


def changed_functions(a, b):
    """Functions whose instruction text differs, with the rewritten sites of
    the NEW version: an instruction that is new or replaced, or the first one
    after a deletion."""
    out = {}
    for name in b.funcs:
        if name not in a.funcs:
            continue
        ta, tb = a.function_text(name), b.function_text(name)
        la = [t for _, t in ta]
        lb = [t for _, t in tb]
        if la == lb:
            continue
        sites = set()
        sm = difflib.SequenceMatcher(None, la, lb, autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == "equal":
                continue
            if j2 > j1:
                for j in range(j1, j2):
                    sites.add(tb[j][0])
            elif j1 < len(tb):
                sites.add(tb[j1][0])
        out[name] = sites
    return out


def compare(ea, ca, eb, cb):
    if not ca and not cb:
        return ea == eb
    n = min(len(ea), len(eb)) - 1         # the cap marker itself excluded
    return ea[:n] == eb[:n]


# cc8086's own labels name only the inside of its setcc lowering, `mov al,1 /
# jCC x / mov al,0 / x:` (tools/cc8086.py, Lowerer.label), so x is reached
# with AL = 0 or 1 and from nowhere else. Entering there with a random AX is a
# machine no program can be in, and the pass's rewrites of that idiom rest on
# the 0/1 (tools/ccpeep.py, idiom()) - so a mid-function entry never starts
# at one; it starts at the label above the idiom instead.
IDIOM = "..@cc8086_"


def entry_labels(a, b, name, missed):
    """For each site not yet executed, the nearest label above it that
    exists in BOTH versions, in the same function."""
    s, k0 = b.funcs[name]
    out = []
    lst = b.seq[s]
    for j in sorted(missed):
        k = j
        while k > k0:
            n = lst[k]
            if n.kind == "label" and n.label in a.lab and \
                    a.lab[n.label][0] == s and \
                    not n.label.startswith(IDIOM):
                break
            k -= 1
        lab = lst[k].label
        if k > k0 and lab not in out and not lab.startswith(IDIOM):
            out.append(lab)
    return out


def verify(orig, new, tmin=8, tmax=48, seed=1, log=None, maxfail=5,
           tmid=12):
    """Run every changed function of the two texts against each other.
    Returns (failures, stats)."""
    a, b = Program(orig), Program(new)
    ch = changed_functions(a, b)
    fails = []
    nsites = ncov = trials = 0
    unsup = {}
    for name in sorted(ch):
        sites = ch[name]
        cover = set()
        pool = a.consts(name)
        for t in range(tmax):
            sd = mix(seed, zlib.crc32(name.encode()) + t * 7919)
            pl = pool if t % 4 else ()      # a quarter of trials without
            regs = initial_regs(sd, pl)
            ea, ca = a.run(name, sd, regs, None, pl)
            eb, cb = b.run(name, sd, regs, cover, pl)
            trials += 1
            for e in (ea, eb):
                if e and e[-1][0] == "X" and e[-1][1] != "cap":
                    why = e[-1][1] if len(e[-1]) < 3 else e[-1][2]
                    unsup[why] = unsup.get(why, 0) + 1
            if not compare(ea, ca, eb, cb):
                fails.append((name, sd, first_diff(ea, eb)))
                break
            if t + 1 >= tmin and sites <= cover:
                break
        # MID-FUNCTION ENTRIES. A site random inputs did not reach from the
        # top is entered from the nearest label above it that both versions
        # still have, with the whole machine random. That is sound for this
        # pass because no rewrite rests on a fact established ABOVE a label:
        # each works on a straight run with no label inside it, plus liveness,
        # which looks only forward - so the two versions must agree from
        # every common label on, for every state.
        if not fails or fails[-1][0] != name:
            for lab in entry_labels(a, b, name, sites - cover):
                for t in range(tmid):
                    sd = mix(seed ^ 0x3C3C, zlib.crc32(lab.encode()) + t)
                    pl = pool if t % 4 else ()
                    regs = initial_regs(sd, pl)
                    ea, ca = a.run(name, sd, regs, None, pl, lab)
                    eb, cb = b.run(name, sd, regs, cover, pl, lab)
                    trials += 1
                    if not compare(ea, ca, eb, cb):
                        fails.append((name, sd, "from %s: %s"
                                      % (lab, first_diff(ea, eb))))
                        break
                    if sites <= cover:
                        break
                if fails and fails[-1][0] == name or sites <= cover:
                    break
        nsites += len(sites)
        ncov += len(sites & cover)
        if len(fails) >= maxfail:
            break
    return fails, {"functions": len(ch), "sites": nsites, "covered": ncov,
                   "trials": trials, "unsupported": unsup}


def first_diff(ea, eb):
    for k, (x, y) in enumerate(zip(ea, eb)):
        if x != y:
            return "event %d: before %r, after %r" % (k, x, y)
    return "lengths %d / %d; tails %r / %r" % (len(ea), len(eb), ea[-2:],
                                               eb[-2:])


def lower(path):
    """tools/cc8086.py's own output for a raw SmallerC file, peephole OFF."""
    text = open(path, encoding="utf-8", errors="replace").read()
    ns = argparse.Namespace(max_frame=10000, no_gate=False, quiet=True, output=None,
                            no_peep=True)
    gate, errors, out = cc8086.run(path, text, ns)
    if gate or errors:
        raise SystemExit("ccverify: %s does not lower cleanly" % path)
    return out


# ------------------------------------------------------------- the self test
# Each entry is a wrong rewrite planted into ccpeep.Prog for one run; the
# verifier must report a difference for every one of them.
def _break_setcc(P):
    orig = P.r_setcc

    def bad(self, i, touched):
        hit = orig(self, i, touched)
        if hit:
            it = self.nodes[i].it
            m = it.mnem
            self.set_ins(i, "j%s\t%s" % (ccpeep.NEG[m[1:]], it.ops[0]))
        return hit
    P.r_setcc = bad


def _break_dead(P):
    def bad(self, i, touched):
        it = self.nodes[i].it
        if it.mnem == "mov" and it.ops and ccpeep.low(it.ops[0]) == "ax" \
                and not cc8086.is_mem(it.ops[1]) and \
                self.lout(i) & ccpeep.AXM and "ccverify" not in it.comment:
            self.nodes[i].dead = True       # deletes a LIVE write
            self.count("dead")
            return (i,)
        return None
    P.r_dead = bad


def _break_fold(P):
    orig_lout = P.lout

    def bad(self, i, touched):
        save = self.lout
        self.lout = lambda j: 0             # pretend nothing is ever live
        try:
            return P._fold_orig(self, i, touched)
        finally:
            self.lout = save
    P._fold_orig = P.r_fold
    P.r_fold = bad
    del orig_lout


def _break_shift(P):
    orig = P.r_shift

    def bad(self, i, touched):
        hit = orig(self, i, touched)
        if hit:
            for k in hit:
                n = self.nodes[k]
                if not n.dead and n.it.mnem == "mov" and \
                        ccpeep.low(n.it.ops[0]) == "cl":
                    v = cc8086.imm_value(n.it.ops[1])
                    self.set_ins(k, "mov\tcl, %d" % (v - 1))
        return hit
    P.r_shift = bad


def _break_jccjmp(P):
    P.BREAK_JCCJMP = True


def _planted(name):
    """A rule ccpeep.Prog itself knows how to get wrong (its BREAK set)."""
    def plant(P):
        P.BREAK = frozenset([name])
    return plant


BREAKS = {"setcc": _break_setcc, "dead": _break_dead, "fold": _break_fold,
          "shift": _break_shift, "jccjmp": _break_jccjmp}
# the rules of the shared round (SPEC.md 73.15): what each plants
#   chain    the known outcome taken the wrong way round
#   renorm   a `setne` of a 0/1 treated as a `sete`
#   bthread  the threaded branch's condition inverted
#   bool1    `inc ax` for `dec ax`
#   immcopy  a static's CONTENTS taken for its address (`[_x]` as `_x`)
#   direct   the displacement two bytes off
#   addimm   the sum one off
#   andid    every `and r, K` taken for an identity
#   pushst   the base loaded into BX before the value's run, not after it
for _n in ("chain", "renorm", "bthread", "bool1", "immcopy", "direct",
           "addimm", "andid", "pushst"):
    BREAKS[_n] = _planted(_n)


def self_test(orig, args):
    ok = True
    for name, plant in BREAKS.items():
        saved = dict(ccpeep.Prog.__dict__)
        try:
            plant(ccpeep.Prog)
            new, counts = ccpeep.peephole(orig)
        finally:
            for k in list(ccpeep.Prog.__dict__):
                if k not in saved:
                    delattr(ccpeep.Prog, k)
            for k, v in saved.items():
                if k not in ("__dict__", "__weakref__"):
                    setattr(ccpeep.Prog, k, v)
        if not counts.get(name):
            print("ccverify: self-test %-9s n/a - the rule never fires on "
                  "this input" % name)
            continue
        fails, stats = verify(orig, new, args.tmin, args.tmax, args.seed,
                              maxfail=1)
        caught = bool(fails)
        ok &= caught
        print("ccverify: self-test %-9s %s  (%s)"
              % (name, "CAUGHT" if caught else "MISSED <-- the verifier is "
                 "blind to this", fails[0][2] if fails else stats))
    return ok


def main():
    ap = argparse.ArgumentParser(
        description="Differentially check tools/ccpeep.py on C packages.")
    ap.add_argument("inputs", nargs="+", metavar="X.raw.asm",
                    help="smlrcc output; lowered here by tools/cc8086.py")
    ap.add_argument("--tmin", type=int, default=8)
    ap.add_argument("--tmax", type=int, default=48)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--self-test", action="store_true",
                    help="plant wrong rewrites and require each be caught")
    args = ap.parse_args()
    bad = 0
    for path in args.inputs:
        orig = lower(path)
        if args.self_test:
            if not self_test(orig, args):
                bad += 1
            continue
        new, counts = ccpeep.peephole(orig)
        fails, st = verify(orig, new, args.tmin, args.tmax, args.seed)
        pct = 100.0 * st["covered"] / st["sites"] if st["sites"] else 100.0
        print("ccverify: %s: %d function(s) changed, %d trial(s), %d/%d "
              "rewritten site(s) executed (%.1f%%), %d run(s) stopped on a "
              "trap - %s"
              % (path, st["functions"], st["trials"], st["covered"],
                 st["sites"], pct, sum(st["unsupported"].values()),
                 "EQUIVALENT on every trial" if not fails else
                 "%d DIFFERENCE(S)" % len(fails)))
        for why, k in sorted(st["unsupported"].items()):
            print("ccverify:   trap %r x%d" % (why, k))
        for name, sd, why in fails:
            print("ccverify:   %s (seed %d): %s" % (name, sd, why))
        if fails:
            bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
