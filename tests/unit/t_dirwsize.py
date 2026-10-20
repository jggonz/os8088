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

    # ...and the THIRD of the three places, which was never read. This slot
    # held `9 * runs == 126` - a literal that was DSK_RAH_RUNS = 14's own bar,
    # written to guard SPEC.md 18.95.5 against changing what a 640KB machine
    # gets. The shipped width is a DECISION below the ceiling now (SPEC.md
    # 18.95.6), so that number moved and the rule did not: a row that pins a
    # decision goes red when somebody takes it, which is the opposite of what
    # a guard is for.
    #
    # What belongs here instead is the one number this file's own header names
    # and nothing checked: the MULTIPLY. `kb_of` above is a TRANSCRIPTION of
    # it, so every row that uses it is checking the kernel against a copy of
    # the kernel. dsk_rah_want computes KB as:
    #
    #     mov cx, 9 / ... / div cx / ... / mul cx / inc ax / shr ax, 1
    #
    # which is ceil(n * 9 / 2) - and the 9 has to be DSK_RAH_SECS, because
    # that is what makes it ceil(n * 4.5KB) for a 4,608-byte chunk. A factor
    # dropped here claims LESS than the width it then fills, which is this
    # row's headline failure with nothing above able to see it.
    #
    # **IT USED TO BE A SHIFT-ADD** - `mov bx,ax / shl ax,1 x3 / add ax,bx` -
    # and the size pass replaced it with `mul cx`, CX still holding the very 9
    # the divide two lines up was given. That is eight bytes off a resident
    # kernel AND a stronger invariant than this row could check before: the
    # multiplier is no longer a second spelling of the divisor that could
    # drift from it, it is the SAME REGISTER. So the check below is that the
    # KB block between `div cx` and the halve is exactly `mul cx / inc ax`,
    # with nothing in it that could write CX in between.
    want_body = re.search(r"^dsk_rah_want:(.*?)^osapi_dsk_cache_x:", src,
                          re.S | re.M)
    check(want_body is not None, "dsk_rah_want's body is findable in %s" % DISK,
          "Every check below reads it; without it they would scan the whole "
          "file and match somebody else's arithmetic", got=bool(want_body),
          want=True)
    want_src = want_body.group(1) if want_body else ""
    m = re.search(r"^\s*div cx\b(.*?)shr ax, 1", want_src, re.S | re.M)
    body = m.group(1) if m else ""
    muls = len(re.findall(r"^\s*mul cx\b", body, re.M))
    ups = len(re.findall(r"^\s*inc ax\b", body, re.M))
    clob = re.findall(r"^\s*(?:mov|add|sub|xor|and|or|inc|dec|pop|xchg|shl|shr)"
                      r"\s+(?:cx|cl|ch)\b", body, re.M)
    check(m is not None and muls == 1 and ups == 1 and not clob,
          "the KB block in the source IS ceil(n * <the divisor> / 2)",
          "kb_of above is a transcription of this, so without this check "
          "every width row is comparing the kernel with a copy of itself. "
          "The claim is sized here and filled from [dsk_rah_runs], so a "
          "multiplier one short claims less than it fills - an int 13h "
          "landing in whatever the heap handed out next. The multiply reuses "
          "the divide's own CX, so anything writing CX between them is the "
          "one way the two can disagree",
          got="%d x `mul cx`, %d x `inc ax`, CX written by %s"
              % (muls, ups, clob or "nothing"),
          want="1 x `mul cx`, 1 x `inc ax`, CX untouched between")

    # ...and that shared divisor/multiplier is the chunk length these rows
    # assume, not a second opinion
    m = re.search(r"mov cx, (\d+)\s*;[^\n]*\n[^\n]*div cx", want_src)
    got = int(m.group(1)) if m else None
    check(got == 9 and got == secs,
          "the gate's divisor is the DSK_RAH_SECS these rows assume",
          "The rows above prove the arithmetic; this one proves the kernel "
          "does that arithmetic. A divisor and a chunk size that disagree "
          "give a width the claim does not cover - and since the KB multiply "
          "now reuses this same CX, this one literal is BOTH halves",
          got=got if m else "no `div cx` after a `mov cx, <n>`",
          want="9, and == DSK_RAH_SECS (%d)" % secs)

    return done("dirwsize")


if __name__ == "__main__":
    main()
