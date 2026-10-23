#!/usr/bin/env python3
"""Custom menus: ADD.MENU, ADD.COMMAND, CHECK/ENABLE/RENAME.COMMAND,
DELETE.COMMAND, ADD.BAR/SHOW.BAR/DELETE.BAR (macro plan wave 4, SPEC.md
81.95).

    make && python3 tests/sheetmmenu.py

MAIN builds a "Tools" menu after Help from a description range - title, then
command name and macro per row - adds a third command, ticks the second,
greys the third and renames the first. The test then USES the menu with the
mouse, with no run going: the renamed command runs its macro, the greyed one
runs nothing. BAR makes the custom bar, gives it the same menu, shows it -
and a click on its first title, which on the custom bar is Tools and not
File, runs the second command. The effects are read from SHEET's own save.
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








WORK = "build/sheetmmenu"               # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmmenu.img"
NAME = "MENU.SLK"
MACRO = (435, 45)
RUN = (MACRO[0] + 17, 57 + 12 + 2)
TOOLS = (592, 45)                       # "Go", after Help: 576..607
TOOLS7 = (72, 45)                       # alone on the custom bar: 56..87
ITEM = lambda x, i: (x - 8, 57 + 12 * i + 2)
T = ('bool', True)

MACROS = {
    0: ('MAIN', ['SET.VALUE(H1,ADD.MENU(1,B1:C3))',
                 'ADD.COMMAND(1,"Go",B5:C5)',
                 'ERROR(FALSE)',
                 'SET.VALUE(H9,ADD.MENU(1,B7:C7))',  # "Toolbox": no room
                 'ERROR(TRUE)',
                 'CHECK.COMMAND(1,10,2,TRUE)',
                 'ENABLE.COMMAND(1,10,3,FALSE)',
                 'RENAME.COMMAND(1,10,1,"Hi")',
                 'ERROR(FALSE)',
                 'SET.VALUE(H10,RENAME.COMMAND(1,10,0,"Toolbox"))',
                 'ERROR(TRUE)',
                 'RETURN()']),
    4: ('HEL', ['SET.VALUE(H2,H2+1)', 'RETURN()']),
    5: ('BYE', ['SET.VALUE(H3,H3+1)', 'RETURN()']),
    6: ('THR', ['SET.VALUE(H4,4)', 'RETURN()']),
    8: ('BAR', ['ON.KEY("{F9}","BACK")',  # the custom bar has no Macro menu
                'SET.VALUE(H5,ADD.BAR())',
                'SET.VALUE(H6,ADD.MENU(7,B1:C3))',
                'SHOW.BAR(7)', 'RETURN()']),
    9: ('BACK', ['SHOW.BAR()', 'SET.VALUE(H7,DELETE.BAR(7))',
                 'SET.VALUE(H8,DELETE.MENU(1,"Go"))', 'RETURN()']),
}
DESC = {(0, 1): 'Go', (1, 1): 'Hello', (1, 2): 'HEL',
        (2, 1): 'Bye', (2, 2): 'BYE', (4, 1): 'Third', (4, 2): 'THR',
        (6, 1): 'Toolbox'}


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells, names = dict(DESC), []
    for col, (name, body) in MACROS.items():
        for r, f in enumerate(body):
            cells[(r + 10, col)] = ('formula', f, 0.0)
        names.append('NN;N%s;ER11C%d' % (name, col + 1))
    cells[(1, 7)] = 0.0
    cells[(2, 7)] = 0.0
    body = F.write_sylk(cells).decode('latin-1').split('\r\n')
    body[1:1] = names
    src = os.path.join(WORK, NAME)
    open(src, "wb").write('\r\n'.join(body).encode('latin-1'))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL",
                    "build/MACRO.OVL", src],
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

        def shot(tag):
            _, _, rr = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rr)

        def run(name):
            mo.menu(MACRO[0], MACRO[1], *RUN)
            M.settle(m)
            for ch in "\b" * 8 + name:
                m.type_text(ch)
                M.guest_sleep(m, 0.15)
            m.type_text("\n")
            M.settle(m, limit=300)

        def pick(title, i):
            mo.menu(title[0], title[1], *ITEM(title[0], i))
            M.settle(m, limit=300)

        run("MAIN")
        shot("1-bar")
        mo.to(*TOOLS)                           # the dropdown, held open
        mo._edge(True)
        M.settle(m)
        shot("2-tools")
        mo._edge(False)
        M.settle(m)
        pick(TOOLS, 0)                          # "Hi": H2 = 1
        pick(TOOLS, 2)                          # "Third", greyed: nothing
        run("BAR")
        shot("3-custombar")
        pick(TOOLS7, 1)                         # Tools' "Bye" on bar 7: H3 = 1
        mo.to(634, 190)
        m.key("F9")                             # BACK, by ON.KEY: no Macro
        M.guest_sleep(m, 1.0)                   # menu on the custom bar
        M.settle(m, limit=300)
        shot("4-back")
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
    check(data is not None, "SHEET saved the sheet", "the rest reads it",
          want=NAME)
    check(h(1) == 10.0, "ADD.MENU(1, ...) puts Go tenth, after Help",
          "its position on bar 1, counting the nine built-in menus",
          got=h(1), want=10.0)
    check(h(2) == 1.0, "the renamed first command, picked from the bar, "
          "runs its macro", "RENAME.COMMAND kept its macro; a click on it "
          "ran HEL once", got=h(2), want=1.0)
    check(h(4) is None, "the greyed third command runs nothing",
          "ENABLE.COMMAND(...,FALSE): a pick on it is refused", got=h(4),
          want=None)
    check(h(10) == ('bool', False), "RENAME.COMMAND of a TITLE is measured "
          "too", "the same refusal ADD.MENU got: \"Toolbox\" would be drawn "
          "past the window's edge and left there. The menu below still works, "
          "so the old title was put back", got=h(10), want=('bool', False))
    check(h(9) == ('bool', False), "a menu too wide for the bar is refused",
          "Help ends 575 and the window 617: \"Toolbox\" would be drawn past "
          "the window's edge, and was, and was never erased", got=h(9),
          want=('bool', False))
    check(h(5) == 7.0 and h(6) == 1.0, "ADD.BAR() is bar 7, and ADD.MENU "
          "puts Tools first on it", "a custom bar holds its own menus only",
          got=[h(5), h(6)], want=[7.0, 1.0])
    check(h(3) == 1.0, "SHOW.BAR(7): the bar's first title is Tools, and its "
          "second command runs BYE", "on the custom bar there is no File",
          got=h(3), want=1.0)
    check(h(7) == T and h(8) == T, "SHOW.BAR() goes back, and then "
          "DELETE.BAR(7) and DELETE.MENU(1,\"Tools\") are allowed",
          "a bar that is not showing can be deleted", got=[h(7), h(8)],
          want=[T, T])
    done("sheetmmenu")


if __name__ == "__main__":
    main()
