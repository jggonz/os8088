"""isoTron's screenshots, for tools/guide/manuals/ISOTRON_O88.md.

    python3 tools/guide/capture/run.py ISOTRON_O88 --apps build/games360.img

The player's light cycle is steered by the game's own test bot (the harness
flag tests/isotronlib.py documents), so the rounds last long enough to show
walls going up; everything else is the real game reached through its keys.
"""
import os
import sys

import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "..", "tests"))
import isotronlib as L  # noqa: E402


def _tap(m, name, secs=0.2):
    m.key(name, up=False)
    os88marty.guest_sleep(m, 0.06)
    m.key(name, down=False)
    os88marty.guest_sleep(m, secs)


def _wait_state(g, states, secs):
    for _ in range(int(secs / 0.1)):
        if g.b("it_state") in states:
            return True
        os88marty.guest_sleep(g.m, 0.1)
    return False


def capture(ui, shot):
    m = ui.m
    ui.path("B:/ISOTRON.O88")
    g = L.Game(ui)
    ui.mo.to(630, 470)
    os88marty.guest_sleep(m, 4.0)
    shot("isotron-title", window="isoTron", pad=6)

    # A Rookie game with sound; the bot steers.
    g.put("it_tflags", L.TF_BOT)
    _tap(m, "Digit1")
    _wait_state(g, (L.ST_PLAY,), 15)
    os88marty.guest_sleep(m, 3.0)
    shot("isotron-window", window="isoTron", pad=6, settle=False)

    _wait_state(g, (L.ST_WAIT, L.ST_OVER), 60)
    os88marty.guest_sleep(m, 1.0)
    shot("isotron-crash", window="isoTron", pad=6)

    # The next round, full screen.
    _tap(m, "KeyF")
    os88marty.guest_sleep(m, 2.0)
    _tap(m, "Space")
    _wait_state(g, (L.ST_PLAY,), 15)
    os88marty.guest_sleep(m, 3.0)
    shot("isotron-full", settle=False)
    print("tower height", g.b("it_h"), "state", g.b("it_state"))
    _wait_state(g, (L.ST_WAIT, L.ST_OVER), 60)
    _tap(m, "KeyF")
    os88marty.guest_sleep(m, 2.0)
