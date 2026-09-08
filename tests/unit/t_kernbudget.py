#!/usr/bin/env python3
"""docs/KERNEL-MEMORY.md's baseline has to describe THIS kernel's budget.

    python3 tests/unit/t_kernbudget.py

`tools/kernsize.py` reports what a change cost the kernel, against a baseline
kept between the `kernsize:` markers in docs/KERNEL-MEMORY.md, and its own
header says the rule: *"Bless it in the same commit as the change."*  Nothing
enforced that, and nothing could NOTICE it either - the report compared
`spare`, which is `KERN_BUDGET - KERN_SIZE`, so a budget raise and a size
shrink of the same amount read identically and a raise on its own read as the
kernel having got smaller.

So the baseline sat two moves behind: 119,808 against a `kernel.asm` that had
said 121,344 and then 122,368, and every `make` printed a report with the old
figure in it and no complaint.  That is how the document came to be stale in
the first place, and the document is where every RAM number in this project
is produced from.

WHY THIS IS A GATE AND NOT A WARNING.  `KERN_BUDGET` is not a measurement -
CLAUDE.md is explicit that raising it "is a decision to take with whoever
asked for the feature, not a build fix".  A commit that moves it and does not
bless leaves the ledger in docs/KERNEL-MEMORY.md describing a machine that no
longer exists, and the next person's arithmetic comes out of that ledger.
Failing here costs one command, `tools/kernsize.py --bless`; not failing here
has cost two moves already.

WHAT IT DOES NOT CHECK, so nobody trusts it further: only `budget`, and only
against the `equ` in kernel/kernel.asm.  Every other figure in the baseline is
a MEASUREMENT of a previous build and is meant to lag - that is what makes it
a baseline.  A knob kernel has no baseline at all (`--bless` refuses one) and
is not looked at here.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

DOC = os.path.join(ROOT, "docs", "KERNEL-MEMORY.md")
KERNEL = os.path.join(ROOT, "kernel", "kernel.asm")
BEGIN, END = "<!-- kernsize:begin -->", "<!-- kernsize:end -->"


def budgets():
    """{"big": n, "small": n} from kernel/kernel.asm's two arms.

    They are `%ifdef KERN_BIG` / `%else`, in that order and nowhere else, so
    the first `KERN_BUDGET equ` is big's and the second is small's.  The order
    is ASSERTED rather than assumed: a file that grew a third arm, or lost the
    `%ifdef`, would otherwise be read confidently and wrongly.

    BIG'S IS DERIVED AND NOT A LITERAL.  Rule 3 says kern_big fully resides in
    KERN_RESIDENT_KB once it is at the desktop, and the span starts at
    KERNEL_SEG, so there is nothing left to choose - the arm reads
    `KERN_RESIDENT_KB*1024 - KERNEL_SEG*16` and this evaluates it from the same
    file.  Only that one shape: an arm doing arithmetic this does not
    recognise has to fail rather than be guessed at, for the reason the
    positional parse above fails.
    """
    src = open(KERNEL, errors="replace").read()
    hits = [(m.start(), m.group(1).strip())
            for m in re.finditer(r"^KERN_BUDGET\s+equ\s+([^;\n]+)", src, re.M)]
    big = src.find("%ifdef KERN_BIG")
    if len(hits) != 2 or big < 0 or not (big < hits[0][0] < hits[1][0]):
        return None
    out = {}
    for key, (_, expr) in zip(("big", "small"), hits):
        if re.fullmatch(r"\d+", expr):
            out[key] = int(expr)
            continue
        if expr != "KERN_RESIDENT_KB*1024 - KERNEL_SEG*16":
            return None
        c = {}
        for name in ("KERN_RESIDENT_KB", "KERNEL_SEG"):
            m = re.search(r"^%s\s+equ\s+(0x[0-9A-Fa-f]+|\d+)" % name, src, re.M)
            if not m:
                return None
            c[name] = int(m.group(1), 0)
        out[key] = c["KERN_RESIDENT_KB"] * 1024 - c["KERNEL_SEG"] * 16
    return out


def baseline():
    """The blessed figures, per variant, or None if the block is unreadable."""
    doc = open(DOC, errors="replace").read()
    m = re.search(re.escape(BEGIN) + r"(.*?)" + re.escape(END), doc, re.S)
    if not m:
        return None
    body = re.sub(r"^```\w*$", "", m.group(1).strip(), flags=re.M).strip()
    try:
        blob = json.loads(body)
    except ValueError:
        return None
    return blob if isinstance(blob, dict) else None


def main():
    have, base = budgets(), baseline()
    check(have is not None,
          "kernel/kernel.asm's two KERN_BUDGET arms are where this expects",
          "the parse is positional - %ifdef KERN_BIG, then %else - so a third "
          "arm or a moved %ifdef has to fail rather than answer",
          got="%d KERN_BUDGET equ line(s)" % (len(re.findall(
              r"^KERN_BUDGET\s+equ", open(KERNEL, errors="replace").read(),
              re.M))),
          want="exactly 2 after `%ifdef KERN_BIG`, each a literal or "
               "`KERN_RESIDENT_KB*1024 - KERNEL_SEG*16`")
    check(base is not None,
          "docs/KERNEL-MEMORY.md carries a readable kernsize baseline",
          "tools/kernsize.py writes it and reads it; without it every report "
          "prints absolute numbers and no delta at all",
          got="no parseable block between the kernsize markers",
          want="the JSON tools/kernsize.py --bless writes")
    if have is None or base is None:
        done("t_kernbudget")
        return

    for variant in ("big", "small"):
        v = base.get(variant)
        if not isinstance(v, dict) or "budget" not in v:
            check(False, "the baseline carries kern_%s" % variant,
                  "both are SHIPPED kernels (docs/history/KERN-SPLIT-PLAN.md), so both "
                  "are blessable and both should be blessed",
                  got="no `budget` for %r" % variant,
                  want="run tools/kernsize.py --bless for it")
            continue
        check(v["budget"] == have[variant],
              "the blessed kern_%s budget is kernel.asm's" % variant,
              "KERN_BUDGET is a decision, not a measurement (CLAUDE.md's memory "
              "rule), and a baseline holding the old one makes every kernsize "
              "report describe a machine that no longer exists. One command: "
              "tools/kernsize.py --bless",
              got="docs/KERNEL-MEMORY.md says %d" % v["budget"],
              want="kernel/kernel.asm's %d" % have[variant])

    print("t_kernbudget: KERN_BUDGET big %d, small %d - blessed baseline agrees"
          % (have["big"], have["small"]))
    done("t_kernbudget")


if __name__ == "__main__":
    main()
