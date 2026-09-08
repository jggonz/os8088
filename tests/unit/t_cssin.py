#!/usr/bin/env python3
"""CLEAR SKIES' sine table is a QUARTER of the turn (SPEC.md 88.5.9), and
nothing held it to its generator before it became one.

    python3 tests/unit/t_cssin.py

`apps/skies/cssin.inc` says GENERATED at the top of it and was, once, by a
snippet in SPEC.md 88.5 that lives nowhere a build can run. This row is that
snippet: it regenerates the 257 entries and compares them, and then it walks
`cs_sin`'s own arithmetic - the top ten bits of the angle, bit 8 reflecting
the quarter and bit 9 negating it - over all 1,024 indices of a full turn
against `sin` itself.

THE ONE DIFFERENCE IS DELIBERATE AND IS CHECKED FOR: the full table used to
hold -32,768 at exactly 270 degrees, because that is what rounding sin(270)
gives and only the positive peak was clamped. A quarter table's peaks are
symmetric, so 270 now reads -32,767 - the same 1/32768 that SPEC.md 88.5.3
already calls nobody's pixel, at one index of 1,024. Every OTHER index must
agree to the unit.
"""
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "apps", "skies", "cssin.inc")
ASM = os.path.join(ROOT, "apps", "skies", "cs3d.inc")
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def table():
    out = []
    for ln in open(SRC):
        ln = ln.split(";")[0]
        m = re.match(r"\s*dw\s+(.*)", ln)
        if m:
            out += [int(t) for t in m.group(1).split(",") if t.strip()]
    return out


def main():
    t = table()
    check(len(t) == 257,
          "a quarter of the turn plus its last entry: 257 words (%d)" % len(t))
    if len(t) != 257:
        return 1
    want = [min(32767, int(round(math.sin(k * 2 * math.pi / 1024) * 32768)))
            for k in range(257)]
    off = [k for k in range(257) if t[k] != want[k]]
    check(not off, "every entry is SPEC.md 88.5's own snippet (%d off: %s)"
          % (len(off), off[:6]))
    check(t[0] == 0 and t[256] == 32767,
          "sin 0 is 0 and sin 90 is the clamp (%d, %d)" % (t[0], t[256]))

    # cs_sin's arithmetic, written out: idx = angle >> 6, the quarter's offset
    # in the low eight bits, bit 8 reflects and bit 9 negates
    def cs_sin(idx):
        r, quad = idx & 255, (idx >> 8) & 3
        v = t[256 - r] if (quad & 1) else t[r]
        return -v if (quad & 2) else v

    wrong = []
    for i in range(1024):
        truth = min(32767, int(round(math.sin(i * 2 * math.pi / 1024) * 32768)))
        got = cs_sin(i)
        if got != truth and not (i == 768 and got == -32767):
            wrong.append((i, got, truth))
    check(not wrong, "the reflection walks a whole turn (%d wrong: %s)"
          % (len(wrong), wrong[:4]))
    check(cs_sin(768) == -32767,
          "...and 270 degrees is the one deliberate unit: -32767 for the old "
          "table's -32768 (%d)" % cs_sin(768))
    check(cs_sin(0) == 0 and cs_sin(256) == 32767 and cs_sin(512) == 0,
          "the axis crossings are EXACT, which SPEC.md 88.9.2.2's divide by "
          "cos depends on (%d, %d, %d)"
          % (cs_sin(0), cs_sin(256), cs_sin(512)))

    # ...and the routine still reads the table the way this row assumes
    src = open(ASM).read()
    m = re.search(r"^cs_sin:(.*?)^\s*\.pos:", src, re.S | re.M)
    check(bool(m) and "mov cl, 6" in m.group(1) and "and bx, 255" in m.group(1)
          and "test ch, 1" in m.group(1) and "test ch, 2" in m.group(1),
          "cs_sin is the quarter-table form this row models")

    print("  %s" % ("ok" if not bad else "FAILED: %d" % len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
