#!/usr/bin/env python3
"""dmg-acid2 through RealDMG's renderer, against its reference, pixel for pixel.

    python3 tests/rdgcore/rdgacid.py [--save PREFIX]

dmg-acid2 (Matt Currie, MIT) draws a face whose every feature is one PPU
rule - the window's line counter, the ten-sprite limit, sprite priority by X
then OAM index, X and Y flip, 8x16 tiles, the BG's colour-0 priority, the
tile-data and map selects. tools/getgbtest.py fetches it with its reference
picture; this boots it bare (tests/rdgcore/rdghost.asm, MODE 4), takes the
frame rdg_render composed (apps/realdmg/rdgppu.inc), and compares all 23,040
pixels with the reference's four greys, darkest last. Exit 0 if every pixel
agrees - and if a core with LDH (n),A broken does not, the negative control.
"""
import argparse
import os
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tests", "rdgcore"))
import rdgimg  # noqa: E402
import rdgshot  # noqa: E402

LDH_NOP = "E000:8A3446FFE2"          # mov dh,[si] / inc si / jmp dx


def png_grey(path):
    """A PNG's pixels as a list of RGB tuples, with zlib alone (8-bit RGB or
    RGBA, or palette or grey at any depth, non-interlaced)."""
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n"
    i, idat, plte = 8, b"", None
    while i < len(d):
        n = int.from_bytes(d[i:i + 4], "big")
        t, body = d[i + 4:i + 8], d[i + 8:i + 8 + n]
        if t == b"IHDR":
            w, h = int.from_bytes(body[0:4], "big"), int.from_bytes(body[4:8], "big")
            depth, ctype = body[8], body[9]
        elif t == b"PLTE":
            plte = [tuple(body[k:k + 3]) for k in range(0, len(body), 3)]
        elif t == b"IDAT":
            idat += body
        i += 12 + n
    raw = zlib.decompress(idat)
    bpp = {2: 3, 6: 4, 3: 1, 0: 1}[ctype]
    assert depth == 8 or ctype in (0, 3), (depth, ctype)
    stride = (w * bpp * depth + 7) // 8
    out, prev = [], bytearray(stride)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b, c = prev[x], prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        if depth < 8:                       # unpack sub-byte samples
            per = 8 // depth
            line = bytearray((line[x // per] >> (8 - depth * (x % per + 1))) & ((1 << depth) - 1)
                             for x in range(w))
            if ctype == 0:
                line = bytearray(v * 255 // ((1 << depth) - 1) for v in line)
        for x in range(w):
            px = line[x * bpp:x * bpp + bpp]
            out.append(plte[px[0]] if ctype == 3 else (px[0],) * 3 if ctype == 0 else tuple(px[:3]))
    return w, h, out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--save", metavar="PREFIX", help="write PREFIX-core.pgm and PREFIX-diff.pgm")
    a = ap.parse_args()
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools/getgbtest.py")],
                          stdout=subprocess.DEVNULL)
    g = lambda n: subprocess.check_output([sys.executable, os.path.join(ROOT, "tools/getgbtest.py"),
                                           "--path", n], text=True).strip()
    w, h, ref = png_grey(g("dmg-acid2-dmg.png"))
    greys = sorted(set(ref), key=lambda c: -sum(c))
    assert (w, h) == (160, 144) and len(greys) <= 4, (w, h, greys)
    want = [greys.index(c) for c in ref]
    img = os.path.join(ROOT, "build", "realdmg", "acid.img")
    out = os.path.join(ROOT, "build", "realdmg")
    core, host = rdgimg.build_core(False, out), rdgimg.build_host(out)

    def frame(c, jit=True):
        rdgimg.make_image(g("dmg-acid2.gb"), img, mode="idle", frames=120, core=c, host=host,
                         jit=jit)
        return rdgshot.core_frame(rdgshot.capture(img, 180)[1])
    got = frame(core)
    bad = [i for i in range(160 * 144) if got[i] != want[i]]
    # the negative control: LDH (n),A made a no-op must lose the picture
    try:
        ctl = frame(rdgimg.patch_core(core, [LDH_NOP]), jit=False)
    except SystemExit:                  # it never reached a frame at all
        ctl = None
    if ctl is not None and sum(1 for i in range(160 * 144) if ctl[i] != want[i]) == 0:
        print("rdgacid: control: LDH (n),A broken and the picture still matches - the gate cannot go red")
        return 1
    if a.save:
        with open(a.save + "-core.pgm", "wb") as f:
            f.write(b"P5 160 144 255\n" + bytes(rdgshot.SHADE[p] for p in got))
        with open(a.save + "-diff.pgm", "wb") as f:
            f.write(b"P5 160 144 255\n" + bytes(0 if i in set(bad) else 255 for i in range(23040)))
    if bad:
        ys = sorted(set(i // 160 for i in bad))
        print("rdgacid: %d pixels differ, on %d lines (first %d, last %d)" % (len(bad), len(ys), ys[0], ys[-1]))
        return 1
    print("rdgacid: dmg-acid2 matches its reference, all 23,040 pixels")
    return 0


if __name__ == "__main__":
    sys.exit(main())
