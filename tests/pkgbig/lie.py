#!/usr/bin/env python3
"""Write a file's SIZE into its root-directory entry on a FAT12 image, and
nothing else - the cluster chain stays what it is.

    python3 tests/pkgbig/lie.py IMAGE NAME.EXT BYTES

SPEC.md 19.1 types a *.O88 by the entry's size before anything reads it, and
PKG_FILE_HI's boundary (32MB) fits no floppy, so tests/pkgbig.py's HUGE.O88
carries 1MB of data and an entry that says 32MB. Run AFTER os88disk.py
--verify, which would rightly call the disk inconsistent. Nothing shipped is
ever written by this.
"""
import struct
import sys


def main():
    path, name, size = sys.argv[1], sys.argv[2].upper(), int(sys.argv[3], 0)
    stem, _, ext = name.partition(".")
    want = stem.ljust(8).encode() + ext.ljust(3).encode()
    with open(path, "r+b") as f:
        bs = f.read(512)
        bps, spc, res, nfat, nroot = struct.unpack_from("<HBHBH", bs, 11)
        spf = struct.unpack_from("<H", bs, 22)[0]
        root = (res + nfat * spf) * bps
        for i in range(nroot):
            f.seek(root + 32 * i)
            e = f.read(32)
            if e[:11] == want:
                f.seek(root + 32 * i + 28)
                f.write(struct.pack("<I", size))
                print("lie: %s's entry says %d bytes" % (name, size))
                return 0
    raise SystemExit("lie: %s is not in %s's root" % (name, path))


if __name__ == "__main__":
    sys.exit(main())
