#!/usr/bin/env python3
"""os88pdstk - PageDesk's static stack gate (DECISIONS D21/D22/D23).

    python3 tools/os88pdstk.py [--build DIR] [--top N] [-v]

WHY IT EXISTS. Every PageDesk module verb used to run on the CALLER's stack,
the UI task's 512 bytes (SPEC.md 2.1, STK0_SIZE). PDREND's recursive bez()
went seven frames deep there and wrote over the kernel's menu bar, which is
the last thing in .lowbss under that stack. The canary did not catch it
(docs: rendfix-report). Water-marking the stack afterwards showed a bigger
problem. The shell, PDREND, PDENG's thirteen-frame lexer, the kernel's
READ_SEQ, int 13h and IRQ0 used 510 of the 512 bytes. D23 answers that with
a PRIVATE 2 KiB deep stack. pdu_mcall runs every module verb on it, and each
kernel call a module makes swaps back to the UI stack at the depth the shell
reached. This gate checks the two budgets that split leaves:

  DEEP   the door's entry (its pushes included) down through every C frame,
         push and return address, the nested PDREND/PDWRT -> PDENG call
         included. deepstk's figures (pagedesk/deepstk 6c3e70673):
         PD_DSTK_BYTES = 2048, an 8-byte record at the top and a 4-byte
         canary at the bottom; pdu_mcall loads SP = 2040 and far-calls the
         door, so the door is entered at 2036 and 2036 - 4 = 2032 is usable.
         Less D23's 256 for the ISR floor, the ROM's int 09h and sch_isr's
         frame: <= 1776.
  UI     the kernel's callback dispatch (DISPATCH = 48, measured: the UI
         task's top down to the package's C callback) + the primary's chain
         + the kernel API underneath (KAPI = 156, measured: READ_SEQ ->
         int 13h -> IRQ0 drawing the menu-bar progress widget, the deepest
         thing the kernel does on this stack). A module's kernel call runs
         at pdu_mcall's banked SP: the primary's chain + pdu_mcall's own
         frame, then SWAP = 8 (the deep SS:SP banked on the UI stack, 4,
         and the far return, 4) and the API (deepstk's figures).
         Must be <= 384, which leaves 128 for the interrupt floor.

It also REFUSES RECURSION (D21 a). A cycle anywhere in the call graph fails
and is named.

HOW. It reads the source each segment is assembled from. That is the top
.asm, every quoted %include (search path: the file's own directory, apps/,
build/), and the C as tools/cc8086.py left it in build/*.gen.asm. Macros are
expanded, so the SDK's CC_T_* thunks become what they are. %ifdef/%ifndef
take the defines seen so far. It does not run nasm: that is ~10 s for the
four segments, and this is a fast row. Per function it scans the
instructions in order. A push or pushf adds 2, a pop subtracts 2,
`sub sp, N` adds N, `mov bp, sp` / `mov sp, bp` record and restore the
frame, and a branch target takes the deepest depth that reaches it. The
deepest point and every call site are kept with their depths. Each call
then costs:

  near call     depth + 2 + the callee
  tail jmp      depth + the callee
  OSAPI_*       depth + 4 + KAPI on the UI stack; deep side + GATE 6: the
                thunk's call to its slot 2, the slot's call to pdk_up 2,
                pdk_up's one push 2 (deepstk)
  far edges     PDREND _pdr_eng and PDWRT _pdw_engcall -> PDENG's door;
                the primary's pdu_mcall -> the three doors and every
                occupant's; PDREND's "slot" call -> every occupant's door;
                an occupant's far call -> PDENG's door (20.22.2.4); the C overlay's
                cc_ovm_* / cc_ovv_* vectors -> the function they name, + OVL
  indirect      `call ax` / `call [bx]` (os88ui.inc's handlers) can't be
                followed. They are listed, never silently zero.

The figures are bytes below the return address the caller pushed. A
function's own figure includes its whole subtree.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DEEP_SIZE = 2048            # D23 1: PD_DSTK_BYTES (pdabi.h)
DEEP_RECORD = 8             # deepstk: the record at offsets 2040..2047
DEEP_FARRET = 4             # pdu_mcall's far call: the door is entered at 2036
DEEP_CANARY = 4             # deepstk: the canary at 0..3
DEEP_RESERVE = 256          # D23 5: ISR floor + int 09h ROM + sch_isr frame
DEEP_BUDGET = DEEP_SIZE - DEEP_RECORD - DEEP_FARRET - DEEP_CANARY - DEEP_RESERVE  # 1776
UI_BUDGET = 384             # D22 1 / D23: task 0's 512 less 128 for interrupts
DISPATCH = 48               # measured (rendfix): UI task top -> the C callback
KAPI = 156                  # measured (rendfix): READ_SEQ + int 13h + IRQ0 draw


def inline_cells(root="."):
    """The OSAPI_* names whose cell is an OSAPI_ICELL in kernel/kernel.asm -
    a store or a load and a retf, no call under it (kernel.asm's OSAPI_ICELL
    macro) - so they cost the far frame alone, not KAPI. Read from the cell's
    own address comment and apps/os88api.inc's %define, so a cell that stops
    being inline stops being cheap here without anyone editing this table."""
    try:
        k = open(os.path.join(root, "kernel/kernel.asm")).read()
        a = open(os.path.join(root, "apps/os88api.inc")).read()
    except OSError:
        return set()
    addrs = set(m.upper() for m in
                re.findall(r'OSAPI_ICELL\s+\d+\s*;\s*0x([0-9A-Fa-f]{4})', k))
    return set(n for n, ad in
               re.findall(r'%define\s+(OSAPI_\w+)\s+KERNEL_SEG:0x([0-9A-Fa-f]{4})', a)
               if ad.upper() in addrs)


ICELLS = inline_cells()
GATE = 6                    # deepstk: a module's kernel call on the DEEP stack -
                            # near call to the slot 2 + pdk_up's call 2 + push 2
SWAP = 8                    # deepstk: on the UI stack under pdu_mcall's banked
                            # SP - the deep SS:SP 4 + the far return 4
OVL = 8                     # cc_ovthunk / cc_ovmthunk's stash path + far frame

# THE SLOT'S OCCUPANTS (PAGEDESK-SPEC 20.22.2.4): one segment each, entered
# by the primary's pdu_mcall (PDV_X_INIT, PDV_KEY_*) and by PDREND through
# the door PDV_REND_SLOT gave it, and calling PDENG alone. THE NAMING RULE an
# owner follows so this gate reads it: the top source is
# apps/pagedesk/<name>.asm, the door `<name>_entry`, the dispatcher
# `_<name>_verb`; PDREND's far call into the slot names "slot" in its operand
# (`call far [_pdr_slot_off]`); and every far call an occupant makes is to
# PDENG's door. An occupant whose source is not in the tree yet (the stand-in,
# stubs/pdstub.asm) has nothing of its own to charge and is left out.
OCCUPANTS = ["pdxk", "pdxg", "pdxf", "pdxi", "pdxj"]
# an occupant whose door and dispatcher predate the rule: (door, dispatcher)
OCC_NAMES = {"pdxk": ("pdk_entry", "_pdk_verb"), "pdxg": ("pdx_entry", "_pdx_verb"),
             "pdxf": ("pdx_entry", "_pdxf_verb"), "pdxi": ("pdi_entry", "_pdi_verb")}


def occupant_segments(root=ROOT, names=OCCUPANTS):
    return [(n.upper(), "apps/pagedesk/%s.asm" % n) + OCC_NAMES.get(n, ("%s_entry" % n, "_%s_verb" % n))
            for n in names if os.path.isfile(os.path.join(root, "apps/pagedesk/%s.asm" % n))]


# segment: top source, door label, dispatcher (per-verb chains)
SEGMENTS = [
    ("PRIMARY", "apps/pagedesk/pagedesk.asm", None, None),
    ("PDENG", "apps/pagedesk/pdeng.asm", "pde_entry", "_pde_verb"),
    ("PDREND", "apps/pagedesk/pdrend.asm", "pdr_entry", "_pdr_verb"),
    ("PDWRT", "apps/pagedesk/pdwrt.asm", "pdw_entry", "_pdw_verb"),
] + occupant_segments()
# far edges: (segment, function, operand) -> list of (segment, label)
FAR_EDGES = {
    ("PRIMARY", "_pdu_mcall", "[si]"): [("PDENG", "pde_entry"), ("PDREND", "pdr_entry"),
                                        ("PDWRT", "pdw_entry")],
    ("PDREND", "_pdr_eng", "[_pdr_eng_off]"): [("PDENG", "pde_entry")],
    ("PDWRT", "_pdw_engcall", "[_pdw_engvec]"): [("PDENG", "pde_entry")],
}
MCALL = ("PRIMARY", "_pdu_mcall")

# BOUNDED RECURSION ALREADY IN THE TREE, each with its bound, its owner and
# the file. D21 forbids recursion, and this list is a RATCHET in the shape of
# tests/textsites.txt: it only shrinks. A cycle that is not here FAILS. A
# cycle here is charged (bound - 1) more trips round it, then the deepest
# exit, and it is printed on every run until its owner makes it iterative.
ALLOWED_RECURSION = {
    # empty since f6184ae14 (sget, verify-flate2) and 9ae748a75 (sc_val, pages)
}

CODE_SECTIONS = (".text", ".modc")
PREFIXES = {"rep", "repe", "repz", "repne", "repnz", "lock", "cs", "ds", "es", "ss",
            "o16", "a16"}
DATA_OPS = {"db", "dw", "dd", "dq", "dt", "resb", "resw", "resd", "times", "align",
            "alignb", "equ", "incbin", "istruc", "iend", "at", "cpu", "bits", "org",
            "global", "extern", "absolute", "struc", "endstruc"}
LABEL_RE = re.compile(r'^\s*([A-Za-z_.?@$][\w.?@$#~]*)\s*:(.*)$')
NUM_RE = re.compile(r'^[-+]?(0x[0-9a-fA-F]+|[0-9][0-9a-fA-F]*h|\d+)$')


def num(s):
    s = s.strip()
    if not NUM_RE.match(s):
        return None
    neg = s.startswith("-")
    s = s.lstrip("+-")
    v = int(s[:-1], 16) if s.lower().endswith("h") else int(s, 0)
    return -v if neg else v


def strip_comment(line):
    if "'" not in line and '"' not in line and "`" not in line:
        return line.split(";", 1)[0].rstrip()
    out, q = [], None
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
    return "".join(out).rstrip()


# ---------------------------------------------------------------------------
# reading: includes, %ifdef, macros
# ---------------------------------------------------------------------------
class Source:
    def __init__(self, top, build, defines=(), root=ROOT):
        self.root = root
        self.paths = [os.path.join(root, "apps"), build]
        self.defines = set(defines)
        self.macros = {}
        self.lines = []             # (file, lineno, text) after expansion
        self.missing = []
        self.mseq = 0
        self._read(top, 0)

    def _find(self, name, here):
        for d in [os.path.dirname(here)] + self.paths:
            p = os.path.join(d, name)
            if os.path.isfile(p):
                return p
        return None

    def _read(self, path, depth):
        if depth > 12:
            return
        with open(path, errors="replace") as f:
            raw = f.read().split("\n")
        cond = []                   # stack of [active, taken]
        mac = None
        for n, line in enumerate(raw, 1):
            s = strip_comment(line).strip()
            if mac is not None:
                if re.match(r'%endm(acro)?\b', s, re.I):
                    self.macros[mac[0]] = mac[1]
                    mac = None
                else:
                    mac[1].append(line)
                continue
            if not s.startswith("%"):
                if not cond or all(c[0] for c in cond):
                    self._emit(path, n, line, 0)
                continue
            active = all(c[0] for c in cond)
            m = re.match(r'%(if|ifdef|ifndef|ifidn|ifidni|ifnum|ifstr|ifid|ifmacro|elif|elifdef|elifndef|else|endif)\b\s*(.*)', s, re.I)
            if m:
                k, arg = m.group(1).lower(), m.group(2).strip()
                if k.startswith("if"):
                    v = self._test(k, arg)
                    cond.append([v, v])
                elif k in ("elif", "elifdef", "elifndef"):
                    if cond:
                        v = (not cond[-1][1]) and self._test(k.replace("el", "", 1) or "if", arg)
                        cond[-1][0] = v
                        cond[-1][1] = cond[-1][1] or v
                elif k == "else":
                    if cond:
                        cond[-1][0] = not cond[-1][1]
                        cond[-1][1] = True
                elif k == "endif":
                    if cond:
                        cond.pop()
                continue
            if not active:
                continue
            m = re.match(r'%(i?)macro\s+([\w.]+)', s, re.I)
            if m:
                mac = (m.group(2), [])
                continue
            m = re.match(r'%(x|i|ix)?define\s+([\w.]+)', s, re.I)
            if m:
                self.defines.add(m.group(2))
                continue
            m = re.match(r'%include\s+"([^"]+)"', s, re.I)
            if m:
                p = self._find(m.group(1), path)
                if p:
                    self._read(p, depth + 1)
                else:
                    self.missing.append(m.group(1))
                continue
            if s.startswith("%"):
                continue
            self._emit(path, n, line, 0)

    def _test(self, k, arg):
        name = arg.split()[0] if arg else ""
        if k == "ifdef":
            return name in self.defines
        if k == "ifndef":
            return name not in self.defines
        if k == "ifmacro":
            return name in self.macros
        return True                 # %if <expr>: the first arm (assertions, mostly)

    def _emit(self, path, n, line, depth):
        s = strip_comment(line).strip()
        lab = LABEL_RE.match(s)
        body = lab.group(2).strip() if lab else s
        tok = body.split(None, 1)
        if tok and tok[0] in self.macros and depth < 6:
            if lab:
                self.lines.append((path, n, lab.group(1) + ":"))
            args = [a.strip() for a in self._split_args(tok[1] if len(tok) > 1 else "")]
            self.mseq += 1
            for bl in self.macros[tok[0]]:
                t = bl
                for i in range(9, 0, -1):
                    v = args[i - 1] if i <= len(args) else ""
                    t = t.replace("%%{%d}" % i, v).replace("%%%d" % i, v)
                t = re.sub(r'%%([\w.]+)', lambda m: "..mx%d_%s" % (self.mseq, m.group(1)), t)
                ts = strip_comment(t).strip()
                if ts.startswith("%"):
                    m = re.match(r'%(x|i|ix)?define\s+([\w.]+)', ts, re.I)
                    if m:
                        self.defines.add(m.group(2))
                    continue
                self._emit(path, n, t, depth + 1)
            return
        self.lines.append((path, n, s))

    @staticmethod
    def _split_args(s):
        out, cur, depth, q = [], "", 0, None
        for ch in s:
            if q:
                cur += ch
                if ch == q:
                    q = None
            elif ch in "'\"":
                q = ch
                cur += ch
            elif ch in "([{":
                depth += 1
                cur += ch
            elif ch in ")]}":
                depth -= 1
                cur += ch
            elif ch == "," and depth == 0:
                out.append(cur)
                cur = ""
            else:
                cur += ch
        if cur.strip():
            out.append(cur)
        return out


# ---------------------------------------------------------------------------
# per-function scan
# ---------------------------------------------------------------------------
class Func:
    def __init__(self, seg, name, where):
        self.seg, self.name, self.where = seg, name, where
        self.items = []             # ("label", name) / ("ins", mnem, ops, where)
        self.maxd = 0
        self.sites = []             # (kind, target, depth, where)
        self.notes = []
        self.bank_d = None          # the depth at the first SS/SP load (pdu_mcall's bank)


def is_local(name):
    return name.startswith(".") or re.match(r'^L\d+$', name) or name.startswith("..@") \
        or name.startswith("..mx")


def split_functions(seg, src):
    root = getattr(src, "root", ROOT)
    funcs, order = {}, []
    sect = ".text"
    cur = None
    prev_uncond = True
    rel = {}
    for path, n, s in src.lines:
        if not s:
            continue
        if path not in rel:
            rel[path] = os.path.relpath(path, root)
        m = re.match(r'(section|segment)\s+(\S+)', s, re.I)
        if m:
            sect = m.group(2)
            continue
        if sect not in CODE_SECTIONS:
            continue
        lab = LABEL_RE.match(s)
        if lab:
            name = lab.group(1)
            s = lab.group(2).strip()
            if is_local(name) and cur is not None:
                cur.items.append(("label", name))
            elif not is_local(name):
                where = "%s:%d" % (rel[path], n)
                f = Func(seg, name, where)
                if cur is not None and not prev_uncond and cur.items:
                    cur.items.append(("ins", "jmp", name, where))   # fall-through
                if name in funcs:
                    name = name + "#dup%d" % len(funcs)
                    f.name = name
                funcs[name] = f
                order.append(name)
                cur = f
                prev_uncond = False
            if not s:
                continue
        if cur is None:
            continue
        parts = s.split(None, 1)
        mn = parts[0].lower()
        ops = parts[1].strip() if len(parts) > 1 else ""
        while mn in PREFIXES and ops:
            p2 = ops.split(None, 1)
            mn, ops = p2[0].lower(), (p2[1].strip() if len(p2) > 1 else "")
        if mn in DATA_OPS or re.match(r'^[\w.]+\s+equ\b', s, re.I):
            continue
        cur.items.append(("ins", mn, ops, "%s:%d" % (rel[path], n)))
        prev_uncond = mn in ("jmp", "ret", "retf", "retn", "iret") and not ops.startswith("short .") \
            if mn != "jmp" else True
    return funcs, order


def norm_target(ops):
    o = re.sub(r'^(short|near|far|word|dword)\s+', '', ops.strip(), flags=re.I)
    o = re.sub(r'^(short|near|far|word|dword)\s+', '', o, flags=re.I)
    return o.strip()


def scan(f):
    d, bpd, maxd = 0, None, 0
    rec = {}
    last_bx = None
    uncond = False
    for it in f.items:
        if it[0] == "label":
            if it[1] in rec:
                d = rec[it[1]] if uncond else max(d, rec[it[1]])
            uncond = False
            continue
        _, mn, ops, where = it
        if uncond:
            uncond = False          # an unreferenced label-less resume: keep d
        o = ops.replace(" ", "")
        if mn in ("push", "pushf"):
            d += 2
        elif mn in ("pop", "popf"):
            d = max(0, d - 2)
        elif mn in ("sub", "add") and o.lower().startswith("sp,"):
            v = num(o[3:])
            if v is None:
                f.notes.append("%s: `%s sp, %s` is not a number - not counted" % (where, mn, o[3:]))
            else:
                d = max(0, d + v if mn == "sub" else d - v)
        elif mn == "mov" and o.lower() == "bp,sp":
            bpd = d
        elif mn == "mov" and o.lower() == "sp,bp":
            if bpd is not None:
                d = bpd
        elif mn == "leave":
            if bpd is not None:
                d = max(0, bpd - 2)
        elif (mn == "mov" and (o.lower().startswith("sp,") or o.lower().startswith("ss,"))) \
                or mn == "lss" or (mn == "xchg" and "sp" in o.lower().split(",")):
            if f.bank_d is None:
                f.bank_d = d
            f.notes.append("%s: SP is loaded (`%s %s`) - a stack switch the scan does "
                           "not follow" % (where, mn, ops))
        elif mn == "mov" and o.lower().startswith("bx,cc_ovm_"):
            last_bx = o[3:]
        elif mn == "call":
            t = norm_target(ops)
            if t == "[cc_ovvmm]" and last_bx:
                t, ops = "[%s]#mm" % last_bx, "far [%s]#mm" % last_bx   # module -> module
            last_bx = None
            far = bool(re.match(r'^far\b', ops.strip(), re.I))
            if t.startswith("OSAPI_") or t.startswith("KERNEL_SEG:") or t.startswith("KSEG:"):
                f.sites.append(("kernel", t, d, where))
            elif t.startswith("[") or far:
                f.sites.append(("far", t, d, where))
            elif re.match(r'^(ax|bx|cx|dx|si|di|bp)$', t, re.I):
                f.sites.append(("indirect", t, d, where))
            elif ":" in t:
                f.sites.append(("farimm", t, d, where))
            elif t.startswith("."):
                f.sites.append(("near", f.name + t, d, where))
            else:
                f.sites.append(("near", t, d, where))
        elif mn == "jmp":
            t = norm_target(ops)
            if t.startswith(".") or is_local(t):
                rec[t] = max(rec.get(t, 0), d)
            elif t.startswith("[") or re.match(r'^(ax|bx|cx|dx|si|di)$', t, re.I):
                pass                # a jump table: its targets are local labels
            elif t.startswith("OSAPI_"):
                f.sites.append(("kernel", t, d, where))
            else:
                f.sites.append(("tail", t, d, where))
            uncond = True
        elif mn.startswith("j") or mn in ("loop", "loope", "loopne", "loopz", "loopnz"):
            t = norm_target(ops)
            if t.startswith(".") or is_local(t):
                rec[t] = max(rec.get(t, 0), d)
            else:
                f.sites.append(("tail", t, d, where))
        elif mn in ("ret", "retf", "retn", "iret"):
            uncond = True
        elif mn == "int":
            f.sites.append(("int", ops, d, where))
        maxd = max(maxd, d)
    f.maxd = maxd


# ---------------------------------------------------------------------------
# the graph
# ---------------------------------------------------------------------------
class Graph:
    def __init__(self, build, segments=SEGMENTS, root=ROOT):
        self.build = build
        self.segments = segments
        self.funcs = {}             # (seg, name) -> Func
        self.missing = {}
        self.recursion = []
        self.bounded = []
        self.unknown = []
        for seg, top, door, disp in segments:
            src = Source(os.path.join(root, top), build, root=root)
            fs, _ = split_functions(seg, src)
            for name, f in fs.items():
                scan(f)
                self.funcs[(seg, name)] = f
            self.missing[seg] = [m for m in src.missing if m.endswith(".gen.asm")]
        self.occ = [(seg, door) for seg, top, door, disp in segments
                    if seg.startswith("PDX") and door]
        self.occnames = {seg for seg, _ in self.occ}
        self.memo = {}

    def callee(self, seg, f, kind, t):
        """Resolve a site to [(cost_extra, (seg, name))] - the frame the call adds."""
        if kind == "near":
            return [(2, (seg, t))]
        if kind == "tail":
            return [(0, (seg, t))]
        if kind == "far":
            key = (seg, f.name, t)
            if key in FAR_EDGES:
                e = list(FAR_EDGES[key])
                if (seg, f.name) == MCALL:      # PDV_X_INIT, PDV_KEY_*
                    e += self.occ
                return [(4, x) for x in e]
            if seg == "PDREND" and "slot" in t.lower() and self.occ:
                return [(4, x) for x in self.occ]
            if seg in self.occnames:            # an occupant reads a stream
                return [(4, ("PDENG", "pde_entry"))]
            m = re.match(r'^\[cc_ov[mv]_(\w+)\](#mm)?$', t)
            if m:
                return [(4 + OVL, (seg, "_" + m.group(1)))]
        return None

    def cost(self, key, mode, trail=(), nocycle=False):
        """The deepest bytes below `key`'s return address, and the path.

        mode "ui":   kernel calls cost KAPI; pdu_mcall's module call costs the
                     swap-back (SWAP) + a module kernel call (D23).
        mode "deep": kernel calls cost GATE (D23 3 swaps them to the UI stack).
        mode "flat": today's layout, no deep stack - everything on one stack.
        trail is ((key, bytes the call into it added), ...) from the root.
        """
        mk = (key, mode, nocycle)
        if mk in self.memo:
            return self.memo[mk]
        f = self.funcs.get(key)
        if f is None:
            return (0, [key[1] + "?"])
        keys = [k for k, _ in trail]
        if key in keys[:-1]:
            at = keys.index(key)
            members = tuple(sorted({k[1] for k in keys[at:]}))
            per = sum(e for _, e in trail[at + 1:])
            allow = None
            for (aseg, names), v in ALLOWED_RECURSION.items():
                if aseg == key[0] and set(members) <= set(names):
                    allow = v
            name = " -> ".join("%s:%s" % k for k in keys[at:])
            if allow is None:
                if name not in self.recursion:
                    self.recursion.append(name)
                return (0, ["(RECURSION)"])
            if name not in self.bounded:
                self.bounded.append(name)
            if nocycle:
                return (0, ["(bounded recursion)"])
            bound = allow[0]
            base = self.cost(key, mode, (), True)
            return (max(0, bound - 2) * per + base[0],
                    ["(x%d more round %s, %d B a trip)" % (bound - 1, "/".join(members), per)] + base[1])
        best, path = f.maxd, [f.name]
        for kind, t, d, where in f.sites:
            if kind == "kernel":
                if mode == "deep":
                    c, p = d + GATE, ["%s+%d" % (f.name, d), "%s(gate %d)" % (t, GATE)]
                else:
                    ka = 0 if t in ICELLS else KAPI     # an inline cell: retf only
                    c, p = d + 4 + ka, ["%s+%d" % (f.name, d + 4), "%s(%d)" % (t, ka)]
            elif kind in ("indirect", "farimm", "int"):
                self._unknown(f, kind, t, where)
                c, p = d + 4, [f.name, "%s %s (not followed)" % (kind, t)]
            else:
                tg = self.callee(key[0], f, kind, t)
                if tg is None:
                    self._unknown(f, kind, t, where)
                    continue
                c, p = -1, None
                for extra, tk in tg:
                    if (key == MCALL) and mode == "ui":
                        # D23 2/3: the module runs on the deep stack; what is
                        # charged HERE is the swap back for its kernel call
                        # at the depth pdu_mcall banked SS:SP (its first SS/SP
                        # load), or - before deepstk - at the far call itself
                        bd = f.bank_d if f.bank_d is not None else d
                        cc, pp = bd + SWAP + KAPI, ["%s+%d" % (f.name, bd + SWAP), "a module's kernel call (swapped back: %d + KAPI %d)" % (SWAP, KAPI)]
                    else:
                        sub = self.cost(tk, mode, trail + ((tk, d + extra),) if trail else ((key, 0), (tk, d + extra)), nocycle)
                        cc, pp = d + extra + sub[0], ["%s+%d" % (f.name, d + extra)] + sub[1]
                    if tk not in self.funcs and tk[1].startswith("_"):
                        pp = pp + ["(no body found)"]
                    if cc > c:
                        c, p = cc, pp
                    if t.startswith("[cc_ovm_") and not t.endswith("#mm") and mode != "deep":
                        # a LAZY overlay part is fetched (op_fetch, a disk
                        # read) at the call's own depth the first time a
                        # RESIDENT function calls into it (an overlay function
                        # calling another one is already resident: #mm)
                        cf = d + extra + 4 + KAPI
                        if cf > c:
                            c, p = cf, ["%s+%d" % (f.name, d + extra + 4), "fetch of the overlay for %s(%d)" % (tk[1], KAPI)]
            if c > best:
                best, path = c, p
        if not trail or not any(k == key for k, _ in trail[:-1]):
            self.memo[mk] = (best, path)
        return (best, path)

    def _unknown(self, f, kind, t, where):
        s = "%s %s: %s `%s` (%s)" % (f.seg, f.name, kind, t, where)
        if s not in self.unknown:
            self.unknown.append(s)

    def callers(self):
        seen = set()
        for (seg, name), f in self.funcs.items():
            for kind, t, d, w in f.sites:
                for _, tk in (self.callee(seg, f, kind, t) or []):
                    seen.add(tk)
        return seen


def fmt_path(p):
    return " > ".join(p)


def analyse(build, top=10, verbose=False, out=print, segments=SEGMENTS, root=ROOT):
    g = Graph(build, segments, root)
    fails = []
    for seg, miss in g.missing.items():
        if miss:
            out("SKIP %s: %s not built (make pagedesk)" % (seg, ", ".join(miss)))
    have = {seg for seg, _, _, _ in segments if not g.missing[seg]}
    # ---- DEEP: per door, per verb
    deep_max = DEEP_BUDGET
    out("DEEP STACK (D23: <= %d from the door's entry = %d - record %d - far return %d - canary %d "
        "- interrupt reserve %d; a kernel call costs the gate's %d here)"
        % (deep_max, DEEP_SIZE, DEEP_RECORD, DEEP_FARRET, DEEP_CANARY, DEEP_RESERVE, GATE))
    for seg, _, door, disp in segments:
        if not door or seg not in have:
            continue
        dk = (seg, door)
        tot, path = g.cost(dk, "deep")
        out("  %-6s door %-10s worst %5d  %s" % (seg, door, tot, fmt_path(path)))
        df = g.funcs.get(dk)
        disp_f = g.funcs.get((seg, disp))
        if df and disp_f:
            dsite = max((d for k, t, d, w in df.sites if k == "near" and t == disp), default=0)
            rows = {}
            for k, t, d, w in disp_f.sites:
                if k != "near":
                    continue
                c, p = g.cost((seg, t), "deep")
                v = dsite + 2 + d + 2 + c
                if v > rows.get(t, (0,))[0]:
                    rows[t] = (v, p)
            for t, (v, p) in sorted(rows.items(), key=lambda x: -x[1][0])[:top if not verbose else None]:
                flag = "  OVER" if v > deep_max else ""
                out("      %-24s %5d%s  %s" % (t, v, flag, fmt_path(p) if verbose or flag else ""))
        if tot > deep_max:
            fails.append("DEEP %s %s: %d > %d: %s" % (seg, door, tot, deep_max, fmt_path(path)))
    # ---- UI: every primary root (a function nothing calls) + DISPATCH
    if "PRIMARY" in have:
        called = g.callers()
        roots = [k for k in g.funcs if k[0] == "PRIMARY" and k not in called]
        # DISPATCH was measured down to the C callback's own return address
        # (os88_onwake's, under cc_onwake). The roots here are the crt0
        # trampolines themselves, so the kernel's share is DISPATCH less what
        # cc_onwake adds before that return address.
        tw = g.funcs.get(("PRIMARY", "cc_onwake"))
        tdep = max((d for k, t, d, w in tw.sites if t == "_os88_onwake"), default=14) if tw else 14
        kdisp = DISPATCH - (tdep + 2)
        rows = []
        for k in roots:
            c, p = g.cost(k, "ui")
            fl, fp = g.cost(k, "flat")
            rows.append((kdisp + c, k[1], p, kdisp + fl, fp))
        rows.sort(key=lambda r: -r[0])
        out("UI STACK (D22/D23: <= %d = the kernel's dispatch %d (%d measured to the C callback, less "
            "cc_onwake's %d) + the primary's chain + kernel API %d; a module's kernel call is charged at "
            "pdu_mcall's banked SP + %d)" % (UI_BUDGET, kdisp, DISPATCH, tdep + 2, KAPI, SWAP))
        for v, name, p, fl, fp in rows[:top if not verbose else None]:
            flag = "  OVER" if v > UI_BUDGET else ""
            out("  %-28s %5d%s  (no deep stack: %d)  %s" % (name, v, flag, fl, fmt_path(p)))
        for v, name, p, fl, fp in rows:
            if v > UI_BUDGET:
                fails.append("UI %s: %d > %d: %s" % (name, v, UI_BUDGET, fmt_path(p)))
        if rows:
            worst_flat = max(rows, key=lambda r: r[3])
            out("  today, with every verb on the UI stack (no D23): worst %d via %s: %s"
                % (worst_flat[3], worst_flat[1], fmt_path(worst_flat[4])))
    # ---- recursion, unknowns
    for r in g.recursion:
        fails.append("RECURSION %s (D21: no recursion - make it iterative)" % r)
    for (seg, names), (bound, owner, why) in ALLOWED_RECURSION.items():
        out("BOUNDED RECURSION, allowed until its owner fixes it (D21; charged x%d): %s - %s"
            % (bound, owner, why))
    if g.unknown:
        out("NOT FOLLOWED (%d call sites: charged the far frame only):" % len(g.unknown))
        for u in g.unknown[:None if verbose else 12]:
            out("  " + u)
        if not verbose and len(g.unknown) > 12:
            out("  ... %d more (-v)" % (len(g.unknown) - 12))
    notes = [n for f in g.funcs.values() for n in f.notes]
    if notes and verbose:
        out("NOTES:")
        for n in notes:
            out("  " + n)
    return fails, g


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build", default=os.path.join(ROOT, "build"))
    ap.add_argument("--top", type=int, default=8)
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    fails, _ = analyse(a.build, a.top, a.verbose)
    if fails:
        print("os88pdstk: %d FAILED" % len(fails))
        for f in fails:
            print("  FAIL " + f)
        return 1
    print("os88pdstk: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
