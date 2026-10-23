#!/usr/bin/env python3
"""A hidden row and a hidden column (SPEC.md 81.73).

    make && python3 tests/sheethide.py

Hiding is what turned the viewport's visible-to-real mapping from ARITHMETIC
into a TABLE. Before it, a visible slot's real column was the scroll origin
plus the slot's own index (plus 81.70's frozen prefix); a hidden column
anywhere between them ends that, because the slots stop marching in step with
the columns. So what this row really gates is that mapping: the headers, the
cell contents, the selection and the hit test all have to agree about which
real column a slot is showing, and they each reach it through a different one
of the four routines that read the table.

The sheet is labelled so every cell says where it is - A1 is `A1`, B1 is `B1`
and so on across four columns and four rows.

  1. Hiding column B (Format > Column Width..., 0 - Excel's own way, and the
     line that used to REFUSE with a comment saying Excel's 0 was refused)
     takes it out of the headers: they read A, C, D, and the second slot
     shows C1 rather than B1
  2. ...and the row headers do the same for row 2: 1, 3, 4
  3. A CLICK still lands on the right cell: clicking the second slot selects
     C1, which is sh_gridhit reading the same table the painter did
  4. Unhiding needs no second command: selecting ACROSS the gap and typing a
     real width brings B back, because the apply loop walks REAL columns
  5. ...and 81.73.2's own half: dragging a heading's trailing edge resizes
     that row or column and only that one
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88marty as M                                       # noqa: E402
import os88sheetfmt as F                                     # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
import dispcp                                                # noqa: E402
import os88sym                                              # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import glass                                                 # noqa: E402

WORK = "build/sheethide"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheethide.img"
NAME = "HIDE.SLK"
FMT = SF.FORMAT_MENU
ITEM = lambda x, i: (x + 17, 57 + 12 * i + 2)
ROWH, COLW = 5, 6                       # sh_i_format's own order

CELLS = {(r, c): "%s%d" % ("ABCD"[c], r + 1)
         for r in range(4) for c in range(4)}


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk(CELLS))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    seen = {}
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        ds = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, ds)
        dispcp.open_named(m, mo, S, M.settle, wx, wy, name=NAME)
        M.settle(m, limit=240)
        w, h, rows = m.vram("cga")
        g = glass.grid(w, h, rows)
        if not g:
            check(False, "the grid is on the glass", "no grid")
            done("sheethide")
            return
        ys, xs = g
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)
        box = lambda r, c: (xs[c], ys[r], xs[c + 1], ys[r + 1])
        table = glass.glyphs(m)

        def look(tag):
            """the column headers (the strip just above the grid), the row
            headers (the strip just left of it) and what the second slot
            shows. The grid is RE-MEASURED every time rather than reused from
            the open: hiding a column moves every line right of it, and
            unhiding at a different width moves them again - reading the
            headers through the opening geometry answered None for every
            column past the first."""
            mo.to(634, 190)
            M.settle(m)
            _, _, rw = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rw)
            g = glass.grid(w, h, rw)
            if not g:
                seen[tag] = {}
                return
            gy, gx = g
            hdr = [glass.cell_text(rw, (gx[i] + 2, gy[0] - 15, gx[i + 1] - 2,
                                        gy[0] - 2), table) for i in range(3)]
            rh = [glass.cell_text(rw, (gx[0] - 26, gy[i] + 1, gx[0] - 2,
                                       gy[i + 1] - 1), table) for i in range(3)]
            seen[tag] = {
                'cols': hdr,
                'rows': rh,
                'slot': glass.cell_text(rw, (gx[1], gy[0], gx[2], gy[1]),
                                        table, gx, gy),
            }

        def fmt(item, text):
            mo.menu(FMT[0], FMT[1], *ITEM(FMT[0], item))
            M.settle(m)
            m.type_text("\b" * 10 + text + "\n")
            M.settle(m, limit=180)

        def select(r1, c1, r2, c2):
            mo.click(*at(r1, c1))
            M.settle(m)
            if (r1, c1) != (r2, c2):
                m.key("ShiftLeft", down=True, up=False)
                mo.click(*at(r2, c2))
                m.key("ShiftLeft", down=False, up=True)
                M.settle(m)

        look("1-open")

        # --- 81.73.2: drag column A's own trailing edge 24px right ---------
        # The heading strip sits between the grid's top line and the line
        # above it; the boundary being grabbed is the gridline at xs[1].
        def colwidths(tag):
            mo.to(634, 190)
            M.settle(m)
            _, _, rw = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rw)
            g2 = glass.grid(w, h, rw)
            if not g2:
                return None
            gx = g2[1]
            return [gx[i + 1] - gx[i] for i in range(3)]

        seen['w-before'] = colwidths("1a-beforedrag")
        mo.drag(xs[1], ys[0] - 7, xs[1] + 24, ys[0] - 7)
        M.settle(m, limit=180)
        seen['w-after'] = colwidths("1b-afterdrag")
        mo.drag(xs[1] + 24, ys[0] - 7, xs[1], ys[0] - 7)   # ...and back
        M.settle(m, limit=180)

        select(0, 1, 0, 1)              # B1, then hide its column
        fmt(COLW, "0")
        look("2-colhidden")
        select(0, 0, 0, 0)              # back to a visible cell, then hide
        mo.click(*at(1, 0))             # row 2 - clicked, since it is still
        M.settle(m)                     # on the glass at this point
        fmt(ROWH, "0")
        look("3-rowhidden")
        # the click test: the SECOND slot must now be C1, not B1
        mo.click(*at(0, 1))
        M.settle(m)
        mo.to(634, 190)
        M.settle(m)
        _, _, rw = m.vram("cga")
        M.write_png(os.path.join(WORK, "4-clicked.png"), 640, 200, rw)
        seen['ref'] = glass.cell_text(rw, (56, 57, 122, 71), table)
        # unhide: select ACROSS the gap and give it a real width
        select(0, 0, 0, 2)
        fmt(COLW, "9")
        look("5-unhidden")

    s = lambda tag, k: seen.get(tag, {}).get(k)
    check(s("1-open", 'cols') == ['A', 'B', 'C'],
          "the columns open as A, B, C", "they read %r" % (s("1-open", 'cols'),))
    check(s("2-colhidden", 'cols') == ['A', 'C', 'D'],
          "Column Width 0 hides B: the headers read A, C, D",
          "they read %r" % (s("2-colhidden", 'cols'),))
    check(s("2-colhidden", 'slot') == 'C1',
          "...and the second slot shows C1, not B1 - the painter read the "
          "same table the headers did",
          "it shows %r" % (s("2-colhidden", 'slot'),))
    check(s("3-rowhidden", 'rows') == ['1', '3', '4'],
          "Row Height 0 hides row 2: the row headers read 1, 3, 4",
          "they read %r" % (s("3-rowhidden", 'rows'),))
    check(seen.get('ref') == 'C1',
          "a click on the second slot selects C1 - sh_gridhit reads the same "
          "table, so the hit test and the paint cannot disagree",
          "the reference box reads %r" % (seen.get('ref'),))
    wb, wa = seen.get('w-before'), seen.get('w-after')
    check(wb and wa and wa[0] == wb[0] + 24 and wa[1] == wb[1],
          "81.73.2: dragging column A's heading edge 24px right widens A by "
          "exactly that and leaves B alone",
          "the first three columns were %r and are %r" % (wb, wa))
    check(s("5-unhidden", 'cols') == ['A', 'B', 'C'],
          "selecting ACROSS the gap and typing a real width unhides B, which "
          "is Excel's own way back and needs no second command",
          "the headers read %r" % (s("5-unhidden", 'cols'),))
    done("sheethide")


if __name__ == "__main__":
    main()
