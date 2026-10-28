#!/usr/bin/env python3
"""Every heap claim moves, or says here WHY it does not (NO-PINNING-PLAN 4).

    python3 tests/unit/t_pins.py              the gate
    python3 tests/unit/t_pins.py --list       every claim site and its verdict
    python3 tests/unit/t_pins.py --seed       registry lines for what is pinned

A claim is born PINNED (SPEC.md 66.2): `MC_RLOC` = 0 until its holder calls
`OSAPI_MEM_MOVABLE` with a relocation proc. A pinned claim is a WALL, and the
owner's rule (docs/plans/NO-PINNING-PLAN.md 0) says a claim may stay one for
three reasons and no other:

  M  it is an on-demand kernel MODULE (MEM_K_MOD, SPEC.md 2.8);
  D  a device is armed to write into it - DMA, int 13h, an ISR's ring;
  C  the program is using it RIGHT NOW - claimed and freed inside one
     callback or command, or pinned only across a call holding its segment
     (a file call's ES:BX, an fsx bracket).

Plus three that are not pins at all, and one that is a debt:

  B  a BOOT ceiling claim: top-down, made before the first desktop frame and
     never freed - everything later stacks under it, so it walls nothing
     (NO-PINNING-PLAN 2.2's finding);
  S  shed by design: a purgeable cache the compactor DROPS (6.2, 6.4). A
     `MEM_P_*` kernel claim is accepted without a line;
  X  another reason, and it had better be a very good one (60+ characters);
  N  NO VALID REASON YET. The backlog: what is pinned today that the rule
     says should move. Every N line is a finding for the owner, and the
     count is printed so it can only be seen going down.

THE KEY - (FILE, the WORD the claim's segment is stored to). Chosen after
reading a few dozen sites in asm and C, and it is the identity a relocation
proc already uses: `np_reloc` compares BX against `[np_dseg]`, `wd_rltab` is a
table of those words, and `os88_onmove` re-points the C globals by name. So:

  * asm: the first `mov [W], dx` after the claim, before DX is overwritten
    or control leaves (`ret`/`jmp`). Kernel claims prefer their TAG (`mov
    bx, MEM_K_*` before the call), because the tag is what mem_tab records
    and what the runtime half reads back.
  * C: the lvalue the claim is assigned to - `func:var` when var is a local.
  * neither: `@routine`, the enclosing non-local label. Coarser, and a
    rename of the routine re-keys it - which fails LOUDLY (an unregistered
    site plus a stale line), never silently.

A claim is DECLARED when some `OSAPI_MEM_MOVABLE` (or the kernel's
`mem_movable_x`, or the parts standard's `op_movable`, or C's
`os88_mem_movable(x, nonzero)`) has DX loaded from that same word, or is
reached from the claim with DX still holding it - through any package-local
wrapper (`pt_movable`, `vp_mmovz`), through a table walk (`wd_rltab`,
`rw_mtab`, `lem_cltab[]`) or straight after the store. A declaration with AX = 0
is a PIN and declares nothing. Package-local CLAIM wrappers (`vp_mclaim`, a
thunk ending in `ret` with DX still the claim) are seen through the same way,
so the site is where the wrapper is CALLED.

THE TRADE-OFF, stated so the next reader need not re-derive it. Word identity
is a FALSE-NEGATIVE risk in one shape: a package that declares a word for one
claim and stores a DIFFERENT, undeclared claim to the same word elsewhere
reads as declared everywhere (Paint's `pt_gseg` is not that - its staging
lives in its own word). It is a FALSE-POSITIVE risk in two: a declaration
whose DX comes from somewhere this cannot follow (a register copy, a pushed
DX) reads as undeclared, and so does a claim stored only through a register
pointer. Both fail loudly, in the diff, and a registry line or a plainer
spelling fixes them. Nothing reads "declared" that never was without a word
match, which is the direction that matters: a false green here is a wall
nobody looks for.

SCOPE: apps/ (by package include graph - t_movable's walk), drivers/ (by
directory), the shared SDK includes (apps/*.inc, each its own unit) and
kernel/ (one unit). tests/ is instruments, and tests/pinme exists to be
pinned. The implementations of the claim slots themselves (memory.inc, the
parts library, the C thunk) are exempt - their callers are the sites.

THE REGISTRY TURNS ONE WAY (t_movable.py's and t_textrules.py's shape): a
line whose claim now declares is a FAILURE, as is a line naming a key that no
longer exists, so tests/pins.txt cannot rot into a list of things that used
to be true. A C line must name the call or bracket it is confined to and a D
line the device (NO-PINNING-PLAN 4).

The RUNTIME half is tests/pinsrt.py: what this scan cannot see - a claim made
through a path it does not follow - is read back out of mem_tab on MartyPC.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402
import t_movable                                          # noqa: E402

ROOT = t_movable.ROOT
REGISTRY = os.path.join(ROOT, "tests", "pins.txt")
HEAPDOC = os.path.join(ROOT, "docs", "HEAP-CLAIMS.md")

BUCKETS = {
    "M": "an on-demand kernel module",
    "D": "a device is armed on it",
    "C": "in use right now: one callback, command, call or bracket",
    "B": "a boot ceiling claim, top-down and never freed",
    "S": "shed by design (purgeable)",
    "X": "another, very good, reason",
    "N": "no valid reason yet - a finding",
}
MINREASON = 20
WRAPMAX = 12            # a thunk declares within this many instructions
XREASON = 60
# what a C line must say it is confined to, and a D line what is armed on it
C_WORDS = re.compile(r"callback|command|across|inside|bracket|\bcall\b|"
                     r"one paint|transfer|for the length|while", re.I)
D_WORDS = re.compile(r"DMA|chip|card|int 13h|ISR|ring|armed|8237|device|"
                     r"hook|interrupt", re.I)

# The slots' own bodies: their callers are the sites.
EXEMPT = {
    "apps/os88api.inc": "defines the slots",
    "apps/os88parts.inc": "the parts standard: op_load/op_fetch callers are "
                          "the sites",
    "apps/os88partsbody.inc": "...and its body",
    "apps/cc/os88thunk.asm": "the C SDK's thunk IS os88_mem_claim; the C "
                             "callers are the sites",
    "apps/cc/os88.h": "declares them",
    "kernel/memory.inc": "implements the claim heap",
}


def rel(p):
    return os.path.relpath(p, ROOT).replace("\\", "/")


def read(path):
    with open(os.path.join(ROOT, path), errors="replace") as fh:
        return fh.read()


_CODE = {}
_HOT = {}
HOT = re.compile(r"\b(?:call|jmp)\b|mem_claim|mem_movable")


def hot(lines):
    """The lines that can be a claim, a declaration or a call to a wrapper -
    every other line is skipped by the walks that look for those. Once per
    file; it is what keeps 141 units over one tree under a few seconds."""
    tab = _HOT.get(id(lines))
    if tab is None or tab[0] is not lines:
        tab = _HOT[id(lines)] = (lines, [
            (i, asm_code(ln)) for i, ln in enumerate(lines)
            if HOT.search(asm_code(ln))])
    return tab[1]


def asm_code(ln):
    c = _CODE.get(ln)
    if c is None:
        c = "" if ln.lstrip().startswith(";") else ln.split(";")[0].rstrip()
        _CODE[ln] = c
    return c


def c_code(ln):
    if ln.lstrip().startswith(("//", "*", "/*")):
        return ""
    return ln.split("//")[0].rstrip()


# --- the asm vocabulary --------------------------------------------------------
API_CLAIM = re.compile(r"\bcall\s+(?:far\s+)?OSAPI_MEM_(?:CLAIM|CLAIM_DMA|"
                       r"CLAIM_HI|CLAIM_DMA_HI|HUGE)\b")
K_CLAIM = re.compile(r"\b(?:call\s+(?:far\s+)?(?:COLD_SEG:)?|[A-Z_]+\s+)"
                     r"(?:mem_claim|mem_claim_x|mem_claim_hi_x|mem_claim_dma_x|"
                     r"mem_claim_dma_hi_x|mmf_mem_claim)\b"
                     r"|\bmov\s+bp\s*,\s*mem_claim\w*\b")
OP_LOAD = re.compile(r"\bcall\s+op_load\b")
OP_FETCH = re.compile(r"\bcall\s+op_fetch\b")
API_DECL = re.compile(r"\bcall\s+(?:far\s+)?OSAPI_MEM_MOVABLE\b")
K_DECL = re.compile(r"\b(?:call\s+(?:far\s+)?(?:COLD_SEG:)?|[A-Z_]+\s+)"
                    r"(?:mem_movable_x|mmf_mem_movable)\b")
OP_DECL = re.compile(r"\bcall\s+op_movable\b")
CALL = re.compile(r"^\s*(?:call|jmp)\s+(?:near\s+|short\s+)?([A-Za-z_]\w*)\s*$")
LABEL = re.compile(r"^([A-Za-z_]\w*):")
STORE_DX = re.compile(r"\bmov\s+(?:word\s+)?\[([^\]]+)\]\s*,\s*dx\b", re.I)
DX_WRITE = re.compile(r"\b(?:mov|pop|lds|les|xor|sub|add|adc|sbb|inc|dec|shl|"
                      r"shr|neg|not)\s+dx\b|\bxchg\b[^\n]*\bdx\b|\b(?:i?mul|"
                      r"i?div|cwd)\b|\b(?:or|and)\s+dx\s*,(?!\s*dx\b)", re.I)
AX_WRITE = re.compile(r"\b(?:mov|pop|xor|sub|lea)\s+ax\b|\bxchg\b[^\n]*\bax\b",
                      re.I)
AX_ZERO = re.compile(r"\b(?:xor|sub)\s+ax\s*,\s*ax\b|\bmov\s+ax\s*,\s*0\b", re.I)
AX_REG = re.compile(r"\bmov\s+ax\s*,\s*(?:bx|cx|dx|si|di|bp)\s*$|"
                    r"\bxchg\s+(?:ax\s*,\s*\w\w|\w\w\s*,\s*ax)\s*$", re.I)
LEAVE = re.compile(r"^\s*(?:ret|retf|iret|jmp)\b", re.I)
RET = re.compile(r"^\s*(?:ret|retf)\b", re.I)
TAG = re.compile(r"\bmov\s+bx\s*,\s*(MEM_[KP]_\w+)\b")
TABLE_REG = re.compile(r"\bmov\s+(?:si|di|bx|bp)\s*,\s*([A-Za-z_]\w*)\s*$")
DW_HEAD = re.compile(r"^([A-Za-z_]\w*):?\s+dw\b(.*)$")
DW_MORE = re.compile(r"^\s+dw\b(.*)$")
REGS = {"ax", "bx", "cx", "dx", "si", "di", "bp", "sp"}
STORE_ANY = re.compile(r"\bmov\s+(?:word\s+)?\[([^\]]+)\]\s*,\s*"
                       r"(ax|bx|cx|dx|si|di|bp|es|ds)\b", re.I)


def norm(w):
    w = re.sub(r"\b(?:cs|ds|es|ss)\s*:", "", w)
    w = re.sub(r"\b(?:CSEG|FCV)\b", "", w)
    return re.sub(r"\s+", "", w)


def wordish(w):
    """A store target that names something, not a bare register pointer."""
    parts = [p for p in re.split(r"[+\-*]", w) if p]
    return any(p not in REGS and not p.isdigit() and not re.match(r"0x", p)
               for p in parts)


_ROUT = {}


def routine(lines, i):
    """The non-local label line i sits under. Tabulated once per file: it is
    asked for every line of every unit, and the walk back was 40% of the
    row's time."""
    tab = _ROUT.get(id(lines))
    if tab is None or tab[0] is not lines:
        cur, names = "?", []
        for ln in lines:
            m = LABEL.match(ln)
            if m:
                cur = m.group(1)
            names.append(cur)
        tab = _ROUT[id(lines)] = (lines, names)
    return tab[1][i]


class Unit(object):
    """One scope a declaration can reach: a package, a driver, a library."""

    def __init__(self, name, files, lines_of, kernel=False, parts=None):
        self.name, self.files, self.kernel = name, files, kernel
        self.lines_of, self.parts = lines_of, parts
        self.asm = [f for f in files if not f.endswith((".c", ".h"))]
        self.c = [f for f in files if f.endswith((".c", ".h"))]
        self.tables = self._tables()
        self.cwrap = self._wrappers(self._is_claim_line, claim=True)
        self.dwrap = self._wrappers(self._is_decl_line, claim=False)
        self.wtag = self._wrapper_tags() if kernel else {}
        self.cccarve = (False, False, None)
        self.only = None        # sites from these files alone (orphan parts)

    def _wrapper_tags(self):
        """A kernel claim thunk that sets its own tag (cmz_mclaim) gives it
        to every caller."""
        out = {}
        while True:
            n = len(out)
            for f in self.asm:
                L = self.lines_of[f]
                for i, c in hot(L):
                    r = routine(L, i)
                    if r not in self.cwrap or r in out or \
                            not self._is_claim_line(c, self.cwrap):
                        continue
                    m = CALL.match(c)
                    t = self._tag(L, i) or (m and out.get(m.group(1)))
                    if t:
                        out[r] = t
            if len(out) == n:
                return out

    # -- tables a declaration walks ------------------------------------------
    def _tables(self):
        out = {}
        for f in self.asm:
            cur = None
            for ln in self.lines_of[f]:
                c = asm_code(ln)
                m = DW_HEAD.match(c)
                if m:
                    cur = m.group(1)
                    out[cur] = re.findall(r"[A-Za-z_]\w*", m.group(2))
                    continue
                m = DW_MORE.match(c)
                if m and cur:
                    out[cur] += re.findall(r"[A-Za-z_]\w*", m.group(1))
                    continue
                if c.strip():
                    cur = None
        return out

    def _is_claim_line(self, c, wraps):
        if API_CLAIM.search(c) or (self.kernel and K_CLAIM.search(c)):
            return True
        m = CALL.match(c)
        return bool(m and m.group(1) in wraps)

    def _is_decl_line(self, c, wraps):
        if API_DECL.search(c) or (self.kernel and K_DECL.search(c)):
            return True
        m = CALL.match(c)
        return bool(m and m.group(1) in wraps)

    def _wrappers(self, test, claim):
        """Routines that hand the caller's DX through to a claim or a
        declaration (or back out of a claim) - to a fixpoint, since a
        wrapper can wrap a wrapper (vp_mmovz -> vp_mmov -> the slot)."""
        wraps = set()
        while True:
            new = set()
            for f in self.asm:
                L = self.lines_of[f]
                for i, c in hot(L):
                    if not test(c, wraps):
                        continue
                    r = routine(L, i)
                    if r == "?" or r in wraps:
                        continue
                    if claim and self._returns_dx(L, i):
                        new.add(r)
                    elif not claim and self._dx_from_caller(L, i):
                        new.add(r)
            if not new - wraps:
                return wraps
            wraps |= new

    def _returns_dx(self, L, i):
        """A claim thunk: DX goes back to the caller untouched."""
        j, n = i + 1, 0
        while j < len(L) and n < 8:
            if re.match(r"^\s*%(?:else|elif)", L[j]):
                j = self._endif(L, j)   # vp_mclhi: one arm, then its ret
                continue
            c = asm_code(L[j])
            j += 1
            if not c.strip():
                continue
            n += 1
            if LABEL.match(L[j - 1]):
                return False
            if RET.match(c):
                return True
            if STORE_DX.search(c) or DX_WRITE.search(c) or \
                    re.match(r"^\s*(?:call|jmp)\b", c):
                return False
        return False

    def _dx_from_caller(self, L, i):
        """A declaration thunk: DX was never written between the routine's
        label and the declaration, and the declaration is near the top. The
        bound is what keeps an ordinary callback - px_onkey, which reaches a
        thunk forty lines in with DX its caller's junk - from being read as a
        thunk itself, which cascades into every call of it."""
        n = 0
        for j in range(i - 1, -1, -1):
            if LABEL.match(L[j]):
                return True
            c = asm_code(L[j])
            if DX_WRITE.search(c) or API_CLAIM.search(c) or OP_FETCH.search(c) \
                    or (self.kernel and K_CLAIM.search(c)):
                return False        # a claim writes DX too
            if c.strip():
                n += 1
                if n > WRAPMAX:
                    return False
        return False

    # -- the sites --------------------------------------------------------------
    def sites(self):
        out = []
        for f in self.asm:
            if self.only is not None and f not in self.only:
                continue
            L = self.lines_of[f]
            for i, c in hot(L):
                if self.parts and OP_LOAD.search(c):
                    out.append(Site(f, i + 1, "op:carve", routine(L, i), "carve"))
                    continue
                fetch = self.parts is not None and OP_FETCH.search(c)
                if not (fetch or self._is_claim_line(c, self.cwrap)):
                    continue
                r = routine(L, i)
                if r in self.cwrap and not CALL.match(c):
                    continue        # a thunk's own claim: its callers count
                if r in self.cwrap and self._returns_dx(L, i):
                    continue
                key = self._tag(L, i) if self.kernel else None
                m = CALL.match(c)
                if self.kernel and key is None and m:
                    key = self.wtag.get(m.group(1))
                word, direct = self._flow(L, i)
                part = None
                if fetch:
                    for j in range(i - 1, max(-1, i - 8), -1):
                        m = re.search(r"\bmov\s+al\s*,\s*(\w+)", asm_code(L[j]))
                        if m:
                            part = "op:" + m.group(1)
                            break
                st = Site(f, i + 1, key or word or part or "@" + r, r,
                          "fetch" if fetch else "claim", word)
                st.part = part
                st.direct = direct
                st.stores = self.stores
                st.chain = (f, self.chain) if self.chain else None
                out.append(st)
        has, mv, main = self.cccarve
        if has:
            L = self.lines_of[main]
            n = next((k for k, x in enumerate(L)
                      if re.match(r"^\s*%define\s+CC_HAS_PARTS\b", x)), 0)
            st = Site(main, n + 1, "cc:carve", "crt0", "cccarve")
            st.ok = mv
            out.append(st)
        at = {(s.file, s.line): s for s in out}
        for s in out:
            nxt = at.get(s.chain) if s.chain else None
            if nxt is not None and nxt is not s:
                s.key, s.word, s.direct = nxt.key, nxt.word, nxt.direct
                s.stores = nxt.stores
        cwraps = self._c_wrappers()
        call = re.compile(r"([A-Za-z_][\w\.\->\[\]]*)\s*=\s*(?:os88_mem_(?:claim|"
                          r"huge)|%s)\s*\(" % "|".join(sorted(cwraps) or ["$^"]))
        for f in self.c:
            if self.only is not None and f not in self.only:
                continue
            L = self.lines_of[f]
            for i, ln in enumerate(L):
                ms = list(call.finditer(c_code(ln)))
                if not ms:
                    continue
                fn = c_func(L, i)
                if fn in cwraps:
                    continue        # a wrapper's own claim: its callers count
                for m in ms:
                    lv = norm(m.group(1))
                    key = ("%s:%s" % (fn, lv)) if c_local(L, i, lv) else lv
                    out.append(Site(f, i + 1, key, fn, "c", lv))
        return out

    def _c_wrappers(self):
        """C functions that hand a claim straight back (`return s;` -
        rc_oft_claim): the claim belongs to whatever the CALLER stores it to.
        To a fixpoint, so a wrapper of a wrapper is seen through too."""
        wraps = set()
        while True:
            pat = re.compile(r"([A-Za-z_]\w*)\s*=\s*(?:os88_mem_(?:claim|huge)%s)"
                             r"\s*\(" % "".join("|" + w for w in sorted(wraps)))
            new = set()
            for f in self.c:
                L = self.lines_of[f]
                for i, ln in enumerate(L):
                    for m in pat.finditer(c_code(ln)):
                        var = m.group(1)
                        for j in range(i, min(len(L), i + 12)):
                            cj = c_code(L[j])
                            if re.search(r"\breturn\s+%s\s*;" % re.escape(var), cj):
                                new.add(c_func(L, i))
                                break
                            if j > i and re.search(r"\b%s\s*=[^=]"
                                                   % re.escape(var), cj):
                                break
            if not new - wraps:
                return wraps
            wraps |= new

    def _tag(self, L, i):
        n = 0
        for j in range(i - 1, -1, -1):
            if LABEL.match(L[j]):
                return None
            c = asm_code(L[j])
            if not c.strip():
                continue
            m = TAG.search(c)
            if m:
                return m.group(1)
            if re.search(r"\bmov\s+bx\s*,", c):
                return None
            n += 1
            if n > 12:
                return None
        return None

    def _store(self, L, i):
        return self._flow(L, i)[0]

    def _flow(self, L, i):
        """Follow DX out of the claim at line i through the routine's own
        local branches (`jnc .got` / `.got: mov [w], dx`), for a few dozen
        instructions -> (the word it lands in, does a declaration take it
        while DX still holds it?). A lazy part's segment comes back through
        op_seg in AX instead."""
        locs = {}
        r0 = routine(L, i)
        for j in range(i, len(L)):
            if j > i and LABEL.match(L[j]):
                break
            m = re.match(r"^\s*(\.\w+):", L[j])
            if m:
                locs[m.group(1)] = j
        word, direct, chain = None, False, None
        stores = set()
        todo, seen, budget = [(i + 1, frozenset(["dx"]), ())], set(), 160
        while todo and budget > 0:
            j, held, stk = todo.pop(0)
            while j < len(L) and budget > 0 and (held or True in stk):
                if (j, held, stk) in seen:
                    break
                seen.add((j, held, stk))
                budget -= 1
                if LABEL.match(L[j]) and routine(L, j) != r0:
                    break
                c = asm_code(L[j])
                j += 1
                if not c.strip():
                    continue
                st = STORE_ANY.search(c)
                if st and st.group(2).lower() in held and \
                        wordish(norm(st.group(1))):
                    if word is None:
                        word = norm(st.group(1))
                    stores.add(norm(st.group(1)))
                if "dx" in held and self._is_decl_line(c, self.dwrap):
                    if not self._ax_zero(L, j - 1):
                        direct = True
                if self._is_claim_line(c, self.cwrap) or OP_FETCH.search(c):
                    m = CALL.match(c)
                    if word is None and m and m.group(1) in self.cwrap:
                        chain = j   # a wrapper that takes this DX on: the
                    break           # site THERE is this claim's (trk_claimsel)
                if re.match(r"^\s*%(?:else|elif)", L[j - 1]):
                    j = self._endif(L, j - 1)   # the other arm is not this
                    continue                    # path - nasm keeps one
                if re.match(r"^\s*call\s+op_seg\b", c):
                    held = frozenset(["ax"])    # a lazy part: op_seg's AX
                    continue
                m = re.match(r"^\s*push\s+(\w\w)\s*$", c)
                if m:               # a pushed copy comes back at its pop
                    stk = (stk + (m.group(1).lower() in held,))[-8:]
                    continue
                m = re.match(r"^\s*pop\s+(\w\w)\s*$", c)
                if m:
                    r = m.group(1).lower()
                    top = stk[-1] if stk else False
                    stk = stk[:-1]
                    held = (held | {r}) if top else (held - {r})
                    continue
                held = self._carry(c, held)
                m = re.match(r"^\s*j(\w+)\s+(?:short\s+|near\s+)?(\.\w+)\s*$",
                             c)
                if m:
                    if m.group(2) in locs:
                        todo.append((locs[m.group(2)] + 1, held, stk))
                    if m.group(1) == "mp":
                        break
                    continue
                if LEAVE.match(c):
                    break
            if word and direct:
                break
        self.chain = chain
        self.stores = stores
        return word, direct

    @staticmethod
    def _carry(c, held):
        """The registers still holding the claim's segment after line c."""
        m = re.match(r"^\s*mov\s+(\w\w)\s*,\s*(\w\w)\s*$", c)
        if m:
            d, s_ = m.group(1).lower(), m.group(2).lower()
            if s_ in held:
                return held | {d}
            return held - {d}
        m = re.match(r"^\s*xchg\s+(\w\w)\s*,\s*(\w\w)\s*$", c)
        if m:
            a_, b_ = m.group(1).lower(), m.group(2).lower()
            out = set(held) - {a_, b_}
            if a_ in held:
                out.add(b_)
            if b_ in held:
                out.add(a_)
            return frozenset(out)
        out = set(held)
        for r in list(held):
            if re.search(r"\b(?:mov|pop|lds|les|xor|sub|add|adc|sbb|inc|dec|"
                         r"shl|shr|neg|not|or|and|lea)\s+%s\b" % r, c, re.I) \
                    and not re.search(r"\b(?:or|and)\s+%s\s*,\s*%s\b" % (r, r),
                                      c, re.I):
                out.discard(r)
            if r == "dx" and re.search(r"\b(?:i?mul|i?div|cwd)\b", c, re.I):
                out.discard(r)
            if r == "ax" and re.search(r"\b(?:i?mul|i?div|cbw|lodsw|lodsb)\b",
                                       c, re.I):
                out.discard(r)
        return frozenset(out)

    def _endif(self, L, j):
        depth = 0
        for k in range(j + 1, len(L)):
            t = L[k].lstrip()
            if re.match(r"%if", t):
                depth += 1
            elif re.match(r"%endif", t):
                if depth == 0:
                    return k + 1
                depth -= 1
        return len(L)

    def _ax_zero(self, L, i):
        """Is AX zeroed in the same straight-line block before line i - a
        PIN rather than a declaration?"""
        for j in range(i - 1, max(-1, i - 12), -1):
            if re.match(r"^\s*\.?\w+:", L[j]):
                return False
            c = asm_code(L[j])
            if AX_WRITE.search(c):
                # zero is a pin; and AX copied from a register is a proc the
                # CALLER chose - maybe 0 (sc_m_mem's CX) - so it proves
                # nothing and is not counted as a declaration either
                return bool(AX_ZERO.search(c) or AX_REG.search(c))
        return False

    # -- the declarations -------------------------------------------------------
    def declared(self, sites):
        """-> (words, direct site ids, carve?, fetch?, C keys)."""
        words, direct = set(), set()
        carve, fetch = False, set()
        at = {(s.file, s.line): s for s in sites}
        for f in self.asm:
            L = self.lines_of[f]
            for i, c in hot(L):
                if OP_DECL.search(c):
                    al = None
                    for j in range(i - 1, max(-1, i - 10), -1):
                        m = re.search(r"\bmov\s+al\s*,\s*(\w+)", asm_code(L[j]))
                        if m:
                            al = m.group(1)
                            break
                    if al == "OP_CARVE":
                        carve = True
                    elif al and not al.lower() in REGS | {"al", "ah", "bl",
                                                         "cl", "dl"}:
                        fetch.add("op:" + al)
                    else:
                        fetch.add("op:*")       # AL the caller's: any part
                    continue
                if not self._is_decl_line(c, self.dwrap):
                    continue
                if routine(L, i) in self.dwrap and not CALL.match(c):
                    continue        # inside a thunk: its callers are the decls
                src, zero = self._dx_source(f, L, i, at)
                if zero:
                    continue
                if src is None:
                    continue
                kind, val = src
                if kind == "word":
                    words |= val
                elif kind == "site":
                    direct.add(val)
                elif kind == "table":
                    words |= set(self.tables.get(val, ()))
        return words, direct, carve, fetch, c_decls(self.c, self.lines_of)

    def _dx_source(self, f, L, i, at):
        """Where the DX a declaration at line i holds came from: a word, a
        claim site, or a table a walk loads it out of. A register copy
        (`mov dx, ax` / `mov ax, [w]`) is followed back."""
        zero = self._ax_zero(L, i)
        src, unresolved, reg = None, False, "dx"
        aliases = set()
        for j in range(i - 1, max(-1, i - 40), -1):
            if LABEL.match(L[j]):
                break
            c = asm_code(L[j])
            if not c.strip():
                continue
            if src is None and not unresolved:
                if (f, j + 1) in at and reg == "dx":
                    src = ("site", (f, j + 1))
                    break
                m = re.search(r"\bmov\s+%s\s*,\s*(?:word\s+)?\[([^\]]+)\]"
                              % reg, c, re.I)
                if m:
                    w = norm(m.group(1))
                    if wordish(w):
                        aliases.add(w)
                        src = ("word", aliases)
                        break
                    unresolved = True
                    continue
                m = re.search(r"\bmov\s+(?:word\s+)?\[([^\]]+)\]\s*,\s*%s\b"
                              % reg, c, re.I)
                if m and wordish(norm(m.group(1))):
                    aliases.add(norm(m.group(1)))   # the same value, and the
                    continue                        # load may be further up
                m = re.search(r"\bmov\s+%s\s*,\s*(ax|bx|cx|si|di|bp|dx|es|ds)"
                              r"\s*$" % reg, c, re.I)
                if m:
                    reg = m.group(1).lower()
                    continue
                m = re.search(r"\bxchg\s+(\w\w)\s*,\s*(\w\w)\s*$", c, re.I)
                if m and reg in (m.group(1).lower(), m.group(2).lower()):
                    a_, b_ = m.group(1).lower(), m.group(2).lower()
                    reg = b_ if reg == a_ else a_
                    continue
                w = re.compile(r"\b(?:mov|pop|lds|les|xor|sub|add|inc|dec|shl|"
                               r"shr)\s+%s\b|\bxchg\b[^\n]*\b%s\b" % (reg, reg),
                               re.I)
                if w.search(c) or (reg == "dx" and DX_WRITE.search(c)):
                    unresolved = True
                    continue
            if unresolved:
                m = TABLE_REG.search(c)
                if m and m.group(1) in self.tables:
                    src = ("table", m.group(1))
                    break
        if src is None and aliases:
            src = ("word", aliases)
        return src, zero


class Site(object):
    __slots__ = ("file", "line", "key", "rout", "kind", "word", "ok",
                 "direct", "chain", "part", "stores")

    def __init__(self, f, line, key, rout, kind, word=None):
        self.file, self.line, self.key, self.rout = f, line, key, rout
        self.kind, self.word, self.ok = kind, word, False
        self.direct = False
        self.chain = None
        self.part = None
        self.stores = set()


# --- the C vocabulary ----------------------------------------------------------
C_CLAIM = re.compile(r"([A-Za-z_][\w\.\->\[\]]*)\s*=\s*os88_mem_(?:claim|huge)"
                     r"\s*\(")
C_DECL = re.compile(r"\bos88_mem_movable\s*\(\s*([^,]+?)\s*,\s*([^)]+?)\s*\)")
C_FUNC = re.compile(r"^[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\([^;]*$")
C_TYPES = r"(?:unsigned|int|char|short|long|signed|static|register|const)"


def c_func(L, i):
    for j in range(i, -1, -1):
        m = C_FUNC.match(L[j])
        if m and not L[j].startswith(("#", "typedef", "return", "else")):
            return m.group(1)
    return "?"


def c_local(L, i, lv):
    if not re.match(r"^\w+$", lv):
        return False
    decl = re.compile(r"^\s+%s\b[^;()]*\b%s\b" % (C_TYPES, re.escape(lv)))
    for j in range(i, -1, -1):
        if C_FUNC.match(L[j]):
            return False
        if decl.search(c_code(L[j])):
            return True
    return False


def c_local_declared(L, i, var):
    """A function-local claim is declared when os88_mem_movable(var, on)
    follows it BEFORE var is assigned again or the function ends - c64kbd's
    `seg` is the clipboard's staging in one arm and Paste's queue in the
    next, and only the second moves."""
    again = re.compile(r"\b%s\s*=[^=]" % re.escape(var))
    for j in range(i + 1, len(L)):
        if C_FUNC.match(L[j]) and not L[j].startswith(("#", "return")):
            return False
        c = c_code(L[j])
        for m in C_DECL.finditer(c):
            if norm(m.group(1)) == var and m.group(2).strip() != "0":
                return True
        if again.search(c):
            return False
    return False


def c_decls(files, lines_of):
    out, tabs = set(), {}
    for f in files:
        txt = "\n".join(c_code(x) for x in lines_of[f])
        for m in re.finditer(r"(\w+)\s*\[[^\]]*\]\s*=\s*\{([^}]*)\}", txt):
            tabs[m.group(1)] = re.findall(r"&\s*(\w+)", m.group(2))
    for f in files:
        L = lines_of[f]
        for i, ln in enumerate(L):
            for m in C_DECL.finditer(c_code(ln)):
                a, on = norm(m.group(1)), m.group(2).strip()
                if on == "0":
                    continue        # a pin, not a declaration
                t = re.match(r"\*?\(?\*?(\w+)\[", a)
                if t and t.group(1) in tabs:
                    out |= set(tabs[t.group(1)])
                out.add(a)
                out.add("%s:%s" % (c_func(L, i), a))
    return out


# --- discovery -----------------------------------------------------------------
def all_files():
    out = []
    for sub in ("apps", "drivers", "kernel"):
        for base, _dirs, files in os.walk(os.path.join(ROOT, sub)):
            if os.path.basename(base) == "hosttest":
                continue
            for f in sorted(files):
                if f.endswith((".asm", ".inc", ".c", ".h")):
                    out.append(rel(os.path.join(base, f)))
    return out


PART = re.compile(r"^\s*OS88_PART\b(.*)$", re.M)
REHOME = re.compile(r"\bcall\s+OSAPI_PKG_REHOME\b|^\s*OS88_REHOME\b", re.M)
CCDEF = re.compile(r"^\s*%define\s+(CC_HAS_PARTS|CC_ONMOVE_PARTS)\b", re.M)


def units():
    lines_of = {f: read(f).splitlines() for f in all_files() if f not in EXEMPT}
    pkgs = t_movable.packages()
    out, owned = [], set()
    for main, own in sorted(pkgs.items()):
        own = [f for f in own if f in lines_of]
        owned |= set(own)
        blob = "\n".join(asm_code(x) for f in own if f.endswith((".asm", ".inc"))
                         for x in lines_of[f])
        parts = None
        if re.search(r'%include\s+"os88parts\.inc"', read(main)):
            # a carve exists when any part is EAGER; a re-home LOADER's carve
            # is the region of the part it hands to (t_movable's question)
            eager = any("OP_LAZY" not in m for m in PART.findall(blob))
            parts = eager and not REHOME.search(blob)
        u = Unit(main, own, lines_of, parts=parts)
        cc = set(CCDEF.findall(read(main)))
        # A C PACKAGE'S CARVE is crt0's: declared when the package says
        # CC_ONMOVE_PARTS (SPEC.md 73.14.2.1), and with the module alone in
        # it by cc_ovcarve - so the question is asked of CC_HAS_PARTS only
        u.cccarve = ("CC_HAS_PARTS" in cc, "CC_ONMOVE_PARTS" in cc, main)
        out.append(u)
    # FILES NO PACKAGE INCLUDES: a part assembled on its own (PiXEL's
    # pxedit.asm, pxgif.asm - OP_SEG parts with no header of their own), a
    # module, a host-built table. Scanned with their WHOLE DIRECTORY as the
    # scope, since the package that loads them is where they declare - and
    # only their own sites are taken, so no claim is counted twice.
    orphans = {}
    for f in lines_of:
        if f.startswith("apps/") and f.count("/") >= 2 and f not in owned \
                and not f.startswith("apps/cc/"):
            orphans.setdefault(f.split("/")[1], []).append(f)
    for d, fs in sorted(orphans.items()):
        scope = sorted(x for x in lines_of if x.startswith("apps/%s/" % d))
        u = Unit("apps/%s/(parts)" % d, scope, lines_of)
        u.only = set(fs)
        out.append(u)
    drv = {}
    for f in lines_of:
        if f.startswith("drivers/") and f.count("/") >= 2:
            drv.setdefault(f.split("/")[1], []).append(f)
    for d, fs in sorted(drv.items()):
        out.append(Unit("drivers/" + d, sorted(fs), lines_of))
    for f in sorted(lines_of):
        if (f.startswith("apps/") and f.count("/") == 1 or
                f.startswith("apps/cc/")) and f.endswith((".inc", ".asm")):
            out.append(Unit(f, [f], lines_of))
    kern = sorted(f for f in lines_of if f.startswith("kernel/"))
    out.append(Unit("kernel", kern, lines_of, kernel=True))
    return out


def scan():
    """-> {(file, key): [Site...]}, every site with .ok set."""
    sites = {}
    for u in units():
        ss = u.sites()
        words, direct, carve, fetch, cdec = u.declared(ss)
        for s in ss:
            if s.kind == "cccarve":
                ok = s.ok
            elif s.kind == "carve":
                ok = carve
            elif s.kind == "fetch":
                ok = "op:*" in fetch or s.part in fetch or s.key in words \
                    or s.direct
            elif s.kind == "c":
                if ":" in s.key:    # a LOCAL: declared before its next use
                    ok = c_local_declared(u.lines_of[s.file], s.line - 1,
                                          s.word)
                else:
                    ok = s.key in cdec
            else:
                # every word the segment was stored to names the same claim
                # (vp_fseek: [vp_ring] and [vp_rclm]), so any one declared is
                ok = s.key in words or bool(s.stores & words) \
                    or (s.file, s.line) in direct or s.direct
            prev = sites.get((s.file, s.line))
            # a file two packages include must be declared in BOTH
            s.ok = ok and (prev.ok if prev else True)
            sites[(s.file, s.line)] = s
    out = {}
    for s in sites.values():
        out.setdefault((s.file, s.key), []).append(s)
    return out


def purgeable(key):
    return key.startswith("MEM_P_")


# --- the registry ------------------------------------------------------------
def registry():
    out, bad = {}, []
    with open(REGISTRY) as fh:
        lines = fh.read().splitlines()
    i = 0
    while i < len(lines):
        raw = lines[i]
        i += 1
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        body, _, reason = raw.partition("#")
        parts = body.split()
        # a reason may continue on following lines that are only a comment
        # indented past the first column - textsites.txt's shape
        while i < len(lines) and lines[i].startswith(" ") and \
                lines[i].lstrip().startswith("#"):
            reason += " " + lines[i].strip()[1:].strip()
            i += 1
        if len(parts) != 3 or parts[0] not in BUCKETS:
            bad.append((i, raw))
            continue
        out[(parts[1], parts[2])] = (parts[0], " ".join(reason.split()))
    return out, bad


def main():
    found = scan()
    pinned = {k: v for k, v in found.items() if not all(s.ok for s in v)}
    if "--list" in sys.argv:
        for (f, key), ss in sorted(found.items()):
            print("%-4s %-38s %-28s %s" % (
                "ok" if all(s.ok for s in ss) else "PIN", f, key,
                ",".join(str(s.line) for s in ss)))
        return
    if "--seed" in sys.argv:
        for (f, key), ss in sorted(pinned.items()):
            if not purgeable(key):
                print("N %s %s  # %s, line %s" % (
                    f, key, ss[0].rout, ",".join(str(s.line) for s in ss)))
        return

    reg, bad = registry()
    check(len(found) > 100, "pins: the scan found almost no claim sites",
          why="the discovery walks apps/, drivers/ and kernel/ for every "
              "OSAPI_MEM_CLAIM*, os88_mem_claim, op_load/op_fetch and kernel "
              "mem_claim. A handful means it is asserting nothing - the one "
              "failure a green suite cannot show",
          got="%d site key(s)" % len(found), want="the tree's ~240")
    for n, raw in bad:
        check(False, "pins: tests/pins.txt line %d is malformed" % n,
              why="one line per pinned claim: `<bucket> <file> <key>  # "
                  "<reason>`, bucket one of " + "".join(sorted(BUCKETS)),
              got=raw.strip(), want="M|D|C|B|S|X|N <file> <key>  # <reason>")

    for (f, key), ss in sorted(pinned.items()):
        if purgeable(key) and (f, key) not in reg:
            continue        # shed by design (NO-PINNING-PLAN 6.2, 6.4)
        check((f, key) in reg,
              "pins: %s claims into %s and never declares it movable" % (f, key),
              why="NO-PINNING-PLAN 0: a pinned claim is a WALL in the arena. "
                  "Declare it (OSAPI_MEM_MOVABLE with a proc, os88_mem_movable, "
                  "op_movable) - or add a line to tests/pins.txt with its "
                  "bucket (M module, D device armed, C in use right now) and "
                  "a reason, so the choice is in the diff",
              got="pinned, line(s) %s (%s)" % (
                  ",".join(str(s.line) for s in ss), ss[0].rout),
              want="a declaration, or a registry line")

    counts = {b: 0 for b in BUCKETS}
    for (f, key), (bucket, reason) in sorted(reg.items()):
        counts[bucket] += 1
        if (f, key) not in found:
            check(False, "pins: tests/pins.txt names %s %s, which no longer "
                         "claims" % (f, key),
                  why="the registry is checked against the tree, so a line "
                      "cannot outlive the claim it excused - a renamed word "
                      "or routine re-keys the site, and the old line goes",
                  got="no claim site with that key", want="the line deleted")
            continue
        check((f, key) in pinned,
              "pins: %s %s is registered as pinned and now declares" % (f, key),
              why="the list only turns one way (t_movable's ratchet). The claim "
                  "is declared movable now, so deleting the line is the diff "
                  "saying the work happened",
              got="a '%s' line" % bucket, want="no line")
        floor = XREASON if bucket == "X" else MINREASON
        check(len(reason) >= floor,
              "pins: %s %s carries no reason" % (f, key),
              why="a pin without an argument cannot be reviewed or retired",
              got="%r" % reason, want="at least %d characters" % floor)
        if bucket == "C":
            check(bool(C_WORDS.search(reason)),
                  "pins: %s %s is bucket C and names no call or bracket"
                  % (f, key),
                  why="NO-PINNING-PLAN 4: a bucket-C pin is MOMENTARY, so its "
                      "line says what it is confined to - one callback, one "
                      "command, across which call, inside which bracket",
                  got=reason[:80], want="the call/bracket it lasts for")
        if bucket == "D":
            check(bool(D_WORDS.search(reason)),
                  "pins: %s %s is bucket D and names no device" % (f, key),
                  why="NO-PINNING-PLAN 4: a bucket-D line names what is armed "
                      "on the claim - the DMA channel, the card, the ISR",
                  got=reason[:80], want="the device")
        if f == "kernel" or f.startswith("kernel/"):
            if key.startswith("MEM_"):
                check(key in heapdoc(),
                      "pins: kernel claim %s is pinned and not in "
                      "docs/HEAP-CLAIMS.md" % key,
                      why="the register is one row per claim; a kernel pin "
                          "with no row there is one nobody can look up",
                      got="no mention of %s" % key, want="a row")

    total = sum(len(v) for v in found.values())
    print("t_pins: %d claim site(s) under %d key(s); %d declared movable, %d "
          "pinned (%d shed by design); registry M%d D%d C%d B%d S%d X%d N%d - "
          "N is the backlog" % (
              total, len(found), len(found) - len(pinned), len(pinned),
              sum(1 for k in pinned if purgeable(k[1])),
              counts["M"], counts["D"], counts["C"], counts["B"], counts["S"],
              counts["X"], counts["N"]))
    done("t_pins")


_HEAPDOC = []


def heapdoc():
    if not _HEAPDOC:
        with open(HEAPDOC) as fh:
            _HEAPDOC.append(fh.read())
    return _HEAPDOC[0]


if __name__ == "__main__":
    main()
