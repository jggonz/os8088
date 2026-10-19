#!/usr/bin/env python3
"""Every on-demand module is DROPPED somewhere, or says here why it is not.

    python3 tests/unit/t_moddrop.py

A module (SPEC.md 2.8) is kernel code read into a heap claim when its feature
is asked for - and the whole of what it buys is that the claim is given back
when the feature is done. A module nothing ever drops is the same bytes in a
different account: resident from its first use to power-off, and PINNED,
an island in the arena (docs/plans/completed/O88-MULTISEG-PLAN.md 1 says the
same of a module that is never dropped).

WHY A RATCHET. DESK.DRV shipped with "no mod_drop anywhere" written into its
own banner, priced at a 1 KB claim; it was a 5 KB claim, held for the session
after the last Sticky App closed, and it took a field report to find it
(SPEC.md 11.103.10's lifetime). Nothing refuses a module that is never
dropped - the feature works, the heap is just smaller - so the gate has to be
one that fails the build, and it has to be on the path EVERY new module takes:
a new `mod_tab` row. That row is what this reads.

THE CHECK. For each row of `mod_tab` (kernel/mod.inc), its MOD_* name has to
appear in CODE (not a comment) within WINDOW lines above a `mod_drop` in the
same file - `mov al, MOD_X / call mod_drop`, a tail `jmp mod_drop`, or a macro
argument (dock.inc's `%3 mod_drop`). It does not prove the drop is REACHED -
tests/stiknodesk.py's G is that, for DESK.DRV - it proves somebody wrote one.

A module that is held until restart ON PURPOSE goes in tests/moddrop.txt with
the reason, and the list turns ONE WAY: a line for a module that now has a
drop is a failure, so it cannot rot.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
KDIR = os.path.join(ROOT, "kernel")
REGISTRY = os.path.join(ROOT, "tests", "moddrop.txt")
WINDOW = 25                     # dock.inc's macro is 17 lines from its id


def code(line):
    return line.split(";", 1)[0]


def rows():
    """The MOD_* name of every mod_tab row, in order."""
    src = open(os.path.join(KDIR, "mod.inc")).read()
    body = src[src.index("\nmod_tab:"):src.index("\nmod_tab_end:")]
    defined = set(re.findall(r"^(MOD_\w+)\s+equ\b", src, re.M))
    out = []
    for line in body.splitlines():
        m = re.match(r"\s*dw\s+0\s*,\s*mod_f_(\w+)", line)
        if not m:
            continue
        named = re.search(r";\s*(MOD_\w+)", line)
        name = named.group(1) if named else "MOD_" + m.group(1).upper()
        out.append((name, name in defined, line.strip()))
    return out


def drops():
    """{MOD_name: [file:line, ...]} - every mod_drop each name reaches."""
    found = {}
    for fn in sorted(os.listdir(KDIR)):
        if not fn.endswith(".inc"):
            continue
        lines = open(os.path.join(KDIR, fn), errors="replace").read() \
            .splitlines()
        for i, line in enumerate(lines):
            if not re.search(r"\bmod_drop\b", code(line)) or \
                    re.match(r"\s*mod_drop\s*:", line):
                continue
            for j in range(max(0, i - WINDOW), i):
                for name in re.findall(r"\bMOD_[A-Z0-9]+\b", code(lines[j])):
                    found.setdefault(name, []).append("%s:%d" % (fn, i + 1))
    return found


def registry():
    held = {}
    if not os.path.exists(REGISTRY):
        return held
    for n, line in enumerate(open(REGISTRY), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r"(MOD_\w+)\s+#\s*(\S.*)", line)
        check(m is not None, "tests/moddrop.txt:%d is `MOD_X  # reason`" % n,
              "a line with no reason is a module nobody had to argue for",
              got=line, want="MOD_X  # why it is held until restart")
        if m:
            held[m.group(1)] = m.group(2)
    return held


def main():
    tab = rows()
    check(len(tab) >= 8, "mod_tab's rows were read out of kernel/mod.inc",
          "the parse is what everything below stands on",
          got=len(tab), want="every row")
    found = drops()
    held = registry()
    names = set()
    for name, ok, line in tab:
        names.add(name)
        check(ok, "%s names a MOD_* equ in kernel/mod.inc" % name,
              "a row whose comment names no module cannot be checked; name "
              "it in the row's comment as every other row does",
              got=line, want="dw 0, mod_f_x  ; MOD_X")
        where = found.get(name, [])
        if name in held:
            check(not where, "%s is registered as held, and has no drop" % name,
                  "the list turns one way: this module is dropped now, so "
                  "its line in tests/moddrop.txt is false - delete it",
                  got=", ".join(where), want="no mod_drop")
        else:
            check(bool(where), "%s is dropped somewhere" % name,
                  "a module nothing drops holds its claim from first use to "
                  "power-off - the bytes a module exists to give back. Drop it "
                  "when its feature is done (SPEC.md 2.8.3), or add a line to "
                  "tests/moddrop.txt saying why it is held until restart",
                  got="no `mod_drop` within %d lines of %s in kernel/*.inc"
                  % (WINDOW, name), want="a drop site")
    for name in held:
        check(name in names, "tests/moddrop.txt's %s is a mod_tab row" % name,
              "a line for a module that does not exist is a stale line",
              got=name, want="one of %s" % sorted(names))
    print("t_moddrop: %d mod_tab rows, %d held on purpose"
          % (len(tab), len(held)))
    done("t_moddrop")


if __name__ == "__main__":
    main()
