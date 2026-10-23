#!/usr/bin/env python3
"""Every text, information and transcendental function, evaluated (SPEC.md
81.62).

    make && python3 tests/sheetfunc.py [--out FILE]

81.62 moved these three families - 48 functions - out of SHEET's resident
image into CHART.OVL, where the financial family already was. A move is only
checked by a gate that passes BOTH builds, so this is written to: the host
writes a SYLK file with one formula per function, each cached with a WRONG
value, SHEET opens it and saves it as SYLK - every formula recomputed first
(sh_dowrite) - and the host reads the values back. --out keeps SHEET's file,
for the byte comparison between the resident build and the moved one.

The last case is a defined name beginning RC, for the R1C1 scan's word
boundary; SEARCH is its other half. The values are Excel's. Where SHEET
differs from Excel on a build, it is reported here as what it is: a
difference in the function, not in where the function lives.
"""
import math
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

WORK = "build/sheetfunc"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetfunc.img"
NAME = "FUNC.SLK"
WRONG = -999.0
T, FA = ('bool', True), ('bool', False)
# the data the formulas read: A1 text, A2 a number, A3 spaced text; A9 empty
DATA = {(0, 0): 'Hello World', (1, 0): 1234.567, (2, 0): '  a  b  '}
CASES = [
    # --- text (sh_ptext) ---
    ('LEN(A1)', 11.0),
    ('LEFT(A1,4)', 'Hell'),
    ('RIGHT(A1,3)', 'rld'),
    ('MID(A1,7,3)', 'Wor'),
    ('UPPER(A1)', 'HELLO WORLD'),
    ('LOWER(A1)', 'hello world'),
    ('PROPER("hELLO wORLD")', 'Hello World'),
    ('TRIM(A3)', 'a b'),
    ('REPT("ab",3)', 'ababab'),
    ('CHAR(65)', 'A'),
    ('CODE("A")', 65.0),
    ('EXACT("a","A")', FA),
    ('T(A1)', 'Hello World'),
    ('VALUE("12.5")', 12.5),
    ('FIND("o",A1)', 5.0),
    ('SEARCH("w",A1)', 7.0),
    ('SUBSTITUTE(A1,"o","0")', 'Hell0 W0rld'),
    ('REPLACE(A1,1,5,"Howdy")', 'Howdy World'),
    ('TEXT(A2,"0.0")', '1234.6'),
    ('DOLLAR(A2,2)', '$1,234.57'),
    ('FIXED(A2,1)', '1,234.6'),
    ('CLEAN("ab")', 'ab'),
    ('INDIRECT("A2")', 1234.567),
    # --- information (sh_pinfo) ---
    ('ISBLANK(A9)', T),
    ('ISNUMBER(A2)', T),
    ('ISTEXT(A1)', T),
    ('ISLOGICAL(TRUE)', T),
    ('ISERROR(1/0)', T),
    ('ISERR(NA())', FA),
    ('ISNA(NA())', T),
    ('ISREF(A1)', T),
    ('NA()', ('err', '#N/A')),
    ('TYPE(A1)', 2.0),
    ('N(A2)', 1234.567),
    ('ERROR.TYPE(1/0)', 2.0),
    ('ISNONTEXT(A2)', T),
    # --- logarithms and trigonometry (sh_ptrans) ---
    ('LN(10)', math.log(10)),
    ('LOG10(1000)', 3.0),
    ('EXP(1)', math.e),
    ('PI()', math.pi),
    ('LOG(8,2)', 3.0),
    ('SIN(PI()/6)', 0.5),
    ('COS(0)', 1.0),
    ('TAN(PI()/4)', 1.0),
    ('ASIN(1)', math.pi / 2),
    ('ACOS(0)', math.pi / 2),
    ('ATAN(1)', math.pi / 4),
    ('ATAN2(1,1)', math.pi / 4),
    # --- and the SYLK reader's R1C1 scan (81.62): the RC inside SEARCH above
    # was read as "this cell", so SEARCH came in as SEAC16H. A reference must
    # END where a word does - RCOST is a name - and START where one does:
    # BARC ends in RC, and SEARCH, whose RC runs into an H, cannot show that
    ('RCOST*2', 2469.134),
    ('BARC+1', 1235.567),
]
COL = 2


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells = dict(DATA)
    for i, (expr, _) in enumerate(CASES):
        cells[(i, COL)] = ('formula', expr, WRONG)
    src = os.path.join(WORK, NAME)
    body = F.write_sylk(cells).decode('latin-1').split('\r\n')
    body[1:1] = ['NN;NRCOST;ER2C1', 'NN;NBARC;ER2C1']   # both name A2
    open(src, "wb").write('\r\n'.join(body).encode('latin-1'))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def same(want, got):
    if isinstance(got, tuple) and got and got[0] == 'formula':
        got = got[2]
    if isinstance(want, float):
        return (isinstance(got, float)
                and abs(got - want) <= 1e-9 * max(1.0, abs(want)))
    return got == want


def main():
    out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else None
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
    check(data is not None, "SHEET saved the sheet", "FUNC.SLK never changed")
    if out and data is not None:
        open(out, "wb").write(data)
    got = F.read_sylk(data) if data else {}
    for i, (expr, want) in enumerate(CASES):
        g = got.get((i, COL))
        check(same(want, g), "=%s is %r" % (expr, want), "SHEET holds %r" % (g,))
    done("sheetfunc")


if __name__ == "__main__":
    main()
