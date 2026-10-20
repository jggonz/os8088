#!/usr/bin/env python3
"""README.TXT packs to exactly 8,088 bytes, because the machine is an 8088.

    python3 tests/unit/t_readme8088.py

A JOKE, PINNED.  `readme.txt` is the system disk's manual (SPEC.md 19.6) and
it ships LZ4-wrapped (SPEC.md 20.13.4); the packed file is 8,088 bytes and the
whole reason for that number is the part number on the front of the box.
Nothing in the OS reads it, no layout depends on it, and a byte either way
costs the machine nothing - which is exactly why it needs a row.  A size that
means something is defended by whatever it means; this one is defended by
nobody, so the next ordinary edit to the prose would retire it silently and
the person after that would never know it had been there.

**SO WHEN THIS GOES RED, NOTHING IS BROKEN.**  Somebody edited the manual and
the joke came loose.  The fix is the PROSE and never the number in this file:
add or cut until the packed file is 8,088 again.  The row prints the delta and
roughly what it is worth in characters, and the two ends the manual is already
bounded at do not move - `tools/checkreadme.py` still holds the 28-column
formatted width and Note Pad's 16KB ceiling, so there is room in both
directions.

WHY IT IS `fast`, WHICH THE RULE SAYS IT IS NOT.  It is about one file and
only an edit to that file can break it, so docs/WRITING-TESTS.md 2.1 puts it in
`soak` - and it sat there while two ordinary edits took the packed manual to
8,721 bytes with every `make` green (incident 79).  The owner moved it to
`fast`: it is 0.1 s and needs no build.  That is a decision about THIS row,
recorded in 2.1 beside `kernbudget`, and not an argument another row can
borrow.

WHAT IT COMPUTES RATHER THAN READS.  The Makefile applies CRLF at build time
and wraps the result (`$(SYSDOCRAW)` and `$(SYSDOC)`), so this does those two
steps to `readme.txt` itself.  The row then needs no `build/` at all and
cannot be fooled by a knob build: a tree built with `make PKGZ=` has a PLAIN
`build/readme.txt`, and a row that read it would go red for a knob rather than
for an edit.  The shipped artefact is checked too, but only when it IS a `CZ`
file - which is the same assertion from the other end, and catches the
Makefile wrapping the manual in some way this file does not predict.

THE OTHER PLACES THE SIZE IS WRITTEN DOWN, since a re-take has to reach them:
the `$(SYSDOC)` comment in the Makefile and SPEC.md 20.13.4's `README.TXT`
paragraph both quote it, and nothing checks either.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, eq, done                       # noqa: E402
import os88lz                                             # noqa: E402

WANT = 8088                             # the part number, and the whole point
SRC = os.path.join(ROOT, "readme.txt")
BUILT = os.path.join(ROOT, "build", "readme.txt")

# What a character of prose is worth once it has been through LZ4, for the
# advice below.  Measured over the edit that first landed the file on 8,088:
# one cut of 254 source bytes moved the packed file 126, and a 17-byte tweak
# at the end moved it 8.  So about two source bytes to the compressed byte -
# an aim, not an arithmetic, because the ratio depends on what the text is.
PER = 2


def crlf(data):
    """What `$(SYSDOCRAW)` writes, idempotent the way that rule is: LF is
    normalised out first, so a source that already had CRLF never doubles."""
    return data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")


def main():
    raw = open(SRC, "rb").read()
    disk = crlf(raw)
    packed, did = os88lz.cz_wrap(disk, os88lz.LZ4)
    n = len(packed)
    off = n - WANT

    if off:
        why = ("readme.txt is %d bytes of prose, %d with CRLF on the disk, "
               "and packs to %d - %s %d. About %d characters of prose to the "
               "compressed byte on this file, so %s roughly %d of them and "
               "run this again; the last few are worth doing as a wording "
               "choice you would defend anyway rather than as padding. "
               "tools/checkreadme.py bounds the other end and is unmoved."
               % (len(raw), len(disk), n,
                  "over by" if off > 0 else "under by", abs(off),
                  PER, "cut" if off > 0 else "add", abs(off) * PER))
    else:
        why = ""
    eq(n, WANT, "readme.txt no longer packs to %d bytes" % WANT, why)

    check(did, "readme.txt did not compress at all",
          "cz_wrap stores a file plain when wrapping it would not make it "
          "smaller (SPEC.md 20.14.5). Prose at 55%% cannot reach that, so "
          "this means the file is no longer prose or the compressor is "
          "broken - and tests/unit/t_lzfmt.py is the row for the second.")

    # The other end: what the tree actually BUILT, when what it built is
    # comparable. Three things make it not comparable, and every one of them
    # is somebody exercising the tree rather than breaking the manual - so
    # each is a skip with a reason on the screen, never a finding:
    #
    #   * no build/readme.txt at all - a clean tree;
    #   * a build/readme.txt that is not LZ4 - `make PKGZ=` leaves it PLAIN
    #     and `make PKGZ=lzb` leaves it LZB, and both are supported A/Bs;
    #   * one OLDER than readme.txt - the manual has been edited since the
    #     build, so the artefact describes the previous text and comparing it
    #     would print a second failure saying the same thing as the first.
    #
    # That last one is why this is a skip and not a check: without it, the one
    # edit this row exists to catch reports TWICE, and the second report reads
    # like an unrelated fault in the build.
    say = None
    if not os.path.exists(BUILT):
        say = "no build/readme.txt - nothing built yet"
    elif os.path.getmtime(BUILT) < os.path.getmtime(SRC):
        say = "build/readme.txt is older than readme.txt - stale, says nothing"
    else:
        blob = open(BUILT, "rb").read()
        got = os88lz.cz_parse(blob)
        if not got:
            say = "build/readme.txt is PLAIN - a `make PKGZ=` tree"
        elif got[0] != os88lz.LZ4:
            say = "build/readme.txt is %s, not LZ4 - a `make PKGZ=lzb` tree" % (
                os88lz.NAMES[got[0]])
        else:
            eq(blob, packed,
               "build/readme.txt is not the wrap this row computes",
               "the Makefile's $(SYSDOC) rule and this file have to agree on "
               "the format and its parameters, or the size checked above is "
               "of a file the disks do not carry.")
    if say:
        print("t_readme8088: shipped artefact not compared - %s" % say)

    done("t_readme8088")


if __name__ == "__main__":
    main()
