#!/usr/bin/env python3
"""Insert and Delete move a cell's BORDERS with it (SPEC.md 81.58).

    make && python3 tests/sheetside.py

Insert and Delete Row/Column moved the cells, their formulas' references
(the reidx pass) and, since 81.56, the column widths - but not the border
table or the note table, which are keyed by position like the cells and were
never walked. A bordered cell moved down a row and its border stayed where it
had been drawn, on a cell that was now something else.

The border is what can be SEEN: with gridlines off, a border is the only ink
on a cell's edge. The host writes a BIFF3 file with A1 bordered top and
bottom; SHEET opens it, the grid is measured with its lines on, and then
Options > Gridlines: Off leaves the borders alone on the glass.

  1. A1's top and bottom edges are inked, A2's bottom is not
  2. Insert Row at A1: the border is on A2 now - A1's top edge is bare
  3. Delete Row at A1: back where it began
  4. Save As Normal: A1's cell names an XF with the top and bottom borders
  5. Delete Row at A1 again, the bordered cell's own row: its border goes
     with it, rather than landing on the row that moves up into its place

The note table goes through the same routine (sh_rc_table) with its own
record size; nothing SHEET writes carries a note, so it is the borders that
are observed.
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
from os88mouse import Mouse                                  # noqa: E402
import dispcp                                                # noqa: E402
import os88sym                                              # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import sheetxl2 as X2                                        # noqa: E402
import glass                                                 # noqa: E402

WORK = "build/sheetside"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetside.img"
EDIT = (123, 45)
OPTIONS = (371, 45)
ITEM = lambda x, i: (x + 17, 57 + 12 * i + 2)
DELETE, INSERT = 7, 8


def rec(op, body):
    return struct.pack('<HH', op, len(body)) + body


def biff3():
    """A1 = 1 bordered top and bottom (XF 1), A3 = 3 plain (XF 0)."""
    plain = bytes([0, 0, 1, 0]) + bytes(8)
    edged = bytes([0, 0, 1, 0]) + bytes(4) + bytes([1, 0, 1, 0])  # top, bottom
    out = rec(0x0209, struct.pack('<HHH', 0x0300, 0x0010, 0))
    out += rec(0x0231, struct.pack('<HHH', 200, 0, 0x7FFF) + b'\x04Helv')
    out += rec(0x0243, plain) + rec(0x0243, edged)
    out += rec(0x0203, struct.pack('<HHHd', 0, 0, 1, 1.0))
    out += rec(0x0203, struct.pack('<HHHd', 2, 0, 0, 3.0))
    return out + rec(0x000A, b'')


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, "SIDE.BIF")
    open(src, "wb").write(biff3())
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    edges = {}
    data = None
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        ds = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, ds)
        dispcp.open_named(m, mo, S, M.settle, wx, wy, name="SIDE.BIF")
        M.settle(m, limit=240)
        w, h, rows = m.vram("cga")
        g = glass.grid(w, h, rows)
        if not g:
            check(False, "the grid is on the glass", "no grid")
            done("sheetside")
            return
        ys, xs = g
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)
        # Gridlines off: a box in Options > Display... since 81.106
        check(SF.display_toggle(m, mo, SF.DISP_GRIDLINES, OPTIONS),
              "Options > Display... opened, Gridlines unchecked")

        def look(tag):
            mo.click(*at(3, 5))             # the selection, well away
            M.settle(m)
            mo.to(634, 180)
            M.settle(m)
            _, _, rows = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rows)
            x1, x2 = xs[0] + 3, xs[1] - 3   # column A, clear of its sides

            def inked(y):
                return sum(1 for x in range(x1, x2) if not rows[y][x]) > \
                    (x2 - x1) * 3 // 4
            # each of rows 1-3 as (top, bottom): a top border is drawn one
            # row IN from the cell's gridline (SPEC.md 81.17.2 - on the line
            # itself it could not be seen), and a BOTTOM one on its last - the
            # row above the next cell's line (sh_drawborders)
            edges[tag] = [(inked(ys[r] + 1), inked(ys[r + 1] - 1))
                          for r in range(3)]

        def rowop(item):
            mo.click(*at(0, 0))
            M.settle(m)
            mo.menu(EDIT[0], EDIT[1], *ITEM(EDIT[0], item))
            M.settle(m)
            mo.click(*SF.fdlg_xy(m, 0))             # Row
            M.settle(m)
            mo.click(*SF.fdlg_okxy(m))
            M.settle(m)

        look("1-open")
        rowop(INSERT)
        look("2-inserted")
        rowop(DELETE)
        look("3-deleted")
        mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0], SF.SAVE_AS[1])
        M.settle(m)
        mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['bif']))
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m, limit=120)
        mo.click(*SF.SAVE_BUTTON)
        before = open(os.path.join(WORK, "SIDE.BIF"), "rb").read()
        for _ in range(60):
            v = os88flush.Flush(marty=m).volume(1)
            if "SIDE.BIF" in v.names() and v.read("SIDE.BIF") != before:
                data = v.read("SIDE.BIF")
                break
            M.settle(m, quiet=2.0, stable=2, limit=60)
        rowop(DELETE)                       # 5: the bordered row ITSELF goes
        look("5-own")

    e = edges
    T, N = (True, True), (False, False)
    check(e.get("1-open") == [T, N, N],
          "with gridlines off, A1's top and bottom edges carry its border",
          "rows 1-3's (top, bottom) read %r" % (e.get("1-open"),))
    check(e.get("2-inserted") == [N, T, N],
          "Insert Row at A1 takes the border down to A2 with its cell",
          "the edges read %r - row 1 inked is a border left behind"
          % (e.get("2-inserted"),))
    check(e.get("3-deleted") == [T, N, N],
          "Delete Row at A1 brings it back", "the edges read %r"
          % (e.get("3-deleted"),))
    check(e.get("5-own") == [N, N, N],
          "Delete Row on the bordered cell's own row takes its border with it",
          "the edges read %r - a border landed on the row that moved up"
          % (e.get("5-own"),))
    xf = X2.biff3_xfs(data) if data else {}
    b = xf.get((0, 0), (None, None, None, b''))[3]
    check(len(b) == 4 and b[0] & 7 and b[2] & 7,
          "the saved A1 names an XF with its top and bottom borders",
          "its border bytes are %r" % (b.hex() if b else b,))
    done("sheetside")


if __name__ == "__main__":
    main()
