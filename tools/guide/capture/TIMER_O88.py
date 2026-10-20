"""Timer's screenshots, for tools/guide/manuals/TIMER_O88.md."""

import os88marty

TITLE_H = 18


def _press(ui, w, k):
    """Click button k (0 Start, 1 Stop, 2 Reset): 46 wide, 6 apart, at +4,+20."""
    x0, y0 = w.x + 1, w.y + TITLE_H
    ui.mo.click(x0 + 4 + k * 52 + 23, y0 + 27)
    os88marty.guest_sleep(ui.m, 0.5)


def capture(ui, shot):
    w = ui.path("B:/APPS/TIMER.O88")
    w = ui.window("Timer")
    os88marty.guest_sleep(ui.m, 6.0)
    shot("timer-window", window="Timer", pad=10, settle=False)

    _press(ui, w, 1)                    # Stop
    ui.mo.to(w.x + w.w + 40, w.y + w.h + 30)
    ui.settle()
    shot("timer-stopped", window="Timer", pad=10)

    ui.menu_pick("Timer", "About Timer")
    ui.settle()
    shot("timer-about", window="Timer", pad=10, settle=False)
