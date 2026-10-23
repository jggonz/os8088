#!/usr/bin/env python3
"""DIALOG.BOX: a custom dialog described by a range (macro plan wave 4,
SPEC.md 81.96).

    make && python3 tests/sheetmdbox.py

B1:H10 describes one dialog - OK, Cancel, a label, a text box, a check box,
an option group of two, and a number box - and MAIN puts it up four times.
The first is typed at and clicked through, then Enter: every result lands in
the seventh column and the cell answers 1, the default OK's item number. The
second is dismissed with Esc, the third with its Cancel button, and neither
touches a result. The fourth, OK clicked with the mouse, answers 1 again, and the fifth is
dismissed with its CLOSE BOX, which must also answer FALSE and must not cost
a window slot - the kernel only HIDES a package's secondary window, so the
dialog has to refuse that close and destroy itself instead (81.96.2).
Before any of them, three ranges are refused under ERROR(FALSE), each as
FALSE: one with a list box (type 15), one only three columns wide - which
would read its sizes from cells outside itself and write every result over
them - and one of a single row, which would be a dialog with no items and no
button to end it. Everything is read back from SHEET's own save.
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
from os88geom import WIN_SIZE, MAX_WIN, W_FLAGS                 # noqa: E402

WORK = "build/sheetmdbox"               # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmdbox.img"
NAME = "DBOX.SLK"
MACRO = (435, 45)
RUN = (MACRO[0] + 17, 57 + 12 + 2)
DLG_W, DLG_H = 282, 120                 # 280 wide + 2; 150 * 2/3 + 18 + 2
T, FA = ('bool', True), ('bool', False)

# row -> type, x, y, w, h, text, initial (Excel's dialog units)
DEF = [
    (None, None, None, 280, 150, 'Order', None),
    (1, 200, 12, None, None, 'OK', None),
    (2, 200, 36, None, None, 'Cancel', None),
    (5, 8, 14, None, None, 'Name:', None),
    (6, 56, 12, 120, None, None, 'Bob'),
    (13, 8, 40, None, None, 'Rush', True),
    (11, 8, 64, 150, 76, 'Size', 2.0),
    (12, 16, 84, None, None, 'Small', None),
    (12, 16, 108, None, None, 'Large', None),
    (8, 200, 80, 64, None, None, 5.0),
]
BAD = [(None, None, None, 200, 100, 'Bad', None),
       (15, 8, 8, 100, 60, None, None)]

MACROS = {
    10: ('MAIN', ['ERROR(FALSE)',
                  'SET.VALUE(J5,DIALOG.BOX(B12:H13))',
                  'SET.VALUE(J6,DIALOG.BOX(B1:D10))',
                  'SET.VALUE(J7,DIALOG.BOX(B1:H1))',
                  'ERROR(TRUE)',
                  'SET.VALUE(J1,DIALOG.BOX(B1:H10))',
                  'SET.VALUE(J2,DIALOG.BOX(B1:H10))',
                  'SET.VALUE(J3,DIALOG.BOX(B1:H10))',
                  'SET.VALUE(J4,DIALOG.BOX(B1:H10))',
                  'SET.VALUE(J8,DIALOG.BOX(B1:H10))',
                  'RETURN()']),
}


def put(cells, row0, rows):
    for r, vals in enumerate(rows):
        for c, v in enumerate(vals):
            if v is None:
                continue
            if isinstance(v, bool):
                cells[(row0 + r, 1 + c)] = ('bool', v)
            elif isinstance(v, (int, float)):
                cells[(row0 + r, 1 + c)] = float(v)
            else:
                cells[(row0 + r, 1 + c)] = v


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    cells, names = {}, []
    put(cells, 0, DEF)
    put(cells, 11, BAD)
    for col, (name, body) in MACROS.items():
        for r, f in enumerate(body):
            cells[(r, col)] = ('formula', f, 0.0)
        names.append('NN;N%s;ER1C%d' % (name, col + 1))
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
    data, seen = None, []
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

        def dialog():
            """The content origin of DIALOG.BOX's window, or None."""
            for s in dispcp.win_list(m, S, check=False):
                x, y, w, h = dispcp.win_rect(m, S, s)
                if (w, h) == (DLG_W, DLG_H):
                    return x + 1, y + 18
            return None

        def at(o, x, y):                # dialog units to the glass
            return o[0] + x, o[1] + y * 2 // 3

        def rect():
            for sl in dispcp.win_list(m, S, check=False):
                r = dispcp.win_rect(m, S, sl)
                if r[2:] == (DLG_W, DLG_H):
                    return r
            return None

        def used():                     # window records IN USE, hidden or not
            t = m.read(S("wm_wins"), MAX_WIN * WIN_SIZE)
            return sum(1 for i in range(MAX_WIN)
                       if int.from_bytes(t[i * WIN_SIZE + W_FLAGS:
                                           i * WIN_SIZE + W_FLAGS + 2],
                                         "little") & 1)

        def keys(text):
            for ch in text:
                m.type_text(ch)
                M.guest_sleep(m, 0.15)

        mo.menu(MACRO[0], MACRO[1], *RUN)
        M.settle(m)
        keys("\b" * 8 + "MAIN")
        m.type_text("\n")
        M.settle(m, limit=300)
        o = dialog()
        seen.append(o is not None)
        where = rect()
        shot("1-dialog")
        if o:
            keys("\b\b\bAnn")                   # the text box has the keys
            mo.click(*at(o, 12, 44))            # Rush: off
            M.settle(m)
            mo.click(*at(o, 20, 88))            # Small
            M.settle(m)
            keys("\t\b42")                      # Tab: the number box
            shot("2-edited")
            m.type_text("\n")                   # Enter: the default OK
            M.settle(m, limit=300)
        o = dialog()                            # the second: Esc
        seen.append(o is not None)
        shot("3-again")
        if o:
            m.key("Escape")
            M.guest_sleep(m, 0.5)
            M.settle(m, limit=300)
        o = dialog()                            # the third: its Cancel
        seen.append(o is not None)
        if o:
            mo.click(*at(o, 230, 42))
            M.settle(m, limit=300)
        o = dialog()                            # the fourth: its OK
        seen.append(o is not None)
        if o:
            mo.click(*at(o, 230, 18))
            M.settle(m, limit=300)
        slots = used()                          # the fifth: its CLOSE BOX
        r = rect()
        seen.append(r is not None)
        if r:
            mo.click(r[0] + 9, r[1] + 9)
            M.settle(m, limit=300)
        shot("4-closed")
        leaked = used() - (slots - 1)           # one window went, and no
        seen.append(dialog() is None)           # record stayed behind
        shot("5-done")
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
    j = lambda r: g(r - 1, 9)
    check(data is not None, "SHEET saved the sheet", "the rest reads it",
          want=NAME)
    check(seen == [True] * 6,
          "each DIALOG.BOX put its window up, and the last one went",
          "a 282x120 window in the kernel's list while the run waits",
          got=seen, want=[True] * 6)
    # centred, then SNAPPED so the content starts on an 8-pixel column
    # (SPEC.md 11.94), which is why x is 175 and not 179
    mid = ((640 - DLG_W) // 2, (200 - DLG_H) // 2)
    ok = (where is not None and abs(where[0] - mid[0]) <= 8
          and (where[0] + 1) % 8 == 0 and where[1] == mid[1])
    check(ok, "the dialog is CENTRED on the screen",
          "OSAPI_VIDEO answers the size in AX/BX and CLOBBERS CX and DX, so "
          "centring off the registers it was called with put it at x=231, "
          "y=28 - and would have clamped a wide one instead of centring it",
          got=where, want="%r, snapped to an 8-pixel column" % (mid,))
    check(leaked == 0, "the close box costs no window record",
          "the kernel only HIDES a package's secondary window, so its slot "
          "would never come back: MAX_WIN is 12 and SHEET's dialogs would "
          "stop opening", got=leaked, want=0)
    check(j(8) == FA, "...and the close box answers FALSE, like Cancel",
          "the run must carry on, not wait for a window that is gone",
          got=j(8), want=FA)
    check(j(5) == FA, "a list box (type 15) is refused",
          "ERROR(FALSE): the refusal answers FALSE and the run goes on",
          got=j(5), want=FA)
    check(j(6) == FA, "a range narrower than seven columns is refused",
          "B1:D10 would take each item's size from cells outside the range, "
          "and OK would write every result over them", got=j(6), want=FA)
    check(j(7) == FA, "a range of one row is refused",
          "the dialog row alone: no items, so no button either, and only the "
          "close box could end it", got=j(7), want=FA)
    check(j(1) == 1.0, "Enter presses the default OK: the answer is its "
          "item number", "OK is the first row after the dialog's own",
          got=j(1), want=1.0)
    check(g(4, 7) == 'Ann', "the text box's result is what was typed",
          "Bob, three backspaces, Ann", got=g(4, 7), want='Ann')
    check(g(5, 7) == FA, "the check box, clicked, is FALSE",
          "it started TRUE", got=g(5, 7), want=FA)
    check(g(6, 7) == 1.0, "the option group answers the button clicked",
          "Small is its first; it started on 2", got=g(6, 7), want=1.0)
    check(g(9, 7) == 42.0, "the number box, reached by Tab, is a NUMBER",
          "5, a backspace, 42 - stored as 42 and not as the text \"42\"",
          got=g(9, 7), want=42.0)
    check(j(2) == FA and j(3) == FA, "Esc and the Cancel button answer FALSE",
          "and leave the results alone", got=[j(2), j(3)], want=[FA, FA])
    check(j(4) == 1.0, "OK clicked with the mouse answers 1", "",
          got=j(4), want=1.0)
    done("sheetmdbox")


if __name__ == "__main__":
    main()
