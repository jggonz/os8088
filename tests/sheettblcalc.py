#!/usr/bin/env python3
"""A Data > Table RECALCULATES, and the three modes decide when (81.114.8)

    make && python3 tests/sheettblcalc.py

A table's result cells hold STORED VALUES, so this app's whole recalculation
mechanism - advance [sh_pass], and sh_eval_cell re-evaluates lazily as the
grid draws - does nothing for them at all. They have to be computed again,
one full formula evaluation PER RESULT CELL.

That cost is why Excel 2.1's own Calculation dialog offers three modes rather
than two, and the capture is the authority for it:
LIBRARY/documentation/screenshots/excel/dialog_calculation.png shows
Automatic / Automatic except Tables / Manual. This app had Automatic / Manual
/ Calculate Now; it has Excel's three with Calculate Now after them.

  1 AUTO      under Automatic, changing an input VALUE re-runs the table -
              and ONLY the results that value governs
  2 FORMULA   ...and so does changing the governing FORMULA, which is the
              other thing a result depends on
  3 AUEX      under Automatic except Tables, the same edit leaves the table
              alone. This is the mode the machine wants the moment a table
              exists, and a mode that silently recalculated anyway would be
              the feature doing nothing
  4 NOW       ...and Calculate Now catches it up, which under that mode is
              the ONLY thing that ever will
  5 MANUAL    under Manual, neither formulas nor tables move until Calculate
              Now, which then moves both
  6 DEL       the Del key is not sh_commit, and the cell it empties may be an
              input cell - so it owes its own re-run
  7 SHEET2    a table on SHEET 2, re-run while Sheet 1 is showing, writes to
              Sheet 2. sh_getcell2 and sh_setvald work on [sh_cursheet], so
              a walk that did not set it from the record would read the
              wrong grid and scribble its answers onto the VISIBLE one. This
              is the exact bug class that already bit this feature once, in
              silence: 81.114.4's sh_table_unbank restored input cells
              correctly on Sheet 1 and nowhere else, for a whole cycle,
              because every fixture had only ever used Sheet 1

EVERY VALUE IS READ OUT OF THE CELL RECORDS, not off the glass and not out of
a saved file: a result cell holds a stored number, so the record IS the
answer, and reading it directly is what lets this row assert a table that did
NOT move as precisely as one that did. (That is only sound for a stored
value - 81's "stored vs published" trap is about FORMULA cells, which this
row never reads.)

THE ARITHMETIC IS CHOSEN SO NO TWO CELLS AGREE at any point in the sequence,
so a re-run that wrote the right number into the wrong cell cannot pass.

WHAT THIS ROW DOES NOT CLAIM: that only the affected results are recomputed.
A recalculation re-runs EVERY table entirely, by design (81.114.8 - there is
no dependency tracking, and the mode switch is the answer Excel shipped for
the cost). Re-running row 3 with unchanged inputs gives unchanged row 3
values, so no assertion here could tell a targeted re-run from a total one,
and none tries to.
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
import os88sym                                               # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import glass                                                 # noqa: E402
from paintmove import pkg_syms                                # noqa: E402
from os88geom import WIN_SIZE, W_SEG                          # noqa: E402

WORK = "build/sheettblcalc"
DISK = "build/sheettblcalc.img"
NAME = "SHIN.SLK"

# B1:D3, one input table, COLUMN input cell A1. A1 is absent, as 81.114.4's
# other arm; the values live in B2/B3 and the formulas in C1/D1.
CELLS = {
    (0, 2): ('formula', 'A1*2', 0.0),             # C1
    (0, 3): ('formula', 'A1+10', 10.0),           # D1
    (1, 1): 4.0,                                  # B2, row 2's input value
    (2, 1): 9.0,                                  # B3, row 3's
}
# C2 D2 C3 D3 after the first run
START = {(1, 2): 8.0, (1, 3): 14.0, (2, 2): 18.0, (2, 3): 19.0}


def main():
    os.chdir(os.path.join(HERE, ".."))
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk(CELLS))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "build/sheet.o88", "build/CHART.OVL",
                    "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)
    S = os88sym.linear
    sym = pkg_syms("apps/sheet/sheet.asm")
    got = {}

    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        SF.open_shin(m, mo, NAME)
        M.settle(m, limit=240)
        sl = dispcp.win_list(m, S, check=False)[-1]
        seg = m.read(S("wm_wins") + sl * WIN_SIZE + W_SEG, 2)
        seg = seg[0] | (seg[1] << 8)
        mo.to(634, 180)
        M.settle(m)

        g = glass.grid(*m.vram("cga"))
        if not g:
            check(False, "the grid is on the glass", "no grid")
            done("sheettblcalc")
            return
        ys, xs = g
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)
        val = lambda r, c: SF.cell_val(m, seg, sym, c, r)
        quad = lambda: [val(1, 2), val(1, 3), val(2, 2), val(2, 3)]

        def select(r1, c1, r2, c2):
            mo.click(*at(r1, c1))
            M.settle(m)
            m.key("ShiftLeft", down=True, up=False)
            mo.click(*at(r2, c2))
            m.key("ShiftLeft", down=False, up=True)
            M.settle(m)

        def typed(r, c, what):
            mo.click(*at(r, c))
            M.settle(m)
            m.type_text(what + "\n")
            M.settle(m, limit=180)

        ROW = lambda lbl: SF.radio_row(m, seg, sym, "sh_fd_i_calc", lbl)
        AUTO, AUEX, MANUAL = (ROW("sh_fd_cauto"), ROW("sh_fd_cauex"),
                              ROW("sh_fd_cmanual"))

        def mode(label):
            """Options > Calculation..., pick a mode BY NAME, OK. Never by
            row number: inserting this very dialog's middle mode is what
            broke two older rows that had counted."""
            SF.menu_pick(m, mo, seg, sym, "sh_i_options", "calc")
            M.settle(m, limit=120)
            SF.fdlg_ok(m, mo, seg, sym, row=ROW(label))
            M.settle(m, limit=120)

        def calc_now():
            SF.menu_pick(m, mo, seg, sym, "sh_i_options", "calcnow")
            M.settle(m, limit=240)

        # --- the table ------------------------------------------------------
        select(0, 1, 2, 3)                        # B1:D3
        SF.menu_pick(m, mo, seg, sym, "sh_i_data", "dtable")
        M.settle(m, limit=120)
        m.type_text("\n")                         # Row Input Cell: none
        M.settle(m, limit=120)
        m.type_text("A1\n")                       # Column Input Cell: A1
        M.settle(m, limit=240)
        got["start"] = quad()
        got["mode0"] = m.readseg(seg, sym["sh_calcmode"], 1)[0]

        # --- 1 AUTO: B2 is row 2's input value ------------------------------
        typed(1, 1, "5")                          # B2 4 -> 5
        got["auto"] = quad()                      # C2 10, D2 15; row 3 still

        # --- 2 FORMULA: C1 governs column C ---------------------------------
        typed(0, 2, "=A1*100")
        got["formula"] = quad()                   # C2 500, C3 900

        # --- 3 AUEX ----------------------------------------------------------
        mode("sh_fd_cauex")
        got["mode1"] = m.readseg(seg, sym["sh_calcmode"], 1)[0]
        typed(2, 1, "3")                          # B3 9 -> 3: row 3 is STALE
        got["auex"] = quad()

        # --- 4 NOW -----------------------------------------------------------
        calc_now()
        got["now"] = quad()                       # C3 300, D3 13

        # --- 5 MANUAL ---------------------------------------------------------
        mode("sh_fd_cmanual")
        got["mode2"] = m.readseg(seg, sym["sh_calcmode"], 1)[0]
        typed(1, 1, "2")                          # B2 5 -> 2: still stale
        got["manual"] = quad()
        calc_now()
        got["mannow"] = quad()                    # C2 200, D2 12

        # --- 6 DEL: back to Automatic, and empty an input VALUE --------------
        mode("sh_fd_cauto")
        mo.click(*at(2, 1))                       # B3
        M.settle(m)
        m.key("Delete")
        M.settle(m, limit=240)
        got["del"] = quad()                       # row 3 recomputes on 0

        # --- 7: a table on SHEET 2, re-run while Sheet 1 is showing --------
        SF.menu_pick(m, mo, seg, sym, "sh_i_sheet", "sheet2")
        M.settle(m, limit=180)
        typed(0, 6, "=A1+1")                      # G1 on Sheet 2
        typed(1, 5, "20")                         # F2
        typed(2, 5, "30")                         # F3
        select(0, 5, 2, 6)                        # F1:G3
        SF.menu_pick(m, mo, seg, sym, "sh_i_data", "dtable")
        M.settle(m, limit=120)
        m.type_text("\n")
        M.settle(m, limit=120)
        m.type_text("A1\n")
        M.settle(m, limit=240)
        got["s2start"] = [SF.cell_val(m, seg, sym, 6, 1, sheet=1),
                          SF.cell_val(m, seg, sym, 6, 2, sheet=1)]
        SF.menu_pick(m, mo, seg, sym, "sh_i_sheet", "sheet1")
        M.settle(m, limit=180)
        typed(3, 0, "1")                          # ANY commit on Sheet 1 (A4,
                                                  # which nothing here reads)
                                                  # re-runs EVERY table
        got["s2after"] = [SF.cell_val(m, seg, sym, 6, 1, sheet=1),
                          SF.cell_val(m, seg, sym, 6, 2, sheet=1)]
        got["s1ghost"] = [SF.cell_val(m, seg, sym, 6, 1),
                          SF.cell_val(m, seg, sym, 6, 2)]

    s = got["start"]
    check(s == [8.0, 14.0, 18.0, 19.0], "0 FILLED",
          "the table filled before anything was recalculated - the control "
          "for every check below, and the same arithmetic sheettable proves",
          got=repr(s), want="[8.0, 14.0, 18.0, 19.0]")
    check(got["auto"] == [10.0, 15.0, 18.0, 19.0], "1 AUTO",
          "changing B2, which is row 2's input value, re-runs the table "
          "under Automatic. The WHOLE quad is asserted, so a re-run that "
          "put the right number in the wrong cell is caught - but note that "
          "row 3 standing still proves nothing about WHICH cells were "
          "recomputed: every table re-runs entirely, and row 3's inputs did "
          "not change",
          got=repr(got["auto"]), want="[10.0, 15.0, 18.0, 19.0]")
    check(got["formula"] == [500.0, 15.0, 900.0, 19.0], "2 FORMULA",
          "...and changing C1, the formula governing column C, re-runs it "
          "too: a result depends on the formula as much as on the value, "
          "and both edits arrive through sh_commit",
          got=repr(got["formula"]), want="[500.0, 15.0, 900.0, 19.0]")
    check(got["mode1"] == SF.CALC_AUEX
          and got["auex"] == [500.0, 15.0, 900.0, 19.0], "3 AUEX",
          "under Automatic except Tables the same edit leaves the table "
          "alone - B3 is 3 now and row 3 still reads the answers for 9. "
          "ORDINARY formulas still recalculate in this mode; it is the "
          "table, whose every cell is a full evaluation, that waits",
          got="mode %d, %r" % (got["mode1"], got["auex"]),
          want="mode 1, unchanged [500.0, 15.0, 900.0, 19.0]")
    check(got["now"] == [500.0, 15.0, 300.0, 13.0], "4 NOW",
          "...and Calculate Now catches it up, which under that mode is the "
          "only thing that ever will. It re-runs tables WHATEVER the mode, "
          "which is the whole point of the command",
          got=repr(got["now"]), want="[500.0, 15.0, 300.0, 13.0]")
    check(got["mode2"] == SF.CALC_MANUAL
          and got["manual"] == [500.0, 15.0, 300.0, 13.0]
          and got["mannow"] == [200.0, 12.0, 300.0, 13.0], "5 MANUAL",
          "under Manual nothing moves on an edit and Calculate Now moves it",
          got="mode %d, %r then %r" % (got["mode2"], got["manual"],
                                       got["mannow"]),
          want="mode 2, unchanged, then [200.0, 12.0, 300.0, 13.0]")
    check(got["del"] == [200.0, 12.0, 0.0, 10.0], "6 DEL",
          "Del does not go through sh_commit, so it owes its own re-run: "
          "emptying B3 makes row 3's input 0, and 0*100 is 0 with 0+10 "
          "being 10. Without the hook row 3 would still read 300 and 13",
          got=repr(got["del"]), want="[200.0, 12.0, 0.0, 10.0]")
    check(got["s2start"] == [21.0, 31.0] and got["s2after"] == [21.0, 31.0]
          and got["s1ghost"] == [None, None], "7 SHEET2",
          "a table on SHEET 2, re-run by a commit made while SHEET 1 is "
          "showing, still reads and writes Sheet 2 - and leaves no ghost at "
          "the same coordinates on Sheet 1. sh_getcell2 and sh_setvald work "
          "on [sh_cursheet], so a walk that did not set it from each "
          "record's own sheet byte would read the wrong grid and write its "
          "answers onto the visible one. THE GHOST IS THE SHARP HALF: "
          "Sheet 2's values could survive by simply not being touched, "
          "where a cell appearing on Sheet 1 can only have come from this "
          "walk. 81.114.4's sh_table_unbank had exactly this defect for a "
          "cycle and every fixture used Sheet 1 only",
          got="sheet2 %r -> %r, sheet1 ghost %r" % (got["s2start"],
                                                    got["s2after"],
                                                    got["s1ghost"]),
          want="[21.0, 31.0] both times, and no Sheet 1 cells")
    done("sheettblcalc")


if __name__ == "__main__":
    sys.exit(main())
