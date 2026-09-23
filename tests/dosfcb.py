#!/usr/bin/env python3
"""AH=29h parses a name into an FCB, exactly as DOS does (SPEC.md 96.28).

THE TABLE BELOW IS A MEASUREMENT OF IBM DOS 3.30, not of anything this project
builds, so it is written down rather than derived: `tests/dostrap/parsefcb.asm`
is the same binary run under a real DOS, and every row here is what that
machine answered.  It cannot drift with the disk, the geometry or the build.

THE CARRY IS THE ROW.  Unimplemented, AH=29h fell to the "invalid function"
arm and answered CF=1 with AX=0001 - and DOS does not use the carry for this
call at all, so a program reading AL, which for this call every program does,
was told its plain name HAD WILDCARDS IN IT.  That is SPEC.md 96.22's shape:
not a refusal a caller can act on, but a plausible answer to a question nobody
asked.  A handler that gets the FCB right and leaves the carry set would still
be broken, so CF is asserted on every row.

FOUR ROWS DECIDE THE IMPLEMENTATION and none is guessable: a wildcard becomes
`?` and not `*`; an invalid drive answers FFh AND STILL WRITES its number; the
name is upper-cased (`B:Prince.exe` is what Prince's installer really passes);
and a PATH is not a path - `A:\\DIR\\PRINCE.EXE` advances SI by TWO and leaves
the name blank, because 29h parses a NAME and stops at the first separator.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
GATE = "build/dosfcb360.img"

# input -> (AL, CF, SI advance, the drive byte as printed, the eleven bytes)
WANT = [
    ("PRINCE.EXE",        "00", "0", "0A", "0", "PRINCE  EXE"),
    ("B:PRINCE.EXE",      "00", "0", "0C", "2", "PRINCE  EXE"),
    ("*.*",               "01", "0", "03", "0", "???????????"),
    ("B:*.*",             "01", "0", "05", "2", "???????????"),
    ("PRINCE",            "00", "0", "06", "0", "PRINCE     "),
    ("Z:PRINCE.EXE",      "FF", "0", "0C", ":", "PRINCE  EXE"),
    ("B:Prince.exe",      "00", "0", "0C", "2", "PRINCE  EXE"),
    ("prince.dat",        "00", "0", "0A", "0", "PRINCE  DAT"),
    ("A:\\DIR\\PRINCE.EXE", "00", "0", "02", "1", "           "),
]

ROW = re.compile(r"AL=([0-9A-F]{2}) CF=(\d) SI\+=([0-9A-F]{2}) FCB=(.)\[(.{11})\]")


def fail(msg):
    print("dosfcb: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, GATE):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=GATE) as ui:
        m = ui.m
        if not ui.path("B:/PARSEFCB.COM"):
            fail("double-clicking PARSEFCB.COM opened no window")
        rows = []
        end = time.time() + 180.0
        while time.time() < end:
            rows = m.screen() or []
            if any("PARSEFCB READY" in r for r in rows):
                break
            time.sleep(0.3)
        else:
            fail("the program never finished; the last text screen was %r"
                 % ([r.rstrip() for r in rows if r.strip()][:14],))
        print("dosfcb: the bracket's text screen:")
        for r in rows[:16]:
            if r.strip():
                print("      | %s" % r.rstrip())
        got = [ROW.search(r) for r in rows]
        got = [g.groups() for g in got if g]

    if len(got) < len(WANT):
        fail("read %d answer lines off the screen and wanted at least %d"
             % (len(got), len(WANT)))

    # The probe prints more rows than are asserted here; match each wanted one
    # against the answers IN ORDER, skipping the ones this row does not pin.
    it = iter(got)
    for name, al, cf, adv, drv, fcb in WANT:
        for g in it:
            if g[2] == adv and g[0] == al:
                break
        else:
            fail("no answer line matching %r (AL=%s SI+=%s) in %r"
                 % (name, al, adv, got))
        if g[1] != cf:
            fail("%r answered CF=%s and IBM DOS 3.30 answers CF=%s. AH=29h "
                 "does not use the carry AT ALL - a caller reads AL "
                 "(SPEC.md 96.28)" % (name, g[1], cf))
        if g[3] != drv:
            fail("%r put %r in the FCB's drive byte; DOS puts %r "
                 "(1 = A, and an INVALID drive still writes its number)"
                 % (name, g[3], drv))
        if g[4] != fcb:
            fail("%r filled the FCB with %r; DOS fills it with %r "
                 "(a wildcard is `?`, the name is UPPER, and a path stops at "
                 "the first separator)" % (name, g[4], fcb))
        print("dosfcb: ok  - %-18s AL=%s CF=%s SI+=%s [%s]"
              % (name, al, cf, adv, fcb))
    print("dosfcb: PASS")


if __name__ == "__main__":
    main()
