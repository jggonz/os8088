#!/usr/bin/env python3
"""Screendump the VGA/CGA display as a PNG, optionally cropped and zoomed.

    python3 tools/shot.py <socket> <out.png> [--crop X,Y,W,H] [--zoom N]

QMP's own `screendump` writes NetPBM, which nothing in a headless container
can open - there is no Pillow here and this project deliberately has no
dependencies. So this is the P6 reader plus the same stdlib PNG writer
tools/hercshot.py already uses, wrapped around the dump so one command gets
you something you can actually look at.

The crop is the point, not a convenience. A change worth testing is often a
few pixels - one revealed Minesweeper cell, a greyed menu item, a selection
band moving one row - and in a full 640x480 dump all of those read as
"nothing happened", which is how a working click gets diagnosed as a lost
one. Crop to the control and zoom it up before concluding anything:

    python3 tools/shot.py build/qmp.sock /tmp/cell.png --crop 180,150,64,64 --zoom 6

Two things about the coordinates catch people out. The dump is the HOST
scanout, so on the CGA path (`make test VIDEO=cga`) it comes back 640x400 -
QEMU line-doubles 640x200 - and every Y here is therefore twice the kernel's
own Y, while X matches. VGA is 640x480 and 1:1. And Hercules never appears
in a screendump at all: that framebuffer is guest RAM the VGA device has
never heard of, so this tool will happily dump a black or stale VGA screen
and tell you nothing. Use tools/hercshot.py there (docs/HERCULES-TESTING.md).
"""
import os
import struct
import subprocess
import sys
import zlib


def read_ppm(path):
    """Parse a binary P6 NetPBM into (w, h, RGB bytes)."""
    data = open(path, "rb").read()
    fields, pos = [], 0
    # magic, width, height, maxval - any may be preceded by a # comment line.
    while len(fields) < 4:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    if fields[0] != b"P6":
        raise SystemExit(f"not a binary PPM: {fields[0]!r}")
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[pos + 1:pos + 1 + w * h * 3]


def png(path, w, h, pix):
    """Write 8-bit truecolour PNG, no dependencies."""
    raw = b"".join(b"\x00" + pix[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def opt(name, default=None):
    for a in sys.argv[1:]:
        if a.startswith(name + "="):
            return a.split("=", 1)[1]
        if a == name:                       # --crop X,Y,W,H (separate token)
            i = sys.argv.index(a)
            if i + 1 < len(sys.argv):
                return sys.argv[i + 1]
    return default


def main():
    args = []
    skip = False
    for a in sys.argv[1:]:
        if skip:
            skip = False
            continue
        if a.startswith("--"):
            skip = "=" not in a
            continue
        args.append(a)
    if len(args) < 2:
        raise SystemExit(__doc__)
    sock, out = args[0], args[1]

    # screendump resolves relative paths against QEMU's cwd, not ours.
    ppm = os.path.abspath(out + ".ppm")
    subprocess.run([sys.executable, "tools/qmp.py", sock,
                    f'screendump "{ppm}"'], check=True)
    w, h, pix = read_ppm(ppm)
    os.unlink(ppm)

    full = f"{w}x{h}"
    crop = opt("--crop")
    zoom = int(opt("--zoom", "1"))
    if crop:
        cx, cy, cw, ch = (int(v) for v in crop.replace(",", " ").split())
        cx, cy = max(0, cx), max(0, cy)
        cw, ch = max(0, min(cw, w - cx)), max(0, min(ch, h - cy))
        if not cw or not ch:
            raise SystemExit(f"crop {crop} lies outside the {full} screen")
        rows = []
        for y in range(cy, cy + ch):
            row = pix[(y * w + cx) * 3:(y * w + cx + cw) * 3]
            if zoom > 1:                    # nearest-neighbour: keep pixels crisp
                row = b"".join(row[i * 3:i * 3 + 3] * zoom for i in range(cw))
            rows.extend([row] * zoom)
        pix, w, h = b"".join(rows), cw * zoom, ch * zoom

    png(out, w, h, pix)
    ink = 100.0 * sum(1 for i in range(0, len(pix), 3) if pix[i:i + 3] != b"\xff" * 3) \
        / max(1, w * h)
    note = f" (cropped from {full})" if crop else ""
    print(f"{out}: {w}x{h}{note}, {ink:.1f}% non-white")
    return 0


if __name__ == "__main__":
    sys.exit(main())
