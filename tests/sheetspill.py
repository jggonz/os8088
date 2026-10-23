#!/usr/bin/env python3
"""A long label runs on into the empty cells to its right (SPEC.md 81.54).

    make && python3 tests/sheetspill.py

Excel draws a label wider than its column across the EMPTY cells beside it,
and stops at the first one that holds anything. SHEET clipped every label to
its own cell, so an Excel 2.1 worksheet opened with its headings cut to seven
letters (81.52's screenshots: "1st Qua", "Ladies'").

This is about what is DRAWN, so it is read off the glass: SHEET opens a SYLK
the host wrote, and the host reads the 1bpp framebuffer, finds the grid by its
own lines - the rows from the longest evenly spaced run of horizontal lines,
the columns from the vertical ones that run unbroken through every row, which
no glyph can - and counts ink inside each cell, two pixels in from its lines.

  row 1  A1 is a 24-character label with B1:E1 empty - B1, C1 and D1 carry
         its text, E1 is past its end
  row 2  A2 is as long, and B2 holds a number - B2's left part stays empty
  row 3  A3 is "Hi" - B3 has nothing to show
  row 4  C4 is =REPT("ab",10), a TEXT RESULT of 20 characters - D4 and E4
         carry it, and E4 is only reached by the RESULT: the formula's own
         text is 13 characters and would stop in D4. B4, left of it, is empty
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
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
from glass import grid, ink                                  # noqa: E402

WORK = "build/sheetspill"               # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetspill.img"
CELLS = {
    (0, 0): 'Spill across three cells',
    (1, 0): 'Blocked by the number',
    (1, 1): 5.0,
    (2, 0): 'Hi',
    (3, 2): ('formula', 'REPT("ab",10)', 'x'),
    # 81.76's two directions. Both labels are FIFTEEN characters in a column
    # seven wide (SH_CW_NORMAL), so the excess is eight - which is what makes
    # every cell below computable rather than eyeballed.
    #
    # THEY GO IN ROWS 2 AND 3, not in rows of their own, because the window is
    # FOUR ROWS TALL on this machine: CGA is 640x200 and the grid gets what is
    # left under the menu bar, the formula bar and the headings. A fifth row
    # would have been off the glass, and the failure reads as "no grid".
    (1, 6): 'Right aligned!!',      # G2, hanging LEFT over F2 and into E2
    (2, 3): 'Centred label!!',      # D3, hanging BOTH ways over C3 and E3
}
# The alignment is the whole point of those two, and it cannot be reached from
# the host any other way - write_sylk grew an `align` for exactly this.
ALIGN = {(1, 6): 'R', (2, 3): 'C'}


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, "SHIN.SLK")
    open(src, "wb").write(F.write_sylk(CELLS, align=ALIGN))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "APPS:build/sheet.o88",
                    "APPS:build/CHART.OVL", "APPS:build/MACRO.OVL", "APPS:" + src],
                   check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, lambda n: m.sym(n), M.settle, letter="B")
        M.settle(m)
        mo.dblclick(*SF.APPS_FOLDER)
        M.settle(m)
        SF.open_shin(m, mo)
        M.settle(m, limit=180)
        # A1 is selected: its border is heavier, but on A1 alone, which no
        # check reads, and too short to pass for a gridline
        mo.to(634, 180)                     # THE POINTER OFF THE GRID: it is
        M.settle(m)                         # left where the file was double-
                                            # clicked, over B3 and B4, and is
                                            # ink like any other
        w, h, rows = m.vram("cga")
        M.write_png(os.path.join(WORK, "glass.png"), w, h, rows)
    g = grid(w, h, rows)
    g = g if g and len(g[0]) >= 5 and len(g[1]) >= 9 else None
    check(g is not None, "the grid is on the glass",
          "no evenly spaced run of five horizontal lines, or fewer than\n"
          "          eight columns across - see glass.png")
    if not g:
        done("sheetspill")
        return
    ys, xs = g
    box = lambda r, c: (xs[c], ys[r], xs[c + 1], ys[r + 1])
    cell = lambda r, c: "%s%d" % (chr(65 + c), r + 1)
    for r, c in ((0, 1), (0, 2), (0, 3), (3, 3), (3, 4)):
        n = ink(rows, box(r, c))
        check(n > 0, "%s carries the label on its left" % cell(r, c),
              "no ink in it")
    # ...and the other direction (81.76). A right-aligned label hangs LEFT and
    # a centred one both ways, which is geometry rather than a preference:
    # centring a string wider than its box leaves half the excess each side.
    for r, c, why in ((1, 5, "G2 is right-aligned, so it hangs left"),
                      (1, 4, "...and reaches E2 by one character"),
                      (2, 2, "D3 is centred, so it hangs BOTH ways - left"),
                      (2, 4, "...and right")):
        n = ink(rows, box(r, c))
        check(n > 0, "%s carries the label beside it - %s" % (cell(r, c), why),
              "no ink in it")
    for r, c, why in ((0, 4, "past the label's end"),
                      (2, 1, "A3 is two letters"),
                      (3, 1, "left of C4"),
                      (3, 5, "past the RESULT's end"),
                      (1, 3, "the excess is 8 and F2+E2 take it all"),
                      (1, 7, "a right-aligned label does NOT hang right"),
                      (2, 5, "the centred label's right half stops in E3")):
        n = ink(rows, box(r, c))
        check(n == 0, "%s is empty - %s" % (cell(r, c), why),
              "%d pixels of ink in it" % n)
    n = ink(rows, box(1, 1), part=0.55)
    check(n == 0, "B2's number stops A2's label: B2's left half is empty",
          "%d pixels there" % n)
    check(ink(rows, box(1, 1)) > 0, "...and B2 shows its number",
          "B2 is blank")
    done("sheetspill")


if __name__ == "__main__":
    main()
