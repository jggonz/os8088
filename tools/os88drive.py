#!/usr/bin/env python3
"""Absolute mouse driving for os8088 under MartyPC.

A serial mouse is relative, so position is derived the way tools/mouse.py
derives it under QEMU: slam into the top-left corner, where the kernel's own
edge clamp pins it, and count from there.

PACED IN GUEST TIME, NOT WALL TIME (docs/SNAPSHOT-PLAN.md §2.1). Every wait
here is `advance(frames=N)`, which runs the emulator a bounded number of
completed video frames and stops. The obvious alternative - `time.sleep` while
the machine free-runs - makes the same script land somewhere different every
run: the emulator goes at whatever multiple of real time the host manages, and
two instances given the same sleep(22) were measured 21.7 MILLION cycles apart.
That is enough for every later click to hit a different thing.

With this pacing a scripted navigation is reproducible, which is what makes it
usable as the approach to a snapshot point *and* as a replay.
"""
import sys
sys.path.insert(0, "/home/user/os8088/tools")
from os88marty import Marty

# Frames, not seconds. 60 frames is ~1 s of guest time on a 60 Hz card.
F_SETTLE = 8       # after a move, for the packet to be decoded and drawn
F_CLICK = 8        # between press and release
F_AFTER = 18       # after a click, for the app to react


class Pointer:
    def __init__(self, m, w=640, h=480):
        self.m, self.w, self.h = m, w, h
        self.x = self.y = 0

    def _wait(self, frames):
        self.m.advance(frames=frames)

    def home(self):
        """Slam into the corner; the kernel's edge clamp pins it at (0,0)."""
        for _ in range(12):
            self.m.mouse(-120, -120)
            self._wait(2)
        self.x = self.y = 0
        self._wait(F_SETTLE)

    def goto(self, x, y, held=False, step=60):
        dx, dy = x - self.x, y - self.y
        while dx or dy:
            sx = max(-step, min(step, dx))
            sy = max(-step, min(step, dy))
            self.m.mouse(sx, sy, l=held)
            self._wait(2)
            dx -= sx
            dy -= sy
        self.x, self.y = x, y
        self._wait(F_SETTLE)

    def click(self, x=None, y=None):
        if x is not None:
            self.goto(x, y)
        self.m.mouse(0, 0, l=True); self._wait(F_CLICK)
        self.m.mouse(0, 0);         self._wait(F_AFTER)

    def dblclick(self, x=None, y=None):
        # SPEC.md §13's double-click window is 9 ticks (~0.5 s), so the two
        # clicks have to be close in GUEST time - which is exactly what this
        # pacing guarantees and what a wall-clock script cannot.
        if x is not None:
            self.goto(x, y)
        for _ in range(2):
            self.m.mouse(0, 0, l=True); self._wait(3)
            self.m.mouse(0, 0);         self._wait(3)
        self._wait(F_AFTER)

    def menu(self, bar_x, bar_y, item_x, item_y):
        """Press on the bar, drag to the item, release - the only way menus
        are driven (SPEC.md §12)."""
        self.goto(bar_x, bar_y)
        self.m.mouse(0, 0, l=True); self._wait(F_CLICK)
        self.goto(item_x, item_y, held=True)
        self._wait(F_CLICK)
        self.m.mouse(0, 0); self._wait(F_AFTER)


def shot(m, path):
    from os88marty import write_png
    w, h, rows = m.vram()
    write_png(path, w, h, rows)
    return path
