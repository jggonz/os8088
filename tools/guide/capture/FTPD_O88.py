"""The FTP Server's screenshots, for tools/guide/manuals/FTPD_O88.md.

    python3 tools/guide/capture/run.py FTPD_O88

MartyPC has no network card, so Start is refused and says why; the window,
its menu and the Setup page are what is shown.
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
    ui.path("B:/APPS/FTPD.O88")
    os88marty.guest_sleep(ui.m, 3)
    shot("ftpd-window", window="FTP Server", pad=8)

    _menu_shot(ui, shot, "Server", "ftpd-menu-server")

    # Start: with no network card the server stays stopped and says why.
    ui.menu_pick("Server", "Start / Stop")
    os88marty.guest_sleep(ui.m, 4)
    shot("ftpd-nodriver", window="FTP Server", pad=8)

    ui.menu_pick("Server", "Setup...")
    os88marty.guest_sleep(ui.m, 3)
    shot("ftpd-setup", window="FTP Server", pad=8)
