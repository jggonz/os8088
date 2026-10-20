#!/usr/bin/env python3
"""`AH=43h` is asked about DIRECTORIES, and answering 2 is `Directory not found`.

    python3 tests/dosattr.py [--machine NAME]

SPEC.md 96.12.4.  Microsoft Works's Save As, given a name on another drive,
asks `AH=43h` about the directory the file would go in before it writes
anything - and puts up `Directory not found` when that is refused.  `.att_get`
resolved every name through `dos_fh_stat`, the FILE lookup `AH=3Dh` opens
through, so every directory on every disk read as missing.

A ROOT is the sharper half: `A:\\` parses to a drive and NO 8.3 name at all,
so the lookup was for the empty name.  That is the case Works hits, and it is
why the failure looked like the drive switch - which works perfectly, the
trace showing `AH=0Eh` select A:, `AH=19h` confirming AL=00, and `AH=0Eh`
back again, all before the refusal.

THE REFERENCE IS THE SPECIFICATION and was taken on the machine
(docs/DOS-DEBUGGING.md).  IBM DOS 3.30 answers `\\` and `A:\\` with CF=0
CX=0074, a subdirectory with 0010, a file with 0020, and only a missing name
with CF=1 AX=0002.

**IT ASSERTS THE PROPERTY AND NOT DOS's EXACT CX FOR A ROOT.**  0074 is bits
DOS never deliberately set - a root has no directory entry to read them from -
and what every caller tests is CF and bit 4.  Copying an uninitialised byte
would be copying a bug and calling it a contract.

ATTRDIR.COM makes its own subdirectory and removes it again, so the disk needs
nothing but the program - and it runs under this box AND under a real DOS
unchanged, which is how the table above was taken.

WHAT IT WOULD CATCH, MEASURED rather than reasoned about: built against the
kernel with the fix taken back out, this row reads

    A root  "\\"         ax=0002 cx=2402 cf=1
    B root  "A:\\"       ax=0002 cx=2402 cf=1
    C subdir ATTRDIRX   ax=0002 cx=2402 cf=1
    D file  ATTRDIR.COM ax=0020 cx=0020 cf=0
    E missing NOSUCH    ax=0002 cx=0020 cf=1
    ATTRDIR FAIL

- red on exactly the three the fix is about, and still green on the file and
the missing name, so it fails on the DEFECT and not merely on a change.

**AND THE PROBE ITSELF PASSES UNDER IBM DOS 3.30**, which is the check that
says the assertion is not one only this box could satisfy: DOS answers the
two roots 0074 where we answer 0010, and `ATTRDIR PASS` comes up on both
machines because what is asserted is CF and bit 4.

THE CARRY IS BANKED BEFORE IT IS TESTED, in `ask`, and that is not a nicety:
the first version pushed AX and CX and then did `or bl, bl` between INT 21h
and the `jc`, so the judgement read its own flag. It printed FAIL beside five
correct answers, which is the shape of a gate that would later print PASS
beside five wrong ones.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88marty                                           # noqa: E402
import os88ui                                              # noqa: E402

SYS = os.path.join(ROOT, "build", "os8088-360.img")
FLOPPY = os.path.join(ROOT, "build", "attrdir360.img")
MACHINE = "os8088_5150_cga_gla"
PROG = "B:/ATTRDIR.COM"


def fail(msg):
    print("dosattr: FAIL: %s" % msg)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    a = ap.parse_args()
    for p in (SYS, FLOPPY):
        if not os.path.exists(p):
            fail("%s is missing - `make kdostest` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=FLOPPY, machine=a.machine) as ui:
        m = ui.m
        if not ui.path(PROG):
            fail("double-clicking %s opened no window" % PROG)

        seen = {"rows": []}

        def reported(_):
            rows = [r.rstrip() for r in (m.screen() or []) if r.strip()]
            seen["rows"] = rows
            return any("ATTRDIR PASS" in r or "ATTRDIR FAIL" in r
                       for r in rows)
        try:                            # a GUEST-time budget
            os88marty.until(m, reported, "ATTRDIR.COM to report", poll=0.5,
                            limit=240.0)
        except os88marty.MartyError:
            fail("ATTRDIR.COM never reported; the last screen was %r"
                 % seen["rows"][:12])
        rows = seen["rows"]

        for r in rows:
            print("  | %s" % r)
        if any("ATTRDIR FAIL" in r for r in rows):
            fail("the probe reported FAIL - the lettered lines above carry "
                 "each answer, and SPEC.md 96.12.4 has IBM DOS 3.30's own "
                 "beside them")
        print("dosattr: ok - a directory answers as a directory, and a root "
              "is a directory")
    return 0


if __name__ == "__main__":
    sys.exit(main())
