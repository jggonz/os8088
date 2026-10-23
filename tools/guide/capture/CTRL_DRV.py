"""The Control Panel's screenshots, for tools/guide/manuals/CTRL_DRV.md and
the pages of the modules it reaches: THMPAL_DRV.md (Customize...),
WIN95_DRV.md (the Windows theme) and DOCK_DRV.md (Dock page). The extended
desktop is EXTD_DRV.py's.

    python3 tools/guide/capture/run.py CTRL_DRV --machine os8088_xt_vga_herc

A VGA XT with a Hercules card beside it, so the Display page is shown (a
machine with one adapter hides it). The Drivers page is SOUND_DRV.md's and is not taken here.
"""
import os
import struct
import sys

import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "tests"))

CP = "Control Panel"
CP_RX = 96              # the right-hand pane's left edge, content-relative
TITLE_H = 18


def _pane(ui, px, py):
    """A click at pane-relative (px, py), aimed off the panel's live rect."""
    w = ui.window(CP)
    ui.mo.click(w.x + 1 + CP_RX + px, w.y + TITLE_H + 1 + py)
    os88marty.guest_sleep(ui.m, 2.0)


def _page(ui, page):
    import dispcp
    dispcp.open_panel(ui.m, ui.mo, ui._S, ui.settle, page=page)
    ui.mo.to(620, 470)
    os88marty.guest_sleep(ui.m, 1.5)


def _menu_rect(ui, x0, x1):
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    return (left, 0, max(x1, mx2) - left + 8, my2 + 6)


def capture(ui, shot):
    m = ui.m
    m.write(ui._S("clk_day"), bytes([14, 10]))
    m.write(ui._S("clk_year"), struct.pack("<H", 2026))
    m.write(ui._S("clk_sec"), bytes([0, 20, 9]))       # 09:20:00

    # A Disk window behind the panel, so the theme shots have chrome to show.
    ui.open_drive("B")
    os88marty.guest_sleep(m, 2.0)

    # --- one shot per page ---------------------------------------------------
    for page, sid in ((0, "ctrl-drv-scheduler"), (1, "ctrl-drv-datetime"),
                      (3, "ctrl-drv-display"), (4, "ctrl-drv-sound"),
                      (5, "ctrl-drv-theme"), (6, "ctrl-drv-dock"),
                      (7, "ctrl-drv-floppy")):
        _page(ui, page)
        shot(sid, window=CP, pad=6)

    # --- Dock: stand the strip on the left (DOCK.DRV) -------------------------
    _page(ui, 6)
    _pane(ui, 4 + 6, 34 + 6)                # Left
    os88marty.guest_sleep(m, 4.0)
    ui.mo.to(620, 470)
    shot("dock-drv-left")
    _pane(ui, 4 + 6, 18 + 6)                # Bottom again
    os88marty.guest_sleep(m, 4.0)

    # --- Theme: Customize... (THMPAL.DRV) -------------------------------------
    _page(ui, 5)
    _pane(ui, 112 + 50, 48 + 8)             # Customize...
    os88marty.guest_sleep(m, 5.0)
    tv = ui.window("Color Theme")
    shot("thmpal-drv-window", window="Color Theme", pad=6)
    # Forest is the fourth preset: a click previews it at once.
    ui.mo.click(tv.x + 1 + 60, tv.y + TITLE_H + 1 + 3 + 3 * 11 + 5)
    os88marty.guest_sleep(m, 5.0)
    ui.mo.to(620, 470)
    shot("thmpal-drv-forest")
    # Cancel puts the opening theme back.
    tv = ui.window("Color Theme")
    ui.mo.click(tv.x + 1 + 130 + 35, tv.y + TITLE_H + 1 + 167 + 8)
    os88marty.guest_sleep(m, 5.0)

    # --- Theme: Windows (WIN95.DRV) -------------------------------------------
    _page(ui, 5)
    _pane(ui, 4 + 6, 66 + 6)                # Windows
    os88marty.guest_sleep(m, 6.0)
    ui.mo.to(620, 470)
    shot("win95-drv-desktop")
    # A menu opened with one click stays open.
    cells = ui.menus()
    if len(cells) > 1:
        _, x0, x1, _items = cells[1]
        ui.mo.click((x0 + x1) // 2, 5)
        os88marty.guest_sleep(m, 2.0)
        shot("win95-drv-menu", rect=_menu_rect(ui, x0, x1), settle=False)
        m.key("Escape")
        os88marty.guest_sleep(m, 2.0)
    _page(ui, 5)
    _pane(ui, 4 + 6, 18 + 6)                # back to Bright
    os88marty.guest_sleep(m, 6.0)
