#!/usr/bin/env python3
"""os88pkg: validate a .o88 v2 package, generate its relocation table, stamp.

    python3 tools/os88pkg.py IN.bin --alt ALT.bin -o OUT.o88

IN.bin is the package assembled at the 0xB000 link base; ALT.bin is the
SAME source assembled at org 0xB800 (the Makefile passes -DOS88_ORG=0xB800).
The probe delta 0x800 has a zero low byte, so every relocated word differs
from IN in exactly its high byte - the diff below finds each fixup site
deterministically. os88pkg verifies the header (SPEC.md section 20.2), diffs
the two images into the sorted fixup-offset table, HARD-VERIFIES that
adding the delta at exactly those words reconstructs ALT byte-for-byte
(catching any byte-split/truncated address, which the format forbids),
stamps the relocation count into header offset 12, and writes
OUT.o88 = IN + the table. Any failure exits 1 with a message on stderr;
OUT.o88 is not written on failure.
"""
import argparse
import struct
import sys

HEADER_SIZE = 32
MAGIC = 0x384F            # 'O','8' little-endian
VERSION = 2
LINK_BASE = 0xB000
PROBE_ORG = 0xB800
PROBE_DELTA = PROBE_ORG - LINK_BASE   # 0x800: low byte zero (see above)
ENTRY_MIN = 0x20          # image-relative: first byte after the header
ENTRY_MIN_ICON = 0x60     # first byte after the embedded icon (flags bit 0)
ICON_END = 96             # header (32) + icon block (64)
APP_MAX_SIZE = 0x4E00     # image + bss budget; also the load-region bound
SECTOR = 512


def fail(msg: str) -> None:
    print(f"os88pkg: error: {msg}", file=sys.stderr)
    sys.exit(1)


def parse_name(raw: bytes) -> str:
    """16 name bytes: printable ASCII, NUL padding, at least one char."""
    for b in raw:
        if b != 0 and not (0x20 <= b <= 0x7E):
            fail(f"name contains non-printable byte 0x{b:02X}")
    name = raw.split(b"\0", 1)[0]
    if not name:
        fail("name field is empty")
    if len(name) > 15:
        fail("name is 16 bytes with no NUL; the field is NUL-padded, max 15 chars")
    if any(b for b in raw[len(name):]):
        fail("name field has non-NUL bytes after the terminator")
    return name.decode("ascii")


def read_file(path: str) -> bytes:
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError as e:
        fail(f"cannot read {path}: {e}")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Validate an os8088 v2 package, build its relocation table, "
                    "and write the .o88 file.")
    ap.add_argument("input", metavar="IN.bin",
                    help="flat binary assembled at the 0xB000 link base")
    ap.add_argument("--alt", metavar="ALT.bin", required=True,
                    help="the same source assembled at org 0xB800 "
                         "(-DOS88_ORG=0xB800)")
    ap.add_argument("-o", "--output", metavar="OUT.o88", required=True,
                    help="package file to write on success")
    args = ap.parse_args()

    a = read_file(args.input)
    b = read_file(args.alt)

    # --- 1. the two assemblies must be byte-count identical -------------------
    if len(a) != len(b):
        fail(f"dual assembly produced different lengths ({len(a)} vs "
             f"{len(b)}): some encoding is org-dependent")
    if len(a) < HEADER_SIZE:
        fail(f"file is {len(a)} bytes; header alone is {HEADER_SIZE}")

    # --- 2. header validation (must be org-independent: byte-identical) -------
    (magic, version, flags, link, entry, image, bss, rcount, resv) = \
        struct.unpack_from("<HBBHHHHHH", a, 0)
    name = parse_name(a[16:32])

    if magic != MAGIC:
        fail(f"bad magic 0x{magic:04X} (want 0x{MAGIC:04X} 'O8')")
    if version != VERSION:
        fail(f"bad version {version} (want {VERSION}; rebuild against the "
             "v2 os88api.inc)")
    if flags & 0xFE:
        fail(f"flags 0x{flags:02X} has reserved bits set (bits 1-7 must be 0)")
    if link != LINK_BASE:
        fail(f"bad link base 0x{link:04X} (want 0x{LINK_BASE:04X})")
    if image != len(a):
        fail(f"image size field {image} != actual file size {len(a)}")
    entry_min = ENTRY_MIN_ICON if flags & 1 else ENTRY_MIN
    if not (entry_min <= entry < image):
        fail(f"entry +0x{entry:04X} outside [0x{entry_min:04X}, "
             f"0x{image:04X}) (image-relative)")
    if image + bss > APP_MAX_SIZE:
        fail(f"image {image} + bss {bss} = {image + bss} exceeds "
             f"budget 0x{APP_MAX_SIZE:04X} ({APP_MAX_SIZE})")
    if rcount != 0:
        fail("relocation-count field is non-zero; os88pkg stamps it - "
             "OS88_HEADER must emit 0")
    if resv != 0:
        fail("reserved header word (offset 14) is non-zero")
    if flags & 1 and image < ICON_END:
        fail(f"flags bit 0 set but image is {image} bytes; the embedded "
             f"icon needs at least {ICON_END}")
    if a[0:HEADER_SIZE] != b[0:HEADER_SIZE]:
        fail("headers differ between the two assemblies (org leaked into "
             "a header field)")
    if flags & 1 and a[HEADER_SIZE:ICON_END] != b[HEADER_SIZE:ICON_END]:
        fail("icon blocks differ between the two assemblies")

    # --- 3. diff scan: every mismatch is the high byte of a fixup word --------
    # Class 0 (+0x800): an embedded package address, relocated with the
    # load base. Class 1 (-0x800): the rel16 displacement of a near
    # call/jmp into the kernel (call OSAPI_*): the site moves with the
    # base while the target stays put, so the load-time patch is the
    # negation. Encoded in the table entry's bit 15 (SPEC.md 20.2).
    hi = PROBE_DELTA >> 8
    lo_hi = (-PROBE_DELTA >> 8) & 0xFF        # 0xF8
    fixups: list[int] = []                    # table entries (bit 15 = class)
    i = HEADER_SIZE               # headers are byte-equal; scan the image
    prev = -2
    n = len(a)
    while i < n:
        if a[i] == b[i]:
            i += 1
            continue
        o = i - 1
        if o < HEADER_SIZE:
            fail(f"byte 0x{i:04X} differs at the header boundary in a "
                 "non-relocation way")
        d = (b[i] - a[i]) & 0xFF
        if a[o] != b[o] or d not in (hi, lo_hi):
            fail(f"byte 0x{i:04X} differs but not as a whole-word ±0x800 "
                 "relocation: a package address was byte-split, shifted or "
                 "folded (SPEC.md 20.2 author rule)")
        if o > image - 2:
            fail(f"fixup at 0x{o:04X} runs past the image end")
        if o < prev + 2:
            fail(f"overlapping fixups at 0x{prev:04X} and 0x{o:04X}")
        fixups.append(o if d == hi else 0x8000 | o)
        prev = o
        i += 1

    # --- 4. hard verify: IN ± delta at the fixups must reconstruct ALT --------
    recon = bytearray(a)
    for e in fixups:
        o = e & 0x7FFF
        delta = -PROBE_DELTA if e & 0x8000 else PROBE_DELTA
        w = ((recon[o] | recon[o + 1] << 8) + delta) & 0xFFFF
        recon[o] = w & 0xFF
        recon[o + 1] = w >> 8
    if bytes(recon) != b:
        fail("reconstruction mismatch: the images differ by more than "
             "whole-word relocations")

    # --- 5. loadable bound: the file (image + table) must fit a region --------
    fsize = image + 2 * len(fixups)
    alloc = max(image + bss, (fsize + SECTOR - 1) // SECTOR * SECTOR)
    if alloc > APP_MAX_SIZE:
        fail(f"image {image} + {len(fixups)} relocs (+bss {bss}) needs a "
             f"{alloc}-byte region; the pool budget is {APP_MAX_SIZE} - "
             "the reloc table costs sector-rounded load space")

    # --- 6. stamp the count and emit -------------------------------------------
    out = bytearray(a)
    struct.pack_into("<H", out, 12, len(fixups))
    out += b"".join(struct.pack("<H", o) for o in fixups)
    try:
        with open(args.output, "wb") as f:
            f.write(out)
    except OSError as e:
        fail(f"cannot write {args.output}: {e}")

    icon = "yes" if flags & 1 else "no"
    print(f"os88pkg: {name!r} entry=+0x{entry:04X} image={image} bss={bss} "
          f"relocs={len(fixups)} icon={icon} -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
