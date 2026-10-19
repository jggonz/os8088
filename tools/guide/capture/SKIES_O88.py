"""Clear Skies' screenshots, for tools/guide/manuals/SKIES_O88.md.

    python3 tools/guide/capture/run.py SKIES_O88 --apps build/games360.img

SKIES.O88 is at the root of the games disk. A real flight in the Cessna from
Paris-Issy: full throttle, the stick back at take-off speed, a climb
towards the Eiffel Tower and the map. The waits read the simulator's own
state (the same names tests/skies.py reads).
"""
import os
import struct
import sys
import zlib

import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "..", "tests"))
import dispapps  # noqa: E402
import os88geom as geom  # noqa: E402

CS_ST_AIR = 1
VROT = 28 * 128                     # the Cessna's take-off speed


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


def capture(ui, shot):
    ui.path("B:/SKIES.O88")
    w = ui.window("Clear Skies")
    seg = struct.unpack("<H", ui.m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE
                                        + geom.W_SEG, 2))[0]
    base = (seg << 4) + struct.unpack("<H", ui.m.readseg(seg, 8, 2))[0]

    def addr(n):
        return base + dispapps.bss_off("skies", n)

    def rb(n):
        return ui.m.read(addr(n), 1)[0]

    def rw(n):
        return struct.unpack("<H", ui.m.read(addr(n), 2))[0]

    def alt():
        v = struct.unpack("<i", ui.m.read(addr("cs_py"), 4))[0]
        return v // 256

    def sleep(s):
        os88marty.guest_sleep(ui.m, s)

    def until(pred, secs, step=0.25):
        for _ in range(int(secs / step)):
            if pred():
                return True
            sleep(step)
        return pred()

    ui.mo.to(630, 470)
    ui.settle(limit=60.0)
    shot("skies-window", window="Clear Skies", pad=10)

    ui.menu_pick("Flight", "Settings")
    ui.mo.to(630, 470)
    ui.settle(limit=60.0)
    shot("skies-settings", window="Clear Skies", pad=10)
    ui.m.key("Escape")                  # any key goes back
    ui.settle(limit=60.0)
    ui.menu_pick("Flight", "Instructions")
    ui.mo.to(630, 470)
    ui.settle(limit=60.0)
    shot("skies-instructions", window="Clear Skies", pad=10)
    ui.m.key("Escape")
    ui.settle(limit=60.0)

    ui.m.type_text("f")                 # fly: full screen
    sleep(5.0)
    _fullshot(ui, "skies-runway")

    ui.m.type_text("9")                 # full throttle
    until(lambda: rw("cs_spd") >= VROT, 90.0)
    ui.m.key("ArrowDown", down=True, up=False)      # the stick back
    until(lambda: rb("cs_state") == CS_ST_AIR, 20.0)
    sleep(0.3)
    ui.m.key("ArrowDown", down=False, up=True)
    until(lambda: alt() >= 25, 60.0)    # a steady climb towards the tower
    sleep(1.0)
    _fullshot(ui, "skies-climb")
    ui.m.type_text("7")                 # cruise power
    until(lambda: alt() >= 60, 60.0)
    ui.m.key("ArrowUp")                 # a nudge towards level
    sleep(12.0)
    _fullshot(ui, "skies-flying")

    ui.m.type_text("m")                 # the map: near
    sleep(3.0)
    _fullshot(ui, "skies-map")
    ui.m.type_text("mm")                # far, then off
    sleep(2.0)

    ui.m.key("Escape")                  # back to the window
    sleep(4.0)
