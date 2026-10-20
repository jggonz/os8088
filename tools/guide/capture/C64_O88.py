"""Commodore 64's screenshots, for tools/guide/manuals/C64_O88.md.

The emulated C64 runs at about one per cent of its real speed on the 4.77 MHz
XT: the KERNAL takes a minute or two of guest time to reach READY, and it
scans its keyboard about once every two guest seconds, so every key is typed
on its own with a gap after it.
"""
import os88marty
import os88ui

TITLE = "VICE"
AWAY = (620, 300)                           # the pointer, off the window


def slow(ui, s, gap=3.0):
    for ch in s:
        ui.m.type_text(ch)
        os88marty.guest_sleep(ui.m, gap)


def drop(ui, title):
    """Press a menu title and hold it, so the pull-down can be shot."""
    for t, x0, x1, items in ui.menus():
        if t == title:
            ui.mo.to((x0 + x1) // 2, os88ui.geom.MBAR_H // 2)
            ui.mo._edge(True)
            os88marty.guest_sleep(ui.m, 2)
            return x0
    raise RuntimeError("no menu %r" % title)


def undrop(ui):
    ui.mo.to(*AWAY)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 2)


def capture(ui, shot):
    w = ui.path("B:/C64/C64.O88")
    print("menus:", ui.menus())
    ui.mo.to(*AWAY)
    os88marty.guest_sleep(ui.m, 150)        # the KERNAL's own start-up
    slow(ui, 'load"*",8\n')                 # there is no disk drive
    os88marty.guest_sleep(ui.m, 20)
    shot("c64-ready", window=TITLE, pad=10)

    slow(ui, '10 print "hello from os8088"\n')
    slow(ui, "20 goto 10\n")
    slow(ui, "run\n")
    os88marty.guest_sleep(ui.m, 30)
    shot("c64-window", window=TITLE, pad=10)
    ui.m.key("Escape")                      # RUN/STOP
    os88marty.guest_sleep(ui.m, 10)

    x0 = drop(ui, "Preferences")
    shot("c64-prefs", rect=(0, 0, x0 + 240, 215))
    undrop(ui)

    ui.menu_pick("Help", "About VICE...")
    ui.mo.to(*AWAY)
    os88marty.guest_sleep(ui.m, 5)
    shot("c64-about", window=TITLE, pad=10)
