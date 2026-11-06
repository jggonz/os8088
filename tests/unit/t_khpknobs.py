#!/usr/bin/env python3
"""Every KHP_* knob still assembles, on kern_big and kern_small, all on.

    python3 tests/unit/t_khpknobs.py

WHY. kernel/kernel.asm's KHP block holds kernel hot-paths pass 1's rewrites
of hot paths as knobs: each is an `%ifdef KHP_x` arm around the new body with
the old one in its `%else`. kern_mega defines eleven of them; seven are
defined by NO shipped kernel, kept so they outlive the branch they were
measured on (docs/plans/LAST-DROP-PERF.md section 7). An arm nobody assembles
rots silently: a renamed label, a moved variable or a changed register
contract in the code around it breaks the arm and nothing notices until the
day somebody turns the knob on. This assembles both kernels with EVERY knob
the sources test turned on, so a change that breaks an arm fails here, at
the change. (tools/stkbalance.py already walks the corpus with every knob on;
this is the assembler's half.)

What it does not check: that a knob still PAYS, or that one knob alone
assembles (each did when it was gated; see the LOG, section L.2). Turning a
knob on for a shipped kernel is a re-measure and its rows, not this row.

The output goes to a temporary file, never /dev/null (t_nulldev.py's rule).
"""
import glob
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KNOB = re.compile(r"^\s*%ifn?def\s+(KHP_\w+)", re.M)
FAIL = []


def knobs():
    names = set()
    for p in glob.glob(os.path.join(ROOT, "kernel", "*.inc")) + \
            glob.glob(os.path.join(ROOT, "kernel", "*.asm")):
        with open(p, encoding="utf-8", errors="replace") as f:
            names |= set(KNOB.findall(f.read()))
    return sorted(names)


def assemble(label, inc, defs, ks):
    if not os.path.isdir(os.path.join(ROOT, inc)):
        print("  skip: %s (%s not built)" % (label, inc))
        return
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "k.bin")
        cmd = (["nasm", "-f", "bin", "-w+error", "-I", "kernel/", "-I",
                "apps/", "-I", inc] + defs +
               ["-DKZIP", "-DKZ_SECS=0", "-DKZ_RPARA=0", "-DKZ_NBLK=1",
                "-DKZ_HEADSEC=9"] + ["-D" + k for k in ks] +
               ["-o", out, "kernel/kernel.asm"])
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        FAIL.append(label)
        print("  FAIL: %s with every knob on:" % label)
        for line in (r.stderr or r.stdout).splitlines()[:8]:
            print("        " + line)
    else:
        print("  ok: %s assembles with all %d knobs on" % (label, len(ks)))


def main():
    ks = knobs()
    if len(ks) < 2:
        print("t_khpknobs: found %d KHP_ knobs - the pattern or the tree "
              "moved" % len(ks))
        return 1
    assemble("kern_big", "build/", ["-DKERN_BIG"], ks)
    assemble("kern_small", "build/smallk/", ["-DKERN_SMALL"], ks)
    print("t_khpknobs: %s (%d knobs)" % ("FAIL" if FAIL else "ok", len(ks)))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
