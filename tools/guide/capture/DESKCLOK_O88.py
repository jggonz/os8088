"""Desk Clock's screenshots, for tools/guide/manuals/DESKCLOK_O88.md.

    python3 tools/guide/capture/run.py DESKCLOK_O88

The clock off the system disk's STIKAPPS folder. The machine has no clock
card, so the date and time are set by hand first (the fields the clock
reads), and set again to just before the alarm to make it ring. With the
seconds on the screen never stops changing, so no shot waits for it to.
"""
import struct

import os88marty

TITLE = "Desk Clock"


def _clock(ui):
    return ui.window(TITLE)


def _activate(ui):
    """A press on the clock's face: it takes the menu bar and the keys."""
    w = _clock(ui)
    rec = ui._wrec(w)
    if ui._word("menu_win") != rec:
        ui.mo.click(w.x + w.w // 2, w.y + 20, settle=0)
        ui._wait(lambda: ui._word("menu_win") == rec,
                 "the clock to take the bar", 30)
    os88marty.guest_sleep(ui.m, 0.5)


def _key(ui, name, wait=3.0):
    ui.m.key(name)
    os88marty.guest_sleep(ui.m, wait)


def _rest(ui):
    ui.mo.to(620, 440)
    os88marty.guest_sleep(ui.m, 1.5)


def _menu_shot(ui, shot, title, sid):
    cells = ui.menus()
    _, x0, x1, _items = [c for c in cells if c[0].upper() == title.upper()][0]
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 2.0)
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6), settle=False)
    ui.mo.to(620, 440, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def capture(ui, shot):
    m = ui.m
    m.write(ui._S("clk_day"), bytes([14, 10]))         # Wed 14 Oct 2026,
    m.write(ui._S("clk_year"), struct.pack("<H", 2026))
    m.write(ui._S("clk_sec"), bytes([0, 9, 10]))       # 10:09:00
    w = ui.path("A:/STIKAPPS/DESKCLOK.O88")
    for o in list(ui.windows()):
        if o.i != w.i and o.visible:
            ui.close(o)
    os88marty.guest_sleep(m, 3.0)

    # Medium Classic, with the seconds on.
    _activate(ui)
    _key(ui, "KeyS", 2.0)
    _rest(ui)
    shot("deskclok-clock", window=TITLE, pad=8, settle=False)

    _activate(ui)
    _menu_shot(ui, shot, "Clock", "deskclok-menu")

    # The settings window beside the clock, Red LED previewed on the desk.
    _activate(ui)
    _key(ui, "Enter", 14.0)
    for _ in range(6):
        _key(ui, "ArrowRight", 3.0)
    _rest(ui)
    shot("deskclok-desktop", settle=False)
    shot("deskclok-settings", window="Desk Clock Settings", pad=8, settle=False)
    _key(ui, "Escape", 8.0)             # Cancel: Classic again

    # The alarm window: on, 07:30, a label.
    _activate(ui)
    _key(ui, "KeyA", 5.0)
    _key(ui, "Space", 0.8)
    _key(ui, "Tab", 0.8)
    _key(ui, "Digit3", 0.8)
    _key(ui, "Digit0", 0.8)
    _key(ui, "Tab", 0.8)
    for _ in range(8):
        _key(ui, "Backspace", 0.4)
    for i, ch in enumerate("Back up files"):
        m.type_text(ch)
        os88marty.pace(m, 0.3)
    os88marty.guest_sleep(m, 1.5)
    _rest(ui)
    shot("deskclok-alarm", window="Alarm", pad=8, settle=False)
    _key(ui, "Enter", 4.0)

    # It rings at 07:30: the card grows for Snooze and Dismiss.
    _rest(ui)
    h0 = _clock(ui).h
    m.write(ui._S("clk_sec"), bytes([58, 29, 7]))
    ui._wait(lambda: _clock(ui).h > h0, "the alarm to ring", 60)
    os88marty.guest_sleep(m, 1.0)
    shot("deskclok-ringing", window=TITLE, pad=8, settle=False)
    _activate(ui)
    _key(ui, "KeyD", 3.0)

    # A digital face, Large, the face alone.
    _activate(ui)
    for _ in range(6):
        _key(ui, "ArrowRight", 3.0)
    _key(ui, "ArrowUp", 5.0)
    _key(ui, "KeyF", 5.0)
    _rest(ui)
    shot("deskclok-led", window=TITLE, pad=8, settle=False)
