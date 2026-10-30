#!/usr/bin/env python3
"""w16icon.py - WIN16's own system icons (SPEC.md 124.18.4, WIN16-PLAN.md S12).

`LoadIcon(NULL, IDI_HAND)` and a message box's icon are Windows' own art on
Windows, and Microsoft's drawings never enter this tree. WIN16 draws its own:
apps/win16/icons/*.txt, one 32 x 32 drawing a file, a character a pixel -
`.` see-through, a hex digit a colour in Windows' palette order. THE ART IS
THE SOURCE, and this tool builds it into the part WIN16.O88 carries.

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
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS = os.path.join(ROOT, "apps", "win16", "icons")
ORDER = [(32512, "application"), (32513, "hand"), (32514, "question"),
         (32515, "exclamation"), (32516, "asterisk")]
SIZE = 32
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
    off = 2 + 2 * len(imgs)
    head = struct.pack("<H", len(imgs))
    for img in imgs:
        head += struct.pack("<H", off)
        off += len(img)
    return head + b"".join(imgs)


def check(where=ICONS):
    """0 when every drawing reads and the set is exactly ORDER's"""
    have = sorted(f for f in os.listdir(where) if f.endswith(".txt"))
    want = sorted(n + ".txt" for _, n in ORDER)
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
