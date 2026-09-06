#!/usr/bin/env python3
"""A doc that names the task stack's SIZE must name the size the kernel has.

    python3 tests/unit/t_stackprose.py

WHY THIS EXISTS, and it is not a style rule. `SCH_STACK` has been **1,536,
512, 256 and 384** (kernel/sched.inc's own comment carries the history, and
docs/KERNEL-MEMORY.md's "The switch is a constant and a rebuild now" carries
the 12x256 -> 8x384 arithmetic). The authoritative sections followed it every
time - SPEC.md 2.1 and 20.6 rule 6 both say 384 and always did the day it
landed. What did NOT follow were the forty-odd places that CITE them: a
comment in `kernel/wm.inc`, a rule in `docs/UPSTREAM.md`, the message
`tools/cc8086.py` prints at a package author when a frame is too wide.

That is worse than a wrong number in a doc, because of what a reader does
with it. A session merging `main` into this branch read docs/UPSTREAM.md's

    "Check the worker's stack if the package claims one. The slice is 256
     bytes (SPEC.md 8, 20.6 rule 6)."

...and SPEC.md 56.13's "it is 256 bytes here and 512 on `main`", and reported
a live contract difference between the two trees that had to be adapted for.
There was none: both trees have been 384 since #112, and #112 is an ancestor
of both. The cost of that is not a wrong sentence. It is an INVESTIGATION -
the session went looking for what the incoming package would have to change,
and the honest version of that hunt is hours. docs/UPSTREAM.md's whole subject
is telling a real divergence from an imagined one, so a stale number in THAT
file manufactures exactly the failure the file exists to prevent.

The same shape as tools/os88geom.py and t_mirror, one layer out. os88geom
guards a constant a host SCRIPT retyped, where the symptom is a harness that
reports the feature under test as broken. This guards a constant a HUMAN
retyped in prose, where the symptom is a person believing something moved.
Neither is caught by an assembler, a linker there is none of, or a diff.

WHAT IT SCANS. Every `.md`, `.inc`, `.asm`, `.c`, `.h` and `.py` in the tree
for a sentence that states a task or worker stack size, then compares the
number against the kernel. Nothing enumerates which files are covered, so a
new doc is covered the day it is written - t_mirror's "the list maintains
itself" property, and for its reason.

THE ALLOWLIST IS FOR HISTORY, and history is a real thing to want: a field
note recording an overrun that happened at 256, a budget ledger row, a port
narrative whose measurements were taken then. Those must keep their numbers -
rewriting a measurement to today's constant would falsify the record. So they
are registered here with a reason, and the registration is the point: it is
the difference between a number that is deliberately of its time and one
nobody has looked at since 2026. A sentence in the present tense is not
history and does not belong in this list; date it in the prose instead
("the slice was 256 when this was measured"), which is what every site fixed
alongside this file now does.
"""
import bisect
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tests", "unit"))
from harness import check, done                                # noqa: E402

# --- what the kernel actually says -------------------------------------------
#
# Read, never mirrored: a copy of the value in here would be one more thing
# that can go stale, which is the bug this file is about.
AUTHORITY = {
    "SCH_STACK": "kernel/sched.inc",       # a background/worker task's slice
    "MAX_TASKS": "kernel/sched.inc",       # slots; slot 0 owns no slice
    "STK0_SIZE": "kernel/kernel.asm",      # task 0 - the UI task's own stack
}

_EQU = re.compile(r'^\s*(\w+)\s+equ\s+([0-9][0-9a-fA-FxX]*)', re.M)
_NL = re.compile(r"\n")


_COND = re.compile(r"^\s*%(\w+)\s*(\S*)", re.M)


def kernel_constants(arm="big"):
    """The three constants, READ from the kernel and never mirrored.

    **It folds the OTHER kernel's arm out first**, and it has to: the tree
    builds two kernels off one source and `MAX_TASKS` is 7 under
    `%ifdef KERN_SMALL` and 14 otherwise (SPEC.md 51.0.0). A reader that takes
    the first `equ` it sees would compare every sentence in the tree against
    whichever arm happens to be written first - and the prose describes the
    SHIPPED kernel, so `big` is the default.

    That is not hypothetical. `tools/os88geom.py` had exactly this bug for as
    long as there were two kernels: it mirrored `WIN_SIZE` = 34 and kern_small
    is 28, so every script pointed at that build decoded the window table at
    the wrong stride and returned plausible numbers. The fix there was the same
    fold, and this is the second file to need it - which is why it is written
    out rather than borrowed: this file's whole contract is that it reads the
    kernel with no help from anything that could itself be stale.
    """
    out = {}
    for name, rel in AUTHORITY.items():
        text = open(os.path.join(ROOT, rel), encoding="utf-8").read()
        live, depth = [], 0
        for line in text.split("\n"):
            mo = _COND.match(line)
            if mo:
                d, a = mo.group(1).lower(), mo.group(2)
                if d == "ifdef" and a in ("KERN_BIG", "KERN_SMALL"):
                    live.append(["k", (a == "KERN_BIG") == (arm == "big")])
                elif d.startswith("if"):
                    live.append(["?", True])
                elif d.startswith("eli") and live:
                    live[-1] = ["?", True]          # not understood -> read it
                elif d == "else" and live and live[-1][0] == "k":
                    live[-1][1] = not live[-1][1]
                elif d == "endif" and live:
                    live.pop()
                continue
            if not all(e[1] for e in live):
                continue
            m = _EQU.match(line)
            if m and m.group(1) == name:
                out[name] = int(m.group(2), 0)
                break
    return out


# --- the sentences that state a size -----------------------------------------
#
# Tight on purpose. This tree calls a great many unrelated things a "slice" -
# WEAVE's op budget, the C64's wall slice in CPU cycles, RunCPM's instruction
# slice, a colour table's stride - and a guard that cried wolf over those
# would be switched off within a week. Every pattern below requires the words
# `task` or `worker` next to the number. The `[- ]` between the two words
# is load-bearing and was found by TESTING this guard rather than reading
# it: docs/TESTING.md writes "task-stack margin" with a hyphen, and a
# space-only pattern walked straight past it.
WORKER = [
    re.compile(r'(\d[\d,]*)[-\s]+byte(?:s)?\s+(?:task|worker)[-\s]+(?:stack|slice)', re.I),
    re.compile(r"(?<!UI )(?:task|worker)(?:'s)?(?:\s+[\w']+){0,2}[-\s]+(?:stack|slice)\s+is\s+(?:\*\*)?(\d[\d,]*)(?:\*\*)?\s+bytes", re.I),
    re.compile(r'worker\s+(?:gets|owns|has)\s+(?:\*\*)?(\d[\d,]*)(?:\*\*)?[-\s]*(?:bytes|BYTES)', re.I),
    re.compile(r'(?:task|worker)(?:\'s)?[-\s]+stacks?\s+(?:are|is)\s+(\d[\d,]*)\s+bytes', re.I),
    re.compile(r'(?:worker|task)[^.\n]{0,40}?has\s+(\d[\d,]*)\s+bytes of stack', re.I),
    re.compile(r'(\d[\d,]*)[-\s]+byte\s+worker[-\s]+stack', re.I),
]
# Task 0's stack is a different constant, and conflating the two is its own
# bug: a package's window callbacks run on 1,024 bytes, its worker on 384.
TASK0 = [
    re.compile(r'task 0\'s\s+(\d[\d,]*)[-\s]+byte\s+stack', re.I),
    re.compile(r'UI task\'s\s+stack\s+is\s+(\d[\d,]*)[-\s]*byte', re.I),
    re.compile(r'UI task\'s\s+stack\s+is\s+(\d[\d,]*)\s+bytes', re.I),
    re.compile(r'package\'s\s+stack\s+is\s+(\d[\d,]*)\s+bytes', re.I),
]

SKIP_DIRS = {".git", "build", "__pycache__", "vm", "node_modules",
             ".claude"}     # agent worktrees under .claude/worktrees/ are whole
                            # copies of the tree; ten of them took this row
                            # from 5 s to a 60 s timeout
EXTS = (".md", ".inc", ".asm", ".h", ".c", ".py", ".txt")

# --- deliberate history ------------------------------------------------------
#
# (path, the exact snippet, why it keeps its number). A measurement is not
# refactorable: it was taken on a machine that had that slice.
HISTORY = [
    ("docs/KERNEL-MEMORY.md",
     "`.lowbss` migration + 256-byte task stacks",
     "a row of the BUDGET LEDGER - what that move cost when it was made. The "
     "ledger is a history of raises and this row is one of them; rewriting it "
     "to 384 would claim the .lowbss migration bought something it did not"),
]


def scan():
    """Every (path, line, number, kind, text) this tree states.

    NORMALISED, not line-by-line, and that is not tidiness. Two of the three
    sites this guard was written for state the size ACROSS a line break -
    docs/UPSTREAM.md puts "the worker's stack" on one line and "**384** bytes"
    on the next - and a line-based scan walked past both while passing. So the
    file is flattened first: a newline plus whatever leader the next line
    carries (`;` in asm, `*` in a C block, `#`, `//`, a markdown `-`) collapses
    to one space, with a map back to the original line number for the report.
    """
    hits = []
    # Every newline is a match (the leader group is optional), so this is
    # walked once per LINE and never per character - a char loop over SPEC.md
    # alone put this row at 15s against the fast tier's whole 30s budget.
    leader = re.compile(r'\n[ \t]*(?:;+|\*|#+|//|-|\|)?[ \t]*')
    for root, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for fn in sorted(files):
            if not fn.endswith(EXTS):
                continue
            path = os.path.join(root, fn)
            rel = os.path.relpath(path, ROOT)
            if rel == os.path.join("tests", "unit", "t_stackprose.py"):
                continue                     # this file quotes the bad numbers
            try:
                raw = open(path, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            low = raw.lower()
            if "stack" not in low and "slice" not in low:
                continue                     # the cheap majority, skipped whole

            # Flatten, keeping a map back to the source line. `parts` copies
            # verbatim runs and collapses each line leader to one space;
            # `segs` records where each run started in both texts, so a match
            # offset is turned into a line by two bisects rather than a scan.
            parts, segs, flat_len, pos = [], [], 0, 0
            for m in leader.finditer(raw):
                chunk = raw[pos:m.start()]
                segs.append((flat_len, pos))
                parts.append(chunk)
                flat_len += len(chunk)
                segs.append((flat_len, m.start()))
                parts.append(" ")
                flat_len += 1
                pos = m.end()
            segs.append((flat_len, pos))
            parts.append(raw[pos:])
            text = "".join(parts)

            nl = [m.start() for m in _NL.finditer(raw)]
            fstarts = [f for f, _ in segs]

            def lineof(off):
                i = bisect.bisect_right(fstarts, off) - 1
                fs, rs = segs[i if i >= 0 else 0]
                return bisect.bisect_right(nl, rs + (off - fs)) + 1

            srclines = raw.split("\n")
            seen = set()
            for kind, pats in (("task0", TASK0), ("worker", WORKER)):
                for pat in pats:
                    for m in pat.finditer(text):
                        no = lineof(m.start())
                        val = int(m.group(1).replace(",", ""))
                        if (no, val) in seen:
                            continue
                        seen.add((no, val))
                        # the SOURCE line, not the matched fragment: HISTORY
                        # registers a snippet of the line a reader sees, and
                        # after normalisation m.group(0) is neither.
                        src = srclines[no - 1] if no <= len(srclines) else ""
                        hits.append((rel, no, val, kind, src.strip()))
    return hits


def main():
    k = kernel_constants()
    missing = [n for n in AUTHORITY if n not in k]
    check(not missing,
          "every authority constant was found in the kernel",
          "this file reads SCH_STACK, MAX_TASKS and STK0_SIZE out of the "
          "kernel rather than mirroring them. A rename that this scanner "
          "cannot follow makes it pass vacuously, which is worse than "
          "failing",
          got=", ".join(missing) or "none missing", want="all three read")
    if missing:
        done("t_stackprose")

    want = {"worker": k["SCH_STACK"], "task0": k["STK0_SIZE"]}
    allowed = {(p, s) for p, s, _ in HISTORY}

    hits = scan()
    stale = []
    for rel, no, val, kind, text in hits:
        if val == want[kind]:
            continue
        if any(rel == p and snip in text for p, snip in allowed):
            continue
        stale.append((rel, no, val, kind, text))

    check(not stale,
          "no doc or comment states a task-stack size the kernel disagrees with",
          "SCH_STACK has been 1,536, 512, 256 and 384; a citation that did "
          "not follow reads as a live fact. docs/UPSTREAM.md's stale 256 had "
          "a session report a contract difference between this branch and "
          "`main` that did not exist - both have been 384 since #112. Either "
          "correct the number, or, if it is a MEASUREMENT taken when the "
          "slice was smaller, say so in the prose ('the slice was 256 when "
          "this was measured') and leave the figure alone. Register a row in "
          "HISTORY only for a record that must keep its number, with the "
          "reason",
          got="; ".join("%s:%d says %d (%s), kernel says %d"
                        % (r, n, v, kd, want[kd]) for r, n, v, kd, _ in stale)
              or "none",
          want="every stated size equal to the kernel's, or registered history")

    for path, snip, _why in HISTORY:
        full = os.path.join(ROOT, path)
        text = open(full, encoding="utf-8", errors="replace").read() if os.path.exists(full) else ""
        check(snip in text,
              "registered history still exists: %s" % path,
              "a HISTORY row whose snippet has been edited away is a stale "
              "exemption, and the next wrong number in that file goes "
              "unnoticed behind it",
              got="not found" if snip not in text else "present",
              want=snip[:60])

    print("t_stackprose: %d stated size(s) checked against SCH_STACK=%d, "
          "STK0_SIZE=%d, MAX_TASKS=%d; %d registered as history"
          % (len(hits), k["SCH_STACK"], k["STK0_SIZE"], k["MAX_TASKS"],
             len(HISTORY)))
    done("t_stackprose")


if __name__ == "__main__":
    main()
