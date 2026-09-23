#!/usr/bin/env python3
"""INT 33h's event handler is CALLED, and the pointer it reports is live.

    python3 tests/dosmouevt.py [--machine NAME]

SPEC.md 96.10.4, and the row exists because of what the histogram found:
Microsoft Works - the first program anyone ran on this box from outside the
project - "had a mouse" and had none.  96.10.3's INT 33h histogram says why in
one line: Works calls 00h, 08h, 0Ah and **0Ch SET EVENT HANDLER**, and then
nothing.  It never polls function 3.

So a box whose functions 3, 5, 6 and 0Bh are all exact and which answers 0Ch
with `not supported` has told a program a mouse exists and then never mentions
it again - which a program cannot tell from no mouse at all.

**THE ASSERTION IS NOT `THE POINTER MOVES`.**  Function 3 was correct through
the whole of that report.  It is that OUR CODE RUNS: a far handler installed
through 0Ch, called from the chained IRQ0 (96.10.4.1), with a position that
tracks the mouse the harness is moving.

MOUEVT.COM runs under this box AND under a real DOS unchanged
(docs/DOS-DEBUGGING.md).  Under a DOS with no mouse driver it prints SKIP and
stops, which is the honest answer: INT 33h not being installed is not this
box's defect.

WHAT IT WOULD CATCH: 0Ch falling back to `.none`, which is the defect itself
(B, then C); the IRQ0 hook not going in, or going in twice (C); a dispatcher
that never matches an event bit (C); and a handler that is still called after
the program asked for none, which is a callback into code that may have been
freed (E).
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
# RELATIVE, which is the short list CLAUDE.md names: this is motion with no
# destination. What the events are about is the MOVING, not the arriving, and
# an absolute driver that resolves one position and goes there emits the
# packets to get there and then stops.
import os88mouserel                                        # noqa: E402

SYS = os.path.join(ROOT, "build", "os8088-360.img")
FLOPPY = os.path.join(ROOT, "build", "mouevt360.img")
MACHINE = "os8088_5150_cga_gla"
PROG = "B:/MOUEVT.COM"


def fail(msg):
    print("dosmouevt: FAIL: %s" % msg)
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

        # Wait for step B's `ok` - the handler is in and the probe is now
        # counting.  Moving before that is moving at nothing.
        end = time.time() + 120.0
        while time.time() < end:
            rows = [r.rstrip() for r in (m.screen() or []) if r.strip()]
            if any("MOUEVT SKIP" in r for r in rows):
                fail("the probe found no INT 33h at all, which on THIS box is "
                     "the mouse vector not being installed (SPEC.md 96.10)")
            if any(r.startswith("B handler in") and "ok" in r for r in rows):
                break
            time.sleep(0.5)
        else:
            fail("the probe never got its handler in; the last screen was %r"
                 % rows[:12])

        # ...and now give it something to be called ABOUT. Relative, because
        # this is motion with no destination (CLAUDE.md, the two mouse
        # drivers): what the events are about is the moving, not the arriving.
        # **`pace="wall"` AND NOT THE DEFAULT**, which is the trap this row is
        # made of. `Rel`'s default paces by FRAMES - `m.advance(frames=2)`
        # between packets - and `advance` leaves the emulator PAUSED. For a
        # test that reads a result out of the guest afterwards that is
        # harmless; for one whose whole subject is what happens WHILE the
        # program runs, it is fatal and it does not look like a harness
        # mistake: the guest stops, the BIOS tick at 0040:006C freezes, and
        # every symptom reads as `moving the mouse hangs the machine`. That
        # cost four A/B builds here - the callback removed, the host read
        # removed, the IRQ0 hook removed - each one keeping the symptom,
        # which is exactly what should have named the cause sooner. `wall`
        # sleeps instead of advancing, so the machine never stops.
        mo = os88mouserel.Rel(m, pace="wall")
        for _ in range(6):
            mo.move(40, 24)
            mo.move(-40, -24)
        mo.packet(l=True)                       # a press...
        mo.packet()                             # ...and its release
        m.run()                                 # belt and braces: whatever
                                                # paced it, it runs from here

        end = time.time() + 180.0
        while time.time() < end:
            rows = [r.rstrip() for r in (m.screen() or []) if r.strip()]
            if any("MOUEVT PASS" in r or "MOUEVT FAIL" in r for r in rows):
                break
            time.sleep(0.5)
        else:
            fail("MOUEVT.COM never reported; the last screen was %r"
                 % rows[:12])

        for r in rows:
            print("  | %s" % r)
        if any("MOUEVT FAIL" in r for r in rows):
            fail("the probe reported FAIL - the lettered line above it says "
                 "which of A..E, and SPEC.md 96.10.4 says what each is about")
        line = [r for r in rows if r.startswith("events ")]
        if not line:
            fail("PASS with no count line, which the probe always prints")
        print("dosmouevt: ok - %s" % line[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
