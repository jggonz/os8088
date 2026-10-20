#!/usr/bin/env python3
"""Nothing hands /dev/null to NASM as an output or a listing.

    python3 tests/unit/t_nulldev.py

NASM TREATS ITS OUTPUT FILES AS ITS OWN. On a failed assembly it deletes the
file `-o` names, and it replaces the file `-l` names even when the assembly
succeeds - measured on 2.16.01 and 3.02, against a private `mknod c 1 3`:

    -o <dev>, success   still a character device
    -o <dev>, failure   GONE (unlinked)
    -l <dev>, success   a REGULAR FILE
    -l <dev>, failure   a REGULAR FILE

This container runs as root, so pointed at /dev/null that is /dev/null
itself. Every `>/dev/null` in every process afterwards writes into a file,
which grows, and `./configure`, `diff` and `cmp` against it lie.
docs/plans/SOAK-PARALLEL.md 16 spent a session on exactly that symptom and
put it down to the layer under the repo; it was `tests/kerndos.py` running
`nasm -l /dev/null` on every soak - the file's birth time sat inside that
row's run - and eleven more sites were one failed assembly away from the
same thing.

So the gate is the ARGUMENT and not the device: `-o` or `-l` followed by
os.devnull or "/dev/null", in any Python, shell or make file this tree runs,
where the command it belongs to names nasm (the 400 characters before it -
an argv list is a few lines, and `curl -o /dev/null` or a Python writer that
open()s the path is harmless). A throwaway output goes to a temp file beside
the listing or map it was assembled for, and is removed with it.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

NUL = r"(?:os\.devnull|[\"']/dev/null[\"'])"
FLAG = r"[\"']-[ol][\"']"
# `"-o", os.devnull` in an argv list, possibly across a line break...
ARGV = re.compile(FLAG + r"\s*,\s*" + NUL)
# ...and `-o /dev/null` inside a shell line or a command string.
SHELL = re.compile(r"(?<![\w-])-[ol]\s+/dev/null\b")


def tracked():
    out = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True,
                         text=True).stdout.split()
    return [f for f in out
            if f.endswith((".py", ".sh", ".mk")) or os.path.basename(f) ==
            "Makefile"]


def main():
    me = os.path.relpath(os.path.abspath(__file__), ROOT)
    files = [f for f in tracked() if f != me]
    check(len(files) > 100, "the tree's scripts were found",
          why="an empty list passes every check below", got=len(files),
          want="> 100")
    hits = []
    for f in files:
        try:
            s = open(os.path.join(ROOT, f), errors="replace").read()
        except OSError:
            continue
        for rx in (ARGV, SHELL):
            for m in rx.finditer(s):
                if "nasm" not in s[max(0, m.start() - 400):m.start()]:
                    continue
                line = s.count("\n", 0, m.start()) + 1
                hits.append("%s:%d: %s" % (f, line, m.group(0).split("\n")[0]))
    check(not hits, "no NASM output or listing is pointed at /dev/null",
          why="nasm deletes a failed -o target and replaces a -l target even "
              "on success, so as root either one destroys /dev/null for the "
              "whole container (this file's docstring)",
          got=hits[:8], want="[]")
    done("t_nulldev")


if __name__ == "__main__":
    main()
