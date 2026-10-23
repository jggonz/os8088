#!/usr/bin/env python3
"""Data > Table fills a what-if table, both shapes (SPEC.md 81.114)

    make && python3 tests/sheettable.py

The last menu command Excel 2.1d has that this app did not. A result cell is
the governing formula evaluated once per input value, with that value
substituted into an input cell elsewhere on the sheet - so the thing to
assert is the NUMBERS, and the fixture is built so that a plausible wrong
implementation produces different ones.

  1 COLUMN   a one-input table whose values run DOWN the left column and
             whose formulas run ACROSS the top row
  3 TWO      a two-input table: one formula in the corner, a row of values
             feeding one input cell and a column of values feeding the other
  4 INTACT   ...AND THE INPUT CELLS STILL HOLD WHAT THEY HELD. Every result
             reads them, so a table that forgot to put them back would make
             its own answers depend on the order it filled them - and would
             leave its last substitution sitting in the user's sheet
  5 FIRST    the first result of each table, named separately because it is
             the one the pass stamp buys - see the check itself

THE ARITHMETIC IS CHOSEN SO NO TWO CELLS AGREE. A table of x*2 over inputs
1,2 would give 2 and 4 in one column and the same 2 and 4 in the other, and
a build that substituted the wrong value could land on a number the fixture
also expects. Every expected value below is distinct.

BOTH TABLES LIVE IN ROWS 1-3, side by side, so every cell of both is on the
screen and the ranges can be selected by CLICKING. Goto with a plain
reference COLLAPSES the selection to one cell - which an earlier run of this
row found, the engine quite correctly refusing a selection with no interior -
and Goto with a name would make this row also assert that the SYLK reader
takes an NN record, which is not the thing under test.
"""
import os
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
import os88sym                                               # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import glass                                                 # noqa: E402

WORK = "build/sheettable"
DISK = "build/sheettable.img"
NAME = "SHIN.SLK"

# MEASURED off a photograph of the pulldown taken mid-hold, never computed:
# the bar titles are File 78, Edit 126, Formula 187, Format 254, Data 311,
# and the items run at a flat pitch of 12 from y 57 with the separators
# drawn INSIDE that pitch rather than adding rows. Table... is item 8.
DATA_MENU = (311, 45)
TABLE_ITEM = (330, 153)

# --- the one-input table: B1:D3, input cell A1 ------------------------------
ONE_IN = {
    (0, 0): 0.0,                                  # A1, the input cell
    (0, 2): ('formula', 'A1*2', 0.0),             # C1  }  the formulas,
    (0, 3): ('formula', 'A1+10', 10.0),           # D1  }  across the top row
    (1, 1): 4.0,                                  # B2  }  the values, down
    (2, 1): 9.0,                                  # B3  }  the left column
}
WANT_COL = {(1, 2): 8.0, (1, 3): 14.0,            # C2 D2  (input 4)
            (2, 2): 18.0, (2, 3): 19.0}           # C3 D3  (input 9)

# --- the two-input table: F1:H3, input cells A2 (row) and A3 (column) -------
TWO_IN = {
    (1, 0): 0.0,                                  # A2,  the ROW input cell
    (2, 0): 0.0,                                  # A3,  the COLUMN input cell
    (0, 5): ('formula', 'A2*A3+A2', 0.0),         # F1,  the corner formula
    (0, 6): 4.0, (0, 7): 5.0,                     # G1 H1, across the top
    (1, 5): 2.0, (2, 5): 3.0,                     # F2 F3, down the left
}
# x*y+x over (x across, y down): 4*2+4=12, 5*2+5=15, 4*3+4=16, 5*3+5=20
WANT_TWO = {(1, 6): 12.0, (1, 7): 15.0,
            (2, 6): 16.0, (2, 7): 20.0}


def build_disk(cells):
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk(cells))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "build/sheet.o88",
                    "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)
    return open(src, "rb").read()


def main():
    os.chdir(os.path.join(HERE, ".."))
    cells = dict(ONE_IN)
    cells.update(TWO_IN)
    before = build_disk(cells)
    S = os88sym.linear
    saved = None

    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        SF.open_shin(m, mo, NAME)
        M.settle(m, limit=240)
        mo.to(634, 180)
        M.settle(m)

        w, h, rows = m.vram("cga")
        g = glass.grid(w, h, rows)
        if not g:
            check(False, "the grid is on the glass", "no grid")
            done("sheettable")
            return
        ys, xs = g
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)

        def select(r1, c1, r2, c2):
            mo.click(*at(r1, c1))
            M.settle(m)
            m.key("ShiftLeft", down=True, up=False)
            mo.click(*at(r2, c2))
            m.key("ShiftLeft", down=False, up=True)
            M.settle(m)

        def table(rowref, colref):
            """Data > Table, and its two fields in sequence. EITHER MAY BE
            EMPTY: that is how a one-input table says which kind it is."""
            mo.menu(DATA_MENU[0], DATA_MENU[1], *TABLE_ITEM)
            M.settle(m, limit=120)
            m.type_text(rowref + "\n")       # Row Input Cell
            M.settle(m, limit=120)
            m.type_text(colref + "\n")       # ...then Column Input Cell
            M.settle(m, limit=240)

        def save():
            mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1],
                    SF.SAVE_AS[0], SF.SAVE_AS[1])
            M.settle(m)
            mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['slk']))
            M.settle(m)
            mo.click(*SF.fdlg_okxy(m))
            M.settle(m, limit=120)
            mo.click(*SF.SAVE_BUTTON)
            for _ in range(60):
                v = os88flush.Flush(marty=m).volume(1)
                if NAME in v.names() and v.read(NAME) != before:
                    return v.read(NAME)
                M.settle(m, quiet=2.0, stable=2, limit=60)
            return None

        select(0, 1, 2, 3)                   # B1:D3 - the rectangle INCLUDES
        table("", "A1")                      # the formulas and the values
        select(0, 5, 2, 7)                   # F1:H3
        table("A2", "A3")
        saved = save()

    if not saved:
        check(False, "1 COLUMN", "SHEET never wrote the file back",
              got="no save", want="a SYLK file")
        done("sheettable")
        return
    cs = F.read_sylk(saved)
    val = lambda rc: F.scalar(cs.get(rc))
    num = lambda rc: val(rc) if val(rc) is not None else -1e30

    bad = [rc for rc, w in sorted(WANT_COL.items()) if not F.close(num(rc), w)]
    check(not bad, "1 COLUMN",
          "values down the left column and formulas across the top: each "
          "result is its own column's formula with its own row's value "
          "substituted into A1",
          got=repr([(rc, val(rc)) for rc in sorted(WANT_COL)]),
          want=repr(sorted(WANT_COL.items())))

    bad2 = [rc for rc, w in sorted(WANT_TWO.items()) if not F.close(num(rc), w)]
    check(not bad2, "3 TWO",
          "one formula in the corner, the row of values feeding the ROW "
          "input cell and the column feeding the COLUMN one",
          got=repr([(rc, val(rc)) for rc in sorted(WANT_TWO)]),
          want=repr(sorted(WANT_TWO.items())))

    inputs = {(0, 0): 0.0, (1, 0): 0.0, (2, 0): 0.0}
    badi = [rc for rc, w in sorted(inputs.items()) if not F.close(num(rc), w)]
    check(not badi, "4 INTACT",
          "the input cells hold what they held. Every result reads them, so "
          "a table that did not put them back would depend on the order it "
          "filled - and would leave its last substitution in the sheet. "
          "MEASURED NECESSARY: dropping the restore leaves A1 9, A2 5 and "
          "A3 3 with every result still correct, so this check fails ALONE",
          got=repr([(rc, val(rc)) for rc in sorted(inputs)]),
          want="A1, A2 and A3 all 0")

    # 5 IS A NAMED SUBSET of 1 and 3 and says so: they assert these same two
    # cells exactly. It exists to put the failure in words where it would be
    # read, and to record what the pass stamp actually buys.
    k1, k2 = sorted(WANT_COL)[0], sorted(WANT_TWO)[0]
    first = [val(k1), val(k2)]
    want1 = [WANT_COL[k1], WANT_TWO[k2]]
    check(first == want1, "5 FIRST CELL",
          "THE FIRST result of each table is what `inc word [sh_pass]` buys "
          "(81.114.3). sh_eval_cell memoizes per repaint pass, and without "
          "the stamp that first cell - and ONLY that one - comes back as the "
          "formula's unsubstituted answer, because the pass then moves on "
          "its own. MEASURED: removing it gives 0.0 in both these cells and "
          "leaves the other six results correct. This row first asked "
          "instead whether the four results DIFFER, on the assumption that "
          "memoization would repeat ONE number - and that check PASSED on "
          "its own mutation, because 0, 14, 18 and 19 are four distinct "
          "values",
          got=repr(first), want=repr(want1))
    done("sheettable")


if __name__ == "__main__":
    sys.exit(main())
