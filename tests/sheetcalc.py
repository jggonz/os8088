#!/usr/bin/env python3
"""Calculation: Manual, and Options > Calculate Now (SPEC.md 81.78).

    make && python3 tests/sheetcalc.py

`sh_drawgrid` re-evaluates every formula cell on EVERY repaint, so
Calculation: Manual is not a label on a no-op - it is what a big sheet on a
4.77 MHz 8088 actually needs, and Calculate Now is then the only way to catch
up. Until 81.78 that command existed only as the third choice inside the
Calculation dialog; Excel has it on the Options menu as well, directly after
Calculation..., and now so does SHEET.

The whole point is a formula that is DELIBERATELY STALE, which is the one
thing an ordinary sheet never shows - so every check here is about a value
being wrong on purpose and then right again:

  1. B1 is =A1+1 with A1 = 1, so it reads 2
  2. Calculation: Manual, then 5 typed into A1 - B1 still reads 2, because
     nothing recalculates. A gate that only checked the END state would pass
     on a build where Manual did nothing at all
  3. Options > Calculate Now - B1 reads 6

It is read off the GLASS, by the kernel's own glyphs, because "what the cell
shows" is exactly the question: the record's cached value is what Manual
leaves stale, and reading the record would be reading the thing under test.
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
import glass                                                 # noqa: E402
from paintmove import pkg_syms                                # noqa: E402
from os88geom import WIN_SIZE, W_SEG                          # noqa: E402

WORK = "build/sheetcalc"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetcalc.img"
NAME = "SHIN.SLK"                       # the fixture's shared name. It was
                                        # NOT "CALC.SLK" because the rows
                                        # clicked the third row of a SORTED
                                        # listing, and CALC sorted ahead of
                                        # CHART.OVL and SHEET.O88; since
                                        # 81.94 they open it BY NAME
                                        # (SF.open_shin), which no file sorting
                                        # in between can move

OPTIONS = (371, 45)                     # the Options menu, sheetfreeze's
ITEM = lambda x, i: (x + 17, 57 + 12 * i + 2)
CALCULATION, CALCNOW = 3, 4             # ...and the two items, by POSITION

# The Calculation dialog is an sh_fdlg like the Save-format one. THE CLAIM
# THAT USED TO BE HERE - "every fdlg kind is the same size, so it opens in
# the same place and sheetfmt's own coordinates carry straight over" - was
# true until 81.115 sized each kind to its own rows, and it is exactly the
# shape of assumption that makes a click land on a live WRONG row. The rows
# are asked for now (SF.fdlg_xy reads the window), not stepped from a
# remembered origin.
# ...and the MANUAL row is ASKED FOR BY NAME, never counted. It was written
# here as 1, with a comment reading "0 Automatic / 1 Manual / 2 Calc Now",
# and 81.114.8 put Excel's "Automatic except Tables" between the first two.
# Row 1 is still a LIVE row, so this went on clicking it, silently selected
# the new middle mode, and reported Manual as broken - the dialog-shaped twin
# of the menu index that shifted when Data > Table... went in at 8.

CELLS = {(0, 0): 1.0, (0, 1): ('formula', 'A1+1', 2.0)}


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk(CELLS))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "APPS:build/sheet.o88",
                    "APPS:build/CHART.OVL", "APPS:build/MACRO.OVL", "APPS:" + src],
                   check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    shots = {}
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        table = glass.glyphs(m)
        dispcp.open_drive(m, mo, lambda n: m.sym(n), M.settle, letter="B")
        M.settle(m)
        mo.dblclick(*SF.APPS_FOLDER)
        M.settle(m)
        SF.open_shin(m, mo)
        M.settle(m, limit=180)
        sym = pkg_syms("apps/sheet/sheet.asm")
        sl = dispcp.win_list(m, lambda n: m.sym(n), check=False)[-1]
        seg = m.read(m.sym("wm_wins") + sl * WIN_SIZE + W_SEG, 2)
        seg = seg[0] | (seg[1] << 8)
        MANUAL = SF.radio_row(m, seg, sym, "sh_fd_i_calc", "sh_fd_cmanual")

        def look(tag):
            mo.to(634, 180)             # the pointer off the grid: it is ink
            M.settle(m)
            w, h, rows = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), w, h, rows)
            g = glass.grid(w, h, rows)
            if not g or len(g[0]) < 3 or len(g[1]) < 3:
                return None
            ys, xs = g
            shots[tag] = (rows, xs, ys)
            return glass.cell_text(rows, (xs[1], ys[0], xs[2], ys[1]),
                                   table, xs, ys)

        fresh = look("1-fresh")

        # --- Calculation: Manual ----------------------------------------------
        mo.menu(OPTIONS[0], OPTIONS[1], *ITEM(OPTIONS[0], CALCULATION))
        M.settle(m)
        mo.click(*SF.fdlg_xy(m, MANUAL))
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m)

        # ...and a value under the formula, which must NOT be picked up
        if shots.get("1-fresh"):
            _r, xs, ys = shots["1-fresh"]
            mo.click((xs[0] + xs[1]) // 2, (ys[0] + ys[1]) // 2)
            M.settle(m)
        m.type_text("5\n")
        M.settle(m)
        stale = look("2-stale")

        # --- Options > Calculate Now -------------------------------------------
        mo.menu(OPTIONS[0], OPTIONS[1], *ITEM(OPTIONS[0], CALCNOW))
        M.settle(m, limit=120)
        after = look("3-calcnow")

    check(fresh == '2', "B1 is =A1+1 over A1=1, so it reads 2",
          "the fixture's own cached value, and the baseline everything else "
          "is measured against", got=repr(fresh), want="'2'")
    check(stale == '2',
          "Calculation: Manual leaves B1 STALE when A1 becomes 5",
          "this is the check with the teeth. sh_drawgrid re-evaluates every "
          "formula on every repaint unless [sh_calcmanual] says otherwise, so "
          "a build where Manual did nothing would show 6 here - and would "
          "still pass the last check, which is why the stale state is "
          "asserted rather than only the end state",
          got=repr(stale), want="'2'")
    check(after == '6', "Options > Calculate Now catches it up",
          "81.78's menu item. It bumps [sh_pass] - a stamp nothing has cached "
          "- which is what forces the recompute, and it is the same four "
          "lines the Calculation dialog's third choice runs",
          got=repr(after), want="'6'")
    done("sheetcalc")


if __name__ == "__main__":
    main()
