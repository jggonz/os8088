#!/usr/bin/env python3
"""A row that opens a BUILD ARTEFACT must resolve it against the run's tree.

    python3 tests/unit/t_artpath.py

THE FAILURE THIS EXISTS FOR, and it is the one that keeps coming back.  A soak
does not read `build/`: `tools/os88soak.py` freezes a tree of the run's own
(docs/plans/SOAK-PARALLEL.md 14.2) so that somebody else's `make` cannot rewrite
the images under 368 rows, and `$OS88_TREE` points at it.  `os88build.at()` is
what turns a `build/...` string into a path inside that tree, and it is applied
at the few places a path is USED - `os88marty.launch` stages both floppies,
`scratch_disk` reads its inputs, `os88sym` has honoured `$OS88_BUILD` forever.

A row that opens an artefact ITSELF is a use site those three do not cover, and
nothing said so.  It works perfectly by hand - with `$OS88_TREE` unset `at()` is
the identity function - and fails only inside a soak, several frames from the
cause, as `FileNotFoundError: 'build/pinme.o88'`.  That reads as a missing
build step, so the fix reached for is `wants=`, which the row usually ALREADY
HAS: `regpin` declares `build/regpin360.img`, prebuild builds it and every
prerequisite INTO THE TREE, and the row then opens `build/pinme.o88` and misses.
Measured: `regpin` passes standalone in 140s and failed the same soak.

Ten rows failed a 368-row soak this way in one run, and the count had grown
every soak because nothing counted it.  This is the ratchet: a new bare read is
a build failure with the fix in the message, not a soak row that dies in an
hour and a half.

WHAT IT ALLOWS.  Only the ARTEFACT extensions below - the things `make` writes
and a frozen tree therefore holds a different copy of.  `build/qemu.pid` and
friends are a row's own runtime scratch, written and read by the same process
in the same directory, and are deliberately not artefacts.  A path already
inside `os88build.at(...)`, or handed to a verb that resolves it (`launch`,
`scratch_disk`, `apps=`), is what this is asking for and passes.

THE ONE SHAPE IT CANNOT SEE is an argparse DEFAULT the row then probes itself:
`--bench default="build/bench360.img"` and an `os.path.exists(a.bench)` forty
lines down, through a variable no line-scan can follow.  Flagging every
`default=` instead was tried and is WRONG - it fires on 101 files, because the
usual default is `build/os8088-360.img` handed straight to `launch()`, which
resolves it already.  A gate that cries wolf on the common case is one people
route around, so this one stays quiet there.  `facescan` and `fsxdisp` were the
two rows that failed that way; both now resolve AT THE DEFAULT, which fixes
every downstream use at once and is the shape to copy.

The SPLIT form is the other blind spot - `os.path.join(ROOT, "build",
"mseg.img")` carries no `build/x` string at all.  Matching it was tried too and
is wrong for the same reason: it fires on 26 files, most of them building a
path they then hand to `launch()`.  `msegxms` and `xmcheck` were the two that
failed that way, both by passing `TESTAPPS=` at a `build/` path while
`os88fixture.make` had already redirected `BUILD=` to the tree - "No rule to
make target".  Both are fixed and both are worth recognising by eye.

So the rule this gate enforces is exactly one shape, and enforces it
completely: a `build/<name>.<artefact-ext>` LITERAL on a line that reads or
probes it.  That is the shape all ten soak failures took.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tests", "unit"))
from harness import check, done                              # noqa: E402

# What `make` writes.  A frozen run holds its own copy of each of these, so a
# bare read of one is a read of the wrong tree.
ART = re.compile(r'^build/[A-Za-z0-9_./%-]+\.'
                 r'(bin|o88|img|drv|ovl|OVL|F88|sys|kz|pkl)$')
# A read or a probe.  Passing the string to something that resolves it is fine.
USE = re.compile(r'\b(open|getsize|isfile|exists|read_bytes|Path|listdir)\b')
RESOLVES = re.compile(r'(os88build|\bat\(|launch|scratch_disk|apps=|--apps|--img)')
PATHLIT = re.compile(r'(["\'])((?:build)/[A-Za-z0-9_./%-]+)\1')


def scan(root):
    """Every bare artefact read under tests/, as (file, line, path)."""
    bare = []
    for sub in ("tests", os.path.join("tests", "unit")):
        d = os.path.join(root, sub)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if not name.endswith(".py") or name == "t_artpath.py":
                continue        # ...this file, whose PROSE names the paths it
                                # is about - the scan reads lines, not code
            f = os.path.join(d, name)
            rel = os.path.relpath(f, root)
            for i, ln in enumerate(open(f, errors="replace").read().split("\n"), 1):
                if RESOLVES.search(ln) or not USE.search(ln):
                    continue
                for m in PATHLIT.finditer(ln):
                    if ART.match(m.group(2).replace("%s", "x")):
                        bare.append((rel, i, m.group(2)))
    return bare


def main():
    bare = scan(ROOT)
    check(not bare,
          "no row reads a build artefact without os88build.at()",
          "wrap the path: `open(os88build.at(\"build/x.o88\"))`. With "
          "$OS88_TREE unset at() is the identity function, so nothing changes "
          "for a standalone run or for `make`; inside a soak it is the "
          "difference between reading the run's frozen tree and reading a "
          "build/ that another session may be rewriting. `wants=` is a "
          "SEPARATE question and usually already answered - it says what to "
          "BUILD, this says where to READ",
          got="; ".join("%s:%d %s" % b for b in bare) or "none",
          want="every artefact read resolved through os88build.at()")

    files = len({b[0] for b in bare})
    print("t_artpath: scanned tests/ and tests/unit/, %d bare artefact "
          "read(s) in %d file(s)" % (len(bare), files))
    done("t_artpath")


if __name__ == "__main__":
    main()
