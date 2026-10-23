#!/usr/bin/env python3
"""RealDMG's 5150 demo at the Game Boy's own speed, on a 4.77 MHz 8088.

    python3 tests/gbdemo.py [--secs S] [--machine M] [--window]

docs/plans/REALDMG-PLAN.md section 6 and wave 8's gate. tools/gbours.py
builds GBDEMO.GB (tools/gbprof/gbdemo.asm, ours: a still scene, a bouncing
ball, a clock and a blip on each landing, then HALT). This boots os8088 on
MartyPC's IBM 5150 with a Hercules card, opens the ROM, takes full screen
with Alt+Enter (REALDMG-SPEC section 15.1, the Hercules page), and reads,
over S guest seconds:

  * the GAME's speed: its frame counter (S_FRAMES in the package's view)
    against S x 59.7275 - which must be 1.00 to two places, the plan's bar;
  * the frames DRAWN a second, off the package's own F1 line - reported, not
    asserted: the drawing is what the machine has left after the game.

A second Alt+Enter must then leave the bracket. Breaking the draw clock (rw_drawdue answering "due" always) gives the
picture the whole machine and the game falls behind: the row fails on the
ratio. MartyPC is cycle-exact, so the ratio is the 5150's.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty as M  # noqa: E402
import os88ui  # noqa: E402
import gbwindow  # noqa: E402

GB_HZ = 59.7275
CLOCK = 4772727.0


def overlay(m, seg):
    """The F1 line's 'NN% NNfps', read out of the package's bss."""
    b = m.read(seg * 16, 0x8000) + m.read(seg * 16 + 0x8000, 0x8000)
    r = re.search(rb"(\d+)% (\d+)fps(?: JIT)?\0", b)
    return (int(r.group(1)), int(r.group(2))) if r else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--secs", type=float, default=60)
    ap.add_argument("--machine", default="os8088_5150_herc_sb_720_gla")
    ap.add_argument("--window", action="store_true", help="stay in the window")
    a = ap.parse_args()
    rom = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "gbours.py"),
                                   "gbdemo"], text=True).strip().splitlines()[-1]
    tmp = tempfile.mkdtemp(prefix="gbdemo-")
    gb = os.path.join(tmp, "GBDEMO.GB")
    with open(rom, "rb") as i, open(gb, "wb") as o:
        o.write(i.read())
    disk = os.path.join(tmp, "apps.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o", disk,
                           "--size", "720", os.path.join(ROOT, "build", "realdmg.o88"), gb],
                          stdout=subprocess.DEVNULL)
    viewoff = gbwindow.label_off(os.path.join(ROOT, "build", "realdmg.lst"), "rdg_viewseg")
    where = "window" if a.window else "full screen"
    with os88ui.boot(os.path.join(ROOT, "build", "os8088-720.img"), apps=disk,
                     machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GBDEMO.GB")
        seg = view = None
        for _ in range(60):
            M.guest_sleep(m, 1)
            seg = seg or gbwindow.find_seg(m, b"REALDMG", viewoff)
            if seg:
                view = int.from_bytes(m.read(seg * 16 + viewoff, 2), "little")
            if view and gbwindow.frames(m, view) > 30:
                break
        if not view:
            print("FAIL gbdemo: the package never started the demo")
            return 1
        fsx = m.sym("fsx_task")
        bracket = lambda: int.from_bytes(m.read(fsx, 2), "little")   # noqa: E731
        if not a.window:                        # Alt+Enter, the full-screen
            m.alt("Enter")                      #   key (SPEC.md 11.2.1.1)
            M.guest_sleep(m, 2)
            if bracket() == 0xFFFF:
                print("FAIL gbdemo: Alt+Enter did not take RealDMG full screen")
                return 1
        m.key("F1")                             # the speed line
        M.guest_sleep(m, 5)                     # settled in
        f0, c0 = gbwindow.frames(m, view), int(m.status()["cycles"])
        drawn = []
        left = a.secs
        while left > 0:
            step = min(10, left)
            M.guest_sleep(m, step)
            left -= step
            ov = overlay(m, seg)
            if ov:
                drawn.append(ov[1])
        f1, c1 = gbwindow.frames(m, view), int(m.status()["cycles"])
        left = None
        if not a.window:                        # ...and Alt+Enter back out
            m.alt("Enter", hold=0.5)
            M.guest_sleep(m, 3)
            left = bracket() == 0xFFFF
    secs = (c1 - c0) / CLOCK
    ratio = (f1 - f0) / (secs * GB_HZ)
    fps = sorted(drawn)[len(drawn) // 2] if drawn else None
    print("gbdemo %s on %s: %d game frames in %.1f guest s = %.3f of real time; %s frames drawn a second"
          % (where, a.machine, f1 - f0, secs, ratio, fps if fps is not None else "no F1 line,"))
    fails = []
    if round(ratio, 2) < 1.00:
        fails.append("the game ran at %.3f of real time, not 1.00" % ratio)
    if not drawn:
        fails.append("no F1 line in the package: nothing says what was drawn")
    if left is False:
        fails.append("a second Alt+Enter did not leave full screen")
    for f in fails:
        print("FAIL gbdemo: " + f)
    print("gbdemo: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
