#!/usr/bin/env python3
"""SPEC.md 18.93.1 - the boot sector's canary must sit in a sector that CROSSES
A HEAD in every shipped geometry.

This is the row that exists because the first version of the canary did not.
Offset 16384 is "past the first head flip" in all three geometries and is still
in the FIRST half of a transfer run in all three - the half that loads correctly
on exactly the machine the canary exists to catch. It passed there, and the
kernel it let through was scrambled.

A run transfers correctly up to the head boundary and only goes wrong after it.
So the test is not "past a flip" but "in a run's second half", re-derived here
from each image's own BPB the way boot.asm derives it at run time.

KSIG_OFF IS A MEMORY OFFSET AND THIS IS A QUESTION ABOUT A SECTOR.  Since
SPEC.md 2.9 put stage 2 in front of the image, the two are BOOT2_SECS apart:
the Makefile's KSIGDEF2 reads the signature word out of the file at
KSIG_OFF + BOOT2_SECS*512, so that is the sector the probe actually lands in
and the one this row has to ask about.  Asking about KSIG_OFF/512 instead
validated a sector nothing reads - which is the same failure the file opens
with, one indirection further along.

boot/boot2.asm types KSIG_OFF out a second time, and there is no linker here,
so the two are checked against each other before anything else is asked.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build"
IMAGES = ["os8088-360.img", "os8088-720.img", "os8088.img"]


def ksig_off():
    m = re.search(r"^KSIG_OFF\s*:=\s*(\d+)", (ROOT / "Makefile").read_text(), re.M)
    if not m:
        sys.exit("t_canary: no KSIG_OFF in the Makefile")
    off = int(m.group(1))
    b2 = re.search(r"^KSIG_OFF\s+equ\s+(\d+)",
                   (ROOT / "boot" / "boot2.asm").read_text(), re.M)
    if not b2:
        sys.exit("t_canary: no KSIG_OFF equ in boot/boot2.asm")
    if int(b2.group(1)) != off:
        sys.exit(f"t_canary: the Makefile says KSIG_OFF {off} and "
                 f"boot/boot2.asm says {b2.group(1)} - the build would inject "
                 f"the word from one offset and the sector compare the other")
    return off


def boot2_secs():
    """Stage 2's length, which is how far the image sits into the file."""
    m = re.search(r"^BOOT2_SECS\s+equ\s+(\d+)",
                  (ROOT / "kernel" / "kernel.asm").read_text(), re.M)
    if not m:
        sys.exit("t_canary: no BOOT2_SECS in kernel/kernel.asm")
    return int(m.group(1))


def crosses(img, off):
    """Does file offset `off` land in a sector a transfer run reads AFTER the
    head boundary?  Mirrors boot.asm read_run's partitioning exactly."""
    d = img.read_bytes()[:512]
    rsvd = struct.unpack_from("<H", d, 14)[0]
    nfat, fatsz = d[16], struct.unpack_from("<H", d, 22)[0]
    rootent = struct.unpack_from("<H", d, 17)[0]
    spt, heads = struct.unpack_from("<H", d, 24)[0], struct.unpack_from("<H", d, 26)[0]
    data = rsvd + nfat * fatsz + (rootent + 15) // 16
    run = spt * heads
    want = data + off // 512                      # the LBA the canary lives at
    lba = data
    while lba <= want:
        s0 = lba % run
        span = run - s0
        if lba + span > want:
            return s0 < spt and (s0 + (want - lba)) >= spt
        lba += span
    return False


def main():
    off = ksig_off()                     # the MEMORY offset, from KERNEL_SEG
    fileoff = off + boot2_secs() * 512   # ...and where those bytes sit in the FILE
    kern = BUILD / "kernel.bin"
    if not kern.exists():
        sys.exit("t_canary: no build/kernel.bin - run make first")
    k = kern.read_bytes()
    if len(k) < fileoff + 2:
        sys.exit(f"t_canary: KERNEL.SYS is {len(k)} bytes, shorter than the "
                 f"canary's file offset {fileoff}")
    if off + 2 > 65536:
        sys.exit(f"t_canary: KSIG_OFF {off} is outside the 64KB the handoff's ES names")

    bad = []
    for name in IMAGES:
        img = BUILD / name
        if not img.exists():
            continue
        if not crosses(img, fileoff):
            bad.append(name)
    if bad:
        sys.exit(
            f"t_canary: KSIG_OFF {off} (file sector {fileoff // 512}) does NOT cross a "
            f"head on {', '.join(bad)} - it would be loaded CORRECTLY on a machine "
            f"whose FDC cannot flip, and the canary would pass on the one fault it "
            f"is for (SPEC.md 18.93.1)"
        )

    # ...and it has to be tellable from its neighbours, or a shifted read matches
    w = lambda o: int.from_bytes(k[o:o + 2], "little")
    for step in (-512, 512):
        if 0 <= fileoff + step < len(k) - 1 and w(fileoff) == w(fileoff + step):
            sys.exit(
                f"t_canary: the word at {fileoff} equals the one at {fileoff + step}, "
                f"so a read shifted by a sector still matches (SPEC.md 18.93.1)"
            )
    print(f"canary: memory offset {off}, file sector {fileoff // 512}, crosses a "
          f"head on {len([n for n in IMAGES if (BUILD / n).exists()])} geometries, "
          f"word 0x{w(fileoff):04X} distinct from both neighbours")


main()
