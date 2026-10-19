"""Desk Calendar's screenshots, for tools/guide/manuals/DESKCAL_O88.md.

The machine has no clock card, so the date and time are set by hand first
(the same clock fields the calendar reads), which puts the pictures on a
Wednesday morning in October rather than on 1 January 1980.
"""
import struct

import os88marty
import os88stik

TITLE = "Desk Calendar"


def _at(ui, card, x, y):
    w = ui.window(TITLE)
    fx, fy = w.x + 1, w.y
    li = ui._wfield(w, "stik_li")
    return fx + x, fy + os88stik.deskcal_card_top(li, card) + y


def _type(ui, s):
    for i in range(0, len(s), 4):
        ui.m.type_text(s[i:i + 4])
        os88marty.pace(ui.m, 0.6)


def _entry(ui, key, text):
    ui.widget_activate(ui.window(TITLE))
    ui.m.type_text(key)
    os88marty.guest_sleep(ui.m, 1.0)
    _type(ui, text)
    ui.m.key("Enter")
    os88marty.guest_sleep(ui.m, 1.5)


def _menu_shot(ui, shot, title, sid, rect=None):
    cells = ui.menus()
    cell = [c for c in cells if c[0].upper() == title.upper()][0]
    _, x0, x1, items = cell
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 2.0)
    if rect is None:
        mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
        my2 = ui._word("menu_y2")
        left = max(0, min(x0, mx1) - 6)
        rect = (left, 0, max(x1, mx2) - left + 8, my2 + 6)
    shot(sid, rect=rect, settle=False)
    ui.mo.to(200, 300, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def capture(ui, shot):
    m = ui.m
    m.write(ui._S("clk_day"), bytes([14, 10]))
    m.write(ui._S("clk_year"), struct.pack("<H", 2026))
    m.write(ui._S("clk_sec"), bytes([0, 20, 9]))       # 09:20:00
    w = ui.path("A:/STIKAPPS/DESKCAL.O88")
    for o in list(ui.windows()):
        if o.i != w.i and o.visible:
            ui.close(o)
    os88marty.guest_sleep(m, 3.0)

    _entry(ui, "n", "9:30 Dentist")
    _entry(ui, "n", "1400 Team meeting")
    _entry(ui, "k", "Buy floppy disks")
    _entry(ui, "k", "Ribbon for printer")
    # Tick the first task's box.
    g = os88stik.DESKCAL_GEO[ui._wfield(ui.window(TITLE), "stik_li")]
    x, y = _at(ui, 1, 8 + g["BOX"] // 2, g["T0"] + g["BOX"] // 2)
    ui.mo.click(x, y)
    os88marty.guest_sleep(m, 1.5)
    ui.mo.to(60, 300)
    os88marty.guest_sleep(m, 2.0)
    shot("deskcal-widget", window=TITLE, pad=8)

    # The one-line entry, open on card 2's heading.
    ui.widget_activate(ui.window(TITLE))
    m.type_text("n")
    os88marty.guest_sleep(m, 1.0)
    _type(ui, "18:00 Pick up Sam")
    ui.mo.to(60, 300)
    os88marty.guest_sleep(m, 1.5)
    shot("deskcal-entry", window=TITLE, pad=8)
    m.key("Enter")
    os88marty.guest_sleep(m, 1.5)

    # The Calendar and Remind menus, while the calendar owns the bar.
    ui.widget_activate(ui.window(TITLE))
    _menu_shot(ui, shot, "Calendar", "deskcal-menu")
    ui.widget_activate(ui.window(TITLE))
    _menu_shot(ui, shot, "Remind", "deskcal-remind-menu")

    # A reminder going off: the clock to just before 09:30.
    m.write(ui._S("clk_sec"), bytes([50, 29, 9]))
    os88marty.guest_sleep(m, 16.0)
    shot("deskcal-ringing", window=TITLE, pad=8)
    shot("deskcal-desktop")

    # The name tag turns into a handle under the pointer.
    w = ui.widget_hover(ui.window(TITLE), True)
    x, y, ww, hh = ui._rect(w.i)
    ui.mo.to(x + 100, y + hh - 6)
    os88marty.guest_sleep(m, 1.0)
    shot("deskcal-handle", rect=(x - 8, y + hh - 70, ww + 18, 80))

    # Locator's Sticky Apps menu, with the calendar ticked On.
    ui.mo.to(60, 300)
    os88marty.guest_sleep(m, 2.0)
    ui.m.key("Escape")
    os88marty.guest_sleep(m, 1.5)
    cells = ui.menus()
    _, x0, x1, _items = [c for c in cells if c[0] == "Sticky Apps"][0]
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(m, 8.0)
    shot("deskcal-stickymenu", rect=(152, 0, 344, 340), settle=False)
    ui.mo.to(200, 440, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(m, 2.0)
