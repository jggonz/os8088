#!/usr/bin/env python3
"""CELL, Excel's compatibility subset of nine attributes (SPEC.md 81.66).

    make && python3 tests/sheetcell.py

`CELL(type_of_info [, reference])` - "width" "row" "col" "protect" "address"
"contents" "format" "prefix" "type", checked against the real text of
`Microsoft Excel Functions and Macros` (`LIBRARY/documentation/excel_man/`,
p.31-33) rather than recalled. type_of_info is matched case-insensitively.

A1 holds the label "Hello", A2 the number 42, A3 nothing - CELL("type",...)
on each is the whole point of that attribute. Every other attribute is
checked against its DEFAULT: no format, no explicit alignment and no
unlocked cell has been set anywhere on this sheet (SYLK carries none of
those), so "format" must answer "G", "prefix" must answer "" and "protect"
must answer 1 - Excel's own default, and this app's, is an untouched cell
LOCKED (81.46). The last case clicks B3 and asks CELL("address") with NO
reference at all, which is Excel's rule for "the current selection".
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

WORK = "build/sheetcell"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetcell.img"
NAME = "CELLT.SLK"

DATA = {(0, 0): 'Hello', (1, 0): 42.0}          # A1, A2 - A3 stays blank
WRONG = -999.0
CASES = [
    ('CELL("type",A1)', 'l'),
    ('CELL("type",A2)', 'v'),
    ('CELL("type",A3)', 'b'),
    ('CELL("ROW",A1)', 1.0),
    ('CELL("row",B5)', 5.0),
    ('CELL("col",A1)', 1.0),
    ('CELL("Col",C1)', 3.0),
    ('CELL("address",B5)', '$B$5'),
    ('CELL("contents",A1)', 'Hello'),
    ('CELL("contents",A2)', 42.0),
    ('CELL("width",A1)', 7.0),
    ('CELL("protect",A1)', 1.0),
    ('CELL("format",A2)', 'G'),
    ('CELL("prefix",A1)', ''),
]
COL = 5
LAST_ROW = len(CASES)                   # CELL("address") with no reference


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells = dict(DATA)
    for i, (expr, _) in enumerate(CASES):
        cells[(i, COL)] = ('formula', expr, WRONG)
    cells[(LAST_ROW, COL)] = ('formula', 'CELL("address")', WRONG)
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
        if g:
            ys, xs = g
            bx = (xs[1] + xs[2]) // 2       # B3: column B (index 1), row 3
            by = (ys[2] + ys[3]) // 2       # (0-based glass rows: row 3 is 2)
            mo.click(bx, by)
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
    check(data is not None, "SHEET saved the sheet", "CELLT.SLK never changed")
    got = F.read_sylk(data) if data else {}
    for i, (expr, want) in enumerate(CASES):
        g = got.get((i, COL))
        check(same(want, g), "=%s is %r" % (expr, want), "SHEET holds %r" % (g,))
    g = got.get((LAST_ROW, COL))
    check(same('$B$3', g),
          'CELL("address") with no reference is the CURRENT SELECTION - B3, '
          'clicked before the save',
          "SHEET holds %r" % (g,))
    done("sheetcell")


if __name__ == "__main__":
    main()
