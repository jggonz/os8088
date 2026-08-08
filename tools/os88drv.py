#!/usr/bin/env python3
"""os88drv: validate a flat driver binary and stamp it as a .drv file.

    python3 tools/os88drv.py build/sound.bin -o build/sound.drv

The sibling of tools/os88pkg.py, and deliberately the same shape: a
validator, not a generator. A driver is assembled once at org 0 and owns the
segment the kernel claims for it, so there is nothing to relocate and nothing
to rewrite -- this checks that the 32-byte header the OS88_DRIVER macro
emitted actually describes the file that came out of nasm, and copies it.

What it enforces (kernel/driver.inc's drv_check does all of it again at load
time, against the image that actually arrived in memory):

  +0   magic 'O8'                the package magic; the version tells them apart
  +2   version 4                 3 is an application. A 3 here would be a
                                 driver the app loader would try to RUN.
  +3   class                     1 = sound; must be one the kernel knows
  +4   link base 0               org 0, like every v3 package
  +6   entry                     inside the image and past the header
  +8   image size                == the file's own size, exactly. A driver
                                 has no separate bss: its zeroed data ships
                                 inside the image, which is what lets the
                                 kernel make ONE heap claim, at the size the
                                 directory entry already reported.
  +12  FF D5 CB                  `call bp / retf`: the dispatcher every
                                 kernel-to-driver call lands on
  +16  name                      NUL-padded, printable, non-empty

Exit 0 and the file is written; exit 1 with the reason on stderr otherwise.
"""
import argparse
import struct
import sys

HDR = 32
MAGIC = 0x384F                  # 'O','8'
DRV_VER = 4
MAX_SIZE = 40 * 1024            # DRV_MAX_KB in kernel/driver.inc
CLASSES = {1: "sound", 2: "disk", 3: "debug"}


def fail(msg: str) -> None:
    print(f"os88drv: error: {msg}", file=sys.stderr)
    sys.exit(1)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Validate and stamp an os8088 driver image.")
    ap.add_argument("input", metavar="DRIVER.bin")
    ap.add_argument("-o", "--output", metavar="OUT.drv", required=True)
    args = ap.parse_args()

    try:
        with open(args.input, "rb") as f:
            data = f.read()
    except OSError as e:
        fail(f"cannot read {args.input}: {e}")

    if len(data) < HDR:
        fail(f"{args.input}: too short for a header ({len(data)} bytes)")
    if len(data) > MAX_SIZE:
        fail(f"{args.input}: {len(data)} bytes; the kernel claims at most "
             f"{MAX_SIZE}")

    magic, ver, cls, link, entry, image, rsv = struct.unpack_from(
        "<HBBHHHH", data, 0)
    if magic != MAGIC:
        fail(f"{args.input}: magic {magic:#06x}, not 'O8'")
    if ver != DRV_VER:
        fail(f"{args.input}: header version {ver}; a driver is {DRV_VER} "
             f"(3 is an application package - wrong macro?)")
    if cls not in CLASSES:
        fail(f"{args.input}: class {cls} is not one the kernel knows "
             f"({', '.join(f'{k}={v}' for k, v in CLASSES.items())})")
    if link != 0:
        fail(f"{args.input}: link base {link:#06x}, not 0")
    if image != len(data):
        fail(f"{args.input}: header says {image} bytes, file is {len(data)} "
             f"- a driver's data ships INSIDE its image, so the two must "
             f"match exactly")
    if not HDR <= entry < len(data):
        fail(f"{args.input}: entry {entry:#06x} is outside the image")
    if data[12:15] != b"\xFF\xD5\xCB":
        fail(f"{args.input}: no `call bp / retf` dispatcher at +12")

    name = data[16:32].split(b"\0", 1)[0]
    if not name:
        fail(f"{args.input}: empty name")
    if any(b < 0x20 or b > 0x7E for b in name):
        fail(f"{args.input}: name is not printable ASCII")

    try:
        with open(args.output, "wb") as f:
            f.write(data)
    except OSError as e:
        fail(f"cannot write {args.output}: {e}")

    print(f"os88drv: '{name.decode()}' class={CLASSES[cls]} "
          f"entry=+{entry:#06x} image={len(data)} -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
