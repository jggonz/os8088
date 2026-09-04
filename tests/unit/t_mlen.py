#!/usr/bin/env python3
"""Twelve month lengths, read back out of the kernel this tree just built.

    python3 tests/unit/t_mlen.py

`clk_mlen` used to be a twelve-byte table anybody could check by eye:

    clk_mdays: db 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31

Kernel size pass 3 replaced it with a 16-bit mask - `mov bx, 0x15AA`, then
`shr bx, cl` and `and al, 1` - which is three bytes shorter and, as a review
artefact, is twelve facts collapsed into one hex constant that nobody can
verify by reading it.  This row turns it back into twelve numbers.

**IT READS THE BINARY, NOT THE SOURCE**, for `t_bsssentinel`'s and
`t_api_abi`'s reason: the question is what NASM actually placed, and a scan of
the source re-derives the intent instead.  `os88sym` asserts byte-identity
with `build/kernel.bin` before it answers, so the address and the bytes at it
describe one kernel.

**AND IT IS OWED, RATHER THAN OFFERED.**  `tests/dtfield.py` row 3 is the only
thing in the tree that exercises `clk_mlen` at all, and it is *"30 Jan + one
month lands on 28/29 Feb"* - February, which is the BRANCH below the mask and
the one arm the rewrite does not touch.  The eleven months the mask decides
were covered by nothing anywhere.  A wrong bit surfaces as "31 April is
accepted in the Date/Time page" and as a midnight rollover on the wrong day -
which no harness in this project can run long enough to see, and which on a
machine with no RTC is the only date it has.

WHAT A FAILURE MEANS.  The mask is `bit m set = month m has 31 days`, m =
1..12, so bit 0 is unused and bit 2 (February) is clear because February is
the branch.  If this row goes red after an edit to `clk_mlen`, the arithmetic
below says which month moved; check that against the table in the docstring
before touching anything else.  If it goes red because the OPCODE scan found
the wrong instruction, the routine's shape has changed and this file has to
follow it - the scan is deliberately narrow so that it fails loudly rather
than reading a plausible number out of the wrong immediate.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                            # noqa: E402
import os88layout                                          # noqa: E402
import os88sym                                             # noqa: E402

# The table clk_mdays held, and the answer this row exists to defend.
MONTHS = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]

WHY = ("clk_mlen carries the eleven non-February month lengths as a bit mask "
       "(SPEC.md 37). Nothing else in the tree reads them, and the only test "
       "that reaches the routine - tests/dtfield.py row 3 - exercises "
       "February, which is the branch and not the mask")

SCAN = 24               # bytes of the routine to look in: it is 34 long and
                        # all three constants are in its first dozen


def one(blob, off, opcode, what, size):
    """The single occurrence of `opcode` in the routine's first SCAN bytes."""
    hits = [i for i in range(SCAN) if blob[off + i] == opcode]
    if len(hits) != 1:
        check(False, "clk_mlen has exactly one %s" % what,
              "the scan below reads the wrong immediate otherwise, which is a "
              "plausible number and not an error",
              got="%d occurrences of opcode 0x%02X" % (len(hits), opcode),
              want="one")
        return None
    i = off + hits[0] + 1
    return blob[i] if size == 1 else blob[i] | (blob[i + 1] << 8)


def main():
    binp = os.path.join(ROOT, "build", "kernel.bin")
    if not os.path.exists(binp):
        print("t_mlen: no build/kernel.bin - `make` first")
        sys.exit(1)

    syms = os88sym.syms()
    sect = os88sym.sections()
    blob = open(binp, "rb").read()

    if not check("clk_mlen" in syms, "clk_mlen is a kernel symbol", WHY):
        done("t_mlen")
        return
    check(sect.get("clk_mlen") == ".text", "clk_mlen is in .text",
          "it is far-called from CTRL.DRV through cw_clk_mlen and near-called "
          "from clk_inc_sec's day carry, so it is resident by contract",
          got=sect.get("clk_mlen"), want=".text")

    off = os88layout.text_at(syms["clk_mlen"], ROOT)
    if not check(off + SCAN < len(blob), "clk_mlen is inside kernel.bin", WHY):
        done("t_mlen")
        return

    mask = one(blob, off, 0xBB, "`mov bx, imm16` (the mask)", 2)
    base = one(blob, off, 0x04, "`add al, imm8` (the not-31 length)", 1)
    feb = one(blob, off, 0xB0, "`mov al, imm8` (February)", 1)
    if mask is None or base is None or feb is None:
        done("t_mlen")
        return

    check(base == 30, "the mask's `not 31` answer is 30",
          "every month the mask leaves clear has 30 days; February is the "
          "branch and never reaches this add", got=base, want=30)
    check(feb == 28, "February's own answer is 28 before the leap test",
          "the `inc al` under `test byte [clk_year],3` is what makes it 29",
          got=feb, want=28)

    for m in range(1, 13):
        got = feb if m == 2 else base + ((mask >> m) & 1)
        check(got == MONTHS[m - 1],
              "month %2d is %d days" % (m, MONTHS[m - 1]), WHY,
              got=got, want=MONTHS[m - 1])

    check(not (mask & 1), "bit 0 of the mask is clear",
          "there is no month 0; a set bit there is a sign the mask was "
          "written 0-based, which shifts every month by one", got=mask & 1,
          want=0)
    check(not (mask & (1 << 2)), "bit 2 (February) is clear",
          "February is decided by the branch below, not by the mask - a set "
          "bit here is dead in one direction and misleading in the other",
          got=(mask >> 2) & 1, want=0)
    check(mask < (1 << 13), "no bit above 12 is set",
          "months run 1..12; a bit above that is unreachable and means the "
          "constant was derived some other way", got="0x%04X" % mask,
          want="< 0x2000")

    done("t_mlen")


if __name__ == "__main__":
    main()
