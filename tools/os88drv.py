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
  +3   class                     1 = sound; must be one this tool knows.
                                 4 = an overlay, which is a driver's own
                                 loadable half and not a kernel class
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
import os
import struct
import sys

HDR = 32
# +31 is the bss size in PARAGRAPHS. A byte, and it is free because the name
# at +16 is capped at 15 characters - so every driver ever built has a NUL
# there, and 0 reads as "no bss", which is what makes this compatible with
# every one of them. The kernel over-claims by this many paragraphs before it
# has read the header, so raising it costs every driver load on every machine.
BSS_MAX_PARA = 255
MAGIC = 0x384F                  # 'O','8'
DRV_VER = 4
MAX_SIZE = 40 * 1024            # DRV_MAX_KB in kernel/driver.inc
# 0x40 is NOT a kernel driver class: it is a driver's own loadable half
# (OS88_OVERLAY, SPEC.md 52.11), stamped by this tool because the header, the
# dispatcher and the one-claim load discipline are identical. The kernel never
# loads one - its OWNER does - so drv_check never sees it and would refuse it.
CLASSES = {1: "sound", 2: "disk", 3: "debug", 4: "net", 5: "file",
           0x40: "overlay"}


def fail(msg: str) -> None:
    print(f"os88drv: error: {msg}", file=sys.stderr)
    sys.exit(1)


def image_unwrap(blob: bytes) -> bytes:
    """A shipped `.drv` back to the IMAGE drv_load will run.

    os88pkg.image_unwrap's twin, and the same rule decides which of the two a
    caller wants: the FILE is what lands on a floppy, the IMAGE is what the
    size field, the bss arithmetic and an assembly are about. A compressed
    driver is a plain 'CZ' file (SPEC.md 20.13.3.1) - the header inside the
    stream with everything else, the kernel's transparent read expanding it
    into the claim drv_load cut from the directory hint - so this is
    os88lz.cz_unwrap and nothing of its own. Returns `blob` unchanged when it
    is not compressed.
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import os88lz
    return os88lz.cz_unwrap(blob)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Validate and stamp an os8088 driver image.")
    ap.add_argument("input", metavar="DRIVER.bin")
    ap.add_argument("-o", "--output", metavar="OUT.drv", required=True)
    ap.add_argument("--compress", metavar="FMT", nargs="?", const="lz4",
                    choices=("lz4", "lzb", "none"), default=None,
                    help="write the driver as a 'CZ' file (SPEC.md 20.13.3.1): "
                         "the whole image, header included, inside the "
                         "container. The kernel's transparent read expands it "
                         "into the claim drv_load cuts from the directory "
                         "hint, and nothing in the driver loader knows it was "
                         "ever compressed")
    ap.add_argument("--compress-if", metavar="FMT", dest="compress_if",
                    choices=("lz4", "lzb", "none"), default=None,
                    help="...the same thing SOFTLY: compress when it pays, and "
                         "write the driver PLAIN with a line saying why when "
                         "it does not. What a fleet-wide `make PKGZ=` needs, "
                         "where --compress above stays strict")
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

    if data[31] != 0:
        fail(f"{args.input}: byte +31 is {data[31]:#04x} and must be zero - it "
             f"is the bss field this tool fills, and the name above is capped "
             f"at 15 characters so it is always free")

    # --- TRAILING ZEROS BECOME A BSS (docs/plans/O88-COMPRESSION-PLAN.md 12.6) -----
    # drivers/os88drv.inc used to say "there is no bss: a driver's zeroed data
    # is written as `db 0` and ships on the floppy". Measured, that is 6,722
    # bytes of trailing zeros across the twelve shipped drivers - ether.drv
    # alone has a single 4,066-byte run - and every one of them is a byte read
    # off a floppy to be told it is zero.
    #
    # It costs the driver AUTHOR nothing: the stripping is here, the zeroing is
    # the loader's, and no driver source changes. A driver built before this
    # has byte +31 = 0, which reads as "no bss" and is exactly right.
    body = data.rstrip(b"\0")
    keep = max(len(body), entry + 1, HDR)
    # ...BUT ONLY AS FAR AS THE KB BOUNDARY THE FOOTPRINT NEEDS (SPEC.md
    # 51.1.2). The kernel claims a driver from the size the DIRECTORY reports
    # and cannot know the bss until it has read the header out of that claim,
    # so a strip that takes the image below its own KB rung leaves the claim
    # short by the difference. Padding the claim instead is what shipped, and
    # `drv_bss` handing the pad back with a shrink is what could not return
    # it: the tail of a top-down claim is walled in above the image. Measured
    # on a machine wanting every driver, 14,336 bytes stranded and the largest
    # free run 375.0K -> 361.0K.
    #
    # So the strip stops at the rung. What that costs is zeros back on the
    # floppy - and on a packed disk it costs almost nothing, because a run of
    # zeros is what LZ4 is best at. That is the half which was not true when
    # the stripping was written: it was measured against an UNCOMPRESSED disk,
    # where 6,722 bytes of trailing zeros were 6,722 bytes of floppy.
    rung = -(-len(data) // 1024) * 1024         # the driver's real footprint,
    if keep <= rung - 1024:                     # rounded up to whole KB
        keep = rung - 1023                      # the least that reaches it
    # ROUND DOWN. The bss is measured in paragraphs, and rounding the count UP
    # takes bytes that are not zero with it - sound.drv has nine trailing zeros
    # and the first version stripped sixteen, seven of them real code.
    para = (len(data) - keep) // 16
    if para > BSS_MAX_PARA:
        # ...cap it rather than refuse: the loader over-claims by a fixed
        # BSS_MAX_PARA before it has read the header to learn the real figure,
        # and a driver that wanted more would need that constant raised on
        # every machine.
        para = BSS_MAX_PARA
    keep = len(data) - para * 16
    stripped = len(data) - keep
    data = bytearray(data[:keep])
    struct.pack_into("<H", data, 8, keep)        # +8 is the FILE, still
    data[31] = para
    data = bytes(data)

    name = data[16:32].split(b"\0", 1)[0]
    if not name:
        fail(f"{args.input}: empty name")
    if any(b < 0x20 or b > 0x7E for b in name):
        fail(f"{args.input}: name is not printable ASCII")

    zfmt, soft = args.compress, False
    if args.compress and args.compress_if:
        fail("--compress and --compress-if are the same decision taken two "
             "ways; give one")
    if not zfmt and args.compress_if:
        zfmt, soft = args.compress_if, True
    if zfmt and zfmt != "none":
        # A 'CZ' FILE, header and all (SPEC.md 20.13.3.1). The driver loader
        # claims from the directory entry's size before it reads, and the
        # directory hint says what a 'CZ' file BECOMES (20.14.1), so the
        # read expands it straight into that claim and drv_check sees a plain
        # driver. Nothing in kernel/driver.inc knows the file was ever
        # compressed - which is the whole of what this format buys over the
        # in-header one it replaced.
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import os88lz
        fid = os88lz.LZ4 if zfmt == "lz4" else os88lz.LZB
        out, did = os88lz.cz_wrap(data, fid)
        if not did:
            msg = (f"{zfmt} does not make this driver smaller. Ship it "
                   "uncompressed")
            if not soft:
                fail(msg)
            print(f"os88drv: NOT compressed: {msg}")
        else:
            print(f"os88drv: compressed {zfmt}: image {len(data)} -> "
                  f"file {len(out)} bytes ({len(out) / len(data):.1%})")
            data = bytes(out)

    try:
        with open(args.output, "wb") as f:
            f.write(data)
    except OSError as e:
        fail(f"cannot write {args.output}: {e}")

    print(f"os88drv: '{name.decode()}' class={CLASSES[cls]} "
          f"entry=+{entry:#06x} "
          f"image={image} file={len(data)}"
          + (f" (+{para * 16} bss, {stripped} trailing zeros off the disk)"
             if para else "")
          + f" -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
