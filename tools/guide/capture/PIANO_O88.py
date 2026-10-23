"""Piano's screenshots, for tools/guide/manuals/PIANO_O88.md."""

import os88marty

TITLE_H = 18


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
    ui.mo.to(470, 5, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def capture(ui, shot):
    ui.path("B:/APPS/PIANO.O88")
    w = ui.window("Piano")
    x0, y0 = w.x + 1, w.y + TITLE_H
    ui.mo.to(w.x + w.w + 40, w.y + 20)
    # a few bars played on the keyboard's letters
    for k in "ddfggfdsaasddss":
        ui.m.type_text(k)
        os88marty.guest_sleep(ui.m, 0.35)
    ui.settle()
    shot("piano-window", window="Piano", pad=10)

    _menu_shot(ui, shot, "Songs", "piano-songs-menu")

    ui.menu_pick("Songs", "Ode to Joy")
    ui.settle()
    shot("piano-song", window="Piano", pad=10)

    # REPLAY, and the picture taken while it plays
    ui.mo.click(x0 + 30, y0 + 60)
    os88marty.guest_sleep(ui.m, 3.0)
    shot("piano-replay", window="Piano", pad=10, settle=False)
