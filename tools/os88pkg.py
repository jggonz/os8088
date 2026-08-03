#!/usr/bin/env python3
"""os88pkg: validate a .o88 v3 package and stamp it.

    python3 tools/os88pkg.py IN.bin -o OUT.o88 [--needs KB]

Since v3 (SPEC.md section 20.2) a package is assembled at org 0 and loaded
into its own segment: there is no link base, no relocation table and no
dual-assembly diff - this tool is a validator and stamper, nothing more.
The validation is strict on purpose: every header rule the kernel's
ld_check_hdr enforces at load time is enforced here first, on the host,
where the error messages are cheap. --needs KB overrides the header's +14
memory-requirement word (the default is whatever OS88_HEADER emitted,
SPEC.md section 20.5). Any failure exits 1 with a message on stderr;
OUT.o88 is not written on failure.
"""
import argparse
import struct
import sys

HEADER_SIZE = 32
MAGIC = 0x384F            # 'O','8' little-endian
VERSION = 3
ENTRY_MIN = 0x20          # image-relative: first byte after the header
ENTRY_MIN_ICON = 0x60     # first byte after the embedded icon (flags bit 0)
ICON_END = 96             # header (32) + icon block (64)
PKG_MAX_PARA = 0x0FFF     # region cap in paragraphs (SPEC.md 2.5)
PKG_MAX_BYTES = PKG_MAX_PARA * 16   # 65,520: image + bss budget (one segment)
NEEDS_MIN = 256           # plausible conventional-RAM figures, KB: below the
NEEDS_MAX = 640           # floor machine or above the real-mode ceiling is
                          # an authoring mistake, not a machine that exists


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
        description="Validate an os8088 v3 package and write the .o88 file.")
    ap.add_argument("input", metavar="IN.bin",
                    help="flat binary assembled at org 0 (OS88_HEADER emits it)")
    ap.add_argument("-o", "--output", metavar="OUT.o88", required=True,
                    help="package file to write on success")
    ap.add_argument("--needs", metavar="KB", type=int, default=None,
                    help="override the header's +14 memory-requirement word "
                         "(minimum conventional RAM in KB; default: keep what "
                         "OS88_HEADER emitted)")
    args = ap.parse_args()

    a = read_file(args.input)

    if len(a) < HEADER_SIZE:
        fail(f"file is {len(a)} bytes; header alone is {HEADER_SIZE}")
    if len(a) > PKG_MAX_BYTES:
        fail(f"file is {len(a)} bytes; a package caps at {PKG_MAX_BYTES} "
             f"(PKG_MAX_PARA*16, one segment minus the wrap guard)")

    # --- header validation (SPEC.md 20.2; mirrors the kernel's ld_check_hdr) --
    (magic, version, flags, link, entry, image, bss, rcount, needs) = \
        struct.unpack_from("<HBBHHHHHH", a, 0)
    name = parse_name(a[16:32])

    if magic != MAGIC:
        fail(f"bad magic 0x{magic:04X} (want 0x{MAGIC:04X} 'O8')")
    if version != VERSION:
        fail(f"bad version {version} (want {VERSION}; rebuild against the "
             "v3 os88api.inc)")
    if flags & 0xFE:
        fail(f"flags 0x{flags:02X} has reserved bits set (bits 1-7 must be 0)")
    if link != 0:
        fail(f"header word +4 is 0x{link:04X}; it must be ZERO since v3 "
             "(the v2 link base is retired - org 0 has none)")
    if rcount != 0:
        fail(f"header word +12 is 0x{rcount:04X}; it must be ZERO since v3 "
             "(the relocation count is retired - there is no relocation)")
    if image != len(a):
        fail(f"image size field {image} != actual file size {len(a)} "
             "(they must be EQUAL - the kernel's truncation guard depends "
             "on it, SPEC.md 21)")
    entry_min = ENTRY_MIN_ICON if flags & 1 else ENTRY_MIN
    if not (entry_min <= entry < image):
        fail(f"entry +0x{entry:04X} outside [0x{entry_min:04X}, "
             f"0x{image:04X}) (image-relative)")
    if image + bss > PKG_MAX_BYTES:
        fail(f"image {image} + bss {bss} = {image + bss} exceeds the "
             f"one-segment budget {PKG_MAX_BYTES} (PKG_MAX_PARA*16)")
    if flags & 1 and image < ICON_END:
        fail(f"flags bit 0 set but image is {image} bytes; the embedded "
             f"icon needs at least {ICON_END}")

    # --- the memory-requirement word (SPEC.md 20.2, header +14) ---------------
    if args.needs is not None:
        needs = args.needs
    if not (NEEDS_MIN <= needs <= NEEDS_MAX):
        fail(f"memory requirement {needs}KB implausible (want "
             f"{NEEDS_MIN}..{NEEDS_MAX}: conventional RAM in KB)")

    # --- stamp and emit -------------------------------------------------------
    out = bytearray(a)
    struct.pack_into("<H", out, 14, needs)
    try:
        with open(args.output, "wb") as f:
            f.write(out)
    except OSError as e:
        fail(f"cannot write {args.output}: {e}")

    icon = "yes" if flags & 1 else "no"
    print(f"os88pkg: {name!r} entry=+0x{entry:04X} image={image} bss={bss} "
          f"needs={needs}K icon={icon} -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
