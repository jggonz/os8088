#!/usr/bin/env python3
"""A Data > Table is PROTECTED, and clears all-or-nothing (SPEC.md 81.114.7)

    make && python3 tests/sheettbl2.py

tests/sheettable.py proves the numbers; this proves the thing that makes them
a table rather than a one-shot fill. Excel 2.1d's Reference Guide, pp. 150-156:
"Other than copying, you cannot edit cells inside the table. If you try to
edit a portion of the table, Microsoft Excel displays a message", and
"Clearing the Values in a Table: ... You must clear all the resulting values."

  1 RECORDED  both tables outlive their runs - [sh_ntbl] is 2. Without a
              record of the rectangle neither rule below can be asked
  2 REFUSED   typing into a result cell sets the status line to sh_s_tblpart
  3 PART      a Clear over HALF a result area is refused the same way, and
              both tables are still there
  4 FORMATS   ...but a Clear of FORMATS over that same half is ALLOWED. A
              table's cells can be formatted - the guide tells you to - so
              taking a number format off them is a format change and not an
              edit of the values
  5 FMTKEEP   ...AND IT DID NOT UN-PROTECT THEM. Clearing formats took the
              whole border record, which is where the mark and the two
              protection bits live, so Formats silently made a table's cells
              editable (and reset any cell's Locked/Hidden - an 81.46 defect
              older than this feature). Read off the border table itself
  6 WHOLE     a Clear over ALL of one table's results is allowed and takes
              that table with it: [sh_ntbl] goes 2 -> 1
  7 DROPPED   ...so those cells take a typed value now, WHILE THE SECOND
              TABLE KEEPS THE GUARD ARMED
  8 FORMULAS  and the second table, cleared the OTHER allowed way, loses its
              marks too. Clear > All removes each cell's whole border record
              and takes the mark with it whatever else happens; Clear >
              FORMULAS deliberately KEEPS that record (81.63: a cell with a
              border survives its contents), so the mark has to be taken off
              by the drop itself or it is orphaned there for ever
  9 INPUT     both input cells, which NEVER EXISTED, are still absent; the
              typed 99 is in C2; and every result cell of both tables is gone

CHECKS 8 AND 9 OVERLAP on the same fix and say so: 8 reads the border table,
9 reads the file. With the marks orphaned, sh_clear_one sees a cell that
still has a border record and KEEPS it holding zero, so the file shows 0.0
where it should show nothing - which is the same defect seen from the other
end. Neither row asserts the second table's NUMBERS; sheettable.py does that,
over two tables, and this row is about the rules.

THERE ARE TWO TABLES FOR CHECK 6's SAKE, and that is a measured requirement
rather than thoroughness. sh_prot_blocked short-circuits on
`[sh_ntbl] == 0` - an unprotected document with no table is every document
this app had before the feature, and the scan must cost it nothing - so with
ONE table, clearing it disarms the guard and the cell is editable again
whether or not its mark was removed. The first version of this row had one
table, and the mutation that leaves the mark behind PASSED ALL SIX CHECKS.

THE INPUT CELLS ARE DELIBERATELY ABSENT. sheettable.py's fixture gives A1, A2
and A3 real zeroes, so its "4 INTACT" check exercises only the restore-what-
was-there arm; this fixture takes the other one, where sh_table_subst CREATES
the cell and unbanking has to take it away again.

THE MENU IS DRIVEN BY NAME, through sheetfmt.menu_pick: the item's row comes
out of the assembler's map and the pull-down's rect out of the guest with the
button still down. Three rows broke when Data > Table... was inserted at index
8, and a shifted index that lands on another LIVE item would not have broken
anything - it would have run the wrong command and blamed the feature.
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
from paintmove import pkg_syms                                # noqa: E402
from os88geom import WIN_SIZE, W_SEG                          # noqa: E402

WORK = "build/sheettbl2"
DISK = "build/sheettbl2.img"
NAME = "SHIN.SLK"

# TWO one-input tables with COLUMN input cells, side by side in rows 1-3 so
# every cell of both can be selected by CLICKING. Neither input cell is in
# the fixture: the formulas read an empty cell and each table has to put that
# emptiness back. NO TWO EXPECTED VALUES AGREE, in either table or across
# them, so a substitution that went to the wrong place cannot land on a right
# answer by luck.
CELLS = {
    (0, 2): ('formula', 'A1*2', 0.0),             # C1 }  table ONE: formulas
    (0, 3): ('formula', 'A1+10', 10.0),           # D1 }  across the top row
    (1, 1): 4.0,                                  # B2 }  values down the left
    (2, 1): 9.0,                                  # B3 }  column, input A1

    (0, 6): ('formula', 'A5*3', 0.0),             # G1 }  table TWO, input A5
    (0, 7): ('formula', 'A5+100', 100.0),         # H1 }
    (1, 5): 2.0,                                  # F2 }
    (2, 5): 7.0,                                  # F3 }
}
ONE = {(1, 2): 8.0, (1, 3): 14.0,                 # C2 D2  (input 4)
       (2, 2): 18.0, (2, 3): 19.0}                # C3 D3  (input 9)
TWO = {(1, 6): 6.0, (1, 7): 102.0,                # G2 H2  (input 2)
       (2, 6): 21.0, (2, 7): 107.0}               # G3 H3  (input 7)
TYPED = 99.0


def main():
    os.chdir(os.path.join(HERE, ".."))
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk(CELLS))
    before = open(src, "rb").read()
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "build/sheet.o88", "build/CHART.OVL",
                    "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)
    S = os88sym.linear
    sym = pkg_syms("apps/sheet/sheet.asm")
    got = {}
    saved = None

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
        word = lambda n: SF._w(m, seg, sym[n])
        mo.to(634, 180)
        M.settle(m)

        def mark(col, row):
            """SH_TBL_RESULT as the border table actually holds it."""
            b = SF.bt_byte(m, seg, sym, col, row)
            return None if b is None else bool(b & SF.SH_TBL_RESULT)

        def shot(tag):
            w, h, rgb = m.fbuf()
            M.write_png_rgb(os.path.join(WORK, tag + ".png"), w, h, rgb)
            return "%s: nwin=%d C2mark=%r" % (
                tag, len(dispcp.win_list(m, S, check=False)), mark(2, 1))
        print("   ", shot("0-open"))

        g = glass.grid(*m.vram("cga"))
        if not g:
            check(False, "the grid is on the glass", "no grid")
            done("sheettbl2")
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

        def clear(row=None):
            """Edit > Clear...: `row` None leaves the radio on All (0), 2 is
            Formats. Both the item and OK come out of the guest."""
            SF.menu_pick(m, mo, seg, sym, "sh_i_edit", "clear")
            M.settle(m, limit=120)
            SF.fdlg_ok(m, mo, seg, sym, row=row)
            M.settle(m, limit=180)

        def typed_into(r, c, what):
            """Type into (r,c) and report [sh_msg] afterwards."""
            mo.click(*at(r, c))
            M.settle(m)
            m.type_text(what + "\n")
            M.settle(m, limit=120)
            return word("sh_msg")

        # --- the two tables -------------------------------------------------
        def table(r1, c1, r2, c2, incell):
            select(r1, c1, r2, c2)
            SF.menu_pick(m, mo, seg, sym, "sh_i_data", "dtable")
            M.settle(m, limit=120)
            m.type_text("\n")                     # Row Input Cell: none...
            M.settle(m, limit=120)
            m.type_text(incell + "\n")            # ...so this is a COLUMN one
            M.settle(m, limit=240)

        table(0, 1, 2, 3, "A1")                   # B1:D3
        table(0, 5, 2, 7, "A5")                   # F1:H3
        got["ntbl"] = word("sh_ntbl")
        print("   ", shot("1-filled"), "ntbl", got["ntbl"])

        # --- 2: an edit inside it -------------------------------------------
        got["typemsg"] = typed_into(1, 2, "%d" % TYPED)
        print("   ", shot("2-typed"))

        # --- 3: half of it --------------------------------------------------
        select(1, 2, 2, 2)                        # C2:C3, half of table one
        clear()
        got["partmsg"] = word("sh_msg")
        got["partn"] = word("sh_ntbl")
        print("   ", shot("3-part"), "ntbl", got["partn"])

        # --- 4: ...and the same half, FORMATS ONLY --------------------------
        select(1, 2, 2, 2)
        clear(row=2)                              # Formats
        got["fmtmsg"] = word("sh_msg")
        got["fmtn"] = word("sh_ntbl")
        got["fmtmark"] = mark(2, 1)
        print("   ", shot("4-formats"), "ntbl", got["fmtn"])

        # --- 5: all of table one --------------------------------------------
        select(1, 2, 2, 3)                        # C2:D3, every result cell
        clear()
        got["wholen"] = word("sh_ntbl")
        got["wholemsg"] = word("sh_msg")
        print("   ", shot("5-whole"), "ntbl", got["wholen"])

        # --- 7: ...and now that cell is ordinary, with table two still up ---
        got["c2mark"] = mark(2, 1)                # BEFORE the type: the clear
        got["afmsg"] = typed_into(1, 2, "%d" % TYPED)
        got["afn"] = word("sh_ntbl")
        print("   ", shot("6-after"), "ntbl", got["afn"])

        # --- 8: table two, cleared the OTHER allowed way --------------------
        got["g2mark0"] = mark(6, 1)               # G2 is marked going in
        select(1, 6, 2, 7)                        # G2:H3, all of table two
        clear(row=1)                              # Formulas
        got["fmln"] = word("sh_ntbl")
        got["g2mark"] = mark(6, 1)
        print("   ", shot("7-formulas"), "ntbl", got["fmln"],
              "G2mark", got["g2mark"])

        mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0], SF.SAVE_AS[1])
        M.settle(m)
        mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['slk']))
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m, limit=120)
        mo.click(*SF.SAVE_BUTTON)
        for _ in range(60):
            v = os88flush.Flush(marty=m).volume(1)
            if NAME in v.names() and v.read(NAME) != before:
                saved = v.read(NAME)
                break
            M.settle(m, quiet=2.0, stable=2, limit=60)

    part = sym["sh_s_tblpart"]
    check(got["ntbl"] == 2, "1 RECORDED",
          "a table outlives the run that made it - the rectangle and its "
          "input cells go in the sh_tblseg list, which is what every rule "
          "below is asked against",
          got=got["ntbl"], want=2)
    check(got["typemsg"] == part, "2 REFUSED",
          "typing into a result cell is refused, and the status line says "
          "'Cannot change part of a table.' - sh_prot_blocked reads "
          "SH_TBL_RESULT out of the border byte it already fetches",
          got="[sh_msg]=0x%04X" % got["typemsg"], want="0x%04X" % part)
    check(got["partmsg"] == part and got["partn"] == 2, "3 PART",
          "a Clear over HALF a result area is refused the same way and both "
          "tables survive: Excel's 'You must clear all the resulting values'",
          got="[sh_msg]=0x%04X [sh_ntbl]=%d" % (got["partmsg"], got["partn"]),
          want="0x%04X and 2" % part)
    check(got["fmtmsg"] != part and got["fmtn"] == 2, "4 FORMATS",
          "the SAME half-selection, cleared as FORMATS, is allowed - a "
          "table's cells can be formatted, so taking a number format off "
          "them is a format change and not an edit of the values. This is "
          "the one branch of sh_tbl_clearguard that lets a partial cover "
          "through, and 3 above is its control: same cells, same menu, the "
          "radio row is the only difference",
          got="[sh_msg]=0x%04X [sh_ntbl]=%d" % (got["fmtmsg"], got["fmtn"]),
          want="anything but 0x%04X, and 2" % part)
    check(got["fmtmark"] is True, "5 FMTKEEP",
          "...and that clear did NOT un-protect the cells. SH_TBL_RESULT and "
          "the two protection bits share the border byte with the edges and "
          "the shade, and Clear > Formats removed the whole record - so it "
          "reset any cell's Locked/Hidden to the default (an 81.46 defect "
          "older than this feature) and, once the table mark joined them, "
          "made a table's result cells editable while the table itself "
          "stayed recorded. READ OFF THE BORDER TABLE, not inferred from "
          "what the guard then did: check 4 above passed throughout, because "
          "whether the clear was allowed is a different fact",
          got="C2 mark after Clear > Formats = %r" % (got["fmtmark"],),
          want=True)
    check(got["wholen"] == 1, "6 WHOLE",
          "a Clear that covers EVERY result cell of ONE table is allowed and "
          "drops that table - sh_tbl_clearguard agrees and sh_tbl_dropsel "
          "forgets it, leaving the other one alone",
          got="[sh_ntbl]=%d, [sh_msg]=0x%04X" % (got["wholen"],
                                                 got["wholemsg"]),
          want=1)
    check(got["afmsg"] != part and got["afn"] == 1
          and got["c2mark"] is not True, "7 DROPPED",
          "...so the same cell takes a typed value now - AND TABLE TWO IS "
          "STILL THERE to keep the guard armed. MEASURED NECESSARY: with one "
          "table, clearing it takes [sh_ntbl] to 0 and sh_prot_blocked "
          "short-circuits before it ever reads a mark, so the mutation that "
          "leaves the cleared cells MARKED passed all six checks of the "
          "one-table version of this row",
          got="C2 mark=%r then [sh_msg]=0x%04X [sh_ntbl]=%d"
              % (got["c2mark"], got["afmsg"], got["afn"]),
          want="the mark gone, anything but 0x%04X, and 1" % part)
    check(got["g2mark0"] is True and got["fmln"] == 0
          and got["g2mark"] is not True, "8 FORMULAS",
          "the second table cleared as FORMULAS: dropped, and its marks gone "
          "with it. MEASURED NECESSARY - Clear > All removes each cell's "
          "whole border record in sh_clear_one and takes the mark along "
          "whatever else happens, so with ALL alone the mutation that stops "
          "sh_tbl_dropsel unmarking passed ALL EIGHT checks. 81.63 keeps the "
          "record on this path on purpose, which is exactly why the drop has "
          "to do the unmarking",
          got="G2 mark %r -> %r, [sh_ntbl]=%d" % (got["g2mark0"],
                                                  got["g2mark"], got["fmln"]),
          want="True -> gone, and 0")

    if not saved:
        check(False, "9 INPUT", "SHEET never wrote the file back",
              got="no save", want="a SYLK file")
        done("sheettbl2")
        return
    cs = F.read_sylk(saved)
    val = lambda rc: F.scalar(cs.get(rc))
    a1, a5 = val((0, 0)), val((4, 0))
    gone = {rc: val(rc) for rc in sorted(ONE) if rc != (1, 2)}
    two = {rc: val(rc) for rc in sorted(TWO)}
    ok = (a1 is None and a5 is None and val((1, 2)) == TYPED
          and all(v is None for v in gone.values())
          and all(v is None for v in two.values()))
    check(ok, "9 INPUT",
          "A1 and A5 never existed in the fixture, so after two tables that "
          "substituted into them twice each they must still be empty. The "
          "cleared results are gone - BOTH tables' - and the typed one "
          "stands. Table two went through Clear > FORMULAS, which keeps a "
          "cell that still has a border record: with its marks orphaned, "
          "those four cells come back as 0.0 instead of nothing, which is "
          "check 8's defect seen from the file end. All read off what SHEET "
          "wrote, so nothing here is a screen artefact",
          got="A1=%r A5=%r C2=%r cleared=%r two=%r" % (a1, a5, val((1, 2)),
                                                       gone, two),
          want="A1/A5 None, C2 %g, every other result cell None" % TYPED)
    done("sheettbl2")


if __name__ == "__main__":
    sys.exit(main())
