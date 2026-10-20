"""Screenshots for the hard-disk side of the system files: HDDTOOL_DRV.md,
HIBER_DRV.md, RAMPAGE_DRV.md and SYSTEM_CFG.md.

Needs a machine with a hard disk:

    python3 tools/guide/capture/run.py HDDTOOL_DRV --machine os8088_xt_vga_hdd

The disk is MartyPC's bundled 31MB XT-IDE image, which the launcher copies
into the instance's own directory, so nothing here touches a shared file.
Nothing is formatted, installed or hibernated: every dialog is shown and then
cancelled or closed.
"""
import os
import sys

import os88marty
import os88geom as geom

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tests"))
import dispcp  # noqa: E402


def _wait(ui, secs):
    os88marty.guest_sleep(ui.m, secs)


def _hold_menu(ui, shot, menu, item, sid, rect):
    """Drop `menu`, highlight `item`, take the shot, release off the menu."""
    cells = ui.menus()
    i = [k for k, c in enumerate(cells) if c[0] == menu][0]
    _, x0, x1, _ = cells[i]
    ui.mo.to((x0 + x1) // 2, geom.MBAR_H // 2)
    ui.mo._edge(True)
    _wait(ui, 1)
    k = [j for j, (s, _) in enumerate(ui.menus()[i][3]) if s == item][0]
    y1 = ui._word("menu_y1")
    ui.mo.to(ui._word("menu_x1") + 8,
             y1 + 1 + k * geom.MENU_ITEM_H + geom.MENU_ITEM_H // 2, l=True)
    _wait(ui, 1)
    shot(sid, rect=rect)
    ui.mo.to(500, 400, l=True)
    ui.mo._edge(False)
    _wait(ui, 1)


def _cp_row(ui, row):
    """Click drawn row `row` of the Control Panel's page list."""
    w = ui.window("Control Panel")
    ui.mo.click(w.x + 1 + dispcp.CP_IX + 30,
                w.y + dispcp.TITLE_H + 1 + dispcp.CP_I0Y
                + row * dispcp.CP_IROWH + dispcp.CP_IROWH // 2)
    _wait(ui, 8)


def capture(ui, shot):
    # --- Hibernate: the System menu item, then its window -------------------
    _hold_menu(ui, shot, "Apple", "Hibernate...", "hiber-drv-menu",
               (0, 0, 180, 130))
    ui.menu_pick("Apple", "Hibernate...")
    ui.wait_window("Hibernate", limit=60)
    ui.mo.to(600, 420)
    shot("hiber-drv-window", window="Hibernate", pad=8)
    ui.m.key("Escape")                      # the window's Cancel
    _wait(ui, 3)

    # --- the Control Panel's Hard Drive page, and its two windows ----------
    ui.menu_pick("Apple", "Control Panel")
    ui.wait_window("Control Panel", limit=60)
    _cp_row(ui, 8)                          # Hard Drive, the last row
    ui.mo.to(600, 420)
    shot("hddtool-drv-page", window="Control Panel", pad=8)

    w = ui.window("Control Panel")
    ui.mo.click(w.x + 127, w.y + 107)       # Format
    ui.wait_window("Format", limit=60)
    _wait(ui, 4)
    ui.mo.to(600, 420)
    shot("hddtool-drv-format", window="Format", pad=8)
    t = ui.window("Format")
    ui.mo.click(t.x + 175, t.y + 149)       # its Close button
    _wait(ui, 4)

    ui.raise_window(ui.window("Control Panel"))
    w = ui.window("Control Panel")
    ui.mo.click(w.x + 195, w.y + 107)       # Install
    ui.wait_window("Install", limit=60)
    _wait(ui, 6)
    ui.mo.to(600, 420)
    shot("hddtool-drv-install", window="Install", pad=8)
    t = ui.window("Install")
    ui.mo.click(t.x + 131, t.y + 121)       # its Close button
    _wait(ui, 4)

    # --- the Drivers page: tick Ram Disk, then open its page ----------------
    ui.raise_window(ui.window("Control Panel"))
    _cp_row(ui, 2)                          # Drivers
    cp = ui.window("Control Panel")
    cx, cy = cp.x + 1, cp.y + 19
    ui.mo.click(cx + dispcp.CP_RX + 8,
                cy + dispcp.CP_DBY1 + 3 * dispcp.CP_DROWH + 8)
    _wait(ui, 10)
    ui.mo.to(600, 420)
    shot("rampage-drv-drivers", window="Control Panel", pad=8)

    cp = ui.window("Control Panel")         # the list scrolls now: two down
    for _ in range(2):
        ui.mo.click(cp.x + 60, cp.y + 142)
        _wait(ui, 2)
    _cp_row(ui, 7)                          # Ram Disk
    ui.mo.to(600, 420)
    shot("rampage-drv-page", window="Control Panel", pad=8)

    # --- closing the panel writes SYSTEM.CFG --------------------------------
    ui.close(ui.window("Control Panel"))
    ui.wait_toast(says="Settings", limit=60)
    shot("system-cfg-saved", rect=(0, 0, 640, 20), settle=False)
