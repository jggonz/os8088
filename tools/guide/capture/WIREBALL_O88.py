"""WireBall's screenshots, for tools/guide/manuals/WIREBALL_O88.md.

    python3 tools/guide/capture/run.py WIREBALL_O88 --apps build/games360.img

The splash window and its controls page are ordinary window shots. The court
is a full-screen 320x240 "Mode X" screen on VGA, and MartyPC's display capture
crops the bottom 40 rows of that mode, so the court shots are cut out of the
card's debug aperture (the whole raster) instead. In Practice a small
autopilot keeps the left paddle under the ball, so the shot is a rally.
"""
import os
import sys

import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "..", "tests"))
import dispapps  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "..", "build", "guide", "shots")


def _tap(m, name, secs=0.15):
    m.key(name, up=False)
    os88marty.guest_sleep(m, 0.06)
    m.key(name, down=False)
    os88marty.guest_sleep(m, secs)


def _hold(m, name, secs):
    m.key(name, up=False)
    os88marty.guest_sleep(m, secs)
    m.key(name, down=False)
    os88marty.guest_sleep(m, 0.06)


def _full(ui, sid, rows=480):
    """The whole screen. The card's normal aperture stops 400 rows down, so
    the debug aperture (the whole raster) is cropped to the picture instead."""
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


def _autopilot(ui, secs, seg, sy):
    """Keep the left paddle under the ball, hitting it a little off centre."""
    m = ui.m
    n = int(secs / 0.03)
    for k in range(n):
        by = int.from_bytes(m.readseg(seg, sy["wb_bally"], 2), "little")
        top = by - 7 + (3 if (k // 40) % 2 else -3)
        m.write((seg << 4) + sy["wb_left"], bytes([max(0, min(64, top))]))
        os88marty.guest_sleep(m, 0.03)


def capture(ui, shot):
    m = ui.m
    ui.path("B:/WIREBALL.O88")
    seg = dispapps.pkg_seg(m, 0)[1]
    sy = dispapps._map("wireball")
    ui.mo.to(620, 460)
    os88marty.guest_sleep(m, 3.0)
    shot("wireball-splash", window="WireBall", pad=8)

    _tap(m, "KeyH")
    os88marty.guest_sleep(m, 1.5)
    shot("wireball-help", window="WireBall", pad=8)
    _tap(m, "KeyH")
    os88marty.guest_sleep(m, 1.0)

    # Serial Match with nobody on the other end: the court waits, searching.
    _tap(m, "KeyS")
    os88marty.guest_sleep(m, 2.5)
    _full(ui, "wireball-search")
    _tap(m, "Escape")
    os88marty.guest_sleep(m, 2.0)
    ui.settle()

    # Practice against the computer, the autopilot holding a rally.
    _tap(m, "KeyL")
    os88marty.guest_sleep(m, 3.0)
    _autopilot(ui, 12.0, seg, sy)
    _full(ui, "wireball-practice")
    _tap(m, "Escape")
    os88marty.guest_sleep(m, 2.0)
