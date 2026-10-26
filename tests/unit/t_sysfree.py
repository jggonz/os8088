#!/usr/bin/env python3
"""Every system disk keeps room for what the machine itself saves on it.

    python3 tests/unit/t_sysfree.py [IMAGE ...]

A system disk is the one thing on the machine that is WRITTEN at run time by
the system and not by the user: the Control Panel's SYSTEM.CFG, an AUTOSTRT
link, the association cache, the network pages' files. Each of those is a
write that fails with "disk full" the day the disk ships with no room for it,
and nothing at build time notices - the 360KB disk reached 512 bytes free on
main with every gate green. This row is the floor, at the owner's word:

  * a disk with 512-byte clusters keeps at least  5 KB free (10 clusters);
  * a disk with larger clusters keeps at least   10 KB free.

That is the critical list below plus one more save - the Calendar's 2.5 KB,
the Clock's state or a small sticky note. It is FREE SPACE ON THE BUILT
IMAGE, counted from the FAT by this file's own reader, not os88disk.py's.

Every shipped system disk is required; the on-demand ones (`make small`,
`make emu`, `make mega`) are checked when they have been built.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

SHIPPED = ["build/os8088.img", "build/os8088-120.img", "build/os8088-720.img",
           "build/os8088-360.img",
           # the 128KB machine's, shipped since SPEC.md 24.5.7
           "build/small360.img", "build/small720.img"]
# The small kernel's 1.44MB disk is build/small.img: these used to name
# build/smallk/os8088*.img, which no rule writes, so it was never read.
ON_DEMAND = ["build/small.img",
             "build/emu.img", "build/mega.img", "build/mega120.img"]

# What the system writes to its own boot disk and must always be able to.
# Sizes are the most each file is (SPEC.md 51.5's CFG_FBUF; desksc.inc's
# LNK_SIZE; ASSOC.DAT at the 2 KB the owner set; ethcfg.inc; HTTPDRV.CFG).
CRITICAL = [
    ("SYSTEM.CFG", 164, "every setting the Control Panel saves (SPEC.md 51.5)"),
    ("AUTOSTRT\\*.LNK", 50, "an autostart link"),
    ("AUTOSTRT\\*.LNK", 50, "a second autostart link"),
    ("ASSOC.DAT", 2048, "the association cache's write-back (SPEC.md 54.7.6)"),
    ("ETHER.CFG", 20, "the Ethernet page's address (SPEC.md 72)"),
    ("HTTPDRV.CFG", 64, "the HTTP drive's address (SPEC.md 122.5)"),
]


def floor_bytes(clus):
    return 5 * 1024 if clus <= 512 else 10 * 1024


def free_bytes(path):
    with open(path, "rb") as f:
        img = f.read()
    bps, spc, res, nfats, nroot, tot16, _, spf = struct.unpack_from(
        "<HBHBHHBH", img, 11)
    tot = tot16 or struct.unpack_from("<I", img, 32)[0]
    rootsecs = (nroot * 32 + bps - 1) // bps
    data0 = res + nfats * spf + rootsecs
    nclus = (tot - data0) // spc
    fat = img[res * bps:(res + spf) * bps]
    free = 0
    for n in range(2, nclus + 2):
        off = n * 3 // 2
        v = fat[off] | fat[off + 1] << 8
        v = v >> 4 if n & 1 else v & 0xFFF
        free += v == 0
    return free * bps * spc, bps * spc


def report(clus):
    lines, total = [], 0
    for name, size, what in CRITICAL:
        c = -(-size // clus)
        total += c
        lines.append("            %-16s %5d bytes = %2d cluster(s)  %s"
                     % (name, size, c, what))
    lines.append("            critical total: %d clusters = %d bytes, then one "
                 "more save (DESKCAL.DAT 2,456, DESKCLOK.DAT 48, a sticky "
                 "note)" % (total, total * clus))
    return "\n" + "\n".join(lines)


def main():
    images = sys.argv[1:]
    required = set(images or SHIPPED)
    if not images:
        images = SHIPPED + [p for p in ON_DEMAND
                            if os.path.exists(os.path.join(ROOT, p))]
    for img in images:
        p = img if os.path.isabs(img) else os.path.join(ROOT, img)
        if not os.path.exists(p):
            check(img not in required, "%s exists" % img, "run `make` first")
            continue
        free, clus = free_bytes(p)
        need = floor_bytes(clus)
        print("t_sysfree: %-28s %7d bytes free, %4d-byte clusters, floor %d"
              % (img, free, clus, need))
        check(free >= need,
              "%s keeps room for what the system saves on it" % img,
              "a system disk with less than the floor free cannot save these"
              + report(clus),
              got="%d bytes free" % free, want=">= %d" % need)
    done("t_sysfree")


if __name__ == "__main__":
    main()
