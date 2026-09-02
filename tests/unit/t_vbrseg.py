#!/usr/bin/env python3
"""The volume boot record points at THIS kernel's heap, not another one's.

    python3 tests/unit/t_vbrseg.py

`boot/boothd.asm` is 512 bytes that the installer writes to the hard disk's
partition, and two of the numbers in it are not its own (its header says so):
**`BLOB_SEG`**, where stage 2's blob is loaded, and **`SPL_FSEG`**, the offset
of the kernel word it writes that address into.  Both are injected by the
Makefile, and both are computed by re-running host tools over `kernel.asm`.

**That is a second opinion about the memory ladder, and SPEC.md 52.10.2.1 is
what happens when it disagrees with the first.**  `tools/kernsize.py --json`
re-assembles the kernel to work out where the heap starts; it was doing that
with the DEFAULT defines while the kernel in `build/` had been built with a
knob, so a `BOOTPROF=1` machine's volume boot record loaded the blob three
rungs below where the kernel expected it and the machine executed wild.  The
floppy path could not show it - `boot/boot.asm` learns the address from the
kernel it just loaded - so this is a hard-disk-only defect in a file that
nothing assembles a second copy of.

**It is silent in the worst way**: every configuration builds, the sector is
512 bytes with a `55 AA` on the end, and the failure is a machine that boots
to a black screen or a hang somewhere unrelated.

So the check is the whole of the contract, read back out of the assembled
sector: the two `BLOB_SEG` immediates and the one `SPL_FSEG` displacement have
to be the values the kernel's OWN map gives for the configuration in `build/`.
It costs one assembly, and it fails on a `build/` where the kernel and the
volume boot record were built from different define sets - which includes the
ordinary trap of building a knob kernel and not rebuilding.

WHAT IT DOES NOT COVER: one `build/` at a time.  Per-configuration coverage is
`t_buildmatrix.py`'s to grow, and the END-TO-END version - install a knob
kernel and boot it off the disk on both adapters - is `tests/knobhd.py`.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                            # noqa: E402
import os88sym                                             # noqa: E402

VBR = os.path.join(ROOT, "build", "boothd.bin")


def main():
    d = open(VBR, "rb").read()
    check(len(d) == 512, "build/boothd.bin is one sector",
          "the volume boot record is written to LBA 0 of the partition and "
          "the BIOS reads exactly 512 bytes of it")
    check(d[510:512] == b"\x55\xaa", "...with a boot signature",
          "no signature, no boot - and the installer writes it either way")

    # CHECKED against build/kernel.bin, deliberately: a `build/` holding a
    # knob kernel while $OS88_DEFINES is unset is a different complaint from
    # a VBR that disagrees, and os88sym says which in one sentence. Reading
    # the map unchecked would report it as this row's failure instead.
    heap = os88sym.equates()["HEAP_SEG"]
    off = os88sym.syms()["spl_fseg"]

    # boothd.asm loads the blob address twice (a `mov reg, imm16` each) and
    # stores it once through ES with a 16-bit displacement.  Reading the
    # ENCODINGS back is what makes this a check of the shipped sector rather
    # than of the Makefile's arithmetic, which is the half that was wrong.
    want = [
        ("BLOB_SEG as `mov dx, imm`", bytes([0xBA]) + heap.to_bytes(2, "little")),
        ("BLOB_SEG as `mov ax, imm`", bytes([0xB8]) + heap.to_bytes(2, "little")),
        ("SPL_FSEG as `mov [es:disp], ax`",
         bytes([0x26, 0xA3]) + off.to_bytes(2, "little")),
    ]
    for what, pat in want:
        check(pat in d, "the VBR carries %s = %04X"
                        % (what, int.from_bytes(pat[-2:], "little")),
              "SPEC.md 52.10.2.1: build/kernel.bin's map says HEAP_SEG %04X "
              "and spl_fseg %04X, and the volume boot record in build/ does "
              "not agree. The two were built from different define sets - "
              "`make` after changing a knob, and check the boothd.bin recipe "
              "still exports OS88_DEFINES and passes $(VIDDEF) to kernsize"
              % (heap, off))

    done("vbrseg")


main()
