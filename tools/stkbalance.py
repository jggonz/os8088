#!/usr/bin/env python3
"""Every `ret` must be reached with the stack exactly where it started.

WHY A COUNT IS NOT ENOUGH, and why this walks paths instead. A routine with two
exits repeats its pops on each, so comparing total pushes with total pops flags
409 of 4130 routines in this tree - one in ten, all of them fine. A gate wrong
that often is worse than no gate, because it gets ignored and then it is worse
than nothing when it is finally right.

So this builds a control-flow graph and propagates stack depth through it. A
finding is one of three things, and each is a real bug:

  * a `ret` reached at non-zero depth   - returns to a saved register
  * a label reached at two depths       - one path pushed and another did not
  * a `jmp` to another routine at depth - a tail call carrying rubbish

The first is what hung Sheet's chart legend: `ch_legend` pushed SI, never
popped it, and its `ret` jumped to whatever SI held. No crash, no message - a
black canvas and a wedged app.

WHY THE WALK IS CORPUS-WIDE, AND WHY IT FOLLOWS TAIL JUMPS. Splitting the file
at every global label and walking each chunk from depth 0 is wrong for the
shape this kernel is full of: a label that is only ever JUMPED to is not a
routine, it is a CONTINUATION, and the pushes it pops happened in whatever
jumped into it.

    ui_lit_on:  push ax / mov al,1 / jmp short ui_lit_go
    ui_lit_off: push ax / xor al,al
    ui_lit_go:  ... / pop ax / ret

Walked as its own routine, `ui_lit_go` starts at 0, pops AX and reports
`ret at depth -1`. It is correct code. Thirteen of the kernel's twenty-four
findings were this one mistake, and it is also the whole of the old
"no-ret chunks not walked" blind spot: a chunk whose every exit is a tail jmp
was not walked AT ALL, which is exactly the shape tail-merging produces - so
the gate went blindest precisely where a size pass does its work.

So: an ENTRY is a global that is called, or whose address is taken, or that
nothing references (an ISR installed by vector). A global that is only the
target of a jmp or a jcc is a continuation, and is never walked from zero -
it inherits the depth of the path that arrives at it. Tail jumps are FOLLOWED,
across files, because shared tails cross them: fdlg.inc banks SI and jumps
into files.inc's `fm_dotin`, which pops it.

THREE IDIOMS THE 8086 USES THAT ARE NOT STACK LEAKS. Each was a finding
before it was understood, and each is mechanical enough to recognise:

  * `jmp short $+2` - an I/O settling delay (clock.inc, mouse.inc). It is a
    jump to the next instruction, not a tail call.
  * `pushf` + `call far [handler]` - chaining an interrupt. The pushf supplies
    the FLAGS the chained handler's `iret` pops, so the PAIR is net zero, not
    the +1 a depth-neutral `call` leaves behind (splash.inc's `spl_isr`).
  * `push seg` / `push off` / `retf` - a constructed FAR JUMP. The `retf`
    consumes exactly the two words pushed, so depth +2 there is the idiom's
    signature rather than a leak (driver.inc's `drv_pkg_disp`).

DELIBERATE STACK SURGERY is real and rare, and there are two kinds.

A routine that BANKS something on the caller's stack and a partner that takes
it off again - Sheet's sh_vpush/sh_binop_pre pair, os88fp's fp_push_a/fp_pop_a.
Mark those `; STKBALANCE-NET: +4` (or -4), and the declared delta is applied at
every `call` to them, so their CALLERS come out balanced instead of inheriting
a phantom leak. That is the difference between a tool that finds two real bugs
and one that reports six routines because two of them are unusual.

A loop that pushes N things and a second loop that pops them - every itoa in
this tree. The count lives in a register, so no static walk can pair them.
Mark the routine `; STKBALANCE-LOOP: <reason>` and a depth conflict inside it
is counted rather than reported. The marker is per ROUTINE and deliberate,
because the unmarked shape it would otherwise be confused with is a real bug:
`push cx / .l: pop cx / loop .l / ret` pops the return address on its second
turn, and its loop target DOMINATES the source exactly as an itoa's does - so
no dominance test, no forward/backward test and no jcc-only test can tell the
two apart. Suppressing every backward conflict was tried first and it hid
both this and a tail-merged epilogue one register deep, which is the commonest
shape a size pass makes. Now nothing is suppressed without a name on it.

`; STKBALANCE-OK: <reason>` skips a routine outright. The reason is the point -
an unexplained exemption is how a gate stops meaning anything. It is the answer
for surgery no static walk can follow, of which this tree has exactly one:
`task_yield` FABRICATES an int 08h frame (`pushf` / `push cs` / `call .save`)
and its `ret` is reached by the scheduler's `iret`, not by `.save` returning.

TWO SDK MACROS ARE EXPANDED, because their labels are built with `%+` and no
source line spells them: `OS88_POPLADDER` and `OS88_PUSHER` (apps/os88api.inc,
SPEC.md 20.5). See Unit._expand.

A FINDING NAMES THE JUMP THAT CAUSED IT. A shared epilogue reached by two
broken jumps is two findings, each naming the exit by which its walk left the
routine (see walk) - one `ret` with a count of one hid the second jump.

  python3 tools/stkbalance.py [file ...]     default: apps/ and kernel/
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CALL = re.compile(r"^call\s+(?:near\s+|far\s+|word\s+)?(\S+)", re.I)
CALLFAR = re.compile(r"^call\s+far\b", re.I)
NET = re.compile(r"STKBALANCE-NET:\s*([+-]?\d+)")
PUSH = re.compile(r"^(push|pusha|pushf)\b", re.I)
POP = re.compile(r"^(pop|popa|popf)\b", re.I)
RET = re.compile(r"^(ret|retn|retf|iret)\b", re.I)
RETF = re.compile(r"^retf\b", re.I)
PUSHF = re.compile(r"^pushf\b", re.I)
JMP = re.compile(r"^jmp\s+(?:strict\s+)?(?:short\s+|near\s+|word\s+)?(\S+)", re.I)
JCC = re.compile(r"^(j[a-z]{1,3}|loop|loope|loopne|loopz|loopnz)\s+"
                 r"(?:short\s+)?(\S+)$", re.I)
SPADD = re.compile(r"^(add|sub)\s+sp\s*,\s*(\S+)$", re.I)
# `lea sp, [bp+N]` - a frame's teardown in ONE instruction, where it was
# `mov sp, bp` / `add sp, N` (gfx_blit1_x's `.noswap`). The walk has never
# modelled `mov sp, bp`: it trusts the frame's own `mov bp, sp` and reads the
# pair as its `add sp, N` half, so this is read exactly the same way - the
# two spellings must give the same verdict, or a size pass that takes the
# shorter one reads as a leak.
LEASP = re.compile(r"^lea\s+sp\s*,\s*\[\s*bp\s*\+\s*([^\]\s]+)\s*\]$", re.I)
GLOBAL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
LOCAL = re.compile(r"^(\.[A-Za-z0-9_]+):")
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
# ...and the same with ONE dot allowed, for a `dw` row.  A table's arm is as
# often `owner.local` as a global - mppui.inc's nine glyph arms and wfx.inc's
# twenty-three opcode arms are all locals of the routine that dispatches into
# them - and IDENT read `dw tab.arm` as two words, `tab` and `arm`.  The walk
# then pushed the OWNER's global label at the dispatch depth (a backward edge
# into the same routine, and so suppressed) and never walked the arm at all,
# which is how an arm that pushed and never popped passed the gate.
IDENT_DOT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)?")
LOOPMARK = re.compile(r"STKBALANCE-LOOP\b")
# apps/os88api.inc's OS88_POPLADDER and OS88_PUSHER (SPEC.md 20.5).  The
# ladder's rung labels are built with `%+` inside the macro, so no source line
# ever SPELLS them: read as text, `jmp pk_pop_3` is a tail jump into nothing at
# depth +3 and every adopter failed this gate.  So an invocation is EXPANDED
# here, into exactly the lines the macro emits - see Unit.__init__.
LADDER = re.compile(r"^OS88_POPLADDER\s+(\.?[A-Za-z_][A-Za-z0-9_]*)\s*,\s*(.+)$", re.I)
PUSHER = re.compile(r"^OS88_PUSHER\s+(\.?[A-Za-z_][A-Za-z0-9_]*)\s*,\s*(.+)$", re.I)
MACINV = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s+(\S+)\s*$")
MACDEF = re.compile(r"^%macro\s+([A-Za-z_][A-Za-z0-9_]*)\s+(\d+)", re.I)
MACEND = re.compile(r"^%endmacro\b", re.I)
# `jmp %1` / `call %1` inside a macro body: the macro's ARGUMENT is a branch
# target, so an invocation is control flow and not an address being taken.
MACJMP = re.compile(r"^(?:jmp|j[a-z]{1,3})\s+(?:strict\s+)?(?:short\s+|near\s+)?%1\s*$", re.I)
MACCALL = re.compile(r"^call\s+%1\s*$", re.I)
# `jmp short $+2` and friends: a jump to the very next instruction, used all
# over this tree to let an I/O port settle.  It is not a tail call.
SETTLE = re.compile(r"^\$\s*\+\s*2$")
# `jmp 0xFFFF:0x0000`: a far jump to a LITERAL address leaves every routine the
# walk can see (a reset, a ROM entry), so the path simply ends there - the
# stack it leaves is never popped by anything in the corpus.  A far jump to a
# LABEL (`jmp KERNEL_SEG:sched_unhook`) is still a tail call and still walked.
FARLIT = re.compile(r"^(?:0x[0-9a-f]+|[0-9][0-9a-f]*h?)\s*:\s*(?:0x[0-9a-f]+|[0-9][0-9a-f]*h?)$", re.I)
# A table is not code.  `dbg_reg` is `dw TAG, handler` pairs and the walk used
# to fall out of the bottom of it into whatever followed, reporting that
# routine's `retf`.  Control flow never runs THROUGH a table, so a data
# directive ends the walk.
DATA = re.compile(r"^(d[bwdq]|res[bwdq]|incbin)\b", re.I)
# `%define a b` (and its x/i spellings): an alias, not an address (see _scan)
PPDEF = re.compile(r"^%x?i?define\b", re.I)
# A `section` directive is a HARD BARRIER: the next byte is in a different
# address space, so nothing falls through it.  Without this a bare label at the
# end of one section (boot2_end, modmap_end) is walked into whatever the next
# section opens with, and the walk reports a depth no execution can reach.
SECT = re.compile(r"^section\b", re.I)
DWORDS = re.compile(r"^d[wd]\s+(.*)$", re.I)
# `jmp [wsm_tab + bx]` - a jump TABLE.  Its arms are not routines entered at
# zero: wcanvas.asm's far entry banks five registers and dispatches, so every
# arm runs at +5 and exits through the shared `wsm_out` that pops them.  Read
# as entries they each report `retf at depth -5`; followed from the dispatch,
# they are exactly balanced.
#
# The operand is matched WHOLE and then searched for a known table, because
# the three CPU cores in this tree write the same thing four other ways:
# `jmp [cs:bx+ed_tab]` puts a segment override in front and the table SECOND,
# and `jmp [cs:bx+wvm_btab-12]` biases the base as well.  A regex that expects
# the table first and no override reads every opcode handler in RunCPM's Z80,
# the C64's 6510 and Weave's VM as a routine entered at depth 0 - which is how
# all three came to be dispatch tables nothing had ever walked.
JMPTAB = re.compile(r"^jmp\s+(?:word\s+)?\[([^\]]+)\]", re.I)

# A walk that follows tail jumps can in principle wander a long way.  In this
# tree it does not, but a bound keeps a pathological chain from hanging the
# build, and the count of walks that hit it is printed rather than hidden.
WALK_MAX = 60000


def strip(line):
    """Drop the comment, keeping quoted semicolons intact."""
    # The character loop below only matters when a QUOTE comes before the
    # first `;` - everywhere else the comment starts at that `;` and str.find
    # is the same answer. It ran on every line of the corpus and was the
    # largest single cost of the stkapps row.
    semi = line.find(";")
    head = line if semi < 0 else line[:semi]
    if "'" not in head and '"' not in head:
        return head.strip()
    out, q = [], None
    for ch in line:
        if q:
            out.append(ch)
            if ch == q:
                q = None
            continue
        if ch in "'\"":
            q = ch
            out.append(ch)
            continue
        if ch == ";":
            break
        out.append(ch)
    return "".join(out).strip()


class Unit(object):
    """One source file, flat.  Labels are resolved the way NASM resolves them:
    a local belongs to the last global above it, so `.out` in two routines is
    two different labels and the owner is what tells them apart."""

    def __init__(self, path):
        self.path = path
        self.body = []          # [(idx, text, raw)]
        self.glab = {}          # "name"          -> idx
        self.llab = {}          # ("owner", ".x") -> idx
        self.owner = []         # idx -> owning global name (or None)
        self.gof = {}           # "name" -> [idx of every line it owns]
        self.lineno = []        # idx -> 1-based SOURCE line (expansions share one)
        self.rungs = set()      # globals an OS88_POPLADDER emitted
        cur = None
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.read().split("\n")
        for n, raw in enumerate(lines, 1):
            self._n = n
            text = strip(raw)
            m = LADDER.match(text) or PUSHER.match(text)
            if m:
                cur = self._expand(m, text, raw, cur)
                continue
            m = GLOBAL.match(text)
            if m and not text.startswith("."):
                cur = m.group(1)
                self.glab.setdefault(cur, len(self.body))
                rest = text[m.end():].strip()
                self._add(rest, raw, cur)
                continue
            m = LOCAL.match(text)
            if m and cur is not None:
                self.llab.setdefault((cur, m.group(1)), len(self.body))
                rest = text[m.end():].strip()
                self._add(rest, raw, cur)
                continue
            self._add(text, raw, cur)

    def _add(self, text, raw, cur):
        # A comment-only line is kept with empty text: the STKBALANCE markers
        # live in comments, and dropping the line drops the marker with it.
        self.body.append((len(self.body), text, raw))
        self.owner.append(cur)
        self.lineno.append(self._n)

    def _label(self, name, cur):
        """Define `name` here the way NASM would; returns the new owner."""
        if name.startswith("."):
            if cur is not None:
                self.llab.setdefault((cur, name), len(self.body))
            return cur
        self.glab.setdefault(name, len(self.body))
        return name

    def _expand(self, m, text, raw, cur):
        """One OS88_POPLADDER / OS88_PUSHER invocation, as the lines the macro
        emits (apps/os88api.inc).  Each synthetic line keeps the invocation's
        source line number and says which rung it is, so a finding names it.

        OS88_POPLADDER name, r1, ..., rN:
            name_N: pop rN / ... / name_1: pop r1 / name_0: ret
        Every rung is a global the macro DEFINES whether or not anything jumps
        to it, so an unreferenced rung must not read as an entry (entered at
        depth 0 it pops its caller): rungs are recorded, and entries() admits
        one only when it is called or its address is taken.

        OS88_PUSHER name, r1, ..., rN is a called routine that leaves N words
        under its own return - the banking shape `; STKBALANCE-NET: +N`
        declares - so it is emitted as one with that marker, and a `call name`
        moves the caller's depth by N exactly as the macro does."""
        name = m.group(1)
        regs = [r.strip() for r in m.group(2).split(",") if r.strip()]
        what = text.split(None, 1)[0].upper()

        def note(lab, insn):
            return "%s    ; [%s: %s]" % (raw.rstrip(), lab, insn)

        if what == "OS88_POPLADDER":
            for k in range(len(regs), 0, -1):
                lab = "%s_%d" % (name, k)
                cur = self._label(lab, cur)
                if not lab.startswith("."):
                    self.rungs.add(lab)
                self._add("pop " + regs[k - 1], note(lab, "pop " + regs[k - 1]), cur)
            lab = name + "_0"
            cur = self._label(lab, cur)
            if not lab.startswith("."):
                self.rungs.add(lab)
            self._add("ret", note(lab, "ret"), cur)
            return cur
        cur = self._label(name, cur)
        self._add("", "; STKBALANCE-NET: +%d (OS88_PUSHER)" % len(regs), cur)
        for r in regs:
            self._add("push " + r, note(name, "push " + r), cur)
        self._add("ret", note(name, "ret"), cur)
        return cur


def delta(text):
    """How this instruction moves SP, in words. None = not a stack move."""
    if PUSH.match(text):
        return 8 if text.lower().startswith("pusha") else 1
    if POP.match(text):
        return -8 if text.lower().startswith("popa") else -1
    m = LEASP.match(text)
    if m:
        try:
            return -(int(m.group(1), 0) // 2)
        except ValueError:
            return None
    m = SPADD.match(text)
    if m:
        try:
            n = int(m.group(2), 0)
        except ValueError:
            return None
        return -(n // 2) if m.group(1).lower() == "add" else (n // 2)
    return None


class Corpus(object):
    """Every file at once.  Cross-file tail jumps are the reason: a shared tail
    is reached from another file as often as from its own."""

    def __init__(self, files):
        self.units = [Unit(p) for p in files]
        self.gindex = {}        # "name" -> (unit, idx)
        # A corpus spanning several TRANSLATION UNITS can define one name
        # twice - apps/cc/crt0.asm's C runtime is copied into Loom's PV module,
        # drivers/net and drivers/ether share `eth_*` state names. The first
        # definition wins, which is a real inaccuracy and a small one (22 of
        # 9,786 across apps/ and drivers/), so it is COUNTED and printed rather
        # than left silent - the same treatment as the back edges.
        self.dup = 0
        self.rungs = set()      # OS88_POPLADDER's rung labels (Unit._expand)
        for u in self.units:
            self.rungs |= u.rungs
            for name, idx in u.glab.items():
                if name in self.gindex:
                    self.dup += 1
                else:
                    self.gindex[name] = (u, idx)
        self.called = {}
        self.jumped = {}
        self.addressed = {}
        self.nets = {}
        self.skip = set()
        self.loops = set()      # routines marked `; STKBALANCE-LOOP:`
        self._entries = None
        self.tables = {}        # "tab" -> [label, ...] from its dw/dd rows
        self.jumptabs = set()   # tables reached by `jmp [tab + reg]`
        self.macro = {}         # "RAE" -> "jmp" | "call"
        self._infos = {}        # text -> walk()'s verdicts, see info()
        self._macros()
        self._tables()
        self._scan()

    def _macros(self):
        """Macros whose body branches to their own argument.  wvm.inc wraps
        `je %%o / jmp %1 / %%o:` in seven of them (`RNE`, `RAE`, ...) and uses
        them at fifty sites; read as bare mentions, every one of those targets
        looks like an address being taken, which promotes a refusal handler to
        a routine entered at depth 0 and walks it from there."""
        for u in self.units:
            cur, nargs, body = None, 0, []
            for idx, text, _ in u.body:
                if cur is None:
                    m = MACDEF.match(text) if text else None
                    if m:
                        cur, nargs, body = m.group(1), int(m.group(2)), []
                    continue
                if text and MACEND.match(text):
                    if nargs >= 1:
                        for t in body:
                            if MACCALL.match(t):
                                self.macro[cur] = "call"
                                break
                            if MACJMP.match(t):
                                self.macro[cur] = "jmp"
                                break
                    cur = None
                    continue
                if text:
                    body.append(text)

    def info(self, text):
        """Every regex verdict walk() asks of one instruction, computed once
        per distinct TEXT.  walk() runs once per entry - 18,068 of them over
        apps/ and drivers/ - and each re-asked the same ten patterns of the
        same lines; they depend on the text alone (and on the macro table,
        fixed once the corpus is built), so the answer is shared."""
        got = self._infos.get(text)
        if got is None:
            m = JMPTAB.match(text)
            tab = IDENT.findall(m.group(1)) if m else None
            m = JMP.match(text)
            jmp = m.group(1) if m else None
            kind, mtgt = self._macinvoke(text)
            m = JCC.match(text)
            jcc = m.group(2) if m else None
            m = CALL.match(text)
            got = self._infos[text] = (
                bool(DATA.match(text) or SECT.match(text)),
                bool(RET.match(text)), bool(RETF.match(text)), tab, jmp,
                jmp is not None and bool(SETTLE.match(jmp)),
                jmp is not None and bool(FARLIT.match(jmp)),
                kind, mtgt, jcc, delta(text),
                m.group(1) if m else None, bool(CALLFAR.match(text)))
        return got

    def _macinvoke(self, text):
        """(kind, target) if this line invokes a branching macro."""
        m = MACINV.match(text)
        if m and m.group(1) in self.macro:
            return self.macro[m.group(1)], m.group(2)
        return None, None

    def _tables(self):
        for u in self.units:
            for idx, text, _ in u.body:
                if not text:
                    continue
                m = DWORDS.match(text)
                if m and u.owner[idx]:
                    row = self.tables.setdefault(u.owner[idx], [])
                    for w in IDENT_DOT.findall(m.group(1)):
                        row.append(w)

    def _bump(self, d, k):
        d[k] = d.get(k, 0) + 1

    def _scan(self):
        for u in self.units:
            for idx, text, raw in u.body:
                if "STKBALANCE-" in raw:     # all three markers carry it
                    m = NET.search(raw)
                    if m and u.owner[idx]:
                        self.nets.setdefault(u.owner[idx], int(m.group(1)))
                    if "STKBALANCE-OK" in raw and u.owner[idx]:
                        self.skip.add(u.owner[idx])
                    if LOOPMARK.search(raw) and u.owner[idx]:
                        self.loops.add(u.owner[idx])
                if not text:
                    continue
                if DWORDS.match(text):
                    # a `dw` row names its arms WHOLE: `dw disp.arm1` takes
                    # the local's address, not the owner's.
                    for w in IDENT_DOT.findall(text):
                        self._bump(self.addressed, w)
                    continue
                m = CALL.match(text)
                if m:
                    self._bump(self.called, m.group(1).lstrip("["))
                    continue
                kind, tgt = self._macinvoke(text)
                if kind is not None:
                    self._bump(self.called if kind == "call" else self.jumped,
                               tgt)
                    continue
                m = JMPTAB.match(text)
                if m:
                    for w in IDENT.findall(m.group(1)):
                        if w in self.tables:
                            self.jumptabs.add(w)
                    continue
                m = JMP.match(text)
                if m:
                    self._bump(self.jumped, m.group(1))
                    continue
                m = JCC.match(text)
                if m:
                    self._bump(self.jumped, m.group(2))
                    continue
                # A `%define` is a textual ALIAS and takes no address: fdlg.inc's
                # `%define fdk_di kretc_di` makes kern_big's `jmp fdk_di` the
                # kernel's own epilogue, and read as a mention it entered BOTH
                # ladders at depth 0 and reported their pops.  What an alias
                # names is still walked through its uses.
                if PPDEF.match(text):
                    continue
                # anything else that names a label takes its address: a vector
                # table, a `dw handler`, a `mov ax, proc`.  Conservative on
                # purpose - an addressed label may be entered at depth 0.
                for w in IDENT.findall(text):
                    self._bump(self.addressed, w)

    def arms(self):
        """Every label reached only as an arm of a dispatched jump table."""
        out = set()
        for tab in self.jumptabs:
            for t in self.tables.get(tab, ()):
                if t in self.gindex:
                    out.add(t)
        return out

    def entries(self):
        """Globals that may be entered at depth 0.  A global that is ONLY ever
        jumped to - directly, or as the arm of a dispatched jump table - is a
        continuation and is deliberately not in this list."""
        if self._entries is None:
            arms = self.arms()
            out = []
            for name, (u, idx) in self.gindex.items():
                if name in self.skip or name in self.nets:
                    continue
                if name in arms and not self.called.get(name):
                    continue
                # A rung the ladder macro emitted is a continuation unless
                # something calls it or takes its address: nothing jumping to
                # it means only that this package never ends N deep.
                if name in self.rungs and not (self.called.get(name)
                                               or self.addressed.get(name)):
                    continue
                if (self.called.get(name) or self.addressed.get(name)
                        or not self.jumped.get(name)):
                    out.append(name)
            self._entries = sorted(out)
        return self._entries

    def resolve(self, u, idx, tgt):
        """Where a jump goes: (unit, idx) or None if we cannot see it."""
        if tgt.startswith("."):
            owner = u.owner[idx]
            if owner is not None and (owner, tgt) in u.llab:
                return (u, u.llab[(owner, tgt)])
            return None
        if tgt in u.glab:
            return (u, u.glab[tgt])
        if tgt in self.gindex:
            return self.gindex[tgt]
        # `font_char.chok` - a local named through its owner.  NASM allows it
        # and this tree uses it nine times, all of them inside the routine
        # that owns the label, which is why it must resolve rather than read
        # as a tail call into another routine.
        if "." in tgt:
            own, _, loc = tgt.partition(".")
            where = self.gindex.get(own)
            if where is not None:
                ou = where[0]
                if (own, "." + loc) in ou.llab:
                    return (ou, ou.llab[(own, "." + loc)])
        return None


def walk(corp, name):
    """Propagate depth from one entry.  Returns (findings, suppressed, hit_cap).

    A finding is (path, idx, entry, text, why, exit): `idx` the body line it is
    AT, and `exit` the (path, idx) of the branch by which this walk first LEFT
    the entry's own routine to get there (None while it is still inside it).

    WHY THE EXIT IS PART OF THE STATE.  A shared epilogue - `vp_ld7`, a pop
    ladder - is reached from many routines and from several exits of one.  Two
    broken jumps into it each end at the SAME `ret` at the same wrong depth, and
    while the walk pruned a label once seen at a depth and findings were
    de-duplicated by (file, line text, why), they were ONE finding: Video
    seeded two broken `jmp vp_ld7`s and this reported one, naming the `ret` and
    neither jump.  The same keying folded two IDENTICAL broken lines in two
    routines (`jmp mc_pop_6` is the same text at every site) into one.  So a
    label is visited once per EXIT, the finding carries the exit, and it is
    keyed by line NUMBER: N wrong jumps are N findings, each naming its jump.
    Depth agreement is still checked across exits - two exits arriving at one
    label at different depths is the cross-jump this gate exists for."""
    u0, i0 = corp.gindex[name]
    findings = []
    suppressed = 0
    seen = {}                       # (unit_path, idx) -> first depth
    visited = set()                 # (unit_path, idx, exit)
    work = [(u0, i0, 0, None)]
    seen[(u0.path, i0)] = 0
    visited.add((u0.path, i0, None))
    steps = 0
    seen_conflict = False

    def push(u, j, d, src, ex):
        """src = (unit, idx) the edge comes FROM.  A label reached at two
        depths is reported UNLESS the routine that owns it carries
        `; STKBALANCE-LOOP:` - the push-N/pop-N loop whose count lives in a
        register.  It used to be "unless the edge is backward inside one
        routine", and that rule is statically indistinguishable from
        `push cx / .l: pop cx / loop .l / ret`, whose second turn pops the
        return address: the loop target dominates the source in both.  A
        tail-merged epilogue one register deep hid behind it too.  So the
        exemption is a name on the routine, and the count of conflicts taken
        under one is printed.

        A branch whose target another routine owns is this walk's EXIT the
        first time one is taken (see walk), and the exit rides on the path."""
        nonlocal suppressed, seen_conflict
        if j >= len(u.body):
            return                  # off the end of the file: nothing runs
        su, si = src
        if ex is None and u.owner[j] != su.owner[si] and (
                u is not su or j != si + 1):
            ex = (su.path, si)
        key = (u.path, j)
        if key in seen and seen[key] != d:
            if not seen_conflict:
                if u.owner[j] in corp.loops:
                    suppressed += 1
                else:
                    # ...naming the routine that OWNS the label when the walk
                    # entered somewhere else, because that is where a marker
                    # would go: hd_utoa jumps into hd_utoa_body's loop.
                    own = u.owner[j]
                    findings.append((su.path, si, name, su.body[si][2].strip(),
                                     "reached at depth %+d and %+d%s"
                                     % (seen[key], d,
                                        "" if own in (None, name)
                                        else " (label owned by %s)" % own),
                                     None))
                    seen_conflict = True
            return
        vkey = (u.path, j, ex)
        if vkey in visited:
            return
        seen.setdefault(key, d)
        visited.add(vkey)
        work.append((u, j, d, ex))

    while work:
        if steps > WALK_MAX:
            return findings, suppressed, True
        steps += 1
        u, i, d, ex = work.pop()
        if i >= len(u.body):
            continue
        # An exempted routine is exempt however it is REACHED, not only when it
        # is the entry: `task_exit` abandons the stack and its `iret` belongs to
        # the next task, so a walk that tail-jumps into it must stop rather than
        # measure that iret against our entry.
        if u.owner[i] in corp.skip and i != i0:
            continue
        _, text, raw = u.body[i]
        if not text:
            push(u, i + 1, d, (u, i), ex)
            continue
        (data, isret, isretf, tab, jmp, settle, farlit, kind, mtgt, jcc,
         dd, calltgt, callfar) = corp.info(text)
        if data:
            continue

        if isret:
            # `push seg / push off / retf` is a constructed FAR JUMP: the retf
            # eats exactly the two words just pushed, so +2 here is the idiom.
            if isretf and d == 2 and _two_pushes_before(u, i):
                continue
            if d != 0:
                findings.append((u.path, i, name, raw.strip(),
                                 "ret at depth %+d" % d, ex))
            continue

        if tab is not None:
            hit = [w for w in tab if w in corp.tables]
            if hit:
                for t in corp.tables[hit[0]]:
                    # a global, or `owner.local` through resolve()'s fallback
                    where = corp.resolve(u, i, t)
                    if where is not None:
                        push(where[0], where[1], d, (u, i), ex)
                continue

        if jmp is not None:
            tgt = jmp
            if settle:                      # `jmp short $+2` - an I/O settle
                push(u, i + 1, d, (u, i), ex)
                continue
            if farlit:
                # `jmp 0xFFFF:0x0000` - the reset vector, or any LITERAL
                # SEG:OFF.  No routine this walk can see is there, so nothing
                # will ever pop what is left: the stack is the machine's, and
                # abandoned with it (disk.inc's dsk_fdd_park_x warm reset)
                continue
            where = corp.resolve(u, i, tgt)
            if where is None:
                # a computed jump, or a label we cannot see.  Only a jump that
                # leaves depth behind is worth saying anything about.
                if d != 0 and not tgt.startswith((".", "$", "[")):
                    findings.append((u.path, i, name, raw.strip(),
                                     "tail jmp to %s at depth %+d" % (tgt, d),
                                     ex))
                continue
            push(where[0], where[1], d, (u, i), ex)
            continue

        nxts = [(u, i + 1)]
        if kind == "jmp":
            # `RAE wvm_aoob` is `jb %%o / jmp wvm_aoob / %%o:` - a branch to
            # the target at this depth, and a fallthrough past it.
            where = corp.resolve(u, i, mtgt)
            if where is not None:
                nxts.append(where)
        elif jcc is not None:
            where = corp.resolve(u, i, jcc)
            if where is not None:
                nxts.append(where)

        if dd is None:
            dd = 0
            if kind == "call" and mtgt in corp.nets:
                # `FDENT kentc_di` (fdlg.inc) is `call %1` - a macro call to
                # a declared banking routine banks exactly what the call does
                dd = corp.nets[mtgt]
            elif calltgt is not None:
                if calltgt in corp.nets:
                    dd = corp.nets[calltgt]         # a declared banking pair
                elif callfar and _pushf_before(u, i):
                    # chaining an interrupt: our pushf is the FLAGS the chained
                    # handler's iret pops, so the pair is net zero.
                    dd = -1
        for uu, j in nxts:
            push(uu, j, d + dd, (u, i), ex)
    return findings, suppressed, False


def _prev_code(u, i, n):
    """The n code lines before index i, nearest first (comments skipped)."""
    out = []
    j = i - 1
    while j >= 0 and len(out) < n:
        t = u.body[j][1]
        if t:
            out.append(t)
        j -= 1
    return out


def _two_pushes_before(u, i):
    p = _prev_code(u, i, 2)
    return len(p) == 2 and all(PUSH.match(t) for t in p)


def _pushf_before(u, i):
    p = _prev_code(u, i, 1)
    return len(p) == 1 and PUSHF.match(p[0])


def routines(path):
    """Kept for callers that want one file's routines: ([(name, body, labels)],
    skipped).  The walk itself no longer uses this - it walks the corpus."""
    u = Unit(path)
    out = []
    names = sorted(u.glab.items(), key=lambda kv: kv[1])
    for k, (name, start) in enumerate(names):
        end = names[k + 1][1] if k + 1 < len(names) else len(u.body)
        body = [(n, t, r) for n, (_, t, r) in enumerate(u.body[start:end])]
        labels = {lab: idx - start for (own, lab), idx in u.llab.items()
                  if own == name and start <= idx < end}
        out.append((name, body, labels))
    return out, 0


def main():
    args = sys.argv[1:]
    if args:
        files = args
    else:
        files = sorted(glob.glob(os.path.join(ROOT, "apps", "*.inc")) +
                       glob.glob(os.path.join(ROOT, "apps", "*", "*.asm")) +
                       glob.glob(os.path.join(ROOT, "kernel", "*.inc")))
    corp = Corpus(files)
    units = {u.path: u for u in corp.units}
    # (path, idx, why) -> {exit: (entry, text)}, in the order first found.
    # One LINE reached wrongly by two exits is two findings (see walk); the
    # same line found again from a second ENTRY by the same exit is one.  And
    # when a line is wrong from INSIDE the routine that owns it (exit None),
    # every jump that reaches it is that defect's echo, not a defect of its
    # own, so only the inside finding is kept.
    groups, sup, capped = {}, 0, 0
    for name in corp.entries():
        got, n, hit = walk(corp, name)
        for path, idx, entry, text, why, ex in got:
            row = groups.setdefault((path, idx, why), {})
            row.setdefault(ex, (entry, text))
        sup += n
        capped += 1 if hit else 0

    def where(path, idx):
        return "%s:%d" % (os.path.relpath(path, ROOT), units[path].lineno[idx])

    total = 0
    for (path, idx, why), row in groups.items():
        if None in row:
            entry, text = row[None]
            total += 1
            print("%s: %s: %s\n    %s" % (where(path, idx), entry, why, text))
            continue
        exits = list(row.items())
        total += len(exits)
        print("%s: %s, by %d exit(s):\n    %s"
              % (where(path, idx), why, len(exits), exits[0][1][1]))
        for (epath, eidx), (entry, _) in exits:
            print("      %s left by %s: %s"
                  % (entry, where(epath, eidx),
                     units[epath].body[eidx][2].strip()))
    print("stkbalance: %d unbalanced path(s)"
          "  (%d entries walked, %d declared banking routines,"
          " %d exempted, %d conflicts inside %d STKBALANCE-LOOP routines,"
          " %d walks capped, %d names defined twice)"
          % (total, len(corp.entries()), len(corp.nets),
             len(corp.skip), sup, len(corp.loops), capped, corp.dup))
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
