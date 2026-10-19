"""Task Manager's screenshots, for tools/guide/manuals/TASKMGR_O88.md.

Two programs are opened first so the list has rows of its own, and the
Calculator is given a little work so the CPU column moves.
"""
import os88marty

TITLE = "Task Manager"


def _menu_shot(ui, shot, title, sid):
    cells = ui.menus()
    cell = [c for c in cells if c[0].upper() == title.upper()][0]
    _, x0, x1, items = cell
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 2.0)
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6), settle=False)
    ui.mo.to(600, 460, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def _content_click(ui):
    w = ui.window(TITLE)
    x, y, ww, hh = ui._rect(w.i)
    ui.mo.click(x + ww // 2, y + hh - 30)
    os88marty.guest_sleep(ui.m, 3.0)


def capture(ui, shot):
    m = ui.m
    ui.path("B:/APPS/NOTEPAD.O88")
    m.type_text("Shopping list")
    ui.path("B:/APPS/CALC.O88")
    m.type_text("12*3=")
    for o in list(ui.windows()):
        if o.visible and o.title not in ("Note Pad", "Calculator"):
            ui.close(o)

    # The System menu (the chip at the left of the bar) carries the item.
    _menu_shot(ui, shot, "Apple", "taskmgr-sysmenu")
    ui.menu_pick("Apple", "Task Manager")
    ui.wait_window(TITLE)
    os88marty.guest_sleep(m, 12.0)
    shot("taskmgr-window", window=TITLE, pad=8, settle=False)

    _content_click(ui)
    os88marty.guest_sleep(m, 3.0)
    shot("taskmgr-memory", window=TITLE, pad=8, settle=False)

    _content_click(ui)
    os88marty.guest_sleep(m, 3.0)
    shot("taskmgr-heap", window=TITLE, pad=8, settle=False)
