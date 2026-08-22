#!/usr/bin/env python3
# =============================================================================
# os8088 - tools/c64rom.py
#
# Build C64.ROM, the C64 package's ROM SIDECAR (C64-SPEC §1.4), out of
# the three Commodore ROM binaries committed under apps/c64/rom/.
#
#     python3 tools/c64rom.py                 -> build/c64-rom/C64.ROM
#     python3 tools/c64rom.py -o <path>       -> somewhere else
#     python3 tools/c64rom.py --check         -> verify the inputs, write
#                                                nothing
#
# WHY A SIDECAR AND NOT AN EMBEDDED ARRAY. 20,480 bytes of ROM inside the
# package would have put it at about 73,000 bytes against SPEC.md 73's 61,440
# cap - refused on paper, before a line was written. So the ROMs live in a
# file of their own that the package reads into a 20KB heap claim at launch,
# which is also what the user asked for in so many words: "use a sidecar that
# is loaded at runtime ... so that we can load the rom at runtime instead of
# embedding it in the package."
#
# THE LAYOUT IS FIXED AND THE PACKAGE DEPENDS ON IT (C64-SPEC §1.4):
#
#     0x0000  8192   KERNAL   kernal-901227-03.bin
#     0x2000  8192   BASIC    basic-901226-01.bin
#     0x4000  4096   CHARGEN  chargen-901225-01.bin
#             -----
#             20480 = 40 sectors, exactly 20KB, 512-aligned by construction,
#                     so os88_file_read_seg lands it straight at the base of
#                     the claim with no scratch buffer (SPEC.md 2.1.1).
#
# EVERY INPUT IS CHECKED BY SHA-256 BEFORE ANYTHING IS WRITTEN. The three
# hashes below are the ones C64-SPEC §1.3 and apps/c64/rom/README.md
# carry, and the three files are the defaults VICE 3.10's own
# src/c64/c64-resources.c picks. A mismatch stops the build naming the file:
# a ROM that is not the one the port was written against is a machine that
# boots to something else, and "it booted" is not the same claim as "it booted
# the machine this document describes".
#
# No network, no VICE tree, no fetch. This script reads three committed files
# and writes one. It is deterministic - the same three inputs give the same
# 20,480 bytes on every host - so a released C64.ROM rebuilds byte for byte.
# =============================================================================
import argparse
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ROMDIR = os.path.join(ROOT, "apps", "c64", "rom")

# (file, offset in C64.ROM, length, sha-256) - C64-SPEC §1.3
PARTS = [
    ("kernal-901227-03.bin", 0x0000, 8192,
     "83c60d47047d7beab8e5b7bf6f67f80daa088b7a6a27de0d7e016f6484042721"),
    ("basic-901226-01.bin", 0x2000, 8192,
     "89878cea0a268734696de11c4bae593eaaa506465d2029d619c0e0cbccdfa62d"),
    ("chargen-901225-01.bin", 0x4000, 4096,
     "fd0d53b8480e86163ac98998976c72cc58d5dd8eb824ed7b829774e74213b420"),
]

ROMSIZE = 20480


def die(msg):
    sys.stderr.write("c64rom: %s\n" % msg)
    raise SystemExit(1)


def read_parts():
    """Read and verify the three inputs. Returns the assembled image."""
    img = bytearray(ROMSIZE)
    for name, off, length, want in PARTS:
        path = os.path.join(ROMDIR, name)
        if not os.path.exists(path):
            die("%s is missing. The three Commodore ROM images are committed "
                "under apps/c64/rom/ (C64-SPEC §1.3)." % path)
        data = open(path, "rb").read()
        if len(data) != length:
            die("%s is %d bytes, expected %d" % (name, len(data), length))
        got = hashlib.sha256(data).hexdigest()
        if got != want:
            die("%s has SHA-256 %s, expected %s. This is not the ROM the port "
                "was written against (C64-SPEC §1.3)." %
                (name, got, want))
        img[off:off + length] = data
    return bytes(img)


def main():
    ap = argparse.ArgumentParser(
        description="Build C64.ROM from the committed Commodore ROM images.")
    ap.add_argument("-o", "--out",
                    default=os.path.join(ROOT, "build", "c64-rom", "C64.ROM"),
                    help="output path (default build/c64-rom/C64.ROM)")
    ap.add_argument("--check", action="store_true",
                    help="verify the inputs and write nothing")
    args = ap.parse_args()

    img = read_parts()
    if len(img) != ROMSIZE:
        die("internal: assembled %d bytes, expected %d" % (len(img), ROMSIZE))

    if args.check:
        print("c64rom: 3 inputs verified, %d bytes would be written" % ROMSIZE)
        return

    outdir = os.path.dirname(os.path.abspath(args.out))
    if outdir:
        os.makedirs(outdir, exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(img)
    print("c64rom: %s  %d bytes  (KERNAL 8192 + BASIC 8192 + CHARGEN 4096)" %
          (args.out, ROMSIZE))


if __name__ == "__main__":
    main()
