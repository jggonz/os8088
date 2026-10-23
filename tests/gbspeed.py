#!/usr/bin/env python3
"""REALDMG.O88's Machine > Speed: the game at a fraction of its rate, exactly,
and kept for the game.

    python3 tests/gbspeed.py

docs/REALDMG-SPEC.md section 15.2. Speed scales the clock the game runs to -
the window's frames a BIOS tick and the full screen's rate periods - by
256ths, so 75% is 3 Game Boy frames for every 4 periods of real time, held
steady, and the picture gets whatever the game no longer uses. This boots
MartyPC's VGA XT with the package and CLOCK0.GB (tools/gbclock.py's
do-nothing variant, ours: a frame counter and nothing else, so even an
8088 runs it at the Game Boy's own rate), opens the ROM and reads the
core's frame counter against guest seconds:

  1. at the default, ~100% of 59.7275 frames a second;
  2. after Machine > Speed twice (100 -> 90 -> 75%), 75%;
  3. with the window closed and the game opened again, 75% still - the
     setting is the game's, kept in SYSTEM\\APPDATA\\REALDMG.CFG by its
     header's checksums.

Without the clock's scaling (rw_pertick back to RW_PERTICK) step 2 reads
100%; without rw_spdgame step 3 does.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty as M  # noqa: E402
import os88ui  # noqa: E402
import gbwindow  # noqa: E402

MACHINE = "os8088_xt_vga_720b"
SYS = os.path.join(ROOT, "build", "os8088-720.img")
GB_HZ = 4194304 / 70224
SECS = 10


def started(ui, viewoff):
    """the running package's view, once its game has run 30 frames"""
    m = ui.m
    seg = view = None
    for _ in range(60):
        M.guest_sleep(m, 2)
        seg = seg or gbwindow.find_seg(m, b"REALDMG", viewoff)
        if seg:
            view = int.from_bytes(m.read(seg * 16 + viewoff, 2), "little")
        if view and gbwindow.frames(m, view) >= 30:
            return view
    return None


def rate(m, view):
    """the game's speed over SECS guest seconds, as a share of a Game Boy's"""
    f0 = gbwindow.frames(m, view)
    M.guest_sleep(m, SECS)
    f1 = gbwindow.frames(m, view)
    return ((f1 - f0) & 0xFFFF) / SECS / GB_HZ


def main():
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools/gbclock.py"), "--busy", "0"],
                          stdout=subprocess.DEVNULL)
    src = os.path.join(ROOT, "build", "gbclock", "CLOCK0.GB")
    tmp = tempfile.mkdtemp(prefix="gbspeed-")
    rom = os.path.join(tmp, "CLOCK0.GB")
    with open(src, "rb") as i, open(rom, "wb") as o:
        o.write(i.read())
    disk = os.path.join(tmp, "apps.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools/os88disk.py"), "-o", disk,
                           "--size", "720", "--folder", "SYSTEM/APPDATA",   # (REALDMG.CFG's:
                           os.path.join(ROOT, "build", "realdmg.o88"), rom],   # its own volume)
                          stdout=subprocess.DEVNULL)
    viewoff = gbwindow.label_off(os.path.join(ROOT, "build", "realdmg.lst"), "rdg_viewseg")
    fails = []
    with os88ui.boot(SYS, apps=disk, machine=MACHINE) as ui:
        m = ui.m
        w = ui.path("B:/CLOCK0.GB")
        view = started(ui, viewoff)
        if not view:
            print("FAIL gbspeed: the package never started the game")
            return 1
        M.guest_sleep(m, 3)                 # (what the start owed, run: the
        r = rate(m, view)                   #   first look read 102-103%)
        print("gbspeed: Speed 100%%: %.1f%% of a Game Boy" % (r * 100))
        if not 0.97 <= r <= 1.03:
            fails.append("at 100%% it ran at %.1f%%" % (r * 100))
        ui.menu_pick("CPU", "Speed: 75%")
        M.guest_sleep(m, 3)                 # (what the menu's stall owed, run)
        r = rate(m, view)
        print("gbspeed: Speed 75%%: %.1f%%" % (r * 100))
        if not 0.73 <= r <= 0.77:
            fails.append("at 75%% it ran at %.1f%%" % (r * 100))
        ui.close(w)
        M.guest_sleep(m, 3)
        w = ui.path("B:/CLOCK0.GB")
        view = started(ui, viewoff)
        if not view:
            fails.append("the game did not start again")
        else:
            M.guest_sleep(m, 3)             # (what the start owed, run - as
            r = rate(m, view)               #   at the first look)
            print("gbspeed: opened again: %.1f%%" % (r * 100))
            if not 0.73 <= r <= 0.77:
                fails.append("opened again it ran at %.1f%% - the game's 75%% was not kept"
                             % (r * 100))
    for f in fails:
        print("FAIL gbspeed: " + f)
    print("gbspeed: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
