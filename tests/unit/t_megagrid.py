#!/usr/bin/env python3
"""A segment an OS88_HIGH package BUILDS stays on the 8-paragraph grid, and
this is the gate (docs/plans/KERN-MEGA-PLAN.md 10.3 items 59 and 63, 14.39).

    python3 tests/unit/t_megagrid.py           the gate
    python3 tests/unit/t_megagrid.py --list    every site, by file and kind

In real mode any paragraph is a segment, so a far pointer is normalised with
`and si, 15` and the rest folded into the segment, and a walk steps the
segment by one paragraph. Under kern_mega's protected mode only a MULTIPLE OF
EIGHT paragraphs is an identity selector (KERN-MEGA-PLAN 3.1): a value with
RPL bits set faults, and one with bit 2 set loads an LDT code ALIAS of a
different address - readable, so reads come from the wrong memory without a
fault, and a write is either reported (14.39's guard) or, inside another
claim, completed there. Paint's staged BMP rows, PiXEL's work buffers and the
Video Player's record walk all shipped like that. The SDK rule is: normalise
to EIGHT paragraphs - `and si, 127` with the offset's >> 7 << 3 folded in -
never one. Nothing in an emulator flags the real-mode spelling until the
segment it builds happens to be off the grid, so the static count is the
guard, t_seg0.py's shape.

SCOPE: the packages that DECLARE OS88_HIGH (an OS88_HEADER or a C package's
CC_STACK_CLASS naming it), every .asm/.inc under their apps/<dir>/ except a
hosttest/; a package that is never loaded above 1MB is still loaded at an
identity segment, but saying OS88_HIGH is the declaration that it does no
arithmetic on its own segments (apps/os88api.inc), so that is where the rule
is enforced. Three shapes, each ending in a `mov ds|es` within a few lines:

  norm    `and R16, 15` beside a 16-bit right shift by four - a far pointer
          normalised to ONE paragraph
  round   `add R16, 15` and a shift by four - a byte count rounded up to
          paragraphs and added to a segment (a tail placed past a buffer) -
          unless an `and al, 0xF8` before the load puts it back on the grid
  step    `inc R` / `dec R`, or `add|sub R, n` with n not a multiple of 8,
          as the last write to R before `mov ds|es, R`

Held to `tests/megagrid.txt` per file, as t_seg0.py holds tests/seg0.txt: a
file not registered may have no site, a registered one may not exceed its
count, and a count now too HIGH fails as well, so a conversion lowers the
line. A site that is RIGHT - a real-mode-only arm behind a CPU_F_PM test, a
segment that is never a selector - is registered with that reason.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(
    __file__)), "..", ".."))
REGISTRY = os.path.join(ROOT, "tests", "megagrid.txt")

LBL = r"(?:[A-Za-z_.$@?][\w.$@?#~]*:\s*)?"
R16 = r"(?:ax|bx|cx|dx|si|di|bp)"
FIFTEEN = r"(?:15|0*fh|0x0*f)"
DECLARES = re.compile(r"^\s*(?:OS88_HEADER\b|%define\s+CC_STACK_CLASS\b)"
                      r"[^;\n]*\bOS88_HIGH\b", re.M)
AND15 = re.compile(r"^\s*" + LBL + r"and\s+(" + R16 + r")\s*,\s*" + FIFTEEN +
                   r"\s*$", re.I)
ADD15 = re.compile(r"^\s*" + LBL + r"add\s+(" + R16 + r")\s*,\s*" + FIFTEEN +
                   r"\s*$", re.I)
SHRCL = re.compile(r"^\s*" + LBL + r"(?:shr|sar|rcr)\s+(" + R16 +
                   r")\s*,\s*(cl|1|4)\s*$", re.I)
CL4 = re.compile(r"^\s*" + LBL + r"mov\s+(?:cl|cx)\s*,\s*4\s*$", re.I)
SEGLOAD = re.compile(r"^\s*" + LBL + r"mov\s+(?:ds|es)\s*,\s*(\S+?)\s*$",
                     re.I)
STEP = re.compile(r"^\s*" + LBL + r"(inc|dec)\s+(\w\w)\s*$", re.I)
ADDI = re.compile(r"^\s*" + LBL + r"(?:add|sub)\s+(\w\w)\s*,\s*"
                  r"([0-9][0-9a-fA-Fx]*[hH]?)\s*$")
WRITER = re.compile(r"^\s*" + LBL + r"([a-z]+)\s+(\w\w)\b", re.I)
FLOW = re.compile(r"^\s*" + LBL + r"(?:call|ret|retf|iret|jmp)\b", re.I)


def code(line):
    return line.split(";", 1)[0].rstrip()


def number(t):
    t = t.lower()
    try:
        if t.startswith("0x"):
            return int(t, 16)
        if t.endswith("h"):
            return int(t[:-1], 16)
        return int(t)
    except ValueError:
        return None


def shift4(lines, i, span=6):
    """A 16-bit right shift BY FOUR within `span` lines of line i: `shr R, cl`
    with CL (or CX) = 4 set in the four lines before it, `shr R, 4`, or the
    `mov cx, 4 / shr / rcr / loop` of a 32-bit count."""
    for j in range(max(0, i - span), min(len(lines), i + span + 1)):
        m = SHRCL.match(lines[j])
        if not m:
            continue
        if m.group(2) == "4":
            return True
        if any(CL4.match(lines[k]) for k in range(max(0, j - 4), j)):
            return True
    return False


def segload_after(lines, i, span=16):
    return any(SEGLOAD.match(lines[j])
               for j in range(i + 1, min(len(lines), i + span + 1)))


REGRID = re.compile(r"^\s*" + LBL + r"and\s+(?:[abcd]l|" + R16 + r")\s*,\s*"
                    r"(?:0x0*f8|0*f8h|0xfff8|0*fff8h|-8)\s*$", re.I)


def regridded(lines, i, span=16):
    """A `round` site whose paragraphs are put back on the grid - `and al,
    0xF8` (or the word's 0xFFF8) after the shift and before the segment is
    loaded: the count rounded up to EIGHT paragraphs, which is the fix
    (KERN-MEGA-PLAN 14.40's histogram tail)."""
    for j in range(i + 1, min(len(lines), i + span + 1)):
        if SEGLOAD.match(lines[j]):
            return False
        if REGRID.match(lines[j]):
            return True
    return False


def mega_arm(lines):
    """`lines` with the SHIPPED arm of every PKG_MEGA conditional blanked
    (docs/plans/KERN-MEGA-PLAN.md 14.66, apps/os88mega.inc): the `%ifndef
    PKG_MEGA` body and the `%else` of an `%ifdef PKG_MEGA`. That arm is the
    build kern_mega is NOT meant to run - its package refuses there, or keeps
    its claims conventional, by its own word at the place - and it is the
    real-mode spelling a conversion leaves behind, which this gate exempts as
    t_cswrite.py exempts `%ifndef KERN_MEGA`. Line numbers are kept"""
    out, stack = [], []         # per open %if: [is PKG_MEGA's, live now]
    for ln in lines:
        tok = ln.split()
        d = tok[0].lower() if tok else ""
        arg = tok[1] if len(tok) > 1 else ""
        if d in ("%ifdef", "%ifndef", "%if", "%ifidn", "%ifnidn",
                 "%ifnum", "%ifstr", "%ifmacro", "%ifctx", "%ifempty"):
            pm = d in ("%ifdef", "%ifndef") and arg == "PKG_MEGA"
            stack.append([pm, (d == "%ifdef") if pm else True])
        elif d == "%else" and stack and stack[-1][0]:
            stack[-1][1] = not stack[-1][1]
        elif d == "%endif" and stack:
            stack.pop()
        dead = any(s[0] and not s[1] for s in stack)
        out.append("" if dead else ln)
    return out


def scan(path):
    """[(line, kind)] for every off-the-grid segment build in one source."""
    with open(path, errors="replace") as f:
        lines = mega_arm([code(x) for x in f.read().split("\n")])
    out = []
    rounding = set()
    for i, l in enumerate(lines):
        if AND15.match(l) and shift4(lines, i) and segload_after(lines, i):
            out.append((i + 1, "norm"))
        elif ADD15.match(l) and shift4(lines, i) and segload_after(lines, i):
            rounding.add(i)
            if not regridded(lines, i):
                out.append((i + 1, "round"))
    for i, l in enumerate(lines):
        m = SEGLOAD.match(l)
        if not m or not re.match(R16 + r"$", m.group(1), re.I):
            continue
        r = m.group(1).lower()
        for j in range(i - 1, max(i - 7, -1), -1):
            lj = lines[j]
            if FLOW.match(lj):
                break                   # a call or a jump: R is unknown here
            w = WRITER.match(lj)
            if not w or w.group(2).lower() != r:
                continue
            if w.group(1).lower() in ("cmp", "test", "push", "out"):
                continue                # reads R, does not write it
            s = STEP.match(lj)
            a = ADDI.match(lj)
            if s:
                out.append((i + 1, "step"))
            elif a and j not in rounding:
                n = number(a.group(2))
                if n is not None and n % 8:
                    out.append((i + 1, "step"))
            break                       # the nearest writer decides
    return sorted(out)


def sources():
    """Every .asm/.inc of every package directory that declares OS88_HIGH."""
    apps = os.path.join(ROOT, "apps")
    for d in sorted(os.listdir(apps)):
        top = os.path.join(apps, d)
        if not os.path.isdir(top):
            continue
        files = []
        high = False
        for base, dirs, fs in os.walk(top):
            dirs[:] = sorted(x for x in dirs if x != "hosttest")
            for f in sorted(fs):
                if not f.endswith((".asm", ".inc")):
                    continue
                p = os.path.join(base, f)
                files.append(p)
                if f.endswith(".asm") and not high:
                    with open(p, errors="replace") as fh:
                        high = bool(DECLARES.search(fh.read()))
        if high:
            for p in files:
                yield os.path.relpath(p, ROOT).replace("\\", "/")


def registry():
    """{path: (count, lineno)}, refusing a line it cannot read."""
    reg = {}
    with open(REGISTRY) as f:
        for n, raw in enumerate(f, 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            ok = len(parts) == 2 and parts[0].isdigit() and "#" in raw
            check(ok, "tests/megagrid.txt:%d reads `<sites> <path>  # "
                      "<reason>`" % n)
            if ok:
                check(parts[1] not in reg, "tests/megagrid.txt:%d registers "
                                           "%s twice" % (n, parts[1]))
                reg[parts[1]] = (int(parts[0]), n)
    return reg


def main():
    found = {}
    nfiles = 0
    for p in sources():
        nfiles += 1
        sites = scan(os.path.join(ROOT, p))
        if sites:
            found[p] = sites

    if "--list" in sys.argv:
        for p in sorted(found):
            for line, kind in found[p]:
                print("%s:%d  %s" % (p, line, kind))
        return

    reg = registry()
    for p, sites in sorted(found.items()):
        if p not in reg:
            check(False, "%s:%d builds a segment off kern_mega's 8-paragraph "
                         "grid (%s) in a package that says OS88_HIGH, and the "
                         "file is not in tests/megagrid.txt - a segment that "
                         "is not a multiple of 8 paragraphs is a fault or, "
                         "worse, an LDT alias of other memory under protected "
                         "mode (docs/plans/KERN-MEGA-PLAN.md 14.39). Normalise "
                         "to 8 paragraphs (`and si, 127`), or register the "
                         "file with a reason" % (p, sites[0][0], sites[0][1]))
            continue
        want, n = reg[p]
        check(len(sites) <= want,
              "%s has %d off-grid segment builds against the %d "
              "tests/megagrid.txt:%d allows (%s)" %
              (p, len(sites), want, n,
               ", ".join("%d %s" % s for s in sites)))
        check(len(sites) >= want,
              "%s has %d off-grid segment builds, below the %d "
              "tests/megagrid.txt:%d registers - lower the line" %
              (p, len(sites), want, n))
    for p, (_c, n) in sorted(reg.items()):
        check(p in found, "tests/megagrid.txt:%d registers %s, which has no "
                          "site left (or is gone) - delete the line" % (n, p))
    done("megagrid: %d off-grid segment builds in %d of %d OS88_HIGH "
         "package files, none new"
         % (sum(c for c, _n in reg.values()), len(reg), nfiles))


if __name__ == "__main__":
    main()
