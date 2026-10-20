"""PiXEL's screenshots, for tools/guide/manuals/PIXEL_O88.md.

    python3 tools/guide/capture/run.py PIXEL_O88 --apps build/pixel360.img
"""

import os88marty


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
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6))
    # let go over the menu bar's empty right-hand end: nothing is picked
    ui.mo.to(470, 5, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def _ctrl(ui, key):
    ui.m.key("ControlLeft", up=False)
    ui.m.key(key)
    ui.m.key("ControlLeft", down=False)


def capture(ui, shot):
    ui.path("B:/PICTURES/VACATION.JPG")
    os88marty.guest_sleep(ui.m, 20.0)
    ui.mo.to(630, 470)
    ui.settle(limit=400.0)
    shot("pixel-window", window="PiXEL")

    _menu_shot(ui, shot, "Image", "pixel-menu-image")
    _menu_shot(ui, shot, "Effects", "pixel-menu-effects")

    # a parameter card
    ui.menu_pick("Image", "Brightness")
    os88marty.guest_sleep(ui.m, 3.0)
    ui.mo.to(630, 470)
    ui.settle(limit=120.0)
    shot("pixel-card", window="PiXEL")
    ui.m.key("Escape")
    os88marty.guest_sleep(ui.m, 3.0)

    # a selection with the Marquee tool
    w = ui.window("PiXEL")
    x, y, ww, hh = ui._rect(w.i)
    mx, my = x + ww // 2 - 40, y + hh // 2
    ui.m.type_text("m")
    os88marty.guest_sleep(ui.m, 1.0)
    ui.mo.drag(mx - 60, my - 45, mx + 50, my + 40)
    ui.mo.to(630, 470)
    ui.settle(limit=120.0)
    shot("pixel-select", window="PiXEL")
    ui.m.key("Escape")
    os88marty.guest_sleep(ui.m, 2.0)

    # the key card
    ui.m.key("F1")
    os88marty.guest_sleep(ui.m, 3.0)
    ui.settle(limit=120.0)
    shot("pixel-help", window="PiXEL")
    ui.m.key("Escape")
    os88marty.guest_sleep(ui.m, 3.0)
    ui.settle(limit=120.0)

    # Save As's format card
    _ctrl(ui, "KeyS")
    os88marty.guest_sleep(ui.m, 4.0)
    ui.settle(limit=120.0)
    shot("pixel-saveas", window="PiXEL")
    ui.m.key("Escape")
    os88marty.guest_sleep(ui.m, 3.0)
    ui.settle(limit=120.0)

    # full screen, 320x240 in 256 colours
    ui.m.type_text("f")
    os88marty.guest_sleep(ui.m, 30.0)
    ui.settle(limit=300.0)
    shot("pixel-full")
    ui.m.type_text("f")
    os88marty.guest_sleep(ui.m, 5.0)
