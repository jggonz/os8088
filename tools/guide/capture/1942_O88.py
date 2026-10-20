"""1942's screenshots, for tools/guide/manuals/1942_O88.md.

    python3 tools/guide/capture/run.py 1942_O88 --apps build/1942-360.img

The launcher is an ordinary window. The game is a full-screen 320x240
"Mode X" screen on VGA, and MartyPC's normal display aperture stops 400 rows
down in that mode, so the game shots are cut out of the card's debug aperture
(the whole raster) instead.
"""
import os

import os88marty

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "..", "build", "guide", "shots")


def _tap(m, name, secs=0.2):
    m.key(name, up=False)
    os88marty.guest_sleep(m, 0.08)
    m.key(name, down=False)
    os88marty.guest_sleep(m, secs)


def _full(ui, sid, rows=480):
    m = ui.m
    v = m.cmd(cmd="video")
    a0 = v["apertures"][0]
    dbg = [i for i, a in enumerate(v["apertures"]) if a.get("debug")][0]
    w, h, data = m.fbuf(aperture=dbg)
    x0, y0, cw = a0["x"], a0["y"], a0["w"]
    ch = min(rows, h - y0)
    crop = b"".join(data[((y0 + y) * w + x0) * 3:((y0 + y) * w + x0 + cw) * 3]
                    for y in range(ch))
    os88marty.write_png_rgb(os.path.join(OUT, sid + ".png"), cw, ch, crop)
    print("shot %s %dx%d" % (sid, cw, ch))


def _fly(m, secs):
    """Fire all the time and weave left and right."""
    m.key("Space", up=False)
    t = 0.0
    k = 0
    while t < secs:
        d = ("ArrowLeft", "ArrowRight")[k % 2]
        m.key(d, up=False)
        os88marty.guest_sleep(m, 1.2)
        m.key(d, down=False)
        os88marty.guest_sleep(m, 0.6)
        t += 1.8
        k += 1
    m.key("Space", down=False)
    os88marty.guest_sleep(m, 0.1)


def capture(ui, shot):
    m = ui.m
    ui.path("B:/1942.O88")
    ui.mo.to(630, 470)
    os88marty.guest_sleep(m, 15.0)
    shot("1942-launcher", window="1942", pad=8)

    _tap(m, "KeyH")
    os88marty.guest_sleep(m, 2.0)
    shot("1942-help", window="1942", pad=8)
    _tap(m, "KeyH")
    os88marty.guest_sleep(m, 1.0)

    # One player, take off.
    _tap(m, "Enter")
    os88marty.guest_sleep(m, 20.0)
    _fly(m, 25.0)
    _full(ui, "1942-play")
    _fly(m, 20.0)
    _tap(m, "KeyP")
    os88marty.guest_sleep(m, 1.0)
    _full(ui, "1942-paused")
    _tap(m, "KeyP")

    # Back to the desktop: the launcher offers to resume.
    _tap(m, "Escape")
    os88marty.guest_sleep(m, 10.0)
    shot("1942-resume", window="1942", pad=8)
