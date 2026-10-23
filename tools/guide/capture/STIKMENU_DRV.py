"""Screenshots for tools/guide/manuals/STIKMENU_DRV.md and DESK_DRV.md:
Locator's Sticky Apps menu, and the Desk Calendar living on the desktop.

    python3 tools/guide/capture/run.py STIKMENU_DRV
"""
import struct

import os88marty


def _sticky_menu(ui, shot, sid):
    cells = ui.menus()
    _, x0, x1, _items = [c for c in cells if c[0] == "Sticky Apps"][0]
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 8.0)
    shot(sid, rect=(max(0, x0 - 40), 0, 360, 342), settle=False)
    ui.mo.to(200, 440, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 2.0)


def capture(ui, shot):
    m = ui.m
    m.write(ui._S("clk_day"), bytes([14, 10]))
    m.write(ui._S("clk_year"), struct.pack("<H", 2026))
    m.write(ui._S("clk_sec"), bytes([0, 20, 9]))       # 09:20:00

    # The menu before anything is running: Desk Calendar offers Add.
    _sticky_menu(ui, shot, "stikmenu-drv-add")

    # Open the calendar from its folder; DESK.DRV comes with it.
    w = ui.path("A:/STIKAPPS/DESKCAL.O88")
    for o in list(ui.windows()):
        if o.i != w.i and o.visible:
            ui.close(o)
    os88marty.guest_sleep(m, 3.0)
    ui.mo.to(60, 300)
    os88marty.guest_sleep(m, 2.0)
    shot("desk-drv-desktop")

    # The name tag becomes a handle under the pointer: that is the drag.
    w = ui.widget_hover(ui.window("Desk Calendar"), True)
    x, y, ww, hh = ui._rect(w.i)
    ui.mo.to(x + 100, y + hh - 6)
    os88marty.guest_sleep(m, 1.0)
    shot("desk-drv-handle", rect=(x - 8, y + hh - 70, ww + 18, 80))
    ui.mo.to(60, 300)
    os88marty.guest_sleep(m, 2.0)
    m.key("Escape")
    os88marty.guest_sleep(m, 1.5)

    # ...and the menu with it running: ticked On.
    _sticky_menu(ui, shot, "stikmenu-drv-on")
