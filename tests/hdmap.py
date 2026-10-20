#!/usr/bin/env python3
"""The disk tool's drive map: where on the drive each slot IS (SPEC.md 52.2.8).

    python3 tests/hdmap.py

The fixture is the shape that asked for the map, on bigvol's 321MB XT-IDE
drive (654/16/63): TWO 32MB PARTITIONS IN THE MIDDLE, with free space either
side of them - slot 1 an empty FAT16 extent at 65,520 (Not Formatted), slot 2
a FAT16 volume at 131,040, slots 3 and 4 free. Every column below is worked
out here from the table and the drive, the way the tool must work it out:
x = (lba >> k) * 280 / (total >> k), k the shift that fits the total in a word.

  0. The machine is a CGA, and the tool is 178 rows: its frame must be all
     of them - hanging over the dock (WF_KEEPH, SPEC.md 11.93) rather than
     cut at it with the buttons drawn on through the cut.
  1. OPEN (slot 1 selected). The bar, column by column: slot 1 is its digit
     and a 50% GREY fill (a table entry with no volume), slot 2 its digit and
     SOLID black (a volume), white between and around them, one white column
     at the end of each. The underline is slot 1's own extent. The line under
     it reads `Free 257M, largest 225M` - 65,457 + 462,672 sectors, the second
     being the hole a Format of a free slot takes.
  2. Slot 3's ROW is picked. The underline moves to the hole behind slot 2 -
     not the one in front of slot 1, which is smaller - and the size line
     says `225`.
  3. The BAR is clicked on slot 2's black: slot 2 is picked, and the
     underline is its extent.
  4. The bar is clicked on the WHITE in front of slot 1: the first free slot
     (3) is picked, and the underline is the hole behind slot 2 again - which
     is the lesson of the map, that Format takes the largest hole and not the
     one you clicked.

BREAK IT ON PURPOSE: with hd_tw_map's call taken out of hd_tw_paint, step 1
reads white where the bar should be; with the `add [hd_xsum], ax` pair taken
out of hd_slot_extent's walk, the line reads `Free 0M`; with hd_map_hit's
call replaced by the row arithmetic, step 3 picks nothing.

OS88_SHOT=<dir> writes the tool window as a PNG at each of the four reads.
"""
import os
import struct
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty as M                                       # noqa: E402
import instdeep as ID                                       # noqa: E402
import instrest as IR                                       # noqa: E402
import bigvol as BV                                         # noqa: E402
import hdsize as HS                                         # noqa: E402

SECTOR = 512
C, H, S = BV.C, BV.H, BV.S
CYL = H * S
TOTAL = C * CYL

# tool.inc's geometry, content-relative
HTW_LX, HTW_R0Y, HTW_ROWH = 4, 22, 12
HTW_MAPY, HTW_MAPH, HTW_MAPW, HTW_FRY = 71, 8, 280, 88
HTW_H = 178
HTW_SZY, HTW_SZBX, HTW_SZBW = HS.HTW_SZY, HS.HTW_SZBX, HS.HTW_SZBW
BAR_X = HTW_LX + 1                  # the bar's first inner column

SLOTS = [(65 * CYL, 65 * CYL, False),       # LBA, sectors, has a volume
         (130 * CYL, 65 * CYL, True)]


def stage(vhd):
    size, foot = BV.vhd_footer(C, H, S)
    mbr = bytearray(SECTOR)
    for i, (lba, n, _) in enumerate(SLOTS):
        mbr[446 + 16 * i:462 + 16 * i] = (
            bytes([0x00]) + BV.chs(lba) + bytes([0x04])
            + BV.chs(lba + n - 1) + struct.pack("<II", lba, n))
    mbr[510:512] = b"\x55\xAA"
    with open(vhd, "wb") as f:
        f.write(mbr)
        for lba, n, vol in SLOTS:
            if vol:                 # what hd_fmt_isfat asks of a boot sector
                b = bytearray(SECTOR)
                b[0:3] = b"\xEB\x3C\x90"
                struct.pack_into("<HBHBHHBH", b, 11, 512, 4, 1, 2, 512, 0,
                                 0xF8, 64)
                struct.pack_into("<I", b, 32, n)
                b[54:62] = b"FAT16   "
                b[510:512] = b"\x55\xAA"
                f.seek(lba * SECTOR)
                f.write(b)
        f.truncate(size)
        f.seek(size)
        f.write(foot)


def col(v):
    """tool.inc's hd_map_x: sectors to a bar column."""
    k = 0
    while TOTAL >> k > 0xFFFF:
        k += 1
    return (min(v, TOTAL) >> k) * HTW_MAPW // (TOTAL >> k)


def span(lba, n):
    """hd_map_span: first and last column, a white one left at the end."""
    a = min(col(lba), HTW_MAPW - 1)
    return a, max(a, col(lba + n) - 2)


def holes():
    """hd_slot_extent's walk with no slot excused: every hole, and the
    largest Format would take (cylinder-trimmed, under the ceiling)."""
    start, out = S, []
    for lba, n, _ in sorted(SLOTS) + [(TOTAL, 0, False)]:
        if lba > start:
            out.append((start, lba - start))
        start = max(start, -(-(lba + n) // CYL) * CYL)
    return out


def want_bar():
    """What each bar column must be: w(hite), b(lack), g(rey), d(igit)."""
    w = ["w"] * HTW_MAPW
    for i, (lba, n, vol) in enumerate(SLOTS):
        a, z = span(lba, n)
        if z - a >= 8:
            for x in range(a, a + 8):
                w[x] = "d"
            a += 8
        for x in range(a, z + 1):
            w[x] = "b" if vol else "g"
    return "".join(w)


def read_bar(rows, tx, ty):
    out = []
    for c in range(HTW_MAPW):
        px = [rows[ty + HTW_MAPY + 1 + r][tx + BAR_X + c]
              for r in range(HTW_MAPH)]
        if all(px):
            out.append("w")
        elif not any(px):
            out.append("b")
        elif all(px[r] != px[r + 1] for r in range(HTW_MAPH - 1)):
            out.append("g")
        else:
            out.append("d")
    return "".join(out)


def read_under(rows, tx, ty):
    """The underline's columns, or None."""
    y = ty + HTW_MAPY + HTW_MAPH + 3
    lit = [c for c in range(HTW_MAPW)
           if not rows[y][tx + BAR_X + c] and not rows[y + 1][tx + BAR_X + c]]
    return (lit[0], lit[-1]) if lit else None


def shoot(rows, tx, ty, name):
    if not os.environ.get("OS88_SHOT"):
        return
    from PIL import Image
    h = min(180, len(rows) - (ty - 14))
    img = Image.new("L", (320, h))
    img.putdata([255 * rows[ty - 14 + y][tx - 2 + x]
                 for y in range(h) for x in range(320)])
    img.resize((960, 3 * h)).save(os.path.join(os.environ["OS88_SHOT"],
                                               name + ".png"))


def look(m, mo, tx, ty, name):
    IR.park(mo)
    M.settle(m)
    rows = IR.screen(m)[2]
    shoot(rows, tx, ty, name)
    return rows


def main():
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    run_dir = M.stage_run_dir("hdmap")
    vhd = os.path.join(run_dir, ID.VHD_REL)
    stage(vhd)
    hs = holes()
    free = sum(n for _, n in hs)
    big = max(hs, key=lambda h: h[1])
    line = "Free %dM, largest %dM" % (free >> 11, big[1] >> 11)
    bar, s1, s2 = want_bar(), span(*SLOTS[0][:2]), span(*SLOTS[1][:2])
    back = span(*big)
    bad = []
    with M.launch("build/os8088-360.img", apps="build/apps360.img",
                  machine=ID.MACHINE, run_dir=run_dir) as m:
        M.settle(m)
        tab = IR.glyph_table(m)
        mo, tx, ty = HS.open_tool(m)

        tw = [w for w in ID.wins(m) if ID.content(w) == (tx, ty)]
        if not tw or tw[0][4] != HTW_H:
            bad.append("the tool's frame is %r rows, not %d: on a CGA it "
                       "must hang over the dock whole (WF_KEEPH, SPEC.md "
                       "11.93), or the buttons are drawn through the bottom "
                       "of a frame that a press there has left"
                       % ([w[4] for w in tw], HTW_H))
        rows = look(m, mo, tx, ty, "open")
        got = read_bar(rows, tx, ty)
        got = "".join("d" if w == "d" else g for g, w in zip(got, bar))
        if got != bar:                  # a digit's cell is the glyph's test
            bad.append("the bar reads\n    %s\n  where it should read\n    %s"
                       % (got, bar))
        for i, (a, _) in enumerate((s1, s2)):
            ink = [[1 - p for p in r] for r in IR.render(str(i + 1), tab)]
            if not IR.find(rows, tx + BAR_X + a, ty + HTW_MAPY + 1, 8, 8, ink):
                bad.append("slot %d's digit is not at column %d" % (i + 1, a))
        if read_under(rows, tx, ty) != s1:
            bad.append("the underline at open is %r, not slot 1's %r"
                       % (read_under(rows, tx, ty), s1))
        if not IR.find(rows, tx, ty + HTW_FRY - 1, 290, 10,
                       IR.render(line, tab)):
            bad.append("the line under the bar does not read %r" % line)
        else:
            print("  open: bar ok, underline %r, %r" % (s1, line))

        mo.click(tx + 40, ty + HTW_R0Y + 2 * HTW_ROWH + 4, settle=2.0)
        rows = look(m, mo, tx, ty, "slot3")
        if read_under(rows, tx, ty) != back:
            bad.append("slot 3's underline is %r, not the hole behind slot 2 "
                       "%r" % (read_under(rows, tx, ty), back))
        mb = str(big[1] >> 11)
        ink = [[1 - p for p in r] for r in IR.render(mb, tab)]
        if not IR.find(rows, tx + HTW_SZBX, ty + HTW_SZY - 1, HTW_SZBW, 10,
                       ink):
            bad.append("slot 3's size box does not read %s" % mb)
        else:
            print("  slot 3: underline %r, size %s" % (back, mb))

        mo.click(tx + BAR_X + s2[1] - 2, ty + HTW_MAPY + 4, settle=2.0)
        rows = look(m, mo, tx, ty, "bar2")
        if read_under(rows, tx, ty) != s2:
            bad.append("a click on slot 2's segment left the underline at "
                       "%r, not %r" % (read_under(rows, tx, ty), s2))
        else:
            print("  bar click on slot 2: underline %r" % (s2,))

        mo.click(tx + BAR_X + 2, ty + HTW_MAPY + 4, settle=2.0)
        rows = look(m, mo, tx, ty, "bar0")
        if read_under(rows, tx, ty) != back:
            bad.append("a click on the white in front of slot 1 left the "
                       "underline at %r, not the hole Format takes %r"
                       % (read_under(rows, tx, ty), back))
        else:
            print("  bar click on free space: slot 3, underline %r" % (back,))
    if bad:
        sys.exit("hdmap: " + "; ".join(bad) + " (SPEC.md 52.2.8)")
    print("hdmap: the bar is the table to scale, and the underline is where "
          "Format goes")


if __name__ == "__main__":
    main()
