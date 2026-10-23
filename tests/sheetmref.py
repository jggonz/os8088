#!/usr/bin/env python3
"""The reference family: OFFSET, ABSREF, RELREF, REFTEXT, TEXTREF, DEREF,
SELECTION, CALLER - and ACTIVE.CELL, which is one too (macro plan wave 1b,
SPEC.md 81.85).

    make && python3 tests/sheetmref.py

SHEET's evaluator has no reference VALUE, so a reference function answers
its top-left cell's value and leaves the reference in a record that a macro
function's reference ARGUMENT reads back - but only when the whole argument
is one call to the function that wrote it. So there are two halves to hold,
and a row for each way to get either wrong:

  H1   SET.VALUE(OFFSET(C3,-2,5),11)    the reference half: the TARGET is the
                                        answer. Negative offsets included
  H2   SET.VALUE(H2,OFFSET(C3,2,3))     the value half: F5's 55
  H3   REFTEXT(OFFSET(C3:E5,-1,0,3,3),TRUE)   a RANGE through both, as text
  H4   RELREF(A1,C3)                    "R[-2]C[-2]" - the manual's own
  H5   SET.VALUE(ABSREF("R[2]C[5]",C3),44)    relative to C3, not the
                                        active cell (A1 when it runs)
  H6   SET.VALUE(TEXTREF("R6C8"),66)    R1C1 when a1 is omitted
  H7   TEXTREF("B7",FALSE)              #REF!: FALSE means R1C1 ONLY - the
                                        manual's own example
  H8   DEREF(B2)                        the value, 22
  H9   REFTEXT(SELECTION())             after SELECT(C3:D4): "R3C3:R4C4"
  H10  SET.VALUE(OFFSET(ACTIVE.CELL(),7,5),10)  ACTIVE.CELL as a reference
  H12  CALLER() in a subroutine         the calling cell, A12, as "R12C1"
  H13  CALLER() at the top level        #REF!: nothing called it
  H14  OFFSET(OFFSET(A1,1,1),12,6)      nesting: the OUTER call's reference
  A15  SET.VALUE(OFFSET(C3,0,5)+0,77)   an EXPRESSION is not a reference: the
                                        run stops there. H3 keeps its text
                                        and A16's H16 is never written, so a
                                        build that took the record for any
                                        argument fails twice
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


WORK = "build/sheetmref"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmref.img"
NAME = "REF.SLK"
MACRO = (435, 45)                       # sheetmacro's own, unchanged
RUN = (MACRO[0] + 17, 57 + 12 + 2)

MACROS = {
    0: ('MAIN', ['SET.VALUE(OFFSET(C3,-2,5),11)',
                 'SET.VALUE(H2,OFFSET(C3,2,3))',
                 'SET.VALUE(H3,REFTEXT(OFFSET(C3:E5,-1,0,3,3),TRUE))',
                 'SET.VALUE(H4,RELREF(A1,C3))',
                 'SET.VALUE(ABSREF("R[2]C[5]",C3),44)',
                 'SET.VALUE(TEXTREF("R6C8"),66)',
                 'SET.VALUE(H7,TEXTREF("B7",FALSE))',
                 'SET.VALUE(H8,DEREF(B2))',
                 'SELECT(C3:D4)',
                 'SET.VALUE(H9,REFTEXT(SELECTION()))',
                 'SET.VALUE(OFFSET(ACTIVE.CELL(),7,5),10)',
                 'WHO()',
                 'SET.VALUE(H13,CALLER())',
                 'SET.VALUE(OFFSET(OFFSET(A1,1,1),12,6),14)',
                 'SET.VALUE(OFFSET(C3,0,5)+0,77)',
                 'SET.VALUE(H16,1)',
                 'RETURN()']),
    9: ('WHO', ['SET.VALUE(H12,REFTEXT(CALLER()))', 'RETURN()']),
}
DATA = {(1, 1): 22.0, (2, 2): 33.0, (4, 5): 55.0}


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells, names = dict(DATA), []
    for col, (name, body) in MACROS.items():
        for r, f in enumerate(body):
            cells[(r, col)] = ('formula', f, 0.0)
        names.append('NN;N%s;ER1C%d' % (name, col + 1))
    body = F.write_sylk(cells).decode('latin-1').split('\r\n')
    body[1:1] = names
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

    got = F.read_sylk(data) if data else {}
    h = lambda r: plain(got.get((r - 1, 7)))
    check(data is not None, "SHEET saved the sheet after the run",
          "everything below reads that save", want=NAME)
    check(h(1) == 11.0, "SET.VALUE(OFFSET(C3,-2,5),11): OFFSET's REFERENCE is "
          "the target", "C3 up two and right five is H1. A build with no "
          "reference record refuses the argument and stops the run here, and "
          "every row below fails with it", got=h(1), want=11.0)
    check(h(2) == 55.0, "SET.VALUE(H2,OFFSET(C3,2,3)): its VALUE is F5's",
          "a reference converts to its top-left cell's value wherever a value "
          "is wanted", got=h(2), want=55.0)
    check(h(3) == '$C$2:$E$4', "REFTEXT(OFFSET(C3:E5,-1,0,3,3),TRUE): a "
          "RANGE, through both functions, as absolute A1 text",
          "the manual's own OFFSET example is C2:E4", got=h(3),
          want='$C$2:$E$4')
    check(h(4) == 'R[-2]C[-2]', "RELREF(A1,C3) is the manual's "
          "\"R[-2]C[-2]\"", "A1 as seen from C3", got=h(4),
          want='R[-2]C[-2]')
    check(h(5) == 44.0, "ABSREF(\"R[2]C[5]\",C3) is relative to C3, not to "
          "the active cell", "C3 down two, right five is H5; from A1, the "
          "active cell when it runs, it would be F3", got=h(5), want=44.0)
    check(h(6) == 66.0, "TEXTREF(\"R6C8\") reads R1C1 when a1 is omitted",
          "R6C8 is H6", got=h(6), want=66.0)
    check(h(7) == ('err', '#REF!'), "TEXTREF(\"B7\",FALSE) is #REF!",
          "the manual's own example: FALSE means R1C1 and nothing else, and "
          "B7 is not R1C1", got=h(7), want=('err', '#REF!'))
    check(h(8) == 22.0, "DEREF(B2) is B2's value", "22", got=h(8), want=22.0)
    check(h(9) == 'R3C3:R4C4', "REFTEXT(SELECTION()) after SELECT(C3:D4)",
          "the whole selection, as R1C1 text since a1 is omitted", got=h(9),
          want='R3C3:R4C4')
    check(h(10) == 10.0, "SET.VALUE(OFFSET(ACTIVE.CELL(),7,5),10): "
          "ACTIVE.CELL is a reference too", "the active cell is C3 after the "
          "SELECT, and C3 down seven, right five is H10. It answered only a "
          "VALUE before this wave, so OFFSET had nothing to offset",
          got=h(10), want=10.0)
    check(h(12) == 'R12C1', "CALLER() inside a subroutine is the calling "
          "cell", "WHO() is called from A12", got=h(12), want='R12C1')
    check(h(13) == ('err', '#REF!'), "CALLER() at the top level is #REF!",
          "the manual: a command macro the user started has no caller",
          got=h(13), want=('err', '#REF!'))
    check(h(14) == 14.0, "OFFSET(OFFSET(A1,1,1),12,6): the OUTER call is "
          "the argument", "B2 down twelve, right six is H14. Both calls stamp "
          "the record and the outer one stamps it last; a build that kept the "
          "inner stamp refuses the argument, since B2's call does not end "
          "where the argument does", got=h(14), want=14.0)
    check(h(3) == '$C$2:$E$4' and h(16) is None,
          "SET.VALUE(OFFSET(C3,0,5)+0,77) is REFUSED and stops the run",
          "an expression's value names no cell, even when a reference "
          "function inside it left the record set. Taken as H3 it would "
          "overwrite H3's text with 77; the run going on would write H16",
          got=[h(3), h(16)], want=['$C$2:$E$4', None])
    done("sheetmref")


if __name__ == "__main__":
    main()
