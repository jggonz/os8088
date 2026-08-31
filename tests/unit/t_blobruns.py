#!/usr/bin/env python3
"""How many int 13h calls does stage 1 spend on the blob? (SPEC.md 15.3.8.5)

    python3 tests/unit/t_blobruns.py [--sectors N]

`--sectors N` prices a blob this file's kernel does not have, which is what
somebody about to spend a byte of `.boot2` wants to know: `--sectors 14` is
SPLSTARS' blob and shows the 720KB disk taking a third call for one sector.

PERFORMANCE.md's rule is that disk work is priced in CALLS and not in sectors -
one int 13h is 1-2 disk revolutions whatever it moves, near enough 400 ms on
the field machine - and stage 1's read of stage 2 is the very first of them.

THE COUNT IS NOT A FUNCTION OF THE SECTOR COUNT ALONE, which is the whole
reason this file exists. boot/boot.asm bounds a run at the TRACK, and
KERNEL.SYS starts wherever the BPB puts the data area - so the same
BOOT2_SECS splits differently on each geometry, and the boundary is in a
different place on each:

    BOOT2_SECS   360KB          720KB          1.44MB
        13       6 + 7          4 + 9          3 + 10
        14       6 + 8          4 + 9 + 1      3 + 11

**720KB is the tight one and 13 is exactly its last sector.** The 14th costs a
whole extra int 13h to move ONE sector, which is the worst shape a read has.
That was found by measuring SPLSTARS=1 (which does take 14, SPEC.md 15.3.8.5)
after it had been asserted in a commit message that the sector was free, on
the strength of the 360KB arithmetic alone.

So this asserts the SHIPPED blob is two calls on all three geometries. It is a
ratchet rather than a law: growing the blob past a boundary is a decision
somebody may take, and then MAX_RUNS moves with a reason beside it - what it
may not be is a thing nobody noticed.

Host-side and exact: it reads the images `make` just built, finds KERNEL.SYS
in the root directory, and walks the runs the way the sector does. No emulator
- and MartyPC cannot host a 720KB drive with the ROM sets in this tree anyway,
which is precisely how the boundary got missed.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

IMAGES = ("os8088-360.img", "os8088-720.img", "os8088.img")
MAX_RUNS = 2                    # ...per geometry, for the SHIPPED blob


def boot2_secs():
    """The kernel's own constant, not a copy - the Makefile reads it the same
    way and kernel.asm asserts stage 2 fits inside it."""
    path = os.path.join(ROOT, "kernel", "kernel.asm")
    for line in open(path, encoding="utf-8"):
        if line.startswith("BOOT2_SECS ") or line.startswith("BOOT2_SECS\t"):
            head = line.split(";")[0].split()
            if len(head) >= 3 and head[1] == "equ" and head[2].isdigit():
                return int(head[2])
    raise SystemExit("t_blobruns: no BOOT2_SECS in kernel/kernel.asm")


def runs(path, nsec):
    """The (cyl, head, sector, count) runs stage 1 asks for, off the image."""
    d = open(path, "rb").read()
    bps = struct.unpack("<H", d[11:13])[0]
    spc = d[13]
    res = struct.unpack("<H", d[14:16])[0]
    nfat = d[16]
    rootn = struct.unpack("<H", d[17:19])[0]
    fatsz = struct.unpack("<H", d[22:24])[0]
    spt = struct.unpack("<H", d[24:26])[0]
    heads = struct.unpack("<H", d[26:28])[0]
    rootlba = res + nfat * fatsz
    datalba = rootlba + (rootn * 32 + bps - 1) // bps

    start = None
    for i in range(rootn):
        off = rootlba * bps + i * 32
        e = d[off:off + 32]
        if e[0] == 0x00:
            break
        if e[0:11] == b"KERNEL  SYS":
            start = datalba + (struct.unpack("<H", e[26:28])[0] - 2) * spc
            break
    if start is None:
        raise SystemExit("t_blobruns: no KERNEL.SYS in %s" % path)
    if start != datalba:
        raise SystemExit("t_blobruns: KERNEL.SYS is not the first file in %s - "
                         "boot/boot.asm derives its LBA as the data area's and "
                         "would read somebody else's sectors" % path)

    out, lba, left = [], start, nsec
    while left:
        cyl, rem = divmod(lba, spt * heads)
        head, sec = divmod(rem, spt)
        run = min(spt - sec, left)          # sec is 0-based here
        out.append((cyl, head, sec + 1, run))
        lba += run
        left -= run
    return spt, out


def main():
    nsec = boot2_secs()
    argv = sys.argv[1:]
    if argv[:1] == ["--sectors"] and len(argv) > 1:
        nsec = int(argv[1])
        print("  (asked for %d sectors, not the kernel's own %d)"
              % (nsec, boot2_secs()))
    fail = []
    print("  stage 1 reads BOOT2_SECS = %d sectors of KERNEL.SYS" % nsec)
    for img in IMAGES:
        path = os.path.join(ROOT, "build", img)
        if not os.path.exists(path):
            print("  %-16s not built - skipped" % img)
            continue
        spt, r = runs(path, nsec)
        print("  %-16s SPT=%-2d  %d call(s): %s"
              % (img, spt, len(r),
                 " + ".join("%d@C%dH%dS%d" % (n, c, h, s) for c, h, s, n in r)))
        if len(r) > MAX_RUNS:
            fail.append("%s: the blob takes %d int 13h calls and the ratchet is "
                        "%d. One call is 1-2 revolutions WHATEVER it moves "
                        "(PERFORMANCE.md Part 2), so the last run here costs a "
                        "whole one to move %d sector(s). Either the blob fits "
                        "again or MAX_RUNS moves with a reason beside it "
                        "(SPEC.md 15.3.8.5)"
                        % (img, len(r), MAX_RUNS, r[-1][3]))

    for f in fail:
        print("FAIL: %s" % f)
    print("blobruns: %s" % ("FAILED" if fail else
                            "the blob is %d int 13h call(s) on every geometry"
                            % MAX_RUNS))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
