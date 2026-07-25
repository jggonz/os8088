#!/usr/bin/env python3
"""jopdisk: build a jopfs v2 data floppy image from .jop packages.

    python3 tools/jopdisk.py -o OUT.img --size {1440,360} [PKG.jop ...]

jopfs v2 layout (SPEC.md section 19): LBA 0 superblock ("JOPFS2\\0\\0",
geometry, file count), LBA 1-2 directory (32 entries x 32 bytes), LBA 3-6
icon table (32 entries x 64 bytes: package bytes 32..95 when its flags
bit 0 is set, else all zeros = "no icon"), file data from LBA 7, every
file sector-aligned. Directory names come from each package's header name
field (SPEC.md section 20.2). Zero packages is legal (an empty disk). The
image is zero-filled to exactly 1474560 (1.44MB) or 368640 (360KB) bytes.
Fails with a non-zero exit if there are more than 32 files or the data
does not fit on the disk.
"""
import argparse
import struct
import sys

SECTOR = 512
MAX_FILES = 32
ICON_SECTORS = 4      # LBA 3-6: 32 icon entries x 64 bytes
DATA_LBA = 7
TYPE_APP = 1
GEOMETRY = {          # size -> (sectors per track, heads, total sectors)
    1440: (18, 2, 2880),
    360: (9, 2, 720),
}


def fail(msg: str) -> None:
    print(f"jopdisk: error: {msg}", file=sys.stderr)
    sys.exit(1)


def read_package(path: str) -> tuple[bytes, bytes, bytes]:
    """Read one .jop file; return (name16, icon64, data)."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        fail(f"cannot read {path}: {e}")
    if len(data) < 32:
        fail(f"{path}: too short for a jop header ({len(data)} bytes)")
    magic, = struct.unpack_from("<H", data, 0)
    if magic != 0x504A:
        fail(f"{path}: bad magic 0x{magic:04X} (not a .jop package)")
    if data[2] != 2:
        fail(f"{path}: format version {data[2]}; this is the v2 toolchain "
             "(rebuild the package)")
    if len(data) > 0xFFFF:
        fail(f"{path}: {len(data)} bytes overflows the 16-bit size field")
    image, rcount = struct.unpack_from("<H", data, 8)[0], \
        struct.unpack_from("<H", data, 12)[0]
    if not 32 <= image <= len(data):
        fail(f"{path}: header image size {image} out of range")
    if image + 2 * rcount != len(data):
        fail(f"{path}: image {image} + {rcount} reloc words != file size "
             f"{len(data)} (run jopkg.py first)")
    name16 = data[16:32]
    if not name16.split(b"\0", 1)[0]:
        fail(f"{path}: empty name field in header")
    flags = data[3]
    if flags & 1:                     # embedded icon (SPEC.md 20.2)
        if len(data) < 96:
            fail(f"{path}: flags bit 0 set but no icon block "
                 "(run jopkg.py first)")
        icon64 = data[32:96]
    else:
        icon64 = bytes(64)            # all-zero = "no icon"
    return name16, icon64, data


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Build a jopfs data floppy from .jop packages.")
    ap.add_argument("-o", "--output", metavar="OUT.img", required=True,
                    help="floppy image to write")
    ap.add_argument("--size", type=int, choices=(1440, 360), required=True,
                    help="disk size in KB: 1440 (18 spt) or 360 (9 spt)")
    ap.add_argument("packages", metavar="PKG.jop", nargs="*",
                    help="package files, in directory order (none = empty disk)")
    args = ap.parse_args()

    spt, heads, total_sectors = GEOMETRY[args.size]
    if len(args.packages) > MAX_FILES:
        fail(f"{len(args.packages)} files; jopfs holds at most {MAX_FILES}")

    # Superblock (LBA 0).
    sb = struct.pack("<8sHHH", b"JOPFS2\0\0", spt, heads, len(args.packages))
    image = bytearray(sb.ljust(SECTOR, b"\0"))

    # Directory (LBA 1-2) + icon table (LBA 3-6) + data (LBA 7 onward,
    # each file sector-aligned).
    directory = bytearray(2 * SECTOR)
    icons = bytearray(ICON_SECTORS * SECTOR)
    data = bytearray()
    lba = DATA_LBA
    for i, path in enumerate(args.packages):
        name16, icon64, body = read_package(path)
        sectors = (len(body) + SECTOR - 1) // SECTOR
        struct.pack_into("<16sHHH", directory, i * 32,
                         name16, TYPE_APP, lba, len(body))
        icons[i * 64:(i + 1) * 64] = icon64
        data += body.ljust(sectors * SECTOR, b"\0")
        lba += sectors

    if lba > total_sectors:
        fail(f"data ends at LBA {lba}; disk holds {total_sectors} sectors")

    image += directory + icons + data
    image += b"\0" * (total_sectors * SECTOR - len(image))
    assert len(image) == total_sectors * SECTOR

    try:
        with open(args.output, "wb") as f:
            f.write(image)
    except OSError as e:
        fail(f"cannot write {args.output}: {e}")

    span = f"data LBA {DATA_LBA}..{lba - 1}" if args.packages else "no data"
    print(f"jopdisk: {args.output} ({args.size}KB, {spt} spt) "
          f"{len(args.packages)} file(s), {span}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
