"""DOS's screenshots, for tools/guide/manuals/DOS_O88.md.

    python3 tools/guide/capture/run.py DOS_O88

DOS.O88 is in A:\\APPS on the system disk. The DOS program it runs is
OS88NET.COM, which rides B:\\SYSTEM\\DOS on the 360KB apps disk.
"""

import os88marty

T = "DOS"


def _menu_shot(ui, shot, title, sid):
    """Drop a menu, take the picture, and let it go over nothing."""
    cells = ui.menus()
    cell = [c for c in cells if c[0].upper() == title.upper()][0]
    _, x0, x1, items = cell
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 1.5)
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6), settle=False)
    ui.mo.to(470, 5, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def _console(ui):
    """Click in the console band, so the keys go to the prompt."""
    w = ui.window(T)
    x, y, ww, hh = ui._rect(w.i)
    ui.mo.click(x + ww // 2, y + hh - 40)
    ui.settle()


def _type(ui, line, wait=2.0):
    ui.m.type_text(line + "\n")
    os88marty.guest_sleep(ui.m, wait)
    ui.settle()


def capture(ui, shot):
    ui.path("A:/APPS/DOS.O88")
    ui.settle()
    _console(ui)

    # a walk to the program and a listing
    _type(ui, "b:")
    _type(ui, "cd system")
    _type(ui, "cd dos")
    _type(ui, "dir", 4.0)
    # a DOS program, with arguments: it prints its help and ends
    _type(ui, "os88net /?", 15.0)
    shot("dos-window", window=T, pad=6)

    _type(ui, "help")
    shot("dos-help", window=T, pad=6)

    _menu_shot(ui, shot, "Program", "dos-menu")

    # a program's own screen comes back into the console when it ends:
    # with no parallel port, OS88NET says so and stops
    _console(ui)
    _type(ui, "os88net", 20.0)
    shot("dos-ended", window=T, pad=6)

    # VER, and a redirection to a file (the prompt refuses it)
    _type(ui, "ver")
    _type(ui, "ver > out.txt")
    shot("dos-ver", window=T, pad=6)

    # Program > Full Screen: the console on the whole text screen
    ui.menu_pick("Program", "Full Screen")
    os88marty.guest_sleep(ui.m, 4.0)
    shot("dos-fullscreen", settle=False)
    ui.m.key("Escape")
    os88marty.guest_sleep(ui.m, 4.0)
    ui.settle()

    # Setup
    ui.menu_pick("Program", "Setup")
    ui.settle()
    shot("dos-setup", window=T, pad=6)
