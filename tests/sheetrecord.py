#!/usr/bin/env python3
"""The macro recorder (SPEC.md 81.74).

    make && python3 tests/sheetrecord.py

81.68 is titled "Macro arguments in R1C1, the way the recorder writes them",
and this is that recorder. What makes it worth gating is that it emits the
SAME language 81.63 runs: a recording is an ordinary macro sheet afterwards.
So this test does not just read the cells it wrote - it RUNS them, on a fresh
part of the sheet, and checks the actions happened again.

  1. Set Recorder on E1, Record, then: select A1, type 11, select A2, type
     22. Stop.
  2. The recorded cells are read out of SHEET's own SYLK save and must be the
     macro text, in ABSOLUTE references - Excel's initial state and this
     recorder's default. Each entry is followed by the step down Enter caused,
     and the redundant click on A2 records nothing at all
  3. ...and running it from Macro > Run puts 11 and 22 back after A1:A2 has
     been cleared, which is the part that proves the recording is a MACRO and
     not a transcript

and Macro > Start Recorder (81.107), Excel's pause-and-continue:

  4. it is GREYED before Set Recorder, and live after the first Stop
  5. choosing it CONTINUES the same macro: typing 33 into A3 lands on the
     row Stop's RETURN() was on - "if the end of the recorder range contains
     a RETURN function, RETURN is overwritten" - and the second Stop writes
     the one RETURN at the new end. The replay then puts 33 back as well
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
from os88geom import WIN_SIZE, W_SEG                            # noqa: E402
from paintmove import pkg_syms                                  # noqa: E402

WORK = "build/sheetrecord"              # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetrecord.img"
NAME = "REC.SLK"
MACRO = (435, 45)
ITEM = lambda x, i: (x + 17, 57 + 12 * i + 2)
RECORD, RUN, START, SETREC = 0, 1, 2, 3  # the Macro menu: Start Recorder
                                         # third since 81.107, as Excel has it
EDIT = (123, 45)
CLEAR = 4                               # Edit > Clear..., sh_i_edit's order
REC_COL = 4                             # E: the recorder range, clear of the
                                         # cells the macro itself touches
# Enter MOVES the selection after it commits, and the recorder records that
# too - so each entry is followed by the step down it caused. That is faithful
# rather than verbose: replaying it lands in the same place. The click on A2
# between them records NOTHING, because Enter had already put the selection
# there and sh_rec_sel does not write a move that did not happen.
WANT = ['SELECT("R1C1")', 'FORMULA("11")', 'SELECT("R2C1")',
        'FORMULA("22")', 'SELECT("R3C1")',
        # 81.107: Start Recorder continued it here, over Stop's RETURN()
        'FORMULA("33")', 'SELECT("R4C1")', 'RETURN()']


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk({(0, 0): 'x'}))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def text_of(v):
    """a recorded cell is a FORMULA whose own source is the macro text"""
    if isinstance(v, tuple) and v and v[0] == 'formula':
        return v[1]
    return v


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
        w, h, rows = m.vram("cga")
        g = glass.grid(w, h, rows)
        if not g:
            check(False, "the grid is on the glass", "no grid")
            done("sheetrecord")
            return
        ys, xs = g
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)
        sym = pkg_syms("apps/sheet/sheet.asm")
        sl = dispcp.win_list(m, S, check=False)[-1]
        sseg = m.read(S("wm_wins") + sl * WIN_SIZE + W_SEG, 2)
        sseg = sseg[0] | (sseg[1] << 8)

        def start_greyed():
            """Start Recorder's item string begins with MENU_DIS (1)"""
            v = m.readseg(sseg, sym["sh_i_macro"] + 2 * START, 2)
            return m.readseg(sseg, v[0] | (v[1] << 8), 1)[0] == 1
        grey = {"fresh": start_greyed()}

        def mac(i):
            mo.menu(MACRO[0], MACRO[1], *ITEM(MACRO[0], i))
            M.settle(m)

        def shot(tag):
            mo.to(634, 190)
            M.settle(m)
            _, _, rw = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rw)

        # --- 1: point the recorder at E1, name the macro, record ------------
        mo.click(*at(0, REC_COL))
        M.settle(m)
        mac(SETREC)
        shot("1-setrec")
        mac(RECORD)
        M.settle(m, limit=120)
        shot("2-namedialog")
        m.type_text("\b" * 12 + "DOIT\n")      # the Name field
        M.settle(m, limit=180)

        mo.click(*at(0, 0))                    # A1
        M.settle(m)
        m.type_text("11\n")
        M.settle(m, limit=120)
        mo.click(*at(1, 0))                    # A2
        M.settle(m)
        m.type_text("22\n")
        M.settle(m, limit=120)
        mac(RECORD)                            # ...which reads Stop Recorder
        M.settle(m, limit=180)
        shot("3-stopped")
        grey["stopped"] = start_greyed()
        # --- 81.107: Start Recorder continues the same macro -------------
        mac(START)
        M.settle(m, limit=120)
        grey["recording"] = start_greyed()
        m.type_text("33\n")                   # A3: Enter left it selected
        M.settle(m, limit=120)
        mac(RECORD)                            # Stop Recorder again
        M.settle(m, limit=180)
        shot("3b-continued")

        # --- 3: clear A1:A2, then run the recording back --------------------
        mo.click(*at(0, 0))
        M.settle(m)
        m.key("ShiftLeft", down=True, up=False)
        mo.click(*at(2, 0))                    # A1:A3
        m.key("ShiftLeft", down=False, up=True)
        M.settle(m)
        mo.menu(EDIT[0], EDIT[1], *ITEM(EDIT[0], CLEAR))
        M.settle(m)
        mo.click(*SF.fdlg_xy(m, 0))            # All
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m, limit=120)
        shot("4-cleared")
        mac(RUN)
        M.settle(m, limit=120)
        m.type_text("\b" * 12 + "DOIT\n")
        M.settle(m, limit=240)
        shot("5-ran")

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

    check(data is not None, "SHEET saved the sheet", "REC.SLK never changed")
    got = F.read_sylk(data) if data else {}
    rec = [text_of(got.get((r, REC_COL))) for r in range(len(WANT))]
    check(rec == WANT,
          "the recording is the macro language itself, in absolute R1C1: %r"
          % (WANT,),
          "column E holds %r" % (rec,))
    a1, a2, a3 = got.get((0, 0)), got.get((1, 0)), got.get((2, 0))
    check(a1 == 11.0 and a2 == 22.0 and a3 == 33.0,
          "...and RUNNING it puts 11, 22 and 33 back after the cells were "
          "cleared, which is what makes it a macro and not a transcript - "
          "the continued part included",
          "A1, A2, A3 hold %r, %r, %r" % (a1, a2, a3))
    check(grey == {"fresh": True, "stopped": False, "recording": True},
          "Start Recorder is greyed with no recorder range and while "
          "recording, and live once a recording has stopped (81.107)",
          "%r" % (grey,))
    done("sheetrecord")


if __name__ == "__main__":
    main()
