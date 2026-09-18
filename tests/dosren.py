#!/usr/bin/env python3
"""AH=56h renames where it stands, and says so when it cannot (SPEC.md 96.31).

THE TABLE IS A MEASUREMENT OF IBM DOS 3.30 by the same binary
(tests/dostrap/renref.asm), so it is written down rather than derived: it is a
property of DOS and cannot drift with the disk or the build.

AX IS JUNK ON SUCCESS - DOS reports 0012h on the rows that worked - so only CF
is asserted there.  Everywhere else the code is asserted too.

TWO ROWS DECIDE THE IMPLEMENTATION.  The two names must resolve to the SAME
drive and an unqualified one means the CURRENT drive, not the other name's: a
handler that resolves the new name against wherever the old one lives renames
happily on the other drive, where DOS answers 11h.  And a path in the new name
is a MOVE that DOS makes and OSAPI_FILE_RENAME cannot, so it is refused.

THE DRIVE LETTERS ARE BUILT AT RUN TIME, from AH=19h, and that is what lets one
row mean the same thing on both machines - the reference runs from A: and the
package is launched off B:.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
GATE = "build/dosren360.img"

# label -> (AX or None when it is junk, CF)
WANT = [
    ("rename",         None, "0"),
    ("gone",         "0002", "1"),
    ("onto itself",  "0005", "1"),
    ("old THIS drv",   None, "0"),
    ("old OTHER drv", "0011", "1"),
    ("new OTHER drv", "0011", "1"),
    ("new is a path",  None, "0"),
    ("old OTHER,real","0011", "1"),
]
ROW = re.compile(r"^(.*?)\s*AX=([0-9A-F]{4}) CF=(\d)\s*$")


def fail(msg):
    print("dosren: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, GATE):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=GATE) as ui:
        m = ui.m
        if not ui.path("B:/RENREF.COM"):
            fail("double-clicking RENREF.COM opened no window")
        rows = []
        end = time.time() + 180.0
        while time.time() < end:
            rows = m.screen() or []
            if any("RENREF READY" in r for r in rows):
                break
            time.sleep(0.3)
        else:
            fail("the program never finished; the last text screen was %r"
                 % ([r.rstrip() for r in rows if r.strip()][:14],))
        print("dosren: the bracket's text screen:")
        for r in rows[:14]:
            if r.strip():
                print("      | %s" % r.rstrip())
        got = {}
        order = []
        for r in rows:
            mm = ROW.match(r.rstrip())
            if mm:
                got[mm.group(1).strip()] = (mm.group(2), mm.group(3))
                order.append(mm.group(1).strip())

    for label, ax, cf in WANT:
        if label not in got:
            fail("no line for %r; the program printed %r" % (label, order))
        g_ax, g_cf = got[label]
        if g_cf != cf:
            fail("%r answered CF=%s and IBM DOS 3.30 answers CF=%s "
                 "(SPEC.md 96.31)" % (label, g_cf, cf))
        if ax is not None and g_ax != ax:
            fail("%r answered AX=%s and IBM DOS 3.30 answers AX=%s"
                 % (label, g_ax, ax))
        print("dosren: ok  - %-16s AX=%s CF=%s%s"
              % (label, g_ax, g_cf, "" if ax is not None
                 else "   (AX is junk on success, and DOS's is 0012)"))
    print("dosren: PASS")


if __name__ == "__main__":
    main()
