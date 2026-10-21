"""Lemmings' screenshots, for tools/guide/manuals/LEMMINGS_O88.md.

    python3 tools/guide/capture/run.py LEMMINGS_O88 --apps build/lemmings360.img

The 360KB disk does not carry Fun 1 (a Dirt level), so the capture plays
Fun 2. On the 4.77 MHz XT the preview is drawn in about two seconds and the
level is built in about ten, so the waits below are long in guest time.
"""
import os88marty


def _tap(m, name, secs=0.3):
    m.key(name, up=False)
    os88marty.guest_sleep(m, 0.1)
    m.key(name, down=False)
    os88marty.guest_sleep(m, secs)


def capture(ui, shot):
    m = ui.m
    ui.path("B:/LEMMINGS/LEMMINGS.O88")
    ui.mo.to(630, 470)
    os88marty.guest_sleep(m, 3.0)
    _tap(m, "ArrowDown", 1.0)          # Fun 2, the first level this disk carries
    # The whole screen: the folder window behind is called LEMMINGS and the
    # launcher Lemmings, and the window matcher cannot tell them apart.
    shot("lemmings-launcher", settle=False)

    # Play: the preview in the game's own screen, then the level.
    _tap(m, "Enter", 6.0)
    _tap(m, "Enter", 20.0)
    # Let a few lemmings out before the picture is taken.
    os88marty.guest_sleep(m, 15.0)
    shot("lemmings-play", settle=False)

    # Esc finishes the level; the results screen, then the list again.
    _tap(m, "Escape", 8.0)
    _tap(m, "Escape", 6.0)
