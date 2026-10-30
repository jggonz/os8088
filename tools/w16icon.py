#!/usr/bin/env python3
"""w16icon.py - WIN16's own system icons and bitmaps (SPEC.md 124.18.4,
124.22.7; WIN16-PLAN.md S12, WIN16-ANY-PLAN.md S17).

`LoadIcon(NULL, IDI_HAND)`, a message box's icon and `LoadBitmap(NULL,
OBM_*)` are Windows' own art on Windows, and Microsoft's drawings never enter
this tree. WIN16 draws its own: apps/win16/icons/*.txt, one drawing a file,
a character a pixel - `.` see-through (icons only), a hex digit a colour in
Windows' palette order. An icon is 32 x 32; a bitmap is any size, and the
size Windows 3.0's VGA driver gave it. THE ART IS THE SOURCE, and this tool
builds it into the part WIN16.O88 carries.

    python3 tools/w16icon.py build OUT.bin     apps/win16/icons/ -> the part
    python3 tools/w16icon.py check             (the fast row's half)

THE PART (SPEC.md 124.18.4), every word little-endian:

    +0  dw count (5)
    +2  dw off[count]       each icon, from the part's start, in IDI order:
                            IDI_APPLICATION (32512) .. IDI_ASTERISK (32516)
        then each icon as a Windows 3.x icon image - a BITMAPINFOHEADER (40
        bytes, height doubled), sixteen RGBQUADs, the XOR bits at 4 bits a
        pixel and the AND mask at 1, rows bottom up and padded to a dword -
        so a system icon is drawn by the routine that draws a program's
        own (w16_icodraw).
    +2+2*count  dw nbitmaps, then (dw OBM id, dw off) each, ids ascending
        then each bitmap as a packed DIB - a BITMAPINFOHEADER, its palette
        and its bits, rows bottom up - at 1 bit a pixel when the drawing is
        black and white (0 and f) and 4 otherwise, so `LoadBitmap(NULL,
        OBM_*)` is made by the routine that makes a program's own.
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS = os.path.join(ROOT, "apps", "win16", "icons")
ORDER = [(32512, "application"), (32513, "hand"), (32514, "question"),
         (32515, "exclamation"), (32516, "asterisk")]
SIZE = 32
# Windows' system bitmaps WIN16 draws (OBM_*), with the file each is in.
# The rest of VGA.DRV's thirty-two join as a target needs them.
BITMAPS = [(32762, "obm_old_lfarrow"), (32763, "obm_old_rgarrow"),
           (32764, "obm_old_dnarrow"), (32765, "obm_old_uparrow")]
# Windows' sixteen, in its palette's order (the VGA driver's): R, G, B
PAL = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128),
       (128, 0, 128), (0, 128, 128), (192, 192, 192), (128, 128, 128),
       (255, 0, 0), (0, 255, 0), (255, 255, 0), (0, 0, 255), (255, 0, 255),
       (0, 255, 255), (255, 255, 255)]


def read_art(path, want):
    """the drawing: 32 rows of 32 values, None = see-through"""
    rows, ident = [], None
    for n, line in enumerate(open(path), 1):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        if line.startswith("icon "):
            ident = int(line.split()[1])
            continue
        if len(line) != SIZE:
            sys.exit("w16icon: %s:%d: a row is %d characters, not %d"
                     % (path, n, len(line), SIZE))
        try:
            rows.append([None if c == "." else int(c, 16) for c in line])
        except ValueError:
            sys.exit("w16icon: %s:%d: `.` or a hex digit only" % (path, n))
    if ident != want:
        sys.exit("w16icon: %s: `icon %s` expected" % (path, want))
    if len(rows) != SIZE:
        sys.exit("w16icon: %s: %d rows, not %d" % (path, len(rows), SIZE))
    return rows


def read_bitmap(path, want):
    """a bitmap's drawing: rows of equal length, a hex digit a pixel"""
    rows, ident = [], None
    for n, line in enumerate(open(path), 1):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        if line.startswith("bitmap "):
            ident = int(line.split()[1])
            continue
        try:
            rows.append([int(c, 16) for c in line])
        except ValueError:
            sys.exit("w16icon: %s:%d: a hex digit a pixel" % (path, n))
        if len(rows[-1]) != len(rows[0]):
            sys.exit("w16icon: %s:%d: a row is %d wide, the first %d"
                     % (path, n, len(rows[-1]), len(rows[0])))
    if ident != want:
        sys.exit("w16icon: %s: `bitmap %s` expected" % (path, want))
    if not rows:
        sys.exit("w16icon: %s: no rows" % path)
    return rows


def dib(rows):
    """a packed DIB: 1 bit a pixel when black and white, else 4"""
    w, h = len(rows[0]), len(rows)
    mono = all(v in (0, 15) for row in rows for v in row)
    bpp = 1 if mono else 4
    stride = ((w * bpp + 31) // 32) * 4
    bits = b""
    for row in reversed(rows):
        rb = bytearray(stride)
        for x, v in enumerate(row):
            if mono:
                if v:
                    rb[x >> 3] |= 0x80 >> (x & 7)
            else:
                rb[x >> 1] |= v << (0 if x & 1 else 4)
        bits += bytes(rb)
    pal = [PAL[0], PAL[15]] if mono else PAL
    ct = b"".join(struct.pack("<BBBB", b, g, r, 0) for r, g, b in pal)
    return (struct.pack("<IiiHHIIiiII", 40, w, h, 1, bpp, 0, len(bits),
                        0, 0, 0, 0) + ct + bits)


def image(rows):
    xor, andm = b"", b""
    for row in reversed(rows):                  # bottom up, as a DIB is
        xb = bytearray(SIZE // 2)
        ab = bytearray(SIZE // 8)
        for x, v in enumerate(row):
            if v is None:
                ab[x >> 3] |= 0x80 >> (x & 7)
            else:
                xb[x >> 1] |= v << (0 if x & 1 else 4)
        xor += bytes(xb)
        andm += bytes(ab)
    ct = b"".join(struct.pack("<BBBB", b, g, r, 0) for r, g, b in PAL)
    return (struct.pack("<IiiHHIIiiII", 40, SIZE, 2 * SIZE, 1, 4, 0,
                        len(xor) + len(andm), 0, 0, 0, 0) + ct + xor + andm)


def part(where=ICONS):
    imgs = [image(read_art(os.path.join(where, n + ".txt"), i)) for i, n in ORDER]
    bmps = [dib(read_bitmap(os.path.join(where, n + ".txt"), i))
            for i, n in sorted(BITMAPS)]
    off = 2 + 2 * len(imgs) + 2 + 4 * len(bmps)
    head = struct.pack("<H", len(imgs))
    for img in imgs:
        head += struct.pack("<H", off)
        off += len(img)
    head += struct.pack("<H", len(bmps))
    for (i, _), bmp in zip(sorted(BITMAPS), bmps):
        head += struct.pack("<HH", i, off)
        off += len(bmp)
    return head + b"".join(imgs) + b"".join(bmps)


def check(where=ICONS):
    """0 when every drawing reads and the set is exactly ORDER's"""
    have = sorted(f for f in os.listdir(where) if f.endswith(".txt"))
    want = sorted(n + ".txt" for _, n in ORDER + BITMAPS)
    if have != want:
        print("w16icon: apps/win16/icons/ holds %s, not %s" % (have, want))
        return 1
    part(where)
    return 0


def main(a):
    if a[:1] == ["build"] and len(a) == 2:
        open(a[1], "wb").write(part())
    elif a == ["check"]:
        sys.exit(check())
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
