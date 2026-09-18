#!/usr/bin/env python3
"""Two flags that share one byte may not share a BIT.

    python3 tests/unit/t_bits.py

There is no linker here and no compiler that knows what a bit field is: a
flag is `NAME equ 32` and a byte is a byte.  So the only thing standing
between a tree with 43 flag families in it and two names on one bit is
somebody sorting the `equ` lines by eye - which is the same class of gate as
t_mirror's *"change them together"*, and it failed the same way.

WHAT IT COST.  `FHF_DEV equ 32` was added to `apps/dos/dos.asm` beside
`FHF_INPLC equ 16` - the line above it - seven lines away from the
`FHF_WROTE equ 32` that already owned bit 5, with four unrelated `DOS_DEV_*`
codes sitting in the gap between them.  It assembles, it boots, and CON opens
correctly, which is what the change was for.  What it also does is make the
FIRST `AH=40h` on ANY handle set `FHF_WROTE` and so make that handle read as
a character DEVICE for ever after: every later read answers end of file, and
every later write is ACCEPTED AND DISCARDED with its full count reported.
Microsoft Works saved a document whose 384-byte header was all zeroes and
said nothing, on the first program anyone ran on this box from outside the
project.

THE LIST MAINTAINS ITSELF, which is the whole point and is why this is not a
table of families in a file somewhere.  Nothing here says which constants
share a byte; it reads the CODE.  A `test`/`or`/`and`/`xor` whose destination
is a memory field and whose source is a bare symbolic constant is a statement
that the two belong to that field - so every flag is enrolled by the first
line that uses it, and a flag added tomorrow is covered tomorrow with nobody
remembering anything.  Today that is 43 fields across `apps/`, `kernel/`,
`drivers/` and `kerndos/`, including `W_FLAGS`, `FH_FLAGS`, `SKO_FL`,
`DSK_R_ATTR` and both of Word's and Scribe's run-attribute bytes.

A PREFIX IS NOT A FAMILY, and that was tried first: grouping by the `XX_`
prefix reports 81 "collisions" in this tree and every one of them is two
different families that happen to share a top-level prefix - `SV_NFISH` the
count against `SV_FIVAL` the interval, `PM_READY` the state against `PM_UP`
the direction.  A check with 81 false positives is a check nobody runs.

A MASK IS NOT A FLAG.  A constant with more than one bit set is a named
combination - `test al, FHF_MADE | FHF_INPLC` written once as a name - so it
is reported and not compared.  A symbol whose value cannot be resolved FAILS
rather than passing quietly: an unresolved member means its family is not
being checked at all, and a gate that silently checks nothing is worse than
no gate.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TREES = ("apps", "kernel", "drivers", "boot", "kerndos")

# `NAME equ <value>`, in every spelling nasm takes here: 0x1F, 31, 1Fh, 11111b,
# and `1 << NAME` - which is resolved in a second pass because the shift count
# is itself a symbol in `kernel/vidsel.inc`.
DEF = re.compile(r"^([A-Z][A-Z0-9_]*)\s+equ\s+(.+?)\s*(?:;.*)?$")
NUM = re.compile(r"^(?:0x([0-9A-Fa-f]+)|([0-9A-Fa-f]+)[hH]|([01]+)[bB]|([0-9]+))$")
SHIFT = re.compile(r"^\(?\s*1\s*<<\s*([A-Z][A-Z0-9_]*|[0-9]+)\s*\)?$")

# `test byte [si+FH_FLAGS], FHF_DEV` and its three siblings. The destination
# must be MEMORY: a register is not a field, and `and al, 0x7F` on a value
# that came from one would enrol the wrong things.
USE = re.compile(r"^\s*(?:test|or|and|xor)\s+(?:(?:byte|word)\s+)?"
                 r"(\[[^\]]+\])\s*,\s*([A-Z][A-Z0-9_]*)\s*(?:;.*)?$")
FIELD_OFF = re.compile(r"\+\s*([A-Z][A-Z0-9_]*)\s*\]")
FIELD_ABS = re.compile(r"^\[\s*(?:[a-z]{2}:)?([a-z_][a-z0-9_]*)\s*\]$")


def sources():
    for t in TREES:
        for dp, _, fns in os.walk(os.path.join(ROOT, t)):
            for fn in sorted(fns):
                if fn.endswith((".asm", ".inc")):
                    p = os.path.join(dp, fn)
                    yield os.path.relpath(p, ROOT), p


def literal(txt):
    m = NUM.match(txt)
    if not m:
        return None
    if m.group(1):
        return int(m.group(1), 16)
    if m.group(2):
        return int(m.group(2), 16)
    if m.group(3):
        return int(m.group(3), 2)
    return int(m.group(4), 10)


def read_values():
    """Every NAME -> the set of values the tree gives it.

    A name defined twice with the SAME value is the mirroring t_mirror already
    guards; a name defined twice with two values is ambiguous here and is
    reported rather than guessed at.
    """
    vals, pend = {}, []
    for rel, p in sources():
        with open(p, errors="replace") as f:
            for ln in f:
                m = DEF.match(ln.rstrip())
                if not m:
                    continue
                name, txt = m.group(1), m.group(2)
                n = literal(txt)
                if n is None:
                    s = SHIFT.match(txt)
                    if s:
                        pend.append((name, s.group(1)))
                    continue
                vals.setdefault(name, set()).add(n)
    for _ in range(4):                       # a shift of a shift, if it happens
        again = []
        for name, by in pend:
            if by.isdigit():
                vals.setdefault(name, set()).add(1 << int(by))
            elif by in vals and len(vals[by]) == 1:
                vals.setdefault(name, set()).add(1 << next(iter(vals[by])))
            else:
                again.append((name, by))
        if not again:
            break
        pend = again
    return vals


def read_families():
    """(file, field) -> {symbol: first line that enrolled it}, off the CODE."""
    fams = {}
    for rel, p in sources():
        with open(p, errors="replace") as f:
            for i, ln in enumerate(f, 1):
                m = USE.match(ln.rstrip())
                if not m:
                    continue
                dest, sym = m.group(1), m.group(2)
                f2 = FIELD_OFF.findall(dest) or FIELD_ABS.findall(dest)
                if not f2:
                    continue
                fams.setdefault((rel, f2[0]), {}).setdefault(sym, i)
    return {k: v for k, v in fams.items() if len(v) >= 2}


def main():
    vals = read_values()
    fams = read_families()
    bad, masks = [], 0
    for (rel, field), syms in sorted(fams.items()):
        seen = {}
        for sym, line in sorted(syms.items()):
            v = vals.get(sym)
            if not v:
                bad.append("%s:%d  %s is used against %s and this file cannot "
                           "resolve its value, so that family is NOT checked"
                           % (rel, line, sym, field))
                continue
            if len(v) > 1:
                bad.append("%s:%d  %s is defined as %s in different places, so "
                           "%s cannot be checked" % (rel, line, sym,
                                                     sorted(v), field))
                continue
            n = next(iter(v))
            if n and (n & (n - 1)):
                masks += 1                   # a named combination, not a flag
                continue
            if n in seen:
                bad.append("%s:%d  %s and %s are BOTH %d, and both are used "
                           "against %s - one byte, one bit, two meanings"
                           % (rel, line, seen[n], sym, n, field))
            else:
                seen[n] = sym
    print("t_bits: %d flag field(s) across %s, %d named mask(s) skipped"
          % (len(fams), "/".join(TREES), masks))
    for b in bad:
        print("  FAIL: %s" % b)
    if bad:
        print("t_bits: %d problem(s)" % len(bad))
        return 1
    print("t_bits: ok - no two flags on one bit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
