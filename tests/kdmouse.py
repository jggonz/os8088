#!/usr/bin/env python3
"""INT 33h answers a LIVE pointer under kern_dos (SPEC.md 96.45).

    make kdostest && make dosmou360.img && python3 tests/kdmouse.py

`tests/dosmouse.py` is this question in the WINDOW, where the box fills
`DHK_MOUSE` from `OSAPI_MOUSE` and the kernel owns the hardware. This is the
same program on the other arm, where there is no kernel at all: the handoff's
step 6 calls `mouse_unhook` - it must, or a live IRQ4 vectors into kern_dos's
image at `mou_isr`'s KERNEL offset - so the pointer has to be brought back by
`kerndos/kdmouse.inc` off the port os8088 settled on before it handed over.

WHAT IT WOULD CATCH, and the first two are what it was RED with before the
feature existed:

  - `DHK_MOUSE` left zero          -> rc=FFFF, and x/y that never change. A
                                      driver IS reported present (96.10 says
                                      so whatever the hardware), so "no mouse"
                                      and "a mouse that cannot move" are the
                                      same picture without a MOVE in the test
  - the port never re-enabled      -> the same still pointer, because
                                      `mouse_unhook` wrote IER = 0 and masked
                                      the line on the way over
  - OUT2 not re-asserted           -> bytes arrive, the LSR says so, and no
                                      IRQ is ever raised. Invisible to a
                                      polled reader and fatal to this one
  - the line derived from the base -> a 2F8 card on IRQ4 (SPEC.md 9.5.2.1)
                                      hooks int 0Bh and never fires
  - the deltas decoded unsigned    -> the pointer only ever moves one way,
                                      which is what the two directions below
                                      are for
  - the axes crossed or DX eaten   -> y tracks x; the moves are deliberately
                                      unequal on the two axes so that cannot
                                      pass

**RELATIVE MOTION, AND THAT IS NOT A SHORTCUT** (CLAUDE.md, Testing). The
absolute driver confirms each move by reading the cursor back out of the
KERNEL's own cells, and here there is no kernel to read - `mouse_x` is not a
wrong address, it is not a symbol. `tools/os88mouserel.py` is the driver for
exactly this case: motion with no destination.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import kdhand                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88mouse                                               # noqa: E402
import os88mouserel                                            # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
MOU = "build/dosmou360.img"
MACH = "os8088_5150_cga_gla"


def fail(msg):
    print("kdmouse: FAIL: %s" % msg)
    sys.exit(1)


def wait_line(m, prefix, limit=60.0):
    end = time.time() + limit
    rows = []
    while time.time() < end:
        rows = m.screen() or []
        for r in rows:
            if r.lstrip().startswith(prefix):
                return r.strip()
        time.sleep(0.2)
    fail("no %r line appeared; the last text screen was %r"
         % (prefix, [r.rstrip() for r in rows if r.strip()][:10]))


def fields(line):
    out = {}
    for tok in line.split():
        if "=" in tok:
            k, _, v = tok.partition("=")
            out[k] = v
    return out


def pos(line, what):
    f = fields(line)
    try:
        return int(f["x"]), int(f["y"])
    except (KeyError, ValueError):
        fail("could not read a position out of %s's %r" % (what, line))


def main():
    for p in (SYS, MOU):
        if not os.path.exists(p):
            fail("%s is missing - `make kdostest` and `make dosmou360.img` "
                 "build the pair" % p)

    with os88ui.boot(SYS, apps=MOU, machine=MACH) as ui:
        m = ui.m

        # --- 1. hand the machine over, the way kdmix does -------------------
        # **THE BOX AND NOT THE PROGRAM**, which is a flow correction and not
        # a style one: a program launched by association is RUNNING in the
        # window, and the Memory page cannot be re-armed underneath one. Open
        # DOS.O88 itself, pick the arm, then name the program - which is also
        # the sequence a user takes to reach arm 3 at all.
        if not ui.path("A:/APPS/DOS.O88"):
            fail("could not open DOS.O88 off the system disk")
        os88marty.settle(m)
        # **CHANGE DRIVE FIRST, then name the program without one**, which is
        # tests/kdmix.py's sequence and is what a user does. This row used to
        # type a drive-QUALIFIED path into the box instead, and SPEC.md 96.48
        # is why that stopped working: a name may no longer drag the machine
        # to another volume, so `B:\DOSMOUSE.COM` typed while standing on A:
        # is a path the walk roots on A:. The fresh window has the box
        # focused; Enter on a bare drive letter changes drive and clears it.
        m.type_text("B:\n")
        os88marty.settle(m)
        dm = dosmap.package()
        pseg = dosmap.instance(m)
        base = pseg << 4
        mo = os88mouse.Mouse(marty=m)

        mo.click(*dosmap.centre(m, pseg, dm, "dos_erect"))
        os88marty.settle(m)
        dis = kdhand.rec(m, pseg, dm, kdhand.RD_DIS)
        if dis & (1 << kdhand.WHOLE):
            fail("the Shut down the OS arm is GREYED - this build does not carry "
                 "kern_dos as a part (SPEC.md 96.36.1)")
        x1, y1, x2, _ = dosmap.rect(m, pseg, dm, "dos_mrad")
        pitch = kdhand.rec(m, pseg, dm, kdhand.RD_PITCH)
        mo.click((x1 + x2) // 2, y1 + kdhand.WHOLE * pitch + pitch // 2)
        os88marty.settle(m)
        if kdhand.rec(m, pseg, dm, kdhand.RD_SEL) != kdhand.WHOLE:
            fail("clicking the Shut down the OS arm did not pick it")
        # --- ...back to the main page, and the program's name ---------------
        # The path box has to be CLICKED first and the extension is part of
        # the name - tests/kdmix.py carries both reasons, and both fail as a
        # message about the wrong subject.
        mo.click(*dosmap.centre(m, pseg, dm, "dos_trect"))
        os88marty.settle(m)
        mo.click(*dosmap.centre(m, pseg, dm, "dos_pln"))
        os88marty.settle(m)
        m.type_text("DOSMOUSE.COM")
        os88marty.settle(m)
        mo.click(*dosmap.centre(m, pseg, dm, "dos_rrect"))
        os88marty.settle(m)
        if kdhand.alert_up(m, base, dm):
            mo.click(*kdhand.alert_button(m, base, dm, 1))      # Proceed

        # --- 2. the driver is THERE ------------------------------------------
        line = wait_line(m, "RESET")
        f = fields(line)
        if f.get("ax", "").upper() != "FFFF":
            fail("INT 33h AX=0 answered ax=%s, not FFFF (SPEC.md 96.10)"
                 % f.get("ax"))
        if f.get("bx") != "2":
            fail("INT 33h AX=0 answered bx=%s, not 2 buttons" % f.get("bx"))
        print("kdmouse: under kern_dos, the driver reports %s" % line)

        # --- 3. ...AND IT MOVES, which is the whole row ----------------------
        # The two axes get DIFFERENT magnitudes so a y that is tracking x
        # cannot pass, and the second move goes back the other way so a delta
        # decoded unsigned cannot either.
        # **`pace="wall"` AND NOT THE DEFAULT**, which is the one line of this
        # row that is about the machine rather than the mouse: `Rel`'s frame
        # pacing STOPS the emulator between packets, and the program on the
        # other side of this is free-running - it polls INT 21h AH=0Bh between
        # INT 16h checks. Frame-stepped, the move lands and the keystroke
        # after it never does, which reads exactly like a pointer that did not
        # move. `os88mouserel.py`'s own docstring names this case.
        rel = os88mouserel.Rel(m, pace="wall")
        m.type_text("x")
        p0 = pos(wait_line(m, "POS1"), "POS1")
        print("kdmouse: POS1 %s" % (p0,))

        rel.move(120, 40)
        m.type_text("x")
        p1 = pos(wait_line(m, "POS2"), "POS2")
        print("kdmouse: POS2 %s after +120,+40" % (p1,))
        if p1[0] <= p0[0] or p1[1] <= p0[1]:
            fail("the pointer did not move DOWN-RIGHT: %s -> %s. A still "
                 "pointer here is DHK_MOUSE left zero, a port never "
                 "re-enabled after mouse_unhook, or OUT2 not re-asserted "
                 "(SPEC.md 96.45)" % (p0, p1))
        if (p1[0] - p0[0]) == (p1[1] - p0[1]):
            fail("x and y moved by the SAME amount (%d) against a 120/40 "
                 "input - the axes are crossed or the y decode lost DX"
                 % (p1[0] - p0[0]))

        # --- 4. the button edges, taken while the program is BLOCKED ---------
        rel.click()
        m.type_text("x")
        line = wait_line(m, "PRESS")
        if fields(line).get("n") != "1":
            fail("INT 33h AX=5 answered n=%s after exactly one click: the "
                 "counts accumulate on every state read precisely so a press "
                 "that happened while the program was blocked is still there "
                 "(SPEC.md 96.10.1) - and under kern_dos the read is our own "
                 "ISR's (96.45)" % fields(line).get("n"))
        print("kdmouse: the press edge survived the block -> %s" % line)
        line = wait_line(m, "REL")
        if fields(line).get("n") != "1":
            fail("INT 33h AX=6 answered n=%s after one click"
                 % fields(line).get("n"))
        print("kdmouse: ...and the release -> %s" % line)

    print("kdmouse: ok - INT 33h tracks the hand with no kernel on the machine")
    return 0


if __name__ == "__main__":
    sys.exit(main())
