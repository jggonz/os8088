#!/usr/bin/env python3
"""The project guide is paid up - and its fingerprint sees what it must.

    python3 tests/unit/t_guidelock.py           # fails on anything owed (full)
    python3 tests/unit/t_guidelock.py --warn    # reports, exits 0 (fast)

Two halves, t_abilock's shape. The first is the gate: every page in
tools/guide/manuals/ is stamped against the sources it describes and none of
them has moved since (tools/guide/manuals.lock.json), and every file on a
built disk has a line and, if it is a program, driver or system file, a page.
In the fast tier that is a WARNING, so no `make` is held up by documentation;
the full tier fails until it is paid up.

The second half proves the fingerprint is worth locking: it MOVES when a
string a user reads changes - a menu item in assembler, a message in C, a
help text in a host tool - and it does NOT move for a comment, a label or a
changed instruction. A lock that fired on every commit would be stamped
without reading; one that missed a menu would be no lock at all.
"""
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                          # noqa: E402
import guidelock                                         # noqa: E402

WARN = "--warn" in sys.argv[1:]


def fp_of(name, text):
    """The fingerprint of one throwaway source file."""
    d = tempfile.mkdtemp(prefix="guidelock-", dir=os.path.join(ROOT, "build"))
    try:
        open(os.path.join(d, name), "w").write(text)
        return guidelock.fingerprint([os.path.relpath(d, ROOT)])
    finally:
        shutil.rmtree(d)


def main():
    owed, built_all = guidelock.check()
    if WARN:
        for kind, msg in owed:
            print("warn: guide %s: %s" % (kind, msg))
        if owed:
            for line in guidelock.HOWTO:
                print("warn:   " + line)
    else:
        check(not owed, "the project guide is paid up",
              "a page stamped against sources that have since changed what a "
              "user reads, or a file on a disk with no page, is a guide that "
              "tells somebody something untrue. " + " ".join(guidelock.HOWTO),
              got=" | ".join("%s: %s" % o for o in owed[:6])
              + (" ... (%d in all)" % len(owed) if len(owed) > 6 else ""))

    asm = ("cal_mi_copy:  db 'Copy   ^C', 0  ; the Edit menu\n"
           "cal_go:\n    mov ax, 1\n    ret\n")
    base = fp_of("x.asm", asm)
    check(fp_of("x.asm", asm.replace("'Copy   ^C'", "'Copy all ^C'")) != base,
          "the fingerprint moves for a changed menu item",
          "a menu item is exactly what a manual quotes")
    check(fp_of("x.asm", asm.replace("; the Edit menu", "; renamed comment")) == base,
          "the fingerprint stands for a comment",
          "a comment is for the reader of the source, not the user")
    check(fp_of("x.asm", asm.replace("mov ax, 1", "mov ax, 2")) == base,
          "the fingerprint stands for a changed instruction",
          "a fix that says nothing new leaves the manual true")
    check(fp_of("x.asm", "m: db 'a;b', 0\n") != fp_of("x.asm", "m: db 'a', 0\n"),
          "a ';' inside a string is part of the string",
          "cutting the line at the first ';' would drop half a message")

    c = 'int main(void) { msg("Saved"); return 0; }\n'
    check(fp_of("x.c", c.replace("Saved", "Not saved")) != fp_of("x.c", c),
          "the fingerprint moves for a changed C message", "the C packages' text is in literals")
    check(fp_of("x.c", c.replace("return 0", "return 1")) == fp_of("x.c", c),
          "the fingerprint stands for changed C code", "code is not text")

    py = "ap.add_argument('--fps', help='frames a second')\nx = 1\n"
    check(fp_of("x.py", py.replace("frames a second", "frames per second")) != fp_of("x.py", py),
          "the fingerprint moves for a host tool's help text", "a tool's manual lists its options")
    check(fp_of("x.py", py.replace("x = 1", "x = 2")) == fp_of("x.py", py),
          "the fingerprint stands for a host tool's code", "code is not text")

    done("project guide lock")


if __name__ == "__main__":
    main()
