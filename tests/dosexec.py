#!/usr/bin/env python3
"""The DOS EXEC gate (SPEC.md 96.14).

AH=4Bh loads another program and runs it, and control comes back to the
PARENT inside the INT 21h call that asked. It is what a shell is made of, and
what an installer, a launcher stub and a game's own front end use.

WHAT IT ASSERTS, and each is something only this arrangement can see:

  - a 4Bh BEFORE the parent shrinks itself answers 8, because the launched
    program was given the whole arena and there is no free block. That is
    DOS's own rule, not ours, and a shim that "helpfully" found memory
    anyway would be lying to every program that checks;
  - the child prints, so it was blocked, loaded, given a PSP and entered;
  - it prints its command tail out of its own PSP:0080, so the parameter
    block's FAR pointer arrived intact;
  - it reads PSP:0016 and finds a parent, which is the one PSP field only a
    child has;
  - and the PARENT IS STILL RUNNING afterwards, with the child's exit code
    readable through AH=4Dh. That last one is what a wrong stack restore
    destroys, and it fails as a hang or as the bracket ending rather than as
    a wrong number.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88ui                                                  # noqa: E402
import os88marty                                               # noqa: E402

SYS = "build/os8088-360.img"
EXE = "build/dosexec360.img"


def fail(msg):
    print("dosexec: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, EXE):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=EXE) as ui:
        m = ui.m
        if not ui.path("B:/DOSEXEC.COM"):
            fail("double-clicking DOSEXEC.COM opened no window")

        rows = []
        end = time.time() + 180.0
        while time.time() < end:
            rows = m.screen() or []
            if any("READY" in r for r in rows):
                break
            time.sleep(0.3)
        else:
            fail("the program never finished; the last text screen was %r"
                 % ([r.rstrip() for r in rows if r.strip()][:12],))

        text = "\n".join(r.rstrip() for r in rows)
        print("dosexec: the bracket's text screen:")
        for r in rows[:16]:
            if r.strip():
                print("   | %s" % r.rstrip())

        for line in (l.strip() for l in rows):
            if "FAILED" in line:
                fail("the program reported: %s" % line)

        for want, why in (
                ("REFUSED 8 before the shrink",
                 "a 4Bh with no free block must answer 8 (SPEC.md 96.14)"),
                ("CHILD speaking",
                 "the child never ran"),
                ("TAIL: HELLO",
                 "the command tail did not reach the child's PSP:0080"),
                ("PARENT yes",
                 "the child's PSP:0016 does not name its parent"),
                ("BACK in the parent",
                 "control did not return to the parent - which is what a "
                 "wrong stack restore destroys (SPEC.md 96.14.1)"),
                ("CHILD 7",
                 "AH=4Dh did not answer the child's exit code")):
            if want not in text:
                fail("%r is not on the screen - %s" % (want, why))

        print("dosexec: refused before the shrink, ran the child with its "
              "tail, came back, and read its code")

        m.type_text("x")
        os88marty.settle(m)
        if "Disk" not in ui.titles():
            fail("the desktop did not come back after the bracket")

        wd, ht, data = m.fbuf()
        os88marty.write_png_rgb("build/dosexec.png", wd, ht, data)
        print("dosexec: build/dosexec.png written")

    print("dosexec: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
