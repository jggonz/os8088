"""PaccMan's screenshots, for tools/guide/manuals/PACCMAN_O88.md.

    python3 tools/guide/capture/run.py PACCMAN_O88 --apps build/paccman360.img

On the 4.77 MHz XT the game runs at about a quarter of arcade speed, so the
waits below are long in guest time. The full-screen shot is taken from a game
STARTED in full screen: pressing F in the middle of a game left the screen
full of garbage on this machine (see the report that came with the manual).
"""
import os88marty


def _tap(m, name, secs=0.3):
    m.key(name, up=False)
    os88marty.guest_sleep(m, 0.1)
    m.key(name, down=False)
    os88marty.guest_sleep(m, secs)


def _hold(m, name, secs):
    m.key(name, up=False)
    os88marty.guest_sleep(m, secs)
    m.key(name, down=False)
    os88marty.guest_sleep(m, 0.1)


STEER = (("ArrowLeft", 6.0), ("ArrowUp", 5.0), ("ArrowRight", 6.0),
         ("ArrowDown", 4.0), ("ArrowLeft", 5.0))


def capture(ui, shot):
    m = ui.m
    ui.path("B:/PACCMAN.O88")
    ui.mo.to(630, 470)
    # The attract screen introduces the ghosts one by one.
    os88marty.guest_sleep(m, 30.0)
    shot("paccman-attract", window="PaccMan", pad=6, settle=False)

    # Any key starts a game; the prelude tune plays before READY! clears.
    _tap(m, "Enter")
    os88marty.guest_sleep(m, 22.0)
    for k, secs in STEER:
        _hold(m, k, secs)
    shot("paccman-play", window="PaccMan", pad=6, settle=False)

    # A second PaccMan, put full screen on its attract screen, then started.
    ui.menu_pick("PaccMan", "Close")
    os88marty.guest_sleep(m, 3.0)
    ui.path("B:/PACCMAN.O88")
    ui.mo.to(630, 470)
    os88marty.guest_sleep(m, 5.0)
    _tap(m, "KeyF")
    os88marty.guest_sleep(m, 10.0)
    _tap(m, "Enter")
    os88marty.guest_sleep(m, 22.0)
    for k, secs in STEER[:3]:
        _hold(m, k, secs)
    shot("paccman-full", settle=False)
    _tap(m, "KeyF")
    os88marty.guest_sleep(m, 8.0)
