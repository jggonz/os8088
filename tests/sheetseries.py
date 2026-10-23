#!/usr/bin/env python3
"""Data > Series fills a range from its first cell (SPEC.md 81.72).

    make && python3 tests/sheetseries.py

Two dialogs in sequence - the TYPE as a radio column, then the STEP as a text
field - and a direction derived from the selection's own shape. What has to be
proved is that all three of those meet: the radio picks the arithmetic, the
typed step reaches it as a REAL number rather than an integer, and the fill
runs the way the selection is shaped.

Five columns, each seeded with its own start value in row 1 and then filled
down through row 4. Every selection here is FOUR ROWS TALL and at most two
columns wide, so every one of them fills down the columns - which is the
whole of the derived direction, and the first version of this file got it
wrong the other way: a selection of all five columns is WIDER than it is
tall, so Series filled along row 1 instead and overwrote every other
column's seed before its own case ran.

    A       B         C       D            E
    1       (label)   1       1990-01-31   1990-01-31

  1. LINEAR, step 2.5      A1..A4 -> 1, 3.5, 6, 8.5 - and 2.5 is the point:
     sh_pnum_at would have refused it, which is why the step is parsed with
     fp_atof instead. B is selected WITH it, and must be left alone: a column
     whose first cell is a LABEL is skipped whole rather than refusing the
     command, the case Excel leaves alone too
  2. GROWTH, step 3        C1..C4 -> 1, 3, 9, 27
  3. DATE: MONTH, step 1   D1..D4 -> 31 Jan, 28 Feb, 31 Mar, 30 Apr as
     serials - the month rolls and the day clamps, which is sh_ymd_to_ser's
     own behaviour and the reason that arm is two instructions
  4. DATE: YEAR, step 1    E1..E4 -> 1990..1993, same day
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

WORK = "build/sheetseries"              # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetseries.img"
NAME = "SER.SLK"
DATA = (311, 45)
ITEM = lambda x, i: (x + 17, 57 + 12 * i + 2)
SERIES = 7                              # 81.72's slot in the Excel-order menu
LINEAR, GROWTH, D_DAY, D_WD, D_MON, D_YR = 0, 1, 2, 3, 4, 5

JAN31_1990 = 32904.0                    # the serial the host and SHEET must
                                         # agree on; checked by case 3 itself
SEED = {
    (0, 0): 1.0,          # A1 - linear
    (0, 1): 'Label',      # B1 - skipped whole, in the SAME selection as A
    (0, 2): 1.0,          # C1 - growth
    (0, 3): JAN31_1990,   # D1 - date by month
    (0, 4): JAN31_1990,   # E1 - date by year
}


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk(SEED))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def serial(y, m, d):
    """the host's own answer, so case 3/4 check SHEET against arithmetic and
    not against a number this file made up. 1900-01-01 is serial 1, and 1900
    is a leap year in this numbering (Excel's own bug, SPEC.md 81.36)"""
    days = 0
    for yy in range(1900, y):
        days += 366 if (yy % 4 == 0 and (yy % 100 or yy % 400 == 0)) else 365
    ml = [31, 29 if (y % 4 == 0 and (y % 100 or y % 400 == 0)) else 28, 31, 30,
          31, 30, 31, 31, 30, 31, 30, 31]
    days += sum(ml[:m - 1]) + d
    return float(days + 1)              # +1: serial 60 is the phantom 29 Feb


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
        w, h, rows = m.vram("cga")
        g = glass.grid(w, h, rows)
        if not g:
            check(False, "the grid is on the glass", "no grid")
            done("sheetseries")
            return
        ys, xs = g
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)

        def series(c1, c2, kind, step):
            """select rows 1..4 of columns c1..c2, then Type then Step"""
            mo.click(*at(0, c1))
            M.settle(m)
            m.key("ShiftLeft", down=True, up=False)
            mo.click(*at(3, c2))
            m.key("ShiftLeft", down=False, up=True)
            M.settle(m)
            mo.menu(DATA[0], DATA[1], *ITEM(DATA[0], SERIES))
            M.settle(m)
            mo.click(*SF.fdlg_xy(m, kind))
            M.settle(m)
            mo.click(*SF.fdlg_okxy(m))
            M.settle(m, limit=120)
            m.type_text("\b" * 8 + step + "\n")
            M.settle(m, limit=180)

        series(0, 1, LINEAR, "2.5")     # A and B together: B must be skipped
        series(2, 2, GROWTH, "3")
        series(3, 3, D_MON, "1")
        series(4, 4, D_YR, "1")

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

    check(data is not None, "SHEET saved the sheet", "SER.SLK never changed")
    got = F.read_sylk(data) if data else {}
    col = lambda c: [got.get((r, c)) for r in range(4)]

    check(col(0) == [1.0, 3.5, 6.0, 8.5],
          "Linear with a step of 2.5 is 1, 3.5, 6, 8.5 - a FRACTIONAL step, "
          "which is why the field is read with fp_atof and not sh_pnum_at",
          "column A holds %r" % (col(0),))
    check(col(2) == [1.0, 3.0, 9.0, 27.0],
          "Growth with a step of 3 multiplies rather than adds",
          "column C holds %r" % (col(2),))
    want_m = [serial(1990, 1, 31), serial(1990, 2, 28),
              serial(1990, 3, 31), serial(1990, 4, 30)]
    check(col(3) == want_m,
          "Date: Month rolls the month and CLAMPS the day - 31 Jan, 28 Feb, "
          "31 Mar, 30 Apr", "column D holds %r, wanted %r" % (col(3), want_m))
    want_y = [serial(y, 1, 31) for y in range(1990, 1994)]
    check(col(4) == want_y,
          "Date: Year keeps the day and month and walks 1990..1993",
          "column E holds %r, wanted %r" % (col(4), want_y))
    check(col(1) == ['Label', None, None, None],
          "a column whose first cell is a LABEL is skipped whole, and the "
          "one beside it in the same selection still filled",
          "column B holds %r" % (col(1),))
    done("sheetseries")


if __name__ == "__main__":
    main()
