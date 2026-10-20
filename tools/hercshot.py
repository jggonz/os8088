#!/usr/bin/env python3
"""Reconstruct a Hercules (or CGA) framebuffer out of guest RAM as a PNG.

    python3 tools/hercshot.py <socket> <linear-addr> <out.png> [--cga]

QEMU has no Hercules card, so the 720x348 renderer cannot be screendumped
there (MartyPC models one and `os88marty.py shot` reads it back directly;
86Box has no automation socket - docs/HERCULES-TESTING.md).
This reads the framebuffer straight out of guest memory instead and applies
the same banked layout the kernel writes with (SPEC.md 39.3), which verifies
everything except the 6845 programming and the physical display: the stride,
the bank arithmetic, the wrap out of the last bank, the colour reduction and
every glyph, icon and window the UI drew.

Pair it with the HERCSEG hook, which relocates the framebuffer into spare
RAM (B0000 is unmapped under QEMU and silently swallows every write):

    make test VIDEO=herc HERCSEG=0x7000
    python3 tools/hercshot.py build/qmp.sock 0x70000 build/herc.png

A wrong stride shows up as a picture that shears sideways; wrong bank
arithmetic shears it into four interleaved combs. Both are unmistakable.
"""
import os
import struct
import subprocess
import sys
import zlib


def png(path, w, h, rows):
    """Write a 1-bit-per-pixel image out as greyscale PNG, no dependencies."""
    raw = b"".join(b"\x00" + bytes(255 if b else 0 for b in row) for row in rows)

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    cga = "--cga" in sys.argv
    sock, addr, out = args[0], int(args[1], 0), args[2]

    w, h, stride, banks = (640, 200, 80, 2) if cga else (720, 348, 90, 4)
    size = banks * 0x2000

    dump = os.path.abspath(out + ".bin")    # pmemsave resolves against
    subprocess.run([sys.executable, "tools/qmp.py", sock,   # QEMU's own cwd
                    f'pmemsave {addr} {size} "{dump}"'], check=True)
    fb = open(dump, "rb").read()

    rows = []
    for y in range(h):
        base = (y % banks) * 0x2000 + (y // banks) * stride
        row = bytearray()
        for x in range(w):
            byte = fb[base + (x >> 3)]
            row.append((byte >> (7 - (x & 7))) & 1)
        rows.append(row)

    png(out, w, h, rows)
    lit = sum(sum(r) for r in rows)
    print(f"{out}: {w}x{h}, {lit} lit pixels of {w*h} "
          f"({100.0*lit/(w*h):.1f}%)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
