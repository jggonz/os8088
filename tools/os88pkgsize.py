#!/usr/bin/env python3
"""os88pkgsize.py FULL.o88 SMALL.o88 - what does the small build save?

The APPS-side companion to tools/kernsplit.py, and it exists for the same
reason: a package built twice off one source (SPEC.md 27.16) has a claim
attached to it - that -DAPP_SMALL takes real memory off the floor machine and
takes NOTHING off the shipped build - and a claim nobody measures is a claim
that quietly stops being true.

WHAT A PACKAGE COSTS A MACHINE IS ONE NUMBER, and it is not the file size.
The loader takes ONE heap claim per instance and it is image + bss (SPEC.md
20.1), so a byte saved in bss is worth exactly a byte saved in .text - which
is not true of the kernel, where the two land in different rungs. That is why
this prints the sum and leads with it.

WHAT IT CANNOT SEE, and the reason the sum is a floor rather than the answer:
a package's HEAP CLAIMS are not in its header. Note Pad's undo arena grows to
NP_UMAXKB = 16KB (SPEC.md 27.9) and is simply never claimed when NPF_UNDO is
off, which is larger than everything this tool can count. Read the saving
below as "before the program has run", not as the whole of it.

It is a REPORT and never an error. The small build is ALLOWED to be smaller -
that is what it is for - and it is allowed to grow when somebody decides it
should. What is not allowed is either happening without anybody noticing.
"""
import os
import struct
import sys

# SPEC.md 20.2's header: name at +0, image size at +8, bss size at +10.
H_IMAGE, H_BSS = 8, 10


def read(path):
    with open(path, "rb") as f:
        hdr = f.read(32)
    if len(hdr) < 32:
        sys.exit(f"os88pkgsize: {path} is too short to be a package")
    image = struct.unpack_from("<H", hdr, H_IMAGE)[0]
    bss = struct.unpack_from("<H", hdr, H_BSS)[0]
    name = hdr[16:32].split(b"\0")[0].decode("ascii", "replace")
    disk = os.path.getsize(path)
    return name, image, bss, disk


def main():
    if len(sys.argv) != 3:
        print("usage: os88pkgsize.py FULL.o88 SMALL.o88", file=sys.stderr)
        return 2
    for p in sys.argv[1:]:
        if not os.path.exists(p):
            print(f"os88pkgsize: {p} not built - skipping the report")
            return 0
    fname, fi, fb, fd = read(sys.argv[1])
    sname, si, sb, sd = read(sys.argv[2])

    ft, st = fi + fb, si + sb
    saved = ft - st
    pct = (100.0 * saved / ft) if ft else 0.0

    print(f"pkgsize: {fname or 'package'} - what one instance claims "
          f"(image + bss, SPEC.md 20.1)")
    print(f"pkgsize:   full   {fi:6d} + {fb:5d} = {ft:6d} bytes")
    print(f"pkgsize:   small  {si:6d} + {sb:5d} = {st:6d} bytes")
    print(f"pkgsize:   SAVED  {saved:6d} bytes ({pct:.1f}%) per instance, "
          f"before any heap claim")
    if saved <= 0:
        print("pkgsize:   ...which is NOT a saving. -DAPP_SMALL has stopped "
              "reaching this package, or the gates have gone.")
    print(f"pkgsize:   on disk {fd} -> {sd} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
