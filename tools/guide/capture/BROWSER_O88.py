"""The Browser's screenshots, for tools/guide/manuals/BROWSER_O88.md.

    python3 tools/guide/capture/run.py BROWSER_O88

MartyPC has no network card, so every page here comes off the floppy: the
Browser's own manual, BROWSER.HTM in MEDIA/. The two fetches it tries show
the refusals a machine with no network gives.
"""
import os
import sys

import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "tests"))


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


def _browser(ui):
    return [w for w in ui.windows() if "Browser" in w.title][0]


def capture(ui, shot):
    # The Control Panel's Drivers page, where the Ethernet driver is ticked.
    import dispcp
    dispcp.open_panel(ui.m, ui.mo, ui._S, ui.settle, page=2)
    ui.settle()
    # Tick the Ethernet row (the third). With no card the row says so.
    cp = ui.window("Control Panel")
    cx, cy = cp.x + 1, cp.y + 19
    ui.mo.click(cx + dispcp.CP_RX + 8, cy + dispcp.CP_DBY1 + 2 * dispcp.CP_DROWH + 8)
    os88marty.guest_sleep(ui.m, 10)
    shot("browser-drivers", window="Control Panel", pad=8)
    ui.close(ui.window("Control Panel"))

    # Double-clicking BROWSER.HTM opens it in the Browser.
    ui.path("B:/MEDIA/BROWSER.HTM")
    os88marty.guest_sleep(ui.m, 6)
    ui.settle()
    w = _browser(ui)
    # Zoom it: a double-click on the title bar.
    ui.mo.dblclick(w.x + w.w // 2, w.y + 6)
    os88marty.guest_sleep(ui.m, 3)
    ui.settle()
    w = _browser(ui)
    shot("browser-window", window=w.title, pad=0)

    # Further down the page: the table and the refusals.
    # The bar opens with the keyboard; Esc hands it to the page.
    ui.m.key("Escape")
    ui.m.key("End")
    os88marty.guest_sleep(ui.m, 3)
    shot("browser-scrolled", window=w.title, pad=0)

    # An https link is refused by name: the "try one" after "RSA-2048...".
    w = _browser(ui)
    ui.mo.click(w.x + 50, w.y + 143)
    os88marty.guest_sleep(ui.m, 3)
    shot("browser-https", rect=(w.x, w.y, w.w, 180))

    _menu_shot(ui, shot, "File", "browser-menu-file")

    # Open Location: the bar starts with http://. No card, so the fetch
    # stops at once and says why.
    ui.menu_pick("File", "Open Location...")
    os88marty.guest_sleep(ui.m, 1)
    ui.m.type_text("example.com\n")
    os88marty.guest_sleep(ui.m, 4)
    w = _browser(ui)
    shot("browser-nolink", rect=(w.x - 4, w.y - 4, w.w + 10, 60))
