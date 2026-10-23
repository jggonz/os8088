#!/usr/bin/env python3
"""Command equivalents, slice 3a: Format, Edit and Options (macro plan wave
3, SPEC.md 81.88).

    make && python3 tests/sheetmcmd.py

Each command's effect is read back through the information functions wave 2
verified (81.87) - GET.CELL, GET.DOCUMENT, GET.WINDOW, GET.NAME,
GET.FORMULA - into column H, and read from SHEET's own SYLK save. So a
command that did nothing reads the default, and a command that did the
wrong thing reads the wrong row of a table that was checked on its own.
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




WORK = "build/sheetmcmd"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmcmd.img"
NAME = "CMD.SLK"
MACRO = (435, 45)
RUN = (MACRO[0] + 17, 57 + 12 + 2)

T, FA = ('bool', True), ('bool', False)
# (statement, None) or (None, (H row, formula, want, why))
STEPS = [
    ('SELECT(C3)', None),
    ('ALIGNMENT(3)', None),
    (None, (1, 'GET.CELL(8,C3)', 3.0, "ALIGNMENT(3) is Center: GET.CELL(8) reads 3")),
    ('FORMAT.FONT(,,TRUE,,TRUE)', None),
    (None, (2, 'GET.CELL(20,C3)', T, "bold, the third argument")),
    (None, (3, 'GET.CELL(22,C3)', T, "underline, the fifth")),
    ('FORMAT.NUMBER("0.00%")', None),
    (None, (4, 'GET.CELL(7,C3)', '0.00%', "one of the 21 built-in formats, by its text")),
    ('BORDER(TRUE,,,,,TRUE)', None),
    (None, (5, 'GET.CELL(9,C3)', T, "outline draws the left edge too")),
    (None, (6, 'GET.CELL(13,C3)', T, "shade, the sixth")),
    ('CELL.PROTECTION(FALSE,TRUE)', None),
    (None, (7, 'GET.CELL(14,C3)', FA, "not locked - stored inverted")),
    (None, (8, 'GET.CELL(15,C3)', T, "hidden")),
    ('COLUMN.WIDTH(12)', None),
    (None, (9, 'GET.CELL(16,C3)', 12.0, "the selection's column, in characters")),
    ('ROW.HEIGHT(15)', None),
    (None, (10, 'GET.CELL(17,C3)', 15.0, "in points, kept in twips")),
    ('CALCULATION(3)', None),
    (None, (11, 'GET.DOCUMENT(14)', 3.0, "3 is Manual")),
    ('CALCULATION(2)', None),
    (None, (21, 'GET.DOCUMENT(14)', 2.0,
            "2 is Automatic except Tables (81.114.8). THIS NUMBER WAS NOT "
            "SAYABLE before: shm_mcalcn folded 2 into Automatic on the "
            "written ground that 'SHEET has no tables, so automatic', and "
            "GET.DOCUMENT answered 1 or 3 with nothing in between. A macro "
            "asking for the one mode that exists to make tables affordable "
            "was given the mode that does not")),
    ('CALCULATION(1)', None),
    ('DISPLAY(,FALSE)', None),
    (None, (12, 'GET.WINDOW(9)', FA, "the second argument is gridlines")),
    ('DISPLAY(,TRUE)', None),
    ('PROTECT.DOCUMENT(TRUE)', None),
    (None, (13, 'GET.DOCUMENT(7)', T, "contents protected")),
    ('PROTECT.DOCUMENT(FALSE)', None),
    ('SELECT(D2:D4)', None),
    ('FILL.DOWN()', None),
    (None, (14, 'D4', 5.0, "Fill Down copied D2 to D4")),
    ('SET.DATABASE()', None),
    (None, (15, 'GET.NAME("Database")', '=R2C4:R4C4', "Set Database names the selection")),
    ('SELECT(K80)', None),
    ('INSERT(3)', None),
    (None, (16, 'K81', 7.0, "a row inserted at row 80 moves K81 to K82 - and THIS formula's own K81 with it, as Insert rewrites every formula on the sheet, the macro's included. Below the macro: a row inserted through the macro's own rows moves its cells, and the engine runs the INSERT again")),
    ('EDIT.DELETE(3)', None),
    (None, (17, 'K81', 7.0, "...and deleting it moves it back")),
    ('SELECT(E2)', None),
    ('COPY()', None),
    ('SELECT(E4)', None),
    ('PASTE.SPECIAL(3)', None),
    (None, (18, 'GET.FORMULA(E4)', '10', "3 is Values: E2's =D2*2 arrives as the number 10")),
    ('CANCEL.COPY()', None),
    ('PRECISION(TRUE)', None),
    ('CALCULATE.DOCUMENT()', None),
    # 81.114.11: TABLE, the macro equivalent of Data > Table and the last of
    # 81.93's "blocked on a SHEET feature" that the feature unblocks. The
    # rectangle is the SELECTION, formulas and input values and all, exactly
    # as the command takes it; the EMPTY first argument is how a one-input
    # table says it has a COLUMN input cell, the same thing the dialog's
    # empty field says.
    ('SELECT(M21:O23)', None),
    ('TABLE(,L25)', None),
    (None, (22, 'N22', 8.0,
            "TABLE(,L25) over M21:O23 - N21 is =L25*2 and M22 is 4, so the "
            "result is 8. The empty first argument is NOT a refusal: it is "
            "how a one-input table says which kind it is")),
    (None, (23, 'O23', 19.0,
            "...and the far corner: O21 is =L25+10 over M23's 9. No two of "
            "the four results agree, so a substitution that went to the "
            "wrong cell cannot land on a right answer")),
    (None, (24, 'L25', 0.0,
            "AND THE INPUT CELL IS BACK. L25 is not in the fixture at all, "
            "so a table that failed to put it back would leave the last "
            "substituted value sitting in a cell the user never filled "
            "(81.114.4) - and SET.VALUE reads it as 0 when it is empty")),
    (None, (19, '1', 1.0, "the run reached the end")),
]


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells = {(2, 2): 33.0, (1, 3): 5.0, (1, 4): ('formula', 'RC[-1]*2', 10.0),
             (80, 10): 7.0,
             # 81.114.11's table: M21:O23 with the formulas across the top
             # row and the values down the left column. Its input cell L25
             # is DELIBERATELY ABSENT, which is 81.114.4's other arm.
             (20, 13): ('formula', 'L25*2', 0.0),     # N21
             (20, 14): ('formula', 'L25+10', 10.0),   # O21
             (21, 12): 4.0,                           # M22
             (22, 12): 9.0}                           # M23
    r = 0
    for stmt, chk in STEPS:
        f = stmt if stmt else 'SET.VALUE(H%d,%s)' % (chk[0], chk[1])
        cells[(r, 0)] = ('formula', f, 0.0)
        r += 1
    cells[(r, 0)] = ('formula', 'RETURN()', 0.0)
    body = F.write_sylk(cells).decode('latin-1').split('\r\n')
    body[1:1] = ['NN;NMAIN;ER1C1']
    src = os.path.join(WORK, NAME)
    open(src, "wb").write('\r\n'.join(body).encode('latin-1'))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def plain(v):
    return v[2] if isinstance(v, tuple) and v and v[0] == 'formula' else v


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
        mo.menu(MACRO[0], MACRO[1], *RUN)
        M.settle(m)
        m.type_text("\b" * 8 + "MAIN\n")
        M.settle(m, limit=300)
        _, _, rows = m.vram("cga")
        M.write_png(os.path.join(WORK, "ran.png"), 640, 200, rows)
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

    if data:
        open(os.path.join(WORK, "saved.slk"), "wb").write(data)
    got = F.read_sylk(data) if data else {}
    check(data is not None, "SHEET saved the sheet after the run",
          "everything below reads that save", want=NAME)
    for stmt, chk in STEPS:
        if not chk:
            continue
        r, f, want, why = chk
        g = plain(got.get((r - 1, 7)))
        if isinstance(want, float) and isinstance(g, float):
            ok = abs(g - want) < 1e-9
        else:
            ok = g == want
        check(ok, "H%d = %s" % (r, f), why, got=g, want=want)
    done("sheetmcmd")


if __name__ == "__main__":
    main()
