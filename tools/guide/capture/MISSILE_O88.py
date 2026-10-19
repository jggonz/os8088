"""Missile Command's screenshots, for tools/guide/manuals/MISSILE_O88.md.

A small autopilot plays: the incoming missiles are read out of the game's own
memory and a shot is put into the game's own fire queue (what a click puts
there) where each one will be, so the shots show trails, counter-missiles
and bursts. It plays wave 1 windowed through
to the bonus tally, then on in Full Screen.
(No Mode X shot: on MartyPC's VGA its frame came back 640x400 with the
bottom of the screen missing.)
"""
import os
import struct
import sys

import os88geom as geom
import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "..", "tests"))
import dispapps  # noqa: E402

M_READY, M_PLAY, M_ENDA, M_ENDC, M_OVER, M_PAUSE = range(6)
NICBM = 8


def capture(ui, shot):
    ui.path("B:/GAMES/MISSILE.O88")
    w = ui.window("Missile Command")
    seg = struct.unpack("<H", ui.m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE
                                        + geom.W_SEG, 2))[0]
    base = (seg << 4) + dispapps.img_size("missile")
    off = {n: dispapps.bss_off("missile", n) for n in
           ("mc_ox", "mc_oy", "mc_cw", "mc_ch", "mc_mode", "mc_ia",
            "mc_ix16", "mc_iy16", "mc_ivx", "mc_ivy", "mc_fire", "mc_firex",
            "mc_firey", "mc_fireb", "mc_basex", "mc_balive", "mc_bmis",
            "mc_groundy", "mc_objh")}

    def rw(n, i=0):
        return struct.unpack("<h", ui.m.read(base + off[n] + 2 * i, 2))[0]

    def rb(n, i=0):
        return ui.m.read(base + off[n] + i, 1)[0]

    def ww(n, v):
        ui.m.write(base + off[n], struct.pack("<h", v))

    def intercept(i):
        """Where to burst to meet ICBM i: its position after the time a
        counter-missile from the nearest loaded base takes to get there."""
        x, y = rw("mc_ix16", i) / 16, rw("mc_iy16", i) / 16
        vx, vy = rw("mc_ivx", i) / 16, rw("mc_ivy", i) / 16
        bases = [rw("mc_basex", b) for b in range(3)
                 if rb("mc_balive", b) and rb("mc_bmis", b)]
        if not bases:
            return None
        by = rw("mc_groundy") - rw("mc_objh")
        t = 0
        for _ in range(4):
            px, py = x + vx * t, y + vy * t
            bx = min(bases, key=lambda b: abs(b - px))
            t = (((px - bx) ** 2 + (py - by) ** 2) ** 0.5) / 7.5 + 2
        px, py = int(x + vx * t), int(y + vy * t)
        if not (4 < px < rw("mc_cw") - 4 and 16 < py < by - 12):
            return None
        return px, py, t

    engaged = {}

    def play(secs, until=None):
        """Fire one counter-missile at a time at each incoming missile that
        has none on its way, straight into the game's own fire queue."""
        step = 0.06
        for k in range(int(secs / step)):
            if until and until():
                return
            if rw("mc_fire") == 0 and rb("mc_mode") == M_PLAY:
                for i in range(NICBM):
                    if rb("mc_ia", i) not in (1, 2) or engaged.get(i, -1) > k:
                        continue
                    hit = intercept(i)
                    if hit is None:
                        continue
                    ww("mc_firex", hit[0])
                    ww("mc_firey", hit[1])
                    ui.m.write(base + off["mc_fireb"], b"\xff")
                    ww("mc_fire", 1)
                    engaged[i] = k + int(hit[2] / 18.2 / step) + 12
                    break
            os88marty.guest_sleep(ui.m, step)

    # Wave 1, windowed.
    play(9)
    shot("missile-window", window="Missile Command", pad=10, settle=False)

    play(90, until=lambda: rb("mc_mode") in (M_ENDA, M_ENDC, M_OVER))
    os88marty.guest_sleep(ui.m, 0.8)
    shot("missile-bonus", window="Missile Command", pad=10, settle=False)

    # Wave 2, in Full Screen.
    for _ in range(100):
        if rb("mc_mode") == M_PLAY:
            break
        os88marty.guest_sleep(ui.m, 0.2)
    ui.m.key("KeyF")
    os88marty.guest_sleep(ui.m, 1.0)
    play(10)
    shot("missile-full", settle=False)
    ui.m.key("Escape")
    os88marty.guest_sleep(ui.m, 1.0)
