"""Tank Attack's screenshots, for tools/guide/manuals/TANK_O88.md.

    python3 tools/guide/capture/run.py TANK_O88 --apps build/games360.img

TANK.O88 is at the root of the games disk. The game is played for real: the
tank turns until the radar's ENEMY IN RANGE line lights and fires. To reach
the end of a game in one run, the score and the tanks left are set in the
game's own memory before the last hit, so the high-score prompt comes up.
"""
import os
import struct
import sys
import zlib

import os88geom as geom
import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "..", "tests"))
import dispapps  # noqa: E402


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
    ui.path("B:/TANK.O88")
    w = ui.window("Tank Attack")
    seg = struct.unpack("<H", ui.m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE
                                        + geom.W_SEG, 2))[0]
    base = (seg << 4) + struct.unpack("<H", ui.m.readseg(seg, 8, 2))[0]

    def addr(n):
        return base + dispapps.bss_off("tank", n)

    def rb(n):
        return ui.m.read(addr(n), 1)[0]

    def sleep(s):
        os88marty.guest_sleep(ui.m, s)

    ui.mo.to(630, 470)
    sleep(8.0)                                  # the logo draws itself in
    shot("tank-window", window="Tank Attack", pad=10, settle=False)

    ui.m.type_text("f")                         # into the game, full screen
    sleep(4.0)

    # turn right until a tank is in the sight's range
    for _ in range(60):
        if rb("tk_inrange"):
            break
        ui.m.key("KeyD", down=True, up=False)
        sleep(0.4)
        ui.m.key("KeyD", down=False, up=True)
        sleep(0.3)
    sleep(0.5)
    _fullshot(ui, "tank-range")
    ui.m.key("Space")                           # fire
    sleep(0.35)
    _fullshot(ui, "tank-fire")
    for _ in range(6):                          # a few more, and a kill if
        sleep(1.5)                              # the aim was good
        ui.m.key("Space")

    # the last tank: one more hit ends the game, with a score for the table
    ui.m.write(addr("tk_lives"), b"\x00")
    sc = struct.unpack("<I", ui.m.read(addr("tk_score"), 4))[0]
    if sc < 4000:
        ui.m.write(addr("tk_score"), struct.pack("<I", 4200))
    for _ in range(240):
        if rb("tk_dead"):
            break
        sleep(0.5)
    sleep(0.3)
    _fullshot(ui, "tank-hit")
    for _ in range(120):
        if rb("tk_over") == 2:
            break
        sleep(0.5)
    sleep(1.0)
    ui.m.type_text("abc")
    sleep(1.0)
    ui.m.key("Enter")
    sleep(4.0)
    _fullshot(ui, "tank-over")

    ui.m.type_text("f")                         # back to the desktop
    sleep(6.0)
    shot("tank-scores", window="Tank Attack", pad=10, settle=False)
