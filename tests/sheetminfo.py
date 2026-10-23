#!/usr/bin/env python3
"""Macro information: GET.CELL, GET.FORMULA, GET.NAME, GET.DEF, GET.NOTE,
NAMES, GET.DOCUMENT, GET.WINDOW, GET.WORKSPACE and DIRECTORY (macro plan
wave 2, SPEC.md 81.87).

    make && python3 tests/sheetminfo.py

One run of MAIN writes each answer into column H, read back from SHEET's
own SYLK save. The fixture fixes what each answer has to be: C3 holds 33
right-aligned, D4 text, E5 the formula =C3*2, DATA names B2:C3, and the
disk has a folder SUB. Every row names the table row it reads, because the
GET.* functions are numbered tables and an off-by-one reads a neighbour
that is also a plausible answer.
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



WORK = "build/sheetminfo"               # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetminfo.img"
NAME = "INFO.SLK"
MACRO = (435, 45)                       # sheetmacro's own, unchanged
RUN = (MACRO[0] + 17, 57 + 12 + 2)

# row (1-based) of H -> (formula, want)
T, FA = ('bool', True), ('bool', False)
ROWS = [
    ('GET.CELL(1,C3)', '$C$3'),
    ('GET.CELL(2,C3)', 3.0),
    ('GET.CELL(3,D4)', 4.0),
    ('GET.CELL(4,D4)', 2.0),
    ('GET.CELL(5,C3)', 33.0),
    ('GET.CELL(6,E5)', '=C3*2'),
    ('GET.CELL(7,C3)', 'General'),
    ('GET.CELL(8,C3)', 4.0),
    ('GET.CELL(14,C3)', T),
    ('GET.CELL(16,C3)', 7.0),
    ('GET.CELL(17,C3)', 12.75),
    ('GET.FORMULA(E5)', '=R[-2]C[-2]*2'),
    ('GET.NAME("DATA")', '=R2C2:R3C3'),
    ('GET.DEF("=R2C2:R3C3")', 'DATA'),
    ('NAMES()', 'DATA'),
    ('GET.DOCUMENT(1)', NAME),
    ('GET.DOCUMENT(9)', 1.0),
    ('GET.DOCUMENT(14)', 1.0),
    ('GET.WINDOW(9)', T),
    ('GET.WORKSPACE(2)', '2.1'),
    ('GET.WORKSPACE(4)', FA),
    ('LEN(GET.NOTE(C3))', 0.0),
    ('DIRECTORY()', 'B:\\'),
    ('DIRECTORY("\\SUB")', 'B:\\SUB'),
    ('DIRECTORY()', 'B:\\SUB'),
    ('ISERROR(DIRECTORY("\\NOPE"))', T),
    ('DIRECTORY()', 'B:\\SUB'),
    ('DIRECTORY("\\")', 'B:\\'),
    ('ISERROR(GET.CELL(99))', T),
    ('GET.DOCUMENT(12)', 8.0),
]
WHY = {
    1: "table row 1 is the reference as text, absolute",
    3: "row 3 is the COLUMN: D is 4",
    4: "row 4 is TYPE(): text is 2",
    6: "row 6 is the formula as text",
    7: "row 7 is the number format's own text",
    8: "row 8 is alignment, 1 General .. 4 Right; C3 is right-aligned",
    9: "row 14 is locked, and a cell with no protection record is locked",
    10: "row 16 is the column width in characters, 7 standard",
    11: "row 17 is the row height in points: 255 twips / 20",
    12: "GET.FORMULA gives R1C1 references: C3 from E5 is R[-2]C[-2]",
    13: "a name's definition, R1C1, absolute",
    14: "the reverse lookup",
    15: "the first element of the names array",
    16: "GET.DOCUMENT(1) is the document's name",
    17: "row 9 is the first used row",
    18: "row 14 is the calculation mode, 1 automatic",
    19: "GET.WINDOW(9) is gridlines",
    20: "GET.WORKSPACE(2) is the version of Excel SHEET is modelled on",
    21: "GET.WORKSPACE(4) is R1C1 mode, off",
    22: "no note is empty text",
    23: "the instance stands at B:'s root",
    24: "DIRECTORY walks into SUB and answers the path",
    25: "...which DIRECTORY() then knows",
    26: "a folder that is not there is refused",
    27: "...and the refusal leaves the instance where it was",
    28: "back to the root, so the save lands where the test reads it",
    29: "a type past the table is #VALUE!",
    30: "row 12 is the last used column - H, where these answers go",
}


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells = {(1, 1): 22.0, (2, 2): 33.0, (3, 3): 'txt',
             (4, 4): ('formula', 'R[-2]C[-2]*2', 66.0)}  # SYLK is R1C1
    for r, (f, _) in enumerate(ROWS):
        cells[(r, 0)] = ('formula', 'SET.VALUE(H%d,%s)' % (r + 1, f), 0.0)
    cells[(len(ROWS), 0)] = ('formula', 'RETURN()', 0.0)
    body = F.write_sylk(cells, align={(2, 2): 'R'}).decode('latin-1')
    body = body.split('\r\n')
    body[1:1] = ['NN;NDATA;ER2C2:R3C3', 'NN;NMAIN;ER1C1']
    src = os.path.join(WORK, NAME)
    open(src, "wb").write('\r\n'.join(body).encode('latin-1'))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "--folder", "SUB", "build/sheet.o88",
                    "build/CHART.OVL", "build/MACRO.OVL", src],
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
    for r, (f, want) in enumerate(ROWS, 1):
        g = plain(got.get((r - 1, 7)))
        if isinstance(want, float) and isinstance(g, float):
            ok = abs(g - want) < 1e-9
        else:
            ok = g == want
        check(ok, "H%d = %s" % (r, f), WHY.get(r, "the table's own row"),
              got=g, want=want)
    done("sheetminfo")


if __name__ == "__main__":
    main()
