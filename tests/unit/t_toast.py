#!/usr/bin/env python3
"""Every string that can reach a toast is a TOASTSTR (SPEC.md 59.10).

    python3 tests/unit/t_toast.py [-v]

`toast_show` copies at most `TOAST_MAX` = 24 characters and drops the rest
SILENTLY.  That cap is GEOMETRY, not a budget (SPEC.md 59.10), so every fixed
message is written to fit - and for five rounds this file tried to hold that
by INFERRING which strings were toasts, from their neighbourhood, and missed a
shape each round: `label db` without a colon, a MODULE nobody walked, a
package's `.inc` files, a wrapper reached by a tail `jmp`.  Each miss shipped a
message the user saw cut in half (`Arcade: no game is loade`).

THE LENGTH IS NOW CHECKED WHERE THE STRING IS WRITTEN, by the assembler.
`TOASTSTR label, 'text'` (apps/os88api.inc for packages and drivers,
kernel/kernel.asm for the kernel) emits `label: db 'text', 0` - the same bytes
as before - and fails the build AT THAT LINE when the text is longer than
TOAST_MAX; `TOASTRUN label` ... `TOASTEND` does the same for a run of `db`
lines with fields poked at run time.  Nothing has to be inferred about a
string that says what it is.  This file also holds the two macro blocks
identical and every TOASTRUN closed, which NASM itself does not check.

WHAT IS LEFT FOR THIS FILE is to prove that nothing ELSE reaches a toast, and
it does that by following the ARGUMENT, not the neighbourhood: from every call
or `jmp` to a toast entry (OSAPI_TOAST, toast_show, cw_toast_show; toast_say
and its thunk take a TABLE in BX) it walks BACKWARDS through the code for the
register that carries the string - across fall-through, every branch to a
label on the way (so a wrapper's callers are walked exactly like a direct
site, whether they `call` it, `jmp` to it or fall into it), every arm of a
`%if`, `push`/`pop` pairs, register copies, a call whose callee WRITES the
register (summarised per callee from its own `ret`s), a word table indexed by
a register (every entry), and a pointer VARIABLE (every store to it).  Every
path has to end in one of three places:

  * a TOASTSTR label, or a table/variable whose every entry is one - checked;
  * a run-time value - a composed buffer, a caller's argument - registered in
    tests/toastdyn.txt with the reason it cannot be fixed text.  The list is
    a RATCHET: a line nothing reaches any more fails the build;
  * anywhere else is a FAILURE: a raw `db`, a string a package macro made, a
    label it cannot find, an instruction it does not model.

That is the inversion that makes it stop missing things: the old walker
PASSED anything its pattern did not recognise; this one FAILS on it.  A shape
nobody has written yet arrives as a red row naming the line, not as a cut
message on somebody's screen.

The C half is simpler because C has literals: every `os88_toast(` argument in
apps/ that is a string literal is measured; one that is a parameter of the
enclosing function is followed to that function's callers; anything else is a
tests/toastdyn.txt line.

`TOASTDBG=<text> python3 tests/unit/t_toast.py` prints the path the walk took
to every run-time source whose description contains <text> - the way to read
a new failure.  A tests/toastdyn.txt line `<path> <label> cut` stops the walk
at a label, for a path no build can take.

WHAT IT STILL CANNOT SEE, and says so: an indirect call or jump (`call [bx]`,
a callback table), a store to a pointer variable through a string op or a
computed address, and a callee that writes the register through anything
other than the instructions it models.  None of those is how any message in
the tree reaches a toast today.
"""
import bisect
import glob
import os
import re
import sys

sys.setrecursionlimit(20000)
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
ALLOW = os.path.join(ROOT, "tests", "toastdyn.txt")
VERBOSE = "-v" in sys.argv

ASM_GLOBS = ("kernel/*.inc", "kernel/*.asm",
             "apps/**/*.asm", "apps/**/*.inc",
             "drivers/**/*.asm", "drivers/**/*.inc")
C_GLOBS = ("apps/**/*.c", "apps/**/*.h")
INC_PATH = ("apps", "kernel", "drivers", ".")

# Toast entries: name -> (register, kind). 'str' = ES:SI a NUL string; 'tab'
# = BX a word table of NUL strings, which toast_say indexes.
SINKS = {
    "OSAPI_TOAST": ("si", "str"),
    "toast_show": ("si", "str"),
    "cw_toast_show": ("si", "str"),
    "toast_say": ("bx", "tab"),
    "ct_toast_say": ("bx", "tab"),
}
# The entries' own bodies, whose internal calls are not sites.
SINK_BODIES = {"toast_show", "toast_say", "cw_toast_show", "ct_toast_say"}
# OSAPI calls that RETURN a string in SI: the kernel routine behind each,
# which is walked like any callee.
PRODUCERS = {"OSAPI_FERR_TEXT": "osapi_ferr_text"}
# Routines that COPY the string they are given into a buffer and hand the
# buffer back in the same register: the text that comes out is the text that
# went in, so the walk carries on with the argument rather than stopping at
# the buffer. cp_stage (kernel/ctrl.inc) stages a Control Panel string.
COPIERS = {"cp_stage": "si"}

REGS = ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp")
HALVES = {"al": "ax", "ah": "ax", "bl": "bx", "bh": "bx",
          "cl": "cx", "ch": "cx", "dl": "dx", "dh": "dx"}
IMPLICIT = [
    (re.compile(r"^lods[bw]?$"), ("si", "ax")),
    (re.compile(r"^(?:stos[bw]?|scas[bw]?)$"), ("di", "ax")),
    (re.compile(r"^(?:movs[bw]?|cmps[bw]?)$"), ("si", "di")),
    (re.compile(r"^rep\w*\s+(?:movs|cmps)"), ("si", "di", "cx")),
    (re.compile(r"^rep\w*\s+(?:stos|scas)"), ("di", "cx")),
    (re.compile(r"^rep\w*\s+lods"), ("si", "cx", "ax")),
    (re.compile(r"^(?:cbw|aaa|aas|aad|aam|daa|das|xlatb?|lahf|in)$"), ("ax",)),
    (re.compile(r"^cwd$"), ("dx",)),
    (re.compile(r"^(?:mul|imul|div|idiv)$"), ("ax", "dx")),
    (re.compile(r"^loop\w*$"), ("cx",)),
]
UNCOND = re.compile(r"^(?:jmp|ret|retf|retn|iret|hlt)$")
EXIT = re.compile(r"^(?:ret|retf|retn|iret|hlt)$")
BRANCH = re.compile(r"^(?:j[a-z]+|call|loop[a-z]*)$")
# the carry flag, which is how most routines here say "refused": a jump that
# is TAKEN on CF=1 / CF=0, the instructions that clear it, and those that set
# it to something the walk does not know
CF_TAKEN = {"jc": 1, "jb": 1, "jnae": 1, "jnc": 0, "jae": 0, "jnb": 0}
CF_CLEAR = {"or", "and", "xor", "test"}
CF_WRITE = {"add", "adc", "sub", "sbb", "cmp", "neg", "shl", "shr", "sal",
            "sar", "rol", "ror", "rcl", "rcr", "cmc", "popf", "sahf", "mul",
            "imul", "div", "idiv", "aaa", "aas", "daa", "das", "int", "into",
            "scasb", "scasw", "cmpsb", "cmpsw"}
STRUCT = {"%if", "%ifdef", "%ifndef", "%ifidn", "%ifidni", "%ifnidn",
          "%ifnidni", "%ifnum", "%ifstr", "%ifmacro", "%ifnmacro", "%else",
          "%elif", "%elifdef", "%elifndef", "%endif", "%macro", "%imacro",
          "%endmacro", "%rep", "%endrep"}
NAME = r"[A-Za-z_.?][\w.@$?#~]*"
TOPNAME = r"[A-Za-z_?][\w.@$?#~]*"     # not a .local: those are found lazily
IDENT = re.compile(r"^(?:%%)?" + NAME + "$")
NUM = re.compile(r"^(?:\d\w*|0x[0-9a-f]+|\$)$", re.I)

MACRO = re.compile(r"^[ \t]*%i?macro[ \t]+(\w+)[^\n]*\n(.*?)^[ \t]*%endm\w*",
                   re.M | re.S)
# These two run over COMMENT-STRIPPED text (stripping halves the bytes, and
# this tree is mostly prose), which is why they end at `$`.
# Each starts with a LITERAL so the engine can skip to it: a leading `\b` or an
# alternation of mnemonics costs five to eight times as much over 18MB, and
# the word boundary is checked on the few matches instead (word_before).
_TGT = (r"[ \t]+(?:(?:short|near|far)[ \t]+)?(?:[A-Za-z_]\w*[ \t]*:[ \t]*)?("
        + TOPNAME + r")[ \t]*$")
REFS = (re.compile(r"call" + _TGT, re.M), re.compile(r"j[a-z]{1,4}" + _TGT, re.M),
        re.compile(r"loop[a-z]{0,2}" + _TGT, re.M))
STORE = re.compile(r"mov[ \t]+(?:word[ \t]+)?(?:[cdes]s[ \t]*:[ \t]*)?\[[ \t]*"
                   r"(?:[cdes]s[ \t]*:[ \t]*)?("
                   + NAME + r")[ \t]*\][ \t]*,[ \t]*([^\n]+?)[ \t]*$", re.M)
# a label or data at column 0 (no top-level label in the tree is indented),
# and an upper-case first word with a name after it, which is a macro
# invocation - TOASTSTR, or one that defines its first argument - sorted out
# in the loop, because an alternation of every macro's name is slow
DEF0 = re.compile(r"^(?:([A-Z][A-Z0-9_]*)[ \t]+((?:%%)?" + NAME + r")[ \t]*(?:,|$)|("
                  + TOPNAME + r")(?:[ \t]*:|[ \t]+(db|dw|dd|resb|resw|resd|times|equ)\b))",
                  re.M)
DEFM = re.compile(r"^[ \t]+([A-Z][A-Z0-9_]*)[ \t]+((?:%%)?" + NAME + r")[ \t]*(?:,|$)",
                  re.M)
INCLUDE = re.compile(r'%include[ \t]+"([^"]+)"')
CAP = re.compile(r"^TOAST_MAX\s+equ\s+(\d+)", re.M)


def word_before(text, pos):
    return pos > 0 and (text[pos - 1].isalnum() or text[pos - 1] in "_.@$?#~")


def fail(msg):
    print("t_toast: FAIL: %s" % msg)
    sys.exit(1)


def strip_comment(line):
    if ";" not in line:
        return line
    q = None
    for i, c in enumerate(line):
        if q:
            if c == q:
                q = None
        elif c in "'\"`":
            q = c
        elif c == ";":
            return line[:i]
    return line


def split_args(s):
    out, cur, depth, q = [], "", 0, None
    for c in s:
        if q:
            cur += c
            if c == q:
                q = None
            continue
        if c in "'\"`":
            q = c
        elif c in "[(":
            depth += 1
        elif c in "])":
            depth -= 1
        elif c == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
            continue
        cur += c
    out.append(cur.strip())
    return out


class Line(object):
    __slots__ = ("label", "op", "args", "data", "dir", "opraw")


class Src(object):
    """One source file: its lines, its top-level labels, a lazy line parse."""

    def __init__(self, rel, text):
        self.rel = rel
        self.text = text
        self.lines = text.split("\n")
        self._cache = {}
        self.topidx, self.topnm = [], []
        self.starts = None

    def line_of(self, pos):
        if self.starts is None:
            st, n = [], 0
            for ln in self.lines:
                st.append(n)
                n += len(ln) + 1
            self.starts = st
        return bisect.bisect_right(self.starts, pos) - 1

    def top(self, i):
        k = bisect.bisect_right(self.topidx, i) - 1
        return self.topnm[k] if k >= 0 else ""

    def full(self, name, i):
        if name.startswith(".") and not name.startswith(".."):
            return self.top(i) + name
        return name

    def parse(self, i):
        L = self._cache.get(i)
        if L is not None:
            return L
        L = Line()
        L.label = L.op = L.data = L.dir = L.opraw = None
        L.args = []
        s = strip_comment(self.lines[i]).strip() if 0 <= i < len(self.lines) else ""
        self._cache[i] = L
        if not s:
            return L
        if s.startswith("%"):
            L.dir = s.split(None, 1)[0].lower()
            L.args = [s]
            return L
        if s.startswith("["):
            return L
        m = re.match(r"^TOAST(?:STR|RUN)\s+((?:%%)?" + NAME + r")\s*(?:,(.*))?$", s)
        if m:
            L.label = self.full(m.group(1), i)
            L.data = ("toast", m.group(2) or "")
            return L
        m = re.match(r"^(" + NAME + r")\s*:(.*)$", s)
        if m and not re.match(r"^[cdes]s$", m.group(1), re.I):
            L.label = self.full(m.group(1), i)
            s = m.group(2).strip()
        m = re.match(r"^(" + NAME + r")\s+(db|dw|dd|resb|resw|resd|times|equ)\b(.*)$",
                     s, re.I)
        if m and L.label is None:
            L.label = self.full(m.group(1), i)
            L.data = (m.group(2).lower(), m.group(3))
            return L
        m = re.match(r"^(db|dw|dd|resb|resw|resd|times|equ)\b(.*)$", s, re.I)
        if m:
            L.data = (m.group(1).lower(), m.group(2))
            return L
        if not s:
            return L
        parts = s.split(None, 1)
        op, rest = parts[0].lower(), (parts[1] if len(parts) > 1 else "")
        L.opraw = parts[0]
        while op in ("es", "cs", "ss", "ds", "lock") and rest:
            p2 = rest.split(None, 1)
            op, rest = p2[0].lower(), (p2[1] if len(p2) > 1 else "")
        if op.startswith("rep") and rest:
            p2 = rest.split(None, 1)
            op = op + " " + p2[0].lower()
            rest = p2[1] if len(p2) > 1 else ""
        L.op = op
        L.args = split_args(rest) if rest else []
        return L


def branch_target(arg):
    a = re.sub(r"^(?:short|near|far)\s+", "", arg.strip(), flags=re.I)
    if ":" in a and not a.startswith("["):
        a = a.split(":")[-1].strip()
    return a if IDENT.match(a) else None


class Gate(object):
    def __init__(self):
        self.cap = None
        self.ok = {}         # TOASTSTR label -> sites
        self.bad = []        # (message)
        self.dyn = {}        # (path, proc, what) -> [why]
        self.memo = {}
        self.busy = set()
        self.sites = 0
        self.stack = []
        self.cur = None
        self.cuts = set()
        self.cutused = set()
        self.lcache = {}

    # ---- loading -------------------------------------------------------
    def load(self):
        files = set()
        for pat in ASM_GLOBS:
            files.update(glob.glob(os.path.join(ROOT, pat), recursive=True))
        rels = sorted(os.path.relpath(f, ROOT) for f in files)
        texts = {r: open(os.path.join(ROOT, r), encoding="utf-8",
                         errors="replace").read() for r in rels}
        m = CAP.search(texts.get("kernel/kernel.asm", ""))
        if not m:
            fail("kernel/kernel.asm has no `TOAST_MAX equ <n>` - this row reads "
                 "the cap out of the source rather than carrying a copy")
        self.cap = int(m.group(1))
        self.structure(texts)
        # the include graph, so a label is looked up only where it can be seen
        incs = {}
        for r, t in texts.items():
            d = os.path.dirname(r)
            got = []
            for m in INCLUDE.finditer(t):
                if t[t.rfind("\n", 0, m.start()) + 1:m.start()].strip():
                    continue                # not the first thing on its line
                for base in (d,) + INC_PATH:
                    c = os.path.normpath(os.path.join(base, m.group(1)))
                    if c in texts:
                        got.append(c)
                        break
            incs[r] = got
        # a UNIT is a file nothing includes and everything it includes; a
        # label is visible from a file to every file of every unit it is in.
        # Only the units that toast at all are read further.
        included = {h for v in incs.values() for h in v}
        kern = frozenset(r for r in rels if r.startswith("kernel/"))
        sinkpat = re.compile(r"\b(?:%s)\b" % "|".join(SINKS))
        sinky = {r for r in rels if any(n in texts[r] for n in SINKS)
                 and sinkpat.search(texts[r])}
        group = {}
        hot = set(kern)
        for r in rels:
            if r in included or r in kern:
                continue
            seen, st = {r}, [r]
            while st:
                for h in incs[st.pop()]:
                    if h not in seen:
                        seen.add(h)
                        st.append(h)
            if not any(x in sinky for x in seen):
                continue
            hot |= seen
            for g in seen:
                group.setdefault(g, set()).update(seen)
        for r in kern:
            group[r] = set(kern)
        self.group = group
        self.src = {r: Src(r, texts[r]) for r in sorted(hot)}
        # macros, and which of them define their first argument as a label
        self.macros = {}
        for r, S in self.src.items():
            S.st = re.sub(r";[^\n]*", "", S.text)
            if "macro" in S.st:
                for m in MACRO.finditer(S.st):
                    self.macros[m.group(1)] = m.group(2)
        labmac = sorted(n for n, b in self.macros.items()
                        if re.search(r"%1\s*(?::|\s+(?:db|dw|dd|equ|resb|resw|times)\b)", b))
        self.labmac = set(labmac)
        self.defs = {}
        self.refs = {}
        self.stores = {}
        for r, S in self.src.items():
            st = S.st
            cur = ""
            raw = []
            ln, last = 0, 0
            ms = list(DEF0.finditer(st)) + list(DEFM.finditer(st))
            ms.sort(key=lambda m: m.start())
            for m in ms:
                ln += st.count("\n", last, m.start())
                last = m.start()
                i = ln
                if m.group(1):
                    if m.group(1) in ("TOASTSTR", "TOASTRUN"):
                        nm, k = m.group(2), "toast"
                    elif m.group(1) in self.labmac:
                        nm, k = m.group(2), "mac:" + m.group(1)
                    else:
                        continue
                else:
                    nm, k = m.group(3), (m.group(4) or "").lower()
                    if nm.lower() in ("es", "cs", "ss", "ds"):
                        continue
                if nm.startswith(".") and not nm.startswith(".."):
                    raw.append((i, cur + nm, k))
                    continue
                if k != "equ":
                    cur = nm
                    S.topidx.append(i)
                    S.topnm.append(nm)
                raw.append((i, nm, k))
            for i, nm, k in raw:
                self.defs.setdefault(nm, []).append((r, i, k))
            ln, last = 0, 0
            ms = [m for rx in REFS for m in rx.finditer(st)
                  if not word_before(st, m.start())]
            ms.sort(key=lambda m: m.start())
            for m in ms:
                ln += st.count("\n", last, m.start())
                last = m.start()
                self.refs.setdefault(S.full(m.group(1), ln), []).append((r, ln))
            ln, last = 0, 0
            for m in STORE.finditer(st):
                if word_before(st, m.start()):
                    continue
                ln += st.count("\n", last, m.start())
                last = m.start()
                self.stores.setdefault(S.full(m.group(1), ln), []).append(
                    (r, ln, m.group(2)))

    def structure(self, texts):
        """The two things the assembler cannot say: that the SDK's macros are
        the kernel's, and that every TOASTRUN is closed - NASM ends a file
        with a context still pushed and says nothing, and an unclosed run
        checks nothing."""
        blocks = []
        for r in ("kernel/kernel.asm", "apps/os88api.inc"):
            m = re.search(r"; ---- BEGIN TOAST MACROS[^\n]*\n(.*?); ---- END TOAST MACROS",
                          texts.get(r, ""), re.S)
            if not m:
                fail("%s has lost its `; ---- BEGIN TOAST MACROS` block - the "
                     "macros that measure a toast where it is written" % r)
            blocks.append(m.group(1))
        if blocks[0] != blocks[1]:
            self.bad.append("the TOAST macro blocks of kernel/kernel.asm and "
                            "apps/os88api.inc differ - a package and the kernel "
                            "would measure a toast two ways. Make them the same")
        for r, t in texts.items():
            if "TOASTRUN" not in t and "TOASTEND" not in t:
                continue
            open_at = None
            for m in re.finditer(r"^[ \t]*(TOASTRUN|TOASTEND)\b", t, re.M):
                ln = t.count("\n", 0, m.start()) + 1
                if m.group(1) == "TOASTRUN":
                    if open_at:
                        self.bad.append("%s:%d opens a TOASTRUN inside the one "
                                        "opened at line %d" % (r, ln, open_at))
                    open_at = ln
                else:
                    if not open_at:
                        self.bad.append("%s:%d TOASTEND closes no TOASTRUN" % (r, ln))
                    open_at = None
            if open_at:
                self.bad.append("%s:%d TOASTRUN is never closed with TOASTEND, so "
                                "nothing measures it" % (r, open_at))

    # ---- lookups -------------------------------------------------------
    def lookup_all(self, name, f):
        ds = [d for d in self.defs.get(name, ()) if d[0] in self.group.get(f, {f})]
        if not ds and "." in name[1:]:
            d = self.local_def(name, f)
            ds = [d] if d else []
        return ds

    def lookup(self, name, f):
        ds = self.defs.get(name, ())
        vis = [d for d in ds if d[0] in self.group.get(f, {f})]
        if vis:
            return vis[0]
        if "." in name[1:]:
            return self.local_def(name, f)
        return None

    def span(self, proc, f):
        """(file, first line, end line) of the top-level label proc."""
        d = None
        for x in self.defs.get(proc, ()):
            if x[0] in self.group.get(f, {f}):
                d = x
                break
        if d is None:
            return None
        S = self.src[d[0]]
        k = bisect.bisect_right(S.topidx, d[1])
        end = S.topidx[k] if k < len(S.topidx) else len(S.lines)
        return (d[0], d[1], end)

    def local_def(self, name, f):
        key = ("def", name, f)
        if key not in self.lcache:
            self.lcache[key] = None
            sp = self.span(name.split(".", 1)[0], f)
            if sp:
                S = self.src[sp[0]]
                for i in range(sp[1], sp[2]):
                    L = S.parse(i)
                    if L.label == name:
                        k = L.data[0] if L.data else ""
                        self.lcache[key] = (sp[0], i, k)
                        break
        return self.lcache[key]

    def refs_of(self, label, f):
        """Every branch to label: the indexed (top-level spelling) ones, and
        for a .local the ones inside its procedure, found on demand."""
        out = list(self.refs.get(label, ()))
        if "." in label[1:]:
            key = ("refs", label, f)
            if key not in self.lcache:
                got = []
                sp = self.span(label.split(".", 1)[0], f)
                if sp:
                    S = self.src[sp[0]]
                    for i in range(sp[1], sp[2]):
                        if "." not in S.lines[i]:
                            continue
                        L = S.parse(i)
                        if L.op and BRANCH.match(L.op) and L.args:
                            t = branch_target(L.args[0])
                            if t and t.startswith(".") and S.full(t, i) == label:
                                got.append((sp[0], i))
                self.lcache[key] = got
            out += self.lcache[key]
        return out

    def kind_of(self, d):
        """What a definition IS: toast, db, dw, buf, code, mac:<NAME>."""
        r, i, k = d
        if k == "toast":
            return "toast"
        if k.startswith("mac:"):
            body = self.macros.get(k[4:], "")
            if "TOASTSTR" in body or "TOAST_MAX" in body:
                return "toast"
            if re.search(r"\bres[bw]\b|\bequ\b", body):
                return "buf"
            if re.search(r"\bdw\b", body) and not re.search(r"\bdb\b", body):
                return "dw"
            return k
        if k in ("resb", "resw", "resd", "times", "equ"):
            return "buf"                # a package's bss is `equ` offsets
        if k in ("db", "dw", "dd", "equ"):
            return k
        # a bare label: look at what follows it
        S = self.src[r]
        L = S.parse(i)
        if L.data:
            k = L.data[0]
            return "buf" if k in ("resb", "resw", "resd", "times", "equ") else k
        if L.op:
            return "code"
        j = i + 1
        while j < len(S.lines):
            L = S.parse(j)
            if L.label or L.op or L.dir:
                break
            if L.data:
                k = L.data[0]
                return "buf" if k in ("resb", "resw", "times") else k
            j += 1
        return "code"

    def table(self, d):
        """The word entries of the table at d, up to the next label."""
        r, i, k = d
        S = self.src[r]
        out = []
        j = i
        while j < len(S.lines):
            L = S.parse(j)
            if j > i and (L.label or L.op):
                break
            if L.data and L.data[0] == "dw":
                for w in split_args(L.data[1]):
                    w = re.sub(r"\s*[+-]\s*\w+$", "", w.strip())
                    if w and not NUM.match(w):
                        out.append(S.full(w, j))
            elif L.data and j > i:
                break
            j += 1
        return out

    # ---- emitting ------------------------------------------------------
    def trace(self, src):
        dbg = os.environ.get("TOASTDBG")
        if not dbg or src[0] != "dyn" or dbg not in " ".join(src[1:]):
            return
        print("TRACE", src)
        for par, frame in reversed(self.stack):
            k = self.cur if par is self.stack[-1][0] else None
            k = k or (list(par)[-1] if par else None)
            last = None
            n = 0
            while k is not None and n < 4000:
                if k[1] != last or k[0] == "p":
                    print("   %s %s:%d %s | %s" % (k[0], k[1], k[2] + 1, k[3],
                          self.src[k[1]].lines[k[2]].strip()[:70]))
                last = k[1]
                k = par.get(k)
                n += 1
            print("  -- frame", frame[0] if frame else None)

    def emit(self, ctx, src):
        self.trace(src)
        if isinstance(ctx, list):
            ctx.append(src)
            return
        why = ctx
        if src[0] == "lab":
            self.classify(src[1], src[2], src[3], why)
        elif src[0] == "dyn":
            self.dyn.setdefault(src[1:], []).append(why)
        else:
            self.bad.append("%s (from the toast at %s)" % (src[1], why))

    def classify(self, name, f, kind, why, depth=0):
        if name.startswith("%%"):
            self.classify_mlocal(name, f, kind, why)
            return
        ds = self.lookup_all(name, f)
        if not ds:
            self.bad.append("%s names `%s`, which is defined nowhere it can see "
                            "(from the toast at %s)" % (f, name, why))
            return
        for d in ds:                    # EVERY arm of a %if defines it
            self.classify1(name, d, kind, why, depth)

    def classify1(self, name, d, kind, why, depth):
        k = self.kind_of(d)
        where = "%s:%d" % (d[0], d[1] + 1)
        if kind == "tab":
            if k != "dw":
                self.dyn.setdefault((d[0], name, "table"), []).append(why)
                return
            for e in self.table(d):
                self.classify(e, d[0], "str", why + " via table " + name, depth + 1)
            return
        if k == "toast":
            self.ok.setdefault(name, set()).add(why)
        elif k == "buf":
            self.dyn.setdefault((d[0], name, "buffer"), []).append(why)
        elif k == "db":
            self.bad.append("%s `%s` is a raw db string that reaches a toast at "
                            "%s - define it with TOASTSTR" % (where, name, why))
        elif k.startswith("mac:"):
            self.bad.append("%s `%s` is made by the macro %s, which does not "
                            "check TOAST_MAX, and reaches a toast at %s"
                            % (where, name, k[4:], why))
        elif k == "dw":
            self.dyn.setdefault((d[0], name, "pointer"), []).append(why)
        else:
            self.bad.append("%s `%s` is %s, not a string, and reaches a toast at %s"
                            % (where, name, k, why))

    def classify_mlocal(self, name, f, kind, why):
        S = self.src[f]
        for ln in S.lines:
            s = strip_comment(ln).strip()
            if re.match(r"^TOASTSTR\s+" + re.escape(name) + r"\s*(?:,|$)", s):
                self.ok.setdefault(name, set()).add(why)
                return
        self.bad.append("%s: the macro-local `%s` reaches a toast at %s and is not "
                        "a TOASTSTR" % (f, name, why))

    # ---- the walk ------------------------------------------------------
    def walk(self, f, i, reg, kind, ctx, frame=None, visited=None, cf=None):
        """Every path INTO line i of f, for the register reg. cf is what the
        path already knows about the carry flag there (1, 0 or None)."""
        if visited is None:
            visited = set()
        work = [("p", f, i, reg, cf, None)]
        par = {}
        self.stack.append((par, frame))
        while work:
            key = work.pop()
            what, f, j, reg, cf, fz = key
            if key in visited:
                continue
            visited.add(key)
            self.cur = key
            S = self.src[f]
            L = S.parse(j)
            if what == "x":
                if L.data or (L.op and UNCOND.match(L.op)):
                    continue            # control does not fall out of it
                if L.op in CF_TAKEN:    # falling through = NOT taken
                    want = 1 - CF_TAKEN[L.op]
                    if cf is not None and cf != want:
                        continue        # this path cannot happen
                    cf = want
                if fz is not None:
                    # between a pushf and its popf: the flags that matter are
                    # the ones the pushf saved, so nothing here moves them
                    r = self.effect(S, j, reg, kind, ctx, frame, visited, None)
                    if r is None:
                        continue
                    reg = r[0]
                    if j <= fz:
                        fz = None
                elif L.op == "popf" and cf is not None:
                    fz = self.pushf_of(S, j)
                    if fz is None:
                        cf = None
                else:
                    r = self.effect(S, j, reg, kind, ctx, frame, visited, cf)
                    if r is None:
                        continue
                    reg, cf = r
                if L.dir:
                    nxt = self.directive(S, j)
                    if nxt is not None:
                        for k in nxt:
                            nk = ("x", f, k, reg, cf, fz)
                            work.append(nk)
                            par.setdefault(nk, key)
                        continue
            # how control reaches line j
            if frame is not None and (f, j) == frame[0]:
                frame[2][0] = True               # passes through the callee
                continue
            followed = False
            if L.label and not L.data:
                if (f, L.label) in self.cuts:
                    self.cutused.add((f, L.label))
                    continue
                for rf, ri in self.refs_of(L.label, f):
                    if rf not in self.group[f]:
                        continue
                    if frame is not None and (rf, ri) not in frame[1]:
                        continue
                    followed = True
                    rcf = cf
                    rop = self.src[rf].parse(ri).op
                    if rop in CF_TAKEN:
                        if cf is not None and cf != CF_TAKEN[rop]:
                            continue
                        rcf = CF_TAKEN[rop]
                    nk = ("p", rf, ri, reg, rcf, None)
                    work.append(nk)
                    par.setdefault(nk, key)
            k = j - 1
            while k >= 0:
                Lk = S.parse(k)
                if Lk.op or Lk.data or Lk.label or Lk.dir in STRUCT:
                    break
                k -= 1
            if k < 0:
                if not followed:
                    self.emit(ctx, ("dyn", f, S.top(j), "<file start>"))
                continue
            Lk = S.parse(k)
            if Lk.data or (Lk.op and UNCOND.match(Lk.op)) or \
                    (frame is not None and (f, k) not in frame[1]):
                # nothing falls in: an ENTRY if it is a label nobody branches
                # to (a callback), and otherwise a path no configuration runs
                if not followed and frame is None and L.label and not L.data:
                    self.emit(ctx, ("dyn", f, S.top(j), "<entry>"))
                continue
            nk = ("x", f, k, reg, cf, fz)
            work.append(nk)
            par.setdefault(nk, key)
        self.stack.pop()

    def forward_inner(self, S, k, j):
        """A conditional branch at k whose target lies between k and j."""
        L = S.parse(k)
        if not L.args or L.op == "jmp":
            return False
        t = branch_target(L.args[0])
        if not t:
            return False
        t = S.full(t, k)
        for i in range(k + 1, j):
            if S.parse(i).label == t:
                return True
        return False

    def pushf_of(self, S, j):
        """The line of the pushf a popf at j restores, in straight-line code."""
        depth, k = 0, j - 1
        while k >= 0 and j - k < 60:
            L = S.parse(k)
            if L.label or L.data or L.dir or (L.op and (UNCOND.match(L.op) or (
                    BRANCH.match(L.op) and L.op != "call"))):
                return None
            if L.op == "popf":
                depth += 1
            elif L.op == "pushf":
                if depth == 0:
                    return k
                depth -= 1
            k -= 1
        return None

    def directive(self, S, j):
        """Predecessor lines of a preprocessor line met walking backwards, or
        None when it is transparent."""
        L = S.parse(j)
        d = L.dir
        if d in ("%macro", "%imacro"):
            return []
        if d == "%endif":
            arms, k, depth = [], j - 1, 0
            while k >= 0:
                dk = S.parse(k).dir
                if dk == "%endif":
                    depth += 1
                elif dk and dk.startswith("%if"):
                    if depth == 0:
                        break
                    depth -= 1
                elif dk and (dk == "%else" or dk.startswith("%elif")) and depth == 0:
                    arms.append(k)
                k -= 1
            out = [j - 1] + [a - 1 for a in arms]
            if not any(S.parse(a).dir == "%else" for a in arms):
                out.append(k - 1)
            return out
        if d == "%else" or (d and d.startswith("%elif")):
            k, depth = j - 1, 0
            while k >= 0:
                dk = S.parse(k).dir
                if dk == "%endif":
                    depth += 1
                elif dk and dk.startswith("%if"):
                    if depth == 0:
                        return [k - 1]
                    depth -= 1
                k -= 1
            return []
        return None

    def effect(self, S, j, reg, kind, ctx, frame, visited, cf):
        """Line j's effect on reg walking backwards: (the register it is
        carried in before line j, the carry flag known there), or None when
        this path ends here."""
        L = S.parse(j)
        if not L.op:
            return (reg, cf)
        body = self.expandable(L)
        if body is None:
            return self.effect1(S, j, L, reg, kind, ctx, frame, visited, cf)
        for M in reversed(body):
            r = self.effect1(S, j, M, reg, kind, ctx, frame, visited, cf)
            if r is None:
                return None
            reg, cf = r
        return (reg, cf)

    def expandable(self, L):
        """A macro used as an instruction, expanded - when it is plain code
        (no labels, no directives); None otherwise."""
        body = self.macros.get(L.opraw or "")
        if body is None or "%%" in body or re.search(r"^\s*%[a-z]", body, re.M) \
                or re.search(r"^\s*" + NAME + r"\s*:", body, re.M):
            return None
        out = []
        for ln in body.split("\n"):
            for n, a in enumerate(L.args, 1):
                ln = ln.replace("%%{%d}" % n, a).replace("%%%d" % n, a)
            s = ln.strip()
            if not s:
                continue
            M = Line()
            M.label = M.data = M.dir = None
            parts = s.split(None, 1)
            M.opraw = parts[0]
            M.op = parts[0].lower()
            M.args = split_args(parts[1]) if len(parts) > 1 else []
            if M.op in ("es", "cs", "ss", "ds") and M.args:
                p2 = M.args[0].split(None, 1)
                M.op = p2[0].lower()
                M.args = split_args(p2[1]) if len(p2) > 1 else []
            out.append(M)
        return out

    def effect1(self, S, j, L, reg, kind, ctx, frame, visited, cf):
        f = S.rel
        if not L.op:
            return (reg, cf)
        op, a = L.op, [x.lower() for x in L.args]
        top = S.top(j)
        # the carry flag first: an instruction that SETS it decides whether a
        # path that needs a value of it can exist at all
        want = cf
        if op in ("stc", "clc"):
            if cf is not None and cf != (op == "stc"):
                return None
            cf = None
        elif op in CF_CLEAR:
            if cf == 1:
                return None             # `or ax, ax` cannot leave CF set
            cf = None
        elif op in CF_WRITE or op.startswith("rep") or op == "call":
            cf = None
        if op == "call" and L.args:
            t = branch_target(L.args[0])
            if not t:
                return (reg, cf)
            t = S.full(t, j)
            if t in SINKS or t in SINK_BODIES:
                return (reg, cf)
            if COPIERS.get(t) == reg:
                return (reg, cf)        # the string comes out as it went in
            if t in PRODUCERS:
                if reg != "si":
                    return (reg, cf)
                d = self.lookup(PRODUCERS[t], "kernel/kernel.asm")
            else:
                d = self.lookup(t, f)
            if d is None or d[2] != "":
                return (reg, cf)
            srcs, through = self.summary(d, reg, kind, want)
            for x in srcs:
                self.emit(ctx, x)
            return (reg, cf) if through else None
        if op in ("mov", "lea") and len(a) == 2 and a[0] == reg:
            src = L.args[1].strip()
            sl = src.lower()
            if sl in ("sp", "bp"):
                self.emit(ctx, ("dyn", f, top, "mov%s,%s" % (reg, sl)))
                return None             # composed on the stack
            if sl in REGS:
                return (sl, cf)
            if "[" in sl:
                self.memory(S, j, src, reg, kind, ctx, op == "lea")
                return None
            if NUM.match(sl):
                return None             # a number - a code, not text
            t = re.sub(r"\s*\+\s*\w+$", "", src)
            if IDENT.match(t) and not NUM.match(t):
                self.emit(ctx, ("lab", S.full(t, j), f, kind))
                return None
            self.emit(ctx, ("dyn", f, top, "mov%s,%s" % (reg, src.replace(" ", ""))))
            return None
        if op == "xchg" and len(a) == 2 and reg in a:
            o = a[1] if a[0] == reg else a[0]
            if o in REGS:
                return (o, cf)
            if "[" in o:
                self.memory(S, j, L.args[1] if a[0] == reg else L.args[0],
                            reg, kind, ctx, False)
                return None
        if op in ("or", "and") and len(a) == 2 and a[0] == a[1]:
            return (reg, cf)
        if op in ("xor", "sub") and len(a) == 2 and a[0] == a[1] == reg:
            return None                 # a NULL: no text, so nothing too long
        if op == "inc" and a and a[0] == reg or (
                op == "add" and len(a) == 2 and a[0] == reg and
                (NUM.match(a[1]) or re.match(r"^[A-Z_][A-Z0-9_]*$", L.args[1]))):
            return (reg, cf)            # a SUFFIX of the string: never longer
        if op == "pop" and a and a[0] == reg:
            # its push, in the code above: a label on the way is fine when
            # every branch to it comes from between that push and this pop
            # (a skip over a few lines), and nothing else is
            depth, k, inner = 0, j - 1, []
            while k >= 0:
                Lk = S.parse(k)
                if Lk.label and not Lk.data and not Lk.op:
                    inner += self.refs_of(Lk.label, f)
                    k -= 1
                    continue
                if Lk.data or Lk.dir or Lk.label or (
                        Lk.op and (UNCOND.match(Lk.op) or
                                   (BRANCH.match(Lk.op) and Lk.op != "call"
                                    and not any((f, k) == r for r in inner)
                                    and not self.forward_inner(S, k, j)))):
                    break
                if Lk.op == "pop":
                    depth += 1
                elif Lk.op == "push":
                    if depth == 0:
                        x = Lk.args[0].lower() if Lk.args else ""
                        if x in REGS and all(rf == f and k < ri < j
                                             for rf, ri in inner):
                            self.walk(f, k, x, kind, ctx, frame, visited, cf)
                            return None
                        break
                    depth -= 1
                k -= 1
            if frame is not None:
                # inside a callee, a pop the walk cannot pair in its block is
                # the epilogue restoring what the prologue saved: the caller's
                # value comes back, so the register passes through
                frame[2][0] = True
                return None
            self.emit(ctx, ("dyn", f, top, "pop" + reg))
            return None
        if a and (a[0] == reg or HALVES.get(a[0]) == reg) and op not in (
                "cmp", "test", "push", "out") and not BRANCH.match(op) \
                and not op.startswith("j") and not op.startswith("int"):
            self.emit(ctx, ("dyn", f, top, "%s%s" % (op, ",".join(L.args).replace(" ", ""))))
            return None
        for rx, regs in IMPLICIT:
            if rx.match(op) and reg in regs:
                self.emit(ctx, ("dyn", f, top, op.replace(" ", "")))
                return None
        if op in ("les", "lds") and a and a[0] == reg:
            self.emit(ctx, ("dyn", f, top, op))
            return None
        return (reg, cf)

    def memory(self, S, j, src, reg, kind, ctx, lea):
        f = S.rel
        top = S.top(j)
        inner = src[src.index("[") + 1:src.rindex("]")]
        inner = re.sub(r"^\s*[cdes]s\s*:", "", inner, flags=re.I)
        toks = [x for x in re.split(r"[\s+\-*]+", inner) if x]
        regs = [x for x in toks if x.lower() in REGS]
        labs = [S.full(x, j) for x in toks
                if x.lower() not in REGS and IDENT.match(x) and not NUM.match(x)]
        if lea:
            if len(labs) == 1 and not regs:
                self.emit(ctx, ("lab", labs[0], f, kind))
            else:
                self.emit(ctx, ("dyn", f, top, "lea%s,%s" % (reg, src.replace(" ", ""))))
            return
        words, vars_ = [], []
        for x in labs:
            d = self.lookup(x, f)
            if d is not None and self.kind_of(d) == "dw":
                words.append((x, d))
            elif d is not None and self.kind_of(d) == "buf":
                vars_.append((x, d))
        if not words and len(vars_) == 1 and not regs:
            words = vars_
        if len(words) == 1 and kind == "str" and (
                not regs or self.kind_of(words[0][1]) == "dw"):
            x, d = words[0]
            ents = self.table(d) if self.kind_of(d) == "dw" else []
            if regs or len(ents) > 1:
                for e in ents:
                    self.emit(ctx, ("lab", e, d[0], "str"))
                if not ents:
                    self.emit(ctx, ("dyn", f, top, "mov%s,%s" % (reg, src.replace(" ", ""))))
                return
            # a pointer VARIABLE: every store to it, and its initial value
            stores = [s for s in self.stores.get(x, ()) if s[0] in self.group[f]]
            for e in ents:
                self.emit(ctx, ("lab", e, d[0], "str"))
            if not stores and not ents:
                self.emit(ctx, ("dyn", f, top, "mov%s,%s" % (reg, src.replace(" ", ""))))
            for (sf, si, sv) in stores:
                v = sv.strip()
                vl = v.lower()
                if vl in REGS:
                    self.walk(sf, si, vl, kind, ctx)
                elif IDENT.match(v) and not NUM.match(v):
                    self.emit(ctx, ("lab", self.src[sf].full(v, si), sf, kind))
                elif vl in ("0",) or NUM.match(vl):
                    continue                  # cleared: nothing to say
                else:
                    self.emit(ctx, ("dyn", sf, self.src[sf].top(si),
                                    "store[%s],%s" % (x, v.replace(" ", ""))))
            return
        self.emit(ctx, ("dyn", f, top, "mov%s,%s" % (reg, src.replace(" ", ""))))

    def summary(self, d, reg, kind, cf=None):
        """(sources, passes-through) for reg across a call to the routine
        defined at d, from the exits that can leave the carry flag at cf:
        walked back from each of them to the routine's entry."""
        key = (d[0], d[1], reg, kind, cf)
        if key in self.memo:
            return self.memo[key]
        if key in self.busy:
            return ([], True)
        self.busy.add(key)
        reach, exits = self.reach(d[0], d[1])
        srcs, through = [], [False]
        frame = ((d[0], d[1]), reach, through)
        visited = set()
        for (ef, ei) in exits:
            ecf = self.exit_cf(ef, ei)
            if cf is not None and ecf is not None and ecf != cf:
                continue
            self.walk(ef, ei, reg, kind, srcs, frame, visited,
                      ecf if ecf is not None else cf)
        if not exits:
            through[0] = True
        self.busy.discard(key)
        res = (srcs, through[0])
        self.memo[key] = res
        return res

    def exit_cf(self, f, e):
        """The carry flag a `ret` leaves, when the straight line above it
        says (`stc` / `clc`); None when it does not."""
        S = self.src[f]
        k = e - 1
        while k >= 0 and e - k < 40:
            L = S.parse(k)
            if L.label or L.data or L.dir:
                return None
            if L.op == "stc":
                return 1
            if L.op == "clc":
                return 0
            if L.op and (L.op in CF_WRITE or L.op in CF_CLEAR or L.op == "call"
                         or BRANCH.match(L.op) or L.op.startswith("rep")):
                return None
            k -= 1
        return None

    def reach(self, f, i, limit=6000):
        seen, exits, work = set(), [], [(f, i)]
        while work:
            f, j = work.pop()
            if (f, j) in seen or len(seen) > limit:
                continue
            S = self.src.get(f)
            if S is None or j >= len(S.lines):
                continue
            seen.add((f, j))
            L = S.parse(j)
            if L.data:
                continue
            if L.dir and (L.dir == "%else" or L.dir.startswith("%elif")):
                k, depth = j + 1, 0
                while k < len(S.lines):
                    dk = S.parse(k).dir
                    if dk and dk.startswith("%if"):
                        depth += 1
                    elif dk == "%endif":
                        if depth == 0:
                            work.append((f, k + 1))
                            break
                        depth -= 1
                    k += 1
            if not L.op:
                work.append((f, j + 1))
                continue
            if EXIT.match(L.op):
                exits.append((f, j))
                continue
            if BRANCH.match(L.op) or L.op == "jmp":
                t = branch_target(L.args[0]) if L.args else None
                if L.op != "call":
                    dd = self.lookup(S.full(t, j), f) if t else None
                    if dd is not None:
                        work.append((dd[0], dd[1]))
                    elif L.op == "jmp":
                        exits.append((f, j))       # a tail call out of the tree
                    if L.op == "jmp":
                        continue
            work.append((f, j + 1))
        return seen, exits

    # ---- driving -------------------------------------------------------
    def run(self):
        for t, (reg, kind) in SINKS.items():
            for f, i in self.refs.get(t, ()):
                S = self.src[f]
                top = S.top(i)
                if top in SINK_BODIES:
                    continue
                L = S.parse(i)
                if L.op not in ("call", "jmp"):
                    continue
                self.sites += 1
                self.walk(f, i, reg, kind, "%s:%d" % (f, i + 1))


# ---- C --------------------------------------------------------------------
CLIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
CFUNC = re.compile(r"^[A-Za-z_][\w \t\*]*?\b(\w+)[ \t]*\(([^;{}]*?)\)[ \t\n]*\{", re.M)


def c_strlen(body):
    n, i = 0, 0
    while i < len(body):
        if body[i] == "\\":
            i += 1
            c = body[i] if i < len(body) else ""
            if c == "x":
                i += 1
                while i < len(body) and body[i] in "0123456789abcdefABCDEF":
                    i += 1
                n += 1
                continue
            if c in "01234567":
                k = 0
                while i < len(body) and body[i] in "01234567" and k < 3:
                    i += 1
                    k += 1
                n += 1
                continue
        n += 1
        i += 1
    return n


def c_args(text, pos):
    """The argument strings of the call whose '(' is at pos."""
    depth, q, cur, out, i = 0, None, "", [], pos
    while i < len(text):
        c = text[i]
        if q:
            cur += c
            if c == "\\":
                cur += text[i + 1]
                i += 2
                continue
            if c == q:
                q = None
        elif c in "\"'":
            q = c
            cur += c
        elif c == "(":
            depth += 1
            if depth > 1:
                cur += c
        elif c == ")":
            depth -= 1
            if depth == 0:
                out.append(cur.strip())
                return out
            cur += c
        elif c == "," and depth == 1:
            out.append(cur.strip())
            cur = ""
        else:
            cur += c
        i += 1
    return out


def c_check(gate, cap):
    files = set()
    for pat in C_GLOBS:
        files.update(glob.glob(os.path.join(ROOT, pat), recursive=True))
    rels = sorted(os.path.relpath(f, ROOT) for f in files
                  if "/hosttest/" not in f.replace(os.sep, "/"))
    texts = {}
    for r in rels:
        t = open(os.path.join(ROOT, r), encoding="utf-8", errors="replace").read()
        # comments out, keeping the newlines so line numbers hold
        t = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), t,
                   flags=re.S)
        t = re.sub(r"//[^\n]*", "", t)
        texts[r] = t
    sites = [0]

    def lineno(r, pos):
        return texts[r].count("\n", 0, pos) + 1

    def enclosing(r, pos):
        best = None
        for m in CFUNC.finditer(texts[r], 0, pos):
            best = m
        return best

    def check_arg(r, pos, arg, why, depth):
        m = re.match(r"^(.+?)\?((?:\s*\"(?:[^\"\\\n]|\\.)*\")+)\s*:"
                     r"((?:\s*\"(?:[^\"\\\n]|\\.)*\")+)\s*$", arg, re.S)
        if m:
            check_arg(r, pos, m.group(2), why, depth)
            check_arg(r, pos, m.group(3), why, depth)
            return
        lits = CLIT.findall(arg)
        rest = CLIT.sub("", arg).strip()
        if lits and not rest:
            n = sum(c_strlen(x) for x in lits)
            if n > cap:
                gate.bad.append("%s:%d a %d-character literal reaches os88_toast "
                                "(%s) and TOAST_MAX is %d: \"%s\""
                                % (r, lineno(r, pos), n, why, cap, "".join(lits)))
            return
        fn = enclosing(r, pos)
        fname = fn.group(1) if fn else ""
        if re.match(r"^\w+$", arg) and fn:
            params = [p.strip() for p in fn.group(2).split(",")]
            names = [re.findall(r"(\w+)\s*(?:\[\s*\])?$", p) for p in params]
            names = [n[0] if n else "" for n in names]
            if arg in names and depth < 6:
                idx = names.index(arg)
                d = os.path.dirname(r)
                found = False
                for r2 in rels:
                    if os.path.dirname(r2) != d:
                        continue
                    for m in re.finditer(r"\b%s\s*\(" % re.escape(fname), texts[r2]):
                        if r2 == r and m.start() == fn.start(1):
                            continue
                        a = c_args(texts[r2], m.end() - 1)
                        if any(re.search(r"\b(?:char|int|unsigned|const|void|"
                                         r"struct|long|short)\b", CLIT.sub("", x))
                               for x in a):
                            continue          # a declaration, not a call
                        if idx < len(a):
                            found = True
                            check_arg(r2, m.start(), a[idx],
                                      "%s via %s()" % (why, fname), depth + 1)
                if found:
                    return
            # a local pointer the function ASSIGNS: every value it is given
            if fn and depth < 6:
                nxt = CFUNC.search(texts[r], fn.end())
                body = texts[r][fn.end():nxt.start() if nxt else len(texts[r])]
                if re.search(r"\bchar\s*\*\s*%s\s*[;=,]" % re.escape(arg), body):
                    vals = re.findall(r"(?<![=!<>])\b%s\s*=(?!=)\s*([^;]+);"
                                      % re.escape(arg), body)
                    for v in vals:
                        check_arg(r, pos, v.strip(), why, depth + 1)
                    if vals:
                        return
            m = re.search(r"\b(?:static\s+)?(?:const\s+)?char\s+%s\s*\[\s*\w*\s*\]\s*=\s*"
                          r"((?:\s*\"(?:[^\"\\\n]|\\.)*\")+)" % re.escape(arg), texts[r])
            if m:
                check_arg(r, m.start(1), m.group(1), why, depth + 1)
                return
        gate.dyn.setdefault((r, fname, arg.replace(" ", "")), []).append(why)

    for r in rels:
        if r.startswith("apps/cc/") and r.endswith("os88.h"):
            continue
        for m in re.finditer(r"\bos88_toast\s*\(", texts[r]):
            pre = texts[r][max(0, m.start() - 40):m.start()]
            if re.search(r"\bint\s*$", pre):
                continue                      # the declaration
            a = c_args(texts[r], m.end() - 1)
            if not a:
                continue
            sites[0] += 1
            check_arg(r, m.start(), a[0], "%s:%d" % (r, lineno(r, m.start())), 0)
    return sites[0]


def allowlist():
    if not os.path.exists(ALLOW):
        fail("tests/toastdyn.txt is missing - it is where a run-time toast "
             "argument says why it is not fixed text")
    out = {}
    for n, line in enumerate(open(ALLOW, encoding="utf-8"), 1):
        body = line.split("#")[0].strip()
        if not body:
            continue
        parts = body.split()
        if len(parts) != 3 or "#" not in line:
            fail("tests/toastdyn.txt:%d is %r; a line is `<path> <proc> <what>  "
                 "# reason`" % (n, line.rstrip()))
        out[tuple(parts)] = n
    return out


def main():
    allow = allowlist()
    g = Gate()
    g.cuts = {(k[0], k[1]) for k in allow if k[2] == "cut"}
    g.load()
    g.run()
    if g.sites < 40:
        fail("only %d toast call site(s) found - the site pattern has stopped "
             "matching and this row is passing vacuously" % g.sites)
    csites = c_check(g, g.cap)
    bad = list(g.bad)
    for key, whys in sorted(g.dyn.items()):
        if key not in allow:
            bad.append("%s: in `%s`, `%s` reaches a toast (at %s) and is not fixed "
                       "text this row can measure. Make it a TOASTSTR if it is "
                       "fixed; if it really is built at run time, add `%s %s %s  "
                       "# <why>` to tests/toastdyn.txt"
                       % (key[0], key[1] or "?", key[2], whys[0], key[0],
                          key[1] or "?", key[2]))
    for key, n in sorted(allow.items(), key=lambda kv: kv[1]):
        if key[2] == "cut":
            if (key[0], key[1]) not in g.cutused:
                bad.append("tests/toastdyn.txt:%d cuts the walk at `%s`, and no "
                           "walk reaches it any more - take the line out"
                           % (n, key[1]))
        elif key not in g.dyn:
            bad.append("tests/toastdyn.txt:%d `%s` reaches no toast any more - "
                       "take the line out. THE LIST IS A RATCHET" % (n, " ".join(key)))
    if VERBOSE:
        for k in sorted(g.ok):
            print("t_toast:   ok %s" % k)
    if bad:
        for b in sorted(set(bad)):
            print("t_toast:   %s" % b)
        fail("%d problem(s)" % len(set(bad)))
    print("t_toast: TOAST_MAX %d; %d asm site(s) and %d C site(s); %d TOASTSTR "
          "label(s) reach a toast, %d run-time argument(s) registered"
          % (g.cap, g.sites, csites, len(g.ok), len(allow)))
    print("t_toast: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
