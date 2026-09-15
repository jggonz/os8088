#!/usr/bin/env python3
"""Run compiled text and graphics BASIC packages in MartyPC."""

import argparse
import os
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import dispcp
import os88fixture
import os88geom
import os88marty
import os88mouse
import os88sym
from os88map import Syms
from speedybasic import capture, open_drive, open_named, package_segment


SYSTEM = "build/os8088-360.img"
APPS = "build/sbcompiled120.img"
MACHINE = "os8088_5150_herc_gla_144"


def wait_text(marty, address, wanted, frames=900):
    for _ in range(frames):
        if bytes(marty.read(address, len(wanted))) == wanted:
            return True
        marty.advance(frames=1)
    return False


def word(marty, address):
    return int.from_bytes(marty.read(address, 2), "little")


def enter_fullscreen(marty, mouse, linear, app, animated=False):
    ui = dispcp._ui(marty, mouse, None, linear)
    ui.raise_window(app)
    marty.run()
    marty.ctrl("KeyF")
    for _ in range(300):
        if word(marty, linear("wm_fs")):
            break
        marty.advance(frames=1)
    else:
        raise RuntimeError("compiled package did not enter full screen")
    marty.run()
    if animated:
        marty.advance(frames=30)
    else:
        os88marty.settle(marty, limit=180)


def run(capture_dir=None):
    os88fixture.need(SYSTEM)
    os88fixture.need(APPS)
    symbols = Syms("apps/speedybasic/compiler/compiled.asm",
                   "build/sbhello.bin", ["apps", "build"])
    linear = os88sym.linear

    with os88marty.launch(SYSTEM, apps=APPS, machine=MACHINE) as marty:
        mouse = os88mouse.Mouse(marty=marty)
        os88marty.no_saver(marty)
        open_drive(marty, mouse, linear)
        slot = dispcp.win_list(marty, linear)[-1]
        wx, wy = dispcp.win_rect(marty, linear, slot)[:2]
        open_named(marty, mouse, linear, wx, wy, "SBHELLO.O88")
        marty.run()
        os88marty.settle(marty, limit=300)

        app = next((w for w in os88geom.windows(marty, linear)
                    if w.visible and w.title == "SBHELLO"), None)
        if app is None:
            raise RuntimeError("compiled SBHELLO window did not open")
        segment = package_segment(marty, linear, app.i)
        chars = (segment << 4) + symbols.sym("_sbs_ch")
        if not wait_text(marty, chars, b"================================"):
            raise RuntimeError("compiled PRINT output did not reach the retained screen")
        capture(marty, capture_dir, "speedybasic-compiled-windowed.png")

        enter_fullscreen(marty, mouse, linear, app)
        capture(marty, capture_dir, "speedybasic-compiled-fullscreen.png")

    stars_symbols = Syms(
        "apps/speedybasic/compiler/compiled.asm", "build/sbstars.bin",
        ["apps", "build"],
        defines=("SB_COMPILED_NAME='SBSTARS'",
                 'SB_COMPILED_GEN="sbstars.gen.asm"'))
    with os88marty.launch(SYSTEM, apps=APPS, machine=MACHINE) as marty:
        mouse = os88mouse.Mouse(marty=marty)
        os88marty.no_saver(marty)
        open_drive(marty, mouse, linear)
        slot = dispcp.win_list(marty, linear)[-1]
        wx, wy = dispcp.win_rect(marty, linear, slot)[:2]
        open_named(marty, mouse, linear, wx, wy, "SBSTARS.O88")
        marty.run()
        marty.advance(frames=30)

        app = next((w for w in os88geom.windows(marty, linear)
                    if w.visible and w.title == "SBSTARS"), None)
        if app is None:
            raise RuntimeError("compiled SBSTARS window did not open")
        segment = package_segment(marty, linear, app.i)
        base = segment << 4
        mode = base + stars_symbols.sym("_sbs_mode")
        pokes = base + stars_symbols.sym("_sbs_vpoke_count")
        pc = base + stars_symbols.sym("_sbp_pc")
        gseg = base + stars_symbols.sym("_sbs_gseg")
        pseg = base + stars_symbols.sym("_sbs_pseg")
        regax = base + stars_symbols.sym("_rt_reg_ax")
        screen = base + stars_symbols.sym("_sbs_screen")
        for _ in range(1800):
            if word(marty, mode) == 1 and word(marty, pokes) >= 50:
                break
            marty.advance(frames=1)
        else:
            capture(marty, capture_dir,
                    "speedybasic-compiled-stars-failure.png")
            raise RuntimeError(
                "compiled STARS3D did not draw its framebuffer "
                "(mode=%d, screen=%d, ax=%#x, pokes=%d, pc=%d, "
                "gseg=%#x, planes=%#x/%#x)" %
                (word(marty, mode), word(marty, screen), word(marty, regax),
                 word(marty, pokes), word(marty, pc), word(marty, gseg),
                 word(marty, pseg), word(marty, pseg + 2)))
        marty.run()
        marty.advance(frames=30)
        capture(marty, capture_dir, "speedybasic-compiled-stars-windowed.png")
        before_full_pokes = word(marty, pokes)
        enter_fullscreen(marty, mouse, linear, app, animated=True)
        for _ in range(1800):
            if ((word(marty, pokes) - before_full_pokes) & 0xffff) >= 400:
                break
            marty.advance(frames=1)
        else:
            raise RuntimeError("compiled STARS3D stopped after fullscreen")
        marty.advance(frames=30)
        capture(marty, capture_dir, "speedybasic-compiled-stars-fullscreen.png")

    print("speedybasic compiled: HELLO, STARS3D and fullscreen - PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture-dir")
    run(parser.parse_args().capture_dir)
