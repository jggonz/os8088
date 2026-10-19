"""Cyclone 88's screenshots, for tools/guide/manuals/CYCLONE_O88.md.

    python3 tools/guide/capture/run.py CYCLONE_O88 --apps build/games360.img

The title screen says PRESS ENTER TO START, and Enter is what starts it (not
Space). The game's own state byte is read so a missed Enter is pressed again.
"""
import os
import struct
import sys

import os88geom as geom
import os88marty

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tests"))
from cycweb import pkg_syms, Pkg, CYS_TITLE  # noqa: E402

CYS_PLAY = 2


def _play(ui, p, secs):
    """Hold fire and sweep round the rim, with a zap and a jump on the way."""
    m = ui.m
    m.key("Space", up=False)
    t = 0.0
    side = "ArrowLeft"
    while t < secs:
        m.key(side, up=False)
        os88marty.guest_sleep(m, 0.6)
        m.key(side, down=False)
        os88marty.guest_sleep(m, 0.4)
        side = "ArrowRight" if side == "ArrowLeft" else "ArrowLeft"
        t += 1.0
    m.key("Space", down=False)


def capture(ui, shot):
    ui.path("B:/CYCLONE.O88")
    w = ui.window("Cyclone 88")
    for o in ui.windows():
        if o.visible and not o.title.startswith("Cyclone"):
            ui.close(o)
    seg = struct.unpack("<H", ui.m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE
                                        + geom.W_SEG, 2))[0]
    syms, _ = pkg_syms()
    p = Pkg(ui.m, seg, syms)
    ui.mo.to(2, 470)
    os88marty.guest_sleep(ui.m, 6.0)
    shot("cyclone-title", window="Cyclone 88", pad=8, settle=False)

    ui.menu_pick("Help", "How To Play")
    os88marty.guest_sleep(ui.m, 1.0)
    shot("cyclone-help", window="Cyclone 88", pad=8, settle=False)
    ui.m.key("KeyQ")                    # any key puts the panel away
    os88marty.guest_sleep(ui.m, 1.0)

    for _ in range(10):
        if p.rb("cy_state") != CYS_TITLE:
            break
        ui.m.key("Enter")
        os88marty.guest_sleep(ui.m, 1.0)
    for _ in range(40):
        if p.rb("cy_state") == CYS_PLAY:
            break
        os88marty.guest_sleep(ui.m, 0.5)
    _play(ui, p, 8)
    shot("cyclone-play", window="Cyclone 88", pad=8, settle=False)

    # Full screen: the same game across the whole desktop.
    ui.m.key("KeyF")
    os88marty.guest_sleep(ui.m, 4.0)
    if p.rb("cy_state") == CYS_PLAY:
        _play(ui, p, 6)
    shot("cyclone-fullscreen", settle=False)
    ui.m.key("KeyF")
    os88marty.guest_sleep(ui.m, 3.0)
