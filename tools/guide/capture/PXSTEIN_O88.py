"""Pixelstein 3D's screenshots, for tools/guide/manuals/PXSTEIN_O88.md.

    python3 tools/guide/capture/run.py PXSTEIN_O88 --apps build/games360.img

PXSTEIN.O88 is at the root of the games disk. A new game is started for
real; the pictures then move the player in the game's own memory (the way
tests/pxsact.py does) to the first corridor, in front of the first guards
and beside the elevator switch, so the run does not have to play a whole
floor. The player is made invulnerable so a guard cannot end the run.
"""
import os
import struct
import sys
import zlib

import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "..", "tests"))
import pxslib  # noqa: E402


OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "..", "..", "build", "guide", "shots")


def _png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n"
                + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def _fullshot(ui, sid):
    """The whole screen. shot() reads the card's first aperture, which
    MartyPC sizes at 400 rows here and so cuts the bottom 40 lines off a
    320x240 Mode X screen; this reads the whole field and crops it to the
    first aperture's corner, 480 rows deep."""
    v = ui.m.video()
    aps = v["apertures"]
    a0 = aps[0]
    full = max(range(len(aps)), key=lambda i: aps[i]["w"] * aps[i]["h"])
    w, h, data = ui.m.fbuf(full)
    af = aps[full]
    x0, y0 = a0["x"] - af["x"], a0["y"] - af["y"]
    cw, ch = a0["w"], min(480, h - y0)
    rows = [data[((y0 + y) * w + x0) * 3:((y0 + y) * w + x0 + cw) * 3]
            for y in range(ch)]
    _png(os.path.join(OUT, sid + ".png"), cw, ch, b"".join(rows))
    print("shot %s %dx%d" % (sid, cw, ch))


def _menu_shot(ui, shot, title, sid):
    """Drop a menu, take the picture, and let it go over nothing."""
    cells = ui.menus()
    cell = [c for c in cells if c[0].upper() == title.upper()][0]
    _, x0, x1, items = cell
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 1.5)
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6), settle=False)
    ui.mo.to(470, 5, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def capture(ui, shot):
    m = ui.m
    ui.path("B:/PXSTEIN.O88")
    got = pxslib.find(m, guest=30.0)
    g = pxslib.Game(m, *got)

    def sleep(s):
        os88marty.guest_sleep(m, s)

    def state(name, limit=40.0):
        try:
            g.wait_state(name, limit=limit)
        except Exception as e:          # a picture script: say it and go on
            print("  (waiting for %s: %s)" % (name, str(e).split("\n")[0]))

    ui.mo.to(630, 470)
    sleep(6.0)
    shot("pxstein-title", window="Pixelstein 3D", pad=10, settle=False)

    g.start()                           # Space: a new game, GET READY, play
    g.god(True)                         # nothing ends the run early
    sleep(1.0)

    g.scene("a")                        # the first corridor, in the window
    sleep(4.0)
    shot("pxstein-window", window="Pixelstein 3D", pad=10, settle=False)
    _menu_shot(ui, shot, "Detail", "pxstein-menu-detail")

    m.type_text("f")                    # full screen
    sleep(8.0)
    g.scene("a")                        # the first corridor again
    sleep(4.0)
    _fullshot(ui, "pxstein-full")
    g.scene("c")                        # in front of the first guards
    sleep(3.0)
    m.key("ControlLeft", down=True, up=False)       # fire
    sleep(0.4)
    m.key("ControlLeft", down=False, up=True)
    sleep(0.6)
    _fullshot(ui, "pxstein-fight")
    sleep(2.0)

    # beside the elevator switch, facing it: Space ends the floor
    m.pause()
    g._mark()
    g.eye_poke(3 * 256 + 128, 22 * 256 + 128, 2048)
    g.pcell_poke(3 * 256 + 128, 22 * 256 + 128)
    g.force_all_poke()
    m.run()
    sleep(2.0)
    m.key("Space", down=True, up=False)
    sleep(0.4)
    m.key("Space", down=False, up=True)
    state("done", limit=30.0)
    sleep(2.0)
    _fullshot(ui, "pxstein-done")
    pxslib.release_held(m, ("Space", "Enter"), "the card")

    m.key("Escape")                     # back to the desktop
    sleep(6.0)
