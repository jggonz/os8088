#!/usr/bin/env python3
"""`CON` is a character device, not a file name that is missing.

    python3 tests/dosdev.py [--machine NAME]

SPEC.md 96.11.7, and the row exists because of what it cost: Microsoft
Works - the first program anyone ran on this box from outside the project -
could not open its own WORKS.INI, and said `Too many files open` about it.
The box was answering `file not found`, to `CON`.

Works opens CON at ONE CALL SITE over and over until DOS refuses, counts the
handles it got, and closes them again. Traced against IBM DOS 3.30 on the
same disk, DOS hands it 7, 8, 9 and then error 4; we failed the FIRST one, it
counted zero, and every later open it wanted was refused by Works before it
reached us. The handle table had one slot of eight in use throughout.

**THE ASSERTION IS NOT `CON OPENS`**, it is that opening it TWICE gives two
DIFFERENT handles, because that is the property the counting loop rests on -
and an implementation that answers with one of the five standard handles
passes `does CON open` and hangs the loop for ever.

CONDEV.COM runs under this box AND under a real DOS unchanged
(docs/DOS-DEBUGGING.md), so the answers it checks are the reference's.

WHAT IT WOULD CATCH: the device test taken out of AH=3Dh (A); a device handle
routed to the file write path, which refuses it (B); a close that does not
hand the slot back (C); AH=44h AL=08h or AH=0Dh falling back to `invalid
function`, which are the other two answers the Works trace showed DOS giving
and us refusing (D, E).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88marty as M                                      # noqa: E402
import os88ui                                              # noqa: E402

SYS = os.path.join(ROOT, "build", "os8088-360.img")
FLOPPY = os.path.join(ROOT, "build", "condev360.img")
MACHINE = "os8088_5150_cga_gla"
PROG = "B:/CONDEV.COM"


def fail(msg):
    print("dosdev: FAIL: %s" % msg)
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
        # An association open of a .COM RUNS it (SPEC.md 54), which is the
        # shortest route to a DOS program and the one a user takes.
        if not ui.path(PROG):
            fail("double-clicking %s opened no window" % PROG)

        rows = []

        def _seen(_m):
            rows[:] = [r.rstrip() for r in (m.screen() or []) if r.strip()]
            return any("CONDEV PASS" in r or "CONDEV FAIL" in r for r in rows)
        try:                        # GUEST time: `limit` is idle-box seconds
            M.until(m, _seen, "CONDEV.COM to report", poll=0.5,
                            limit=240.0)
        except M.MartyError:
            fail("CONDEV.COM never reported; the last screen was %r"
                 % rows[:12])

        for r in rows:
            print("  | %s" % r)
        if any("CONDEV FAIL" in r for r in rows):
            fail("the probe reported FAIL - the lettered line above it says "
                 "which of A..E, and SPEC.md 96.11.7 / 96.22.2 / 96.11.8 say "
                 "what each one is about")
        if not any("[written through CON]" in r for r in rows):
            fail("B passed but the bytes never reached the screen - a CON "
                 "write must take the teletype, which is what handles 1 and "
                 "2 already do (SPEC.md 96.11.7)")
        print("dosdev: ok - CON is a device, twice over, and hands its slots "
              "back")
    return 0


if __name__ == "__main__":
    sys.exit(main())
