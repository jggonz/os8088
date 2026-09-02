#!/usr/bin/env python3
"""The directory cache's width is solved for, and the claim always covers it.

    python3 tests/unit/t_dirwsize.py

SPEC.md 18.95.5.  `dsk_rah_want` picks a width instead of taking 63KB or
nothing: `n = largest_free_run / 9`, capped at `DSK_RAH_RUNS`, floored at
`DSK_RAH_MIN`, and the claim is `ceil(n * 4.5)` KB.  Three numbers have to
agree for that to be safe and they are written in three different places -
the constants here, the divisor in the gate, and the shift-add that turns a
slot count into KB.

**THE FAILURE IS A WRITE PAST THE CLAIM.**  `dsk_rah_fill` addresses slot `s`
at `(s * DSK_RAH_SECS + delta) << 9` inside the claim, bounded by
`[dsk_rah_runs]`.  If the KB the claim was made with is ever less than
`runs * chunk`, the last slot's fill lands outside it - an `int 13h` writing
into whatever the heap handed out next, which is memory corruption with a
disk controller behind it and nothing anywhere to report it.  So the row that
matters is `kb * 1024 >= n * chunk`, checked at every width the machine can
pick rather than at the one it usually does.

The compile-time half of this lives in `kernel/disk.inc` as two `%error`s -
that a chunk is 4,608 bytes, and that the floor sits inside [1, RUNS].  This
file is the other half: it checks the CONSEQUENCES across the whole range,
which an assert on the constants cannot see.

It is a host-side row even though the widths themselves are measured, because
a sample is not a proof.  `os8088_5150_gla_192k` (tools/martypc/configs) has
76KB of heap and picks **7 slots, a 32,768-byte claim over 32,256 bytes of
slots**; the reference machine has 524KB and picks the ceiling.  Those are two
points on a curve with fourteen, and the row that matters - that the claim
covers the width - has to hold at all of them, including the ones no machine
here lands on.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

DISK = "kernel/disk.inc"


def main():
    with open(os.path.join(ROOT, DISK), errors="replace") as f:
        src = f.read()

    def const(name):
        m = re.search(r"^%s\s+equ\s+(\d+)" % name, src, re.M)
        return int(m.group(1)) if m else None

    secs, runs, floor = (const("DSK_RAH_SECS"), const("DSK_RAH_RUNS"),
                         const("DSK_RAH_MIN"))
    for n, v in (("DSK_RAH_SECS", secs), ("DSK_RAH_RUNS", runs),
                 ("DSK_RAH_MIN", floor)):
        check(v is not None, "%s is readable from %s" % (n, DISK),
              "Every row below is computed from these three", got=v,
              want="an integer")
    if None in (secs, runs, floor):
        return done("dirwsize")

    chunk = secs * 512
    check(chunk == 4608, "a chunk is 4,608 bytes",
          "dsk_rah_want's KB is (n*9+1)>>1 and its gate is n = free/9. Both "
          "are ceil(n * 4.5KB) and 2 x n x 4.5KB written as integers, so a "
          "different chunk size makes both silently wrong - in the direction "
          "that claims LESS than the width it then fills",
          got=chunk, want=4608)
    check(1 <= floor <= runs, "the floor sits inside [1, the ceiling]",
          "At 0 the gate would claim nothing and then scan it; above the "
          "ceiling it can never be satisfied and the cache is dead code",
          got=floor, want="1..%d" % runs)
    check(runs * secs <= 128,
          "the ceiling still fits one 16-bit offset",
          "dsk_rah_have derives a sector as (slot*SECS + delta) << 9 from "
          "dsk_rah_seg, so RUNS*SECS <= 128. Past it dsk_rah_take serves a "
          "sector from the FRONT of the cache and nothing reports it "
          "(SPEC.md 18.95.4)",
          got=runs * secs, want="<= 128")

    # the two formulas, as dsk_rah_want computes them
    kb_of = lambda n: (n * 9 + 1) >> 1          # noqa: E731  ceil(n * 4.5)
    bad = [n for n in range(floor, runs + 1) if kb_of(n) * 1024 < n * chunk]
    check(not bad, "every width's claim COVERS the slots it will be filled with",
          "dsk_rah_fill writes slot s at (s*SECS + delta) << 9 inside the "
          "claim, bounded by [dsk_rah_runs]. A claim shorter than "
          "runs * chunk puts the last fill outside it - an int 13h into "
          "whatever the heap handed out next",
          got="short at n = %s" % bad, want="kb*1024 >= n*%d at every n" % chunk)

    # the gate is the OLD rule with the size solved for: 2 x n x chunk <= free
    bad2 = [n for n in range(floor, runs + 1) if 2 * n * chunk > 9 * n * 1024]
    check(not bad2, "the 2x rule survives at every width",
          "SPEC.md 40.1: an optimisation must not be why something else "
          "fails, so the claim is only taken out of twice its own size. "
          "n = free/9 encodes that - 2 x n x 4.5KB <= free is 9n <= free - "
          "and it has to hold at the small widths too, not just at 14",
          got="violated at n = %s" % bad2, want="2*n*chunk <= 9n KB at every n")

    check(9 * runs == 126, "the ceiling still wants the bar it always did",
          "A 640KB machine must be unaffected by SPEC.md 18.95.5: at n = "
          "DSK_RAH_RUNS the gate has to come out at the same 126KB free run "
          "the fixed-size version demanded, or this changed behaviour where "
          "it promised not to",
          got="%dKB" % (9 * runs), want="126KB")

    # ...and the divisor in the source is the same 9, not a second opinion
    m = re.search(r"mov cx, (\d+)\s*;[^\n]*\n[^\n]*div cx", src)
    check(m is not None and int(m.group(1)) == 9,
          "the gate's divisor is the 9 these rows assume",
          "The rows above prove the arithmetic; this one proves the kernel "
          "does that arithmetic. A divisor and a chunk size that disagree "
          "give a width the claim does not cover",
          got=m.group(1) if m else "no `div cx` after a `mov cx, <n>`",
          want="9")

    return done("dirwsize")


if __name__ == "__main__":
    main()
