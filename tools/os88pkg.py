#!/usr/bin/env python3
"""os88pkg: validate a .o88 v3 package and write it.

    python3 tools/os88pkg.py IN.bin -o OUT.o88

IN.bin is the package assembled at org 0 (the only assembly there is).

**v3 has no relocation table.** Since SPEC.md 20.1 a package owns a SEGMENT:
it links at zero, the loader puts it on a paragraph boundary and hands it its
own CS/DS, so nothing in the image depends on where it landed. That retires
the whole v2 machinery - the dual assembly at 0xB000/0xB800, the diff scan,
the byte-exact reconstruction check and the author rule about whole-word
package addresses - and with it the class of bug where an address folded into
a constant assembled cleanly and relocated wrong.

What is left is validation, which matters more than it used to: the header
now carries the three-byte DISPATCHER the kernel far-calls to reach any
callback (SPEC.md 20.2), and a package that does not carry it would send the
kernel into its data on the first paint. Any failure exits 1 with a message
on stderr; OUT.o88 is not written on failure.
"""
import argparse
import struct
import sys

HEADER_SIZE = 32
MAGIC = 0x384F            # 'O','8' little-endian
VERSION = 3
ENTRY_MIN = 0x20          # first byte after the header
ENTRY_MIN_ICON = 0x60     # first byte after the embedded icon (flags bit 0)
ICON_END = 96             # header (32) + icon block (64)
ASSOC_SIZE = 16           # the association block (SPEC.md 54.6), flags bit 1
ASSOC_MAXN = 5            # ...holding a count byte and up to five extensions
APP_MAX_SIZE = 0xF000     # image + bss budget: 60KB (one segment's worth -
                          # the region is a heap claim, so the real limit is
                          # also whatever the heap has contiguous)
DISPATCH = bytes((0xFF, 0xD5, 0xCB, 0x00))   # call bp / retf / pad, at +12


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
                    help="flat binary assembled at org 0")
    ap.add_argument("-o", "--output", metavar="OUT.o88", required=True,
                    help="package file to write on success")
    args = ap.parse_args()

    a = read_file(args.input)
    if len(a) < HEADER_SIZE:
        fail(f"file is {len(a)} bytes; header alone is {HEADER_SIZE}")

    (magic, version, flags, link, entry, image, bss) = \
        struct.unpack_from("<HBBHHHH", a, 0)
    name = parse_name(a[16:32])

    if magic != MAGIC:
        fail(f"bad magic 0x{magic:04X} (want 0x{MAGIC:04X} 'O8')")
    if version != VERSION:
        fail(f"bad version {version} (want {VERSION}; rebuild against the "
             "v3 os88api.inc)")
    if flags & 0xFC:
        fail(f"flags 0x{flags:02X} has reserved bits set (bits 2-7 must be 0)")
    if link != 0:
        fail(f"bad link base 0x{link:04X}: a v3 package links at org 0")
    if a[12:16] != DISPATCH:
        fail("header offset 12 is not the dispatcher `call bp / retf` - "
             "the OS88_HEADER macro emits it, so this image was built "
             "against an older os88api.inc")
    if image != len(a):
        fail(f"image size field {image} != actual file size {len(a)}")
    # The association block (SPEC.md 54.6) follows the icon, or the header when
    # there is none - so it moves where the code can start. This is the whole
    # of the format change and it needs NO version bump: everything an older
    # kernel reads is unmoved, and LD_H_ENTRY is absolute, so where the code
    # begins is told rather than derived.
    assoc_base = ICON_END if flags & 1 else HEADER_SIZE
    entry_min = ICON_END if flags & 1 else ENTRY_MIN
    if flags & 2:
        entry_min = assoc_base + ASSOC_SIZE
        if image < entry_min:
            fail(f"flags bit 1 set but the image is {image} bytes; the "
                 f"association block needs at least {entry_min}")
        n = a[assoc_base]
        if n > ASSOC_MAXN:
            fail(f"association block declares {n} extensions, at most "
                 f"{ASSOC_MAXN} fit")
        for i in range(n):
            ext = a[assoc_base + 1 + 3 * i:assoc_base + 4 + 3 * i]
            if any(not 0x20 <= b <= 0x7E for b in ext):
                fail(f"association {i}: extension bytes must be printable")
            if ext.decode("ascii") != ext.decode("ascii").upper():
                fail(f"association {i}: {ext!r} must be uppercase - the mount "
                     f"compares exactly (SPEC.md 19.1)")
            if ext == b"O88":
                fail("association: O88 cannot be declared - a package is "
                     "never opened through an association")
    if not (entry_min <= entry < image):
        fail(f"entry +0x{entry:04X} outside [0x{entry_min:04X}, "
             f"0x{image:04X})")
    if image + bss > APP_MAX_SIZE:
        fail(f"image {image} + bss {bss} = {image + bss} exceeds "
             f"budget 0x{APP_MAX_SIZE:04X} ({APP_MAX_SIZE})")
    if flags & 1 and image < ICON_END:
        fail(f"flags bit 0 set but image is {image} bytes; the embedded "
             f"icon needs at least {ICON_END}")

    try:
        with open(args.output, "wb") as f:
            f.write(a)
    except OSError as e:
        fail(f"cannot write {args.output}: {e}")

    icon = "yes" if flags & 1 else "no"
    assoc = a[assoc_base] if flags & 2 else 0
    print(f"os88pkg: {name!r} entry=+0x{entry:04X} image={image} bss={bss} "
          f"icon={icon} assoc={assoc} -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
