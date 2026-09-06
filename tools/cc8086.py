#!/usr/bin/env python3
"""cc8086 - lower SmallerC's NASM to a strict 8086, or refuse to assemble it.

    python3 tools/cc8086.py IN.asm -o OUT.asm [--max-frame N]

IN.asm is what `smlrcc -tiny -S` wrote.  OUT.asm is the same program in
instructions an 8088 can actually execute, and is not written at all if
anything below fails.  Every failure is `file:line: error: ...` on stderr and
exit 1, because the alternative - a package that assembles and misbehaves on
one machine in three - is the bug class this whole repo is arranged against.

Two jobs, and the second is the important one.


LOWERING
--------
SmallerC targets an 80386 even in 16-bit mode.  Measured over its own srclib -
201 files, 198 of which compile, 48,466 lines of emitted assembly - it reaches
past the 8086 at 481 sites, about 1% of instructions, in exactly seven forms:

    setcc r8            200      imul r16, r16, imm     15
    leave               141      sar/shl/shr r, imm>1    2
    push imm            120      movzx r16, r/m8         2
                                 movsx r16, r/m8         1

All 481 are lowered below.  A further 91 sites in the same corpus have no
lowering because the fix belongs in the C - 69 bp-relative `lea`, 18 string
operations through ES, 4 references to a 32-bit register - and those are the
gate, further down.

A rewrite is only worth anything if it is provably right about what it
clobbers, so each one below says what it costs and how that was established.

  `leave`  ->  `mov sp, bp` / `pop bp`.  Unconditionally safe: neither MOV nor
    POP writes FLAGS, and `leave` already defines both SP and BP.

  `setcc r8`  ->  `mov r8, 1` / `j<cc> .1` / `mov r8, 0` / `.1:`.  The obvious
    lowering is a branch around a `mov`, and the obvious lowering is wrong to
    reach for blind: `setcc` does not touch FLAGS and a branch-shaped rewrite
    normally does.  This one does not either - MOV, Jcc and JMP all leave
    FLAGS exactly as they found them on an 8086 - so it needs no liveness
    argument at all.  That matters: FLAGS is only provably dead at 111 of the
    200 sites by a linear scan, because 89 of them are immediately followed by
    a label and a second predecessor decides nothing.  A lowering that costs
    nothing is better than a proof that does not close.

  `push imm`  ->  `mov <scratch>, imm` / `push <scratch>`.  The 8086 has no
    push-immediate, and SP is not a legal base register in an 8086 effective
    address, so a scratch register is unavoidable and the only question is
    which one is dead.  Censused over the corpus with the very `dead_after`
    below: AX is dead at **120 of 120** sites, and it is the only register
    that is (BX 119, CX 115, DX 115, SI 113, DI 113).  It is killed within
    three instructions every time - one instruction at 67 sites, two at 50,
    three at 3 - by a `call` at 83 sites (the ABI has no callee-saved
    register and returns in AX, so a call ends AX's life), by `mov ax, ...`
    at 36, by `lea ax, ...` at 1.  That is the shape of the code: SmallerC
    evaluates each argument into AX and pushes it, so at an argument push the
    previous argument is already on the stack.
    So AX is preferred, CX/DX/BX/SI/DI are tried in turn, and a site where
    none of them is PROVABLY dead is refused rather than guessed at.  MOV and
    PUSH do not write FLAGS, so this costs one register and nothing else.
    (The 120 is larger than the 105 a grep for `push <digit>` finds: 15 of
    them push a symbol, `push _table`, which is just as much an immediate and
    just as illegal.)

  `shl/shr/sar/rol/ror/rcl/rcr r/m, N`  ->  N copies of the 1-bit form.  Exact,
    including through the carry, and it needs no scratch and no liveness
    claim.  The only flag that differs is OF, which the multi-bit form leaves
    undefined for N != 1 anyway.

  `movzx r16, r/m8`  ->  `mov <lo>, src` / `mov <hi>, 0`, low half first so
    that a source addressed through the destination (`movzx bx, byte [bx]`)
    still reads the old value.  No FLAGS, no scratch.  SI/DI/BP have no
    addressable halves, so a widening into one of those stages through AX.

  `movsx r16, r/m8`  ->  `mov al, src` / `cbw` / `mov r16, ax`, collapsing to
    a bare `cbw` when it is already AX <- AL.  CBW writes no flags either, so
    the only cost is AX.

    BOTH WIDENINGS BORROW AX AND GIVE IT BACK when it is not dead: the whole
    sequence is bracketed `push ax` ... `pop ax`, which is four bytes of
    ordinary 8086, writes no flags, and cannot disturb the source because SP
    is not a legal base register in an 8086 address.  Without it
    `char a[200]; x = a[0] + a[199];` is refused - AX holds the left operand
    while the right one is widened - and that is not exotic C.  The bracket
    is emitted only when the scan cannot prove AX free, so nothing pays for
    it that does not need it.

  `imul r16, src, imm`  ->  a shift-and-add chain: one scratch holds the
    multiplicand, the destination accumulates `src << b` for each set bit b.
    `imul ax, ax, 2` becomes a single `shl ax, 1` and needs no scratch at all,
    which covers 9 of the 15 corpus sites; the rest are x3 (2) and x10 (4).
    This is the one lowering that CANNOT preserve FLAGS - ADD and SHL define
    them - so unlike `setcc` it does carry a liveness obligation, and it
    refuses the site unless FLAGS is provably dead after it.  It is also the
    right shape for the target: `mul` is 118-133 cycles on an 8088 and a
    shift is 2.

    That obligation is the reason `flags_dead_after` had to be got right
    rather than merely made safe.  Stopping the scan at a label refused
    `int f(int i) { return i * 10; }` outright, because SmallerC ends every
    function `L<n>: / leave / ret` - see the note above the two scans.

Every liveness question above is asked of the ORIGINAL instruction stream,
which stays correct after rewriting because no lowering changes the control
flow outside itself: the `setcc` branch and its label begin and end between
the same two instructions the `setcc` did, so the set of paths reaching any
later use is unchanged.

Everything the lowering leaves behind is then checked again, and OUT.asm opens
with `cpu 8086`, so NASM is the last word rather than this file's opinion.

All of it is differentially tested rather than argued: each lowering and the
instruction it replaces are executed from bit-identical machine state on a
real x86 under QEMU and the whole register file and FLAGS compared - 428
cases over every `setcc` spelling, seven shift and rotate mnemonics, both
widenings with and without the AX bracket, thirteen multipliers and the
push-immediate scratch search, with four deliberately broken lowerings as a
negative control to prove the comparison can fail.  The AX-bracket cases mask
nothing off: AX and SP are compared like every other register, because the
whole claim about `push ax` ... `pop ax` is that the borrow is invisible.


THE GATE
--------
os8088 runs every task with **SS = LOW_SEG != DS** (CLAUDE.md, SPEC.md 33) and
enters every package callback with **ES = KERNEL_SEG** (SPEC.md 20).  Neither
is negotiable here, and both are invisible to a C compiler that believes it
owns the machine.  So four constructs are refused outright:

  * **any bp-relative `lea`** - this is `&local`, and it is the single most
    important rule in this file.  SmallerC addresses automatics as `[bp+N]`,
    which goes through SS; `lea ax, [bp-4]` hands that offset to code that
    will dereference it through DS, and the two are different segments.  It
    assembles, it runs, and it reads or writes somebody else's memory.  There
    is no lowering for it - the fix is in the C - so every addressable buffer
    must be static.  69 sites in the corpus, every one of them real.

  * **`movs`/`stos`/`scas`/`cmps`, with or without `rep`** - all four touch
    ES:DI, and ES is the KERNEL's segment.  `rep stosb` does not fail, it
    overwrites the kernel.  This is also how SmallerC copies a struct by
    value: it emits a helper that does `rep movsb` from a `lea`'d local, so
    one C assignment trips both of the first two rules at once.  `lods` reads
    DS:SI only and is allowed.

  * **any float** - `float` compiles under SmallerC and is silently 2 bytes
    wide, calling `___addsf3` and friends that are not on this floppy.  Two
    bytes of "float" is not a diagnostic anybody wants to chase.

  * **any 32-bit register** - `cpu 8086` would catch it; naming it here is
    worth more than the error NASM gives.

An inline-asm site that has genuinely earned an exception says so on the line,
`; cc8086:allow <reason>`, which is greppable and reviewable.  Nothing else
gets past.


THE REPORT
----------
Every function's frame size, from its `sub sp, N`, because the budget is not
generous and is not visible from the C.  A worker task gets a **384-byte**
stack (kernel/sched.inc, SCH_STACK) and the UI task has of the order of 700
bytes of headroom, so a single 128-byte C automatic is most of a worker's
stack and nothing says so at the call site.  `--max-frame` (default 96) fails
the build on one; `--quiet` prints only the offenders.
"""
import argparse
import re
import sys

# ---------------------------------------------------------------- registers

WORD_REGS = ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp")
BYTE_OF = {"al": ("ax", "lo"), "ah": ("ax", "hi"),
           "bl": ("bx", "lo"), "bh": ("bx", "hi"),
           "cl": ("cx", "lo"), "ch": ("cx", "hi"),
           "dl": ("dx", "lo"), "dh": ("dx", "hi")}
HALVES = {"ax": ("al", "ah"), "bx": ("bl", "bh"),
          "cx": ("cl", "ch"), "dx": ("dl", "dh")}
SEG_REGS = ("cs", "ds", "es", "ss")
# Order matters: AX first because the census says AX is the register that is
# actually dead at these sites, and `mov ax, imm` is the shortest encoding.
SCRATCH_ORDER = ("ax", "cx", "dx", "bx", "si", "di")

REG32 = re.compile(r"\b(e(?:ax|bx|cx|dx|si|di|bp|sp)|r(?:8|9|1[0-5])[dwb]?)\b",
                   re.I)

# SmallerC prefixes every C identifier with `_`, so the C name `__addsf3`
# reaches the assembly as `___addsf3`.  Both spellings are refused; the bare
# one also appears in the identifier-table COMMENT at the foot of every single
# file it emits, which is why the gate only ever looks at code.
FLOAT_HELPERS = ("__addsf3", "__subsf3", "__mulsf3", "__divsf3", "__negsf2",
                 "__lesf2", "__gesf2", "__eqsf2", "__nesf2", "__ltsf2",
                 "__gtsf2", "__floatsisf", "__floatunsisf", "__fixsfsi",
                 "__fixunssfsi", "__extendsfdf2", "__truncdfsf2")

# ES:DI, every one of them.  `lods` is DS:SI and is deliberately absent.
ES_STRING_OPS = ("movsb", "movsw", "stosb", "stosw", "scasb", "scasw",
                 "cmpsb", "cmpsw", "movs", "stos", "scas", "cmps")
REP_PREFIXES = ("rep", "repe", "repz", "repne", "repnz")

CC = ("o", "no", "b", "nae", "c", "nb", "ae", "nc", "z", "e", "nz", "ne",
      "be", "na", "a", "nbe", "s", "ns", "p", "pe", "np", "po",
      "l", "nge", "ge", "nl", "le", "ng", "g", "nle")
SETCC = re.compile(r"^set(" + "|".join(CC) + r")$")
JCC = re.compile(r"^j(" + "|".join(CC) + r")$")

SHIFTS = ("shl", "shr", "sar", "sal", "rol", "ror", "rcl", "rcr")
ARITH = ("add", "sub", "adc", "sbb", "and", "or", "xor")

# `flags_dead_after` may only answer YES on an instruction that leaves NO flag
# holding its old value, so this set is exactly the instructions that define
# all six.  An instruction that defines some of them is not here: it goes in
# FLAG_TRANSPARENT instead, where it neither ends the scan nor answers it.
# Getting that distinction wrong produces a wrong program rather than a
# refusal, so the membership of each one is stated:
#
#   * NOT and CBW write no flags at all.
#   * INC and DEC DO NOT WRITE CF - that is the whole reason they exist beside
#     ADD/SUB - so `cmp` / `jc` with an `inc` between them still reads the
#     `cmp`'s carry.  They were in this set and it was a latent bug; with the
#     label fix below the scan reaches far enough for it to have fired.
#   * ROL/ROR/RCL/RCR write CF and OF and nothing else, so SF/ZF/AF/PF survive
#     a rotate.  Only the true shifts kill everything, and only for a non-zero
#     count - see `flags_killed`.
#   * CLC/STC/CMC write CF alone; SAHF writes all but OF.
#   * MUL/DIV and the BCD adjusts leave several flags "undefined", which on an
#     8086 means written with something rather than preserved, so no earlier
#     value survives one.  That is what this predicate asks.
FLAG_WRITERS = set(ARITH) | {
    "cmp", "test", "neg", "mul", "imul", "div", "idiv",
    "popf", "aaa", "aas", "aam", "aad", "daa", "das"}

# The true shifts: everything defined, but only when the count is not zero.
# `shl r, cl` with CL = 0 leaves FLAGS EXACTLY as it found them on an 8086.
FULL_SHIFTS = ("shl", "sal", "shr", "sar")

# Instructions that end no scan: either they touch no flag, or they touch SOME
# and leave the rest, which can only shrink what is live.  Walking past one and
# asking the next instruction is always at least as conservative as stopping.
# Anything absent from here AND from FLAG_WRITERS stops the scan with "not
# provably dead", so a mnemonic this file has never seen refuses rather than
# guesses.
FLAG_TRANSPARENT = {
    "mov", "movzx", "movsx", "push", "pop", "lea", "xchg", "cbw", "cwd",
    "not", "nop", "cld", "std", "cli", "sti", "in", "out", "les", "lds",
    "lodsb", "lodsw", "lods", "xlat", "xlatb", "wait", "hlt", "lock",
    "inc", "dec", "clc", "stc", "cmc", "sahf",
    # `leave` and `enter` write no flags, and BOTH SCANS RUN OVER THE INPUT -
    # the 80186 forms are still there when the question is asked, so leaving
    # `leave` out of this set is what refused `return i * 10;`: SmallerC's
    # epilogue is `L<n>: / leave / ret` and the scan died on the second line
    # of it even after the label stopped stopping it.
    "leave", "enter",
} | set(SHIFTS)

FLAG_READERS = {"adc", "sbb", "rcl", "rcr", "pushf", "lahf", "cmc", "into",
                "salc", "daa", "das", "aaa", "aas"}

# The 8086's whole instruction set, for the closing sweep.  A mnemonic absent
# from this is refused by name; a mnemonic present in it can still be refused
# for its operand form (`push imm`, `shl r, 3`).
I8086 = {
    "aaa", "aad", "aam", "aas", "adc", "add", "and", "call", "cbw", "clc",
    "cld", "cli", "cmc", "cmp", "cmpsb", "cmpsw", "cmps", "cwd", "daa", "das",
    "dec", "div", "esc", "hlt", "idiv", "imul", "in", "inc", "int", "int3",
    "into", "iret", "ja", "jae", "jb", "jbe", "jc", "jcxz", "je", "jg", "jge",
    "jl", "jle", "jmp", "jna", "jnae", "jnb", "jnbe", "jnc", "jne", "jng",
    "jnge", "jnl", "jnle", "jno", "jnp", "jns", "jnz", "jo", "jp", "jpe",
    "jpo", "js", "jz", "lahf", "lds", "lea", "les", "lock", "lodsb", "lodsw",
    "lods", "loop", "loope", "loopne", "loopnz", "loopz", "mov", "movsb",
    "movsw", "movs", "mul", "neg", "nop", "not", "or", "out", "pop", "popf",
    "push", "pushf", "rcl", "rcr", "rep", "repe", "repne", "repnz", "repz",
    "ret", "retf", "retn", "rol", "ror", "sahf", "sal", "sar", "sbb", "scasb",
    "scasw", "scas", "shl", "shr", "stc", "std", "sti", "stosb", "stosw",
    "stos", "sub", "test", "wait", "xchg", "xlat", "xlatb", "xor",
}

DIRECTIVES = {
    "section", "segment", "bits", "cpu", "global", "extern", "common",
    "db", "dw", "dd", "dq", "dt", "resb", "resw", "resd", "resq",
    "times", "align", "alignb", "equ", "org", "use16", "default", "absolute",
    "struc", "endstruc", "istruc", "at", "iend", "incbin",
}

ALLOW = re.compile(r"\bcc8086\s*:\s*allow\b", re.I)


class Refuse(Exception):
    """A site this cannot rewrite correctly.  Never a silent fallback."""


# ------------------------------------------------------------------ parsing

def split_comment(line):
    """(code, comment) for one source line, honouring quotes.

    `db "a;b"` is real in SmallerC's string pool, so a naive split on `;`
    would truncate data and - worse - hide a `;`-quoted instruction from the
    gate.  The gate only ever reads `code`.
    """
    quote = None
    for i, c in enumerate(line):
        if quote:
            if c == quote:
                quote = None
        elif c in "\"'":
            quote = c
        elif c == ";":
            return line[:i], line[i:]
    return line, ""


def split_operands(text):
    """Top-level comma split; brackets and quotes hold their commas."""
    out, depth, quote, cur = [], 0, None, ""
    for c in text:
        if quote:
            cur += c
            if c == quote:
                quote = None
            continue
        if c in "\"'":
            quote = c
        elif c == "[":
            depth += 1
        elif c == "]":
            depth -= 1
        elif c == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
            continue
        cur += c
    if cur.strip():
        out.append(cur.strip())
    return out


LABEL = re.compile(r"^\s*([A-Za-z_.?$][\w.?$@#~]*)\s*:\s*(.*)$")


class Item:
    """One source line, plus whatever it turned into."""

    __slots__ = ("no", "raw", "code", "comment", "indent", "label", "kind",
                 "mnem", "ops", "out", "allow")

    def __init__(self, no, raw):
        self.no = no
        self.raw = raw
        self.out = None                     # None => emit `raw` unchanged
        code, self.comment = split_comment(raw)
        self.allow = bool(ALLOW.search(self.comment))
        self.indent = code[:len(code) - len(code.lstrip())] or "\t"
        self.label = None
        m = LABEL.match(code)
        if m and not m.group(2).strip().startswith(":"):
            self.label, code = m.group(1), m.group(2)
        code = self.code = code.strip()
        self.mnem, self.ops = None, []
        if not code:
            self.kind = "label" if self.label else "blank"
            return
        head = code.split(None, 1)
        word = head[0].lower()
        rest = head[1] if len(head) > 1 else ""
        # `foo equ 1` and `foo: db 0` both put the directive in word two.
        if word in DIRECTIVES or word.startswith("%") or word.startswith("["):
            self.kind = "dir"
            self.mnem = word
            return
        self.kind = "ins"
        self.mnem = word
        self.ops = split_operands(rest)


# ------------------------------------------------------------- operand kinds

def reg16(op):
    o = op.strip().lower()
    return o if o in WORD_REGS else None


def reg8(op):
    o = op.strip().lower()
    return o if o in BYTE_OF else None


def is_reg(op):
    o = op.strip().lower()
    return o in WORD_REGS or o in BYTE_OF or o in SEG_REGS


def is_mem(op):
    return "[" in op


def mem_regs(op):
    """The 16-bit registers an effective address reads."""
    out = set()
    for m in re.finditer(r"\[([^\]]*)\]", op):
        for t in re.findall(r"[a-z]{2}", m.group(1).lower()):
            if t in WORD_REGS:
                out.add(t)
    return out


def op_regs(op):
    """Every 16-bit register an operand reads, memory or not."""
    if is_mem(op):
        return mem_regs(op)
    o = op.strip().lower()
    if o in WORD_REGS:
        return {o}
    if o in BYTE_OF:
        return {BYTE_OF[o][0]}
    return set()


# Liveness is tracked per HALF register - `ax.l` and `ax.h` - not per register.
# That is not fussiness: SmallerC's own strstr does
#
#       movsx cx, byte [bx]  /  pop bx  /  mov al, [bx]  /  cbw  /  cmp ax, cx
#
# and AX really is dead across the `movsx` - AL is overwritten by the `mov`
# and AH by the `cbw` that follows it - but a whole-register model sees the
# 8-bit write as a partial one, calls AX live, and refuses a site that is
# perfectly safe.  Halves cost nothing and turn a false refusal into a
# lowering.
def units(reg):
    return {reg + ".l", reg + ".h"}


ALL_UNITS = set().union(*(units(r) for r in WORD_REGS))


def op_units(op):
    """The half-registers an operand READS."""
    if is_mem(op):
        return set().union(*(units(r) for r in mem_regs(op))) \
            if mem_regs(op) else set()
    o = strip_size(op).lower()
    if o in WORD_REGS:
        return units(o)
    if o in BYTE_OF:
        parent, half = BYTE_OF[o]
        return {parent + ("." + ("l" if half == "lo" else "h"))}
    return set()


def strip_size(op):
    return re.sub(r"^\s*(byte|word|dword|near|far|short)\s+", "", op,
                  flags=re.I).strip()


IMM_LITERAL = re.compile(r"^[-+]?(0[xX][0-9a-fA-F]+|0[bB][01]+|\d+[hH]?|"
                         r"[0-9a-fA-F]+[hH])$")


def imm_value(op):
    """The integer an operand denotes, or None if it is not a plain literal."""
    t = strip_size(op).replace(" ", "")
    if not IMM_LITERAL.match(t):
        return None
    sign = 1
    if t[0] in "+-":
        sign = -1 if t[0] == "-" else 1
        t = t[1:]
    try:
        if t.lower().startswith("0x"):
            return sign * int(t[2:], 16)
        if t.lower().startswith("0b"):
            return sign * int(t[2:], 2)
        if t[-1] in "hH":
            return sign * int(t[:-1], 16)
        return sign * int(t, 10)
    except ValueError:
        return None


# --------------------------------------------------------------- def and use
#
# Deliberately coarse, and coarse in ONE direction: anything not understood
# reads every register, so `dead_after` answers "not provably dead" and the
# caller refuses the site.  Being wrong here is a wrong program, so the model
# is written to be wrong only towards refusal.

def def_use(it):
    """(uses, defs, terminator) for one instruction, in HALF registers.

    Reading a half that was never written is what makes a register live; an
    8-bit destination therefore defines one half and leaves the other alone,
    which is the whole point of the split.
    """
    m, ops = it.mnem, it.ops
    u, d = set(), set()

    def dest(op):
        if is_mem(op):
            u.update(op_units(op))
        else:
            d.update(op_units(op))

    def src(op):
        u.update(op_units(op))

    def rmw(op):
        if is_mem(op):
            u.update(op_units(op))
        else:
            src(op)
            dest(op)

    if m in ("mov", "movzx", "movsx"):
        # movzx/movsx write the WHOLE destination even from a byte source.
        if m in ("movzx", "movsx") and reg16(ops[0]):
            d.update(units(reg16(ops[0])))
        else:
            dest(ops[0])
        src(ops[1])
        return u, d, False
    if m == "lea":
        dest(ops[0])
        u.update(op_units(ops[1]))
        return u, d, False
    if m == "push":
        src(ops[0])
        return u, d, False
    if m == "pop":
        dest(ops[0])
        return u, d, False
    if m == "xchg":
        for o in ops:
            src(o)
            dest(o)
        return u, d, False
    if m in ("cmp", "test"):
        for o in ops:
            src(o)
        return u, d, False
    if m in ARITH or m in SHIFTS:
        # `xor r, r` and `sub r, r` are pure definitions, and saying so is
        # what lets a scratch search see past SmallerC's zeroing idiom.
        if (len(ops) == 2 and m in ("xor", "sub")
                and not is_mem(ops[0])
                and ops[0].strip().lower() == ops[1].strip().lower()):
            dest(ops[0])
            return set(), d, False
        rmw(ops[0])
        if len(ops) > 1:
            src(ops[1])
        return u, d, False
    if m in ("inc", "dec", "neg", "not"):
        rmw(ops[0])
        return u, d, False
    if m == "cbw":
        return {"ax.l"}, units("ax"), False
    if m == "cwd":
        return units("ax"), units("dx"), False
    if m == "imul" and len(ops) == 3:
        src(ops[1])
        d.update(units(reg16(ops[0])) if reg16(ops[0]) else set())
        return u, d, False
    if m in ("mul", "imul", "div", "idiv"):
        src(ops[0])
        return u | units("ax") | units("dx"), units("ax") | units("dx"), False
    if SETCC.match(m):
        dest(ops[0])
        return u, d, False
    if m == "call":
        # The SmallerC ABI passes every argument on the stack, returns in AX
        # and preserves NO register, so a call ends the life of all six
        # scratch registers.  This is the one place the model trusts the ABI
        # rather than the instruction, and it is what proves AX dead at 68 of
        # the 105 push-immediate sites.
        if ops:
            src(ops[0])
        d = set().union(*(units(r) for r in ("ax", "bx", "cx", "dx", "si",
                                             "di")))
        return u, d, False
    if m in ("ret", "retn", "retf", "iret"):
        # Nothing falls through, so everything but the return value is dead.
        return units("ax"), ALL_UNITS - units("ax"), True
    if m == "leave":
        return units("bp"), units("sp") | units("bp"), False
    if m in ("cld", "std", "cli", "sti", "nop", "clc", "stc", "cmc", "wait",
             "lock", "hlt", "pushf", "popf", "sahf", "lahf"):
        return set(), set(), False
    if m in ("lodsb", "lodsw", "lods"):
        return units("si") | units("ax"), units("si") | units("ax"), False
    if m in ES_STRING_OPS or m in REP_PREFIXES:
        live = units("ax") | units("cx") | units("si") | units("di")
        return live, units("cx") | units("si") | units("di"), False
    if m in ("xlat", "xlatb"):
        return units("ax") | units("bx"), units("ax"), False
    # jmp / jcc / loop / int / unknown: no useful fall-through knowledge.
    return ALL_UNITS, set(), True


# WHY BOTH SCANS WALK STRAIGHT PAST A LABEL
# -----------------------------------------
# They used to stop at one, answering "not provably dead", and that single
# line refused ordinary C: SmallerC ends every function `L<n>: / leave / ret`,
# so `int f(int i) { return i * 10; }` reached the label one instruction after
# the `imul` and was rejected with advice that did not work either.  Measured
# over four small files of ordinary C: eleven of fourteen constant-multiply
# sites refused before, one after - and that one is an honest refusal for a
# different reason (no scratch register is provably free before a branch).
#
# It was also unnecessary.  Liveness at a point is a question about the paths
# OUT of that point, and a label does not add any: it adds INCOMING edges, and
# what other predecessors do before arriving cannot change what the
# instructions after the label go on to read.  The one path out of flow[pos]
# is the fall-through into the label, and these scans follow exactly that
# path, stopping at the first branch because a branch really does fork it.  A
# loop is covered by the same rule from the other end - the backward `jcc`
# that closes it stops the scan.
#
# A DIRECTIVE still stops both scans.  `section`, `equ`, `times` and the rest
# can put anything at all between two instructions, and unlike a label they
# are not merely a name for the point in between.

def dead_after(flow, pos, reg):
    """Is every half of `reg` provably dead immediately after flow[pos]?

    A conservative linear walk.  It stops - answering no - at a directive, a
    branch or anything it does not model, because an unknown target could keep
    the register alive.  Over the corpus this proves AX dead at every
    push-immediate site, which is the whole reason a linear walk is enough.
    """
    unknown = units(reg)
    for it in flow[pos + 1:]:
        if it.kind == "label":
            continue                    # see the note above
        if it.kind != "ins":
            return False
        u, d, term = def_use(it)
        if unknown & u:
            return False
        unknown -= d
        if not unknown:
            return True
        if term:
            return False
    return False


def flags_killed(it):
    """Does this instruction leave NO flag holding the value it had?

    Not "does it write flags": a partial writer leaves the rest alive, and
    answering yes on one is how a `jc` ends up reading a carry the lowering
    clobbered.  See the note on FLAG_WRITERS.
    """
    if it.mnem in FULL_SHIFTS and len(it.ops) == 2:
        n = imm_value(it.ops[1])
        # A count of zero writes nothing at all; a count this file cannot read
        # (`shl ax, cl`) might be zero, so neither answers the question.
        return n is not None and n != 0
    return it.mnem in FLAG_WRITERS


def flags_dead_after(flow, pos):
    """Is FLAGS provably dead immediately after flow[pos]?

    Only `imul r,r,imm` needs this - every other lowering here leaves FLAGS
    alone by construction.
    """
    for it in flow[pos + 1:]:
        if it.kind == "label":
            continue                    # see the note above
        if it.kind != "ins":
            return False
        m = it.mnem
        if SETCC.match(m) or JCC.match(m) or m in FLAG_READERS:
            return False
        if m == "call" or flags_killed(it):
            return True
        # A `ret` is where this stops being a claim about the 8086 and starts
        # being a claim about THIS TOOL'S INPUT: compiled C, whose whole ABI
        # answers in AX and never in a flag, so no caller can observe the
        # FLAGS a callee returns.  It would be wrong for a hand-written proc
        # that answers in CF - and this file never sees one, because a package
        # author's assembly is not compiled through it.
        if m in ("ret", "retn", "retf", "iret"):
            return True
        if m in ("jmp", "loop", "loope", "loopne", "loopnz", "loopz",
                 "jcxz", "int", "int3") or m in REP_PREFIXES:
            return False
        if m not in FLAG_TRANSPARENT:
            return False                # never seen it; do not guess
    return False


def find_scratch(flow, pos, forbid):
    for r in SCRATCH_ORDER:
        if r in forbid:
            continue
        if dead_after(flow, pos, r):
            return r
    return None


# ---------------------------------------------------------------- lowering

class Lowerer:
    def __init__(self, flow):
        self.flow = flow
        self.n = 0
        self.counts = {}

    def label(self):
        self.n += 1
        # `..@` is NASM's non-local, non-global namespace: it cannot end a
        # local-label scope and cannot collide with a C identifier, which
        # SmallerC always emits with a leading `_`.
        return "..@cc8086_%d" % self.n

    def bump(self, key):
        self.counts[key] = self.counts.get(key, 0) + 1

    def lower(self, it, pos):
        """The replacement instruction list, or None to leave the line be."""
        m, ops = it.mnem, it.ops

        if m == "leave":
            if ops:
                raise Refuse("`leave` takes no operand")
            self.bump("leave")
            return ["mov\tsp, bp", "pop\tbp"]

        if m == "push" and len(ops) == 1 and not is_mem(ops[0]) \
                and not is_reg(strip_size(ops[0])):
            return self.push_imm(it, pos)

        if SETCC.match(m):
            return self.setcc(it)

        if m in SHIFTS and len(ops) == 2:
            n = imm_value(ops[1])
            if n is not None and n != 1:
                return self.shift(it, n)
            return None

        if m == "movzx":
            return self.movzx(it, pos)

        if m == "movsx":
            return self.movsx(it, pos)

        if m == "imul" and len(ops) in (2, 3):
            return self.imul(it, pos)

        return None

    # -- push imm -----------------------------------------------------------
    def push_imm(self, it, pos):
        imm = strip_size(it.ops[0])
        r = find_scratch(self.flow, pos, forbid=())
        if r is None:
            raise Refuse(
                "`push %s`: the 8086 has no push-immediate and SP is not a "
                "legal base register, so this needs a scratch register - and "
                "none of AX/CX/DX/BX/SI/DI is provably dead here. Refusing to "
                "guess. Simplify the call (fewer or simpler arguments) or "
                "hoist the constant into a variable." % imm)
        self.bump("push-imm")
        return ["mov\t%s, %s" % (r, imm), "push\t%s" % r]

    # -- setcc --------------------------------------------------------------
    def setcc(self, it):
        if len(it.ops) != 1:
            raise Refuse("`%s` takes one operand" % it.mnem)
        cc = SETCC.match(it.mnem).group(1)
        dst = it.ops[0].strip()
        if is_mem(dst):
            if not re.match(r"^\s*byte\b", dst, re.I):
                dst = "byte " + dst
        elif not reg8(dst):
            raise Refuse("`%s %s`: destination is not an 8-bit register or "
                         "byte memory" % (it.mnem, dst))
        end = self.label()
        self.bump("setcc")
        # MOV, Jcc and JMP write no flags on an 8086, so this is a drop-in for
        # an instruction that writes none either.
        return ["mov\t%s, 1" % dst,
                "j%s\t%s" % (cc, end),
                "mov\t%s, 0" % dst,
                "%s:" % end]

    # -- multi-bit shifts ---------------------------------------------------
    def shift(self, it, n):
        dst = it.ops[0].strip()
        if n < 0 or n > 31:
            raise Refuse("`%s %s, %d`: shift count out of range"
                         % (it.mnem, dst, n))
        self.bump("shift-imm")
        if n == 0:
            return ["; cc8086: dropped `%s %s, 0` (no-op on 8086)"
                    % (it.mnem, dst)]
        return ["%s\t%s, 1" % (it.mnem, dst)] * n

    # -- movzx --------------------------------------------------------------
    def movzx(self, it, pos):
        if len(it.ops) != 2:
            raise Refuse("`movzx` takes two operands")
        dst = reg16(it.ops[0])
        if dst is None:
            raise Refuse("`movzx %s, %s`: destination must be a 16-bit "
                         "register" % (it.ops[0], it.ops[1]))
        src = it.ops[1].strip()
        bare = strip_size(src)
        if reg16(bare):
            raise Refuse("`movzx %s, %s`: 16-bit source is not a widening"
                         % (it.ops[0], src))
        if not is_mem(src) and not reg8(bare):
            raise Refuse("`movzx %s, %s`: source must be an 8-bit register or "
                         "byte memory" % (it.ops[0], src))
        self.bump("movzx")
        if dst not in HALVES:
            # SI/DI/BP have no addressable halves, so the widening has to
            # happen somewhere that does: AX, borrowed outright if it is dead
            # and BORROWED AND GIVEN BACK if it is not (see `stage` below).
            if dst == "sp":
                raise Refuse("`movzx %s, %s`: SP cannot be a widening "
                             "destination" % (it.ops[0], src))
            body = []
            if reg8(bare) != "al":
                body.append("mov\tal, %s" % src)
            body.append("mov\tah, 0")
            body.append("mov\t%s, ax" % dst)
            return self.stage(body, pos, src)
        lo, hi = HALVES[dst]
        out = []
        # Low half first: the source may be addressed through the very
        # register being written (`movzx bx, byte [bx]`).
        if reg8(bare) != lo:
            out.append("mov\t%s, %s" % (lo, src))
        out.append("mov\t%s, 0" % hi)
        return out

    # -- movsx --------------------------------------------------------------
    def movsx(self, it, pos):
        if len(it.ops) != 2:
            raise Refuse("`movsx` takes two operands")
        dst = reg16(it.ops[0])
        if dst is None:
            raise Refuse("`movsx %s, %s`: destination must be a 16-bit "
                         "register" % (it.ops[0], it.ops[1]))
        src = it.ops[1].strip()
        bare = strip_size(src)
        if reg16(bare):
            raise Refuse("`movsx %s, %s`: 16-bit source is not a widening"
                         % (it.ops[0], src))
        if not is_mem(src) and not reg8(bare):
            raise Refuse("`movsx %s, %s`: source must be an 8-bit register or "
                         "byte memory" % (it.ops[0], src))
        self.bump("movsx")
        if dst == "ax" and bare == "al":
            return ["cbw"]
        # CBW is the only 8086 sign-extension and it is hard-wired to AL->AX,
        # so AX is the unavoidable scratch.  MOV and CBW write no flags.
        if dst == "sp":
            raise Refuse("`movsx %s, %s`: SP cannot be a widening destination"
                         % (it.ops[0], src))
        out = []
        if bare != "al":
            out.append("mov\tal, %s" % src)
        out.append("cbw")
        if dst != "ax":
            out.append("mov\t%s, ax" % dst)
            return self.stage(out, pos, src)
        return out

    # -- borrowing AX -------------------------------------------------------
    def stage(self, body, pos, src):
        """`body` needs AX as scratch; give it back if it was still wanted.

        The widenings both have to go through AL - CBW is hard-wired to it and
        SI/DI have no 8-bit halves to write - so AX is not a choice, and the
        scan can only say whether it is free.  When it is not, the answer is
        not a refusal: PUSH AX ... POP AX is four bytes of ordinary 8086 that
        makes the borrow invisible.  `char a[200]; x = a[0] + a[199];` is
        exactly this and it is not exotic C - AX is holding the left operand
        while the right one is widened, so the site is refusable only because
        the sign-extension is a two-register instruction on a machine that has
        a one-register one.

        Three things make the bracket safe rather than merely plausible:
          * PUSH and POP write no flags, so a lowering that was FLAGS-neutral
            stays FLAGS-neutral and the liveness argument above is untouched;
          * SP is not a legal base register in an 8086 effective address, so
            moving SP cannot change where `src` points.  A BP-relative source
            - which is what a local is, and what every site in practice looks
            like - is likewise unaffected, because BP does not move;
          * the destination is never SP (refused above), so the POP reads the
            word the PUSH wrote.

        The stack cost is two bytes for the length of the widening, inside a
        frame budget the report prints on every build.
        """
        if dead_after(self.flow, pos, "ax"):
            return body
        if "sp" in mem_regs(src):
            raise Refuse(
                "widening `%s` needs AX as a staging register, AX is live, "
                "and the source is addressed through SP so it cannot be "
                "bracketed with PUSH/POP." % src)
        self.bump("stage-ax")
        return ["push\tax"] + body + ["pop\tax"]

    # -- imul r, src, imm ---------------------------------------------------
    def imul(self, it, pos):
        ops = it.ops
        if len(ops) == 2:
            dst_op, src, imm_op = ops[0], ops[0], ops[1]
            if imm_value(imm_op) is None:
                # `imul ax, bx` - the 80386 two-operand form with a register
                # source.  There is no constant to decompose, so it drops
                # through to the closing sweep and is refused by name rather
                # than turned into something with a different clobber set.
                return None
        else:
            dst_op, src, imm_op = ops
        dst = reg16(dst_op)
        if dst is None:
            raise Refuse("`imul %s`: destination must be a 16-bit register"
                         % ", ".join(ops))
        k = imm_value(imm_op)
        if k is None:
            raise Refuse("`imul %s`: the multiplier is not a literal, so it "
                         "cannot be decomposed into shifts"
                         % ", ".join(ops))
        # ADD and SHL define FLAGS; the instruction being replaced defines
        # CF/OF and leaves the rest undefined.  Different enough to insist.
        if not flags_dead_after(self.flow, pos):
            raise Refuse(
                "`imul %s`: an 8086 multiply-by-constant costs a shift/add "
                "chain, which writes FLAGS, and FLAGS is live (or not "
                "provably dead) here. Split the expression so the multiply "
                "is not the last thing before a test." % ", ".join(ops))
        self.bump("imul-imm")
        neg = k < 0
        k = abs(k) & 0xFFFF
        out = []
        if k == 0:
            out.append("mov\t%s, 0" % dst)
        elif k == 1:
            if strip_size(src).lower() != dst:
                out.append("mov\t%s, %s" % (dst, src))
        else:
            bits = [i for i in range(16) if k & (1 << i)]
            if len(bits) == 1:
                if strip_size(src).lower() != dst:
                    out.append("mov\t%s, %s" % (dst, src))
                out += ["shl\t%s, 1" % dst] * bits[0]
            else:
                forbid = set(mem_regs(src)) | {dst}
                if reg16(strip_size(src)):
                    forbid.add(reg16(strip_size(src)))
                tmp = find_scratch(self.flow, pos, forbid)
                if tmp is None:
                    raise Refuse(
                        "`imul %s`: multiplying by %d needs a scratch "
                        "register to hold the multiplicand and none is "
                        "provably dead here. Make the multiplier a power of "
                        "two (pad the struct or array element) or split the "
                        "expression." % (", ".join(ops), k))
                out.append("mov\t%s, %s" % (tmp, src))
                if strip_size(src).lower() != dst:
                    out.append("mov\t%s, %s" % (dst, src))
                out += ["shl\t%s, 1" % dst] * bits[0]
                shifted = 0
                for b in bits[1:]:
                    out += ["shl\t%s, 1" % tmp] * (b - shifted)
                    shifted = b
                    out.append("add\t%s, %s" % (dst, tmp))
        if neg:
            out.append("neg\t%s" % dst)
        return out or ["; cc8086: `imul %s` is the identity" % ", ".join(ops)]


# -------------------------------------------------------------------- gate

def gate(it, errors, path):
    """Everything that has no lowering because the fix belongs in the C."""
    def bad(msg):
        errors.append("%s:%d: error: %s" % (path, it.no, msg))

    if it.kind not in ("ins", "dir"):
        return
    if it.allow:
        return

    # The whole code half of the line, comment stripped: a directive keeps its
    # operands there (`extern ___addsf3` is the float reference that matters
    # most, and it is not an instruction), and an instruction reads back the
    # way the author wrote it.
    text = it.code.strip()

    m = REG32.search(text)
    if m:
        bad("`%s` uses the 32-bit register %s. This tree is `cpu 8086`: there "
            "are no 32-bit registers, no `long`, and no 32-bit arithmetic. "
            "Use `int` (16 bits) or a pair, or call the kernel's 32-bit "
            "helpers." % (text.strip(), m.group(0)))

    for h in FLOAT_HELPERS:
        if re.search(r"\b_?%s\b" % re.escape(h), text):
            bad("`%s` references the floating-point helper `%s`. SmallerC "
                "compiles `float` as a SILENT 2-byte type and calls helpers "
                "that are not on this floppy. There is no floating point "
                "here: use scaled integers." % (text.strip(), h))
            break

    if it.kind != "ins":
        return

    if it.mnem == "lea" and len(it.ops) == 2 and "bp" in mem_regs(it.ops[1]):
        bad("`lea %s, %s` takes the address of an automatic. os8088 runs every "
            "task with SS = LOW_SEG and DS = the package segment, so a BP-"
            "relative offset is meaningless the moment anything dereferences "
            "it through DS: it will read or write the wrong segment and "
            "assemble perfectly while doing it. Move the buffer to STATIC "
            "storage - a file-scope object, or `static` inside the function - "
            "and take its address there. (This is also what `&`, an array "
            "argument, a struct passed or returned by value, and `va_start` "
            "all compile to.)" % (it.ops[0], it.ops[1]))

    op = it.mnem
    if op in REP_PREFIXES:
        op = it.ops[0].split()[0].lower() if it.ops else ""
        if not op:
            return
    if op in ES_STRING_OPS:
        bad("`%s` addresses ES:DI, and a package callback is entered with "
            "ES = KERNEL_SEG (SPEC.md 20). This does not fail, it writes into "
            "the kernel. SmallerC emits it to copy a struct by value, so the "
            "usual cause is a struct assignment, a struct argument or a "
            "struct return - pass a pointer to static storage instead. Hand-"
            "written inline asm that has already loaded ES itself may say so "
            "with a `; cc8086:allow <reason>` comment on the line."
            % text.strip())


def not_8086(it):
    """Why this instruction is still not 8086-legal, or None."""
    m = it.mnem
    # Name the processor that introduced it where that is known: "`enter` is
    # an 80186 instruction" tells an author which of the seven lowerings did
    # not fire, and "not an 8086 instruction" only tells them to look.
    if m in ("leave", "enter", "pusha", "popa", "bound", "ins", "insb",
             "insw", "outs", "outsb", "outsw"):
        return "`%s` is an 80186 instruction" % m
    if m in ("pushad", "popad", "pushfd", "popfd", "shld", "shrd", "bsf",
             "bsr", "bt", "btc", "btr", "bts"):
        return "`%s` is an 80386 instruction" % m
    if m not in I8086 and not JCC.match(m) and not SETCC.match(m):
        return "`%s` is not an 8086 instruction" % m
    if SETCC.match(m):
        return "`%s` is an 80386 instruction" % m
    if m == "push" and len(it.ops) == 1 and not is_mem(it.ops[0]) \
            and not is_reg(strip_size(it.ops[0])):
        return "push-immediate is an 80186 instruction"
    if m in SHIFTS and len(it.ops) == 2 \
            and imm_value(it.ops[1]) not in (None, 1):
        return "a multi-bit shift-immediate is an 80186 instruction"
    if m == "imul" and len(it.ops) > 1:
        return "multi-operand `imul` is an 80186/80386 instruction"
    if m in ("movzx", "movsx"):
        return "`%s` is an 80386 instruction" % m
    return None


def final_sweep(lines, errors, path, lineno, internal):
    """Belt and braces: re-parse what came out and refuse anything left.

    Run over the lowerer's own replacements it should never fire, so when it
    does the message says so - that is a bug in this file, not in the C.  Run
    over lines the lowerer left alone it catches the rest of the 80186/80386
    surface: `pusha` in an inline-asm block, a `bound`, anything a future
    SmallerC starts emitting.  Either way the build stops.
    """
    for raw in lines:
        it = Item(lineno, raw)
        if it.kind != "ins":
            continue
        why = not_8086(it) or (
            "a 32-bit register" if REG32.search(raw) else None)
        if not why:
            continue
        if internal:
            errors.append("%s:%d: error: internal: %s survived lowering "
                          "(emitted `%s`) - this is a cc8086 bug, not a "
                          "source bug" % (path, lineno, why, raw.strip()))
        else:
            errors.append("%s:%d: error: %s and cc8086 has no lowering for "
                          "it: `%s`" % (path, lineno, why, raw.strip()))


# ------------------------------------------------------------------ frames

def frames(items):
    """[(name, bytes, lineno)] for every `push bp` / `mov bp, sp` prologue.

    SmallerC writes the frame adjust immediately after the prologue and
    COMMENTS IT OUT when it is zero (`;sub  sp,  0`), so a zero-frame leaf
    would be invisible to a scan for live `sub sp` alone - and a report that
    silently omits functions is worse than no report.
    """
    flow = [i for i in items if i.kind in ("ins", "label", "dir")]
    where = {id(i): n for n, i in enumerate(items)}
    out = []
    for n, it in enumerate(flow):
        if it.kind != "label":
            continue
        rest = flow[n + 1:n + 3]
        if len(rest) < 2:
            continue
        if not (rest[0].kind == "ins" and rest[0].mnem == "push"
                and rest[0].ops == ["bp"]):
            continue
        if not (rest[1].kind == "ins" and rest[1].mnem == "mov"
                and [o.lower() for o in rest[1].ops] == ["bp", "sp"]):
            continue
        size = 0
        for x in flow[n + 3:n + 5]:
            if x.kind == "ins" and x.mnem == "sub" and len(x.ops) == 2 \
                    and reg16(x.ops[0]) == "sp":
                v = imm_value(x.ops[1])
                if v is not None:
                    size = v
                break
            if x.kind == "ins":
                break
        if size == 0:
            # The commented-out form, which is the usual one for a leaf.
            for x in items[where[id(rest[1])] + 1:][:4]:
                m = re.search(r";\s*sub\s+sp\s*,\s*(-?\d+)", x.raw)
                if m:
                    size = int(m.group(1))
                    break
                if x.kind == "ins":
                    break
        out.append((it.label, size, it.no))
    return out


# ----------------------------------------------------------------- overlay
#
# SPEC.md 73.14.  A C function whose name begins with `ovl_` does not live in
# the package's segment: its code is emitted into `.modc`, which crt0.asm
# declares `follows=.data vstart=0` and tools/os88ovl.py cuts off the end of
# the assembled image into <NAME>.OVL - a file that ships beside the package
# and is read into a heap claim the first time one of its functions is called.
#
# The module keeps DS = the package's segment, so every global, string literal
# and .bss byte the moved code names is still a plain DS-relative reference and
# is still resident.  ONLY CODE MOVES.  That is the whole reason this is
# possible in C at all: a compiled function names its data through DS and there
# is no way to tell the compiler otherwise, so a design where the data moved
# too would need a compiler that does not exist here.
#
# Four rewrites, and each one is a consequence of the module having a CS of its
# own rather than a policy:
#
#   1. `section .text` inside a moved function becomes `section .modc`.  A
#      function is not one contiguous block - SmallerC switches to .rodata in
#      the middle of one to emit a string literal and switches back - so this
#      tracks which function each .text fragment belongs to and moves the code
#      fragments only.  The data fragments stay exactly where they were.
#
#   2. EVERY CALL TO AN OVERLAY FUNCTION BECOMES A FAR CALL, including one
#      from inside the module.  `call far [cc_ovm_<name>]`, a dword of
#      (offset, segment) whose offset is assembled in - the module is
#      assembled WITH the package, one nasm job, so both halves agree about
#      every address by construction - and whose segment cc_ovbind stamps at
#      load.  Uniformly far, because a far call pushes four bytes of return
#      address and a near call pushes two, and the callee's arguments are at a
#      fixed offset from its own frame: two kinds of entry would need two
#      argument layouts for one function body.  An intra-module far call costs
#      two extra bytes and about thirteen clocks on an 8088, which is nothing
#      beside a 756 us drawing call (PERFORMANCE.md).
#
#   3. So every `[bp+N]` with N >= 4 inside a moved function - which is how
#      SmallerC names an ARGUMENT - becomes `[bp+N+2]`.  Locals are `[bp-N]`
#      and are untouched, which is what makes the rewrite safe to do by
#      inspection: the sign of the displacement is the whole distinction.
#      Every `ret` in a moved function becomes `retf` for the same reason.
#
#   4. A call FROM the module to resident code becomes `call far [cc_ovv_<n>]`
#      pointing at a SHIM, never at the routine: every compiled C function ends
#      in a near `ret`, so far-calling one directly pops the offset, leaves the
#      segment on the stack and returns into nothing.  A shim is `call _f` then
#      `retf`, and because the shim's own near call pushes exactly the two
#      bytes the routine's frame expects, the arguments need no fixup in this
#      direction at all.
#
# A resident call site also gets a three-instruction preamble: `call cc_ovneed`
# / `jc` past the call.  cc_ovneed is a no-op of four bytes once the module is
# in, loads it if it is not, and on refusal - no heap, or a disk without the
# file - toasts the reason and answers AX = 0, so the call returns 0 without
# happening (SPEC.md 47: refusal is an ordinary path).  It is safe to insert
# between the argument pushes and the call because the ABI has no callee-saved
# register: nothing is live in a register across a `call`, which is the same
# fact the push-immediate lowering above is built on.
#
# What is refused: TAKING THE ADDRESS of an overlay function.  A function
# pointer here is a 16-bit offset with no segment, and the module's offsets
# mean something else entirely in the package's segment - a call through one
# lands in the middle of resident code that happens to be at that offset.  It
# assembles and it runs, which is the signature of everything else this file
# exists to refuse.

OVL_MARK = "_ovl_"                  # the C name is `ovl_*`; SmallerC adds the _

FUNC_LABEL = re.compile(r"^(_[A-Za-z]\w*):\s*$")
SECTION = re.compile(r"^\s*section\s+(\.\w+)", re.I)
CALL_SYM = re.compile(r"^(\s*)call\s+(_[A-Za-z]\w*)\s*$")
BP_ARG = re.compile(r"\[\s*bp\s*\+\s*(\d+)\s*\]")
BARE_RET = re.compile(r"^(\s*)ret\s*$")


def _owner(lines, i):
    """The function a `section .text` at line i opens, or None to continue.

    The block's own function label is the first thing in it that is not blank,
    a comment or a `global` - so this looks for that and nothing else.  A
    fragment with no label of its own is a continuation of the function the
    last one belonged to, which is what makes a string literal in the middle of
    a function harmless.
    """
    for line in lines[i + 1:]:
        s = line.strip()
        if not s or s.startswith(";") or s.lower().startswith("global"):
            continue
        m = FUNC_LABEL.match(line)
        return m.group(1) if m else None
    return None


def overlay(lines, errors, path):
    """Split `ovl_*` out into .modc and wire both directions.  See above.

    Returns (lines, stats).  `lines` is unchanged, and no table is emitted, if
    the program has no overlay function at all - a package that does not use
    the mechanism must assemble byte for byte as it did before it existed.
    """
    funcs, sect, cur = set(), ".text", None
    for i, line in enumerate(lines):        # pass one: who is out there
        m = SECTION.match(line)
        if m:
            sect = m.group(1).lower()
            if sect == ".text":
                own = _owner(lines, i)
                if own:
                    cur = own
                    if own.startswith(OVL_MARK):
                        funcs.add(own)
    if not funcs:
        return lines, None

    entries, shims, sites, moved = {}, {}, 0, set()
    out, sect, cur, inmod = [], ".text", None, False
    for i, line in enumerate(lines):
        m = SECTION.match(line)
        if m:
            sect = m.group(1).lower()
            if sect == ".text":
                own = _owner(lines, i)
                if own:
                    cur = own
                inmod = cur in funcs
                if inmod:
                    moved.add(cur)
                    out.append("section .modc")
                    continue
            else:
                inmod = False       # a data fragment inside a moved function
            out.append(line)        # is still data, and data does not move
            continue

        call = CALL_SYM.match(line)
        if call:
            pad, sym = call.group(1), call.group(2)
            if sym in funcs:
                entries[sym] = True
                if inmod:           # module to module: it is already loaded,
                    out.append("%scall far [cc_ovm%s]" % (pad, sym))
                else:               # so only a resident site pays for the load
                    sites += 1
                    lab = "..@ovl_site_%d" % sites
                    out.append("%scall\tcc_ovneed\t\t; cc8086: overlay entry"
                               % pad)
                    out.append("%sjc\t%s" % (pad, lab))
                    out.append("%scall far [cc_ovm%s]" % (pad, sym))
                    out.append("%s:" % lab)
                continue
            if inmod:
                shims[sym] = True
                out.append("%scall far [cc_ovv%s]" % (pad, sym))
                continue

        if inmod:
            r = BARE_RET.match(line)
            if r:                   # entered far, so it leaves far
                out.append("%sretf" % r.group(1))
                continue
            line = BP_ARG.sub(
                lambda m: ("[bp+%d]" % (int(m.group(1)) + 2))
                if int(m.group(1)) >= 4 else m.group(0), line)
        out.append(line)

    # Taking the address of one is the mistake this cannot let through.
    for i, line in enumerate(lines):
        if CALL_SYM.match(line) or FUNC_LABEL.match(line):
            continue
        for sym in funcs:
            if re.search(r"(?<![\w.@])%s(?![\w.@])" % re.escape(sym), line) \
                    and not re.match(r"^\s*global\b", line):
                errors.append(
                    "%s:%d: error: the address of the overlay function `%s` is "
                    "taken here (`%s`). An overlay function lives in another "
                    "segment: its offset means something else entirely in this "
                    "one, so a call through the pointer lands in the middle of "
                    "resident code. Call it directly, or move it back out of "
                    "the overlay by renaming it."
                    % (path, i + 1, sym[1:], line.strip()))

    tail = ["", "; " + "-" * 74,
            "; cc8086: the overlay's two vector tables (SPEC.md 73.14).",
            "; Offsets assembled in - one nasm job, so both halves agree about",
            "; every address; segments stamped by cc_ovbind at load.",
            "; " + "-" * 74,
            "%ifndef CC_HAS_OVL",
            '%error "this package has ovl_* functions but its .asm shim does '
            'not %define CC_HAS_OVL - see SPEC.md 73.14"',
            "%endif",
            "section .data",
            "cc_ovm_first:"]
    for sym in sorted(entries):
        tail.append("cc_ovm%s:\tdw %s, 0" % (sym, sym))
    tail += ["cc_ovm_end:", ""]
    if shims:
        # Six bytes each, and the whole convention change is in cc_ovthunk:
        # a shim that made a near call of its own would leave the routine's
        # arguments two bytes further away than every compiled reference to
        # them says (see crt0.asm - a function told 10 saw 49).
        tail.append("section .text")
        for sym in sorted(shims):
            tail.append("cc_ovs%s:\tmov\tbx, %s" % (sym, sym))
            tail.append("\tjmp\tcc_ovthunk")
        tail.append("section .data")
    tail.append("cc_ovv_first:")
    for sym in sorted(shims):
        tail.append("cc_ovv%s:\tdw cc_ovs%s, 0" % (sym, sym))
    tail += ["cc_ovv_end:", "section .text", ""]

    return out + tail, (len(moved), len(entries), len(shims), sites)


# ---------------------------------------------------------------- externals
#
# `nasm -f bin` has no symbol table to import from, and this tree has no
# linker at all (CLAUDE.md): a C package is ONE assembly - the compiled C,
# apps/cc/crt0.asm and apps/cc/os88thunk.asm - so every symbol the C names is
# defined a few thousand lines above it in the same job.  SmallerC does not
# know that.  It emits one `extern` per symbol it did not see a definition for,
# which is every `os88_*` call in the program, and NASM reads `extern X` for an
# X it has already defined as a redefinition of X:
#
#     build/chello.gen.asm:1429: error: label `_os88_strlen' inconsistently
#         redefined
#     apps/cc/os88thunk.asm:1545: info: label `_os88_strlen' originally
#         defined here
#
# So EVERY C package failed to assemble, ccsmoke and the two capability gates
# included, and none of it was noticeable from the outside: nothing in `all`
# reaches a C target (apps/cc/Makefile.inc), and tools/setup-cc.sh's closing
# canary - the one thing that did run the whole chain - is `sed`ed past this
# exact directive on its way to NASM, in the one path where doing so is
# harmless.  The workaround was written and never moved to where the build
# could use it.
#
# THE DECLARATION IS DROPPED, NOT GIVEN AN ADDRESS.  The canary turns each one
# into `X equ 0` because its `sink()` genuinely has no definition anywhere and
# what is being checked there is an instruction encoding.  A real package must
# have the opposite behaviour: a call to an `os88_*` the SDK has no thunk for
# has to be a build failure that names the function, and `equ 0` is a call to
# offset 0 of the package's own segment - the header - which assembles,
# packages and runs, which is the failure mode this whole file exists to
# refuse.  With the line gone, NASM answers `symbol `_os88_foo' not defined`
# and names it.  Verified both ways: tests/covl and tests/chello build and run,
# and a call to a thunk nobody wrote stops the build naming the function.
#
# The gate has already read these lines by the time this runs, which is why
# this is a pass over the emitted text rather than a lowering: `extern
# ___addsf3` is how a `float` in the C is caught (see gate()), and a rewrite
# that ran first would take the evidence away.

EXTERN = re.compile(r"^(\s*)extern\s+(\S.*?)\s*$", re.I)


def externs(lines):
    """Comment out SmallerC's `extern` declarations.  See above.

    Returns (lines, count).  Left as a comment rather than deleted so that the
    generated assembly still records what the C reached for, which is the list
    a reader wants in front of them when NASM reports one of them undefined.
    """
    out, n = [], 0
    for line in lines:
        m = EXTERN.match(line)
        if m:
            out.append("%s; cc8086: extern %s\t\t; resolved in this assembly"
                       % (m.group(1), m.group(2)))
            n += 1
        else:
            out.append(line)
    return out, n


# -------------------------------------------------------------------- main

def run(path, text, args):
    items = [Item(n, line) for n, line in enumerate(text.split("\n"), 1)]
    flow = [i for i in items if i.kind in ("ins", "label", "dir")]
    pos = {id(i): n for n, i in enumerate(flow)}

    # Three buckets, because only one of them is optional: the GATE is the
    # rule set that belongs to this OS, and `--no-gate` drops it for a corpus
    # measurement.  A site the lowerer cannot rewrite, and anything the
    # closing sweep finds, are facts about the 8086 and are never dropped.
    gate_errors, errors = [], []
    for it in items:
        gate(it, gate_errors, path)

    low = Lowerer(flow)
    for it in items:
        if it.kind != "ins":
            continue
        try:
            rep = low.lower(it, pos[id(it)])
        except Refuse as e:
            errors.append("%s:%d: error: %s" % (path, it.no, e))
            continue
        if rep is None:
            final_sweep([it.raw], errors, path, it.no, internal=False)
            continue
        head = "%s\t\t; cc8086: %s" % (rep[0], it.raw.strip()) \
            if not rep[0].startswith(";") else rep[0]
        out = [it.indent + head]
        out += [(l if l.endswith(":") else it.indent + l) for l in rep[1:]]
        if it.label:
            out.insert(0, it.label + ":")
        it.out = out
        final_sweep(out, errors, path, it.no, internal=True)

    lines = []
    for it in items:
        lines.extend(it.out if it.out is not None else [it.raw])

    # AFTER lowering, because a lowering can emit a `[bp+N]` of its own that
    # was not in the source (a fused `imul ax, [bp+6], 2` becomes a `mov` from
    # it), and an argument fixup that ran first would miss exactly those.
    lines, ovl = overlay(lines, errors, path)

    # Last, so that overlay() sees exactly the text it always saw.
    lines, next_ = externs(lines)

    fr = frames(items)
    over = [f for f in fr if f[1] > args.max_frame]
    for name, size, no in over:
        errors.append(
            "%s:%d: error: frame of `%s` is %d bytes; --max-frame is %d. A "
            "worker task's whole stack is 384 bytes (kernel/sched.inc, "
            "SCH_STACK) and the UI task has of the order of 700 bytes of "
            "headroom, so this frame is a crash waiting for a deep call. Move "
            "the big automatic to static storage."
            % (path, no, name, size, args.max_frame))

    # The report goes to stderr when the lowered assembly is going to stdout,
    # or it would end up inside the file the caller is piping.
    rep = sys.stdout if args.output else sys.stderr
    if not args.quiet:
        for name, size, no in sorted(fr, key=lambda f: -f[1]):
            print("cc8086:   frame %-24s %5d" % (name, size), file=rep)
    elif over:
        for name, size, no in over:
            print("cc8086:   frame %-24s %5d  OVER" % (name, size), file=rep)

    tally = ", ".join("%s %d" % (k, v)
                      for k, v in sorted(low.counts.items())) or "none"
    print("cc8086: %s: %d function(s), %d frame byte(s) max, lowered %d "
          "site(s) [%s]"
          % (path, len(fr), max([f[1] for f in fr] or [0]),
             sum(low.counts.values()), tally), file=rep)
    if next_:
        print("cc8086: %s: %d external reference(s) left to this assembly"
              % (path, next_), file=rep)
    if ovl:
        print("cc8086: %s: overlay - %d function(s) moved to .modc, %d entry "
              "vector(s), %d resident shim(s), %d loading call site(s)"
              % (path, ovl[0], ovl[1], ovl[2], ovl[3]), file=rep)

    return gate_errors, errors, "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(
        description="Lower SmallerC's NASM to a strict 8086, or refuse it.")
    ap.add_argument("input", metavar="IN.asm",
                    help="assembly from `smlrcc -S`; `-` for stdin")
    ap.add_argument("-o", "--output", metavar="OUT.asm",
                    help="lowered assembly; stdout if omitted")
    ap.add_argument("--max-frame", type=int, default=96, metavar="N",
                    help="fail on any stack frame wider than N bytes "
                         "(default 96; a worker's whole stack is 384 bytes)")
    ap.add_argument("--no-gate", action="store_true",
                    help="lower but do not enforce the &local / ES-string / "
                         "float / 32-bit rules. FOR MEASURING A CORPUS THAT "
                         "IS NOT GOING TO RUN HERE - never for a package that "
                         "ships")
    ap.add_argument("--quiet", action="store_true",
                    help="print only frames that break the budget")
    args = ap.parse_args()

    if args.input == "-":
        text, path = sys.stdin.read(), "<stdin>"
    else:
        path = args.input
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError as e:
            print("cc8086: error: cannot read %s: %s" % (path, e),
                  file=sys.stderr)
            return 1

    gate_errors, errors, out = run(path, text, args)

    if not args.no_gate:
        errors = gate_errors + errors
    elif gate_errors:
        print("cc8086: --no-gate: %d gate finding(s) suppressed"
              % len(gate_errors), file=sys.stderr)
    if errors:
        for e in errors:
            print(e, file=sys.stderr)
        print("cc8086: %d problem(s); %s not written"
              % (len(errors), args.output or "output"), file=sys.stderr)
        return 1

    header = "; Generated by tools/cc8086.py from %s - do not edit.\ncpu 8086\n" \
             % path
    if args.output:
        try:
            with open(args.output, "w", encoding="utf-8") as f:
                f.write(header + out)
        except OSError as e:
            print("cc8086: error: cannot write %s: %s" % (args.output, e),
                  file=sys.stderr)
            return 1
    else:
        sys.stdout.write(header + out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
