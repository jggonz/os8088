#!/usr/bin/env python3
"""Compile BASIC inside Speedy BASIC, then launch the resulting O88 package.

    make build/speedybasic.img && python3 tests/speedybasic_inos_compile.py

This covers the user-facing path: the Run menu, standard Save dialog, guest
compiler, FAT write, package loader, and the generated program's first run.
"""

import argparse
import os
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import dispcp                                                # noqa: E402
import os88fixture                                           # noqa: E402
import os88geom                                              # noqa: E402
import os88marty                                             # noqa: E402
import os88mouse                                             # noqa: E402
import os88sym                                               # noqa: E402
from os88map import Syms                                     # noqa: E402
from speedybasic import (capture, open_app_file, open_drive, open_named,
                         package_segment)                    # noqa: E402


SYSTEM = "build/os8088-360.img"
APPS = "build/speedybasic.img"
MACHINE = "os8088_5150_herc_gla_144"
OUTPUT = "UNTITLED.O88"
SB_BUILD_DONE = 3


def cstring(marty, address, capacity=64):
    return bytes(marty.read(address, capacity)).split(b"\0", 1)[0]


def wait_byte(marty, address, wanted, frames=1800):
    for _ in range(frames):
        if marty.read(address, 1)[0] == wanted:
            return True
        marty.advance(frames=1)
    return False


def wait_text(marty, address, wanted, frames=1800):
    for _ in range(frames):
        if bytes(marty.read(address, len(wanted))) == wanted:
            return True
        marty.advance(frames=1)
    return False


def wait_word(marty, address, wanted, frames=1800):
    for _ in range(frames):
        if int.from_bytes(marty.read(address, 2), "little") == wanted:
            return True
        marty.advance(frames=1)
    return False


def window(marty, linear, title):
    return next((w for w in os88geom.windows(marty, linear)
                 if w.visible and w.title == title), None)


def wait_window(marty, linear, title, frames=900):
    for _ in range(frames):
        found = window(marty, linear, title)
        if found is not None:
            return found
        marty.advance(frames=1)
    return None


def run(capture_dir):
    os88fixture.need(SYSTEM)
    os88fixture.need(APPS)
    linear = os88sym.linear
    speedy = Syms("apps/speedybasic/speedybasic.asm",
                  "build/speedybasic.bin", ["apps", "build"])
    compiled = Syms("apps/speedybasic/compiler/template.asm",
                    "build/speedycc.bin", ["apps", "build"])

    with os88marty.launch(SYSTEM, apps=APPS, machine=MACHINE) as marty:
        mouse = os88mouse.Mouse(marty=marty)
        os88marty.no_saver(marty)
        open_drive(marty, mouse, linear)
        disk = dispcp._ui(marty, mouse, None, linear).front()
        open_named(marty, mouse, linear, disk.x, disk.y, "SPEEDY")
        disk = dispcp._ui(marty, mouse, None, linear).front()
        disk_slot = disk.i
        try:
            open_named(marty, mouse, linear, disk.x, disk.y, "SPEEDYBA.O88")
        except RuntimeError as error:
            if ("package ran and put up no window" not in str(error) or
                    wait_window(marty, linear, "Speedy Basic", 3600) is None):
                raise
            print("  Speedy Basic opened after the package helper timeout")
        marty.run()
        os88marty.settle(marty, limit=240)

        app = window(marty, linear, "Speedy Basic")
        if app is None:
            raise RuntimeError("Speedy Basic did not open")
        ui = dispcp._ui(marty, mouse, None, linear)
        ui.raise_window(app)
        segment = package_segment(marty, linear, app.i)
        build_state = (segment << 4) + speedy.sym("_sbu_build")

        marty.run()
        ui.menu_pick("Run", "Build Package...")
        dialog = wait_window(marty, linear, "Save As")
        if dialog is None:
            raise RuntimeError("Build Package did not open the Save As dialog")
        default_name = cstring(marty, linear("fdlg_name"), 16)
        if default_name != OUTPUT.encode("ascii"):
            raise RuntimeError("package default is %r, wanted %s" %
                               (default_name, OUTPUT))
        print("  Save As folder entries: %r" %
              ([name for name, _kind in dispcp.snapshot(marty, linear)],))
        marty.run()
        os88marty.settle(marty, limit=180)
        capture(marty, capture_dir, "speedybasic-compiler-save-dialog.png")

        marty.run()
        marty.key("Enter")
        if not wait_byte(marty, build_state, SB_BUILD_DONE, frames=3600):
            error = cstring(marty,
                            (segment << 4) + speedy.sym("_sbu_build_error"))
            state = marty.read(build_state, 1)[0]
            ferr = int.from_bytes(marty.read(
                (segment << 4) + speedy.sym("cc_ferr"), 2), "little")
            inst = marty.read(linear("wm_owner") + app.i, 1)[0]
            fdrv = marty.read(linear("inst_fdrv") + inst, 1)[0]
            fcwd = int.from_bytes(marty.read(
                linear("inst_fcwd") + inst * 2, 2), "little")
            raise RuntimeError(
                "in-OS compile did not finish (state=%d, error=%r, ferr=%d, "
                "instance=%d, drive=%d, cwd=%#x)" %
                (state, error, ferr, inst, fdrv, fcwd))
        marty.run()
        os88marty.settle(marty, limit=240)
        capture(marty, capture_dir, "speedybasic-compiler-built.png")

        # The first standard dialog opens at the volume's default folder
        # (the root on this disk), so leaving SPEEDY also refreshes the Disk
        # window and exposes the just-created package through the ordinary UI.
        disk = next(w for w in os88geom.windows(marty, linear)
                    if w.i == disk_slot)
        ui.raise_window(disk)
        open_named(marty, mouse, linear, disk.x, disk.y, "..")
        disk = next(w for w in os88geom.windows(marty, linear)
                    if w.i == disk_slot)
        open_named(marty, mouse, linear, disk.x, disk.y, OUTPUT)
        marty.run()
        os88marty.settle(marty, limit=300)

        native = window(marty, linear, "UNTITLED")
        if native is None:
            raise RuntimeError("the generated UNTITLED.O88 package did not open")
        native_segment = package_segment(marty, linear, native.i)
        chars = (native_segment << 4) + compiled.sym("_sbs_ch")
        if not wait_text(marty, chars, b"Speedy Basic is ready.", frames=1800):
            raise RuntimeError("generated machine code did not print the welcome text")
        marty.run()
        os88marty.settle(marty, limit=240)
        capture(marty, capture_dir, "speedybasic-compiler-generated-o88.png")

        # Compile a shipped graphics source through the same UI. The Open
        # dialog moves Speedy BASIC into DEMOS, so its package is written
        # there while the compiler temporarily visits its launch folder to
        # fetch SPEEDYCC.RT.
        ui.close(native)
        app = window(marty, linear, "Speedy Basic")
        ui.raise_window(app)
        open_app_file(marty, mouse, linear, app,
                      "SPEEDY/DEMOS/PATTERN.BAS")
        marty.run()
        ui.menu_pick("Run", "Build Package...")
        if wait_window(marty, linear, "Save As") is None:
            raise RuntimeError("PATTERN Build Package did not open Save As")
        if cstring(marty, linear("fdlg_name"), 16) != b"PATTERN.O88":
            raise RuntimeError("PATTERN package default was not PATTERN.O88")
        marty.run()
        marty.key("Enter")
        package_name = (segment << 4) + speedy.sym("_sbu_package_name")
        for _ in range(3600):
            if (marty.read(build_state, 1)[0] == SB_BUILD_DONE and
                    cstring(marty, package_name, 13) == b"PATTERN.O88"):
                break
            marty.advance(frames=1)
        else:
            raise RuntimeError("in-OS PATTERN compile did not finish")

        disk = next(w for w in os88geom.windows(marty, linear)
                    if w.i == disk_slot)
        ui.raise_window(disk)
        open_named(marty, mouse, linear, disk.x, disk.y, "SPEEDY")
        disk = next(w for w in os88geom.windows(marty, linear)
                    if w.i == disk_slot)
        open_named(marty, mouse, linear, disk.x, disk.y, "DEMOS")
        disk = next(w for w in os88geom.windows(marty, linear)
                    if w.i == disk_slot)
        open_named(marty, mouse, linear, disk.x, disk.y, "PATTERN.O88")
        native_graphics = wait_window(marty, linear, "PATTERN")
        if native_graphics is None:
            raise RuntimeError("the generated PATTERN.O88 package did not open")
        graphics_segment = package_segment(marty, linear, native_graphics.i)
        graphics_base = graphics_segment << 4
        if not wait_word(marty, graphics_base + compiled.sym("_sbs_screen"), 7):
            raise RuntimeError("generated PATTERN did not enter SCREEN 7")
        if not wait_word(marty, graphics_base + compiled.sym("_sbaot_pc"), 14,
                         frames=3600):
            raise RuntimeError("generated PATTERN did not execute every LINE")
        gseg = int.from_bytes(marty.read(
            graphics_base + compiled.sym("_sbs_gseg"), 2), "little")
        if not gseg:
            raise RuntimeError("generated PATTERN has no retained graphics")

        def retained_pixel(x, y):
            packed = marty.read((gseg << 4) + y * 160 + x // 2, 1)[0]
            return packed >> 4 if not (x & 1) else packed & 15

        if tuple(retained_pixel(160, y) for y in (60, 90, 120, 150, 180)) \
                != (11, 14, 12, 10, 13):
            raise RuntimeError("generated PATTERN's LINE pixels are absent")
        marty.run()
        os88marty.settle(marty, limit=300)
        capture(marty, capture_dir,
                "speedybasic-compiler-generated-pattern.png")

    print("speedybasic in-OS compiler: dialog, build, FAT write and launch - PASS")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture-dir", default="/tmp/speedybasic-compiler-pr")
    args = parser.parse_args()
    run(args.capture_dir)


if __name__ == "__main__":
    main()
