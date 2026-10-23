#!/usr/bin/env python3
"""Macro text files: FOPEN, FCLOSE, FREAD, FREADLN, FWRITE, FWRITELN, FPOS,
FSIZE (macro plan wave 4, SPEC.md 81.91).

    make && python3 tests/sheetmfile.py

The host writes IN.TXT - three lines, the last with no line end - and the
macro reads it back a line and a piece at a time, is refused a write on a
read-only channel, writes OUT.TXT line by line, reopens it to overwrite one
byte, and is told #N/A for a file that is not there. The answers come back
through SHEET's own SYLK save; OUT.TXT is read off the volume, so what was
WRITTEN is checked as bytes, not as what the macro was told.
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






WORK = "build/sheetmfile"               # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmfile.img"
NAME = "FILE.SLK"
MACRO = (435, 45)
RUN = (MACRO[0] + 17, 57 + 12 + 2)
T, FA = ('bool', True), ('bool', False)
IN = b"alpha\r\nbeta\r\ngamma"

MAIN = [
    'SET.VALUE(H1,FOPEN("IN.TXT",2))',
    'SET.VALUE(H2,FREADLN(H1))',
    'SET.VALUE(H3,FREADLN(H1))',
    'SET.VALUE(H4,FPOS(H1))',
    'SET.VALUE(H5,FREAD(H1,3))',
    'SET.VALUE(H6,FSIZE(H1))',
    'SET.VALUE(H7,FREADLN(H1))',
    'SET.VALUE(H8,ISNA(FREADLN(H1)))',
    'SET.VALUE(H9,ISERROR(FWRITE(H1,"x")))',
    'FCLOSE(H1)',
    'SET.VALUE(H10,FOPEN("OUT.TXT",3))',
    'SET.VALUE(H11,FWRITELN(H10,"one"))',
    'SET.VALUE(H12,FWRITE(H10,"two"))',
    'FCLOSE(H10)',
    'SET.VALUE(H13,FOPEN("OUT.TXT",1))',
    'FPOS(H13,1)',
    'FWRITE(H13,"O")',
    'FCLOSE(H13)',
    'SET.VALUE(H14,ISNA(FOPEN("NOPE.TXT",2)))',
    'SET.VALUE(H15,ISNA(FOPEN("BIG.TXT",2)))',
    'RETURN()',
]
CHECKS = [
    (1, 1.0, "FOPEN answers the channel, the first free"),
    (2, 'alpha', "FREADLN: the line, without its CR LF"),
    (3, 'beta', "...and the next"),
    (4, 14.0, "FPOS is 1-based: 'alpha' CR LF 'beta' CR LF is 13 bytes read"),
    (5, 'gam', "FREAD: exactly that many characters"),
    (6, 18.0, "FSIZE: the file's bytes - 5 + 2 + 4 + 2 + 5"),
    (7, 'ma', "FREADLN to the end of a last line with no line end"),
    (8, T, "past the end, #N/A"),
    (9, T, "a read-only channel refuses a write"),
    (10, 1.0, "the closed channel is free again"),
    (11, 5.0, "FWRITELN counts its CR LF"),
    (12, 3.0, "FWRITE counts the characters written"),
    (14, T, "a file that is not there: #N/A"),
    (15, T, "a file bigger than a channel's share of the module claim's tail: #N/A, not a truncated read"),
]


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells = {}
    for r, f in enumerate(MAIN):
        cells[(r, 0)] = ('formula', f, 0.0)
    body = F.write_sylk(cells).decode('latin-1').split('\r\n')
    body[1:1] = ['NN;NMAIN;ER1C1']
    src = os.path.join(WORK, NAME)
    open(src, "wb").write('\r\n'.join(body).encode('latin-1'))
    inp = os.path.join(WORK, "IN.TXT")
    open(inp, "wb").write(IN)
    big = os.path.join(WORK, "BIG.TXT")
    open(big, "wb").write(b"x" * 8000)   # over a channel's 7,392 (81.94)
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src, inp, big],
                   check=True, stdout=subprocess.DEVNULL)


def plain(v):
    return v[2] if isinstance(v, tuple) and v and v[0] == 'formula' else v


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    data = out = None
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
        for ch in "\b" * 8 + "MAIN":
            m.type_text(ch)
            M.guest_sleep(m, 0.15)
        m.type_text("\n")
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
                if "OUT.TXT" in v.names():
                    out = v.read("OUT.TXT")
                break
            M.settle(m, quiet=2.0, stable=2, limit=60)

    got = F.read_sylk(data) if data else {}
    check(data is not None, "SHEET saved the sheet after the run",
          "the answers are read from that save", want=NAME)
    for r, want, why in CHECKS:
        g = plain(got.get((r - 1, 7)))
        ok = (abs(g - want) < 1e-9) if isinstance(want, float) and \
            isinstance(g, float) else g == want
        check(ok, "H%d" % r, why, got=g, want=want)
    check(out == b"One\r\ntwo", "OUT.TXT holds exactly what was written",
          "FWRITELN's line and its CR LF, FWRITE's text, then FPOS back to 1 "
          "and one byte OVER the first - read off the volume, as bytes",
          got=out, want=b"One\r\ntwo")
    done("sheetmfile")


if __name__ == "__main__":
    main()
