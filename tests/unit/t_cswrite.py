#!/usr/bin/env python3
"""Writes through CS are a RATCHET, and this is the gate
(docs/plans/KERN-MEGA-PLAN.md 2.4).

    python3 tests/unit/t_cswrite.py           the gate
    python3 tests/unit/t_cswrite.py --list    every site, by file and SECTION
    python3 tests/unit/t_cswrite.py --census DIR ...
                                              writes and pairs per file of any
                                              tree, gated or not (the worklist
                                              for a tree not yet in TREES)

In real mode a code segment is ordinary memory, so `mov [cs:x], ax` and
`push cs / pop ds` are free ways to say "my own data".  In 286 PROTECTED MODE
neither survives: a code segment is never writable, so the first is a #GP, and
the second loads DS with a read-only selector, so the first store through it is
one.  kern_mega shares most of kernel/ with kern_big (KERN-MEGA-PLAN 7), and a
new site added to a shared file assembles and runs on kern_big while it breaks
the other build - silently, on a build nobody here can boot yet.

So this counts the two idioms per file under kernel/ and drivers/ - and
apps/pixel/, PiXEL's package and parts, which KERN-MEGA-PLAN 14.40 took to
none, apps/pixelstein/, whose play 14.50 took to none, and what 14.150
converted: apps/xtmp3/ (the XT backend's arm, the engine), apps/notepad/ and
the speaker libraries every player includes (apps/os88spk.inc,
apps/os88spkfx.inc) - and holds them to `tests/cswrites.txt`, textrules'
shape:

  * a FILE NOT IN THE REGISTRY using either idiom fails - the case that
    matters, because it is what new code hits;
  * a file EXCEEDING its registered count fails;
  * a registered count now too HIGH fails too, so a conversion has to lower
    the number and the diff says the work happened.

WHAT COUNTS.  A write: the destination of mov, add, sub, adc, sbb, inc, dec,
or, and, xor, not, neg, the shifts and rotates, and pop, when that destination
is `[cs:...]` - or `[byte cs:...]`, NASM's spelling for the 8-bit
displacement, which the first version of this pattern missed and kern_mega's
first package launch found; and xchg with such an operand on either side.  A pair:
`push cs` whose next code line is `pop ds` or `pop es`.  `push cs` on its own
is a simulated far call (BLOBCALL, COLDCALL) and is legal in protected mode;
`mov reg, cs` or `mov reg, KERNEL_SEG` followed within five lines by `mov
ds|es, reg` (the register not reloaded in between) counts as a pair too;
READS through CS are legal too, a code segment being readable, so neither is
counted.  Inside a `%macro` a site counts ONCE, where it is written, which is
where it gets converted.

`--list` names the SECTION of every site, because that is what decides the
conversion: `.boot2` runs in real mode before kern_mega would switch, a
`.mod*` section is an on-demand module writing its own image, and `.text` or
`.cold` is resident code that kern_mega runs in protected mode.  The section
is the last `section` line above the site, so a section switched inside an
`%if` reads as whichever arm is written last - fine for a worklist, which is
all `--list` is.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
REGISTRY = os.path.join(ROOT, "tests", "cswrites.txt")
TREES = ("kernel", "drivers", "apps/pixel", "apps/pixelstein", "apps/xtmp3",
         "apps/notepad")
FILES = ("apps/os88spk.inc", "apps/os88spkfx.inc")   # (shared, in apps/ itself)

LABEL = r"(?:[A-Za-z_.$@?][\w.$@?#~]*:\s*)?"
WRITE = re.compile(
    r"^\s*" + LABEL +
    r"(?:mov|add|sub|adc|sbb|inc|dec|or|and|xor|not|neg|"
    r"shl|shr|sal|sar|rol|ror|rcl|rcr|pop)\s+"
    r"(?:(?:byte|word|dword)\s+)?\[\s*(?:(?:byte|word|dword)\s+)?cs\s*:",
    re.I)
XCHG = re.compile(r"^\s*" + LABEL +
                  r"xchg\s+.*\[\s*(?:(?:byte|word|dword)\s+)?cs\s*:", re.I)
# ...and the conversion's own spelling, `[CSEG x]` (kernel/kernel.asm's CSDO),
# which is `cs:` on every build but kern_mega: on any line that is not a CSDO
# it is a write through CS again, and counts as one
CSEGW = re.compile(r"\[\s*CSEG\b", re.I)
CSDOL = re.compile(r"^\s*" + LABEL + r"CSDOF?\b", re.I)
# CSDO_ON ... CSDO_OFF is CSDO for a RUN (kernel/kernel.asm): every line
# inside it runs with DS = the image on kern_mega, so a `[CSEG x]` there is
# converted as a CSDO line is - and the run is held to the macro's own rules
# here, because breaking one assembles cleanly on every build and is wrong
# only on kern_mega. A run is ENTERED ONLY AT ITS CSDO_ON AND LEFT ONLY AT ITS
# CSDO_OFF: a label inside it is named by nothing outside it, and a jump
# inside it goes to a label inside it (a loop is fine; `.x` labels are taken
# as local to the last plain label, which is how nasm scopes them); no call,
# return, int, string instruction, macro or nested CSDO; no push or pop that
# does not balance inside the run (its `pop ds` would take the wrong word);
# DS itself neither read nor written; and every memory operand names its
# segment or a BP base - [CSEG x], [cs:x], [es:x], [ss:x], [bp...] - because
# a plain `[x]` inside a run would read the IMAGE, not the kernel
RUNON = re.compile(r"^\s*CSDO_ON\s*$", re.I)
RUNOFF = re.compile(r"^\s*CSDO_OFF\s*$", re.I)
RUNLABEL = re.compile(r"^\s*([A-Za-z_.$@?][\w.$@?#~]*):", re.I)
RUNJUMP = re.compile(r"^\s*(?:[A-Za-z_.$@?][\w.$@?#~]*:\s*)?"
                     r"(j\w+|loop\w*)\s+(?:short\s+|near\s+)?([\w.$@?]+)\s*$",
                     re.I)
RUNFLOW = re.compile(
    r"^\s*(?:[A-Za-z_.$@?][\w.$@?#~]*:\s*)?"
    r"(?:j\w+|call|ret\w*|iret|int\w*|loop\w*|lds|les|"
    r"(?:rep\w*\s+)?(?:movs|lods|stos|cmps|scas|ins|outs)[bw]?|xlatb?|"
    r"CSDOF?|CSDO_SP|CSDO_ON|PUSH_CSDATA|[A-Z_][A-Z0-9_]+)\b")
RUNPUSH = re.compile(r"^\s*(?:[A-Za-z_.$@?][\w.$@?#~]*:\s*)?(push|pop)\w*\b",
                     re.I)
RUNDS = re.compile(r"\bds\b", re.I)
RUNMEM = re.compile(r"\[([^\]]*)\]")
RUNSEG = re.compile(r"^\s*(?:(?:byte|word|dword)\s+)?(?:CSEG\b|[ces]s\s*:)"
                    r"|^\s*(?:(?:byte|word|dword)\s+)?bp\b", re.I)
REFNAME = re.compile(r"(?<![\w.$@?])(\.?[A-Za-z_$@?][\w.$@?#~]*)")


def runcheck(code_lines):
    """[(line, what)] for every breach of a CSDO_ON run's rules."""
    out, start, depth = [], None, 0
    scope = ""
    inside = {}                         # qualified label -> its run's start
    refs = []                           # (line, qualified name, run start)
    for i, l, _where in code_lines:
        if not l.strip() or l.lstrip().startswith("%"):
            continue                    # preprocessor lines are not code
        m = RUNLABEL.match(l)
        if m and not m.group(1).startswith("."):
            scope = m.group(1)
        qual = lambda n: scope + n if n.startswith(".") else n
        if RUNON.match(l):
            if start is not None:
                out.append((i + 1, "CSDO_ON inside a run"))
            start, depth = i + 1, 0
            continue
        if RUNOFF.match(l):
            if start is None:
                out.append((i + 1, "CSDO_OFF with no CSDO_ON"))
            elif depth:
                out.append((i + 1, "the run's pushes and pops do not balance"))
            start = None
            continue
        body = RUNLABEL.sub("", l, count=1) if m else l
        for n in REFNAME.findall(body.split("[")[0] if "[" not in body
                                 else body):
            refs.append((i + 1, qual(n), start))
        if start is None:
            continue
        if m:
            inside[qual(m.group(1))] = start
        j = RUNJUMP.match(l)
        if j:
            refs.append((i + 1, "JUMP " + qual(j.group(2)), start))
        elif RUNFLOW.match(l):
            out.append((i + 1, "a call, return, int, string instruction or "
                               "macro inside a run"))
        p = RUNPUSH.match(l)
        if p:
            depth += 1 if p.group(1).lower() == "push" else -1
            if depth < 0:
                out.append((i + 1, "a pop inside a run of what it never "
                                   "pushed"))
        if RUNDS.search(RUNMEM.sub("", body)):
            out.append((i + 1, "DS named inside a run"))
        for mm in RUNMEM.finditer(body):
            if not RUNSEG.match(mm.group(1)):
                out.append((i + 1, "`[%s]` inside a run names no segment"
                            % mm.group(1).strip()))
    if start is not None:
        out.append((start, "a run with no CSDO_OFF"))
    for line, name, run in refs:
        if name.startswith("JUMP "):
            if inside.get(name[5:]) != run:
                out.append((line, "a jump inside a run to %s, outside it"
                            % name[5:]))
        elif name in inside and inside[name] != run:
            out.append((line, "%s is inside a run and named outside it"
                        % name))
    return out
PUSHCS = re.compile(r"^\s*" + LABEL + r"push\s+cs\s*$", re.I)
POPSEG = re.compile(r"^\s*pop\s+(?:ds|es)\s*$", re.I)
# ...and the two other spellings of "DS (ES) = the code segment": through a
# register, from CS or from the KERNEL_SEG constant - which in kern_mega is
# the kernel's CODE selector as well (kernel/mega.inc), so a store through
# the register loaded from it faults exactly as a `push cs / pop ds` does
VIAREG = re.compile(r"^\s*" + r"(?:[A-Za-z_.$@?][\w.$@?#~]*:\s*)?" +
                    r"mov\s+(\w\w)\s*,\s*(?:cs|KERNEL_SEG)\s*$", re.I)
SECTION = re.compile(r"^\s*section\s+(\.\w+)", re.I)
MACRO = re.compile(r"^\s*%(?:i)?macro\b", re.I)
ENDMACRO = re.compile(r"^\s*%endmacro\b", re.I)


def code(line):
    """The line with its comment taken off.  A `;` inside a string literal
    would cut early, which only ever loses a site from a data line - and no
    data line is a write."""
    return line.split(";", 1)[0].rstrip()


IFDEF = re.compile(r"^\s*%(ifdef|ifndef|elifdef|elifndef)\s+(\w+)", re.I)
IFANY = re.compile(r"^\s*%if", re.I)
ELSE = re.compile(r"^\s*%else\b", re.I)
ELIF = re.compile(r"^\s*%elif", re.I)
ENDIF = re.compile(r"^\s*%endif\b", re.I)


def walk(path):
    """(lines, [(i, line, where)]) for every code line of one source that
    kern_mega ASSEMBLES, `where` being its section or "%macro".

    A line on an arm that kern_mega never assembles - inside `%ifndef
    KERN_MEGA`, or the `%else` of `%ifdef KERN_MEGA` - is left out: it is
    real mode's own spelling of something kern_mega does another way, which is
    exactly what a conversion leaves behind. tests/unit/t_seg0.py walks the
    same way. A PACKAGE's `%ifndef PKG_MEGA` arm is the same thing one
    level out (KERN-MEGA-PLAN 14.66, apps/os88mega.inc): the SHIPPED build,
    which kern_mega's own disks replace with the PKG_MEGA arm."""
    with open(path, errors="replace") as f:
        lines = [code(x) for x in f.read().split("\n")]
    out = []
    section, inmacro = ".text", False
    stack = []                      # per %if: True = this arm excludes mega
    for i, l in enumerate(lines):
        m = IFDEF.match(l)
        if m and m.group(1).lower() in ("ifdef", "ifndef"):
            neg = m.group(1).lower() == "ifndef"
            stack.append(["mega" if m.group(2) in ("KERN_MEGA", "PKG_MEGA")
                          else None, neg])
            continue
        if IFANY.match(l) and not m:
            stack.append([None, False])
            continue
        if ELIF.match(l) and stack:
            stack[-1] = [None, False]
            continue
        if ELSE.match(l) and stack:
            stack[-1][1] = not stack[-1][1]
            continue
        if ENDIF.match(l) and stack:
            stack.pop()
            continue
        realonly = any(f[0] == "mega" and f[1] for f in stack)
        m = SECTION.match(l)
        if m:
            section = m.group(1)
        if MACRO.match(l):
            inmacro = True
        elif ENDMACRO.match(l):
            inmacro = False
        where = "%macro" if inmacro else section
        if not realonly:
            out.append((i, l, where))
    return lines, out


def scan(path):
    """[(line, kind, section)] for every write and pair in one source, on the
    arms kern_mega assembles (walk)."""
    lines, code_lines = walk(path)
    out = []
    inrun = False
    for i, l, where in code_lines:
        if RUNON.match(l):
            inrun = True
        elif RUNOFF.match(l):
            inrun = False
        if WRITE.match(l) or XCHG.match(l) or \
                (CSEGW.search(l) and not CSDOL.match(l) and not inrun):
            out.append((i + 1, "write", where))
        elif PUSHCS.match(l):
            j = i + 1
            while j < len(lines) and not lines[j].strip():
                j += 1
            if j < len(lines) and POPSEG.match(lines[j]):
                out.append((i + 1, "pair", where))
        else:
            m2 = VIAREG.match(l)
            if m2:
                r = m2.group(1)
                for j in range(i + 1, min(i + 6, len(lines))):
                    if re.match(r"^\s*mov\s+(?:ds|es)\s*,\s*%s\s*$" % r,
                                lines[j], re.I):
                        out.append((i + 1, "pair", where))
                        break
                    if re.match(r"^\s*(?:mov|xchg|pop|lea|xor|sub|add)\s+%s\b"
                                % r, lines[j], re.I):
                        break           # the register was reloaded first
    return out


def sources(trees=TREES, files=FILES):
    for sub in trees:
        for base, _dirs, names in os.walk(os.path.join(ROOT, sub)):
            for f in sorted(names):
                if f.endswith((".asm", ".inc")):
                    yield os.path.relpath(os.path.join(base, f),
                                          ROOT).replace("\\", "/")
    for f in files:
        yield f


def registry():
    """{path: (writes, pairs, lineno)}, refusing a line it cannot read."""
    reg = {}
    with open(REGISTRY) as f:
        for n, raw in enumerate(f, 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            ok = (len(parts) == 3 and parts[0].isdigit()
                  and parts[1].isdigit() and "#" in raw)
            check(ok, "tests/cswrites.txt:%d reads `<writes> <pairs> <path>"
                      "  # <reason>`" % n)
            if ok:
                check(parts[2] not in reg,
                      "tests/cswrites.txt:%d registers %s twice" % (n, parts[2]))
                reg[parts[2]] = (int(parts[0]), int(parts[1]), n)
    return reg


def main():
    found = {}
    for p in sources():
        sites = scan(os.path.join(ROOT, p))
        if sites:
            found[p] = sites

    if "--census" in sys.argv:
        trees = sys.argv[sys.argv.index("--census") + 1:] or ["apps"]
        tw = tq = n = 0
        for p in sources(trees, ()):
            sites = scan(os.path.join(ROOT, p))
            if sites:
                w = sum(1 for s in sites if s[1] == "write")
                q = len(sites) - w
                tw, tq, n = tw + w, tq + q, n + 1
                print("%4d %3d  %s" % (w, q, p))
        print("%d writes and %d pairs in %d files" % (tw, tq, n))
        return

    if "--list" in sys.argv:
        for p in sorted(found):
            for line, kind, where in found[p]:
                print("%s:%d  %-5s  %s" % (p, line, kind, where))
        return

    runs = 0
    for p in sources():
        _lines, code_lines = walk(os.path.join(ROOT, p))
        for line, what in runcheck(code_lines):
            check(False, "%s:%d: %s - CSDO_ON's rules (kernel/kernel.asm), "
                         "which only kern_mega would show" % (p, line, what))
        runs += sum(1 for _i, l, _w in code_lines if RUNON.match(l))

    reg = registry()
    for p, sites in sorted(found.items()):
        w = sum(1 for s in sites if s[1] == "write")
        q = sum(1 for s in sites if s[1] == "pair")
        if p not in reg:
            first = sites[0]
            check(False, "%s:%d writes through CS or loads DS/ES from CS "
                         "(%s) and is not in tests/cswrites.txt - kern_mega "
                         "runs this file in protected mode, where code is "
                         "read-only (docs/plans/KERN-MEGA-PLAN.md 2.4). Reach "
                         "the data through DS or ES, or register the file with "
                         "a reason" % (p, first[0], first[1]))
            continue
        rw, rq, n = reg[p]
        check(w <= rw and q <= rq,
              "%s has %d CS writes and %d push cs/pop pairs against the %d and "
              "%d tests/cswrites.txt:%d allows - a new site in a file kern_mega "
              "shares (docs/plans/KERN-MEGA-PLAN.md 2.4)" % (p, w, q, rw, rq, n))
        check(w >= rw and q >= rq,
              "%s has %d CS writes and %d push cs/pop pairs, below the %d and "
              "%d tests/cswrites.txt:%d registers - lower the line, so the "
              "ratchet keeps what the conversion won" % (p, w, q, rw, rq, n))
    for p, (_w, _q, n) in sorted(reg.items()):
        check(p in found, "tests/cswrites.txt:%d registers %s, which has no "
                          "site left (or is gone) - delete the line" % (n, p))
    total_w = sum(r[0] for r in reg.values())
    total_q = sum(r[1] for r in reg.values())
    done("cswrite: %d writes through CS and %d push cs/pop pairs in %d files, "
         "none new; %d CSDO_ON runs within their rules"
         % (total_w, total_q, len(reg), runs))


if __name__ == "__main__":
    main()
