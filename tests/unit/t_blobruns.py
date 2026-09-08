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

    BOOT2_SECS   360KB          720KB          1.2MB        1.44MB
        13       6 + 7          4 + 9          1 + 12       3 + 10
        14       6 + 8          4 + 9 + 1      1 + 13       3 + 11
        19       6 + 9 + 4      4 + 9 + 6      1 + 15 + 3   3 + 16
        22       6 + 9 + 7      4 + 9 + 9      1 + 15 + 6   3 + 18 + 1
        23       6 + 9 + 8      4 + 9 + 9 + 1  1 + 15 + 7   3 + 18 + 2

**720KB is the tight one at 13 and 13 is exactly its last sector.** The 14th
costs a whole extra int 13h to move ONE sector, which is the worst shape a read
has. That was found by measuring SPLSTARS=1 (which took 14, SPEC.md 15.3.8.5)
after it had been asserted in a commit message that the sector was free, on
the strength of the 360KB arithmetic alone.

So this asserts a CALL COUNT PER GEOMETRY, not one number for all of them - the
table above is why, and one number was the shape of the original mistake.
SPEC.md 2.9.12 spent the third call on the two 9-sector geometries; 1.44MB is
still two and 21 is the last size that keeps it there. It is a ratchet rather
than a law: growing the blob past a boundary is a decision somebody may take,
and then THAT GEOMETRY's row moves with a reason beside it - what it may not be
is a thing nobody noticed.

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

# ...AND THE RATCHET IS PER GEOMETRY, because the four do not share an answer
# and never did - the paragraph above is the whole reason this file exists, and
# one number for all of them was the shape of the mistake it was written to
# catch. SPEC.md 2.9.12 took the blob from 13 sectors to 19 for the boot-only
# bodies in docs/plans/LAST-DROP-BYTES.md, which spends the third call on the two
# 9-sector geometries and none on the release one.
#
# **AND THEN SPEC.md 2.5.2 TOOK IT BACK TO 8**, by splitting the overlay by
# lifetime: the bodies that are dead at the first mount ride the kernel's own
# read into the FAT window instead, and only the 1,319 bytes that must outlive
# it stay in the blob. So the third call is given back on both 9-sector
# geometries and the ratchet closes behind it - all of them are TWO now, and
# the 1.44MB row has been two throughout.
#
# The numbers each row cites are the sizes at which its own count changes, and
# they are the reason to keep the rows apart rather than collapse them to one
# figure: 13 is the last two-call blob on 720KB, 15 on 360KB, 16 on 1.2MB and
# 21 on 1.44MB, so a growth that is free on the release geometry can still buy
# a call on the other three - and 720KB is still the one that binds.
IMAGES = {
    "os8088-360.img": (2, "6 + 2 at 8 sectors; it was 6 + 9 + 4 at 19, and "
                          "the third call arrived with the 16th sector"),
    "os8088-720.img": (2, "4 + 4 at 8; 13 is this geometry's last two-call "
                          "blob and the 14th sector was the price"),
    "os8088-120.img": (2, "1 + 7 at 8 - the data area starts one sector "
                          "short of a cylinder end here, so the first call "
                          "is a single sector and 16 is the last two-call "
                          "blob"),
    "os8088.img":     (2, "3 + 5 at 8, and 21 is the last size that is two "
                          "calls here - the next one is not free"),
}


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
    seen = []
    print("  stage 1 reads BOOT2_SECS = %d sectors of KERNEL.SYS" % nsec)
    for img, (cap, why) in IMAGES.items():
        path = os.path.join(ROOT, "build", img)
        if not os.path.exists(path):
            # a FAILURE, not a skip: this row used to print "not built" and
            # pass, so a geometry nobody had built was a geometry whose
            # blob ratchet was never read at all
            fail.append("%s: not built - run make first; the ratchet for "
                        "this geometry (%s) cannot be read off a disk that "
                        "is not there" % (img, why))
            continue
        spt, r = runs(path, nsec)
        seen.append(len(r))
        print("  %-16s SPT=%-2d  %d call(s) of %d: %s"
              % (img, spt, len(r), cap,
                 " + ".join("%d@C%dH%dS%d" % (n, c, h, s) for c, h, s, n in r)))
        if len(r) > cap:
            fail.append("%s: the blob takes %d int 13h calls and this "
                        "geometry's ratchet is %d (%s). One call is 1-2 "
                        "revolutions WHATEVER it moves (PERFORMANCE.md Part "
                        "2), so the last run here costs a whole one to move %d "
                        "sector(s). Either the blob fits again or THIS "
                        "geometry's row moves with a reason beside it "
                        "(SPEC.md 15.3.8.5, 2.9.12)"
                        % (img, len(r), cap, why, r[-1][3]))

    for f in fail:
        print("FAIL: %s" % f)
    print("blobruns: %s" % ("FAILED" if fail else
                            "the blob is %s int 13h call(s), 360/720/1.2/1.44"
                            % "/".join(str(n) for n in seen)))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
