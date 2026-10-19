"""Bounce's screenshots, for tools/guide/manuals/BOUNCE_O88.md."""

import os88geom as geom
import os88marty


def _clean(ui, win):
    """Wait for a frame where the square is whole: the worker draws the new
    square before it wipes what is left of the old one, so a frame caught
    between the two shows a smear."""
    for _ in range(40):
        os88marty.guest_sleep(ui.m, 0.07)
        w, h, data = ui.m.fbuf()
        x, y, ww, hh = win.x, win.y, win.w, win.h
        black = 0
        for yy in range(y + geom.TITLE_H, y + hh - 1):
            row = data[(yy * w + x + 1) * 3:(yy * w + x + ww - 1) * 3]
            black += sum(1 for i in range(0, len(row), 3) if row[i] < 40)
        if black == 64:
            return


def _black(ui, rect):
    w, h, data = ui.m.fbuf()
    x, y, ww, hh = rect
    n = 0
    for yy in range(y, y + hh):
        row = data[(yy * w + x) * 3:(yy * w + x + ww) * 3]
        n += sum(1 for i in range(0, len(row), 3) if row[i] < 40)
    return n


def _card(ui, rect):
    """Wait for the About card to finish drawing: the first frame whose
    black count holds for three looks. The square keeps moving over the
    card, so the earliest whole frame is the one to keep."""
    last, same = -1, 0
    for _ in range(60):
        os88marty.guest_sleep(ui.m, 0.04)
        n = _black(ui, rect)
        same = same + 1 if n == last else 0
        last = n
        if same >= 2 and n > 200:
            return


def capture(ui, shot):
    ui.path("B:/APPS/BOUNCE.O88")
    w = ui.window("Bounce")
    ui.mo.to(620, 400)
    os88marty.guest_sleep(ui.m, 3.0)
    _clean(ui, w)
    shot("bounce-window", window="Bounce", pad=10, settle=False)

    # A second Bounce opens on top of the first; move it aside, and put the
    # Disk windows away so both can be seen.
    ui.path("B:/APPS/BOUNCE.O88")
    second = ui.front()
    ui.move_window(second, w.x - 170, w.y)
    for o in ui.windows():
        if o.visible and o.title != "Bounce":
            ui.close(o)
    ui.mo.to(620, 400)
    os88marty.guest_sleep(ui.m, 2.0)
    shot("bounce-two", rect=(w.x - 180, w.y - 10, 340 + 12, w.h + 22),
         settle=False)

    # The About card. Taken at once: the square keeps moving over it.
    ui.raise_window(second)
    ui.menu_pick("Bounce", "About Bounce")
    x, y, ww, hh = ui._rect(second.i)
    _card(ui, (x + 1, y + geom.TITLE_H, second.w - 2, second.h - geom.TITLE_H - 1))
    shot("bounce-about", rect=(x - 10, y - 10, ww + 22, hh + 22), settle=False)
