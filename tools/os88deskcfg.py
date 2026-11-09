#!/usr/bin/env python3
"""os88deskcfg - a SYSTEM.CFG that carries DESKTOP SHORTCUTS and no setting.

    python3 tools/os88deskcfg.py -o OUT --adapter vga|cga|herc \\
        --glyph-from build/video.o88 [--vol 2] [--folder MEDIA] FILE...

The Video Player's demo disks (SPEC.md 98.5) put a shortcut on the desktop
for every demo video, in the owner's layout, and a desktop shortcut lives in
nowhere but SYSTEM.CFG's trailer (SPEC.md 26.8.6). Those disks carry no
SETTING, so this writes the trailer behind a settings head of exactly one
record:

  DA = 0  (SPEC.md 51.5.4). A file with no 'DA' counts every driver row it
          finds ON as answered by the user - and with no 'DW' either, the
          rows found on are the live ones, the boot's sniffed sound card and
          hard disk - so they would be saved as the user's ticks. With 'DA'
          0 no row is answered and every want stays the machine's, exactly
          as with no file at all. Every other key is absent, and an absent
          key is its default (51.5 rule 4).

EACH ROW IS WHAT A DRAG OUT OF THE FOLDER'S DISK WINDOW WRITES (SPEC.md
26.8.1, 26.8.4): kind 0 (a document), +3 the '\\' sc_m_new leaves there, the
path below the root, the full 8.3 name as the caption, and the Disk window's
own 16x16 picture - SPEC.md 54.3's page with the owning package's 8x8 glyph
inset at x=4..11, y=5..12: the glyph it SHIPS (54.3.2) or its icon reduced
(os88mini.reduce8, the kernel's assoc_reduce). Every row is PLACED
(DSL_PIN), on a cell of the adapter's grid (26.9.1).

THE LAYOUT (the owner's, off a VGA screenshot): the files, in the order
given, fill the LEFTMOST column top to bottom; one column is left empty; the
rest fill the columns to the right of it, top to bottom. The gap is only
for a set that fits two columns: one that needs three (the CGA's eleven on
four rows) runs on with none, since a gap could part only two of them.
Cell 0 is the top of
the RIGHTMOST column and cells run down a column and then a column to the
left (26.9.1), so column c from the right, row r, is cell c * rows + r. The
rightmost column is the drives' and is never used, nor is the one beside it.

The grid is SPEC.md 26.9.1's table, kern_big, the dock at the bottom; on a
screen of another shape the kernel's first reflow (26.9.3) forgets any cell
that screen has not got and the shortcut flows to the first free one, so a
wrong guess costs the layout and not the shortcut.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88geom  # noqa: E402
import os88mini  # noqa: E402
import os88pkg   # noqa: E402

# the kernel's own numbers, through os88geom's checked mirror (kern_big's:
# SC_REC and SC_MAX are OS88_SHORTCUTS', which kern_small has not got)
SC_REC, SC_MAX, DSL_PIN, DSL_SLOT, DVOL_MAX = (
    os88geom.SC_REC, os88geom.SC_MAX, os88geom.DSL_PIN, os88geom.DSL_SLOT,
    os88geom.DVOL_MAX)
# SPEC.md 26.8.1 / 26.8.6, the row's layout and the footer
SC_VER, DESK_SC0 = 2, 9
SC_R_VOL, SC_R_SLOT, SC_R_KIND, SC_R_MARK = 0, 1, 2, 3
SC_R_PATH, SC_PATHMAX, SC_R_CAP, SC_CAPMAX, SC_R_HDR, SC_R_BODY = \
    4, 45, 49, 13, 62, 64
SC_KIND_DOC = 0

# SPEC.md 26.9.1's grid on kern_big: rows, columns
GRIDS = {"vga": (7, 6), "herc": (5, 7), "ega": (5, 6), "cga": (4, 6)}

# SPEC.md 54.3: the document page, 16 mask words then 16 data words, bit 15
# the leftmost pixel - tests/unit/t_assocpage.py's GOLDEN, which the kernel's
# assoc_compose is held to
PAGE = [
    0x3FE0, 0x3FF0, 0x3FF8, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC,
    0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC,
    0x3FE0, 0x2030, 0x2028, 0x203C, 0x2004, 0x2004, 0x2004, 0x2004,
    0x2004, 0x2004, 0x2004, 0x2004, 0x2004, 0x2004, 0x2004, 0x3FFC,
]


def fail(msg):
    sys.exit("os88deskcfg: " + msg)


def doc_picture(glyph):
    """54.3's page with an 8x8 glyph ORed into data rows 5..12, bit 7 at x=4."""
    words = list(PAGE)
    for y, g in enumerate(glyph):
        words[16 + 5 + y] |= g << 4
    return b"".join(struct.pack("<H", w) for w in words)


def package_glyph(path):
    """The glyph the Disk window insets for a package's documents."""
    image = os88pkg.image_unwrap(open(path, "rb").read())
    if not image[3] & 1:
        fail("%s carries no icon, so its documents have no glyph" % path)
    if image[3] & os88mini.GLYPH_BIT:   # os88mini.shipped_glyph's rule
        base = os88mini.HDR + os88mini.ICON + os88mini.ASSOC
        g = list(image[base:base + 8])
        if any(g):
            return g
    base = os88mini.HDR + 32            # ...else os88mini.data_rows' words
    rows = [int.from_bytes(image[base + 2 * y:base + 2 * y + 2], "little")
            for y in range(16)]
    return os88mini.reduce8(rows)


def sc_row(vol, cell, kind, mark, path, caption, picture):
    """One 128-byte row, 26.8.1, as it lies in the file."""
    p, c = path.encode("ascii"), caption.encode("ascii")
    if not 0 <= vol < DVOL_MAX:
        fail("volume %d is not a volume" % vol)
    if len(p) >= SC_PATHMAX or len(c) >= SC_CAPMAX:
        fail("%s: the path or the caption is too long for a row" % path)
    if not 0 <= cell <= DSL_SLOT or len(picture) != 64:
        fail("%s: bad cell or picture" % path)
    row = bytearray(SC_REC)
    row[SC_R_VOL] = vol
    row[SC_R_SLOT] = DSL_PIN | cell     # a shortcut is always placed
    row[SC_R_KIND] = kind
    row[SC_R_MARK] = mark
    row[SC_R_PATH:SC_R_PATH + len(p)] = p
    row[SC_R_CAP:SC_R_CAP + len(c)] = c
    row[SC_R_HDR:SC_R_HDR + 2] = bytes([1, 16])
    row[SC_R_BODY:SC_R_BODY + 64] = picture
    return bytes(row)


def trailer(rows):
    """26.8.6: the rows, no drive placed, the footer read from the end."""
    if len(rows) > SC_MAX:
        fail("%d shortcuts: the kernel keeps %d" % (len(rows), SC_MAX))
    return b"".join(rows) + b"\xff" * DESK_SC0 + \
        b"SC" + bytes([SC_VER, len(rows)])


def head_unanswered():
    """The settings head: generation 3, 'DA' ver 1 = 0, the terminator."""
    return b"O88CFG\0\0" + struct.pack("<H", 3) + \
        b"DA" + bytes([1, 2]) + struct.pack("<H", 0) + b"\0\0"


def cells(n, rows, cols):
    """The owner's layout: the leftmost column, a column empty, the rest -
    the gap only when the rest is ONE column; a set that needs three runs
    on unbroken, since a gap between two of them and not the other two
    reads as a mistake (the owner, on the CGA's eleven)."""
    if n <= 2 * rows:
        order = [cols - 1] + list(range(cols - 3, 1, -1))   # never 0 or 1
    else:
        order = list(range(cols - 1, 1, -1))
    out = []
    for c in order:
        for r in range(rows):
            if len(out) < n:
                out.append(c * rows + r)
    if len(out) < n:
        fail("%d shortcuts do not fit a %d x %d grid this way" %
             (n, rows, cols))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--adapter", required=True, choices=sorted(GRIDS))
    ap.add_argument("--glyph-from", required=True,
                    help="the package that opens the files, for its glyph")
    ap.add_argument("--vol", type=int, default=2, help="the volume (2 = C:)")
    ap.add_argument("--folder", default="MEDIA")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    rows, cols = GRIDS[a.adapter]
    names = [os.path.basename(f).upper() for f in a.files]
    pic = doc_picture(package_glyph(a.glyph_from))
    out = [sc_row(a.vol, cell, SC_KIND_DOC, ord("\\"),
                  a.folder + "\\" + name, name, pic)
           for name, cell in zip(names, cells(len(names), rows, cols))]
    data = head_unanswered() + trailer(out)
    if len(data) >= 4096:
        fail("%d bytes: ovl_sc_load refuses a file of 4KB" % len(data))
    with open(a.output, "wb") as f:
        f.write(data)
    print("os88deskcfg: %s, %d shortcuts on a %d x %d grid, %d bytes" %
          (a.output, len(out), rows, cols, len(data)))


if __name__ == "__main__":
    main()
