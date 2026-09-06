#!/usr/bin/env python3
"""SPEC.md 18.93.1 - the boot sector's canary must sit in a sector that CROSSES
A HEAD in every shipped geometry.

This is the row that exists because the first version of the canary did not.
Offset 16384 is "past the first head flip" in every geometry and is still in
the FIRST half of a transfer run in every one - the half that loads correctly
on exactly the machine the canary exists to catch. It passed there, and the
kernel it let through was scrambled.

THE BAND IS AN INTERSECTION, so a NEW GEOMETRY can invalidate an offset that
nothing touched.  Adding SPEC.md 19's 1.2MB 5.25" disk did exactly that: its
data area starts at LBA 29 and its runs are 30 sectors, so file sector 36 -
the middle of the band the other three shared - lands 5 sectors into a FIRST
half there, and the canary was inert on one shipped disk of four with no
line of the boot path changed.  This row caught it, and it can only keep
catching it if every shipped system image is in IMAGES below.

A run transfers correctly up to the head boundary and only goes wrong after it.
So the test is not "past a flip" but "in a run's second half", re-derived here
from each image's own BPB the way boot.asm derives it at run time.

AND IT IS AN INTERSECTION OVER THE BLOB LENGTHS TOO, for the same reason one
level along: the file sector is the memory offset plus BOOT2_SECS, so a build
with a longer stage 2 puts the same offset in a different sector.  There were
two lengths until SPEC.md 15.3.8.5 retired SPLSTARS', and that intersection is
four sectors wide where the single-length band is seven - which is how this
constant came to sit at the top of `.text` with thirty bytes of room.  This row
reads EVERY `BOOT2_SECS*` equate and requires the offset to cross on all of
them, so a second length that comes back fails here rather than leaving one
arm's canary inert.

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
IMAGES = ["os8088-360.img", "os8088-720.img", "os8088-120.img",
          "os8088.img"]


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
    """EVERY stage-2 length the kernel can be built with.

    The offset is a MEMORY one and the file sector is BOOT2_SECS further in,
    so a build with a longer blob puts the SAME offset in a different sector -
    and the band is an intersection over the blob lengths as well as over the
    geometries.  There was a second length here for years (BOOT2_SECS_STARS,
    SPLSTARS' one-sector-longer blob) and this function found only the first:
    the "legal for both lengths" rule that pinned KSIG_OFF to file sector 106
    lived in a Makefile comment and was enforced by nothing, so moving the
    offset would have passed here while the knob build's canary went inert.
    It is one length today; the point of returning a LIST is that adding a
    second one fails this row instead of going quiet.
    """
    src = (ROOT / "kernel" / "kernel.asm").read_text()
    secs = [int(m.group(2))
            for m in re.finditer(r"^(BOOT2_SECS[A-Z0-9_]*)\s+equ\s+(\d+)",
                                 src, re.M)]
    if not secs:
        sys.exit("t_canary: no BOOT2_SECS in kernel/kernel.asm")
    return sorted(set(secs))


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
    lengths = boot2_secs()               # ...every blob length it must be legal for
    fileoff = off + lengths[0] * 512     # ...and where those bytes sit in the FILE
    kern = BUILD / "kernel.bin"
    if not kern.exists():
        sys.exit("t_canary: no build/kernel.bin - run make first")
    k = kern.read_bytes()
    if len(k) < fileoff + 2:
        sys.exit(f"t_canary: KERNEL.SYS is {len(k)} bytes, shorter than the "
                 f"canary's file offset {fileoff}")
    if off + 2 > 65536:
        sys.exit(f"t_canary: KSIG_OFF {off} is outside the 64KB the handoff's ES names")

    # A missing shipped image FAILS, the way t_image's does. This used to
    # `continue` past one, so a tree that had not built a geometry passed the
    # canary check for it - and the canary is the one guard that must hold on
    # every disk (SPEC.md 18.93.1), which is what the fourth geometry taught.
    missing = [name for name in IMAGES if not (BUILD / name).exists()]
    if missing:
        sys.exit("t_canary: shipped image(s) not built: %s - run make first; "
                 "a geometry this cannot see is a geometry the canary is not "
                 "checked on" % ", ".join(missing))

    bad = []
    for secs in lengths:
        fo = off + secs * 512
        for name in IMAGES:
            img = BUILD / name
            if not crosses(img, fo):
                bad.append(f"{name} (blob {secs} sectors, file sector {fo // 512})")
    if bad:
        sys.exit(
            f"t_canary: KSIG_OFF {off} does NOT cross a "
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
          f"head on {len([n for n in IMAGES if (BUILD / n).exists()])} geometries "
          f"and {len(lengths)} blob length{'' if len(lengths) == 1 else 's'} "
          f"{lengths}, word 0x{w(fileoff):04X} distinct from both neighbours")


main()
