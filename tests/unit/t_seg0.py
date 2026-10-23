#!/usr/bin/env python3
"""Segment 0 in DS or ES is a RATCHET, and this is the gate
(docs/plans/KERN-MEGA-PLAN.md 10.1 item 11).

    python3 tests/unit/t_seg0.py           the gate
    python3 tests/unit/t_seg0.py --list    every site, by file and SECTION

Real mode reaches the IVT and the BIOS data area through segment 0 and a
linear offset - `xor ax, ax / mov ds, ax / mov al, [0x417]` - and the kernel
does it to save the byte `mov ax, 40h` costs. In 286 PROTECTED MODE 0 is the
NULL SELECTOR: loading it is legal and the first access through it is #GP(0).
That took kern_mega down on the Victory 286, the first machine to run the
keyboard pointer (kbm_shf, every UI pass with no mouse found) in protected
mode - and QEMU did NOT fault on the same code, because its TCG checks no
segment on a data access. So, like tests/unit/t_cswrite.py's rule, this one
has no emulator behind it, and the static count is the guard.

A SITE is `mov ds|es, R` on an arm kern_mega assembles (t_cswrite.walk), where
R was zeroed - `xor R, R`, `sub R, R` or `mov R, 0` - within the seven lines
before it and not reloaded in between. The conversion is MEGA_SEG0
(kernel/kernel.asm), which loads the low-memory selector in protected mode and
0 before the switch, and leaves the real-mode spelling in the `%else` arm,
where it is not counted.

Held to `tests/seg0.txt` exactly as t_cswrite holds its list: a file not
registered may have no site, a registered file may not exceed its count, and a
count now too HIGH fails too, so a conversion lowers the line. A site that
only uses the zero as a SENTINEL ("no document", ES = 0) is legal - loading a
null selector does not fault - and is registered as such rather than
converted.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402
import t_cswrite                                           # noqa: E402

ROOT = t_cswrite.ROOT
REGISTRY = os.path.join(ROOT, "tests", "seg0.txt")

ZERO = re.compile(r"^\s*(?:[A-Za-z_.$@?][\w.$@?#~]*:\s*)?"
                  r"(?:(?:xor|sub)\s+(\w\w)\s*,\s*\1|mov\s+(\w\w)\s*,\s*0+h?)"
                  r"\s*$", re.I)
SEGLOAD = re.compile(r"^\s*(?:[A-Za-z_.$@?][\w.$@?#~]*:\s*)?"
                     r"mov\s+(?:ds|es)\s*,\s*(\w\w)\s*$", re.I)
RELOAD = r"^\s*(?:[A-Za-z_.$@?][\w.$@?#~]*:\s*)?" \
         r"(?:mov|xchg|pop|lea|xor|sub|add|adc|sbb|inc|dec|or|and|in|lodsw|" \
         r"shl|shr|not|neg|les|lds)\s+%s\b"


def scan(path):
    """[(line, section)] for every segment-0 load kern_mega assembles."""
    lines, code_lines = t_cswrite.walk(path)
    out = []
    for i, l, where in code_lines:
        m = SEGLOAD.match(l)
        if not m:
            continue
        r = m.group(1)
        for j in range(i - 1, max(i - 8, -1), -1):
            z = ZERO.match(lines[j])
            if z and (z.group(1) or z.group(2)).lower() == r.lower():
                out.append((i + 1, where))
                break
            if re.match(RELOAD % r, lines[j], re.I):
                break                   # R holds something else here
    return out


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
            check(ok, "tests/seg0.txt:%d reads `<sites> <path>  # <reason>`"
                      % n)
            if ok:
                check(parts[1] not in reg,
                      "tests/seg0.txt:%d registers %s twice" % (n, parts[1]))
                reg[parts[1]] = (int(parts[0]), n)
    return reg


def main():
    found = {}
    for p in t_cswrite.sources():
        sites = scan(os.path.join(ROOT, p))
        if sites:
            found[p] = sites

    if "--list" in sys.argv:
        for p in sorted(found):
            for line, where in found[p]:
                print("%s:%d  %s" % (p, line, where))
        return

    reg = registry()
    for p, sites in sorted(found.items()):
        if p not in reg:
            check(False, "%s:%d loads segment 0 into DS or ES and is not in "
                         "tests/seg0.txt - in kern_mega's protected mode 0 is "
                         "the NULL selector and the first access through it "
                         "is #GP(0) (docs/plans/KERN-MEGA-PLAN.md 10.1 item "
                         "11). Use MEGA_SEG0, or register the file with a "
                         "reason" % (p, sites[0][0]))
            continue
        want, n = reg[p]
        check(len(sites) <= want,
              "%s has %d segment-0 loads against the %d tests/seg0.txt:%d "
              "allows - a new one in a file kern_mega shares" %
              (p, len(sites), want, n))
        check(len(sites) >= want,
              "%s has %d segment-0 loads, below the %d tests/seg0.txt:%d "
              "registers - lower the line" % (p, len(sites), want, n))
    for p, (_c, n) in sorted(reg.items()):
        check(p in found, "tests/seg0.txt:%d registers %s, which has no site "
                          "left (or is gone) - delete the line" % (n, p))
    done("seg0: %d segment-0 loads in %d files, none new"
         % (sum(c for c, _n in reg.values()), len(reg)))


if __name__ == "__main__":
    main()
