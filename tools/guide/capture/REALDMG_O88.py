"""RealDMG's screenshots, for tools/guide/manuals/REALDMG_O88.md.

    python3 tools/guide/capture/run.py REALDMG_O88 --apps build/realdmg360.img

RealDMG has a disk of its own: REALDMG.O88, GBDEMO.GB and README.TXT in a
REALDMG folder. Every shot runs GBDEMO.GB, the project's own demo; nothing
else is ever loaded. Double-clicking the demo opens the program with it,
the window's notice comes first (the game's frame counter is still 0 while
it is up), then the demo runs. The menus, Machine > Controls... and full
screen follow.
"""
import os
import re
import struct
import sys

import os88marty
import os88ui

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tests"))
import gbwindow  # noqa: E402

OUT = os.path.join(ROOT, "build", "guide", "shots")

TITLE = "RealDMG"
AWAY = (620, 300)                           # the pointer, off the window


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


def wrect(ui, title, pad=10):
    """A window's rect plus `pad` - matched by its exact title, because the
    Disk window of the REALDMG folder answers to "RealDMG" too."""
    w = [w for w in ui.windows() if w.title == title][0]
    x, y, ww, hh = ui._rect(w.i)
    return (x - pad, y - pad, ww + 2 * pad + 2, hh + 2 * pad + 2)


def ntc_off():
    """Where the package keeps "the notice is up": read off the listing, out
    of the instruction that raises it."""
    for line in open(os.path.join(ROOT, "build", "realdmg.lst"), errors="replace"):
        r = re.search(r"C606\[([0-9A-F]{4})\]01\s+mov byte \[rw_ntc\], 1", line)
        if r:
            v = r.group(1)
            return int(v[2:] + v[:2], 16)
    raise RuntimeError("rw_ntc not in the listing")


def pkgseg(ui, title):
    """The package's segment: the one its window's title is read out of."""
    w = [w for w in ui.windows() if w.title == title][0]
    raw = ui.m.read(ui._S("wm_wins") + w.i * os88ui.geom.WIN_SIZE,
                    os88ui.geom.WIN_SIZE)
    return struct.unpack_from("<H", raw, os88ui.geom.W_SEG)[0]


def fullshot(ui, sid, pad=12):
    """Full screen on VGA is 320x200 with the CRT's display cut to the
    picture and the speed line under it, centred in the card's border. The
    frame buffer holds the display from its top left (the centring is the
    monitor's), so the shot is that 160x154 display with a band of the
    border's own colour round it, each pixel drawn 2x2."""
    W, H, data = ui.m.fbuf(0)
    sx, sy = max(1, W // 320), max(1, H // 200)

    def px(x, y):
        o = ((y * sy) * W + x * sx) * 3
        return bytes(data[o:o + 3])
    band = [px(x, y) for y in range(146, 154) for x in range(160)]
    ground = max(set(band), key=band.count)
    ow, oh = 160 + 2 * pad, 154 + 2 * pad
    rows = []
    for y in range(oh):
        row = bytearray()
        for x in range(ow):
            mx, my = x - pad, y - pad
            c = px(mx, my) if 0 <= mx < 160 and 0 <= my < 154 else ground
            row += c * 2
        rows.append(bytes(row) * 2)
    os88marty.write_png_rgb(os.path.join(OUT, sid + ".png"), ow * 2, oh * 2,
                            b"".join(rows))
    print("shot %s %dx%d (frame buffer %dx%d)" % (sid, ow * 2, oh * 2, W, H))
    if os.environ.get("RDG_RAW"):
        os88marty.write_png_rgb(os.environ["RDG_RAW"], W, H, data)


def capture(ui, shot):
    m = ui.m
    ntc = ntc_off()
    ui.path("B:/REALDMG/GBDEMO.GB")
    ui.mo.to(*AWAY)

    # The notice: up from the moment the game is loaded until its first
    # frame, 1.5 seconds later. One byte of the package's says so.
    seg = pkgseg(ui, TITLE)
    up = lambda: m.read(seg * 16 + ntc, 1)[0]      # noqa: E731
    noticed = False
    for _ in range(2000):
        if up():
            os88marty.guest_sleep(m, 0.3)           # (its paint follows)
            if up():
                shot("realdmg-notice", rect=wrect(ui, TITLE), settle=False)
                noticed = up() != 0
            break
        os88marty.guest_sleep(m, 0.05)
    print("notice shot:", noticed)

    os88marty.guest_sleep(m, 12)            # the ball bouncing, the clock on
    shot("realdmg-window", rect=wrect(ui, TITLE), settle=False)

    for name, sid in (("Machine", "realdmg-machine"), ("CPU", "realdmg-cpu"),
                      ("Save State", "realdmg-savestate")):
        x0 = drop(ui, name)
        shot(sid, rect=(max(0, x0 - 8), 0, 226, 206), settle=False)
        undrop(ui)

    ui.menu_pick("Machine", "Controls...")
    ui.mo.to(*AWAY)
    os88marty.guest_sleep(m, 3)
    shot("realdmg-controls", rect=wrect(ui, "RealDMG Controls"), settle=False)
    m.key("Escape")                         # put the Controls window away
    os88marty.guest_sleep(m, 2)

    ui.menu_pick("Machine", "Full Screen")
    ui.mo.to(*AWAY)
    os88marty.guest_sleep(m, 3)
    m.key("F1", up=False)                   # the speed line: held, so the
    os88marty.guest_sleep(m, 1)             #   full-screen loop sees it
    m.key("F1", down=False)
    os88marty.guest_sleep(m, 6)
    fullshot(ui, "realdmg-fullscreen")
    m.key("Escape", up=False)               # Esc leaves full screen
    os88marty.guest_sleep(m, 1)
    m.key("Escape", down=False)
    os88marty.guest_sleep(m, 3)
