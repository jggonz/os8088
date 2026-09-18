#!/usr/bin/env python3
"""The vectors a real DOS OWNS are installed, not left at 0000:0000 (SPEC.md 96.5.2).

EVERY EXPECTED LINE BELOW IS A MEASUREMENT OF IBM DOS 3.30, taken with the
same binary - `tests/dostrap/vecs.asm` - booted off a real floppy under
MartyPC.  It is written down rather than derived, so it cannot drift with the
disk, the geometry or the build.

WHY THIS ROW EXISTS.  `dos_hook_vectors` installed seven vectors and left the
rest of DOS's own block at 0000:0000, and that is not "unimplemented": an
`int` through a null vector EXECUTES THE VECTOR TABLE.  There is no way for a
program to test for it beforehand, because the probe IS the call - BOLOBALL
asks `int 2Ah AH=00h` three instructions after a version check we answer
correctly, and ran off into the IVT with SP walking down two bytes a lap
(SPEC.md 96.5.2.1).  Nothing in our own INT 21h trace looks wrong at any point.

WHAT EACH LINE CATCHES, and each of them was seen failing:

  NUL=NONE        a vector of DOS's own block back at 0000:0000.  Delete one
                  store from dos_hook_vectors and this names it.
  2A=00           the probe SURVIVING `int 2Ah`, and the handler being an
                  `iret` rather than something that scribbles on AH.
  29=[*]          `int 29h` - fast console output, which DOS's own CON driver
                  writes through.  An `iret` there is SILENCE and not a crash,
                  which is the harder bug: the line would read `29=[]`.
  SPD=0000        INT 25h/26h are the only calls of the era that do not
                  `iret` - DOS leaves the FLAGS the INT pushed ON THE STACK
                  for the caller to pop.  A handler that irets here answers
                  correctly and unbalances the caller by two bytes, which
                  faults somewhere else entirely; the probe's `add sp,2` then
                  reads SPD=FFFE (SPEC.md 96.5.2.2).
  exit code 0     `INT 21h AH=00h` exits with ZERO, not AL.  The probe leaves
                  with AL=42h, so a box that reports AL prints 066 - which is
                  the field's `Exit code 002` in a different hat (96.5.2.3).

CF AND AX ON THE `25` LINE ARE NOT COMPARED and the probe does not judge them:
IBM DOS reads sector 0 of drive A and succeeds (CF0 AX=0100), this box refuses
(CF1 AX=0C01).  That is the designed difference.  What has to agree is SPD.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402
import dosmap                                                  # noqa: E402

SYS = "build/os8088-360.img"
GATE = "build/dosvec360.img"

# IBM DOS 3.30, measured.  The `25` line is split because two of its three
# fields are the designed difference and the third is the assertion.
WANT = {
    "NUL=": "NONE",
    "2A=": "00",
    "29=[": "*]",
}
WANT_SPD = "0000"
EXIT_AL = 0x42          # what the probe leaves in AL; the CODE must be 0


def fail(msg):
    print("dosvec: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, GATE):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=GATE) as ui:
        m = ui.m
        if not ui.path("B:/VECS.COM"):
            fail("double-clicking VECS.COM opened no window")

        # 45s, and the ceiling matters: a NULL vector makes the probe run off
        # into the IVT rather than print a wrong answer, so THE COMMONEST
        # FAILURE HERE IS A HANG - and a wait long enough to hit the runner's
        # own wall clock reports `TIMEOUT` instead of naming the vector.
        rows = []
        end = time.time() + 45.0
        while time.time() < end:
            rows = m.screen() or []
            if any("KEY" in r for r in rows):
                break
            time.sleep(0.25)
        else:
            got = [r.rstrip() for r in (m.screen() or []) if r.strip()]
            nul = next((r for r in got if r.startswith("NUL=")), None)
            if nul and nul != "NUL=NONE":
                fail("the probe stopped after %r - vector(s) %s are "
                     "0000:0000 and an `int` through one of them executes "
                     "the vector table, which is exactly what it just did "
                     "(SPEC.md 96.5.2)" % (nul, nul[4:].strip()))
            fail("the probe never reached its KEY prompt; the last screen was "
                 "%r" % (got[-10:],))

        text = [r.rstrip() for r in rows if r.strip()]
        print("dosvec: the probe said:")
        for r in text:
            if r.startswith(("VECS", "NUL=", "2A=", "29=", "25=", "KEY")):
                print("   | %s" % r)

        joined = "\n".join(text)
        for key, want in WANT.items():
            line = next((r for r in text if r.startswith(key)), None)
            if line is None:
                fail("no %r line at all: %r" % (key, text[-8:]))
            got = line[len(key):]
            if got != want:
                if key == "NUL=":
                    fail("vector(s) %s are still 0000:0000 - an `int` through "
                         "one of them executes the vector table "
                         "(SPEC.md 96.5.2)" % got)
                if key == "29=[":
                    fail("`int 29h` printed %r where IBM DOS 3.30 prints %r - "
                         "fast console output is silent, which is the harder "
                         "bug (SPEC.md 96.5.2.2)" % (got, want))
                fail("%s%s where IBM DOS 3.30 says %s%s" % (key, got, key, want))

        line = next((r for r in text if r.startswith("25=")), None)
        if line is None:
            fail("no `25=` line: INT 25h did not come back at all")
        mo = re.search(r"SPD=([0-9A-F]{4})", line)
        if not mo:
            fail("the `25=` line has no SPD field: %r" % line)
        if mo.group(1) != WANT_SPD:
            fail("INT 25h left the stack %s off (SPD=%s, IBM DOS 3.30 says "
                 "%s): DOS leaves the FLAGS the INT pushed ON THE STACK, so "
                 "the handler returns with `retf` and not `iret` "
                 "(SPEC.md 96.5.2.2)" % (
                     "two bytes" if mo.group(1) == "FFFE" else "some way",
                     mo.group(1), WANT_SPD))
        print("dosvec: INT 25h's stack balances (SPD=%s) and its refusal is "
              "%s" % (mo.group(1), line[3:].split(" SPD")[0]))

        # --- and out through AH=00h, whose exit code is ZERO -----------------
        m.type_text("x")
        os88marty.settle(m)
        titles = ui.titles()
        if "DOS" not in titles:
            fail("the DOS window is gone after the program exited: %r" % titles)
        dm = dosmap.package()
        seg = dosmap.instance(m)
        code = m.read((seg << 4) + dm["dos_exit"], 1)[0]
        if code != 0:
            fail("the box recorded exit code %d where DOS's AH=00h always "
                 "exits 0 - the probe left AL=%02Xh and that is what came "
                 "back (SPEC.md 96.5.2.3)" % (code, EXIT_AL))
        print("dosvec: AH=00h with AL=%02Xh exited %d, as DOS does"
              % (EXIT_AL, code))

    print("dosvec: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
