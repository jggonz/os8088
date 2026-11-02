#!/usr/bin/env python3
"""AH=62h and AH=51h answer the RUNNING program's PSP (SPEC.md 96.55).

IBM DOS 3.30's own XCOPY.EXE asks `AH=62h` before anything else it does and
parses its command tail out of the segment it is told. This DOS had no
handler, so the call fell to the invalid-function arm, which answers CF=1 and
AX=0001 - and leaves BX, the whole answer, as the program had it. DOS sets no
carry for 62h and nobody tests one, so XCOPY read its tail out of a segment
that was a leftover and said `Invalid parameter` to every command line.

`tests/dostrap/getpsp.asm` needs no reference value: a .COM's PSP is its own
CS. It loads BX with 5A5Ah, asks, and prints OK or BAD - for the parent, for
a child it EXECs (which must be told ITS OWN PSP and not its parent's), and
for the parent again once the child is gone. IBM DOS 3.30 prints seven lines
and every one says OK; that is the table below, measured, not derived.
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
GATE = "build/dospsp360.img"

# the line prefixes IBM DOS 3.30 printed, in order, every one `OK`
WANT = ["P62", "P51", "C62", "C51", "R62"]
ROW = re.compile(r"\b([PCR](?:62|51)) BX=([0-9A-F]{4}) PSP=([0-9A-F]{4}) (OK|BAD)")


def fail(msg):
    print("dospsp: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, GATE):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=GATE, quick=True) as ui:
        m = ui.m
        if not ui.path("B:/GETPSP.COM"):
            fail("double-clicking GETPSP.COM opened no window")
        rows = []

        def ready(_m):
            rows[:] = m.screen() or []
            return any("GETPSP READY" in r for r in rows)
        try:
            os88marty.until(m, ready, "GETPSP READY", poll=0.05, limit=120)
        except os88marty.MartyError:
            fail("the program never finished; the last text screen was %r"
                 % ([r.rstrip() for r in rows if r.strip()][:14],))
        print("dospsp: the bracket's text screen:")
        for r in rows:
            if r.strip():
                print("      | %s" % r.rstrip())

    got = [ROW.search(r) for r in rows]
    got = [g.groups() for g in got if g]
    names = [g[0] for g in got]
    if names != WANT:
        fail("the probe printed %r and IBM DOS 3.30 prints %r - a child "
             "that never ran, or one that ran as the parent, is this"
             % (names, WANT))
    by = dict((g[0], g) for g in got)
    for name, bx, psp, verdict in got:
        if verdict != "OK" or bx != psp:
            fail("%s answered BX=%s where the running program's PSP is %s%s "
                 "(SPEC.md 96.55)"
                 % (name, bx, psp,
                    " - 5A5A is the probe's own junk, so the call left BX "
                    "untouched: it is not answered at all" if bx == "5A5A"
                    else ""))
        print("dospsp: ok  - %s BX=%s" % (name, bx))
    if by["C62"][1] == by["P62"][1]:
        fail("the child was told the PARENT's PSP (%s)" % by["P62"][1])
    if by["R62"][1] != by["P62"][1]:
        fail("after the child returned, the parent was told %s and its PSP "
             "is %s" % (by["R62"][1], by["P62"][1]))
    if not any("EXEC=0000" in r for r in rows):
        fail("the probe's EXEC of itself did not run (no EXEC=0000 line)")
    print("dospsp: PASS")


if __name__ == "__main__":
    main()
