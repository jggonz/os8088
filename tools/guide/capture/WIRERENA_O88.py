"""Wire Arena's screenshots, for tools/guide/manuals/WIRERENA_O88.md.

    python3 tools/guide/capture/run.py WIRERENA_O88 --apps build/games2-360.img

One boot on the guide's VGA XT. The setup window and its lists are reached
through the game's own keys and clicks; a match is started as the setup's
Play does, and for the pictures the player is stood somewhere with a view
and one bot is held still in front of it (tests/walink.py's freeze: a leg
too long to end) - everything on the screen is the game drawing it. The
game's symbols are tests/walift.py's amap(), refused unless the map is the
build's own, so `make wirearenadisk` first.
"""
import os
import struct
import sys

import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "..", "tests"))
import walift  # noqa: E402

F = 4772727
TICK = F // 30
FS = 56                                 # a figure record (waconst.inc)
TEXT = (0, 0, 720, 400)                 # a VGA text mode's 400 lines


class Game(object):
    def __init__(self, ui, w):
        self.ui, self.m = ui, ui.m
        self.sym = walift.amap()
        self.w = w
        self.p = os88marty.Pkg(ui.m, w)

    def put(self, name, v, n=2):
        self.p.write(self.sym[name], v.to_bytes(n, "little", signed=v < 0))

    def byte(self, name):
        return self.p.byte(self.sym[name])

    def ticks(self, n):
        self.m.advance(cycles=TICK * n)

    def frames(self, n=2):
        """n frames presented after now (a mode set draws nothing until
        its first)"""
        self.m.go()
        for _ in range(n):
            f0 = self.byte("wa_frame")
            os88marty.until(self.m, lambda _: self.byte("wa_frame") != f0,
                            "a frame", guest=5)

    def match(self, arena, w40=True, mode=None):
        if mode is not None:
            self.put("wa_lkgmode", mode, 1)
        walift.start(self.ui, self.w, self.sym, arena=arena, w40=w40)
        f0 = self.byte("wa_frame")
        os88marty.until(self.m, lambda _: self.byte("wa_frame") != f0,
                        "the match's first frames", guest=20)
        self.put("wa_bdet", 1, 1)       # (the bots hold their fire)

    def leave(self):
        self.m.key("Escape")
        self.m.go()
        os88marty.until(self.m, lambda _: self.byte("wa_quit") == 1,
                        "the match over", guest=5)
        self.m.advance(cycles=F * 2)
        self.m.go()

    def pose(self, x, y, ang):
        self.m.pause()
        for n, v in (("wa_px", x), ("wa_py", y), ("wa_pxf", 0),
                     ("wa_pyf", 0), ("wa_ang", ang << 6), ("wa_pitch", 0),
                     ("wa_jz", 0), ("wa_jv", 0), ("wa_fpx", -1)):
            self.put(n, v)

    def bot(self, i, x, y, ang):
        base = self.sym["wa_figs"] + i * FS
        head = self.p.word(base + 20)
        self.p.write(base + 0, struct.pack("<hh", x, y))
        self.p.write(base + 4, struct.pack("<H", ang))
        self.p.write(base + 8, struct.pack("<H", 30000))
        self.p.write(base + 36, struct.pack("<H", head | 0x8000))
        self.p.write(base + 38, b"\0\0\0\0")


def capture(ui, shot):
    m = ui.m
    w = ui.path("B:/WIRERENA.O88")
    g = Game(ui, w)
    ui.mo.to(4, 190)
    shot("wirerena-setup", window="Wire Arena", pad=4)

    # the Arena list, dropped by a click on its box (wasetup.py's place)
    wn = [x for x in ui.windows() if x.title == "Wire Arena"][0]
    ox, oy = wn.x + 1, wn.y + 18
    dr = g.sym["wa_sdr_a"]
    gy = g.p.word(dr + 2) - oy
    ui.mo.click(ox + 120, oy + gy + 8)
    os88marty.until(m, lambda _: g.p.byte(dr + 16) == 1,
                    "the Arena list open", guest=5)
    ui.mo.to(4, 190)
    shot("wirerena-arenas", rect=(wn.x - 4, wn.y - 4, wn.w + 10, wn.h + 70))
    m.key("Escape")
    ui.settle()

    # the Rotation window, team deathmatch's arenas: the focus to Mode
    # (Arena, Columns, Bots, Skill, Play, Mode - the address field is no
    # stop while Play is offline)
    for _ in range(5):
        m.key("ArrowDown")
    m.key("ArrowRight")                 # (team deathmatch)
    m.key("KeyR")
    ui.mo.to(4, 190)
    shot("wirerena-rotation", rect=(wn.x - 4, wn.y - 4, 300, wn.h + 8))
    m.key("Escape")
    m.key("ArrowLeft")                  # (Mode back to free-for-all, the
    for _ in range(5):                  # focus back to Arena)
        m.key("ArrowUp")
    ui.settle()

    # a match at 40 columns on the Heights, a bot in front
    g.match(arena=2)
    g.pose(1024, 300, 256)
    g.bot(0, 1060, 760, 768)
    g.ticks(8)
    shot("wirerena-match", rect=TEXT, settle=False)
    m.key("Tab", up=False)              # the scoreboard and the minimap
    g.ticks(6)
    shot("wirerena-scores", rect=TEXT, settle=False)
    m.key("Tab", down=False)
    g.ticks(2)
    m.key("F4")                         # a setting says what it is now
    g.ticks(4)
    shot("wirerena-detail", rect=TEXT, settle=False)
    m.key("Equal")                      # 80 columns
    g.frames(3)
    shot("wirerena-wide", rect=TEXT, settle=False)
    g.leave()

    # capture the flag on the Twin: toward blue's flag at its base
    g.match(arena=4, mode=1)
    g.ticks(30)                         # (the side spawn, first)
    g.pose(1856, 1480, 768)              # (north of blue's base, facing it)
    g.ticks(10)
    shot("wirerena-ctf", rect=TEXT, settle=False)
    g.leave()
