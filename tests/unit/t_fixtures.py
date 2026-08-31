#!/usr/bin/env python3
"""A row's scratch disk is a BUILD PRODUCT, so it is rebuilt when build/ moves.

    python3 tests/unit/t_fixtures.py

Emulator rows need floppies `make all` does not build - one package and one
picture, say - so they make them in /tmp and keep them.  Keeping is right:
os88disk is deterministic, the image is a pure function of its inputs, and
rebuilding it forty times buys nothing.

THE TRAP IS THE GUARD THEY WERE KEPT BEHIND.  `if not os.path.exists(path)`
builds the image once, out of whatever build/ held that minute, and every run
afterwards boots that - so a fix reads as having changed nothing, and the
defect it fixed reads as pre-existing.  It is the stale build/kernel.bin trap
in CLAUDE.md's Testing section wearing different clothes, and just as quiet:
nothing fails, nothing warns, the row simply answers a question about a
binary nobody is looking at any more.

It has already cost this project two wrong answers in one session, both of
them acted on: tests/paintsu.py read 0 differing pixels against a fixture
predating the paint.o88 under test - and that number was reported and pushed
on - while tests/paintbig.py blamed pt_resize for a refusal measured on a
Paint that did not have the fix in it.

`os88marty.scratch_disk` is the answer and this row is what keeps it: it takes
os88disk's own positional arguments, so the inputs it mtime-checks cannot
drift out of step with the contents the way a second list passed alongside
them would.  What this file forbids is going back - a `subprocess` call to
os88disk.py sitting under an existence-only guard.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
TESTS = os.path.join(ROOT, "tests")

# Exempt, and why.  The reason has to be a fact about the test: a disk whose
# contents are GENERATED at build time is not stale when build/ moves, because
# build/ is not where it came from.
EXEMPT = {
    "fmthumb.py": "the 30 filler .TXT files are written inside the guard, so "
                  "the disk's inputs are the guard's own output and no "
                  "build/ product reaches it",
    "fdlgthumb.py": "same shape - the fillers and MUPTEST.O88 are built "
                    "inside the guard, which has its own nested exists check "
                    "for the package",
}

CALL = re.compile(r"os88disk\.py")
GUARD = re.compile(r"not\s+os\.path\.exists\s*\(")


def main():
    for name in sorted(os.listdir(TESTS)):
        if not name.endswith(".py"):
            continue
        lines = open(os.path.join(TESTS, name)).read().splitlines()
        hits = []
        for i, line in enumerate(lines):
            if not GUARD.search(line):
                continue
            # Everything more deeply indented than the guard IS the guard's
            # block, which beats counting lines: fdlgthumb builds a package
            # and 24 fillers before it reaches the disk, and any constant big
            # enough for that would be big enough to catch anything.
            depth = len(line) - len(line.lstrip())
            for j in range(i + 1, len(lines)):
                nxt = lines[j]
                if nxt.strip() and len(nxt) - len(nxt.lstrip()) <= depth:
                    break
                if CALL.search(nxt):
                    hits.append(j + 1)
        if name in EXEMPT:
            check(bool(hits), "tests/%s is still exempt" % name,
                  "it no longer builds a disk behind an existence-only "
                  "guard, so the exemption is stale and its reason - %s - "
                  "now describes nothing. Delete the EXEMPT entry."
                  % EXEMPT[name])
            continue
        check(not hits,
              "tests/%s rebuilds its scratch disk when build/ moves" % name,
              "os88disk.py is called under `not os.path.exists(...)` at "
              "line(s) %s, so the image is built ONCE and every run after it "
              "boots whatever build/ held that minute. Use "
              "os88marty.scratch_disk(path, *files) instead - it mtime-checks "
              "the same files it puts on the disk."
              % ", ".join(str(h) for h in hits))

    done("scratch disks")


if __name__ == "__main__":
    main()
