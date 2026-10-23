#!/usr/bin/env python3
"""ON.KEY (macro plan wave 4, SPEC.md 81.92).

    make && python3 tests/sheetmevt.py

MAIN binds three keys and ends: Ctrl+Q and F7 to macros that count into H1
and H2, and "x" to nothing. The test then PRESSES them, with no run going -
which is the only time a binding is heard - and a second macro unbinds F7,
after which F7 must count nothing. The swallowed "x" is checked the way a
failure would show: typed over J1 and Entered, J1 would hold "x".

ON.TIME is not here, for WAIT's reason (81.86.3): this machine has no BIOS
clock, so NOW() is #N/A and there is no time to arrive at.
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







WORK = "build/sheetmevt"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmevt.img"
NAME = "EVT.SLK"
MACRO = (435, 45)
RUN = (MACRO[0] + 17, 57 + 12 + 2)

MACROS = {
    0: ('MAIN', ['ON.KEY("^q","CTLQ")', 'ON.KEY("{F7}","FSEV")',
                 'ON.KEY("x","")', 'SELECT(J1)', 'RETURN()']),
    2: ('CTLQ', ['SET.VALUE(H1,H1+1)', 'RETURN()']),
    3: ('FSEV', ['SET.VALUE(H2,H2+1)', 'RETURN()']),
    4: ('UNB', ['ON.KEY("{F7}")', 'SELECT(J1)', 'RETURN()']),
}


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells, names = {(0, 7): 0.0, (1, 7): 0.0}, []
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

        def run(name):
            mo.menu(MACRO[0], MACRO[1], *RUN)
            M.settle(m)
            for ch in "\b" * 8 + name:
                m.type_text(ch)
                M.guest_sleep(m, 0.15)
            m.type_text("\n")
            M.settle(m, limit=300)

        def press(key, ctrl=False):
            if ctrl:
                m.key("ControlLeft", down=True, up=False)
            m.key(key)
            if ctrl:
                m.key("ControlLeft", down=False, up=True)
            M.guest_sleep(m, 1.0)
            M.settle(m, limit=120)

        run("MAIN")
        press("KeyQ", ctrl=True)              # H1: 1
        press("KeyQ", ctrl=True)              # H1: 2
        press("F7")                            # H2: 1
        press("KeyX")                          # swallowed...
        press("Enter")                         # ...so J1 gets nothing
        run("UNB")
        press("F7")                            # unbound: H2 stays 1
        _, _, rows = m.vram("cga")
        M.write_png(os.path.join(WORK, "keys.png"), 640, 200, rows)
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
    check(h(1) == 2.0, "ON.KEY(\"^q\",...): Ctrl+Q ran CTLQ, once a press",
          "two presses, two runs", got=h(1), want=2.0)
    check(h(2) == 1.0, "ON.KEY(\"{F7}\",...) ran FSEV, and ON.KEY(\"{F7}\") "
          "put F7 back", "one press bound, one unbound - 1, not 2",
          got=h(2), want=1.0)
    j1 = plain(got.get((0, 9)))
    check(j1 is None, "ON.KEY(\"x\",\"\"): x does nothing",
          "typed over J1 and Entered, an unswallowed x would be J1's", got=j1,
          want=None)
    done("sheetmevt")


if __name__ == "__main__":
    main()
