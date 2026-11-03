#!/usr/bin/env python3
"""The windowed DOS layer's segment boundary, mechanically (SPEC.md 96.54.9).

    os88dvwexp.py exp   HOST.map OUT.inc     # the host's symbols, for the part
    os88dvwexp.py check HOST.map             # refuse what the macros miss
    os88dvwexp.py check HOST.map --src DIR   # ...over another tree's sources

DOS's windowed layer (apps/dos/dosw.inc, doswm.inc) is assembled as a PART of
DOS.O88 - `apps/dos/doswp.asm`, org 0, in a claim of its own - while the
console, the core and the doors it calls stay in the host. Every reference
across that line is written ONCE against the macros of apps/dos/doswabi.inc,
chosen by who OWNS the symbol, and two things make that a translation rather
than an audit:

  * the IDENTITY GATE: with the macros' inline arms the host assembles byte
    for byte what it did before the macros (build/dos.bin still does), so the
    rewrite changed nothing but spelling;
  * THIS CHECKER, which runs before the part is assembled and refuses every
    form the macros do not cover, so a site the rewrite missed fails the BUILD
    instead of a run.

`exp` turns the HOST's nasm map into `%define` lines for exactly the names
the part's sources use and do not define themselves - the second pass of the
two-pass build. `%define` rather than `equ` because os88api.inc, which the
part includes too, defines some of the same constants: a define issued after
it shadows them with the same value instead of failing as a redefinition.

OWNERSHIP, which every rule below is phrased in:
  * a PART name is a label in a region the part assembles (the source's
    `%ifndef DVW_SPLIT` blocks), or a cell of apps/dos/doswcell.inc;
  * a HOST address is any symbol the host's map places in a SECTION that the
    part does not define, or an absolute one NAMED like a cell (lower case:
    the core's cells are equ's at fixed offsets, `dos_dstk`); an absolute
    UPPER-case one (DHK_SNAP, CON_COLS) is a number.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRCS = ("apps/dos/dosw.inc", "apps/dos/doswm.inc", "apps/dos/doswp.asm")
CELLS = "apps/dos/doswcell.inc"
ABI = "apps/dos/doswabi.inc"

REGS = set("ax bx cx dx si di bp sp al ah bl bh cl ch dl dh cs ds es ss".split())
KEYW = set("byte word dword short near far strict rel abs seg wrt".split())
STR = re.compile(r"^(?:(?:rep|repe|repz|repne|repnz|cs|es|ss|ds|PSRC)\s+)*"
                 r"(movs[bw]?|lods[bw]?|stos[bw]?|scas[bw]?|cmps[bw]?|xlatb?)\b")
BRANCH = re.compile(r"^(call|jmp|j[a-z]{1,4}|loop[a-z]*|jcxz)\b")
ID = re.compile(r"[A-Za-z_][\w.]*")
OK = "DVWCHK-OK"


def read_map(path):
    """{name: (value, is_address)} out of a nasm `[map all|symbols]` file"""
    out, nosect = {}, False
    for line in open(path):
        if line.startswith("---- "):
            nosect = line.startswith("---- No Section")
            continue
        p = line.split()
        if len(p) == 3:
            try:
                out[p[2]] = (int(p[0], 16), True)
            except ValueError:
                pass
        elif nosect and len(p) == 2:
            try:
                out.setdefault(p[1], (int(p[0], 16), False))
            except ValueError:
                pass
    return out


def code_of(line):
    """the instruction half of a line: up to a `;` outside quotes"""
    q = None
    for i, c in enumerate(line):
        if q:
            if c == q:
                q = None
        elif c in "'\"`":
            q = c
        elif c == ";":
            return line[:i], line[i:]
    return line, ""


def part_lines(src, mode="part"):
    """(lineno, code, comment) of every line the PART assembles. Conditions on
    DVW (true), DVW_PART (true) and DVW_SPLIT (false) are evaluated; any other
    condition includes both its arms, which can only make the check stricter.
    %macro bodies are skipped: their lines are checked where they expand."""
    known = {"DVW": True, "DVW_PART": mode == "part", "DVW_SPLIT": mode != "part"}
    stack, inmac = [], False
    for n, line in enumerate(open(src), 1):
        code, com = code_of(line)
        s = code.strip()
        w = s.split()
        d = w[0].lower() if w else ""
        if d == "%macro":
            inmac = True
            continue
        if d == "%endmacro":
            inmac = False
            continue
        if d in ("%ifdef", "%ifndef") and len(w) > 1 and w[1] in known:
            v = known[w[1]]
            stack.append(v if d == "%ifdef" else not v)
            continue
        if d.startswith("%if"):
            stack.append(None)
            continue
        if d == "%else":
            if stack and stack[-1] is not None:
                stack[-1] = not stack[-1]
            continue
        if d == "%endif":
            stack.pop()
            continue
        if inmac or any(x is False for x in stack) or not s or s.startswith("%"):
            continue
        yield n, code, com


def names(srcs, mode="part"):
    """(labels, constants) the part's own sources define"""
    labels, consts = set(), set()
    for src in srcs:
        for _n, code, _c in part_lines(os.path.join(ROOT, src), mode):
            m = re.match(r"\s*([A-Za-z_]\w*)\s*:", code)
            if m:
                labels.add(m.group(1))
            m = re.match(r"\s*([A-Za-z_]\w*)\s+equ\b", code)
            if m:
                consts.add(m.group(1))
    return labels, consts


def part_cells():
    """the cells apps/dos/doswcell.inc lays out in the part"""
    out = set()
    for line in open(os.path.join(ROOT, CELLS)):
        m = re.match(r"([A-Za-z_]\w*)\s+(?:d[bwd]|times)\b", code_of(line)[0])
        if m:
            out.add(m.group(1))
    return out


def hostcalls():
    """DVW_HOSTCALLS' list: the host routines the part may HCALL/HJMP"""
    return set(re.findall(r"^\s*DVW_HX\s+(\w+)", open(os.path.join(ROOT, ABI)).read(),
                          re.M))


def operands(code):
    """the text after the mnemonic, label and prefixes stripped"""
    code = re.sub(r"^\s*[A-Za-z_.][\w.]*\s*:", "", code).strip()
    return code


def check(mappath, root=None):
    global ROOT
    if root:
        ROOT = root
    hmap = read_map(mappath)
    labels, consts = names(SRCS)
    cells = part_cells()
    part = labels | cells
    pconst = consts - part
    # an absolute symbol is an ADDRESS when it is named like a cell: the core's
    # bss lives at fixed offsets above its code (DOS_CBASE, every host alike),
    # so `dos_dstk` is an equ under "No Section" and is as much a host cell as
    # `con_cx` - while DHK_SNAP, an offset INTO a table, is a number
    host = {k for k, (v, a) in hmap.items() if (a or k[:1].islower())
            and k not in part and k not in pconst and "." not in k}
    calls = hostcalls()
    errs = []

    def err(src, n, why, code):
        errs.append("%s:%d: %s\n    %s" % (src, n, why, code.strip()))

    for src in SRCS:
        for n, code, com in part_lines(os.path.join(ROOT, src)):
            if OK in com:
                continue
            body = operands(code)
            if not body:
                continue
            w = body.split(None, 1)
            mnem, rest = w[0], (w[1] if len(w) > 1 else "")
            ml = mnem.lower()
            # --- the macros: their own arguments only ------------------------
            if mnem in ("HCALL", "HJMP"):
                if rest.strip() not in calls:
                    err(src, n, "%s of %r, which DVW_HOSTCALLS does not list"
                        % (mnem, rest.strip()), code)
                continue
            if mnem == "HICALL":
                if rest.strip() not in host:
                    err(src, n, "HICALL through %r, which is not a host cell"
                        % rest.strip(), code)
                continue
            if mnem in ("HOSTDS", "HOSTES", "PARTDS", "PARTES", "DVW_HBIND",
                        "DVW_POSTRET", "DVW_CLRDK", "DVW_PCOPY", "DVW_PCEND",
                        "DVW_PESDS", "DVW_PESEND", "DVW_LOWSTK"):
                continue
            if mnem == "PARTCS":
                continue
            if mnem == "PCALL":
                err(src, n, "PCALL is the HOST's way into the part, not the "
                    "part's own", code)
                continue
            if ml in ("times", "align", "db", "dw", "dd", "equ") or \
                    re.match(r"\s*\w+\s+equ\b", body):
                if ml in ("dw", "dd"):
                    for t in ID.findall(re.sub(r"(PADDR|HENT)\(\s*\w+\s*\)", "", rest)):
                        if t in part:
                            err(src, n, "a part address %r as data: PADDR() or "
                                "HENT() says which segment it means" % t, code)
                        elif t in host:
                            err(src, n, "a HOST address %r in the part's data" % t,
                                code)
                continue
            # --- string instructions: every one is a decision ----------------
            if STR.match(body.strip()):
                if not body.strip().startswith("PSRC") and \
                        not re.match(r"cs\s+lods", body.strip()):
                    err(src, n, "a string instruction: say which segments it "
                        "means (PSRC, or `; %s:` and why)" % OK, code)
                continue
            # --- segment values ----------------------------------------------
            if re.search(r"\bpush\s+cs\b", body) or \
                    re.search(r",\s*cs\s*$", body.strip()):
                err(src, n, "CS as a value: HOSTDS/HOSTES (or DS where DS is the "
                    "host) if it means the host, PARTDS/PARTES/PARTCS if the part", code)
                continue
            # --- branches -----------------------------------------------------
            if BRANCH.match(ml) or ml in ("call", "jmp"):
                tgt = rest.strip()
                tgt2 = re.sub(r"^(strict\s+)?(short|near|far)\s+", "", tgt)
                if "[" in tgt:
                    if not re.search(r"\bfar\b", tgt):
                        err(src, n, "an indirect NEAR branch through memory: "
                            "HICALL, or a table the part owns and `; %s:`" % OK,
                            code)
                        continue
                elif tgt2.split(":")[0] in REGS:
                    err(src, n, "a branch through a register: whose code does it "
                        "reach? (`; %s:` and why)" % OK, code)
                    continue
                elif tgt2 in host:
                    err(src, n, "a NEAR %s into the host's %r: HCALL/HJMP" %
                        (ml, tgt2), code)
                    continue
                elif tgt2.startswith("OSAPI_") or tgt2.startswith(".") or \
                        tgt2 in part:
                    continue
            # --- memory operands ----------------------------------------------
            stripped = rest
            for mm in re.finditer(r"\[([^\]]*)\]", rest):
                inner = mm.group(1)
                toks = ID.findall(inner)
                seg = re.match(r"\s*(cs|ds|es|ss)\s*:", inner)
                ps = "PS" in toks
                for t in toks:
                    if t == "PS" or t in REGS:
                        continue
                    if t in part:
                        if not ((seg and seg.group(1) == "cs") or ps):
                            err(src, n, "the part's %r with no segment: DS is the "
                                "host's in the part - [PS %s]" % (t, t), code)
                    elif t in host:
                        if ps:
                            err(src, n, "PS on the HOST's %r" % t, code)
                        elif seg and seg.group(1) == "cs":
                            err(src, n, "the host's %r through CS: DS is the host's in "
                                "the part (HOSTDS)" % t, code)
                        elif seg:
                            err(src, n, "the host's %r through %s: (`; %s:` and "
                                "why)" % (t, seg.group(1), OK), code)
                stripped = stripped.replace(mm.group(0), " ")
            # --- what is left is immediates ----------------------------------
            stripped = re.sub(r"(PADDR|HENT)\(\s*\w+\s*\)", " ", stripped)
            for t in ID.findall(stripped):
                if t in part:
                    err(src, n, "the part's %r as a number: PADDR(%s) if it is an "
                        "offset in the part, HENT(%s) if the host or the kernel "
                        "enters it" % (t, t, t), code)
                if ml == "lea" and t in host:
                    err(src, n, "lea of a host cell", code)
    return errs


def exp(mappath, outpath):
    hmap = read_map(mappath)
    labels, consts = names(SRCS)
    part = labels | consts | part_cells()
    used = set()
    for src in SRCS:
        p = os.path.join(ROOT, src)
        if not os.path.exists(p):
            continue
        for line in open(p):
            used.update(ID.findall(code_of(line)[0]))
    # the host's thunks (`dvwh_`) and entry stubs (`hent_`): the part names
    # them through macros, so their names never appear in its source
    used |= {k for k in hmap if k.startswith(("dvwh_", "hent_"))}
    lines = ["; GENERATED by tools/os88dvwexp.py from %s - do not edit." %
             os.path.basename(mappath),
             "; The HOST's symbols the windowed part names (SPEC.md 96.54.9)."]
    # ...nor anything the part's own includes define after this file
    for line in open(os.path.join(ROOT, ABI)):
        m = re.match(r"\s*([A-Za-z_]\w*)\s+equ\b", code_of(line)[0])
        if m:
            part.add(m.group(1))
    n = 0
    for k in sorted(used):
        if k in part or "." in k or k not in hmap:
            continue
        lines.append("%%define %s 0x%X" % (k, hmap[k][0]))
        n += 1
    open(outpath, "w").write("\n".join(lines) + "\n")
    return n


def main(argv):
    if len(argv) >= 3 and argv[1] == "exp":
        n = exp(argv[2], argv[3])
        print("os88dvwexp: %d host symbols for the windowed part" % n)
        return 0
    if len(argv) >= 3 and argv[1] == "check":
        root = None
        if "--src" in argv:
            root = argv[argv.index("--src") + 1]
        errs = check(argv[2], root)
        for e in errs:
            print(e)
        if errs:
            print("os88dvwexp: %d site(s) the boundary's macros do not cover "
                  "(apps/dos/doswabi.inc)" % len(errs))
            return 1
        print("os88dvwexp: the windowed part's sources are clean")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
