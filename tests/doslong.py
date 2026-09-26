#!/usr/bin/env python3
"""DOS truncates a name that is not 8.3; refusing one is a file a program
cannot reach.

    make doscom && python3 tests/doslong.py [--machine NAME]

SPEC.md 96.12.5.  `dos_fh_core` counted the thirteen bytes of its parse
buffer and answered 3, "path not found", for anything longer.  DOS has no such
bound: its parser fills an eleven-byte FCB-shaped field and DISCARDS the rest,
so `plysample.bin` IS `PLYSAMPL.BIN`.

The Playroom is the report - `PLAYEGA.EXE` opens `B:plysample.bin` and prints
`FILE ERROR` / `Abnormal program termination` when refused, which from outside
looks like a program that could not start.

**THE EXPECTATION IS MEASURED AND NOT REASONED.**  LONGNAME.COM runs under a
real IBM DOS 3.30 unchanged (docs/DOS-DEBUGGING.md, `os88dosdbg.py ref`), and
there all five spellings come back with the SAME HANDLE:

    open PLYSAMPL.BIN OK h=0005        open PLYSAMPL.BINARY OK h=0005
    open plysample.bin OK h=0005       open B:plysample.bin OK h=0005
    open plysamplelong.bin OK h=0005

WHAT IT WOULD CATCH: the length bound coming back (all four long rows go
`CF ax=0003` at once, which is what it was red with); the stem ceiling off by
one (`plysample.bin` opens and `plysamplelong.bin` does not, or the other way
round); the extension ceiling not applied (`PLYSAMPL.BINARY` alone fails); and
the drive prefix no longer coming off before the count (`B:plysample.bin`
alone fails, which is the row that would otherwise pass for the wrong reason).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = os.path.join(ROOT, "build", "os8088-360.img")
FLOPPY = os.path.join(ROOT, "build", "doslong360.img")
MACHINE = "os8088_5150_cga_gla"
PROG = "B:/LONGNAME.COM"

# Every row IBM DOS 3.30 answers with a handle, measured.
WANT = ("PLYSAMPL.BIN", "plysample.bin", "plysamplelong.bin",
        "PLYSAMPL.BINARY", "B:plysample.bin")


def fail(msg):
    print("doslong: FAIL: %s" % msg)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    a = ap.parse_args()

    for p in (SYS, FLOPPY):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=FLOPPY, machine=a.machine) as ui:
        m = ui.m
        if not ui.path(PROG):
            fail("double-clicking %s opened no window" % PROG)

        rows = []

        def done(mm):
            rows[:] = mm.screen() or []
            return any("KEY" in r for r in rows)
        try:            # a GUEST budget: a loaded box cannot shorten it
            os88marty.until(m, done, "the program's closing KEY line", poll=0.3, limit=180.0)
        except os88marty.MartyError:
            fail("the program never finished; the last text screen was %r"
                 % ([r.rstrip() for r in rows if r.strip()][:12],))

        lines = [r.rstrip() for r in rows if r.strip()]
        print("doslong: the bracket's text screen:")
        for r in lines[:16]:
            print("   | %s" % r)

        for name in WANT:
            hit = [l for l in lines if l.startswith("open " + name + " ")]
            if not hit:
                fail("no line for %r at all - the probe did not run every row"
                     % name)
            if "OK h=" not in hit[0]:
                fail("%r was REFUSED (%s), and IBM DOS 3.30 hands back a "
                     "handle for it - SPEC.md 96.12.5's table"
                     % (name, hit[0].split(None, 2)[-1]))

        print("doslong: all five spellings reach the one file, which is what "
              "a real DOS 3.30 answers")


if __name__ == "__main__":
    main()
