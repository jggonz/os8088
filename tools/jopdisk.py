#!/usr/bin/env python3
"""jopdisk: build a jopfs data floppy image from .jop packages.

    python3 tools/jopdisk.py -o OUT.img --size {1440,360} PKG.jop ...

jopfs layout (SPEC.md section 19): LBA 0 superblock ("JOPFS1\\0\\0",
geometry, file count), LBA 1-2 directory (32 entries x 32 bytes), file
data from LBA 3, every file sector-aligned. Directory names come from
each package's header name field (SPEC.md section 20.2). The image is
zero-filled to exactly 1474560 (1.44MB) or 368640 (360KB) bytes. Fails
with a non-zero exit if there are more than 32 files or the data does
not fit on the disk.
"""
import argparse
import struct
import sys

SECTOR = 512
MAX_FILES = 32
DATA_LBA = 3
TYPE_APP = 1
GEOMETRY = {          # size -> (sectors per track, heads, total sectors)
    1440: (18, 2, 2880),
    360: (9, 2, 720),
}


def fail(msg: str) -> None:
    print(f"jopdisk: error: {msg}", file=sys.stderr)
    sys.exit(1)


def read_package(path: str) -> tuple[bytes, bytes]:
    """Read one .jop file; return (name16, data)."""
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
    if len(data) > 0xFFFF:
        fail(f"{path}: {len(data)} bytes overflows the 16-bit size field")
    image, = struct.unpack_from("<H", data, 8)
    if image != len(data):
        fail(f"{path}: header image size {image} != file size {len(data)} "
             "(run jopkg.py first)")
    name16 = data[16:32]
    if not name16.split(b"\0", 1)[0]:
        fail(f"{path}: empty name field in header")
    return name16, data


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Build a jopfs data floppy from .jop packages.")
    ap.add_argument("-o", "--output", metavar="OUT.img", required=True,
                    help="floppy image to write")
    ap.add_argument("--size", type=int, choices=(1440, 360), required=True,
                    help="disk size in KB: 1440 (18 spt) or 360 (9 spt)")
    ap.add_argument("packages", metavar="PKG.jop", nargs="+",
                    help="package files, in directory order")
    args = ap.parse_args()

    spt, heads, total_sectors = GEOMETRY[args.size]
    if len(args.packages) > MAX_FILES:
        fail(f"{len(args.packages)} files; jopfs holds at most {MAX_FILES}")

    # Superblock (LBA 0).
    sb = struct.pack("<8sHHH", b"JOPFS1\0\0", spt, heads, len(args.packages))
    image = bytearray(sb.ljust(SECTOR, b"\0"))

    # Directory (LBA 1-2) + data (LBA 3 onward, each file sector-aligned).
    directory = bytearray(2 * SECTOR)
    data = bytearray()
    lba = DATA_LBA
    for i, path in enumerate(args.packages):
        name16, body = read_package(path)
        sectors = (len(body) + SECTOR - 1) // SECTOR
        struct.pack_into("<16sHHH", directory, i * 32,
                         name16, TYPE_APP, lba, len(body))
        data += body.ljust(sectors * SECTOR, b"\0")
        lba += sectors

    if lba > total_sectors:
        fail(f"data ends at LBA {lba}; disk holds {total_sectors} sectors")

    image += directory + data
    image += b"\0" * (total_sectors * SECTOR - len(image))
    assert len(image) == total_sectors * SECTOR

    try:
        with open(args.output, "wb") as f:
            f.write(image)
    except OSError as e:
        fail(f"cannot write {args.output}: {e}")

    print(f"jopdisk: {args.output} ({args.size}KB, {spt} spt) "
          f"{len(args.packages)} file(s), data LBA {DATA_LBA}..{lba - 1}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
