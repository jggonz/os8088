#!/usr/bin/env python3
"""Subroutines: `ref(arg1, ...)`, ARGUMENT and RETURN(value) (macro plan wave 1).

    make && python3 tests/sheetmsub.py

Excel's model (Functions and Macros, "Subroutines:"): a defined name called
like a function branches to that cell, ARGUMENT names the arguments in the
order it executes, and RETURN's value is the call's answer. SHEET brings the
answer back by evaluating the CALLING cell a second time - INPUT's mechanism
(SPEC.md 81.63) - because a subroutine may pause for ALERT or INPUT halfway
through the caller's formula and an 8086 call stack cannot be suspended there.

Each row of MAIN is one shape, and each is a different way to get it wrong:

  H1  SET.VALUE(H1, SQ(7))        a call INSIDE an expression: the answer has
                                  to survive the frame, the pop, and the second
                                  pass. A build that answered the first pass's
                                  FALSE would write FALSE here
  H2  SET.VALUE(H2, ADD2(3,4))    two ARGUMENTs, bound IN THE ORDER THEY RUN.
                                  3+4 = 7 either way, so the fixture uses 30-4
                                  instead: a build that bound backwards gives
                                  -26
  H3  SETX()                      a call as a whole statement, whose subroutine
                                  does the work - the common form
  H4  SET.VALUE(H4, TXT())        a TEXT answer, which is banked in the module
                                  and must come back as text, not as the zero
                                  underneath it
  H5  SET.VALUE(H5, OUTER(5))     a call INSIDE ANOTHER SUBROUTINE'S RETURN:
                                  RETURN(SQ(v)+1). The design review found this
                                  one - SQ's call and RETURN's own control are
                                  set in the same step, and the call was lost
                                  until a pending call outranked RETURN. 26
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

WORK = "build/sheetmsub"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmsub.img"
NAME = "SUB.SLK"
MACRO = (435, 45)                       # sheetmacro's own, unchanged
RUN = (MACRO[0] + 17, 57 + 12 + 2)

# column -> (the name it is run by, its cells)
MACROS = {
    0: ('MAIN', ['SET.VALUE(H1,SQ(7))',
                 'SET.VALUE(H2,ADD2(30,4))',
                 'SETX()',
                 'SET.VALUE(H4,TXT())',
                 'SET.VALUE(H5,OUTER(5))',
                 'RETURN()']),
    2: ('SQ', ['ARGUMENT("num")', 'RETURN(num*num)']),
    3: ('ADD2', ['ARGUMENT("lhs")', 'ARGUMENT("rhs")', 'RETURN(lhs-rhs)']),
    4: ('SETX', ['SET.VALUE(H3,99)', 'RETURN()']),
    5: ('TXT', ['RETURN("hello")']),
    6: ('OUTER', ['ARGUMENT("val")', 'RETURN(SQ(val)+1)']),
}


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells, names = {}, []
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
    h = lambda r: plain(got.get((r, 7)))
    check(data is not None, "SHEET saved the sheet after the run",
          "everything below reads that save", want=NAME)
    check(h(0) == 49.0, "SET.VALUE(H1, SQ(7)): a call inside an expression "
          "answers RETURN's value",
          "the call site answers FALSE on the first pass and RETURN's value on "
          "the second, after the frame is popped. A build that kept the first "
          "pass's answer writes FALSE here", got=h(0), want=49.0)
    check(h(1) == 26.0, "ADD2(30,4): ARGUMENTs bind in the order they RUN",
          "lhs-rhs, so the order is visible - 26 bound forwards, -26 "
          "backwards", got=h(1), want=26.0)
    check(h(2) == 99.0, "SETX(): a call as a whole statement runs its "
          "subroutine", "its SET.VALUE writes H3", got=h(2), want=99.0)
    check(h(3) == 'hello', "TXT(): a TEXT answer comes back as text",
          "banked in the module's pool and restored into sh_sacc - not the "
          "zero a text value's number is", got=h(3), want='hello')
    check(h(4) == 26.0, "OUTER(5): RETURN(SQ(val)+1) - a call inside another "
          "subroutine's RETURN",
          "SQ's call and RETURN's control land in the same step. Until a "
          "pending call outranked every other control the call was lost and "
          "RETURN handed back FALSE+1", got=h(4), want=26.0)
    done("sheetmsub")


if __name__ == "__main__":
    main()
