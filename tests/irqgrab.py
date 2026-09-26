#!/usr/bin/env python3
"""A DOS PROGRAM TAKES THE MOUSE'S IRQ, AND THE KERNEL TAKES IT BACK
(SPEC.md 9.13).

A program inside an fsx bracket owns the machine, and that includes the IVT:
it may hook IRQ3 or IRQ4 for its own serial code, write the vector directly
and chain to nobody. Battle Chess does exactly that, unconditionally, in
early start-up - and from that moment `mou_isr` is never called again, so
`[mouse_x]`/`[mouse_y]` freeze and INT 33h answers the same position for ever.
The report is a game whose cursor never moves (docs/FIELD-NOTES.md 56).

`IRQGRAB.COM` is that theft with nothing else in it: take `int 0Ch`, point it
at a handler that reads the LSR and the data register and EOIs, then BLOCK on
`AH=08h` - which is the window this needs, because the box's key poll is what
samples the mouse.

WHAT IT WOULD CATCH, and the first of these is what it was written against:

  - the kernel never re-arming            -> the pointer is FROZEN while the
    (the state before 9.13)                  vector is held: measured (0,0)
                                             against (400,120)
  - the re-arm taking the VECTOR and not  -> Battle Chess leaves MCR = 08h,
    the UART                                 which is a Microsoft mouse with
                                             its power removed, so the vector
                                             comes back and no byte follows it
  - `[mou_port]` shifted into the tables  -> row 2 indexes off the end of a
                                             two-port table; a machine whose
                                             mouse is on COM1 never notices,
                                             which is why this row reads the
                                             VECTOR and not only the position

KERN_BIG ONLY, and that is a fact about the disk rather than a judgement:
`DOS.O88` is on no kern_small floppy, so that machine has nothing that can
take an IRQ this way and 9.13 is compiled out of it entirely.

It runs on MartyPC and must: the pointer is moved by driving a real serial
mouse into a real 8088, and what is asserted is the kernel's own ISR being
called at all.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88marty                                               # noqa: E402
import os88mouserel                                            # noqa: E402
import os88sym                                                 # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088.img"
DISK = "build/irqgrab360.img"
KSEG = 0x0060


def fail(msg):
    print("irqgrab: FAIL: %s" % msg)
    sys.exit(1)


def word(m, lin):
    return int.from_bytes(m.read(lin, 2), "little")


def vec(m, n):
    return (word(m, n * 4 + 2), word(m, n * 4))


def wait_text(m, want, secs=120):
    """`want` on the guest's text screen, or say what was there."""
    try:
        os88marty.until(m, lambda _: any(want in r for r in
                                         [x.rstrip() for x in (m.screen() or [])]),
                        "the %r line" % want, guest=secs, poll=0.25)
    except os88marty.MartyError as e:
        fail("%r never appeared: %s  The screen read %r"
             % (want, str(e).split("\n")[0][:160],
                [r.rstrip() for r in (m.screen() or []) if r.strip()][-8:]))


def sweep(m, dx, dy, n=10, read=None):
    """Move the pointer with the guest RUNNING - `pace='wall'`, because
    `advance(frames=)` stops the emulator between packets and the box then
    never gets a slice to poll the mouse in. Then until `read` - the pointer
    and the vector - has held still for a guest second: the last packet is in
    and the box's key poll has sampled it."""
    r = os88mouserel.Rel(m, pace="wall")
    for _ in range(n):
        r.move(dx, dy)
    m.run()
    os88marty.quiesce(m, read, guest=0.5, stable=2,
                      what="the pointer to stop moving")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga_144")
    a = ap.parse_args(argv)

    mx, my = os88sym.linear("mouse_x"), os88sym.linear("mouse_y")
    with os88ui.boot(SYS, apps=DISK, machine=a.machine) as ui:
        m = ui.m
        if not ui.path("B:/IRQGRAB.COM"):
            fail("double-clicking IRQGRAB.COM opened no window")
        wait_text(m, "TOOK int 0Ch")

        before = (word(m, mx), word(m, my))
        pos = lambda: (word(m, mx), word(m, my), vec(m, 0x0C))
        sweep(m, 40, 12, read=pos)
        after = (word(m, mx), word(m, my))
        seg, off = vec(m, 0x0C)
        print("irqgrab: while the program holds it: int 0Ch -> %04X:%04X, "
              "the pointer went %s -> %s" % (seg, off, before, after))

        if seg != KSEG:
            fail("int 0Ch is %04X:%04X - the program still has it. The "
                 "kernel re-arms on osapi_mouse, which the box's DHK_MOUSE "
                 "calls on every key poll, and this program is blocked in "
                 "one (SPEC.md 9.13)" % (seg, off))
        if after == before:
            fail("the kernel's pointer did not move while a program held "
                 "int 0Ch: %s. The VECTOR came back but no byte followed it, "
                 "which is the UART half - a program that took the vector "
                 "also wrote MCR, and Battle Chess leaves it at 08h with DTR "
                 "and RTS off (SPEC.md 9.13)" % (before,))

        # ...and the other direction, which is what says the re-arm did not
        # merely nail the pointer to one corner.
        sweep(m, -40, -12, read=pos)
        back = (word(m, mx), word(m, my))
        print("irqgrab: ...and back: %s" % (back,))
        if back == after:
            fail("the pointer moved once and then stopped: %s -> %s -> %s"
                 % (before, after, back))

    print("irqgrab: the vector came back and the packets came with it")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
