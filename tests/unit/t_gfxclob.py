#!/usr/bin/env python3
"""Nothing reads AX..DX after a LINE drawing cell before writing it.

    python3 tests/unit/t_gfxclob.py            # the gate
    python3 tests/unit/t_gfxclob.py --report   # every finding and every blind
                                               # spot, allowlisted ones too
    python3 tests/unit/t_gfxclob.py --fixlist  # site -> registers it must keep
    GFXCLOB_SET="FILL=ax,bx,cx,dx;..." ...     # price ANOTHER contract

SPEC.md 5.7.3 (ABI-BREAK-LIST V1): OSAPI_GFX_PIXEL, _HLINE and _VLINE CLOBBER
AX, BX, CX AND DX - they used to preserve every register, and the push/pops
and call that cost (~55-80 clocks of a ~2,300-clock small call) are gone.
A package that kept a coordinate in one of the four across such a call now
draws the NEXT thing in the wrong place, silently, and only on the cells and
the builds whose body happens to spend that register. So the contract is
held at the SITE, here, by a forward walk:

  from every call to a line cell the four registers (as eight byte halves)
  are JUNK, and every path out of the call must WRITE a half before anything
  READS it.

FILL and FRAME keep their registers (5.7.3.2) and are walked as calls that
clobber nothing; CLOBBER below is the contract, and GFXCLOB_SET prices a
different one without touching a source line (all five clobbering AX..DX:
~260 sites to change, against 64 for the three).

The kernel is walked for its FAR calls into the same cells (ALIAS): modules
and .cold reach a line through its API cell, and cw_gfx_hline IS that cell.

The walk follows fall-through, every conditional and unconditional branch to
a label (locals scoped as NASM scopes them), both arms of every %if, and
near calls INTO a routine (the callee's reads count; its writes at each `ret`
carry back to the instruction after the call). A cell called later reads the
registers its os88api.inc line names as inputs and writes its documented
outputs. A `ret` with junk still live is followed into every CALLER of the
routine it ends (same file, same package directory, every package for a
shared apps/*.inc); a routine nothing calls by name is a callback, and a
window callback may clobber AX..DX (SPEC.md 13.7). What the walk cannot
see - an indirect call or jump, a far call that is not a cell, an `int`, a
macro it does not know, a depth limit - is a finding of its own kind, so
`--report` says how complete the audit is rather than implying it.

A finding that is not a defect goes in tests/gfxclob.txt with its reason:
`path routine CELL  reason` - the file, the label the CALL sits under and
the cell (or the macro) it names, so a line moving does not stale it. The
list only turns one way.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
ALLOW = os.path.join(ROOT, "tests", "gfxclob.txt")
CELLS = ("FILL", "PIXEL", "HLINE", "VLINE", "FRAME")
CELL_RE = re.compile(r"^OSAPI_GFX_(%s)$" % "|".join(CELLS))
# The KERNEL's own far calls INTO the same cells - modules (CTRL.DRV,
# WIN95.DRV) and .cold reach a line through its API cell, so they meet the
# package's contract, and kernel/ is walked for them alone.
ALIAS = {"KERNEL_SEG:apic_gfx_hline": "HLINE",
         "KERNEL_SEG:cw_gfx_hline": "HLINE",
         "ct_cw_gfx_hline": "HLINE",
         "KERNEL_SEG:apic_gfx_vline": "VLINE",
         "KERNEL_SEG:apic_gfx_pixel": "PIXEL",
         "KERNEL_SEG:apic_gfx_fill": "FILL",
         "KERNEL_SEG:apic_gfx_frame": "FRAME"}
ALIAS_RE = "|".join(re.escape(a) for a in ALIAS)


def cell_name(t):
    """The cell a call target names, or None."""
    m = CELL_RE.match(t)
    if m:
        return m.group(1)
    return ALIAS.get(t.replace(" ", ""))
DIRS = ("apps", "drivers", "tests", "kernel")

HALVES = ("al", "ah", "bl", "bh", "cl", "ch", "dl", "dh")
REG16 = {"ax": ("al", "ah"), "bx": ("bl", "bh"),
         "cx": ("cl", "ch"), "dx": ("dl", "dh")}
REG8 = {h: (h,) for h in HALVES}
REGS = dict(REG16)
REGS.update(REG8)
ALL = frozenset(HALVES)
# What each cell clobbers (SPEC.md 5.7.3). A cell missing here preserves.
CLOBBER = {c: ALL for c in ("PIXEL", "HLINE", "VLINE")}
if os.environ.get("GFXCLOB_SET"):            # an experiment: "PIXEL=ax,bx;..."
    CLOBBER = {}
    for part in os.environ["GFXCLOB_SET"].split(";"):
        c, regs = part.split("=")
        CLOBBER[c] = frozenset(h for r in regs.split(",") for h in REG16[r]) \
            if regs else frozenset()
TOKRE = re.compile(r"\b(ax|bx|cx|dx|al|ah|bl|bh|cl|ch|dl|dh)\b", re.I)

MAXDEPTH = 8          # nested calls / caller climbs
MAXSTEPS = 20000      # instructions per walk from one site


# ---------------------------------------------------------------- the source
def strip_comment(line):
    if "'" not in line and '"' not in line and "`" not in line:
        return line.split(";", 1)[0].strip()
    out = []
    q = None
    for ch in line:
        if q:
            out.append(ch)
            if ch == q:
                q = None
        elif ch in "'\"`":
            q = ch
            out.append(ch)
        elif ch == ";":
            break
        else:
            out.append(ch)
    return "".join(out).strip()


def split_ops(s):
    if not any(c in s for c in "[(\'\"`"):
        return [o.strip() for o in s.split(",") if o.strip() != ""]
    ops, depth, cur, q = [], 0, [], None
    for ch in s:
        if q:
            cur.append(ch)
            if ch == q:
                q = None
            continue
        if ch in "'\"`":
            q = ch
        elif ch in "[(":
            depth += 1
        elif ch in "])":
            depth -= 1
        elif ch == "," and depth == 0:
            ops.append("".join(cur).strip())
            cur = []
            continue
        cur.append(ch)
    if cur or ops:
        ops.append("".join(cur).strip())
    return [o for o in ops if o != ""]


LABEL_RE = re.compile(r"^([.A-Za-z_?@$][\w.?@$#~]*)\s*:(.*)$")
PREFIXES = {"rep", "repe", "repz", "repne", "repnz", "lock"}
SEGPFX = {"es", "cs", "ss", "ds"}


class Src:
    """One file: instructions, labels and the %if structure."""

    def __init__(self, path, text=None, rel=None):
        self.path = path
        self.rel = rel or os.path.relpath(path, ROOT)
        if text is None:
            with open(path, encoding="latin-1") as f:
                text = f.read()
        raw = text.split("\n")
        self.raw = raw
        self.ins = []          # per line: None or (mnemonic, ops, text)
        self.labels = {}       # full name -> line
        self.glob_at = []      # per line: the global label in scope
        self.ifs = {}          # %if/%elif/%else line -> next arm line
        self.endif = {}        # arm-start line -> its %endif line
        self.macro_body = set()
        self.macros = []       # (NAME, [body lines])
        self.data = set()      # lines that are data or a stop
        glob = ""
        stack = []
        in_macro = 0
        for i, line in enumerate(raw):
            s = strip_comment(line)
            self.ins.append(None)
            low = s.lower()
            if low.startswith("%macro") or low.startswith("%imacro"):
                in_macro += 1
                if in_macro == 1:
                    mname = s.split()[1].upper() if len(s.split()) > 1 else ""
                    self.macros.append((mname, []))
                self.macro_body.add(i)
                self.glob_at.append(glob)
                continue
            if low.startswith("%endmacro"):
                in_macro = max(0, in_macro - 1)
                self.macro_body.add(i)
                self.glob_at.append(glob)
                continue
            if in_macro:
                self.macro_body.add(i)
                if self.macros:
                    self.macros[-1][1].append(s)
            m = LABEL_RE.match(s)
            if m and not s.startswith("%"):
                name = m.group(1)
                if name.startswith(".") and not name.startswith(".."):
                    full = glob + name
                else:
                    if not name.startswith(".."):
                        glob = name
                    full = name
                if not in_macro:
                    self.labels.setdefault(full, i)
                s = m.group(2).strip()
                low = s.lower()
            self.glob_at.append(glob)
            if not s:
                continue
            if low.startswith("%if"):
                stack.append([i])
                continue
            if low.startswith("%elif") or low.startswith("%else"):
                if stack:
                    stack[-1].append(i)
                continue
            if low.startswith("%endif"):
                if stack:
                    arms = stack.pop()
                    for a, b in zip(arms, arms[1:] + [i]):
                        self.ifs[a] = b
                    for a in arms:
                        self.endif[a] = i
                continue
            if s.startswith("%"):
                if low.startswith("%include"):
                    self.data.add(i)
                continue
            parts = s.split(None, 1)
            mn = parts[0].lower()
            rest = parts[1] if len(parts) > 1 else ""
            while mn in PREFIXES or mn in SEGPFX:
                if not rest:
                    break
                p2 = rest.split(None, 1)
                if mn in PREFIXES:
                    pref = mn
                    mn = p2[0].lower()
                    rest = p2[1] if len(p2) > 1 else ""
                    if pref.startswith("rep"):
                        mn = "rep:" + mn
                else:
                    mn = p2[0].lower()
                    rest = p2[1] if len(p2) > 1 else ""
            if mn in ("db", "dw", "dd", "dq", "times", "resb", "resw",
                      "resd", "equ", "align", "alignb", "section",
                      "segment", "org", "bits", "cpu", "struc",
                      "endstruc", "istruc", "iend", "at", "incbin",
                      "absolute", "global", "extern"):
                self.data.add(i)
                continue
            # `name db ...` without a colon
            if rest.split() and rest.split(None, 1)[0].lower() in (
                    "db", "dw", "dd", "times", "resb", "resw", "equ"):
                self.data.add(i)
                continue
            self.ins[i] = (mn, split_ops(rest), s)

    def resolve(self, name, line):
        if name.startswith(".") and not name.startswith(".."):
            return self.labels.get(self.glob_at[line] + name)
        return self.labels.get(name)


# --------------------------------------------------------------- the cells
def cell_regs():
    """name -> (reads, writes) from apps/os88api.inc's %define comments.

    The comment of a cell is the text after its %define and every
    comment-only line under it. Inputs are register names before the first
    `out`; outputs (and `clobbers`) after it. Coarse on purpose: a register
    named in a cell's comment is read by it, so the walk errs towards a
    finding."""
    path = os.path.join(ROOT, "apps", "os88api.inc")
    lines = open(path, encoding="latin-1").read().split("\n")
    out = {}
    i = 0
    dre = re.compile(r"^%define\s+(OSAPI_\w+)\s+[^;]*;?(.*)$")
    while i < len(lines):
        m = dre.match(lines[i])
        if not m:
            i += 1
            continue
        name, text = m.group(1), m.group(2)
        j = i + 1
        while j < len(lines) and re.match(r"^\s{8,};", lines[j]):
            text += " " + lines[j].split(";", 1)[1]
            j += 1
        low = text.lower()
        cut = len(low)
        for w in ("out:", "out ", "returns", "-> ", "clobbers"):
            k = low.find(w)
            if 0 <= k < cut:
                cut = k
        reads, writes = set(), set()
        for t in TOKRE.finditer(low[:cut]):
            reads.update(REGS[t.group(1).lower()])
        for t in TOKRE.finditer(low[cut:]):
            writes.update(REGS[t.group(1).lower()])
        if re.search(r"clobbers? (all|ax bx cx dx|every)", low):
            writes.update(HALVES)
        out[name] = (frozenset(reads), frozenset(writes - reads))
        i = j
    return out


# ------------------------------------------------------- one instruction
def regs_in(op):
    s = set()
    for t in TOKRE.finditer(op):
        s.update(REGS[t.group(1).lower()])
    return s


def is_reg(op):
    return op.lower() in REGS


def effect(mn, ops):
    """(reads, writes) of AX..DX halves; None = not modelled."""
    r, w = set(), set()
    addr = set()
    for o in ops:
        if "[" in o:
            addr |= regs_in(o[o.index("["):])
    r |= addr
    base = mn[4:] if mn.startswith("rep:") else mn
    if mn.startswith("rep:"):
        r |= {"cl", "ch"}
        w |= {"cl", "ch"}
    d = ops[0] if ops else ""
    sreg = ops[1] if len(ops) > 1 else ""
    dr = set(REGS[d.lower()]) if is_reg(d) else set()
    sr = set(REGS[sreg.lower()]) if is_reg(sreg) else set()
    if base in ("mov",):
        r |= sr if sr else regs_in(sreg) if "[" not in sreg else set()
        w |= dr
    elif base in ("lea", "les", "lds"):
        w |= dr
    elif base in ("xor", "sub", "sbb") and is_reg(d) and d.lower() == sreg.lower():
        if base == "sbb":
            pass
        w |= dr
    elif base in ("add", "adc", "sub", "sbb", "and", "or", "xor"):
        r |= dr | sr
        w |= dr
    elif base in ("cmp", "test"):
        r |= dr | sr
    elif base in ("inc", "dec", "neg", "not"):
        r |= dr
        w |= dr
    elif base in ("shl", "shr", "sal", "sar", "rol", "ror", "rcl", "rcr"):
        r |= dr | sr
        w |= dr
    elif base == "xchg":
        r |= dr | sr
        w |= dr | sr
    elif base == "push":
        r |= dr
    elif base == "pop":
        w |= dr
    elif base in ("mul", "imul"):
        if d.lower() in REG8 or "byte" in d.lower():
            r |= {"al"} | dr
            w |= {"al", "ah"}
        else:
            r |= {"al", "ah"} | dr
            w |= {"al", "ah", "dl", "dh"}
    elif base in ("div", "idiv"):
        if d.lower() in REG8 or "byte" in d.lower():
            r |= {"al", "ah"} | dr
            w |= {"al", "ah"}
        else:
            r |= {"al", "ah", "dl", "dh"} | dr
            w |= {"al", "ah", "dl", "dh"}
    elif base == "cbw":
        r |= {"al"}
        w |= {"ah"}
    elif base == "cwd":
        r |= {"al", "ah"}
        w |= {"dl", "dh"}
    elif base in ("lodsb",):
        w |= {"al"}
    elif base in ("lodsw",):
        w |= {"al", "ah"}
    elif base in ("stosb", "scasb"):
        r |= {"al"}
    elif base in ("stosw", "scasw"):
        r |= {"al", "ah"}
    elif base in ("movsb", "movsw", "cmpsb", "cmpsw", "clc", "stc", "cmc",
                  "cld", "std", "cli", "sti", "nop", "pushf", "popf",
                  "sahf", "hlt", "wait", "fwait"):
        if base == "sahf":
            r |= {"ah"}
    elif base == "lahf":
        w |= {"ah"}
    elif base == "xlat" or base == "xlatb":
        r |= {"al", "bl", "bh"}
        w |= {"al"}
    elif base == "in":
        r |= sr
        w |= dr
    elif base == "out":
        r |= dr | sr
    elif base in ("aaa", "aas", "daa", "das"):
        r |= {"al", "ah"}
        w |= {"al", "ah"}
    elif base in ("aam", "aad"):
        r |= {"al", "ah"}
        w |= {"al", "ah"}
    else:
        return None
    return r, w


JCC = {"jo", "jno", "jb", "jnae", "jc", "jnb", "jae", "jnc", "je", "jz",
       "jne", "jnz", "jbe", "jna", "jnbe", "ja", "js", "jns", "jp", "jpe",
       "jnp", "jpo", "jl", "jnge", "jnl", "jge", "jle", "jng", "jnle", "jg",
       "jcxz", "loop", "loope", "loopz", "loopne", "loopnz"}


def cond_of(line):
    """(symbol, value) a %if line's FIRST arm means, or None."""
    t = strip_comment(line).split()
    if len(t) < 2:
        return None
    k = t[0].lower()
    if k == "%ifdef":
        return (t[1], True)
    if k == "%ifndef":
        return (t[1], False)
    if k == "%if":
        return (" ".join(t[1:]), True)
    return None


def target(op):
    t = op.strip()
    for k in ("short ", "near ", "strict "):
        while t.lower().startswith(k):
            t = t[len(k):].strip()
    return t


# ------------------------------------------------------------- the walk
class Walker:
    def __init__(self, files, cells):
        self.files = files
        self.cells = cells
        self.bydir = {}
        for f in files.values():
            self.bydir.setdefault(os.path.dirname(f.path), []).append(f)
        self.callers_cache = {}
        self.fn_memo = {}
        self.esc_memo = {}
        self.macros = {}
        for f in files.values():
            for name, body in f.macros:
                self.macros.setdefault(name, []).append(body)
        self.draw_macros = {n for n, bodies in self.macros.items()
                            if any(self.body_draws(b) for b in bodies)}

    def body_draws(self, body, seen=None):
        seen = seen or set()
        for line in body:
            parts = line.split(None, 1)
            if not parts:
                continue
            if parts[0].lower() == "call" and len(parts) > 1 and \
                    cell_name(target(parts[1].strip())):
                return True
            n = parts[0].upper()
            if n in self.macros and n not in seen:
                seen.add(n)
                if any(self.body_draws(b, seen) for b in self.macros[n]):
                    return True
        return False

    def macro_effect(self, name, ops, junk, src, i, text):
        """A macro invocation: (reads, writes, draws). Reads are the union
        over every definition, read linearly; writes count only before the
        first branch or conditional of a body, and only when every
        definition makes them. A call inside to anything but a cell is
        reported, being out of this walk's sight."""
        reads, writes = set(), None
        draws = name in self.draw_macros
        for body in self.macros[name]:
            w = set()
            live = set(junk)
            stop = False
            for line in body:
                t = line
                for k in range(9, 0, -1):
                    t = t.replace("%%%d" % k, ops[k - 1] if k <= len(ops)
                                  else "")
                if not t or t.startswith(";"):
                    continue
                if t.startswith("%"):
                    stop = True
                    continue
                m = LABEL_RE.match(t)
                if m:
                    stop = True
                    t = m.group(2).strip()
                    if not t:
                        continue
                parts = t.split(None, 1)
                mn = parts[0].lower()
                mops = split_ops(parts[1]) if len(parts) > 1 else []
                if mn == "call":
                    tg = target(mops[0]) if mops else ""
                    if cell_name(tg):
                        live -= CLOBBER.get(cell_name(tg),
                                            frozenset())
                        continue
                    if tg in self.cells:
                        rd, wr = self.cells[tg]
                        reads |= rd & live
                        if not stop:
                            w |= wr
                        live -= wr
                        continue
                    if live:
                        self.report("macro-call", src, i, text + "  [" + t + "]")
                    continue
                if mn in JCC or mn in ("jmp", "ret", "retf"):
                    stop = True
                    continue
                if mn.upper() in self.macros:
                    if live:
                        self.report("macro-call", src, i, text + "  [" + t + "]")
                    continue
                eff = effect(mn, mops) if mn not in ("push", "pop") else None
                if mn == "pop" and mops and mops[0].lower() in REG16:
                    eff = (set(), set(REG16[mops[0].lower()]))
                if mn == "push" and mops:
                    eff = (regs_in(mops[0]), set())
                if eff is None:
                    continue
                rd, wr = eff
                reads |= set(rd) & live
                live -= set(wr)
                if not stop:
                    w |= set(wr)
            writes = w if writes is None else writes & w
        return reads, writes or set(), draws

    def find_label(self, src, name, line):
        n = src.resolve(name, line)
        if n is not None:
            return src, n
        if name.startswith("."):
            return None
        for f in self.bydir.get(os.path.dirname(src.path), []):
            if name in f.labels:
                return f, f.labels[name]
        for f in self.files.values():
            if os.path.dirname(f.path) == os.path.join(ROOT, "apps") and \
                    name in f.labels:
                return f, f.labels[name]
        return None

    def callers(self, src, label):
        key = (src.path, label)
        if key in self.callers_cache:
            return self.callers_cache[key]
        shared = os.path.dirname(src.path) in (
            os.path.join(ROOT, "apps"), os.path.join(ROOT, "drivers"))
        pool = self.files.values() if shared else \
            self.bydir.get(os.path.dirname(src.path), [])
        res = []
        pat = re.compile(r"^\s*(?:[.\w]+:\s*)?(call|jmp)\s+(?:near\s+|short\s+)?"
                         + re.escape(label) + r"\s*(?:;|$)", re.I)
        for f in pool:
            for i, line in enumerate(f.raw):
                if label in line and pat.match(line) and \
                        i not in f.macro_body:
                    res.append((f, i, pat.match(line).group(1).lower()))
        self.callers_cache[key] = res
        return res

    def site(self, src, line):
        """Walk from one call site; returns a list of (kind, where, text)."""
        self.found = []
        self.steps = 0
        self.seen = set()
        self.walk(src, line + 1, self.site_clobber(src, line), 0, (),
                  top=True)
        return self.found

    def cell_in(self, text):
        m = re.search(r"OSAPI_GFX_(%s)\b" % "|".join(CELLS), text)
        if m:
            return m.group(1)
        m = re.search(r"\bcall\s+(%s)\b" % ALIAS_RE, text)
        return ALIAS[m.group(1)] if m else None

    def site_clobber(self, src, line):
        c = self.cell_in(src.raw[line].split(";")[0])
        if c is None:                   # a macro: the cells in its bodies
            ins = src.ins[line]
            out = frozenset()
            for body in self.macros.get(ins[0].upper(), []):
                for b in body:
                    c2 = self.cell_in(b.split(";")[0])
                    if c2:
                        out |= CLOBBER.get(c2, frozenset())
            return out
        return CLOBBER.get(c, frozenset())

    def report(self, kind, src, i, text):
        self.found.append((kind, "%s:%d" % (src.rel, i + 1), text))

    def walk(self, src, i, junk, depth, pstk, top):
        """Forward from src line i with `junk` halves live.

        pstk: what this walk has PUSHED, a tuple of one entry a word, each
        entry the junk halves of the word ('l', 'h'). A pop restores them,
        and a pop past the bottom is the value from before the drawing call
        - a caller's own `push ax / call OSAPI_GFX_FILL / pop ax` is clean
        by construction."""
        work = [(src, i, junk, tuple(pstk), frozenset())]
        while work:
            src, i, junk, pstk, conds = work.pop()
            while True:
                if not junk and not any(pstk):
                    break
                key = (src.path, i, junk, pstk)
                if key in self.seen:
                    break
                self.seen.add(key)
                self.steps += 1
                if self.steps > MAXSTEPS:
                    self.report("limit", src, i, "step limit")
                    return
                if i >= len(src.raw):
                    break
                if i in src.data:
                    break
                if i in src.ifs and src.raw[i].strip().lower().startswith("%if"):
                    # fork: this arm, and each later arm - but a symbol
                    # this path has already decided decides it again, or a
                    # push under %ifdef X pairs with a pop under %ifndef X
                    cnd = cond_of(src.raw[i])
                    taken = dict(conds).get(cnd[0]) if cnd else None
                    if taken is not None and taken != cnd[1]:
                        nxt = src.ifs[i]
                        if src.raw[nxt].strip().lower().startswith("%endif"):
                            i = nxt + 1
                        else:
                            i = nxt + 1
                        continue
                    newc = conds | {cnd} if cnd else conds
                    if taken is None:
                        a = i
                        alt = conds | {(cnd[0], not cnd[1])} if cnd else conds
                        while a in src.ifs:
                            nxt = src.ifs[a]
                            if src.raw[nxt].strip().lower().startswith("%endif"):
                                work.append((src, nxt + 1, junk, pstk, alt))
                                break
                            work.append((src, nxt + 1, junk, pstk, alt))
                            a = nxt
                    conds = newc
                    i += 1
                    continue
                if i in src.ifs:        # an %elif/%else reached from above
                    i = src.endif.get(i, i) + 1
                    continue
                ins = src.ins[i]
                if ins is None:
                    i += 1
                    continue
                mn, ops, text = ins
                if i in src.macro_body:
                    self.report("macro-body", src, i, text)
                    break
                if mn == "call":
                    t = target(ops[0]) if ops else ""
                    if cell_name(t):
                        # ANOTHER drawing call: what it clobbers is its own
                        # site's finding, so this walk keeps only what it
                        # pushed (a push/pop around that call carries this
                        # site's junk through it)
                        i += 1
                        junk = junk - CLOBBER.get(cell_name(t), frozenset())
                        continue
                    if t.startswith("OSAPI_"):
                        rd, wr = self.cells.get(t, (None, None))
                        if rd is None:
                            self.report("unknown-cell", src, i, text)
                            rd, wr = frozenset(), frozenset()
                        bad = rd & junk
                        if bad:
                            self.report("read", src, i, text + "   <- reads "
                                        + ",".join(sorted(bad)))
                        junk = junk - wr
                        i += 1
                        continue
                    if ":" in t or t.lower().startswith("far") or \
                            "[" in t or is_reg(t) or t.lower() in (
                                "si", "di", "bp"):
                        self.report("indirect", src, i, text)
                        i += 1
                        continue
                    hit = self.find_label(src, t, i)
                    if hit is None:
                        self.report("unresolved", src, i, text)
                        i += 1
                        continue
                    if depth >= MAXDEPTH:
                        self.report("limit", src, i, "depth: " + text)
                        break
                    outs = self.call_into(hit[0], hit[1], junk, depth + 1)
                    for o in outs:
                        work.append((src, i + 1, o, pstk, conds))
                    break
                if mn in ("ret", "retn", "retf", "iret"):
                    if self.in_fn is not None:
                        self.fn_out.add(junk)
                        break
                    if junk:
                        self.escape(src, i, junk, depth)
                    break
                if mn == "jmp":
                    t = target(ops[0]) if ops else ""
                    if ":" in t or "[" in t or is_reg(t) or \
                            t.lower() in ("si", "di", "bp") or \
                            t.lower().startswith("far"):
                        self.report("indirect", src, i, text)
                        break
                    if t.startswith("OSAPI_"):
                        self.report("indirect", src, i, text)
                        break
                    if t.startswith("$"):
                        i += 1                  # `jmp short $+2`: a delay
                        continue
                    hit = self.find_label(src, t, i)
                    if hit is None:
                        self.report("unresolved", src, i, text)
                        break
                    src, i = hit
                    continue
                if mn in JCC:
                    if mn in ("jcxz",) or mn.startswith("loop"):
                        bad = {"cl", "ch"} & junk
                        if bad:
                            self.report("read", src, i, text + "   <- reads "
                                        + ",".join(sorted(bad)))
                        if mn.startswith("loop"):
                            junk = junk - {"cl", "ch"}
                    t = target(ops[0]) if ops else ""
                    hit = self.find_label(src, t, i)
                    if hit is None:
                        self.report("unresolved", src, i, text)
                    else:
                        work.append((hit[0], hit[1], junk, pstk, conds))
                    i += 1
                    continue
                if mn == "int" or mn == "into":
                    self.report("int", src, i, text)
                    i += 1
                    continue
                if mn in ("push", "pushf"):
                    d = ops[0].lower() if ops else ""
                    if d in REG16:
                        lo, hi = REG16[d]
                        e = frozenset(h for h in (lo, hi) if h in junk)
                    else:
                        if "[" in d:
                            bad = regs_in(d[d.index("["):]) & junk
                            if bad:
                                self.report("read", src, i, text +
                                            "   <- reads " +
                                            ",".join(sorted(bad)))
                        e = frozenset()
                    pstk = pstk + (e,)
                    i += 1
                    continue
                if mn in ("pop", "popf"):
                    d = ops[0].lower() if ops else ""
                    e = pstk[-1] if pstk else frozenset()
                    pstk = pstk[:-1]
                    if d in REG16:
                        lo, hi = REG16[d]
                        junk = junk - {lo, hi}
                        if any(h.endswith("l") for h in e):
                            junk = junk | {lo}
                        if any(h.endswith("h") for h in e):
                            junk = junk | {hi}
                        junk = frozenset(junk)
                    elif e:
                        self.report("read", src, i, text + "   <- reads "
                                    + ",".join(sorted(e)) + " (pushed)")
                    i += 1
                    continue
                if mn in ("add", "sub") and ops and ops[0].lower() == "sp":
                    try:
                        n = int(ops[1], 0) // 2
                    except (ValueError, IndexError):
                        n = 0
                    if mn == "add":
                        pstk = pstk[:max(0, len(pstk) - n)]
                    else:
                        pstk = pstk + (frozenset(),) * n
                    i += 1
                    continue
                eff = effect(mn, ops)
                if eff is None and mn.upper() in self.macros:
                    rd, wr, dr = self.macro_effect(mn.upper(), ops, junk,
                                                   src, i, text)
                    bad = rd & junk
                    if bad:
                        self.report("read", src, i, text + "   <- reads "
                                    + ",".join(sorted(bad)))
                    junk = frozenset(junk - bad - wr)
                    if dr:
                        junk = junk - self.site_clobber(src, i)
                    i += 1
                    continue
                if eff is None:
                    self.report("unmodelled", src, i, text)
                    i += 1
                    continue
                rd, wr = eff
                bad = set(rd) & junk
                if bad:
                    self.report("read", src, i, text + "   <- reads "
                                + ",".join(sorted(bad)))
                    junk = junk - bad
                junk = frozenset(junk - set(wr))
                i += 1

    in_fn = None
    fn_out = None

    def call_into(self, src, line, junk, depth):
        """Walk a callee; return the junk sets live at its rets.

        Memoised across sites with the findings it made, which are
        re-reported for every site that reaches it the same way."""
        key = (src.path, line, junk)
        if key in self.fn_memo:
            outs, found = self.fn_memo[key]
            self.found.extend(found)
            return outs
        self.fn_memo[key] = (set(), [])       # recursion guard
        save = (self.in_fn, self.fn_out, self.seen, self.found, self.steps)
        self.in_fn = (src.path, line)
        self.fn_out = set()
        self.seen = set()
        self.found = []
        self.steps = 0
        self.walk(src, line, junk, depth, (), top=False)
        outs, found = self.fn_out, self.found
        self.in_fn, self.fn_out, self.seen, self.found, self.steps = save
        self.found.extend(found)
        self.fn_memo[key] = (outs, found)
        return outs

    def escape(self, src, i, junk, depth):
        """A top-level ret with junk live: the routine's callers inherit."""
        lab = src.glob_at[i]
        if not lab:
            self.report("ret-unknown", src, i, "ret outside any label")
            return
        self.escape_label(src, i, lab, junk, depth)

    def escape_label(self, src, i, lab, junk, depth):
        key = (src.path, lab, junk)
        if key in self.esc_memo:
            self.found.extend(self.esc_memo[key])
            return
        if depth >= MAXDEPTH:
            self.report("limit", src, i, "climb: " + lab)
            return
        self.esc_memo[key] = []
        save = (self.seen, self.found, self.steps)
        self.seen, self.found, self.steps = set(), [], 0
        for f, cl, kind in self.callers(src, lab):
            if kind == "jmp":
                # a tail jump into this routine: ITS ret is the jumper's
                lab2 = f.glob_at[cl]
                if lab2 and lab2 != lab:
                    self.escape_label(f, cl, lab2, junk, depth + 1)
                continue
            self.walk(f, cl + 1, junk, depth + 1, (), top=True)
        found = self.found
        self.seen, self.found, self.steps = save
        self.found.extend(found)
        self.esc_memo[key] = found


# -------------------------------------------------------------- the gate
def sources():
    out = {}
    for d in DIRS:
        for root, _, names in os.walk(os.path.join(ROOT, d)):
            for n in names:
                if n.endswith((".asm", ".inc")):
                    p = os.path.join(root, n)
                    out[p] = Src(p)
    # A ROUTINE A MACRO WRITES (os88ui.inc's OS88UI_BTN_BODY os88ui_btn, and
    # its kind): the invocation becomes a source of its own, the body with
    # its arguments substituted, so its labels resolve and its code is
    # walked like any other.
    macros = {}
    for f in out.values():
        for name, body in f.macros:
            macros.setdefault(name, body)
    extra = {}
    for f in list(out.values()):
        for i, ins in enumerate(f.ins):
            if not ins or i in f.macro_body:
                continue
            body = macros.get(ins[0].upper())
            if not body or not any(LABEL_RE.match(b) for b in body):
                continue
            ops = ins[1]
            lines = []
            for b in body:
                t = b.replace("%%", "..@m%d_" % i)
                for k in range(9, 0, -1):
                    t = t.replace("%%%d" % k, ops[k - 1] if k <= len(ops)
                                  else "")
                lines.append(t)
            p = "%s#L%d" % (f.path, i + 1)
            extra[p] = Src(p, "\n".join(lines), "%s#L%d" % (f.rel, i + 1))
    out.update(extra)
    return out


def allowlist():
    out = {}
    if not os.path.exists(ALLOW):
        return out
    for line in open(ALLOW, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = line.split(None, 3)
        out[" ".join(parts[:3])] = parts[3] if len(parts) > 3 else ""
    return out


SITE = re.compile(r"^\s*(?:[.\w]+:\s*)?call\s+(OSAPI_GFX_(%s)|%s)\b"
                  % ("|".join(CELLS), ALIAS_RE))
GATED = ("read",)


def main():
    report = "--report" in sys.argv
    fixlist = "--fixlist" in sys.argv
    files = sources()
    cells = cell_regs()
    w = Walker(files, cells)
    allow = allowlist()
    nsites = 0
    kinds = {}
    used = set()
    for path in sorted(files):
        src = files[path]
        for i, line in enumerate(src.raw):
            if i in src.macro_body:
                continue
            if not SITE.match(line):
                ins = src.ins[i]
                if not ins or ins[0].upper() not in w.draw_macros:
                    continue
            nsites += 1
            where = "%s:%d" % (src.rel, i + 1)
            akey = "%s %s %s" % (src.rel, src.glob_at[i] or "-",
                                 w.cell_in(line.split(";")[0]) or
                                 src.ins[i][0].upper())
            found = w.site(src, i)
            if fixlist:
                need = set()
                for kind, at, text in found:
                    if kind == "read" and "<- reads " in text:
                        for h in text.split("<- reads ")[1].split()[0].split(","):
                            need.add(h[0] + "x")
                if need:
                    print("%s %s" % (where, ",".join(sorted(need))))
                continue
            seen = set()
            for kind, at, text in found:
                if (kind, at) in seen:
                    continue
                seen.add((kind, at))
                kinds[kind] = kinds.get(kind, 0) + 1
                ok = akey in allow
                if ok:
                    used.add(akey)
                if report:
                    print("%-12s %s  (from %s)%s\n    %s" % (
                        kind, at, where, "  [allowed]" if ok else "",
                        text.strip()))
                elif kind in GATED:
                    check(ok, "%s reads a register the drawing cell at %s "
                          "clobbered: %s" % (at, where, text.strip()),
                          why="OSAPI_GFX_PIXEL/HLINE/VLINE clobber "
                          "AX..DX (SPEC.md 5.7.3): reload or save "
                          "the register around the call, or list the site "
                          "in tests/gfxclob.txt with the reason it is safe")
    stale = sorted(set(allow) - used)
    check(not stale, "tests/gfxclob.txt lists sites with no finding: %s"
          % ", ".join(stale[:8]),
          why="the list only turns one way - delete a line whose site is fixed")
    check(nsites > 300, "found %d drawing-cell call sites" % nsites,
          why="the site pattern has stopped matching the tree")
    if fixlist:
        return
    if report:
        print("\n%d sites; findings by kind: %s" % (nsites, kinds))
    done("gfxclob")


if __name__ == "__main__":
    main()
