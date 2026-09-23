#!/usr/bin/env python3
"""A write PAST the end of a file the same handle created.

    python3 tests/dosgap.py [--machine NAME]

SPEC.md 96.11.6.3, and the row exists because of what it cost: Microsoft
Works - the first program anyone ran on this box from outside the project -
could not save a document, and said `Cannot write file` about a floppy with
42 free clusters on it.

Works's Save As is one shape: CREATE the file, seek to 0x180 while it is
still EMPTY, write the body there, then seek back to 0 and lay the 384-byte
header it left room for.  A format with a header it can only fill in once the
body is written has no other shape to be.

`.fwrite`'s append-only guard was two `jne`s, which is not an ordering test at
all: it refused a write PAST the end in exactly the same breath as one BEHIND
it, and those are opposite cases.  Behind is 96.11.2's real refusal - this
layer cannot overwrite the middle of a file it is still accumulating.  Past is
a GAP, and `dos_fh_wiloop`'s `.ihole` already lays one.

WRGAP.COM runs under this box AND under a real DOS unchanged
(docs/DOS-DEBUGGING.md), so the answers it checks are the reference's.

WHAT IT WOULD CATCH: the seek past the end refusing (A); the write there
refusing, which is the defect itself (B); the header write going to the wrong
place or the accumulator being left in the in-place mode (C); the gap not
counted in the file's length (D, F); the bytes not surviving a close and
reopen, which is what `FHF_MADE` asserted at the wrong moment would do - a
flush appending to a file that does not exist yet (E); and a delete of a
missing name answering `access denied` where DOS says `file not found` (G).

THE GAP'S OWN CONTENT IS NOT ASSERTED and must not be: DOS leaves it
undefined, so a probe that checked it would fail against the reference for
being right.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88ui                                              # noqa: E402

SYS = os.path.join(ROOT, "build", "os8088-360.img")
FLOPPY = os.path.join(ROOT, "build", "wrgap360.img")
MACHINE = "os8088_5150_cga_gla"
PROG = "B:/WRGAP.COM"


def fail(msg):
    print("dosgap: FAIL: %s" % msg)
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
        end = time.time() + 240.0
        while time.time() < end:
            rows = [r.rstrip() for r in (m.screen() or []) if r.strip()]
            if any("WRGAP PASS" in r or "WRGAP FAIL" in r for r in rows):
                break
            time.sleep(0.5)
        else:
            fail("WRGAP.COM never reported; the last screen was %r"
                 % rows[:12])

        for r in rows:
            print("  | %s" % r)
        if any("WRGAP FAIL" in r for r in rows):
            fail("the probe reported FAIL - the lettered line above it says "
                 "which of A..G, and SPEC.md 96.11.6.3 / 96.11.9 say what "
                 "each one is about")
        print("dosgap: ok - a write past the end lays a gap, and a delete of "
              "nothing says 2")
    return 0


if __name__ == "__main__":
    sys.exit(main())
