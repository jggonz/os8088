#!/usr/bin/env python3
"""A block grows back into what it gave up (SPEC.md 96.9.2).

`AH=4Ah` grows a block only into the block immediately above it - DOS's own
rule, implemented faithfully, and only half of DOS: DOS COALESCES adjacent
free blocks during the allocation walk and this box did not.

WHY THAT IS NOT A REFINEMENT.  Every DOS memory manager takes the largest
block there is and then shrinks and grows it as the program's own heap moves.
Each shrink cuts a NEW free tail, so after two of them the space the block
gave up is two or three adjacent free blocks - and a grow that can absorb only
the first of them refuses a block SMALLER than one it has already granted,
answering the previous high-water mark as the maximum.  The arena does not
leak, it fragments permanently, and it does so inside one program's startup.

Measured on Commander Keen 2 under kern_dos: granted 0x78C0 paragraphs
(483 KB), then refused 0x6900 (420 KB) with 221 KB free above the block in
three adjacent pieces.

ONE BINARY ON BOTH, dosregs's and dosvec's shape: tests/dostrap/mcb.asm runs
under this box and under a real IBM DOS 3.30 off a real floppy.

    IBM DOS 3.30            this box
    MAX=93AE                MAX=6D9E
    ask=93AE cf=0 bx=93AE   ask=6D9E cf=0 bx=6D9E
    ask=49D7 cf=0 bx=49D7   ask=36CF cf=0 bx=36CF
    ask=6EC2 cf=0 bx=6EC2   ask=5236 cf=0 bx=5236
    ask=49D7 cf=0 bx=49D7   ask=36CF cf=0 bx=36CF
    ask=8138 cf=0 bx=8138   ask=5FEA cf=0 bx=5FEA

**THE NUMBERS ARE NOT COMPARABLE AND THE SHAPE IS.**  `MAX` is the arena and
the two arenas differ by design, so every figure below it differs too.  What
has to hold on both machines is that all five steps succeed with `bx` equal to
what was asked - and the fifth asks for LESS than the second was granted, so a
`cf=1` there is the defect with nothing to interpret.  Before the fix this row
read `ask=5FEA cf=1 bx=5236`.
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
GATE = "build/dosmcb360.img"
NSTEP = 5
STEP = re.compile(r"ask=([0-9A-F]{4}) cf=([01]) bx=([0-9A-F]{4})")


def fail(msg):
    print("dosmcb: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, GATE):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=GATE) as ui:
        m = ui.m
        if not ui.path("B:/MCB.COM"):
            fail("double-clicking MCB.COM opened no window")

        rows = []

        def keyed(_m):
            rows[:] = m.screen() or []
            return any("KEY" in r for r in rows)
        try:
            os88marty.until(m, keyed, "the KEY prompt", poll=0.25, limit=90)
        except os88marty.MartyError:
            fail("the probe never reached its KEY prompt; the last screen was "
                 "%r" % ([r.rstrip() for r in rows if r.strip()][-10:],))

        text = "\n".join(r.rstrip() for r in rows)
        steps = STEP.findall(text)
        print("dosmcb: the probe said:")
        for r in [x.rstrip() for x in rows if x.strip()]:
            if r.startswith(("MCB", "MAX=", "ask=")):
                print("   | %s" % r)

        if len(steps) != NSTEP:
            fail("%d step(s) came back, not %d - the probe stopped part way: "
                 "%r" % (len(steps), NSTEP, steps))

        # THE SHAPE, not the numbers: the arenas differ by design.
        ask = [int(a, 16) for a, _, _ in steps]
        if not (ask[4] < ask[0] and ask[4] > ask[2] and ask[1] < ask[2]):
            fail("the five asks are not the shrink-grow shape this row is "
                 "about (%r) - tests/dostrap/mcb.asm's table has changed and "
                 "the assertion below no longer means anything" % (ask,))

        for i, (a, cf, bx) in enumerate(steps):
            if cf != "0":
                extra = ""
                if i == NSTEP - 1:
                    extra = (" - and it asks for LESS than step 2 was granted "
                             "(%s), so the free space it gave up has "
                             "FRAGMENTED: a grow absorbs one neighbour and "
                             "nothing coalesced the run (SPEC.md 96.9.2)"
                             % steps[0][0])
                fail("step %d asked for %s and was REFUSED, answering bx=%s%s"
                     % (i + 1, a, bx, extra))
            if bx != a:
                fail("step %d asked for %s and got bx=%s: a granted resize "
                     "answers the size it granted" % (i + 1, a, bx))

        print("dosmcb: %d/%d steps granted, the last (%s) smaller than the "
              "first grant (%s)" % (NSTEP, NSTEP, steps[4][0], steps[0][0]))

    print("dosmcb: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
