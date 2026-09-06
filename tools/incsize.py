#!/usr/bin/env python3
"""What a shared include actually COSTS a package, per routine.

    python3 tools/incsize.py apps/os88ui.inc apps/notepad/notepad.asm [out.txt]

SOURCE BYTES ARE NOT COMPILED BYTES, and for this tree's SDK the gap is total:
`apps/os88api.inc` is 229,207 source bytes included by ninety packages and
emits **nothing at all** - 226 `equ`s, 155 `%define`s and thirteen macros, so
a package pays only for what it uses. Ranking the shared includes by source
size times includers therefore puts the one that cannot be shrunk at the top.
`apps/os88ui.inc` is the one that emits: 2,364 bytes into notepad, 2,384 into
SHEET, 1,448 into Paint - the difference being which of its three optional
regions each package turns on.

`tools/kernsize.py --modules` answers this for the kernel and there was no
equivalent for a package, so a size finding about a shared include had no way
to be measured rather than estimated - which is the one rule a size pass has.

HOW. Assemble the package with `nasm -l`, then attribute by ADDRESS: every
label the include defines is found in the listing and its size is the distance
to the next label. That is what the assembler emitted, jump distances and all,
rather than instruction bytes counted by hand.
"""
import re, sys, subprocess, os

inc, src, out = sys.argv[1], sys.argv[2], sys.argv[3]
lst = "/tmp/attrib.lst"
subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                "-l", lst, "-o", "/tmp/attrib.bin", src], check=True)

# every global label DEFINED in the include
want = set()
for line in open(inc, encoding="utf-8", errors="replace"):
    t = line.split(";")[0].strip()
    m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*):", t)
    if m:
        want.add(m.group(1))

# walk the listing: remember the last label seen, and the first address after it
LAB = re.compile(r"^\s*\d+\s+(?:([0-9A-F]{8})\s+)?(?:[0-9A-F<>\-\(\)]+\s+)?(?:<\d+>\s+)?([A-Za-z_][A-Za-z0-9_]*):")
ADDR = re.compile(r"^\s*\d+\s+([0-9A-F]{8})\s+[0-9A-F]")
marks = []          # (addr, label)
pending = None
for line in open(lst, encoding="utf-8", errors="replace"):
    m = LAB.match(line)
    if m:
        pending = m.group(2)
        if m.group(1):
            marks.append((int(m.group(1), 16), pending)); pending = None
        continue
    m = ADDR.match(line)
    if m and pending:
        marks.append((int(m.group(1), 16), pending)); pending = None

marks.sort()
tot = 0
rows = []
for i, (a, name) in enumerate(marks):
    end = marks[i + 1][0] if i + 1 < len(marks) else a
    if name in want and end > a:
        rows.append((end - a, name, a))
        tot += end - a
rows.sort(reverse=True)
with open(out, "w") as f:
    for n, name, a in rows:
        f.write("%6d  %-28s @%04X\n" % (n, name, a))
    f.write("TOTAL %d bytes in %d routines from %s\n" % (tot, len(rows), inc))
print("TOTAL %d bytes in %d routines" % (tot, len(rows)))
for n, name, a in rows[:22]:
    print("%6d  %-28s @%04X" % (n, name, a))
