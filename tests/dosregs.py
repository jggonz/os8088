#!/usr/bin/env python3
"""INT 21h gives back every register it does not answer in (SPEC.md 96.7.1.2).

EVERY EXPECTED VALUE BELOW IS A MEASUREMENT OF IBM DOS 3.30, not of anything
this project builds, so it is written down rather than derived:
`tests/dostrap/regs.asm` is the same binary run under a real DOS off a real
floppy, and `docs/reports/DOS-INT21-REGISTERS-2026-09-15.md` is that run.  It
cannot drift with the disk, the geometry or the build.

WHY A TABLE AND NOT A RULE.  "DOS preserves every register it does not answer
in" is a sentence out of a reference book, and applying it by reasoning is
what SPEC.md 96.7.1.1 is about: the argument was made for SI, DI and ES, DX
was left out of it because five functions answer in DX, and DX turned out to
be destroyed on 37 opens out of 37.  A mask measured on the machine needs no
interpretation - and it catches the opposite mistake too, a handler that
preserves a register it was supposed to ANSWER in.

THE `57 ` ROW IS RED ON PURPOSE and named as such: AH=57h is not implemented
here, because OSAPI_FILE_FIND's record carries no timestamp (SPEC.md 19.7.1),
so answering it is a kernel ABI change rather than a DOS-box one.  It is in
the table as a KNOWN row rather than left out, so that implementing it fails
this gate and makes somebody update the expectation - which is the opposite of
a gap nobody is holding.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88build                                               # noqa: E402
import os88ui                                                  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SYS = "build/os8088-360.img"
GATE = "build/dosregs360.img"

# B C D S I P E G = BX CX DX SI DI BP ES DS; '.' came back, a letter did not.
# The trailing digit is CF.  What is NOT here is any claim about which changes
# are legitimate - five functions answer in DX and two in ES:BX, so those read
# a letter on a correct DOS too.  The column IS the expectation.
WANT = {
    "19 ": "......../0",   "2A ": ".CD...../0",   "2C ": ".CD...../0",
    "30 ": "BC....../0",   "2F ": "B.....E./0",   "1A ": "......../0",
    "35 ": "B.....E./0",   "47 ": "......../0",   "3D ": "......../0",
    "44o": "..D...../0",   "3F ": "......../0",   "42 ": "......../0",
    "3Ea": "......../0",   "43 ": ".C....../0",   "4E ": "......../0",
    "4F ": "......../1",   "36 ": "BCD...../0",   "3C ": "......../0",
    "44c": "..D...../0",   "40 ": "......../0",   "44w": "..D...../0",
    "3Eb": "......../0",   "56 ": "......../0",   "02 ": "......../0",
    "0B ": "......../0",   "06 ": "......../0",   "09 ": "......../0",
    "0C ": "......../0",   "0E ": "......../0",   "29 ": "...S..../0",
    "25 ": "......../0",   "35b": "B.....E./0",   "39 ": "......../0",
    "3B ": "......../0",   "47b": "......../0",   "3Bb": "......../0",
    "3A ": "......../0",   "4D ": "......../0",   "41 ": "......../0",
    "0Ea": "......../0",   "3Dx": "......../0",   "44x": "..D...../0",
    "3Ex": "......../0",   "0Eb": "......../0",
}

# ...and the one row this box deliberately does not match.  The value is OURS,
# with what DOS says beside it, so that the day AH=57h is implemented this row
# fails and is updated rather than quietly passing on the old answer.
KNOWN = {"57 ": ("......../1", ".CD...../0",
                 "AH=57h is not implemented here (SPEC.md 96.7.1.2, finding "
                 "3): OSAPI_FILE_FIND's record carries no timestamp")}

# AH=44h AL=00h on a file handle, four ways.  Bit 6 is "has NOT been written
# through" and bits 0-5 are the drive THE FILE IS ON - which is only the drive
# the program is standing on by coincidence, so the last reading stands the
# machine on A: and opens B:REGS.COM by name.
DEV = {"open": "0041", "create": "0041", "written": "0001", "onB-from-A": "0041"}

# A label is two hex digits and then a disambiguator - `44o`, `44c`, `44w` are
# three readings of one call - or a space.  It has to be anchored on the `=`,
# because the device-word line below carries hex pairs of its own.
CELL = re.compile(r"([0-9A-F]{2}[a-z ])=([.BCDSIPEG]{8})/([01])")
DEVLINE = re.compile(r"44 devword: (.*)$")


def fail(msg):
    print("dosregs: FAIL: %s" % msg)
    sys.exit(1)


def main():
    # **THROUGH `os88build.at()` AND NOT AS A LITERAL** (docs/WRITING-TESTS.md
    # 70). A soak reads a FROZEN TREE and not `build/`, and the prewarm has
    # already built this row's `wants=` there - so a raw path looks in the
    # wrong directory, and the failure text points at the operator's `make`
    # rather than at the row. `os88ui.boot` resolves its own arguments, so
    # this GUARD was the only thing looking in `build/` and it fired first.
    for p in (SYS, GATE):
        if not os.path.exists(os.path.join(ROOT, os88build.at(p))):
            fail("%s is missing - `make build/dosregs360.img` builds it" % p)

    with os88ui.boot(SYS, apps=GATE) as ui:
        m = ui.m
        if not ui.path("B:/REGS.COM"):
            fail("double-clicking REGS.COM opened no window")
        rows = []
        end = time.time() + 180.0
        while time.time() < end:
            rows = m.screen() or []
            if any("REGS READY" in r for r in rows):
                break
            time.sleep(0.3)
        else:
            fail("the program never finished; the last text screen was %r"
                 % ([r.rstrip() for r in rows if r.strip()][:20],))
        print("dosregs: the bracket's text screen:")
        for r in rows:
            if r.strip():
                print("      | %s" % r.rstrip())

    got = {}
    dev = {}
    for r in rows:
        for label, mask, cf in CELL.findall(r):
            got[label] = "%s/%s" % (mask, cf)
        d = DEVLINE.search(r)
        if d:
            for kv in d.group(1).split():
                k, _, v = kv.partition("=")
                dev[k] = v

    if not got:
        fail("no answer cells on the screen at all - the probe printed "
             "nothing this row can read")

    missing = sorted((set(WANT) | set(KNOWN)) - set(got))
    if missing:
        fail("the probe printed no row for %s - regs.asm's table and this "
             "one have to name the same calls" % ", ".join(repr(k) for k in missing))

    bad = []
    for label in sorted(WANT):
        if got[label] != WANT[label]:
            bad.append("%r answered %s and IBM DOS 3.30 answers %s"
                       % (label, got[label], WANT[label]))
    for label, (ours, theirs, why) in sorted(KNOWN.items()):
        if got[label] == theirs:
            bad.append("%r now answers %s, which is what IBM DOS 3.30 "
                       "answers - the gap is CLOSED and this row's "
                       "expectation is stale (%s)" % (label, theirs, why))
        elif got[label] != ours:
            bad.append("%r answered %s; this box answers %s and IBM DOS 3.30 "
                       "answers %s (%s)" % (label, got[label], ours, theirs, why))

    for k in sorted(DEV):
        if dev.get(k) != DEV[k]:
            bad.append("the device word %r is %s and IBM DOS 3.30 answers %s "
                       "- bit 6 is 'has NOT been written through' and bits "
                       "0-5 are the drive THE FILE is on (SPEC.md 96.7.1.2)"
                       % (k, dev.get(k), DEV[k]))

    if bad:
        for b in bad:
            print("dosregs:   %s" % b)
        fail("%d of %d row(s) disagree with the reference"
             % (len(bad), len(WANT) + len(KNOWN) + len(DEV)))

    print("dosregs: %d calls and %d device words agree with IBM DOS 3.30, "
          "and %d known gap is still a gap"
          % (len(WANT), len(DEV), len(KNOWN)))
    print("dosregs: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
