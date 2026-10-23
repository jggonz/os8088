#!/usr/bin/env python3
"""Macro control: ERROR, RESTART, ALERT's type 1, STEP, CANCEL.KEY and Esc,
DISABLE.INPUT, WAIT and ECHO (macro plan wave 2, SPEC.md 81.86).

    make && python3 tests/sheetmctl.py

Four runs, each read back from SHEET's own SYLK save:

MAIN, driven by the dialogs it raises:
  H1   ERROR(FALSE) then a failing GOTO: the run goes ON
  H2,H3 ERROR(TRUE,E1) then a failing GOTO: the run goes to E1, whose GOTO
       brings it back
  H4..H6 RESTART(1) two subroutines deep: SUBB's RETURN goes straight back
       to MAIN, so SUBA's own line after the call (H5) never runs
  H7,H8 ALERT("..",1): Enter is OK and TRUE, a click on Cancel is FALSE
  H17  a click on the sheet while ALERT waits: the first only RAISES the
       sheet (the kernel calls no handler for a click on a background
       window), burying the alert; the second reaches the sheet, which
       swallows it and brings the alert back. Neither selects anything - the
       active cell is still the one SELECT made
  H9..H11 STEP(): the dialog comes up BEFORE each cell; Step runs one and
       stops at the next, Continue runs the rest
  H13  whether this machine has a clock at all: a 5150 has no BIOS clock
       service, NOW() is #N/A there (81.42), and WAIT of an error is a macro
       error - so WAIT runs only where there is a clock, and H16 after it
  H12  ERROR(TRUE) - the ordinary stop - so H12 is never written

ESCR: a 400-pass loop and an Esc press in the middle of it. Excel's Esc
  raises the Single Step dialog; Halt ends the run with the counter short

NOESC: CANCEL.KEY(FALSE), the same loop and the same press: it runs to
  the end
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


WORK = "build/sheetmctl"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmctl.img"
NAME = "CTL.SLK"
MACRO = (435, 45)                       # sheetmacro's own, unchanged
RUN = (MACRO[0] + 17, 57 + 12 + 2)
TITLE_H, ABW, ABG, ABTNY, ABH = 18, 72, 12, 46, 13   # os88ui.inc's alert
AW, AH = 288, 92                        # ...and its size, which is how one is
                                        # told from the Run dialog
CELL_A1 = (125, 87)                     # a cell on the glass, left of the alert

MACROS = {
    0: ('MAIN', ['ECHO(FALSE)',
                 'ERROR(FALSE)',
                 'GOTO(99)',
                 'SET.VALUE(H1,1)',
                 'ERROR(TRUE,E1)',
                 'GOTO(99)',
                 'SET.VALUE(H3,3)',
                 'ERROR(TRUE)',                 # ...and E1 is done with
                 'SUBA()',
                 'SET.VALUE(H6,6)',
                 'ECHO(TRUE)',
                 'SET.VALUE(H7,ALERT("Pick OK",1))',
                 'SET.VALUE(H8,ALERT("Pick Cancel",1))',
                 'SELECT(B2)',
                 'DISABLE.INPUT(TRUE)',
                 'ALERT("Locked")',
                 'SET.VALUE(H17,REFTEXT(ACTIVE.CELL(),TRUE))',
                 'DISABLE.INPUT(FALSE)',
                 'STEP()',
                 'SET.VALUE(H9,9)',
                 'SET.VALUE(H10,10)',
                 'SET.VALUE(H11,11)',
                 'SET.VALUE(H13,ISNA(NOW()))',
                 'IF(ISNA(NOW()),0,WAIT(NOW()+0.000023))',
                 'SET.VALUE(H16,16)',
                 'ERROR(TRUE)',
                 'GOTO(99)',
                 'SET.VALUE(H12,12)']),
    4: ('HANDLER', ['SET.VALUE(H2,2)', 'GOTO(A7)']),
    5: ('SUBA', ['SUBB()', 'SET.VALUE(H5,5)', 'RETURN()']),
    6: ('SUBB', ['RESTART(1)', 'SET.VALUE(H4,4)', 'RETURN()']),
    9: ('ESCR', ['FOR(K20,1,400)', 'NEXT()', 'SET.VALUE(H14,14)']),
    11: ('NOESC', ['CANCEL.KEY(FALSE)', 'FOR(L20,1,150)', 'NEXT()',
                   'SET.VALUE(H15,15)']),
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
    seen = {}
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
        mo.to(634, 180)
        M.settle(m)

        def shot(tag):
            _, _, rr = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rr)

        last = {'img': None}

        def crop(r):
            _, _, rr = m.vram("cga")
            x, y, ww, _ = r
            return bytes(rr[y + TITLE_H + 8][x:x + ww]) + bytes(
                rr[y + TITLE_H + 12][x:x + ww]) + bytes(
                rr[y + TITLE_H + 16][x:x + ww])

        def alert(want=True, tries=40):
            """The rect of the NEXT alert - one whose message is not the last
            one's, since two alerts in a row share a rect - else None."""
            for _ in range(tries):
                r = up()
                if r:
                    M.settle(m)                 # a half-drawn alert is not
                    r = up()                    # the next one
                    if not r:
                        continue
                    c = crop(r)
                    if c != last['img']:
                        last['img'] = c
                        return r
                if not want:
                    return None
                M.settle(m, quiet=0.5, stable=2, limit=20)
            return None

        def up():
            """The alert's rect if one is up. By its SIZE: the Run dialog is
            a window too, and counting windows took it for one"""
            for w_ in reversed(dispcp.win_list(m, S)):
                r = dispcp.win_rect(m, S, w_)
                if abs(r[2] - AW) <= 4 and abs(r[3] - AH) <= 4:
                    return r
            print("  no alert among", [dispcp.win_rect(m, S, w_) for w_ in
                                       dispcp.win_list(m, S)], flush=True)
            return None

        npress = [0]

        def press(r, i, n):
            print("  press", i, "of", n, "at", button(r, i, n), "alert", r,
                  flush=True)
            mo.click(*button(r, i, n))
            mo.to(634, 190)                     # off every button, so no
            M.settle(m)                         # release lands on the next
            npress[0] += 1
            shot("p%d" % npress[0])

        def button(r, i, n):
            x, y, ww, _ = r
            row = n * (ABW + ABG) - ABG
            left = x + (ww - row) // 2
            return (left + i * (ABW + ABG) + ABW // 2,
                    y + TITLE_H + ABTNY + ABH // 2)

        def run(name):
            mo.menu(MACRO[0], MACRO[1], *RUN)
            M.settle(m)
            for ch in "\b" * 8 + name:        # one key at a time: type_text
                m.type_text(ch)                 # drops keys to a polling
                M.guest_sleep(m, 0.15)          # field (MartyPC)
            shot("run-" + name)
            m.type_text("\n")

        # --- MAIN ----------------------------------------------------------
        run("MAIN")
        r = alert()
        seen['a1'] = r is not None
        print('  seen %s' % seen, flush=True)
        shot("1-okcancel")
        m.type_text("\n")                       # OK
        M.settle(m)
        r = alert()
        seen['a2'] = r is not None
        print('  seen %s' % seen, flush=True)
        if r:
            press(r, 1, 2)                      # Cancel
        r = alert()                             # "Locked"
        seen['a3'] = r is not None
        print('  seen %s' % seen, flush=True)
        shot("2-locked")
        if r:
            seen['clicked'] = CELL_A1
            mo.click(*CELL_A1)                  # the sheet, clear of the alert:
            mo.to(634, 190)                     # the KERNEL raises it and
            M.settle(m)                         # calls no handler, so this one
            seen['buried'] = up() is not None   # buries the alert...
            mo.click(*CELL_A1)                  # ...and the next reaches the
            mo.to(634, 190)                     # sheet, whose gate swallows it
            M.settle(m)                         # and raises the alert again
            r2 = up()
            if r2:                              # OK by the mouse: after a
                press(r2, 0, 1)                 # click on the
                                                # sheet the alert may not
                                                # have the keys
        r = alert()                             # Single Step, before H9
        seen['s1'] = r is not None
        print('  seen %s' % seen, flush=True)
        shot("3-step")
        m.type_text("\n")                       # Step: H9 runs
        M.settle(m)
        r = alert()                             # ...and stops before H10
        seen['s2'] = r is not None
        print('  seen %s' % seen, flush=True)
        if r:
            press(r, 2, 3)                      # Continue
        M.settle(m, limit=120)
        seen['s3'] = up() is None
        shot("3b-continued")
        print('  seen %s' % seen, flush=True)
        M.guest_sleep(m, 5)                     # WAIT's two seconds

        # --- ESCR: Esc single-steps, Halt ends ------------------------------
        run("ESCR")
        M.guest_sleep(m, 1.5)                   # well into the loop
        m.key("Escape", down=True, up=False)
        M.guest_sleep(m, 0.3)
        m.key("Escape", down=False, up=True)
        r = alert()
        seen['e1'] = r is not None
        print('  seen %s' % seen, flush=True)
        shot("4-esc")
        if r:
            press(r, 1, 3)                      # Halt
        M.settle(m, limit=120)

        # --- NOESC: CANCEL.KEY(FALSE) ---------------------------------------
        run("NOESC")
        M.guest_sleep(m, 1.5)                   # well into the loop
        m.key("Escape", down=True, up=False)
        M.guest_sleep(m, 0.3)
        m.key("Escape", down=False, up=True)
        M.guest_sleep(m, 2)
        seen['n1'] = up() is None
        print('  seen %s' % seen, flush=True)
        M.guest_sleep(m, 40)                    # the 150-pass loop
        M.settle(m, limit=60)
        shot("5-end")

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
    T, FA = ('bool', True), ('bool', False)
    print("  seen:", seen, " H13 (no clock):", h(13))
    check(data is not None, "SHEET saved the sheet after the runs",
          "everything below reads that save", want=NAME)
    check(h(1) == 1.0, "ERROR(FALSE): a failing command is ignored and the "
          "run goes on", "GOTO(99) names no cell", got=h(1), want=1.0)
    check(h(2) == 2.0 and h(3) == 3.0, "ERROR(TRUE,E1): the run goes on "
          "from E1", "E1 writes H2 and GOTOs back to A7, which writes H3",
          got=[h(2), h(3)], want=[2.0, 3.0])
    check(h(4) == 4.0 and h(5) is None and h(6) == 6.0,
          "RESTART(1) two deep: the next RETURN skips SUBA",
          "SUBB's RETURN pops to MAIN, so SUBA's line after the call - H5 - "
          "never runs, and MAIN carries on to H6", got=[h(4), h(5), h(6)],
          want=[4.0, None, 6.0])
    check(seen.get('a1') and h(7) == T, "ALERT(..,1): OK answers TRUE",
          "Enter fires OK, the default", got=h(7), want=T)
    check(seen.get('a2') and h(8) == FA, "ALERT(..,1): Cancel answers FALSE",
          "the second button, clicked", got=h(8), want=FA)
    check(seen.get('clicked') and h(17) == '$B$2',
          "a click on the sheet while ALERT waits is swallowed, and the alert "
          "comes back", "SELECT(B2) made B2 active; the click was on %s, and "
          "the OK after it found the alert in front" % (seen.get('clicked'),),
          got=h(17), want='$B$2')
    check(seen.get('s1') and seen.get('s2') and seen.get('s3'),
          "STEP(): the dialog comes up before a cell, again after Step, and "
          "not after Continue", "a Single Step dialog per cell until "
          "Continue", got=[seen.get('s1'), seen.get('s2'), seen.get('s3')],
          want=[True, True, True])
    check(h(9) == 9.0 and h(10) == 10.0 and h(11) == 11.0,
          "...and every stepped cell ran", "Step runs the cell the dialog "
          "showed", got=[h(9), h(10), h(11)], want=[9.0, 10.0, 11.0])
    if h(13) == T:
        print("  NOTE: this machine has no BIOS clock (NOW() is #N/A, 81.42), "
              "so WAIT's timer path was NOT exercised here")
    check(h(13) in (T, FA) and h(16) == 16.0, "WAIT returns and the run goes "
          "on", "H16 is written after it; H13 says whether there was a clock "
          "to wait on", got=[h(13), h(16)], want=["TRUE or FALSE", 16.0])
    check(h(12) is None, "ERROR(TRUE) is the ordinary stop",
          "the failing GOTO ends the run before H12", got=h(12), want=None)
    k20 = plain(got.get((19, 10)))
    check(seen.get('e1') and h(14) is None and isinstance(k20, float)
          and k20 < 400, "Esc raises the Single Step dialog, and Halt ends "
          "the run", "the loop's counter stops short and H14 is never "
          "written", got=[seen.get('e1'), k20, h(14)],
          want=[True, "< 400", None])
    check(seen.get('n1') and h(15) == 15.0,
          "CANCEL.KEY(FALSE): Esc cannot interrupt", "no dialog, and the "
          "loop runs to H15", got=[seen.get('n1'), h(15)], want=[True, 15.0])
    done("sheetmctl")


if __name__ == "__main__":
    main()
