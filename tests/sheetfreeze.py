#!/usr/bin/env python3
"""Options > Freeze Panes pins the rows above and the columns left of the
split (SPEC.md 81.70).

    make && python3 tests/sheetfreeze.py

A freeze is the one Sheet feature whose whole subject is what stays on the
GLASS while the rest of the grid moves under it, so nothing in a saved file
can show it: every assertion here is a cell's text read off the framebuffer
with the kernel's own glyphs (tests/glass.py).

The sheet is labelled so that every visible cell says where it is: A1 is
"ZZ", the rest of row 1 is C1, C2, C3... and the rest of column A is R1, R2,
R3... The CGA window shows four rows and ten columns at a time.

  1. Freeze Panes on A1 is REFUSED in its own words (SPEC.md 47) - there is
     nothing above or left of A1 to pin - and the status bar says so
  2. Freeze at B2, then scroll the selection well down and well right: the
     corner still reads "ZZ", the cell beside it is no longer C1 and the cell
     under it is no longer R1. The frozen prefix and the scrolling window are
     both doing their own job at once
  3. The same item now reads "Unfreeze Panes", and choosing it lets the
     corner scroll away like any other cell
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

WORK = "build/sheetfreeze"              # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetfreeze.img"
NAME = "FRZ.SLK"
OPTIONS = (371, 45)                     # the Options menu, tests/sheetside.py's
ITEM = lambda x, i: (x + 17, 57 + 12 * i + 2)
FREEZE = 1                              # ...and Freeze Panes in it: second, as
                                        # Excel has it, since 81.106 (it was
                                        # 5, after Calculate Now). An index
                                        # is the ROW'S POSITION - SH_OI_FREEZE
STATUS = (58, 158, 430, 172)            # the status bar's own strip
N = 24                                  # labelled rows and columns


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells = {(0, 0): 'ZZ'}
    for i in range(1, N):
        cells[(0, i)] = 'C%d' % i
        cells[(i, 0)] = 'R%d' % i
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk(cells))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def marker(text, letter):
    """C7 -> 7, and None for anything that is not that column's own label."""
    if not text or text[0] != letter or not text[1:].isdigit():
        return None
    return int(text[1:])


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    seen = {}
    status = None
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
            done("sheetfreeze")
            return
        ys, xs = g
        box = lambda r, c: (xs[c], ys[r], xs[c + 1], ys[r + 1])
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)
        table = glass.glyphs(m)

        def freeze():
            mo.menu(OPTIONS[0], OPTIONS[1], *ITEM(OPTIONS[0], FREEZE))
            M.settle(m)

        def look(tag):
            """the three corner cells, read from a cell none of them is. The
            click lands inside the SCROLLING window, so it moves the
            selection without moving the view."""
            mo.click(*at(3, 5))
            M.settle(m)
            mo.to(634, 180)
            M.settle(m)
            _, _, rows = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rows)
            seen[tag] = {k: glass.cell_text(rows, box(*k), table, xs, ys)
                         for k in ((0, 0), (0, 1), (1, 0))}
            return rows

        # --- 1: A1 has nothing to freeze, and says so -----------------------
        mo.click(*at(0, 0))
        M.settle(m)
        freeze()
        mo.to(634, 180)
        M.settle(m)
        _, _, rows = m.vram("cga")
        M.write_png(os.path.join(WORK, "1-message.png"), 640, 200, rows)
        status = glass.cell_text(rows, STATUS, table)   # before look()'s own
                                                         # click clears it
        look("1-refused")

        # --- 2: freeze at B2, then scroll away from it ----------------------
        mo.click(*at(1, 1))
        M.settle(m)
        freeze()
        for _ in range(6):                  # four rows fit, one of them frozen
            m.key("ArrowDown")
            M.settle(m)
        for _ in range(12):                 # ten columns fit, one frozen
            m.key("ArrowRight")
            M.settle(m)
        look("2-frozen")

        # --- 3: ...and the same item lets it go again -----------------------
        freeze()
        look("3-thawed")

    s = lambda tag, k: seen.get(tag, {}).get(k)
    check(status is not None and 'Select below or right' in status,
          "Freeze Panes on A1 refuses in its own words",
          "the status bar reads %r" % (status,))
    check(s("1-refused", (0, 0)) == 'ZZ' and s("1-refused", (0, 1)) == 'C1'
          and s("1-refused", (1, 0)) == 'R1',
          "...and freezes nothing: the grid still opens on A1, B1, A2",
          "the corner shows %r, %r, %r"
          % (s("1-refused", (0, 0)), s("1-refused", (0, 1)),
             s("1-refused", (1, 0))))
    check(s("2-frozen", (0, 0)) == 'ZZ',
          "frozen at B2 and scrolled away, A1 is still in the corner",
          "the corner shows %r" % (s("2-frozen", (0, 0)),))
    col = marker(s("2-frozen", (0, 1)), 'C')
    check(col is not None and col > 1,
          "the frozen ROW scrolled sideways under the frozen column: the "
          "cell beside A1 is past B1",
          "it shows %r" % (s("2-frozen", (0, 1)),))
    row = marker(s("2-frozen", (1, 0)), 'R')
    check(row is not None and row > 1,
          "the frozen COLUMN scrolled down under the frozen row: the cell "
          "under A1 is past A2",
          "it shows %r" % (s("2-frozen", (1, 0)),))
    check(s("3-thawed", (0, 0)) != 'ZZ',
          "Unfreeze Panes lets the corner scroll away like any other cell",
          "it still shows %r" % (s("3-thawed", (0, 0)),))
    done("sheetfreeze")


if __name__ == "__main__":
    main()
