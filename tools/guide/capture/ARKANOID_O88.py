"""Arkanoid's screenshots, for tools/guide/manuals/ARKANOID_O88.md.

The game is played by a small autopilot: the ball's position is read out of
the game's own memory and the paddle is put under it, so the rally lasts long
enough to break some bricks and drop a capsule. The last shots let the ball
fall to reach the game-over banner.
"""
import os
import struct
import sys

import os88geom as geom
import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "..", "tests"))
import dispapps  # noqa: E402

M_READY, M_PLAY, M_DEAD, M_OVER = 0, 1, 2, 3


def capture(ui, shot):
    ui.path("B:/GAMES/ARKANOID.O88")
    w = ui.window("Arkanoid")
    seg = struct.unpack("<H", ui.m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE
                                        + geom.W_SEG, 2))[0]
    base = (seg << 4) + dispapps.img_size("arkanoid")
    off = {n: dispapps.bss_off("arkanoid", n) for n in
           ("ark_bx", "ark_by", "ark_bvy", "ark_px", "ark_pw", "ark_cwid",
            "ark_rail", "ark_bsz", "ark_mode", "ark_padwipe", "ark_pukind",
            "ark_pux", "ark_score", "ark_lives")}

    def rw(n):
        return struct.unpack("<h", ui.m.read(base + off[n], 2))[0]

    def rb(n, i=0):
        return ui.m.read(base + off[n] + i, 1)[0]

    def steer(k=0):
        """Put the paddle under the ball (or under a falling capsule while the
        ball is on its way up)."""
        pw, cw, rail = rw("ark_pw"), rw("ark_cwid"), rw("ark_rail")
        tx = rw("ark_bx") + rw("ark_bsz") // 2
        if rw("ark_bvy") < 0:
            for i in range(3):
                if rb("ark_pukind", i):
                    tx = struct.unpack("<h", ui.m.read(base + off["ark_pux"] + 2 * i, 2))[0] + 6
                    break
        # hit a little off centre, alternating, so the ball does not go
        # straight up and down
        px = tx - pw // 2 + (pw // 4 if k % 2 else -pw // 4)
        px = max(rail, min(cw - rail - pw, px))
        ui.m.write(base + off["ark_px"], struct.pack("<h", px))
        ui.m.write(base + off["ark_padwipe"], b"\x01")

    def play(secs, follow=True):
        n = int(secs / 0.04)
        for k in range(n):
            if follow:
                steer(k // 25)
            os88marty.guest_sleep(ui.m, 0.04)
            if rb("ark_mode") in (M_READY,):
                ui.m.key("Space")

    ui.mo.to(w.x + w.w + 60, w.y + 60)
    shot("arkanoid-ready", window="Arkanoid", pad=10)

    ui.m.key("Space")
    # Play until a capsule is falling and some bricks are gone.
    for _ in range(40):
        play(1.0)
        if rw("ark_score") >= 150 and any(rb("ark_pukind", i) for i in range(3)) \
                and rw("ark_by") > 60:
            break
    shot("arkanoid-window", window="Arkanoid", pad=10, settle=False)

    play(4.0)
    ui.m.key("KeyP")
    os88marty.guest_sleep(ui.m, 0.3)
    shot("arkanoid-pause", window="Arkanoid", pad=10)
    ui.m.key("Space")

    # Stop following and let every ball fall, serving each new one.
    for _ in range(120):
        os88marty.guest_sleep(ui.m, 0.25)
        mode = rb("ark_mode")
        if mode == M_OVER:
            break
        if mode == M_READY:
            ui.m.key("Space")
    os88marty.guest_sleep(ui.m, 0.5)
    shot("arkanoid-over", window="Arkanoid", pad=10)
