#!/usr/bin/env python3
# =============================================================================
# os8088 - tools/ccpeep.py       the C peephole pass over cc8086's output
#
#     run by tools/cc8086.py on every C package (SPEC.md 73.15); by hand:
#     python3 tools/ccpeep.py build/x.gen.asm -o build/x.peep.asm [--only a,b]
#     `make CCPEEP=0` builds every C package without it (the A/B), and
#     tools/ccverify.py is its differential check
#
# WHAT IT IS. SmallerC compiles one expression at a time and tools/cc8086.py
# lowers its 80386 forms one instruction at a time, so the assembly a C package
# carries the seams between statements: a comparison turned into a 0/1 in AX
# and then tested again by the branch that wanted only the flags, a value
# reloaded from the slot it was stored to on the line before, a `cbw` widening
# a byte nobody reads. This pass removes those seams, and only those, using a
# whole-file LIVENESS analysis over the control-flow graph rather than a window
# of a few lines - so a rewrite is justified by what the program can still read
# on EVERY path out of the site, not by what the next line happens to be.
#
# It was written in CWORD's size pass as apps/cword/cwpeep.py and shipped
# OFF there, because its first combined build miscompiled (thread and jccjmp
# rewriting one branch in one sweep - fixed by the per-sweep node ownership in
# pass_flow) and nothing could say whether that was the last such defect.
# tools/ccverify.py is what says so now: it runs every function the pass
# changes before and after, on randomised machines, and fails on any
# observable difference - and --self-test plants wrong rewrites to prove it
# would.
#
# WHAT IT MAY ASSUME is what tools/cc8086.py already rests its own liveness on
# (the note on its def_use()): the input is COMPILED C. The SmallerC ABI passes
# every argument on the stack and answers in AX, so a call reads no register
# and leaves AX and FLAGS defined, and no caller reads the FLAGS a C function
# returns. The one routine the input calls that DOES answer in a flag,
# cc8086's own `call cc_ovneed / jc`, is covered by modelling every call as
# DEFINING the flags - which is exactly what it does. A call is deliberately
# NOT modelled as killing BX, CX or DX: that is the conservative direction,
# and keeps them live across it. Anything this file does not model reads
# every register, so an unknown is a missed rewrite, never a wrong one.
#
# THE REWRITES. Each replaces a sequence by a strict subset of its work, so no
# path through the program gets longer and none gets slower:
#
#   setcc    `mov al,1 / jCC x / mov al,0 / x: cbw / test ax,ax / je|jne L`
#            -> `jNCC|jCC L` when AX and FLAGS are dead on both edges out:
#            `if (a == b)`, which is 11 bytes for 2.
#   thread   a `je`/`jne` straight after `test ax,ax` whose target is another
#            `test ax,ax / je|jne M` has a KNOWN outcome there (AX did not
#            change), so it goes to M, or past the second branch, directly.
#            What an `||`/`&&` chain needs before `setcc` can see it.
#   jmpjmp   a jump to a `jmp M` goes to M.
#   jccjmp   `jCC A / jmp B / A:` -> `jNCC B / A:`.
#   jmpnext  a `jmp L` with only labels between it and L goes.
#   nolabel  a compiler-local label nothing names any more goes (it is a
#            merge point the analysis would otherwise have to respect).
#   reload   `mov [m], ax / mov ax, [m]` (or AL): the reload goes.
#   copy     `mov r1, r2 / mov r3, r1` -> `mov r1, r2 / mov r3, r2`, after
#            which the first is usually `dead`.
#   test0    `cmp r16, 0` -> `test r16, r16`: the same flags but AF, which
#            no compiled C reads (it emits no BCD adjust), in one byte less.
#   fold     `mov ax, m / OP ..ax..` -> `OP ..m..` when AX is dead after: a
#            `push`, a `cmp ax, imm`, a `test ax,ax`, a `mov r, ax`, an
#            `add|sub|and|or|xor|cmp r, ax`.
#   imm      `mov ax, imm / mov m, ax` -> `mov word m, imm` when AX is dead.
#   index    `mov ax, SYM / add ax, R / mov bx, ax / OP [bx..]` ->
#            `mov bx, R / OP [bx+SYM..]`, and `add ax, N / mov bx, ax /
#            OP [bx..]` -> `mov bx, ax / OP [bx+N..]`: an array element is
#            addressed by the 8086's own displacement rather than by
#            arithmetic in AX.
#   dead     an instruction whose only effect is to write AX/BX/CX/DX halves
#            (and possibly FLAGS) that no path reads again goes: the `cbw`
#            after a byte store, `mov ax, [x]` before a statement-level `x++`.
#   spadj    `sub sp, -N` before the `mov sp, bp` that discards it goes.
#   popsp    `sub sp, -2|-4` after a call -> `pop` of a dead register.
#   commute  `mov r, ax / mov ax, X / OP ax, r` -> `OP ax, X` (OP commutes).
#   shift    a run of N `OP r, 1` (cc8086's lowering of `OP r, N`): the first
#            8 become a byte move, and a run of 4 or more `mov cl, N / OP r,
#            cl`, when FLAGS (and CL) are dead after. See SHIFT_CL_MIN.
#
# ...and the shared round's (SPEC.md 73.15.2), which take what the first set
# left of a static's address, a pushed store and a 0/1:
#
#   immcopy  `mov ax, IMM / mov r, ax` -> `mov r, IMM` when AX is dead.
#   direct   `mov bx|si|di, IMM / OP [r+k]` -> `OP [IMM+k]`, when r is dead
#            after its last use and the bytes go down: `s.x` for a static s.
#   addimm   `mov r, IMM / add|sub r, N` (inc, dec) -> `mov r, IMM+N` when
#            FLAGS are dead: `&s.x`, `&a[3]`.
#   andid    `and r, -1` (`x & 0xFFFF`), `or|xor|add|sub r, 0` go when FLAGS
#            are dead, and an `and|or r, K` repeating the one before goes.
#   pushst   `mov ax, A / [add ax, B] / push ax / VALUE / pop bx / OP [bx]`
#            -> `VALUE / [mov bx, BASE] / OP [bx+DISP]` or `OP [DISP]`: a
#            store (or load) through an address SmallerC pushed while it
#            evaluated the value - `p->x = v`, `s.x = v`, `a[i] = v`.
#   chain    the setcc shape whose branch lands on ANOTHER test of AX (an
#            `&&`/`||`): its outcome there is known, so one jump goes where
#            the second branch would have sent it, near or far.
#   bthread  a branch on a fresh 0/1 to a `setne` of AX lands past it.
#   renorm   a 0/1 normalised again - `test / setne` (the identity: gone) or
#            `test / sete` (the comparison inverted) - straight after it.
#   bool1    LAST, once nothing else can: `mov al,1 / jCC x / mov al,0 / x:
#            cbw` -> `mov ax,1 / jCC x / dec ax / x:` when FLAGS are dead.
#   pool     not code: a string literal repeating an earlier one, or the
#            tail of a longer one, becomes an `equ` of it (pool_literals).
#
# EVERY JUMP RETARGET IS SIZE-AWARE (Prog.layout): a retarget is taken only
# when the jump still reaches short, by the exact encoded layout with each
# jump relaxed the way NASM relaxes it, or was long already.
#
# Every rewrite is counted and printed, and `--off` copies the input unchanged
# so the pass can be A/B'd in one rebuild.
# =============================================================================
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cc8086  # noqa: E402

# ------------------------------------------------------------ unit bitmasks
UNITS = sorted(cc8086.ALL_UNITS) + ["F"]
BIT = {u: 1 << k for k, u in enumerate(UNITS)}
ALL = (1 << len(UNITS)) - 1
FB = BIT["F"]


def mask(units):
    v = 0
    for u in units:
        v |= BIT[u]
    return v


def rmask(r):
    return mask(cc8086.units(r))


AXM, BXM, CXM, DXM = rmask("ax"), rmask("bx"), rmask("cx"), rmask("dx")
SPBP = rmask("sp") | rmask("bp")

CODE_SECTIONS = (".text", ".modc")
DATA_DIRS = {"db", "dw", "dd", "dq", "dt", "resb", "resw", "resd", "resq",
             "times", "align", "alignb", "incbin", "equ", "org", "absolute",
             "struc", "endstruc", "istruc", "at", "iend"}
NEG = {"e": "ne", "ne": "e", "z": "nz", "nz": "z", "l": "ge", "ge": "l",
       "g": "le", "le": "g", "b": "ae", "ae": "b", "a": "be", "be": "a",
       "s": "ns", "ns": "s", "c": "nc", "nc": "c", "o": "no", "no": "o",
       "nge": "nl", "nl": "nge", "ng": "nle", "nle": "ng", "nae": "nb",
       "nb": "nae", "na": "nbe", "nbe": "na", "p": "np", "np": "p",
       "pe": "po", "po": "pe"}
WORDREG = ("ax", "bx", "cx", "dx", "si", "di")
GPREG = ("ax", "al", "ah", "bx", "bl", "bh", "cx", "cl", "ch", "dx", "dl",
         "dh")
SECTION = re.compile(r"^\s*section\s+(\.\w+)", re.I)
LOCAL = re.compile(r"^(L\d+|\.\.@cc8086_\d+|\.\.@ccp_\d+)$")
IDENT = re.compile(r"[A-Za-z_.?$][\w.?$@#~]*")
ZF_BRANCH = ("je", "jz", "jne", "jnz")


# ------------------------------------------------------------- byte sizes
# Jump retargeting is SIZE-AWARE: a short jump (2 bytes, +-127) that is aimed
# further than it can reach is relaxed by NASM into the 5-byte inverted pair
# (`jcc` on an 8086) or the 3-byte near `jmp`, which costs more than the hop
# the retarget saves. So the pass keeps a LAYOUT - every instruction's 8086
# encoding size and every jump's short/long form, relaxed the way NASM relaxes
# them - and a retarget is taken only if the jump stays short at its new
# distance (or was long already). Every rewrite in this file only ever
# REMOVES bytes, so a distance measured now can only shrink later: a jump
# that is short when it is retargeted stays short. SHORT_MARGIN is slack for
# the one estimate in the model - a symbol's value is unknown here, so an
# immediate naming one is priced at 16 bits, which is what NASM emits.
SHORT_MARGIN = 2
DISP8 = re.compile(r"^[-+]?\d+$")


def ea_regs(op):
    """The base/index registers of a memory operand, by whole TERM (a symbol
    named `_cw_bx` is not BX, which cc8086.mem_regs's scan would say)."""
    inner = re.search(r"\[([^\]]*)\]", op)
    body = re.sub(r"\b(ss|ds|es|cs)\s*:", "", inner.group(1) if inner else "")
    return [t.lower() for t in re.split(r"[-+\s]+", body)
            if t.lower() in ("bx", "bp", "si", "di")]


def modrm_size(op):
    """ModRM plus displacement bytes for a memory operand."""
    inner = re.search(r"\[([^\]]*)\]", op)
    body = re.sub(r"\b(ss|ds|es|cs)\s*:", "", inner.group(1) if inner else "")
    terms = [t for t in re.split(r"([+-])", body.replace(" ", "")) if t]
    regs, const, sym = [], 0, False
    sign = 1
    for t in terms:
        if t in "+-":
            sign = -1 if t == "-" else 1
            continue
        tl = t.lower()
        if tl in ("bx", "bp", "si", "di"):
            regs.append(tl)
        elif cc8086.imm_value(t) is not None:
            const += sign * cc8086.imm_value(t)
        else:
            sym = True
    if not regs:
        return 3                        # [disp16]
    if sym or not -128 <= const <= 127:
        return 3
    if const == 0 and regs != ["bp"]:
        return 1
    return 2


def imm_fits8(op):
    v = cc8086.imm_value(op)
    return v is not None and -128 <= v <= 127


def ins_size(it):
    """The 8086 encoding size of one non-jump instruction NASM will emit.
    Exact for the subset tools/cc8086.py produces; 4 for anything else."""
    m, ops = it.mnem, [o.strip() for o in it.ops]
    lo = [cc8086.strip_size(o).lower() for o in ops]
    mem = [cc8086.is_mem(o) for o in ops]
    reg8 = [o in cc8086.BYTE_OF for o in lo]
    reg16 = [o in cc8086.WORD_REGS for o in lo]
    if m in ("cbw", "cwd", "ret", "retf", "clc", "stc", "cld", "std", "cmc",
             "nop", "lahf", "sahf", "pushf", "popf", "iret"):
        return 1
    if m in ("ret", "retf") and ops:
        return 3
    if m == "call":
        if ops and ops[0].lower().startswith("far"):
            return 1 + modrm_size(ops[0])
        if mem and mem[0]:
            return 1 + modrm_size(ops[0])
        return 2 if reg16 and reg16[0] else 3
    if m in ("push", "pop") and len(ops) == 1:
        return 1 if reg16[0] else 1 + modrm_size(ops[0])
    if m in ("inc", "dec") and len(ops) == 1:
        return 1 if reg16[0] else 2 if reg8[0] else 1 + modrm_size(ops[0])
    if m in ("neg", "not", "mul", "imul", "div", "idiv") and len(ops) == 1:
        return 1 + (modrm_size(ops[0]) if mem[0] else 1)
    if m in cc8086.SHIFTS and len(ops) == 2:
        return 1 + (modrm_size(ops[0]) if mem[0] else 1)
    if m == "lea" and len(ops) == 2:
        return 1 + modrm_size(ops[1])
    if len(ops) != 2:
        return 4
    acc = lo[0] in ("ax", "al") or lo[1] in ("ax", "al")
    if m == "mov":
        if mem[0] or mem[1]:
            k = 0 if mem[0] else 1
            other = lo[1 - k]
            if other in cc8086.WORD_REGS or other in cc8086.BYTE_OF:
                if other in ("ax", "al") and not ea_regs(ops[k]):
                    return 3            # A0-A3, the accumulator's own form
                return 1 + modrm_size(ops[k])
            byte = ops[0].lower().startswith("byte")
            return 1 + modrm_size(ops[k]) + (1 if byte else 2)
        if reg16[0] and (reg16[1] or lo[1] in cc8086.SEG_REGS):
            return 2
        if reg8[0] and reg8[1]:
            return 2
        if reg16[0]:
            return 3
        if reg8[0]:
            return 2
        return 4
    if m in ("add", "sub", "adc", "sbb", "and", "or", "xor", "cmp", "test",
             "xchg"):
        if mem[0] or mem[1]:
            k = 0 if mem[0] else 1
            if reg16[1 - k] or reg8[1 - k]:
                return 1 + modrm_size(ops[k])
            byte = ops[0].lower().startswith("byte")
            small = byte or (m != "test" and imm_fits8(ops[1]))
            return 1 + modrm_size(ops[k]) + (1 if small else 2)
        if (reg16[0] and reg16[1]) or (reg8[0] and reg8[1]):
            if m == "xchg" and acc and reg16[0]:
                return 1
            return 2
        if reg8[0]:
            return 2
        if reg16[0]:
            if m != "test" and imm_fits8(ops[1]):
                return 3
            return 3 if lo[0] == "ax" else 4
        return 4
    return 4


class Node:
    __slots__ = ("kind", "text", "it", "label", "sec", "dead", "post")

    def __init__(self, kind, text, it=None, label=None, sec=None):
        self.kind = kind            # 'ins' 'label' 'other' 'barrier'
        self.text = text
        self.it = it
        self.label = label
        self.sec = sec
        self.dead = False
        self.post = None            # a label to materialise after this node


GLB = re.compile(r"^;\s*glb\s+(\w+)\s*:\s*\(")
GLB_END = re.compile(r"^;\s*\)\s*(.*?)\s*$")


def void_functions(text):
    """The C functions SmallerC's own `; glb f : ( ... ) void` comment says
    return nothing. AX is not live at such a function's `ret`: no C caller
    can read the value of a void call, and crt0.asm's trampolines for the
    void callbacks (cc_paint, cc_onkey, ...) read nothing back either."""
    out, cur = set(), None
    for line in text.split("\n"):
        m = GLB.match(line)
        if m:
            cur = m.group(1)
            continue
        if cur:
            e = GLB_END.match(line)
            if e:
                if e.group(1) == "void":
                    out.add("_" + cur)
                cur = None
            elif not line.startswith(";"):
                cur = None
    return out


def parse(text):
    nodes, sec = [], ".text"
    for no, raw in enumerate(text.split("\n"), 1):
        m = SECTION.match(raw)
        if m:
            sec = m.group(1).lower()
            nodes.append(Node("other", raw, sec=sec))
            continue
        it = cc8086.Item(no, raw)
        if it.label:
            nodes.append(Node("label", it.label + ":", label=it.label, sec=sec))
            if it.kind == "label":
                continue
            # `name: mov bx, _f` is split so the instruction is a node of its
            # own; NASM assembles the two spellings to the same bytes.
            raw = it.indent + it.code + it.comment
            it = cc8086.Item(no, raw)
        if it.kind == "ins":
            nodes.append(Node("ins", raw, it=it, sec=sec))
        elif it.kind == "dir" and it.mnem in DATA_DIRS:
            nodes.append(Node("barrier", raw, it=it, sec=sec))
        else:
            nodes.append(Node("other", raw, it=it, sec=sec))
    return nodes


def target(it):
    """The label a jmp/jcc names, or None if it is not a plain label."""
    if len(it.ops) != 1:
        return None
    op = cc8086.strip_size(it.ops[0])
    return op if re.match(r"^[A-Za-z_.?$][\w.?$@#~]*$", op) else None


def is_jcc(m):
    return bool(cc8086.JCC.match(m))


def low(op):
    return op.strip().lower()


def norm(op):
    return re.sub(r"\s+", "", cc8086.strip_size(op).lower())


def is_imm(op):
    """A value that is not a register and not memory: a literal or a symbol
    (the address of a static)."""
    o = low(op)
    return not cc8086.is_mem(op) and not cc8086.is_reg(o) and o != ""


def mem_reads(op, reg):
    """Does memory operand `op` address through a half of `reg`?"""
    return reg in cc8086.mem_regs(op)


def sub_base(op, r, value):
    """Memory operand `op` with its base register term `r` replaced by the
    expression `value` - `[bx+24]` with bx = `_z` is `[_z+24]`."""
    inner = re.search(r"\[([^\]]*)\]", op)
    body = re.sub(r"(?i)(?<![\w.?$@#~])%s(?![\w.?$@#~])" % r, value,
                  inner.group(1).strip(), count=1)
    return op[:inner.start(1)] + body + op[inner.end(1):]


def imm_add(a, b):
    """The immediate `a + b` (b an int), folded if both are literals."""
    va = cc8086.imm_value(a)
    if va is not None:
        return "%d" % ((va + b) & 0xFFFF)
    if b == 0:
        return a
    return "%s%+d" % (a, b)


# The local rewrites, in the order pass_local tries them at one site.
RULES = ("setcc", "chain", "renorm", "shift", "reload", "commute", "copy",
         "test0", "pushst", "index", "addimm", "immcopy", "direct", "andid",
         "fold", "imm", "spadj", "popsp", "dead")


class Prog:
    BREAK_JCCJMP = False            # tools/ccverify.py --self-test only
    BREAK = frozenset()             # ...and the rules it plants wrong

    def __init__(self, nodes, voids=frozenset()):
        self.nodes = nodes
        self.voids = voids
        self.flow_ok = None             # None: all; else a set (--only)
        self.counts = {}
        self.newlab = 0

    def count(self, k):
        self.counts[k] = self.counts.get(k, 0) + 1

    def ok(self, k):
        return self.flow_ok is None or k in self.flow_ok

    # ------------------------------------------------------------- the graph
    def compact(self):
        out = []
        for n in self.nodes:
            if not n.dead:
                out.append(n)
            if n.post:
                out.append(Node("label", n.post + ":", label=n.post,
                                sec=n.sec))
                n.post = None
        self.nodes = out

    def rebuild(self):
        self.compact()
        self.nxt, self.prv, self.labpos = {}, {}, {}
        self.isvoid = set()
        prev = {s: None for s in CODE_SECTIONS}
        fn = {s: None for s in CODE_SECTIONS}
        for i, n in enumerate(self.nodes):
            if n.sec not in CODE_SECTIONS or n.kind not in \
                    ("ins", "label", "barrier"):
                continue
            if n.kind == "label" and not LOCAL.match(n.label):
                fn[n.sec] = n.label     # a function, or a shim: a new owner
            if n.kind == "ins" and fn[n.sec] in self.voids:
                self.isvoid.add(i)
            p = prev[n.sec]
            self.prv[i] = p
            self.nxt[i] = None
            if p is not None:
                self.nxt[p] = i
            prev[n.sec] = i
            if n.kind == "label":
                self.labpos[n.label] = i
        self.refs = {}
        for n in self.nodes:
            if n.kind in ("ins", "barrier", "other"):
                src = n.it.code if n.it is not None else n.text
                for t in IDENT.findall(src):
                    self.refs[t] = self.refs.get(t, 0) + 1
        self.order = [i for i in range(len(self.nodes)) if i in self.nxt]
        self.layout()

    def layout(self):
        """Every code node's byte offset in its section, with each jump
        relaxed to short or long exactly as NASM does it: all short to start,
        then grow any that cannot reach until nothing changes."""
        size, jmp = {}, {}
        for i in self.order:
            n = self.nodes[i]
            if n.kind != "ins":
                size[i] = 0
                continue
            m = n.it.mnem
            if m == "jmp" or is_jcc(m):
                t = target(n.it)
                if t and t in self.labpos and \
                        self.nodes[self.labpos[t]].sec == n.sec:
                    jmp[i] = self.labpos[t]
                    size[i] = 2
                    continue
                size[i] = 5 if is_jcc(m) else 3
                continue
            size[i] = ins_size(n.it) * (2 if "\n" in n.text else 1)
        self.size = size
        self.longj = set()
        while True:
            self.off, run = {}, {s: 0 for s in CODE_SECTIONS}
            for i in self.order:
                self.off[i] = run[self.nodes[i].sec]
                run[self.nodes[i].sec] += size[i]
            grew = False
            for i, t in jmp.items():
                if i in self.longj:
                    continue
                d = self.off[t] - (self.off[i] + 2)
                if not -128 <= d <= 127:
                    self.longj.add(i)
                    size[i] = 5 if is_jcc(self.nodes[i].it.mnem) else 3
                    grew = True
            if not grew:
                break

    # live-aware walkers: they skip nodes already deleted this round
    def step(self, i):
        j = self.nxt[i]
        while j is not None and self.nodes[j].dead:
            j = self.nxt[j]
        return j

    def back(self, i):
        j = self.prv[i]
        while j is not None and self.nodes[j].dead:
            j = self.prv[j]
        return j

    def ins_after(self, i):
        """The next node if it is an instruction with no label between."""
        j = self.step(i)
        return j if j is not None and self.nodes[j].kind == "ins" else None

    def ins_before(self, i):
        j = self.back(i)
        return j if j is not None and self.nodes[j].kind == "ins" else None

    def first_ins(self, i):
        """The first instruction at or after node i, skipping labels."""
        while i is not None:
            n = self.nodes[i]
            if not n.dead:
                if n.kind == "ins":
                    return i
                if n.kind == "barrier":
                    return None
            i = self.nxt[i]
        return None

    def effect(self, i):
        """(use, def, succs) of node i as bitmasks; a succ of None is an
        unknown destination, which keeps everything live."""
        n = self.nodes[i]
        nx = self.nxt[i]
        if n.kind == "label":
            return 0, 0, [nx]
        if n.kind == "barrier":
            return ALL, 0, []
        it = n.it
        m = it.mnem
        if m == "jmp":
            t = target(it)
            if t and t in self.labpos:
                return 0, 0, [self.labpos[t]]
            return ALL, 0, []
        if is_jcc(m):
            t = target(it)
            if t and t in self.labpos:
                return FB, 0, [self.labpos[t], nx]
            return ALL, 0, [nx]
        if m in ("ret", "retn", "retf", "iret"):
            return (SPBP if i in self.isvoid else AXM | SPBP), 0, []
        if m == "call":
            u = rmask("sp")
            for o in it.ops:
                u |= mask(cc8086.op_units(o))
            return u, AXM | FB, [nx]
        u, d, term = cc8086.def_use(it)
        if term:
            return ALL, 0, [nx]
        u, d = mask(u), mask(d)
        if cc8086.SETCC.match(m) or m in cc8086.FLAG_READERS:
            u |= FB
        elif cc8086.flags_killed(it):
            d |= FB
        elif m not in cc8086.FLAG_TRANSPARENT and m not in cc8086.FLAG_WRITERS:
            u |= FB                     # never seen it: assume it reads
        if m in ("push", "pop", "pushf", "popf"):
            u |= rmask("sp")
        return u, d, [nx]

    def liveness(self):
        self.rebuild()
        eff = {i: self.effect(i) for i in self.order}
        lin = dict.fromkeys(self.order, 0)
        rev = self.order[::-1]
        changed = True
        while changed:
            changed = False
            for i in rev:
                u, d, succ = eff[i]
                out = 0
                for s in succ:
                    out |= ALL if s is None else lin[s]
                new = u | (out & ~d)
                if new != lin[i]:
                    lin[i] = new
                    changed = True
        self.eff, self.lin = eff, lin

    def lout(self, i):
        out = 0
        for s in self.eff[i][2]:
            out |= ALL if s is None else self.lin[s]
        return out

    # ------------------------------------------------------------- editing
    def set_ins(self, i, code):
        it = self.nodes[i].it
        self.nodes[i].it = cc8086.Item(it.no, it.indent + code)
        self.nodes[i].text = it.indent + code

    def label_after(self, i):
        """A label naming the point just after node i, made if need be."""
        j = self.nxt[i]
        while j is not None and self.nodes[j].dead:
            j = self.nxt[j]
        if j is not None and self.nodes[j].kind == "label":
            return self.nodes[j].label
        if self.nodes[i].post is None:
            self.newlab += 1
            self.nodes[i].post = "..@ccp_%d" % self.newlab
        return self.nodes[i].post

    def near(self, i, new, old=None):
        """Would aiming the jump at node i at `new` keep it the size it is?
        A short jump that becomes a long one costs more than the hop it
        saves, so only a target the short form still reaches - by the
        relaxed layout, see layout() - or a jump that was long already, is
        taken. A label made this sweep (label_after) has no offset yet: it
        names the point after an instruction, so that instruction's offset
        plus its size is used."""
        if not self.ok("near"):
            return True
        if i in self.longj:
            return True
        if self.nodes[new].sec != self.nodes[i].sec:
            return False
        return self.reach(i, self.off[new])

    def reach(self, i, at):
        """Does a SHORT jump at node i reach byte offset `at`?"""
        d = at - (self.off[i] + 2)
        return -128 + SHORT_MARGIN <= d <= 127 - SHORT_MARGIN

    def is_test_ax(self, i):
        it = self.nodes[i].it
        return (it.mnem == "test" and [low(o) for o in it.ops] == ["ax", "ax"])

    # ------------------------------------------------------ control flow
    def pass_flow(self):
        self.rebuild()
        made = 0
        touched = set()
        for i in self.order:
            n = self.nodes[i]
            if n.kind == "label" and LOCAL.match(n.label) and \
                    self.refs.get(n.label, 0) == 0 and self.ok("nolabel"):
                n.dead = True
                self.count("nolabel")
                made += 1
        # ONE REWRITE PER NEIGHBOURHOOD PER SWEEP. A rule fires only if no
        # node it READS or WRITES was read or written by an earlier rule in
        # this sweep, and then claims them all: thread reads the branch after
        # its target and the point after that, and a jccjmp in the same sweep
        # that rewrote that branch and deleted the `jmp` behind it would leave
        # the thread aimed at the wrong side of it. The next sweep rebuilds and
        # sees the program as it now is.
        def free(*ns):
            return not (set(x for x in ns if x is not None) & touched)

        def claim(*ns):
            touched.update(x for x in ns if x is not None)

        for i in self.order:
            n = self.nodes[i]
            if n.dead or i in touched:
                continue
            if n.kind != "ins":
                continue
            m = n.it.mnem
            if m != "jmp" and not is_jcc(m):
                continue
            t = target(n.it)
            if not t or t not in self.labpos:
                continue
            f = self.first_ins(self.labpos[t])
            # jmpjmp
            if f is not None and f != i and self.nodes[f].it.mnem == "jmp" \
                    and self.ok("jmpjmp") and free(i, f):
                t2 = target(self.nodes[f].it)
                if t2 and t2 != t and t2 in self.labpos and \
                        self.near(i, self.labpos[t2]):
                    self.set_ins(i, "%s\t%s" % (m, t2))
                    claim(i, f)
                    self.count("jmpjmp")
                    made += 1
                    continue
            # thread
            if m in ZF_BRANCH and f is not None and self.is_test_ax(f) and \
                    self.ok("thread"):
                p = self.ins_before(i)
                g = self.ins_after(f)
                after = self.step(g) if g is not None else None
                if (p is not None and self.is_test_ax(p) and g is not None
                        and self.nodes[g].it.mnem in ZF_BRANCH
                        and free(i, p, f, g, after)):
                    gm = self.nodes[g].it.mnem
                    taken = (gm in ("jne", "jnz")) == (m in ("jne", "jnz"))
                    if taken:
                        t2 = target(self.nodes[g].it)
                        if t2 and t2 in self.labpos and t2 != t and \
                                self.near(i, self.labpos[t2]):
                            self.set_ins(i, "%s\t%s" % (m, t2))
                            claim(i, p, f, g, after)
                            self.count("thread")
                            made += 1
                            continue
                    elif i in self.longj or not self.ok("near") or \
                            self.reach(i, self.off[g] + self.size[g]):
                        self.set_ins(i, "%s\t%s" % (m, self.label_after(g)))
                        claim(i, p, f, g, after)
                        self.count("thread")
                        made += 1
                        continue
            # bthread: a branch on a fresh 0/1 to a `setne` of AX - the
            # value of an `&&`/`||` being materialised - lands past it: on a
            # 0/1 that normalisation is the identity, flags included (it is
            # `test ax,ax` and three instructions that write none).
            if m in ZF_BRANCH and f is not None and self.is_test_ax(f) and \
                    self.ok("bthread"):
                p = self.ins_before(i)
                j0 = self.ins_after(f)
                got = self.idiom(j0) if j0 is not None else None
                if (p is not None and self.is_test_ax(p) and
                        self.bool_end(self.ins_before(p)) is not None and
                        got is not None and
                        self.nodes[got[0]].it.mnem in ("jne", "jnz")):
                    j3 = got[3]
                    if free(i, p, f, j0, *got) and (
                            i in self.longj or not self.ok("near") or
                            self.reach(i, self.off[j3] + self.size[j3])):
                        bm = m if "bthread" not in self.BREAK else \
                            ("je" if m in ("jne", "jnz") else "jne")
                        self.set_ins(i, "%s\t%s" % (bm, self.label_after(j3)))
                        claim(i, p, f, j0, *got)
                        self.count("bthread")
                        made += 1
                        continue
            # jccjmp
            if is_jcc(m) and m[1:] in NEG and self.ok("jccjmp"):
                j = self.ins_after(i)
                if j is not None and free(i, j) and \
                        self.nodes[j].it.mnem == "jmp":
                    t2 = target(self.nodes[j].it)
                    k = self.step(j)
                    hit = False
                    while k is not None and self.nodes[k].kind == "label":
                        if self.nodes[k].label == t:
                            hit = True
                            break
                        k = self.step(k)
                    # the jcc takes over the jmp's destination: unless it was
                    # long already, it must still reach it short, measured
                    # with the jmp it replaces gone
                    ok = hit and t2 and t2 in self.labpos and (
                        i in self.longj or not self.ok("near") or
                        self.nodes[self.labpos[t2]].sec == n.sec and
                        self.reach(i, self.off[self.labpos[t2]] -
                                   (self.size[j] if self.off[self.labpos[t2]]
                                    > self.off[i] else 0)))
                    if ok and free(k):
                        cc = m[1:] if self.BREAK_JCCJMP else NEG[m[1:]]
                        self.set_ins(i, "j%s\t%s" % (cc, t2))
                        self.nodes[j].dead = True
                        claim(i, j, k)
                        self.count("jccjmp")
                        made += 1
                        continue
            # jmpnext
            if m == "jmp" and self.ok("jmpnext") and free(i):
                k = self.step(i)
                while k is not None and self.nodes[k].kind == "label":
                    if self.nodes[k].label == t:
                        n.dead = True
                        claim(i, k)
                        self.count("jmpnext")
                        made += 1
                        break
                    k = self.step(k)
        return made

    # -------------------------------------------------------- local rewrites
    def pass_local(self):
        self.liveness()
        made = 0
        touched = set()
        for i in self.order:
            n = self.nodes[i]
            if n.dead or n.kind != "ins" or i in touched:
                continue
            for name in RULES:
                hit = getattr(self, "r_" + name)(i, touched)
                if hit:
                    touched.update(hit)
                    made += 1
                    break
        return made

    def r_setcc(self, i, touched):
        it = self.nodes[i].it
        if not (it.mnem == "mov" and [low(o) for o in it.ops] == ["al", "1"]
                and "cc8086: set" in it.comment):
            return None
        i1 = self.ins_after(i)
        if i1 is None or not is_jcc(self.nodes[i1].it.mnem):
            return None
        cc = self.nodes[i1].it.mnem[1:]
        x = target(self.nodes[i1].it)
        i2 = self.ins_after(i1)
        if i2 is None or not (self.nodes[i2].it.mnem == "mov" and
                              [low(o) for o in self.nodes[i2].it.ops]
                              == ["al", "0"]):
            return None
        xl = self.step(i2)
        if (xl is None or self.nodes[xl].kind != "label"
                or self.nodes[xl].label != x or self.refs.get(x, 0) != 1):
            return None
        i3 = self.step(xl)
        if i3 is None or self.nodes[i3].kind != "ins" or \
                self.nodes[i3].it.mnem != "cbw":
            return None
        i4 = self.ins_after(i3)
        if i4 is None or not self.is_test_ax(i4):
            return None
        i5 = self.ins_after(i4)
        if i5 is None or self.nodes[i5].it.mnem not in ZF_BRANCH:
            return None
        L = target(self.nodes[i5].it)
        seq = (i, i1, i2, xl, i3, i4, i5)
        if not L or L not in self.labpos or cc not in NEG or \
                set(seq) & touched:
            return None
        if self.lout(i5) & (AXM | FB):
            return None
        ncc = cc if self.nodes[i5].it.mnem in ("jne", "jnz") else NEG[cc]
        self.set_ins(i, "j%s\t%s" % (ncc, L))
        for k in seq[1:]:
            self.nodes[k].dead = True
        self.count("setcc")
        return seq

    # ---------------------------------------------------- the 0/1 idiom
    def idiom(self, i):
        """cc8086's lowering of `setCC al / movzx`: (jcc node, mov al,0
        node, label x, cbw node) when node i is the `mov al, 1` that opens
        `mov al,1 / jCC x / mov al,0 / x: cbw` and nothing else names x.
        AX is then exactly 0 or 1 after the cbw."""
        n = self.nodes[i]
        if n.kind != "ins":
            return None
        it = n.it
        if not (it.mnem == "mov" and [low(o) for o in it.ops] == ["al", "1"]
                and "cc8086: set" in it.comment):
            return None
        i1 = self.ins_after(i)
        if i1 is None or not is_jcc(self.nodes[i1].it.mnem) or \
                self.nodes[i1].it.mnem[1:] not in NEG:
            return None
        x = target(self.nodes[i1].it)
        i2 = self.ins_after(i1)
        if i2 is None or not (self.nodes[i2].it.mnem == "mov" and
                              [low(o) for o in self.nodes[i2].it.ops]
                              == ["al", "0"]):
            return None
        xl = self.step(i2)
        if (xl is None or self.nodes[xl].kind != "label"
                or self.nodes[xl].label != x or self.refs.get(x, 0) != 1):
            return None
        i3 = self.step(xl)
        if i3 is None or self.nodes[i3].kind != "ins" or \
                self.nodes[i3].it.mnem != "cbw":
            return None
        return (i1, i2, xl, i3)

    def bool_end(self, k):
        """The `mov al, 1` of the idiom whose cbw is node k, or None."""
        if k is None or self.nodes[k].kind != "ins" or \
                self.nodes[k].it.mnem != "cbw":
            return None
        xl = self.back(k)
        i2 = self.back(xl) if xl is not None else None
        i1 = self.back(i2) if i2 is not None else None
        i0 = self.back(i1) if i1 is not None else None
        if i0 is None:
            return None
        got = self.idiom(i0)
        return i0 if got is not None and got[3] == k else None

    def r_chain(self, i, touched):
        """The setcc shape whose branch goes to ANOTHER test of AX:
        `mov al,1 / jCC x / mov al,0 / x: cbw / test ax,ax / je|jne L`
        where L is `test ax,ax / je|jne M`, an `&&`/`||` that SmallerC
        evaluates to a 0/1 and tests twice. The value reaching L is known
        (0 on a je, 1 on a jne), so is where the second branch sends it: M,
        or the point after it. -> `jCC|jNCC D`, when AX and FLAGS are dead
        at D and on the fallthrough.

        It is `thread` and `setcc` taken as one, for the sites `thread`
        cannot take because D is out of a short jump's reach: the one jump
        left may then be the 5-byte relaxed pair, against the 11 bytes it
        replaces, and every path through it executes fewer instructions."""
        got = self.idiom(i)
        if got is None:
            return None
        i1, i2, xl, i3 = got
        i4 = self.ins_after(i3)
        if i4 is None or not self.is_test_ax(i4):
            return None
        i5 = self.ins_after(i4)
        if i5 is None or self.nodes[i5].it.mnem not in ZF_BRANCH:
            return None
        L = target(self.nodes[i5].it)
        if not L or L not in self.labpos:
            return None
        f = self.first_ins(self.labpos[L])
        if f is None or not self.is_test_ax(f) or \
                self.nodes[f].sec != self.nodes[i].sec:
            return None
        g = self.ins_after(f)
        if g is None or self.nodes[g].it.mnem not in ZF_BRANCH:
            return None
        M = target(self.nodes[g].it)
        if not M or M not in self.labpos:
            return None
        seq = (i, i1, i2, xl, i3, i4, i5)
        if set(seq + (f, g)) & touched:
            return None
        v = 0 if self.nodes[i5].it.mnem in ("je", "jz") else 1
        g_taken = (v == 0) == (self.nodes[g].it.mnem in ("je", "jz"))
        fall = self.eff[i5][2][1]
        gnext = self.eff[g][2][1]
        at = self.labpos[M] if g_taken else gnext
        if fall is None or at is None:
            return None
        if (self.lin[fall] | self.lin[at]) & (AXM | FB):
            return None
        if "chain" in self.BREAK:
            g_taken = not g_taken
        dest = M if g_taken else self.label_after(g)
        cc = self.nodes[i1].it.mnem[1:]
        ncc = cc if v == 1 else NEG[cc]
        self.set_ins(i, "j%s\t%s" % (ncc, dest))
        for k in seq[1:]:
            self.nodes[k].dead = True
        self.count("chain")
        return seq + (f, g)

    def r_renorm(self, i, touched):
        """A 0/1 normalised again: the idiom's cbw followed straight on by
        `test ax,ax / mov al,1 / jne|je y / mov al,0 / y: cbw` - how
        SmallerC materialises the value of an `&&`, an `||` or a `!` whose
        operand is already a comparison. On a 0/1 the jne form is the
        identity on AX (only the `test`'s flags are new, and it stays when
        they are read), and the je form is the comparison inverted."""
        if not self.is_test_ax(i):
            return None
        k = self.ins_before(i)
        i0 = self.bool_end(k)
        if i0 is None:
            return None
        j0 = self.ins_after(i)
        if j0 is None:
            return None
        got = self.idiom(j0)
        if got is None:
            return None
        j1, j2, yl, j3 = got
        jm = self.nodes[j1].it.mnem
        if jm not in ("jne", "jnz", "je", "jz"):
            return None
        i1 = self.idiom(i0)[0]
        seq = (i, j0, j1, j2, yl, j3)
        if set(seq + (i0, i1, k)) & touched:
            return None
        flive = self.lout(j3) & FB
        if jm in ("jne", "jnz") and "renorm" not in self.BREAK:
            for n in seq[1:]:
                self.nodes[n].dead = True
            if not flive:
                self.nodes[i].dead = True
        else:
            if flive:
                return None
            cc = self.nodes[i1].it.mnem[1:]
            self.set_ins(i1, "j%s\t%s" % (NEG[cc],
                                          target(self.nodes[i1].it)))
            for n in seq:
                self.nodes[n].dead = True
        self.count("renorm")
        return seq + (i0, i1, k)

    # SHIFT_CL_MIN: the shortest run of `OP r, 1` taken into `mov cl, N /
    # OP r, cl`. The 8088 is fetch-bound: a run costs ~8 clocks a shift (two
    # bytes at four clocks each), the pair 8 for the `mov` and 8+4N for the
    # shift. At N=3 that is 28 against 24 - two bytes for four clocks - at
    # N=4 it is 32 against 32, and from 5 on the pair is faster. 4 keeps the
    # pass's promise that no path gets slower; 3 is the trade to make on
    # purpose if the bytes are wanted more than the clocks.
    SHIFT_CL_MIN = 4

    def r_shift(self, i, touched):
        """A run of N `shl|sal|shr|sar r, 1` (tools/cc8086.py's lowering of a
        constant shift, since an 8086 has no `shl r, imm`):
          N >= 8, r in AX..DX: the first 8 become a byte move - `mov rh, rl
          / mov rl, 0` (shl), `mov rl, rh / mov rh, 0` (shr), `mov al, ah /
          cbw` (sar ax) - and what is left is a run of N-8 for the next round
          N >= SHIFT_CL_MIN: `mov cl, N / OP r, cl`, when CL is dead after.
        Either way FLAGS must be dead after the run: the byte move writes
        none, and a multi-bit shift leaves OF undefined where a run of single
        shifts defines it."""
        it = self.nodes[i].it
        m = it.mnem
        if m not in ("shl", "sal", "shr", "sar") or len(it.ops) != 2 or \
                low(it.ops[1]) != "1":
            return None
        r = low(it.ops[0])
        if r not in WORDREG:
            return None
        p = self.ins_before(i)
        if p is not None and self.nodes[p].it.mnem == m and \
                [low(o) for o in self.nodes[p].it.ops] == [r, "1"] and \
                p not in touched:
            return None                 # not the head of the run
        run = [i]
        while True:
            j = self.ins_after(run[-1])
            if j is None or j in touched or self.nodes[j].it.mnem != m or \
                    [low(o) for o in self.nodes[j].it.ops] != [r, "1"]:
                break
            run.append(j)
        n = len(run)
        out = self.lout(run[-1])
        if out & FB:
            return None
        if n >= 8 and r in ("ax", "bx", "cx", "dx"):
            lo, hi = r[0] + "l", r[0] + "h"
            if m in ("shl", "sal"):
                code = ("mov\t%s, %s" % (hi, lo), "mov\t%s, 0" % lo)
            elif m == "shr":
                code = ("mov\t%s, %s" % (lo, hi), "mov\t%s, 0" % hi)
            elif r == "ax":
                code = ("mov\tal, ah", "cbw")
            else:
                return None
            self.set_ins(run[0], code[0])
            self.set_ins(run[1], code[1])
            for k in run[2:8]:
                self.nodes[k].dead = True
            self.count("shift")
            return tuple(run)
        if n >= self.SHIFT_CL_MIN and r != "cx" and \
                not out & mask({"cx.l"}):
            self.set_ins(run[0], "mov\tcl, %d" % n)
            self.set_ins(run[1], "%s\t%s, cl" % (m, r))
            for k in run[2:]:
                self.nodes[k].dead = True
            self.count("shift")
            return tuple(run)
        return None

    def r_reload(self, i, touched):
        it = self.nodes[i].it
        if not (it.mnem == "mov" and len(it.ops) == 2
                and cc8086.is_mem(it.ops[0])
                and low(it.ops[1]) in ("ax", "al")):
            return None
        r = low(it.ops[1])
        if mem_reads(it.ops[0], "ax"):
            return None
        j = self.ins_after(i)
        if j is None or j in touched:
            return None
        jt = self.nodes[j].it
        if (jt.mnem == "mov" and len(jt.ops) == 2 and low(jt.ops[0]) == r
                and norm(jt.ops[1]) == norm(it.ops[0])):
            self.nodes[j].dead = True
            self.count("reload")
            return (i, j)
        return None

    def r_copy(self, i, touched):
        it = self.nodes[i].it
        if not (it.mnem == "mov" and len(it.ops) == 2
                and low(it.ops[0]) in WORDREG and low(it.ops[1]) in WORDREG):
            return None
        r1, r2 = low(it.ops[0]), low(it.ops[1])
        if r1 == r2:
            return None
        j = self.ins_after(i)
        if j is None or j in touched:
            return None
        jt = self.nodes[j].it
        if (jt.mnem == "mov" and len(jt.ops) == 2
                and low(jt.ops[1]) == r1 and low(jt.ops[0]) in WORDREG):
            r3 = low(jt.ops[0])
            if r3 == r2:
                self.nodes[j].dead = True
            else:
                self.set_ins(j, "mov\t%s, %s" % (r3, r2))
            self.count("copy")
            return (i, j)
        return None

    def r_commute(self, i, touched):
        """`mov r, ax / mov ax, X / OP ax, r` -> `OP ax, X` for a commutative
        OP, when r is dead after: `a + b` with b already in AX."""
        it = self.nodes[i].it
        if not (it.mnem == "mov" and len(it.ops) == 2
                and low(it.ops[0]) in ("bx", "cx", "dx")
                and low(it.ops[1]) == "ax"):
            return None
        r = low(it.ops[0])
        j = self.ins_after(i)
        if j is None or j in touched:
            return None
        jt = self.nodes[j].it
        if not (jt.mnem == "mov" and len(jt.ops) == 2
                and low(jt.ops[0]) == "ax"):
            return None
        X = jt.ops[1].strip()
        xo = low(X)
        if xo in ("ax", "al", "ah", r) or xo in GPREG and xo not in WORDREG:
            return None
        if cc8086.is_mem(X) and (mem_reads(X, "ax") or mem_reads(X, r)):
            return None
        k = self.ins_after(j)
        if k is None or k in touched:
            return None
        kt = self.nodes[k].it
        if not (kt.mnem in ("add", "and", "or", "xor") and
                [low(o) for o in kt.ops] == ["ax", r]):
            return None
        if self.lout(k) & rmask(r):
            return None
        size = "word " if cc8086.is_mem(X) else ""
        self.set_ins(k, "%s\tax, %s%s" % (kt.mnem, size,
                                         cc8086.strip_size(X)))
        self.nodes[i].dead = True
        self.nodes[j].dead = True
        self.count("commute")
        return (i, j, k)

    def r_popsp(self, i, touched):
        """`sub sp, -2|-4` after a call -> one or two `pop` of a dead
        register: one byte for three, and the pop is the faster of the two
        on an 8088 (8 clocks and one fetched byte against 4 and three)."""
        it = self.nodes[i].it
        if not (it.mnem in ("sub", "add") and len(it.ops) == 2
                and low(it.ops[0]) == "sp"):
            return None
        v = cc8086.imm_value(it.ops[1])
        if v is None:
            return None
        n = -v if it.mnem == "sub" else v
        if n not in (2, 4):
            return None
        out = self.lout(i)
        if out & FB:
            return None
        for r in ("cx", "dx", "bx"):
            if not out & rmask(r):
                break
        else:
            return None
        self.set_ins(i, "pop\t%s" % r)
        if n == 4:
            self.nodes[i].text = self.nodes[i].text + "\n" + \
                self.nodes[i].it.indent + "pop\t%s" % r
        self.count("popsp")
        return (i,)

    def r_test0(self, i, touched):
        it = self.nodes[i].it
        if (it.mnem == "cmp" and len(it.ops) == 2
                and low(it.ops[0]) in WORDREG
                and cc8086.imm_value(it.ops[1]) == 0):
            r = low(it.ops[0])
            self.set_ins(i, "test\t%s, %s" % (r, r))
            self.count("test0")
            return (i,)
        return None

    def r_fold(self, i, touched):
        """`mov ax, M / OP` with AX dead after OP: OP takes M directly."""
        it = self.nodes[i].it
        if not (it.mnem == "mov" and len(it.ops) == 2
                and low(it.ops[0]) == "ax" and cc8086.is_mem(it.ops[1])
                and not mem_reads(it.ops[1], "ax")):
            return None
        M = cc8086.strip_size(it.ops[1])
        j = self.ins_after(i)
        if j is None or j in touched:
            return None
        jt = self.nodes[j].it
        m, ops = jt.mnem, [low(o) for o in jt.ops]
        # A DIRECT address costs a byte more in a modrm than [bp+d8] does,
        # and `mov ax, [abs]` has the accumulator's short form, so against
        # one of those only the shapes that still come out shorter are taken.
        direct = not cc8086.mem_regs(M)
        imm = jt.ops[1].strip() if len(jt.ops) == 2 else ""
        v = cc8086.imm_value(imm) if imm else None
        new = None
        if m == "push" and ops == ["ax"] and not direct:
            new = "push\tword %s" % M
        elif m == "test" and ops == ["ax", "ax"] and not direct:
            new = "cmp\tword %s, 0" % M
        elif m == "cmp" and len(ops) == 2 and ops[0] == "ax" and \
                is_imm(jt.ops[1]) and \
                (not direct or (v is not None and -128 <= v <= 127)):
            new = "cmp\tword %s, %s" % (M, imm)
        elif m == "mov" and len(ops) == 2 and ops[1] == "ax" and \
                ops[0] in WORDREG and ops[0] != "ax":
            new = "mov\t%s, %s" % (ops[0], M)
        elif m in ("add", "sub", "and", "or", "xor", "cmp") and \
                len(ops) == 2 and ops[1] == "ax" and ops[0] in WORDREG \
                and ops[0] != "ax":
            new = "%s\t%s, %s" % (m, ops[0], M)
        if new is None:
            return None
        if self.lout(j) & AXM:
            return None
        self.set_ins(j, new)
        self.nodes[i].dead = True
        self.count("fold")
        return (i, j)

    def r_imm(self, i, touched):
        """`mov ax, imm / mov M, ax|al` with AX dead after: `mov word|byte
        M, imm`."""
        it = self.nodes[i].it
        if not (it.mnem == "mov" and len(it.ops) == 2
                and low(it.ops[0]) == "ax" and is_imm(it.ops[1])):
            return None
        j = self.ins_after(i)
        if j is None or j in touched:
            return None
        jt = self.nodes[j].it
        v = cc8086.imm_value(it.ops[1])
        if (jt.mnem == "mov" and len(jt.ops) == 2 and v is not None
                and cc8086.is_mem(jt.ops[0]) and low(jt.ops[1]) == "al"
                and not mem_reads(jt.ops[0], "ax")):
            # a byte store keeps the low byte alone; `mov byte [m], ib` is
            # shorter than the pair for every addressing form
            if self.lout(j) & AXM:
                return None
            self.set_ins(j, "mov\tbyte %s, %d" % (cc8086.strip_size(jt.ops[0]),
                                                  v & 0xFF))
            self.nodes[i].dead = True
            self.count("imm")
            return (i, j)
        if not (jt.mnem == "mov" and len(jt.ops) == 2
                and cc8086.is_mem(jt.ops[0]) and low(jt.ops[1]) == "ax"
                and not mem_reads(jt.ops[0], "ax")
                and cc8086.mem_regs(jt.ops[0])):
            return None                 # `mov [abs], ax` is the 3-byte A3
        if self.lout(j) & AXM:
            return None
        self.set_ins(j, "mov\tword %s, %s" % (cc8086.strip_size(jt.ops[0]),
                                              it.ops[1].strip()))
        self.nodes[i].dead = True
        self.count("imm")
        return (i, j)

    def code_size(self, code):
        return ins_size(cc8086.Item(0, "\t" + code))

    def r_immcopy(self, i, touched):
        """`mov ax, IMM / mov r, ax` -> `mov r, IMM` when AX is dead after:
        the address of a static on its way to a base register."""
        it = self.nodes[i].it
        brk = "immcopy" in self.BREAK
        if not (it.mnem == "mov" and len(it.ops) == 2
                and low(it.ops[0]) == "ax" and
                (is_imm(it.ops[1]) or brk and cc8086.is_mem(it.ops[1])
                 and not cc8086.mem_regs(it.ops[1]))):
            return None
        j = self.ins_after(i)
        if j is None or j in touched:
            return None
        jt = self.nodes[j].it
        r = low(jt.ops[0]) if len(jt.ops) == 2 else ""
        if not (jt.mnem == "mov" and low(jt.ops[1]) == "ax"
                and r in WORDREG and r != "ax"):
            return None
        if self.lout(j) & AXM:
            return None
        self.set_ins(j, "mov\t%s, %s" % (r, cc8086.strip_size(it.ops[1])
                                         .strip("[]")))
        self.nodes[i].dead = True
        self.count("immcopy")
        return (i, j)

    def r_direct(self, i, touched):
        """`mov R, IMM / ... OP [R+k] ...` -> `OP [IMM+k]`, R in BX/SI/DI,
        when R is dead after its last use and every use reads it only as
        that memory operand's base: a member of a static struct, `s.x`,
        addressed by the 8086's own displacement. Taken only when the bytes
        go DOWN - a `[bx]` is a byte shorter than a `[disp16]`, so two of
        them can cost more than the `mov` saves - and then the path is
        faster too: the `mov` is gone and a direct address costs the EA
        unit 6 clocks against [bx+d8]'s 9 and [bx]'s 5."""
        it = self.nodes[i].it
        if not (it.mnem == "mov" and len(it.ops) == 2
                and low(it.ops[0]) in ("bx", "si", "di")
                and is_imm(it.ops[1])):
            return None
        r = low(it.ops[0])
        rm = rmask(r)
        imm = cc8086.strip_size(it.ops[1])
        names = (r,) + ((r[0] + "l", r[0] + "h") if r == "bx" else ())
        uses, k = [], i
        while True:
            k = self.ins_after(k)
            if k is None or k in touched:
                break
            kt = self.nodes[k].it
            m = kt.mnem
            if m in ("call", "jmp", "ret", "retn", "retf", "iret") or \
                    is_jcc(m):
                break
            u, d, _ = self.eff[k]
            if not u & rm:
                if d & rm:
                    break
                continue
            mops = [x for x, o in enumerate(kt.ops) if cc8086.is_mem(o)]
            if m == "lea" or len(mops) != 1 or \
                    ea_regs(kt.ops[mops[0]]) != [r] or \
                    any(low(cc8086.strip_size(o)) in names
                        for x, o in enumerate(kt.ops) if x != mops[0]):
                return None
            uses.append(k)
            if d & rm:
                break
        if not uses:
            return None
        last = uses[-1]
        if self.lout(last) & rm & ~self.eff[last][1]:
            return None
        new, delta = {}, -ins_size(it)
        for k in uses:
            kt = self.nodes[k].it
            ops = [sub_base(o, r, imm + ("+2" if "direct" in self.BREAK
                                         else ""))
                   if cc8086.is_mem(o) else o.strip() for o in kt.ops]
            new[k] = "%s\t%s" % (kt.mnem, ", ".join(ops))
            delta += self.code_size(new[k]) - ins_size(kt)
        if delta >= 0:
            return None
        for k, code in new.items():
            self.set_ins(k, code)
        self.nodes[i].dead = True
        self.count("direct")
        return (i,) + tuple(uses)

    def r_addimm(self, i, touched):
        """`mov r, IMM / add|sub r, N` (or inc/dec) -> `mov r, IMM+N` when
        FLAGS are dead after: `&s.member`, `&a[3]`, a literal's tail. What
        lets `direct` and `immcopy` see a member of a static."""
        it = self.nodes[i].it
        if not (it.mnem == "mov" and len(it.ops) == 2
                and low(it.ops[0]) in WORDREG and is_imm(it.ops[1])):
            return None
        r = low(it.ops[0])
        j = self.ins_after(i)
        if j is None or j in touched:
            return None
        jt = self.nodes[j].it
        ops = [low(o) for o in jt.ops]
        if jt.mnem in ("inc", "dec") and ops == [r]:
            n = 1 if jt.mnem == "inc" else -1
        elif jt.mnem in ("add", "sub") and len(ops) == 2 and ops[0] == r \
                and cc8086.imm_value(jt.ops[1]) is not None:
            n = cc8086.imm_value(jt.ops[1])
            n = n if jt.mnem == "add" else -n
        else:
            return None
        if self.lout(j) & FB:
            return None
        if "addimm" in self.BREAK:
            n += 1
        self.set_ins(i, "mov\t%s, %s" % (r, imm_add(
            cc8086.strip_size(it.ops[1]), n)))
        self.nodes[j].dead = True
        self.count("addimm")
        return (i, j)

    def r_andid(self, i, touched):
        """An ALU op that cannot change its register: `and r, -1` (what
        `x & 0xFFFF` is on a 16-bit int), `or|xor|add|sub r, 0`, goes when
        FLAGS are dead after; and `and|or r, K` straight after the same
        instruction goes always - the value and the flags are the first's."""
        it = self.nodes[i].it
        m = it.mnem
        if m not in ("and", "or", "xor", "add", "sub") or len(it.ops) != 2:
            return None
        r = low(it.ops[0])
        v = cc8086.imm_value(it.ops[1])
        if r not in GPREG or v is None:
            return None
        p = self.ins_before(i)
        if m in ("and", "or") and p is not None and p not in touched and \
                self.nodes[p].it.mnem == m and \
                [norm(o) for o in self.nodes[p].it.ops] == \
                [norm(o) for o in it.ops]:
            self.nodes[i].dead = True
            self.count("andid")
            return (p, i)
        full = 0xFFFF if r in WORDREG else 0xFF
        ident = (v & full) == full if m == "and" else (v & full) == 0
        if m == "and" and "andid" in self.BREAK:
            ident = True
        if not ident or self.lout(i) & FB:
            return None
        self.nodes[i].dead = True
        self.count("andid")
        return (i,)

    def r_pushst(self, i, touched):
        """A store through an address SmallerC pushed while it evaluated
        the value: `mov ax, A / [add ax, B] / push ax / VALUE / pop bx /
        OP [bx..]` - `p->x = v`, `s.x = v`, `a[i] = v`. The address is
        rebuilt where it is used instead: a constant one goes into OP's
        displacement and the `pop` goes, a pointer local (or a static) is
        loaded into BX where the `pop` was, and the push, the pop and the
        arithmetic in AX all go.

        VALUE must be a straight run that pushes, pops, calls and moves SP
        nothing, and does not write the memory the address is read from: a
        `[bp..]` local is safe from every other store (no C code here can
        take its address, SPEC.md 73.5), a static is safe only from a run
        that writes no memory at all."""
        it = self.nodes[i].it
        if not (it.mnem == "mov" and len(it.ops) == 2
                and low(it.ops[0]) == "ax"):
            return None
        X = cc8086.strip_size(it.ops[1])
        if cc8086.is_mem(it.ops[1]):
            if mem_reads(X, "ax") or set(ea_regs(X)) - {"bp"}:
                return None
            base, disp = X, None
        elif is_imm(it.ops[1]):
            base, disp = None, X
        else:
            return None
        b = self.ins_after(i)
        if b is None or b in touched:
            return None
        bt = self.nodes[b].it
        bops = [low(o) for o in bt.ops]
        if bt.mnem == "push" and bops == ["ax"]:
            c, b = b, None
        else:
            if bt.mnem == "inc" and bops == ["ax"]:
                y = "1"
            elif bt.mnem == "add" and len(bops) == 2 and bops[0] == "ax":
                y = cc8086.strip_size(bt.ops[1])
            else:
                return None
            if cc8086.imm_value(y) is not None:
                disp = imm_add(disp, cc8086.imm_value(y)) if disp else y
            elif is_imm(y) and base is not None and disp is None:
                disp = y
            elif cc8086.is_mem(y) and base is None and \
                    not set(ea_regs(y)) - {"bp"} and not mem_reads(y, "ax"):
                base = y
            else:
                return None
            c = self.ins_after(b)
            if c is None or c in touched or not (
                    self.nodes[c].it.mnem == "push" and
                    [low(o) for o in self.nodes[c].it.ops] == ["ax"]):
                return None
        if self.lout(c) & (AXM | FB):
            return None
        local = base is not None and "bp" in ea_regs(base)
        k, run = c, []
        while True:
            k = self.ins_after(k)
            if k is None or k in touched or len(run) > 48:
                return None
            kt = self.nodes[k].it
            if kt.mnem == "pop":
                break
            u, d, _ = self.eff[k]
            if kt.mnem in ("push", "call", "jmp", "ret", "retn", "retf",
                           "iret", "pushf", "popf", "int") or \
                    is_jcc(kt.mnem) or (u | d) & rmask("sp") or \
                    d & rmask("bp"):
                return None
            writes = kt.ops and cc8086.is_mem(kt.ops[0]) and kt.mnem not in \
                ("cmp", "test", "push")
            if base is not None and writes and \
                    (not local or "bp" in ea_regs(kt.ops[0])):
                return None
            run.append(k)
        e = k
        if [low(o) for o in self.nodes[e].it.ops] != ["bx"]:
            return None
        f = self.ins_after(e)
        if f is None or f in touched:
            return None
        ft = self.nodes[f].it
        mops = [x for x, o in enumerate(ft.ops) if cc8086.is_mem(o)]
        if ft.mnem in ("lea", "call") or len(mops) != 1 or \
                ea_regs(ft.ops[mops[0]]) != ["bx"] or \
                any(low(cc8086.strip_size(o)) in ("bx", "bl", "bh")
                    for x, o in enumerate(ft.ops) if x != mops[0]):
            return None
        if self.lout(f) & BXM & ~self.eff[f][1]:
            return None
        ops = list(ft.ops)
        if base is None:
            ops[mops[0]] = sub_base(ops[mops[0]], "bx", disp)
        elif disp is not None:
            ops[mops[0]] = sub_base(ops[mops[0]], "bx", "bx+" + disp)
        newf = "%s\t%s" % (ft.mnem, ", ".join(o.strip() for o in ops))
        newe = "mov\tbx, %s" % base if base is not None else None
        old = ins_size(it) + 2 + ins_size(ft) + \
            (ins_size(bt) if b is not None else 0)
        new = self.code_size(newf) + (self.code_size(newe) if newe else 0)
        if new >= old:
            return None
        self.set_ins(f, newf)
        if newe and "pushst" in self.BREAK:
            self.set_ins(c, newe)       # BX loaded before the run uses it
            self.nodes[e].dead = True
            c = None
        elif newe:
            self.set_ins(e, newe)
        else:
            self.nodes[e].dead = True
        for n in (i, b, c):
            if n is not None:
                self.nodes[n].dead = True
        self.count("pushst")
        return tuple(n for n in (i, b, c, e, f) if n is not None) + tuple(run)

    def r_index(self, i, touched):
        """Fold an address computed in AX into the [bx] that uses it."""
        it = self.nodes[i].it
        if it.mnem == "mov" and len(it.ops) == 2 and low(it.ops[0]) == "ax" \
                and is_imm(it.ops[1]):
            # mov ax, SYM / add ax, R / mov bx, ax / OP [bx]
            a = self.ins_after(i)
            if a is None or a in touched:
                return None
            at = self.nodes[a].it
            if not (at.mnem == "add" and len(at.ops) == 2
                    and low(at.ops[0]) == "ax"):
                return None
            R = at.ops[1].strip()
            if low(R) == "ax" or low(R) == "sp" or is_imm(R):
                return None
            if cc8086.is_mem(R):
                if mem_reads(R, "ax"):
                    return None
            elif low(R) not in WORDREG:
                return None
            b = self.ins_after(a)
            disp = it.ops[1].strip()
            first = "mov\tbx, %s" % (cc8086.strip_size(R) if cc8086.is_mem(R)
                                    else low(R))
            if low(R) == "bx":
                first = None    # bx already holds R
            kill = (a,)
            head = i
        elif it.mnem == "add" and len(it.ops) == 2 and \
                low(it.ops[0]) == "ax" and is_imm(it.ops[1]):
            # add ax, N / mov bx, ax / OP [bx]
            b = self.ins_after(i)
            disp = it.ops[1].strip()
            first = "mov\tbx, ax"
            kill = ()
            head = i
        else:
            return None
        if b is None or b in touched:
            return None
        bt = self.nodes[b].it
        if not (bt.mnem == "mov" and [low(o) for o in bt.ops] == ["bx", "ax"]):
            return None
        c = self.ins_after(b)
        if c is None or c in touched:
            return None
        ct = self.nodes[c].it
        mops = [k for k, o in enumerate(ct.ops) if cc8086.is_mem(o)]
        if ct.mnem in ("lea", "call") or len(mops) != 1:
            return None
        mo = ct.ops[mops[0]]
        inner = re.search(r"\[([^\]]*)\]", mo)
        if not inner or cc8086.mem_regs(mo) != {"bx"}:
            return None
        cu, cd, cs = self.eff[c]
        if cu & AXM:
            return None                 # OP reads AX: it held the address
        out = self.lout(c)
        if (AXM & ~cd) & out or (BXM & ~cd) & out:
            return None                 # AX / BX would differ after OP
        if self.lin[c] & FB:
            return None                 # OP reads the flags the add made
        newmem = mo[:inner.start(1)] + inner.group(1).strip() + "+" + disp + \
            mo[inner.end(1):]
        ops = list(ct.ops)
        ops[mops[0]] = newmem
        self.set_ins(c, "%s\t%s" % (ct.mnem, ", ".join(o.strip() for o in ops)))
        if first is None:
            self.nodes[head].dead = True
        else:
            self.set_ins(head, first)
        for k in kill + (b,):
            self.nodes[k].dead = True
        self.count("index")
        return (head, b, c) + kill

    def r_spadj(self, i, touched):
        it = self.nodes[i].it
        if not (it.mnem in ("sub", "add") and len(it.ops) == 2
                and low(it.ops[0]) == "sp"):
            return None
        v = cc8086.imm_value(it.ops[1])
        if v is None or not ((it.mnem == "sub" and v < 0) or
                             (it.mnem == "add" and v > 0)):
            return None
        if self.lout(i) & FB:
            return None
        j, hops = self.step(i), 0
        while j is not None and hops < 8:
            nj = self.nodes[j]
            if nj.kind == "label":
                j = self.step(j)
                continue
            if nj.kind != "ins":
                return None
            jt = nj.it
            if jt.mnem == "mov" and [low(o) for o in jt.ops] == ["sp", "bp"]:
                self.nodes[i].dead = True
                self.count("spadj")
                return (i,)
            if jt.mnem == "jmp":
                t = target(jt)
                if t and t in self.labpos:
                    j, hops = self.labpos[t], hops + 1
                    continue
            return None
        return None

    def r_dead(self, i, touched):
        it = self.nodes[i].it
        m, ops = it.mnem, it.ops
        if m == "cbw":
            pure = True
        elif not ops or cc8086.is_mem(ops[0]) or low(ops[0]) not in GPREG:
            pure = False
        elif m == "mov" and len(ops) == 2:
            pure = True
        elif m in ("and", "or", "xor", "add", "sub") and len(ops) == 2:
            pure = True
        elif m in ("inc", "dec", "neg", "not") and len(ops) == 1:
            pure = True
        else:
            pure = False
        if not pure:
            return None
        u, d, _ = self.eff[i]
        out = self.lout(i)
        writes_f = bool(d & FB) or m in ("inc", "dec")
        if not (d & ~FB) or (d & ~FB) & out or (writes_f and out & FB):
            return None
        self.nodes[i].dead = True
        self.count("dead")
        return (i,)

    def pass_bool1(self):
        """The last word on the idiom, once nothing else can take it:
        `mov al,1 / jCC x / mov al,0 / x: cbw` -> `mov ax,1 / jCC x / dec ax
        / x:` when FLAGS are dead after (the `dec` writes them on one path).
        A byte shorter, and an instruction fewer on each path: the taken
        one fetches the same five bytes and executes two instructions for
        three. Run after the fixpoint because every rule above reads the
        idiom in its first spelling."""
        if not self.ok("bool1"):
            return 0
        self.liveness()
        made = 0
        for i in self.order:
            got = self.idiom(i)
            if got is None or self.nodes[i].dead:
                continue
            i1, i2, xl, i3 = got
            if self.lout(i3) & FB:
                continue
            self.set_ins(i, "mov\tax, 1")
            self.set_ins(i2, "inc\tax" if "bool1" in self.BREAK
                         else "dec\tax")
            self.nodes[i3].dead = True
            self.count("bool1")
            made += 1
        return made

    def emit(self):
        self.compact()
        return "\n".join(n.text for n in self.nodes)


# ------------------------------------------------------------ the literal pool
# SmallerC emits every string literal as a block of its own - `section
# .rodata`, an `L<n>:` label, its `db`/`times ... db 0` lines - and never looks
# to see whether the same bytes went out already. C permits sharing them: the
# standard leaves it unspecified whether two identical literals are distinct
# objects, and a literal may not be written. So a literal whose bytes (its NUL
# included) equal an earlier one's, or are the TAIL of a longer one - `"x"`
# inside `"max"`, `": "` at the end of a sentence - is deleted and its label
# becomes `L<n> equ L<host> + <offset>`. Every reference is to the label and
# the label still exists, so the code does not change by a byte; only a block
# whose every line is plain string or numeric `db` data ending in its one NUL
# is touched, so a table SmallerC put in .rodata is left alone.
#
# It was apps/loom/lmpool.py's first job (LOOM -2,795 bytes of resident
# .rodata), written there because that pass could not edit a shared tool.
POOL_LABEL = re.compile(r"^(L\d+):\s*$")
POOL_DB = re.compile(r"^\s*db\s+(.*)$")
POOL_TIMES = re.compile(r"^\s*times\s+(\d+)\s+db\s+(\d+)\s*$")


def pool_bytes(text):
    """The bytes of one `db` operand list, or None if it is not plain."""
    out = bytearray()
    i, n = 0, len(text)
    while i < n:
        while i < n and text[i] in " \t":
            i += 1
        if i >= n:
            return None
        if text[i] == '"':
            j = text.find('"', i + 1)
            if j < 0:
                return None
            out += text[i + 1:j].encode("latin-1")
            i = j + 1
        else:
            m = re.match(r"\d+", text[i:])
            if not m or int(m.group(0)) > 255:
                return None
            out.append(int(m.group(0)))
            i += m.end()
        while i < n and text[i] in " \t":
            i += 1
        if i < n:
            if text[i] != ",":
                return None
            i += 1
    return bytes(out)


def pool_literals(text):
    """(text with duplicate and tail literals folded, literals pooled,
    bytes saved)."""
    lines = text.split("\n")
    found, sec, i = [], None, 0
    while i < len(lines):
        m = SECTION.match(lines[i])
        if m:
            sec = m.group(1).lower()
            i += 1
            continue
        m = POOL_LABEL.match(lines[i])
        if sec == ".rodata" and m:
            data, j, ok = bytearray(), i + 1, True
            while j < len(lines):
                ln = lines[j]
                if not ln.strip():
                    j += 1
                    continue
                d, t = POOL_DB.match(ln), POOL_TIMES.match(ln)
                if t:
                    data += bytes([int(t.group(2))]) * int(t.group(1))
                elif d:
                    b = pool_bytes(d.group(1))
                    if b is None:
                        ok = False
                        break
                    data += b
                else:
                    break
                j += 1
            if ok and data.endswith(b"\0") and data.count(0) == 1:
                found.append((m.group(1), i, j, bytes(data)))
            i = j
            continue
        i += 1
    alias, kept, seen = {}, [], {}
    for lab, _, _, data in sorted(found, key=lambda f: -len(f[3])):
        if data in seen:
            alias[lab] = (seen[data], 0)
            continue
        host = next(((kl, len(kb) - len(data)) for kb, kl in kept
                     if kb.endswith(data)), None)
        if host:
            alias[lab] = host
            continue
        seen[data] = lab
        kept.append((data, lab))
    drop, equ, saved = set(), {}, 0
    for lab, a, b, data in found:
        if lab in alias:
            tgt, off = alias[lab]
            drop.update(range(a, b))
            equ[a] = "%s equ %s%s" % (lab, tgt, " + %d" % off if off else "")
            saved += len(data)
    out = [equ.get(k, ln) for k, ln in enumerate(lines)
           if k in equ or k not in drop]
    return "\n".join(out), len(alias), saved


def peephole(text, only=None):
    """The whole pass over one file of tools/cc8086.py output: returns the
    rewritten text and a {rewrite: count} tally. `only` is a set of rewrite
    names to allow (the bisecting aid); None allows them all."""
    p = Prog(parse(text), void_functions(text))
    if only is not None:
        for name in RULES:
            if name not in only:
                setattr(p, "r_" + name, lambda i, t: None)
        p.flow_ok = set(only)
    rounds = 0
    while True:
        rounds += 1
        a = p.pass_flow()
        b = p.pass_local()
        if not a and not b:
            break
    p.pass_bool1()
    p.counts["rounds"] = rounds
    out = p.emit()
    if only is None or "pool" in only:
        out, n, saved = pool_literals(out)
        if n:
            p.counts["pool"] = n
            p.counts["pool-bytes"] = saved
    return out, p.counts


def main():
    ap = argparse.ArgumentParser(
        description="The C peephole pass over tools/cc8086.py's output.")
    ap.add_argument("input")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--only", default="",
                    help="comma list of rewrites to allow (bisecting aid)")
    args = ap.parse_args()
    text = open(args.input, encoding="utf-8").read()
    out, counts = peephole(text, set(args.only.split(",")) if args.only
                           else None)
    with open(args.output, "w", encoding="utf-8") as f:
        f.write(out)
    print("ccpeep: %s -> %s: %s" % (args.input, args.output, tally(counts)))
    return 0


def tally(counts):
    return ", ".join("%s %d" % kv for kv in sorted(counts.items())) \
        or "nothing"


if __name__ == "__main__":
    sys.exit(main())
