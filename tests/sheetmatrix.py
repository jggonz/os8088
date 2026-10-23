#!/usr/bin/env python3
"""The array/matrix functions, evaluated (SPEC.md 81.67).

    make && python3 tests/sheetmatrix.py

TRANSPOSE MMULT MDETERM MINVERSE LINEST LOGEST TREND GROWTH - Sheet has no
array-formula (Ctrl+Shift+Enter) entry, so every one of these publishes only
the TOP-LEFT element of its real result, exactly what Excel itself answers
for a plain (non-CSE) entry. MDETERM/MINVERSE still compute the WHOLE
determinant/inverse either way - there is no way to answer one element of an
inverse without doing the whole elimination - so this is the real gate for
that engine, not a stub of it: a 3x3 case that needs at least one row swap
and one elimination pass below the pivot, a singular 2x2 that must answer
0/#NUM! rather than divide by zero, and a trivial 1x1 case at each end of
the size range. The regression family shares one exact line (y=2x+1) and one
exact curve (y=2^x) across LINEST/TREND and LOGEST/GROWTH, checking the
default known_x's sequence, an explicit one, const=FALSE's through-the-origin
fit, and new_x's given against omitted.

The HOST writes a SYLK file with one formula per case, cached with a wrong
value; SHEET opens it and saves it as SYLK - every formula recomputed first
(sh_dowrite) - and the host reads the values back.
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

WORK = "build/sheetmatrix"              # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmatrix.img"
NAME = "MATRIX.SLK"

DATA = {
    (0, 0): 1.0, (0, 1): 2.0,            # A1:B2 = [[1,2],[3,4]]
    (1, 0): 3.0, (1, 1): 4.0,
    (0, 2): 5.0, (0, 3): 6.0,            # C1:D2 = [[5,6],[7,8]]
    (1, 2): 7.0, (1, 3): 8.0,
    (0, 4): 2.0, (0, 5): 1.0, (0, 6): 1.0,   # E1:G3 = [[2,1,1],[1,3,2],[1,0,0]]
    (1, 4): 1.0, (1, 5): 3.0, (1, 6): 2.0,
    (2, 4): 1.0, (2, 5): 0.0, (2, 6): 0.0,
    (0, 8): 1.0, (0, 9): 2.0,            # I1:J2 = [[1,2],[2,4]], singular
    (1, 8): 2.0, (1, 9): 4.0,
    (0, 7): 7.0,                          # H1, a 1x1
    (0, 10): 'Hello', (1, 10): 42.0,      # K1:K2
    # the regression family's own data (SPEC.md 81.67): a plain y=2x+1 line
    # at A21:B25 (known_x's A, known_y's B - MDETERM/MINVERSE's columns never
    # reach this far down, so there is no collision with the data above)...
    (20, 0): 1.0, (20, 1): 3.0,
    (21, 0): 2.0, (21, 1): 5.0,
    (22, 0): 3.0, (22, 1): 7.0,
    (23, 0): 4.0, (23, 1): 9.0,
    (24, 0): 5.0, (24, 1): 11.0,
    # ...and an exact y=2^x curve at A31:B34, for LOGEST/GROWTH
    (30, 0): 1.0, (30, 1): 2.0,
    (31, 0): 2.0, (31, 1): 4.0,
    (32, 0): 3.0, (32, 1): 8.0,
    (33, 0): 4.0, (33, 1): 16.0,
}
WRONG = -999.0
CASES = [
    ('TRANSPOSE(K1)', 'Hello'),
    ('TRANSPOSE(K2)', 42.0),
    ('MMULT(A1:B2,C1:D2)', 19.0),
    ('MDETERM(A1:B2)', -2.0),
    ('MDETERM(E1:G3)', -1.0),
    ('MINVERSE(A1:B2)', -2.0),
    ('MDETERM(I1:J2)', 0.0),
    ('MINVERSE(I1:J2)', ('err', '#NUM!')),
    ('MDETERM(H1)', 7.0),
    ('MINVERSE(H1)', 1.0 / 7.0),
    # the regression family (81.67): y=2x+1 fits a slope of 2, an intercept
    # of 1 - simple enough that const=TRUE/FALSE and the default known_x's
    # sequence are all distinguishable checks rather than one lucky number
    ('LINEST(B21:B25,A21:A25)', 2.0),
    ('LINEST(B21:B25)', 2.0),               # known_x's omitted -> 1,2,3,4,5,
                                             # identical to A21:A25 here, so
                                             # this exercises the DEFAULT path
    ('LINEST(B21:B25,A21:A25,FALSE)', 2.272727272727273),  # through the
                                             # origin: a different slope
    ('TREND(B21:B25,A21:A25,6)', 13.0),
    ('TREND(B21:B25,A21:A25)', 3.0),        # new_x's omitted -> known_x's[0]
    ('LOGEST(B31:B34,A31:A34)', 2.0),       # y=2^x fits m=2 exactly
    ('GROWTH(B31:B34,A31:A34,5)', 32.0),
    ('GROWTH(B31:B34,A31:A34)', 2.0),       # new_x's omitted -> known_x's[0]=1
]
COL = 12


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells = dict(DATA)
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
    check(data is not None, "SHEET saved the sheet", "MATRIX.SLK never changed")
    got = F.read_sylk(data) if data else {}
    for i, (expr, want) in enumerate(CASES):
        g = got.get((i, COL))
        check(same(want, g), "=%s is %r" % (expr, want), "SHEET holds %r" % (g,))
    done("sheetmatrix")


if __name__ == "__main__":
    main()
