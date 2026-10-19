"""Apple II Plus's screenshots, for tools/guide/manuals/APPLE2_O88.md.

The emulated Apple runs at about half a per cent of its real speed on the
4.77 MHz XT, so every step waits in GUEST seconds and types one key at a time
(the Apple has a one-key keyboard latch).
"""
import os88marty
import os88ui

TITLE = "Apple II Plus"
AWAY = (620, 300)                           # the pointer, off the window


def slow(ui, s, gap=1.5):
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
    # Double-clicking WELCOME.BAS starts the machine with it loaded.
    ui.path("B:/APPLE2/WELCOME.BAS")
    print("menus:", ui.menus())
    ui.mo.to(*AWAY)
    os88marty.guest_sleep(ui.m, 40)         # the ROM's own start-up, to ]
    slow(ui, "RUN\n")
    os88marty.guest_sleep(ui.m, 15)
    shot("apple2-window", window=TITLE, pad=10)
    slow(ui, " ")
    os88marty.guest_sleep(ui.m, 90)         # 32 HLINs, drawn as you watch
    shot("apple2-lores", window=TITLE, pad=10)

    # Control-Reset back to the ] prompt, then some BASIC by hand.
    ui.menu_pick("Machine", "Control-Reset")
    ui.mo.to(*AWAY)
    os88marty.guest_sleep(ui.m, 10)
    slow(ui, "HOME\n")
    os88marty.guest_sleep(ui.m, 5)
    slow(ui, "PRINT 2+2\n")
    os88marty.guest_sleep(ui.m, 5)
    slow(ui, "LIST 30,70\n")
    os88marty.guest_sleep(ui.m, 20)
    shot("apple2-prompt", window=TITLE, pad=10)

    x0 = drop(ui, "Machine")
    shot("apple2-machine", rect=(0, 0, x0 + 240, 215))
    undrop(ui)

    ui.menu_pick("Machine", "Power On")
    ui.mo.to(*AWAY)
    os88marty.guest_sleep(ui.m, 5)
    shot("apple2-power", window=TITLE, pad=10)
    ui.m.type_text("n")
    os88marty.guest_sleep(ui.m, 5)
