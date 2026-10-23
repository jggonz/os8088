#!/usr/bin/env python3
"""Drive a running Bochs (Xvfb :99, mouse captured) by GUEST coordinates -
the input half of tools/os88bochs.py (KERN-MEGA-HANDOFF.md).

    os88bochsui.py capture            grab the mouse (ctrl+middle) - first
    os88bochsui.py goto X Y           home to (0,0), then walk to guest (X, Y)
    os88bochsui.py move DX DY         relative, in guest pixels, no homing
    os88bochsui.py click | dbl        left click / double click where it is
    os88bochsui.py down | up          left button
    os88bochsui.py key K [K ...]      xdotool key names
    os88bochsui.py type TEXT          xdotool type
    os88bochsui.py shot FILE.png      screenshot of the guest area
    os88bochsui.py e9 [OUTDIR]        the debug port's tail (default
                                      build/bochs)

A menu pick is `goto` the title, `down`, `move` to the item, `up`.

The guest moves one pixel per two X pixels (measured), so moves are sent in
small steps and the position is made absolute by pinning at the top-left
corner first, where the kernel clamps.
"""
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENV = dict(os.environ, DISPLAY=os.environ.get("OS88_BOCHS_DISPLAY", ":99"))  # os88bochs.py's


def xd(*a):
    subprocess.run(["xdotool"] + [str(x) for x in a], env=ENV, check=True)


def rel(dx, dy):
    xd("mousemove_relative", "--", dx, dy)
    time.sleep(0.04)


def goto(x, y):
    for _ in range(40):
        rel(-60, -60)
    time.sleep(0.3)
    x2, y2 = 2 * x, 2 * y
    while x2 or y2:
        sx = max(-30, min(30, x2))
        sy = max(-30, min(30, y2))
        rel(sx, sy)
        x2 -= sx
        y2 -= sy
    time.sleep(0.5)


def main(a):
    c = a[0]
    if c == "capture":
        w = subprocess.run(["xdotool", "search", "--name", "Bochs"], env=ENV,
                           capture_output=True, text=True).stdout.split()[0]
        xd("windowfocus", "--sync", w)
        xd("mousemove", 330, 300)
        xd("keydown", "ctrl")
        xd("click", 2)
        xd("keyup", "ctrl")
    elif c == "goto":
        goto(int(a[1]), int(a[2]))
    elif c == "move":               # relative, in GUEST pixels, no homing
        x2, y2 = 2 * int(a[1]), 2 * int(a[2])
        while x2 or y2:
            sx = max(-30, min(30, x2))
            sy = max(-30, min(30, y2))
            rel(sx, sy)
            x2 -= sx
            y2 -= sy
        time.sleep(0.5)
    elif c == "click":
        xd("click", 1)
    elif c == "dbl":
        xd("click", "--repeat", 2, "--delay", 120, 1)
    elif c == "down":
        xd("mousedown", 1)
    elif c == "up":
        xd("mouseup", 1)
    elif c == "key":
        xd("key", "--delay", 80, *a[1:])
    elif c == "type":
        xd("type", "--delay", 80, a[1])
    elif c == "shot":
        subprocess.run("xwd -root -silent | convert xwd:- -crop 650x500+5+40 "
                       "+repage '%s'" % a[1], shell=True, env=ENV,
                       check=True)
    elif c == "e9":
        out = a[1] if len(a) > 1 else os.path.join(ROOT, "build", "bochs")
        with open(os.path.join(out, "e9.out"), "rb") as f:
            t = f.read().decode("latin-1")
        print("\n".join(l for l in t.splitlines() if l.strip())[-1500:])


if __name__ == "__main__":
    main(sys.argv[1:])
