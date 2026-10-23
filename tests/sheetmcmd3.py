#!/usr/bin/env python3
"""Command equivalents, slice 3c: FORMULA.FILL, DATA.SERIES, FILE.DELETE,
SAVE.AS and OPEN (macro plan wave 3, SPEC.md 81.90).

    make && python3 tests/sheetmcmd3.py

MAIN fills, makes two series, deletes a file (and is refused one that is not
there), then SAVE.AS writes OUT.SLK in SYLK - the file this test reads its
answers from, so the save IS the check that SAVE.AS worked. A second macro,
OPN, OPENs OTHER.SLK: the run ends there, and the test's own save of the
document now in the window must carry OTHER's marker cell.
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





WORK = "build/sheetmcmd3"               # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmcmd3.img"
NAME = "CMD3.SLK"
MACRO = (435, 45)
RUN = (MACRO[0] + 17, 57 + 12 + 2)
T, FA = ('bool', True), ('bool', False)

MAIN = [
    'FORMULA.FILL("=B1*2",C1:D3)',
    'SET.VALUE(H1,C3)',
    'SET.VALUE(H2,D1)',
    'SET.VALUE(H3,GET.FORMULA(D3))',
    'SELECT(E1:E4)',
    'DATA.SERIES(,1,,5)',
    'SET.VALUE(H4,E4)',
    'SELECT(F1:F3)',
    'DATA.SERIES(,2,,3)',
    'SET.VALUE(H5,F3)',
    'ERROR(FALSE)',
    'SET.VALUE(H8,FILE.DELETE("NOPE.TXT"))',
    'SET.VALUE(H9,FILE.DELETE("JUNK.TXT"))',
    'ERROR(TRUE)',
    'SAVE.AS("OUT.SLK",2)',
    'RETURN()',
]
CHECKS = [
    (1, 6.0, "FORMULA.FILL: C3 is =B3*2 - the formula moved down with the fill"),
    (2, 4.0, "...and D1 is =C1*2 - and right"),
    (3, '=RC[-1]*2', "GET.FORMULA of D3: relative, as a fill leaves it"),
    (4, 25.0, "DATA.SERIES linear, step 5 from 10: 10, 15, 20, 25"),
    (5, 18.0, "DATA.SERIES growth, step 3 from 2: 2, 6, 18"),
    (8, FA, "FILE.DELETE of a file that is not there is refused - FALSE under ERROR(FALSE)"),
    (9, T, "...and of one that is, done"),
]


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells = {(0, 1): 1.0, (1, 1): 2.0, (2, 1): 3.0, (0, 4): 10.0, (0, 5): 2.0}
    for r, f in enumerate(MAIN):
        cells[(r, 0)] = ('formula', f, 0.0)
    cells[(0, 12)] = ('formula', 'OPEN("OTHER.SLK")', 0.0)
    cells[(1, 12)] = ('formula', 'SET.VALUE(Z1,1)', 0.0)
    body = F.write_sylk(cells).decode('latin-1').split('\r\n')
    body[1:1] = ['NN;NMAIN;ER1C1', 'NN;NOPN;ER1C13']
    src = os.path.join(WORK, NAME)
    open(src, "wb").write('\r\n'.join(body).encode('latin-1'))
    other = os.path.join(WORK, "OTHER.SLK")
    open(other, "wb").write(F.write_sylk({(8, 25): 'other'}))
    junk = os.path.join(WORK, "JUNK.TXT")
    open(junk, "wb").write(b"junk\r\n")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src, other,
                    junk], check=True, stdout=subprocess.DEVNULL)


def plain(v):
    return v[2] if isinstance(v, tuple) and v and v[0] == 'formula' else v


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    out = other = None
    names = []
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

        run("MAIN")
        for _ in range(60):
            v = os88flush.Flush(marty=m).volume(1)
            if "OUT.SLK" in v.names():
                out = v.read("OUT.SLK")
                names = v.names()
                break
            M.settle(m, quiet=2.0, stable=2, limit=60)
        _, _, rows = m.vram("cga")
        M.write_png(os.path.join(WORK, "main.png"), 640, 200, rows)
        run("OPN")
        _, _, rows = m.vram("cga")
        M.write_png(os.path.join(WORK, "opened.png"), 640, 200, rows)
        before = open(os.path.join(WORK, "OTHER.SLK"), "rb").read()
        mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0], SF.SAVE_AS[1])
        M.settle(m)
        mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['slk']))
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m, limit=120)
        mo.click(*SF.SAVE_BUTTON)
        for _ in range(60):
            v = os88flush.Flush(marty=m).volume(1)
            if "OTHER.SLK" in v.names() and v.read("OTHER.SLK") != before:
                other = v.read("OTHER.SLK")
                break
            M.settle(m, quiet=2.0, stable=2, limit=60)

    check(out is not None, "SAVE.AS(\"OUT.SLK\",2) wrote OUT.SLK",
          "the file every answer below is read from", want="OUT.SLK")
    got = F.read_sylk(out) if out else {}
    for r, want, why in CHECKS:
        g = plain(got.get((r - 1, 7)))
        ok = (abs(g - want) < 1e-9) if isinstance(want, float) and \
            isinstance(g, float) else g == want
        check(ok, "H%d" % r, why, got=g, want=want)
    check(out is not None and "JUNK.TXT" not in names,
          "FILE.DELETE(\"JUNK.TXT\"): the file is gone from the disk",
          "read off the volume itself, not from what the macro answered",
          got=names, want="no JUNK.TXT")
    og = F.read_sylk(other) if other else {}
    check(og.get((8, 25)) == 'other' and og.get((0, 25)) is None,
          "OPEN(\"OTHER.SLK\") loaded it, and the run ended there",
          "the window's document saves as OTHER.SLK with OTHER's marker "
          "cell, and OPN's next row (SET.VALUE(Z1,1)) never ran",
          got=[og.get((8, 25)), og.get((0, 25))], want=['other', None])
    done("sheetmcmd3")


if __name__ == "__main__":
    main()
