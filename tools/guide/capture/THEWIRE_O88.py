"""The Wire's screenshots, for tools/guide/manuals/THEWIRE_O88.md.

MartyPC has no network card, so this is The Wire as a machine without one
sees it: the window, the advice in its detail pane, and the File menu with
both actions greyed.
"""
import os88marty

TITLE = "The Wire"


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


def capture(ui, shot):
    ui.path("A:/SYSTEM/THEWIRE.O88")
    for o in list(ui.windows()):
        if o.visible and o.title != TITLE:
            ui.close(o)
    os88marty.guest_sleep(ui.m, 4.0)
    shot("thewire-nocard", window=TITLE, pad=8)
    _menu_shot(ui, shot, "File", "thewire-file-menu")
    ui.menu_pick("The Wire", "About The Wire")
    os88marty.guest_sleep(ui.m, 2.0)
    shot("thewire-about", window=TITLE, pad=8)
