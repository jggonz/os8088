#!/usr/bin/env python3
"""CP/M's CALL 5 reaches DOS and comes back (KERN-MEGA-DOS-PLAN 10.5 defect 3).

    python3 tests/call5.py [--machine NAME] [--leg window|whole|both]

A CP/M-style program puts the function in CL and does a NEAR `call 5`. PSP:5
is a far call, and the box aims it at `dos_cpm`, which dispatches on CL and
returns to the program's own near return. It used to be aimed at PSP:0050,
the DOS 2+ far gate, which takes AH and returns FAR to PSP:000A - a data
field - so CALL 5 had never served CP/M on any kernel.

CALL5.COM prints "C5" through function 2 twice, asks function 19h (current
drive) the same way and exits with ITS answer. Run from B: that is 1, which
only a call that dispatched on CL and came back to the program can produce.

TWO LEGS, because the far call reaches `dos_cpm` two different ways:

  window  the windowed box, a heap region above 64KB: the segment fits a
          word. The exit code is read off the box's own `[dos_exit]`.
  whole   the same program under kern_dos (the Memory page's Shut down the OS
          arm), which runs low in memory: the segment goes below 0 and the
          call reaches `dos_cpm` through the 1MB wrap, as IBM DOS 3.30's own
          CALL 5 does. The exit code is kern_dos's closing line.

kern_mega's HMA run is the third way, and is Bochs's (docs/BOCHS.md).

WHAT IT WOULD CATCH: PSP:5 aimed back at the PSP:0050 gate, which is the
pre-fix box - the program's first CALL 5 dispatches on whatever AH holds and
returns into PSP:000A, so neither leg ever sees an exit code of 1.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import dosmap                                              # noqa: E402
import kdhand                                              # noqa: E402
import os88marty as M                                      # noqa: E402
import os88ui                                              # noqa: E402

SYS = os.path.join(ROOT, "build", "os8088-360.img")
FLOPPY = os.path.join(ROOT, "build", "call5360.img")
MACHINE = "os8088_5150_cga_gla"


def fail(msg):
    print("call5: FAIL: %s" % msg)
    sys.exit(1)


def leg_window(machine):
    """The box opened bare, B: made current at its prompt, CALL5.COM run.

    NOT by association: a program launched from B: runs with A: current,
    and A:'s 0 is also what the pre-fix box exits with (AH is 0 at entry, so
    its first CALL 5 is a terminate). Only B: current makes 1 the answer
    nothing but a dispatched, returning CALL 5 produces."""
    with os88ui.boot(SYS, apps=FLOPPY, machine=machine) as ui:
        m = ui.m
        if not ui.path("A:/APPS/DOS.O88"):
            fail("could not open DOS.O88 off the system disk")
        M.settle(m)
        dm = dosmap.package()
        m.write((dosmap.instance(m) << 4) + dm["dos_exit"], bytes([0xEE]))
        m.type_text("B:\n")
        M.settle(m)
        m.type_text("CALL5.COM\n")
        code = [0xEE]

        def ran(mm):                # the region MOVES at the arena claim, so
            base = dosmap.instance(mm) << 4     # it is asked per read (dosmap)
            code[0] = mm.read(base + dm["dos_exit"], 1)[0]
            return code[0] != 0xEE
        try:                        # GUEST time: `limit` is idle-box seconds
            M.until(m, ran, "CALL5.COM to exit", poll=0.5, limit=120.0)
        except M.MartyError:
            fail("window: CALL5.COM never exited - a CALL 5 that does not "
                 "come back to the program")
        if code[0] != 1:
            fail("window: CALL5.COM exited with %d, not 1 (B:): function "
                 "19h through CALL 5 was not dispatched on CL" % code[0])
        print("call5: window - exit code %d, the current drive B:" % code[0])


def leg_whole(machine):
    with os88ui.boot(SYS, apps=FLOPPY, machine=machine) as ui:
        m = ui.m
        try:
            kdhand.launch_whole(ui, "CALL5.COM")
        except RuntimeError as e:
            fail("whole: %s" % e)
        rs = kdhand.wait_text(m, "exited", secs=150, what="CALL5 under kern_dos")
        line = next(r for r in rs if "exited" in r)
        print("call5: whole - %s" % line.strip())
        code = re.search(r"code\s+(\d+)", line)
        if not code or int(code.group(1)) != 1:
            fail("whole: %r is not exit code 001 (B:): function 19h through "
                 "CALL 5 was not dispatched on CL" % line.strip())
        if not any("C5" in r for r in rs):
            fail("whole: \"C5\" is not on the screen - function 2 through "
                 "CALL 5 printed nothing")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    ap.add_argument("--leg", default="both", choices=("window", "whole",
                                                      "both"))
    a = ap.parse_args()
    for p in (SYS, FLOPPY):
        if not os.path.exists(p):
            fail("%s is missing - `make build/call5360.img` builds the B: "
                 "floppy" % p)
    if a.leg in ("window", "both"):
        leg_window(a.machine)
    if a.leg in ("whole", "both"):
        leg_whole(a.machine)
    print("call5: ok - CALL 5 dispatches on CL and returns to the program")
    return 0


if __name__ == "__main__":
    sys.exit(main())
