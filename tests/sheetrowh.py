#!/usr/bin/env python3
"""Each row its own height (SPEC.md 81.60).

    make && python3 tests/sheetrowh.py

SHEET had ONE row height for the whole sheet, in pixels, and an Excel file's
ROW records were skipped. Excel's Row Height is per row, in POINTS. The grid
is read off the glass (tests/glass.py - rgrid, since grid() needs its rows
one height), and BIFF saves say what the glass cannot. CGA leaves the grid
about sixty pixels, so the heights are the ones that fit it:

  1. The HOST writes a BIFF3 file whose ROW records make row 2 eighteen
     points, row 3 eight and row 5 thirty - and row 4 twenty with bit 15
     set, which says "the default height" and must be ignored. SHEET opens
     it: the lines must be 14, 20, 9 and 14 pixels apart, and row 2's text
     must sit LOW in it, as Excel's does.
  2. A click in the lower part of B2 and a value typed: it must land in B2.
     Dividing by one height puts that point in B3.
  3. Format > Row Height on B4, 9.75 points: 11 pixels. Then OK on row 2's
     dialog and on row 1's, typing nothing: row 2 keeps its 20 (the dialog
     opened on 18, not on its pixels) and row 1 gains no record (it opened
     on 12.75, the standard, exactly).
  4. Save As Normal: ROW records for rows 2-5, in twips - 9.75 is 195.
  5. Edit > Insert, Row, at 1, then Save: every height moves down a row; then
     Undo, and Save: they are back.
  6. Down from B4, the last whole row, onto row 5's thirty points: the view
     must scroll until row 5 shows WHOLE, which is two rows here - taking
     off as many rows as the old view held above it, one, leaves row 5 below
     the glass. And the rows either view shows are not one height, so the
     blit that moves the picture by standard rows must stand aside: the grid
     comes out 9, 11 and 33.
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88marty as M                                       # noqa: E402
import os88flush                                            # noqa: E402
import os88sheetfmt as F                                     # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
import dispcp                                                # noqa: E402
import os88sym                                              # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import glass                                                 # noqa: E402

WORK = "build/sheetrowh"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetrowh.img"
NAME = "ROWH.BIF"
EDIT = (123, 45)
ITEM = lambda x, i: (x + 17, 57 + 12 * i + 2)
UNDO, INSERT = 0, 8
ROWH_ITEM = (SF.FORMAT_MENU[0] + 15, 57 + 12 * 5 + 2)   # Format's 6th item
# no 'l' and no 'I' in what is read: this BIOS face draws the two alike
LABELS = {1: 'Hat', 2: 'Ox'}


def rec(op, body):
    return struct.pack('<HH', op, len(body)) + body


def row(r, h, flags=0x0140):
    return rec(0x0208, struct.pack('<HHHHHHHH', r, 0, 1, h, 0, 0, flags, 15))


def biff3():
    plain = bytes([0, 0, 1, 0]) + bytes(8)
    out = rec(0x0209, struct.pack('<HHH', 0x0300, 0x0010, 0))
    out += rec(0x0231, struct.pack('<HHH', 200, 0, 0x7FFF) + b'\x04Helv')
    out += rec(0x0243, plain)
    out += row(1, 360) + row(2, 160) + row(3, 0x8000 | 400) + row(4, 600)
    for r in range(6):
        if r in LABELS:
            t = LABELS[r].encode()
            out += rec(0x0204, struct.pack('<HHHH', r, 0, 0, len(t)) + t)
        else:
            out += rec(0x0203, struct.pack('<HHHd', r, 0, 0, float(r + 1)))
    return out + rec(0x000A, b'')


def biff_rows(data):
    """{row: twips} of the ROW records that do not say 'the default'."""
    out, i = {}, 0
    while i + 4 <= len(data):
        op, ln = struct.unpack_from('<HH', data, i)
        if op in (0x0008, 0x0208) and ln >= 8:
            r = struct.unpack_from('<H', data, i + 4)[0]
            h = struct.unpack_from('<H', data, i + 10)[0]
            if not h & 0x8000:
                out[r] = h
        elif op == 0x000A:
            break
        i += 4 + ln
    return out


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(biff3())
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    gaps, text, low, saved = {}, {}, None, {}
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        table = glass.glyphs(m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        ds = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, ds)
        dispcp.open_named(m, mo, S, M.settle, wx, wy, name=NAME)
        M.settle(m, limit=240)

        def glass_now(tag):
            mo.to(634, 180)             # the pointer off the grid: it is ink
            M.settle(m)
            w, h, rows = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rows)
            g = glass.rgrid(w, h, rows)
            ys, xs = g if g else ([], [])
            gaps[tag] = [b - a for a, b in zip(ys, ys[1:])]
            return rows, ys, xs

        rows, ys, xs = glass_now("1-open")
        if len(ys) < 5 or len(xs) < 3:
            check(False, "the grid is on the glass", "lines %r, %r" % (ys, xs))
            done("sheetrowh")
            return
        box = lambda r, c: (xs[c], ys[r], xs[c + 1], ys[r + 1])
        for r in (1, 2):
            text[r] = glass.cell_text(rows, box(r, 0), table, xs, ys)
        # row 2's text sits in its LOWER fourteen pixels (sh_vtoff): nothing
        # in the six above them, something below
        x1, y1, x2, y2 = box(1, 0)
        low = (glass.ink(rows, (x1, y1, x2, y1 + 6)),
               glass.ink(rows, (x1, y1 + 5, x2, y2)))

        # --- 2: the lower part of B2, which one height calls B3
        mo.click((xs[1] + xs[2]) // 2, ys[1] + 17)
        M.settle(m)
        m.type_text("7\n")
        M.settle(m)

        def rowheight(r, typed):
            mo.click((xs[1] + xs[2]) // 2, (ys[r] + ys[r + 1]) // 2)
            M.settle(m)
            mo.menu(SF.FORMAT_MENU[0], SF.FORMAT_MENU[1], *ROWH_ITEM)
            M.settle(m)
            m.type_text(typed + "\n")
            M.settle(m)

        # --- 3: 9.75 points on B4; then OK alone on rows 2 and 1
        rowheight(3, "\b\b\b\b\b\b9.75")
        glass_now("3-row4")
        rowheight(1, "")
        rowheight(0, "")
        glass_now("3-okalone")

        def save(tag):
            before = saved.get("last") or open(os.path.join(WORK, NAME),
                                               "rb").read()
            mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1],
                    SF.SAVE_AS[0], SF.SAVE_AS[1])
            M.settle(m)
            mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['bif']))
            M.settle(m)
            mo.click(*SF.fdlg_okxy(m))
            M.settle(m, limit=120)
            mo.click(*SF.SAVE_BUTTON)
            for _ in range(60):
                v = os88flush.Flush(marty=m).volume(1)
                if NAME in v.names() and v.read(NAME) != before:
                    saved[tag] = saved["last"] = v.read(NAME)
                    open(os.path.join(WORK, tag + ".BIF"), "wb").write(
                        saved[tag])     # what SHEET wrote, to look at
                    return
                M.settle(m, quiet=2.0, stable=2, limit=60)

        save("4-saved")                                             # --- 4

        # --- 5: Insert Row at 1, save; Undo, save
        mo.click((xs[0] + xs[1]) // 2, (ys[0] + ys[1]) // 2)
        M.settle(m)
        mo.menu(EDIT[0], EDIT[1], *ITEM(EDIT[0], INSERT))
        M.settle(m)
        mo.click(*SF.fdlg_xy(m, 0))                 # Row
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m)
        save("5-inserted")
        mo.menu(EDIT[0], EDIT[1], *ITEM(EDIT[0], UNDO))
        M.settle(m)
        save("5-undone")

        # --- 6: Down from the last whole row
        rows, ys, xs = glass_now("6-before")
        if len(ys) >= 2 and len(xs) >= 3:
            mo.click((xs[1] + xs[2]) // 2, (ys[-2] + ys[-1]) // 2)
            M.settle(m)
            m.key("ArrowDown")
            M.settle(m)
        glass_now("6-scrolled")

    g = lambda tag: gaps.get(tag, [])
    check(g("1-open") == [14, 20, 9, 14],
          "the file's rows are 14, 20, 9 and 14 pixels on the glass - the "
          "default-flagged row 4 ignored",
          "the gaps between their lines are %r" % (g("1-open"),))
    check(text.get(1) == 'Hat' and text.get(2) == 'Ox',
          "the tall row and the short one both read", "they show %r, %r"
          % (text.get(1), text.get(2)))
    check(low is not None and low[0] == 0 and low[1] > 0,
          "the tall row's text sits low in it, as Excel's does",
          "ink above and below its lower 14 pixels: %r" % (low,))
    check(g("3-row4") == [14, 20, 9, 11],
          "Row Height 9.75 on B4 makes row 4 eleven pixels, and only row 4",
          "the gaps are %r" % (g("3-row4"),))
    check(g("3-okalone") == [14, 20, 9, 11],
          "OK alone on rows 2 and 1 changes neither: the dialog opens on "
          "their own heights, in points", "the gaps are %r"
          % (g("3-okalone"),))
    cells = F.read_biff(saved["4-saved"]) if "4-saved" in saved else {}
    check(cells.get((1, 1)) == 7.0,
          "the click in the lower part of B2 typed into B2",
          "B2 holds %r, B3 %r" % (cells.get((1, 1)), cells.get((2, 1))))
    want = {1: 360, 2: 160, 3: 195, 4: 600}
    for tag, exp, what in (
            ("4-saved", want, "Save As Normal writes a ROW for rows 2-5 in "
             "twips, and none for row 1"),
            ("5-inserted", {k + 1: v for k, v in want.items()},
             "Insert Row at 1 moves every height down with its row"),
            ("5-undone", want, "Undo Insert puts every height back")):
        got = biff_rows(saved[tag]) if tag in saved else None
        check(got == exp, what, "it wrote %r" % (got,))
    check(g("6-scrolled") == [9, 11, 33],
          "Down onto the thirty-point row scrolls until it shows whole, and "
          "the rows come out 9, 11, 33 - no blit by standard rows",
          "the gaps are %r" % (g("6-scrolled"),))
    done("sheetrowh")


if __name__ == "__main__":
    main()
