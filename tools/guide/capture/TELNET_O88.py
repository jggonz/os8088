"""Telnet's screenshots, for tools/guide/manuals/TELNET_O88.md.

    python3 tools/guide/capture/run.py TELNET_O88

MartyPC has no network card, so a connection is refused at once and says
why; the window, the menu and the full-screen terminal are what is shown.
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
    ui.path("B:/APPS/TELNET.O88")
    os88marty.guest_sleep(ui.m, 3)
    w = ui.window("Telnet")
    ui.mo.click(w.x + 120, w.y + 29)            # the host box
    ui.m.type_text("bbs.example.com:23")
    os88marty.guest_sleep(ui.m, 1)
    shot("telnet-window", window="Telnet", pad=8)

    # Connect: with no network card the session fails and says why.
    ui.m.key("Enter")
    os88marty.guest_sleep(ui.m, 4)
    shot("telnet-failed", window="Telnet", pad=8)

    _menu_shot(ui, shot, "Session", "telnet-menu-session")

    # The About card carries the one tip a board needs: Scroll Lock.
    print(ui.menus())
    ui.menu_pick("Telnet", "About Telnet")
    os88marty.guest_sleep(ui.m, 3)
    w = ui.window("Telnet")
    ui.mo.to(w.x + w.w - 20, w.y + 4)
    shot("telnet-about", window="Telnet", pad=8)
