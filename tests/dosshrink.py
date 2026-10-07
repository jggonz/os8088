#!/usr/bin/env python3
"""A DOS call must not scribble on the block the program gave back.

    make doscom && python3 tests/dosshrink.py [--machine NAME]

SPEC.md 96.7.2.  `AH=4Ah` with `BX = SS + 2 - PSP` is the shrink idiom every
launcher and every C runtime start-up uses, and the free MCB `dos_mcb_split`
then cuts sits at the paragraph PAST the block - which for a program whose SP
is a paragraph or two above SS is the sixteen bytes DIRECTLY BELOW its own
stack pointer.

A real DOS switches to an internal stack at its first instruction, so all that
ever lands there is the three words the `int` pushed, at +0A..+0F, where
nothing reads them.  This box built its whole INT 21h frame on the program's
stack instead - six words deeper, onto the signature, the owner and the size.

WHAT IT WOULD CATCH, and the first is what it was RED with:

  - the gate's frame back on the program's stack -> `MCB SMASHED`, and the
    AH=4Bh below refused with 8.  VERIFIED RED at the commit before the fix,
    and the whole causal chain is on one screen:

        A 5A 0000 6C1D        the header as dos_mcb_split left it
        B 5A 0000 001D        the SIZE's high byte zeroed by a push
        MCB SMASHED
        48h FFFF -> 0008 001D 464 bytes is now all the allocator can see
        exec: REFUSED ax=0008 ...which is the field report

    One byte of one word, and it takes 434 KB of free memory down to less
    than the `cmp bx, 64` floor `dos_exec_load` refuses under
  - `dos_be_go` swapping a SECOND time            -> it would swap UPWARDS
    through the frame the gate just built at `[dos_dstk]`, so the damage
    lands on the box rather than the program: a hang or a wild jump
  - `[dos_dstk]` not lowered for a child          -> the child's INT 21h
    writes over the parent's live AH=4Bh frame, and the EXEC never returns

**THE ASSERTION IS THE HEADER AND NOT THE EXEC.**  A failed EXEC is the
symptom the field reported, but it is two steps away from the cause and a
box with more free memory could pass it while still corrupting the chain.
SHRINK.COM snapshots the header before any other call and again after five,
and prints both - so a regression names the bytes.

SHRINK.COM runs under os8088 AND under a real DOS unchanged
(docs/DOS-DEBUGGING.md), which is where `MCB INTACT` comes from.
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
FLOPPY = os.path.join(ROOT, "build", "dosshrink360.img")
MACHINE = "os8088_5150_cga_gla"
PROG = "B:/SHRINK.COM"


def fail(msg):
    print("dosshrink: FAIL: %s" % msg)
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

        text = "\n".join(r.rstrip() for r in rows)
        print("dosshrink: the bracket's text screen:")
        for r in rows[:16]:
            if r.strip():
                print("   | %s" % r.rstrip())

        if "MCB SMASHED" in text:
            fail("the free MCB directly below the program's SP was rewritten "
                 "between two reads with nothing but five AH=30h calls "
                 "in between - the INT 21h gate is building its frame on the "
                 "program's stack again (SPEC.md 96.7.2)")
        if "MCB INTACT" not in text:
            fail("the probe printed neither verdict - it did not get as far "
                 "as comparing the two snapshots")
        if "4Ah cf=0000" not in text:
            fail("the shrink itself was refused, so the rest of the row "
                 "tested nothing")
        if "REFUSED" in text:
            fail("the header survived and AH=4Bh still refused: %s"
                 % next(l.strip() for l in rows if "REFUSED" in l))
        if "CHILD ran" not in text:
            fail("the child never printed, so AH=4Bh did not run it")

        print("dosshrink: the header below SP survived five DOS calls, and "
              "the EXEC it protects ran the child")


if __name__ == "__main__":
    main()
