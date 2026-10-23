#!/usr/bin/env python3
"""A table's stored RECTANGLE follows the grid (SPEC.md 81.114.9)

    make && python3 tests/sheettblshift.py

Insert/Delete Row/Column move the cells, and they move a result cell's MARK
with them for free - the mark lives in the border byte, and sh_rc_sides
already shifts that table. The 16-byte table RECORD did not move, so a table
whose sheet gained a row above it was guarded against a rectangle one row too
high: a clear refused when it should be allowed, or allowed when it should be
refused.

sh_rc_table is the generic shifter for a sparse table whose record begins
(packed row/sheet, column). It does not fit, because a table record is not a
point - and each of those differences is a check here:

  1 ABOVE     a row inserted ABOVE the table moves the whole rectangle down,
              and the INPUT CELL with it
  2 INSIDE    a row inserted INSIDE it GROWS it: r2 moves, r1 does not
  3 SHRINK    a row deleted inside shrinks it again
  4 COLUMN    and a column inserted left moves c1, c2 and the input column
  5 UNDO      ...and Undo puts the RECORD back, not only the cells. The
              snapshot did not carry the table list until 81.114.9, so an
              undone Insert Row restored the cells and the marks and left
              every rectangle shifted - a half-restoring Undo, which is worse
              than none because the user believes it
  6 TOPROW    deleting the table's TOP row takes the formulas with it, so the
              table is dropped and its marks go
  7 UNDROP    ...and Undo brings that whole table back, count and all. Check
              5 undoes an operation that leaves [sh_ntbl] alone, so a build
              that restored the records and not the COUNT passed it
  8 SORT      and a sort over a table's range drops it too, with the status
              line saying so - a sort is an arbitrary permutation, and the
              results are still the right numbers in an order nothing can
              reconstruct

EVERY RECTANGLE IS READ OUT OF THE RECORD ITSELF, never inferred from what
the guard then allowed: the cells moving and the record moving are different
facts, and the whole defect is the two disagreeing.

THE FIXTURE IS SHAPED BY THREE MUTATIONS THAT REFUSED TO FAIL, and each one
is a fixture fault rather than a missing assertion - see CELLS below and
check 7. A one-input table left half the input-shifting code unentered; an
input cell sitting on the table's own top row made the TOP-ROW rule
unobservable, the table being dropped for the other reason; and an Undo that
does not change the number of tables cannot notice a count that was never
restored.
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

WORK = "build/sheettblshift"
DISK = "build/sheettblshift.img"
NAME = "SHIN.SLK"

# A TWO-INPUT table at B1:D3, with both input cells BELOW it at A5 and A6.
# Both of those choices were forced by mutations that refused to fail:
#
#   two-input    a one-input table leaves sh_tbl_ir/ic at SH_TBL_NONE, so
#                HALF the input-shifting code is never entered and a
#                mutation that broke it passed all eight checks
#   below it     with the input cell on the table's own top row, deleting
#                that row dropped the table through the INPUT-CELL rule, and
#                a mutation that removed the TOP-ROW rule passed - the table
#                went either way, for the other reason
#
# The corner formula is B1, the values across the top are C1/D1 and the ones
# down the left are B2/B3, which is Excel's two-input shape.
CELLS = {
    (0, 1): ('formula', 'A5*A6+A5', 0.0),         # B1, the corner formula
    (0, 2): 4.0, (0, 3): 5.0,                     # C1 D1, across the top
    (1, 1): 2.0, (2, 1): 3.0,                     # B2 B3, down the left
}


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
            done("sheettblshift")
            return
        ys, xs = g
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)
        rec = lambda: SF.tbl_rec(m, seg, sym, 0)
        ntbl = lambda: SF._w(m, seg, sym["sh_ntbl"])

        def select(r1, c1, r2, c2):
            mo.click(*at(r1, c1))
            M.settle(m)
            m.key("ShiftLeft", down=True, up=False)
            mo.click(*at(r2, c2))
            m.key("ShiftLeft", down=False, up=True)
            M.settle(m)

        ROWCOL = lambda lbl: SF.radio_row(m, seg, sym, "sh_fd_i_rowcol", lbl)

        def structural(item, which):
            """Edit > Insert.../Delete..., pick Row or Column, OK."""
            SF.menu_pick(m, mo, seg, sym, "sh_i_edit", item)
            M.settle(m, limit=120)
            SF.fdlg_ok(m, mo, seg, sym, row=ROWCOL(which))
            M.settle(m, limit=240)

        def table(r1, c1, r2, c2, rowin, colin):
            select(r1, c1, r2, c2)
            SF.menu_pick(m, mo, seg, sym, "sh_i_data", "dtable")
            M.settle(m, limit=120)
            m.type_text(rowin + "\n")             # Row Input Cell...
            M.settle(m, limit=120)
            m.type_text(colin + "\n")             # ...then Column Input Cell
            M.settle(m, limit=240)

        table(0, 1, 2, 3, "A5", "A6")             # B1:D3, BOTH input cells
        got["start"] = rec()

        # --- 1 ABOVE: a row at row 0 pushes the whole table down -----------
        mo.click(*at(0, 0))
        M.settle(m)
        structural("insert", "sh_fd_rcrow")
        got["above"] = rec()

        # --- 2 INSIDE: the table is rows 1..3 now; a row at 2 GROWS it -----
        mo.click(*at(2, 0))
        M.settle(m)
        structural("insert", "sh_fd_rcrow")
        got["inside"] = rec()

        # --- 3 SHRINK: ...and deleting it again ----------------------------
        mo.click(*at(2, 0))
        M.settle(m)
        structural("delete", "sh_fd_rcrow")
        got["shrink"] = rec()

        # --- 4 COLUMN: a column at A moves c1, c2 and the input column -----
        mo.click(*at(1, 0))
        M.settle(m)
        structural("insert", "sh_fd_rccol")
        got["column"] = rec()

        # --- 5 UNDO: the RECORD comes back, not only the cells -------------
        SF.menu_pick(m, mo, seg, sym, "sh_i_edit", "undo")
        M.settle(m, limit=240)
        got["undo"] = rec()

        # --- 6 TOPROW: deleting the table's own top row drops it -----------
        got["markb4"] = SF.bt_byte(m, seg, sym, 2, 2)
        mo.click(*at(1, 0))                       # row 1 is the table's r1
        M.settle(m)
        structural("delete", "sh_fd_rcrow")
        got["topn"] = ntbl()
        got["markafter"] = SF.bt_byte(m, seg, sym, 2, 2)

        # --- 7 UNDO A DROP: the table COMES BACK, count and all ------------
        SF.menu_pick(m, mo, seg, sym, "sh_i_edit", "undo")
        M.settle(m, limit=240)
        got["undropn"] = ntbl()
        got["undrop"] = rec()

        # --- 8 SORT over the table check 7 just brought back ---------------
        got["sortn0"] = ntbl()
        select(1, 1, 3, 3)                        # its restored rectangle
        SF.menu_pick(m, mo, seg, sym, "sh_i_data", "sort")
        M.settle(m, limit=120)
        m.type_text("\n")                        # SORT IS TWO DIALOGS (81.15):
        M.settle(m, limit=120)                   # the KEY first, defaulted to
        SF.fdlg_ok(m, mo, seg, sym)              # the anchor, then Ascending/
        M.settle(m, limit=300)                   # Descending
        got["sortn"] = ntbl()
        got["sortmsg"] = SF._w(m, seg, sym["sh_msg"])

    R = lambda d: None if d is None else (d["r1"], d["r2"], d["c1"], d["c2"],
                                          d["ir"], d["ic"],
                                          d["ir2"], d["ic2"])
    check(R(got["start"]) == (0, 2, 1, 3, 4, 0, 5, 0), "0 RECORDED",
          "the table as the dialog stored it: rows 0-2, columns 1-3, a ROW "
          "input cell at A5 and a COLUMN input cell at A6. Both are live, "
          "which is what makes the two input-shifting arms testable at all",
          got=repr(R(got["start"])), want="(0, 2, 1, 3, 4, 0, 5, 0)")
    check(R(got["above"]) == (1, 3, 1, 3, 5, 0, 6, 0), "1 ABOVE",
          "a row inserted ABOVE the table moves the whole rectangle down one "
          "- AND THE INPUT CELL WITH IT, which is the half a shifter written "
          "for a point record would never have: A1 is now A2, and a table "
          "still substituting into A1 would read a cell the user never filled",
          got=repr(R(got["above"])), want="(1, 3, 1, 3, 5, 0, 6, 0)")
    check(R(got["inside"]) == (1, 4, 1, 3, 6, 0, 7, 0), "2 INSIDE",
          "a row inserted INSIDE it GROWS it: r2 moves and r1 does not. This "
          "is the difference between a rectangle and a point, and the reason "
          "sh_rc_table could not be reused",
          got=repr(R(got["inside"])), want="(1, 4, 1, 3, 6, 0, 7, 0)")
    check(R(got["shrink"]) == (1, 3, 1, 3, 5, 0, 6, 0), "3 SHRINK",
          "...and deleting that row shrinks it back",
          got=repr(R(got["shrink"])), want="(1, 3, 1, 3, 5, 0, 6, 0)")
    check(R(got["column"]) == (1, 3, 2, 4, 5, 1, 6, 1), "4 COLUMN",
          "a column inserted at A moves c1, c2 and the input cell's COLUMN - "
          "c1 and c2 are single BYTES in the record, as BIFF3's DATATABLE "
          "has them, so this is also the check that the byte fields are "
          "written back",
          got=repr(R(got["column"])), want="(1, 3, 2, 4, 5, 1, 6, 1)")
    check(R(got["undo"]) == (1, 3, 1, 3, 5, 0, 6, 0), "5 UNDO",
          "Undo puts the RECORD back, not only the cells. The snapshot did "
          "not carry the table list at all until this stage, so an undone "
          "Insert Column restored every cell and every mark and left the "
          "rectangle two columns along - the cells and the record "
          "disagreeing, which is the exact defect this whole stage exists "
          "to prevent",
          got=repr(R(got["undo"])), want="(1, 3, 1, 3, 5, 0, 6, 0)")
    check(got["markb4"] is not None and got["topn"] == 0
          and (got["markafter"] is None
               or not (got["markafter"] & SF.SH_TBL_RESULT)), "6 TOPROW",
          "deleting the table's own TOP row takes the formulas with it, so "
          "there is no table left: the record goes AND the result cells are "
          "unmarked. Without the unmark those cells would refuse edits for "
          "ever, with no table to explain why",
          got="mark %r -> %r, [sh_ntbl]=%d" % (got["markb4"],
                                               got["markafter"],
                                               got["topn"]),
          want="a mark, then none, and 0 tables")
    check(got["undropn"] == 1 and R(got["undrop"]) == (1, 3, 1, 3, 5, 0, 6, 0),
          "7 UNDROP",
          "Undo of that delete brings the TABLE back, not only its cells - "
          "which is the half of the snapshot that the COUNT buys. Check 5 "
          "above undoes an operation that leaves [sh_ntbl] unchanged, so a "
          "build that never restored the count passed it; this one restores "
          "a table that was dropped, and cannot",
          got="[sh_ntbl]=%d, %r" % (got["undropn"], R(got["undrop"])),
          want="1, and (1, 3, 1, 3, 5, 0, 6, 0)")
    check(got["sortn0"] == 1 and got["sortn"] == 0
          and got["sortmsg"] == sym["sh_s_sorttbl"], "8 SORT",
          "a sort over a table's range drops it, and SAYS SO. A sort is an "
          "arbitrary permutation: the results are still the right numbers, "
          "in an order nothing can reconstruct, so they stay where the sort "
          "put them and stop being a table. A table silently ceasing to be "
          "one is the kind of thing a user finds out about much later, which "
          "is why the status line is part of this check",
          got="%d -> %d tables, [sh_msg]=0x%04X" % (got["sortn0"],
                                                    got["sortn"],
                                                    got["sortmsg"]),
          want="1 -> 0, and 0x%04X" % sym["sh_s_sorttbl"])
    done("sheettblshift")


if __name__ == "__main__":
    sys.exit(main())
