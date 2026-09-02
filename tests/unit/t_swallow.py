#!/usr/bin/env python3
"""A statement inside a block comment is a statement that does not run.

    python3 tests/unit/t_swallow.py

This tree's house style puts an aligned `/* ... */` to the RIGHT of a
statement and continues it down the following lines with a leading `*`, which
is how most of the reasoning in apps/ is written and is worth keeping.  It is
fine while the continuation lines are prose.  It is a defect the moment one of
them also carries code, because C does not care what the author meant:

    c64_copy_req = 0;                       /* ...and a Copy or Paste queued
    c64_paste_req = 0;                       * behind this one dies with the
                                             * machine that asked for it */

That is `apps/c64/c64.c` as it shipped.  `c64_paste_req = 0;` is inside the
comment the line above it opened, so a C64 machine reset cleared one flag and
not the other - and what it did not clear is the REQUEST the menu leaves for
the wake to spend (`apps/c64/c64cmd.c:296`, `apps/c64/c64kbd.c:1014`), so a
Paste queued behind a reset outlived the machine that asked for it.  Exactly
what the eaten half of the comment says the pair exists to prevent.  Measured
when it was fixed: the package grew SIX BYTES, one `mov word [mem], 0`, which
is the store that had never been emitted.

WHY NOTHING ELSE CATCHES IT.  The surviving statements are valid C, so the
compilation is clean - no warning, no error, no diagnostic of any kind.  The
binary is smaller rather than wrong-looking.  Nothing in the tree diffs a
comment against the code beside it, and a reader's eye reads the column of
statements and not the column of asterisks.  It cost apps/weave the same bug
twice in one afternoon before it cost anything to find: three assignments that
set a text component's pen and cell budget went into a comment, and every
`<text>` then drew at y = 0 with a width of zero cells - which reads as "the
painter does not draw text" and not as a comment bug.

WHAT IT LOOKS FOR, and why the rule is this shape: a line inside an
unterminated `/* ... */` that does not begin with `*` or `/*`.  A continuation
line of genuine prose in this tree's style always begins with the asterisk;
one that does not is a line the author meant as code.  That is a house-style
rule rather than a language rule, which is what lets a twenty-line scanner be
exact where a C parser would have to be a C parser.

ONE LIMITATION, stated so nobody trusts it further: a prose continuation
written WITHOUT the leading asterisk is a false positive, and the fix is to
write the asterisk rather than to except the file.  Every C file in the tree
passes today, so the convention is already universal; this row is what keeps
it that way.
"""
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Every C source in the tree.  Not a list of packages: a package added
# tomorrow is covered tomorrow, which is t_mirror's rule about self-
# maintaining lists applied to a different hazard.
PATTERNS = ("apps/*/*.c", "apps/*/*/*.c", "apps/cc/*.c", "tests/*/*.c")


def swallowed(path):
    """(line, opened_at, text) for every line of code inside a block comment."""
    hits = []
    inside = False
    opened = 0
    with open(path, errors="replace") as f:
        for n, line in enumerate(f.read().split("\n"), 1):
            if not inside:
                j = line.find("/*")
                if j >= 0 and line.find("*/", j) < 0:
                    inside, opened = True, n
                continue
            if "*/" in line:
                inside = False
                continue
            t = line.strip()
            if t and not t.startswith("*") and not t.startswith("/*"):
                hits.append((n, opened, t))
    return hits


def main():
    files = []
    for pat in PATTERNS:
        files += glob.glob(os.path.join(ROOT, pat))
    files = sorted(set(files))

    check(len(files) > 0, "there are C sources to scan",
          "a pattern that matches nothing passes silently and defends nothing",
          got=len(files), want="at least one")

    for path in files:
        rel = os.path.relpath(path, ROOT)
        for n, opened, text in swallowed(path):
            check(False, "%s:%d is code, not comment" % (rel, n),
                  "it sits inside the block comment opened at line %d, so it "
                  "does not run - and the file still compiles, which is the "
                  "whole hazard (see this file's header for the two it cost)"
                  % opened,
                  got=text[:80], want="a line beginning with `*`, or the "
                                      "comment closed above it")

    print("t_swallow: %d C file(s) scanned" % len(files))
    done("t_swallow")


if __name__ == "__main__":
    main()
