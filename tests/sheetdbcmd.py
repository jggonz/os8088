#!/usr/bin/env python3
"""Data > Find, Extract and Delete (SPEC.md 81.71).

    make && python3 tests/sheetdbcmd.py

81.65 built the criteria engine and 81.69 the two names it reads its
rectangles from; these three commands are that engine driven from the MENU,
and what has to be proved is that the menu reaches it - that the DATABASE and
CRITERIA names resolve into the same sh_db_*/sh_cr_* a formula's arguments
fill, and that each command then does its own distinct thing to the rows that
match.

The database (A1:C4) is three records over three fields, with Type the
criterion and Amount the payload - three because the CGA window shows four
rows at once and every cell this test CLICKS has to be on the glass:

    Name    Type   Amount
    Apple   Fruit  10
    Beet    Veg    20
    Cherry  Fruit  30

E1:E2 is the criteria range - the header `Type` over the condition `Fruit` -
so the two Fruit records match and they are NOT adjacent: a Delete that
shifted the wrong way, or an Extract that stopped at the first gap, cannot
pass by luck.

  1. FIND selects the first matching record (Apple, row 2) and selects it
     WHOLE - a range, which the grid draws with a second inset frame, so the
     right edge of the selection is column C's and not column A's
  2. the item now reads `Exit Find`; choosing it puts the label back to
     `Find` with the record still selected, and the NEXT Find steps to Cherry
     in row 4 - the "first record BELOW the active cell" rule, which is the
     Reference Guide's own find-and-replace loop
  3. EXTRACT copies both matching records into G1:H3, whose header row names
     two of the three fields in the OTHER order (Amount, Name) - a subset,
     reordered, which is Excel's own rule
  4. DELETE removes both Fruit records; Beet shifts UP inside the database to
     fill the gap, and - the part that separates this from Edit > Delete -
     the cell parked at C6, below the database, does NOT move

Everything is read back out of SHEET's own SYLK save, except the Find
selection and the relabelled menu item, which only exist on the glass.
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
import os88sym                                              # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import glass                                                 # noqa: E402

WORK = "build/sheetdbcmd"               # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetdbcmd.img"
NAME = "DBCMD.SLK"
DATA = (311, 45)                        # the Data menu, tests/sheetdb.py's
ITEM = lambda x, i: (x + 17, 57 + 12 * i + 2)
FORM = 0                                # 81.71's Excel-order Data menu:
FIND, EXTRACT, DELETE = 1, 2, 3         # Form, Find, Extract, Delete,
SET_DB, SET_CRIT = 4, 5                 # Set Database, Set Criteria, Sort
# 81.115: the radio dialog's OK is DERIVED now - SF.fdlg_okxy(m)
ALERT_OK = (0, 0)                       # filled in from the alert's geometry

# --- the sheet -----------------------------------------------------------
DB = {
    (0, 0): 'Name',   (0, 1): 'Type',  (0, 2): 'Amount',
    (1, 0): 'Apple',  (1, 1): 'Fruit', (1, 2): 10.0,
    (2, 0): 'Beet',   (2, 1): 'Veg',   (2, 2): 20.0,
    (3, 0): 'Cherry', (3, 1): 'Fruit', (3, 2): 30.0,
}
CRIT = {(0, 4): 'Type', (1, 4): 'Fruit'}        # E1:E2
EXHDR = {(0, 6): 'Amount', (0, 7): 'Name'}      # G1:H1 - a SUBSET, REORDERED
BELOW = {(5, 2): 999.0}                          # C6, below the database


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells = dict(DB)
    cells.update(CRIT)
    cells.update(EXHDR)
    cells.update(BELOW)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk(cells))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def num(v):
    if isinstance(v, tuple) and v and v[0] == 'formula':
        v = v[2]
    return v


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    seen = {}
    data = None
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
            done("sheetdbcmd")
            return
        ys, xs = g
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)
        table = glass.glyphs(m)

        def select(r1, c1, r2, c2):
            mo.click(*at(r1, c1))
            M.settle(m)
            if (r1, c1) != (r2, c2):
                m.key("ShiftLeft", down=True, up=False)
                mo.click(*at(r2, c2))
                m.key("ShiftLeft", down=False, up=True)
                M.settle(m)

        def menu(i):
            mo.menu(DATA[0], DATA[1], *ITEM(DATA[0], i))
            M.settle(m)

        def look(tag):
            """The reference box names the selection's ANCHOR, which is how
            Find's answer is read without touching the selection to read it;
            and the selection's own right EDGE says how wide it is. A range
            gets a second, inset frame (sh_drawsel), so its edge is TWO
            adjacent inked columns where a plain gridline is one."""
            mo.to(634, 180)
            M.settle(m)
            _, _, rw = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rw)
            seen[tag] = glass.cell_text(rw, (56, 57, 122, 71), table)
            seen[tag + "-wide"] = (doubled(rw, 1, xs[3]),
                                   doubled(rw, 1, xs[1]))

        def doubled(rw, r, x0):
            """is the selection's own frame at x0 on screen row r? A RANGE
            gets a second inset frame (sh_drawsel), so its edge is two
            ADJACENT inked columns where a plain gridline is one. Sampled at
            the vertical middle of the row band, which is clear of the
            horizontal frame lines, and a column's default width leaves x0
            well clear of the text at the cell's left."""
            y = (ys[r] + ys[r + 1]) // 2
            for x in range(x0 - 2, x0 + 2):
                if not rw[y][x] and not rw[y][x + 1]:
                    return True
            return False

        # --- bind the two names ---------------------------------------------
        select(0, 0, 3, 2)                  # A1:C4 -> DATABASE
        menu(SET_DB)
        select(0, 4, 1, 4)                  # E1:E2 -> CRITERIA
        menu(SET_CRIT)

        # --- 1/2: Find, Exit Find, Find again --------------------------------
        # That is the Reference Guide's own loop, and the relabel is what
        # enforces it: "When you exit Data Find, the matching record you last
        # found stays selected. If you choose Data Find again, Microsoft Excel
        # selects the next matching record. This means that you can find and
        # replace matching database records by choosing Data Find to find the
        # next matching record after each edit."
        select(0, 0, 0, 0)                  # start at the header, above the
        menu(FIND)                          # first record
        look("1-find")

        def itemlabel(tag):
            """the first item's own text, read MID-HOLD: a menu closes on
            release, so the pull-down only exists between the two edges"""
            mo.to(DATA[0], DATA[1])
            mo._edge(True)
            mo.to(*ITEM(DATA[0], 6), l=True)  # hover SORT, not the item being
            M.settle(m)                      # read: the one under the pointer
            _, _, rw = m.vram("cga")         # is INVERTED, and glass reads
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rw)
            seen[tag] = glass.cell_text(
                rw, (DATA[0] - 9, 67, DATA[0] + 101, 80), table)
            mo.to(DATA[0] - 240, 190, l=True)   # off the menu entirely, so
            mo._edge(False)                     # the release fires nothing
            M.settle(m)

        itemlabel("2-exitlabel")            # ...reads Exit Find now
        menu(FIND)                          # which is what this chooses
        itemlabel("3-findlabel")            # ...and it is Find again
        menu(FIND)                          # so THIS one steps to the next
        look("4-findagain")
        menu(FIND)                          # leave find mode for good
        M.settle(m)

        # --- 3: Extract into G1:H3 ------------------------------------------
        select(0, 6, 2, 7)
        menu(EXTRACT)
        mo.click(*SF.fdlg_okxy(m))                # All Matching Records (index 0)
        M.settle(m, limit=120)

        # --- 4: Delete, through its alert -----------------------------------
        menu(DELETE)
        M.settle(m, limit=120)
        _, _, rw = m.vram("cga")
        M.write_png(os.path.join(WORK, "4-alert.png"), 640, 200, rw)
        m.type_text("\n")                   # the alert's default button is Yes
        M.settle(m, limit=180)

        before = open(os.path.join(WORK, NAME), "rb").read()
        mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0], SF.SAVE_AS[1])
        M.settle(m)
        mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['slk']))
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m, limit=120)
        mo.click(*SF.SAVE_BUTTON)
        for _ in range(90):
            v = os88flush.Flush(marty=m).volume(1)
            if NAME in v.names() and v.read(NAME) != before:
                data = v.read(NAME)
                break
            M.settle(m, quiet=2.0, stable=2, limit=60)

    check(data is not None, "SHEET saved the sheet", "DBCMD.SLK never changed")
    got = F.read_sylk(data) if data else {}
    g = lambda r, c: num(got.get((r, c)))

    check(seen.get("1-find") == 'A2',
          "Data > Find lands on the first matching record, Apple",
          "the reference box reads %r" % (seen.get("1-find"),))
    check(seen.get("2-exitlabel") == 'Exit Find',
          "while a find is live the item renames itself to Exit Find",
          "the Data menu's first item reads %r" % (seen.get("2-exitlabel"),))
    check(seen.get("3-findlabel") == 'Find',
          "...and choosing that puts it back to Find, the record it found "
          "still selected",
          "it reads %r" % (seen.get("3-findlabel"),))
    check(seen.get("4-findagain") == 'A4',
          "...so the next Find steps to the match after it, Cherry - the "
          "'first record below the active cell' rule, and the Reference "
          "Guide's own find-and-replace loop",
          "it reads %r" % (seen.get("4-findagain"),))
    wide = seen.get("1-find-wide")
    check(wide == (True, False),
          "...and the record is selected WHOLE: the selection's doubled frame "
          "is at column C's right edge and column A's is a plain gridline",
          "doubled at (C's edge, A's edge) = %r" % (wide,))
    # 3: Extract wrote Amount into G, Name into H, both matching records, in
    # database order and with the Veg record between them skipped
    check(g(1, 6) == 10.0 and g(1, 7) == 'Apple',
          "Extract's first output row is Apple, its fields in the EXTRACT "
          "range's order (Amount, Name) and not the database's",
          "G2, H2 hold %r, %r" % (g(1, 6), g(1, 7)))
    check(g(2, 6) == 30.0 and g(2, 7) == 'Cherry',
          "...and its second is Cherry, the Veg record between them skipped",
          "G3, H3 hold %r, %r" % (g(2, 6), g(2, 7)))
    # 4: Delete compacted the database and left the cell below it alone
    check(g(1, 0) == 'Beet' and g(1, 2) == 20.0,
          "Delete removed both Fruit records and shifted Beet up into the gap "
          "they left, carrying every field of it and not the key column alone",
          "A2, C2 hold %r, %r" % (g(1, 0), g(1, 2)))
    check(g(2, 0) is None and g(3, 0) is None,
          "...and blanked the rows the records vacated",
          "A3, A4 hold %r, %r" % (g(2, 0), g(3, 0)))
    check(g(5, 2) == 999.0,
          "the cell BELOW the database did not move - this is Data > Delete, "
          "not Edit > Delete, and the rest of the sheet is untouched",
          "C6 holds %r" % (g(5, 2),))
    done("sheetdbcmd")


if __name__ == "__main__":
    main()
