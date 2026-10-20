"""WireChat's screenshots, for tools/guide/manuals/WIRECHAT_O88.md.

    python3 tools/guide/capture/run.py WIRECHAT_O88 --apps build/network360.img

WireChat is on the network disk, at its root. MartyPC has no network card,
so the window is shown as a machine with no driver sees it: a nick chosen,
the status bar saying why nothing is connected, the status popup and the
Chat menu.
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
    ui.mo.to(470, 5, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def capture(ui, shot):
    ui.path("B:/WIRECHAT.O88")
    os88marty.guest_sleep(ui.m, 4)
    # The first run asks for a nick; the field has the keyboard.
    ui.m.type_text("George")
    ui.m.key("Enter")
    os88marty.guest_sleep(ui.m, 4)
    ui.m.type_text("Hello from os8088!")
    os88marty.guest_sleep(ui.m, 1)
    shot("wirechat-window", window="WireChat", pad=8)

    # Set status... is the header's button, at its right end.
    w = ui.window("WireChat")
    ui.mo.click(w.x + w.w - 60, w.y + 30)
    ui.mo.to(w.x + w.w - 250, w.y + 120)            # the arrow off the popup
    os88marty.guest_sleep(ui.m, 2)
    shot("wirechat-status", rect=(w.x + w.w - 200, w.y, 208, 90))
    ui.mo.click(w.x + w.w // 2, w.y + w.h // 2)     # a press elsewhere: gone
    os88marty.guest_sleep(ui.m, 2)

    _menu_shot(ui, shot, "Chat", "wirechat-menu-chat")
