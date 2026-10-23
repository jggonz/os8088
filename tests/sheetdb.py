#!/usr/bin/env python3
"""The database functions, evaluated (SPEC.md 81.65).

    make && python3 tests/sheetdb.py

DAVERAGE DCOUNT DCOUNTA DMAX DMIN DPRODUCT DSTDEV DSTDEVP DSUM DVAR DVARP -
81.65's answer to §81.39's largest single gap. Every one takes exactly three
arguments, DFUNC(database, field, criteria), and the eleven differ only in
which existing accumulator (SUM's, AVERAGE's, ...) they reuse - what this
test is really checking is the criteria engine underneath all of them: field
resolution by name and by number, exact/prefix/wildcard/operator criteria,
AND across a criteria row's columns and OR across its rows, and DCOUNT's
numbers-only rule against DCOUNTA's anything-non-blank one.

The database (A1:D7): Name, Type, Amount, Qty. Fig's Amount is the TEXT
"N/A" rather than a number - deliberately, so DCOUNT and DCOUNTA can
disagree about the Fruit rows (2 against 3) the same way COUNT and COUNTA
first could (81.43), and so every numeric fold below quietly proves it
skips a label rather than crashing on one.

    Name      Type   Amount  Qty
    Apple     Fruit  10      3
    Banana    Fruit  20      5
    Carrot    Veg    15      2
    Daikon    Veg    25      4
    Eggplant  Veg    30      1
    Fig       Fruit  N/A     6

The HOST writes a SYLK file holding the database, seven criteria blocks and
one formula per case; SHEET opens it and saves it as SYLK - every formula
recomputed first (sh_dowrite) - and the host reads the values back.

Then Data > Set Database and Data > Set Criteria, through the real menu on
a real mouse-dragged selection (SPEC.md 81.69): A1:D7 becomes the name
DATABASE, F1:F2 becomes CRITERIA, and a formula typed live -
DSUM(Database,"Amount",Criteria) - answers the identical 30.0 the first
CASE already proved for the same range typed out in full.
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

WORK = "build/sheetdb"                  # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetdb.img"
NAME = "DBASE.SLK"
DATA = (311, 45)                        # the Data menu's own screen position
ITEM = lambda x, i: (x + 17, 57 + 12 * i + 2)  # and a dropdown item's, both
SET_DB, SET_CRIT = 4, 5                        # 81.71's Excel-order Data menu

# --- the database, A1:D7 ------------------------------------------------------
DB = {
    (0, 0): 'Name', (0, 1): 'Type', (0, 2): 'Amount', (0, 3): 'Qty',
    (1, 0): 'Apple',    (1, 1): 'Fruit', (1, 2): 10.0, (1, 3): 3.0,
    (2, 0): 'Banana',   (2, 1): 'Fruit', (2, 2): 20.0, (2, 3): 5.0,
    (3, 0): 'Carrot',   (3, 1): 'Veg',   (3, 2): 15.0, (3, 3): 2.0,
    (4, 0): 'Daikon',   (4, 1): 'Veg',   (4, 2): 25.0, (4, 3): 4.0,
    (5, 0): 'Eggplant', (5, 1): 'Veg',   (5, 2): 30.0, (5, 3): 1.0,
    (6, 0): 'Fig',      (6, 1): 'Fruit', (6, 2): 'N/A', (6, 3): 6.0,
}
# --- criteria blocks, each a separate column group so none overlap -----------
CRIT = {
    (0, 5): 'Type',  (1, 5): 'Fruit',                       # F1:F2
    (0, 7): 'Name',  (1, 7): 'B',                           # H1:H2 prefix
    (0, 9): 'Name',  (1, 9): '*an*',                        # J1:J2 wildcard
    (0, 11): 'Amount', (1, 11): '>15',                      # L1:L2 operator
    (0, 13): 'Type', (0, 14): 'Amount',                     # N1:O2 AND
    (1, 13): 'Fruit', (1, 14): '>10',
    (0, 16): '',                                            # Q1 header-only
    (0, 18): 'Type', (0, 19): 'Amount',                     # S1:T3 OR
    (1, 18): 'Fruit',
    (2, 18): 'Veg',  (2, 19): '>20',
}
WRONG = -999.0
CASES = [
    ('DSUM(A1:D7,"Amount",F1:F2)', 30.0),
    ('DAVERAGE(A1:D7,"Amount",F1:F2)', 15.0),
    ('DCOUNT(A1:D7,"Amount",F1:F2)', 2.0),
    ('DCOUNTA(A1:D7,"Name",F1:F2)', 3.0),
    ('DMAX(A1:D7,"Amount",F1:F2)', 20.0),
    ('DMIN(A1:D7,"Amount",F1:F2)', 10.0),
    ('DPRODUCT(A1:D7,"Amount",F1:F2)', 200.0),
    ('DSUM(A1:D7,3,F1:F2)', 30.0),
    ('DSUM(A1:D7,"Amount",H1:H2)', 20.0),
    ('DSUM(A1:D7,"Amount",J1:J2)', 50.0),
    ('DSUM(A1:D7,"Amount",L1:L2)', 75.0),
    ('DSUM(A1:D7,"Amount",N1:O2)', 20.0),
    ('DSUM(A1:D7,"Amount",S1:T3)', 85.0),
    ('DVAR(A1:D7,"Amount",Q1:Q1)', 62.5),
    ('DVARP(A1:D7,"Amount",Q1:Q1)', 50.0),
    ('DSTDEV(A1:D7,"Amount",Q1:Q1)', 7.905694150420949),
    ('DSTDEVP(A1:D7,"Amount",Q1:Q1)', 7.0710678118654755),
]
COL = 21
NAMED_ROW, NAMED_COL = 0, 4             # E1: the gap between the database
                                         # (cols 0-3) and the first criteria
                                         # block (col 5+), and row 0 is
                                         # always on-screen - unlike COL 21
                                         # above, this one is CLICKED, not
                                         # pre-seeded through SYLK (the CGA
                                         # window shows only 4 rows at once,
                                         # SPEC.md 81.69's own gate found),
                                         # so it has to be somewhere the grid
                                         # actually shows without scrolling
NAMED_EXPR = 'DSUM(Database,"Amount",Criteria)'  # typed live, after Set
NAMED_WANT = 30.0                       # Database/Set Criteria (SPEC.md
                                         # 81.69) == CASES[0]'s own DSUM


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells = dict(DB)
    cells.update(CRIT)
    for i, (expr, _) in enumerate(CASES):
        cells[(i, COL)] = ('formula', expr, WRONG)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk(cells))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def same(want, got):
    if isinstance(got, tuple) and got and got[0] == 'formula':
        got = got[2]
    if isinstance(want, float):
        return (isinstance(got, float)
                and abs(got - want) <= 1e-6 * max(1.0, abs(want)))
    return got == want


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
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

        mo.to(634, 180)
        M.settle(m)
        w, h, rows = m.vram("cga")
        g = glass.grid(w, h, rows)
        if not g:
            check(False, "the grid is on the glass", "no grid")
            done("sheetdb")
            return
        ys, xs = g
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)

        def select(r1, c1, r2, c2):
            mo.click(*at(r1, c1))
            M.settle(m)
            if (r1, c1) != (r2, c2):
                m.key("ShiftLeft", down=True, up=False)
                mo.click(*at(r2, c2))
                m.key("ShiftLeft", down=False, up=True)
                M.settle(m)

        def menu(title, i):
            mo.menu(title[0], title[1], *ITEM(title[0], i))
            M.settle(m)

        select(0, 0, 3, 3)                  # A1:D4 -> DATABASE (81.69). Only
                                             # 4 rows: the CGA window shows 4
                                             # rows at once, and Apple+Banana
                                             # (the two Fruit rows in A1:D4)
                                             # sum to the identical 30.0 the
                                             # full A1:D7 does in CASES[0]
        menu(DATA, SET_DB)
        select(0, 5, 1, 5)                  # F1:F2 -> CRITERIA
        menu(DATA, SET_CRIT)
        select(NAMED_ROW, NAMED_COL, NAMED_ROW, NAMED_COL)
        m.type_text('=%s\n' % NAMED_EXPR)
        M.settle(m)

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
    check(data is not None, "SHEET saved the sheet", "DBASE.SLK never changed")
    got = F.read_sylk(data) if data else {}
    for i, (expr, want) in enumerate(CASES):
        g = got.get((i, COL))
        check(same(want, g), "=%s is %r" % (expr, want), "SHEET holds %r" % (g,))
    named = got.get((NAMED_ROW, NAMED_COL))
    check(same(NAMED_WANT, named),
          "Data > Set Database/Set Criteria: =%s is %r" % (NAMED_EXPR, NAMED_WANT),
          "SHEET holds %r" % (named,))
    done("sheetdb")


if __name__ == "__main__":
    main()
