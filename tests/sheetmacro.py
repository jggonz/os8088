#!/usr/bin/env python3
"""The macro language (SPEC.md 81.63).

    make && python3 tests/sheetmacro.py

Five keywords, each only as the WHOLE of a cell, was all a macro had: nothing
could decide, loop or ask. The HOST writes a SYLK file holding two macros and
a name for the first; everything is driven through the real menus and read
back from SHEET's own SYLK save.

COUNT (column A, run BY NAME from Macro > Run...):
    FOR/NEXT summing 1..5 into H2; SELECT(C1) then WHILE(ACTIVE.CELL()<>0)
    walking down with SELECT("R[1]C") and totalling into H3; FORMULA("=H2*2",
    H4) entering a formula; IF(H2>10,GOTO(A12)) jumping over a SET.VALUE
    that must not run; FORMULA("done") into the active cell; a FOR broken out
    of at 3 by IF(...,BREAK()); a FOR stepping -3 from 10 inside another
    FOR, 3 x 4 turns; COPY C1 and PASTE it at E1; CLEAR() on C2 - its
    contents, Excel's default; MESSAGE(TRUE,"Hello") on the status bar;
    SET.VALUE(TOTAL,99) through a defined name; a FOR from 5 to 1, which runs
    no turn at all and must be skipped past its OWN NEXT, not the inner
    loop's; RETURN, with a SET.VALUE after it that must not run.
ASK (column B, run by REFERENCE): ALERT("Hi") - dismissed with Enter - then
    SET.VALUE(H10,INPUT("Number?")), answered 42.
And D1, an ordinary worksheet formula =GOTO(A12): outside a run a macro
    command does nothing and answers FALSE. Then SET.VALUE(H20,5) and
    FORMULA("=R[-1]C*2",H21) - R1C1, the way Excel's macro recorder writes a
    formula argument - and SELECT("R23C8:R24C9"), an R1C1 RANGE as text,
    followed by FORMULA("top") with no ref argument at all, landing on the
    selection's own top-left cell (SPEC.md 81.68).

Writing a macro sheet's own formulas into a real Excel BIFF file - Ftab and
Cetab function numbers, the BOF flag that says MACRO SHEET - is its own,
separate, still-open piece (81.68's own header says why) and is not this
gate's job.
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
import sheetdec as SD                                        # noqa: E402

WORK = "build/sheetmacro"               # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmacro.img"
NAME = "MAC.SLK"
MACRO = (435, 45)
RUN = (MACRO[0] + 17, 57 + 12 + 2)      # Macro's SECOND item: 81.74's
                                         # recorder put Record... above it
COUNT = [
    'FOR(H1,1,5)',
    'SET.VALUE(H2,H2+H1)',
    'NEXT()',
    'SELECT(C1)',
    'WHILE(ACTIVE.CELL()<>0)',
    'SET.VALUE(H3,H3+ACTIVE.CELL())',
    'SELECT("R[1]C")',
    'NEXT()',
    'FORMULA("=H2*2",H4)',
    'IF(H2>10,GOTO(A12))',
    'SET.VALUE(H5,"skipped")',
    'FORMULA("done")',
    'FOR(H7,1,10)',
    'IF(H7=3,BREAK())',
    'NEXT()',
    'SET.VALUE(H8,H7)',
    'FOR(H13,1,3)',
    'FOR(H11,10,1,-3)',
    'SET.VALUE(H12,H12+1)',
    'NEXT()',
    'NEXT()',
    'SELECT(C1)',
    'COPY()',
    'SELECT(E1)',
    'PASTE()',
    'SELECT(C2)',
    'CLEAR()',
    'MESSAGE(TRUE,"Hello")',
    'SET.VALUE(TOTAL,99)',
    'FOR(H15,5,1)',
    'FOR(H16,1,2)',
    'NEXT()',
    'SET.VALUE(H17,1)',                 # between the two NEXTs: runs only if
    'NEXT()',                           # the skip stops at the inner one
    'SET.VALUE(H20,5)',
    'FORMULA("=R[-1]C*2",H21)',         # R1C1, as Excel's recorder writes it
    'SELECT("R23C8:R24C9")',            # a range, as text
    'FORMULA("top")',                   # ...into its top-left, the active cell
    'RETURN()',
    'SET.VALUE(H9,1)',
]
ASK = ['ALERT("Hi")', 'SET.VALUE(H10,INPUT("Number?"))', 'HALT()']
T, FA = ('bool', True), ('bool', False)


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells = {(r, 0): ('formula', f, 0.0) for r, f in enumerate(COUNT)}
    cells.update({(r, 1): ('formula', f, 0.0) for r, f in enumerate(ASK)})
    cells.update({(0, 2): 10.0, (1, 2): 20.0, (2, 2): 30.0})     # C1:C3
    cells[(0, 3)] = ('formula', 'GOTO(A12)', 0.0)                # D1
    body = F.write_sylk(cells).decode('latin-1').split('\r\n')
    body[1:1] = ['NN;NCOUNT;ER1C1', 'NN;NTOTAL;ER14C8']   # A1, and H14
    src = os.path.join(WORK, NAME)
    open(src, "wb").write('\r\n'.join(body).encode('latin-1'))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def a1(t, r, c):
    """SHEET's SYLK formula in A1 - outside the string constants, whose R[1]C
    SHEET leaves alone and sheetdec's converter would not"""
    parts = t.split('"')
    return '"'.join(SD.r1c1_to_a1(p, r, c) if k % 2 == 0 else p
                    for k, p in enumerate(parts))


def plain(v):
    return v[2] if isinstance(v, tuple) and v and v[0] == 'formula' else v


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    data, status = None, None
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

        def shot(tag):
            _, _, rows = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rows)

        def run(start):
            mo.menu(MACRO[0], MACRO[1], *RUN)
            M.settle(m)
            shot("run-" + start)
            m.type_text("\b" * 8 + start + "\n")
            M.settle(m, limit=240)

        table = glass.glyphs(m)
        run("COUNT")
        shot("1-count")
        mo.to(634, 180)
        M.settle(m)
        _, _, rows = m.vram("cga")
        status = glass.cell_text(rows, (58, 158, 300, 172), table)
        run("B1")
        shot("2-alert")
        m.type_text("\n")                   # the alert's OK
        M.settle(m, limit=120)
        shot("3-input")
        m.type_text("42")
        M.settle(m)
        shot("3-typed")
        m.type_text("\n")
        M.settle(m, limit=240)
        shot("4-done")
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

    got = F.read_sylk(data) if data else {}
    g = lambda r, c: plain(got.get((r, c)))
    check(data is not None, "SHEET saved the sheet", "MAC.SLK never changed")
    check(g(1, 7) == 15.0, "FOR/NEXT, run by NAME: H2 = 1+2+3+4+5",
          "H2 holds %r, the counter H1 %r" % (g(1, 7), g(0, 7)))
    check(g(2, 7) == 60.0, "WHILE(ACTIVE.CELL()<>0) walking C1:C3 with "
          "SELECT(\"R[1]C\"): H3 = 60", "H3 holds %r" % (g(2, 7),))
    h4 = got.get((3, 7))
    check(isinstance(h4, tuple) and h4[0] == 'formula' and h4[2] == 30.0,
          "FORMULA(\"=H2*2\",H4) enters a formula", "H4 holds %r" % (h4,))
    check((4, 7) not in got, "IF(H2>10,GOTO(A12)) jumps over a SET.VALUE",
          "H5 holds %r" % (got.get((4, 7)),))
    check(g(3, 2) == 'done', "FORMULA(\"done\") lands in the active cell, C4, "
          "where the WHILE left the selection", "C4 holds %r" % (g(3, 2),))
    check(g(7, 7) == 3.0, "IF(H7=3,BREAK()) leaves the FOR at 3",
          "H8 holds %r, H7 %r" % (g(7, 7), g(6, 7)))
    check((8, 7) not in got, "RETURN() ends the macro",
          "H9 holds %r" % (got.get((8, 7)),))
    check(g(9, 7) == 42.0, "ALERT pauses, then INPUT asks and answers 42",
          "H10 holds %r" % (g(9, 7),))
    check(g(0, 3) == FA, "a macro command in a worksheet formula does nothing "
          "and answers FALSE", "D1 holds %r" % (g(0, 3),))
    check(g(11, 7) == 12.0, "a FOR stepping -3 inside another FOR: 3 x 4 turns",
          "H12 holds %r" % (g(11, 7),))
    check(g(0, 4) == 10.0, "COPY() C1, PASTE() at E1", "E1 holds %r" % (g(0, 4),))
    check((1, 2) not in got, "CLEAR() empties C2", "C2 holds %r" % (g(1, 2),))
    check(g(13, 7) == 99.0, "SET.VALUE(TOTAL,99) through a defined name",
          "H14 holds %r" % (g(13, 7),))
    check((16, 7) not in got and (15, 7) not in got,
          "a FOR that runs no turn is skipped past its own NEXT, over the "
          "loop inside it", "H16 holds %r, H17 %r" % (g(15, 7), g(16, 7)))
    h21 = got.get((20, 7))
    check(isinstance(h21, tuple) and h21[0] == 'formula' and h21[2] == 10.0
          and a1(h21[1], 20, 7) == 'H20*2',
          "FORMULA(\"=R[-1]C*2\",H21) takes R1C1, the recorder's form, as =H20*2",
          "H21 holds %r" % (h21,))
    check(g(22, 7) == 'top', "SELECT(\"R23C8:R24C9\") selects a range, and "
          "FORMULA goes to its top-left", "H23 holds %r" % (g(22, 7),))
    check(status == 'Hello', "MESSAGE(TRUE,\"Hello\") is on the status bar when "
          "the macro RETURNs", "the status bar reads %r" % (status,))
    done("sheetmacro")


if __name__ == "__main__":
    main()
