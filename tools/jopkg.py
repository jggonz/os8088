#!/usr/bin/env python3
"""jopkg: validate a .jop package header and stamp the output file.

    python3 tools/jopkg.py IN.bin -o OUT.jop

Reads a NASM flat binary that already carries the 32-byte jop package
header (SPEC.md section 20.2), validates every field against the actual
file, prints a one-line summary and copies the file to OUT.jop. Any
validation failure exits 1 with a message on stderr; OUT.jop is not
written on failure.
"""
import argparse
import struct
import sys

HEADER_SIZE = 32
MAGIC = 0x504A            # 'J','P' little-endian
VERSION = 1
LOAD_OFF = 0xA000
ENTRY_MIN = 0xA020        # first byte after the header
APP_MAX_SIZE = 0x5000     # image + bss budget


def fail(msg: str) -> None:
    print(f"jopkg: error: {msg}", file=sys.stderr)
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


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Validate a jop package header and write the .jop file.")
    ap.add_argument("input", metavar="IN.bin",
                    help="flat binary carrying the 32-byte jop header")
    ap.add_argument("-o", "--output", metavar="OUT.jop", required=True,
                    help="package file to write on success")
    args = ap.parse_args()

    try:
        with open(args.input, "rb") as f:
            data = f.read()
    except OSError as e:
        fail(f"cannot read {args.input}: {e}")

    if len(data) < HEADER_SIZE:
        fail(f"file is {len(data)} bytes; header alone is {HEADER_SIZE}")

    (magic, version, flags, load, entry, image, bss) = \
        struct.unpack_from("<HBBHHHH", data, 0)
    name = parse_name(data[16:32])

    if magic != MAGIC:
        fail(f"bad magic 0x{magic:04X} (want 0x{MAGIC:04X} 'JP')")
    if version != VERSION:
        fail(f"bad version {version} (want {VERSION})")
    if load != LOAD_OFF:
        fail(f"bad load offset 0x{load:04X} (want 0x{LOAD_OFF:04X})")
    if image != len(data):
        fail(f"image size field {image} != actual file size {len(data)}")
    if not (ENTRY_MIN <= entry < load + image):
        fail(f"entry 0x{entry:04X} outside [0x{ENTRY_MIN:04X}, "
             f"0x{load + image:04X})")
    if image + bss > APP_MAX_SIZE:
        fail(f"image {image} + bss {bss} = {image + bss} exceeds "
             f"budget 0x{APP_MAX_SIZE:04X} ({APP_MAX_SIZE})")

    try:
        with open(args.output, "wb") as f:
            f.write(data)
    except OSError as e:
        fail(f"cannot write {args.output}: {e}")

    print(f"jopkg: {name!r} entry=0x{entry:04X} image={image} bss={bss} "
          f"-> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
