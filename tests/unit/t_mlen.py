#!/usr/bin/env python3
"""Twelve month lengths, read back out of the kernel this tree just built.

    python3 tests/unit/t_mlen.py

`clk_mlen` used to be a twelve-byte table anybody could check by eye:

    clk_mdays: db 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31

Kernel size pass 3 replaced it with a 16-bit mask, and kernel size pass 17
replaced the mask with a PARITY - `cmp al, 8 / adc al, 0 / and al, 1 /
xor al, 31`: a month has 31 days when it is odd below August and even from
August on - which is twelve facts collapsed into two immediates that nobody
can verify by reading them.  This row turns them back into twelve numbers.

**IT READS THE BINARY, NOT THE SOURCE**, for `t_bsssentinel`'s and
`t_api_abi`'s reason: the question is what NASM actually placed, and a scan of
the source re-derives the intent instead.  `os88sym` asserts byte-identity
with `build/kernel.bin` before it answers, so the address and the bytes at it
describe one kernel.

**AND IT IS OWED, RATHER THAN OFFERED.**  `tests/dtfield.py` row 3 is the only
thing in the tree that exercises `clk_mlen` at all, and it is *"30 Jan + one
month lands on 28/29 Feb"* - February, which is the BRANCH beside the parity
and the one arm neither rewrite touched.  The eleven months the parity decides
were covered by nothing anywhere.  A wrong bit surfaces as "31 April is
accepted in the Date/Time page" and as a midnight rollover on the wrong day -
which no harness in this project can run long enough to see, and which on a
machine with no RTC is the only date it has.

WHAT A FAILURE MEANS.  The split is the compare's immediate (8: the first
month whose 31-day test is EVEN) and the 31 is the XOR's; February is the
branch.  If this row goes red after an edit to `clk_mlen`, the arithmetic
below says which month moved; check that against the table in the docstring
before touching anything else.  If it goes red because the OPCODE scan found
the wrong instruction, the routine's shape has changed and this file has to
follow it - every immediate is read at a FIXED offset whose opcode is checked
first, because a scan for an opcode byte can find one inside the address of
[clk_year] and read a plausible number out of it.
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

WHY = ("clk_mlen carries the eleven non-February month lengths as a parity "
       "(SPEC.md 37). Nothing else in the tree reads them, and the only test "
       "that reaches the routine - tests/dtfield.py row 3 - exercises "
       "February, which is the branch and not the mask")

SCAN = 25               # the routine's length


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

    # The routine's shape, byte by byte: (offset, opcode, what). Each
    # immediate is the byte after its opcode, and the opcode is checked first.
    SHAPE = [(0, 0x3C, "`cmp al, 2` (February's branch)"),
             (9, 0xB0, "`mov al, 28` (February)"),
             (16, 0x3C, "`cmp al, imm8` (the parity's split)"),
             (18, 0x14, "`adc al, 0` (the borrow)"),
             (20, 0x24, "`and al, 1` (the low bit)"),
             (22, 0x34, "`xor al, imm8` (the 31)")]
    imm = []
    for at, op, what in SHAPE:
        if not check(blob[off + at] == op, "clk_mlen +%d is %s" % (at, what),
                     "the routine's shape changed: follow it here",
                     got="0x%02X" % blob[off + at], want="0x%02X" % op):
            done("t_mlen")
            return
        imm.append(blob[off + at + 1])
    first, feb, split, adc, low, base = imm

    check(first == 2, "the branch is February's", WHY, got=first, want=2)
    check(adc == 0 and low == 1, "the parity is `adc al, 0 / and al, 1`",
          "anything else is not the parity this row models",
          got=(adc, low), want=(0, 1))
    check(feb == 28, "February's own answer is 28 before the leap test",
          "the `inc al` under `test byte [clk_year],3` is what makes it 29",
          got=feb, want=28)

    for m in range(1, 13):
        got = feb if m == 2 else base ^ (((m + (m < split)) & 0xFF) & low)
        check(got == MONTHS[m - 1],
              "month %2d is %d days" % (m, MONTHS[m - 1]), WHY,
              got=got, want=MONTHS[m - 1])

    done("t_mlen")


if __name__ == "__main__":
    main()
