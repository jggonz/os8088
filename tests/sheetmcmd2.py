#!/usr/bin/env python3
"""Command equivalents, slice 3b: Formula, Data, RUN, movement and the chart
gallery (macro plan wave 3, SPEC.md 81.89).

    make && python3 tests/sheetmcmd2.py

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




WORK = "build/sheetmcmd2"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmcmd2.img"
NAME = "CMD2.SLK"
MACRO = (435, 45)
RUN = (MACRO[0] + 17, 57 + 12 + 2)

T, FA = ('bool', True), ('bool', False)
# (statement, None) or (None, (H row, formula, want, why))
STEPS = [
    ('FORMULA.GOTO(D5)', None),
    (None, (1, 'GET.CELL(1)', '$D$5', "Goto selects the reference: GET.CELL with no reference reads the active cell")),
    ('FORMULA.FIND("findme")', None),
    (None, (2, 'GET.CELL(1)', '$J$6', "the first cell AFTER the active one (D5) whose displayed text holds it")),
    ('FORMULA.FIND.NEXT()', None),
    (None, (3, 'GET.CELL(1)', '$J$3', "...and the next, wrapping round - a name of 17 characters, which the lexer cut at 16")),
    ('DEFINE.NAME("FOO",B2:C3)', None),
    (None, (4, 'GET.NAME("FOO")', '=R2C2:R3C3', "the name binds refers_to")),
    ('SET.NAME("BAR",D4)', None),
    (None, (5, 'GET.NAME("BAR")', '=R4C4', "SET.NAME's value is a reference here")),
    ('DELETE.NAME("BAR")', None),
    (None, (6, 'ISERROR(GET.NAME("BAR"))', T, "and it is gone")),
    ('NOTE("hello",C3)', None),
    (None, (7, 'GET.NOTE(C3)', 'hello', "the note on C3")),
    ('NOTE("",C3)', None),
    (None, (8, 'LEN(GET.NOTE(C3))', 0.0, "an empty one removes it")),
    ('SELECT(M1:M3)', None),
    ('SORT(1,M1,2)', None),
    (None, (9, 'M1', 3.0, "descending, keyed on M")),
    (None, (10, 'M3', 1.0, "...all the way down")),
    (None, (12, 'RUN(K1)', 5.0, "RUN is a subroutine call: RETURN's value is its answer")),
    (None, (11, '11', 11.0, "placeholder, overwritten by the subroutine below")),
    ('VSCROLL(1,TRUE)', None),
    (None, (13, 'GET.WINDOW(14)', 1.0, "the top row is row 1")),
    ('VLINE(10)', None),
    (None, (14, 'GET.WINDOW(14)', 11.0, "ten rows down")),
    ('HSCROLL(3,TRUE)', None),        # 1 would pass on the lost-value bug
    (None, (15, 'GET.WINDOW(13)', 3.0, "column 3 at the left")),
    ('SELECT(M1)', None),
    ('SELECT.END(4)', None),
    (None, (17, 'GET.CELL(1)', '$M$3', "down the run to its last filled cell")),
    ('SELECT(M1)', None),
    ('SELECT.END(2)', None),
    (None, (18, 'GET.CELL(1)', '$IV$1', "nothing to the right: the sheet's edge")),
    ('SELECT(P5)', None),
    ('CELL.PROTECTION(FALSE)', None),
    ('SELECT(P9)', None),
    ('CELL.PROTECTION(FALSE)', None),
    ('SELECT(P6)', None),
    ('UNLOCKED.NEXT()', None),
    (None, (19, 'GET.CELL(1)', '$P$9', "the next unlocked cell in reading order")),
    ('UNLOCKED.NEXT()', None),
    (None, (20, 'GET.CELL(1)', '$P$5', "...wrapping round")),
    ('UNLOCKED.PREV()', None),
    (None, (21, 'GET.CELL(1)', '$P$9', "...and back, wrapping the other way")),
    ('GALLERY.PIE()', None),
    ('SHOW.ACTIVE.CELL()', None),
    ('SELECT.LAST.CELL()', None),
    (None, (16, 'GET.CELL(1)', 'LAST', "the last used row's and column's cell")),
    ('ERROR(TRUE,K20)', None),
    ('FORMULA.FIND.PREV()', None),
    (None, (23, '23', None, "FIND.PREV is refused, so the run went to K20 and never here")),
]


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells = {(2, 2): 33.0, (1, 3): 5.0, (2, 9): 'xx findme',
             (5, 9): 'findme too', (0, 12): 1.0, (1, 12): 3.0, (2, 12): 2.0,
             (0, 10): ('formula', 'SET.VALUE(H11,11)', 0.0),
             (1, 10): ('formula', 'RETURN(5)', 0.0),
             (19, 10): ('formula', 'SET.VALUE(H22,22)', 0.0),
             (20, 10): ('formula', 'RETURN()', 0.0)}
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
        if want == 'LAST':
            want = '$M$%d' % (len(STEPS) + 1)
        g = plain(got.get((r - 1, 7)))
        if isinstance(want, float) and isinstance(g, float):
            ok = abs(g - want) < 1e-9
        else:
            ok = g == want
        check(ok, "H%d = %s" % (r, f), why, got=g, want=want)
    g = plain(got.get((21, 7)))
    check(g == 22.0, "H22 = 22: the refused FIND.PREV went to ERROR's K20",
          "a refusal is a macro error, and ERROR(TRUE,ref) sends it on", got=g,
          want=22.0)
    done("sheetmcmd2")


if __name__ == "__main__":
    main()
