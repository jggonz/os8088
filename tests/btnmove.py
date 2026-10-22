#!/usr/bin/env python3
"""A standard button answers where its window IS after a drag.

    python3 tests/btnmove.py

SPEC.md 20.5.1.3.5. A window dragged by its title is replayed from the drag
cache and never painted (SPEC.md 11.96.12.0), so a package that lays its
button rects out in W_PAINT - most of them - went on hit-testing the window's
OLD place, and os88ui_btnclick tests those rects before any package code
runs. The library now banks where each record's rects were last right and
moves them with the window.

The Timer is the subject because it is the plainest case there is: its rects
are written in its paint and NOWHERE else, and its tick redraws only the
digits, so before the library fix a dragged Timer's buttons stayed dead until
a minimize and restore. On a cycle-accurate 5150/CGA: open B:\\APPS\\TIMER.O88
(it opens RUNNING), read Start's rect off the package, drag the window by
(48, 24), press STOP at the place it is now DRAWN and require [tm_run] to go
1 -> 0; then drag it again, the other way, and press START for 0 -> 1 - which
a fix that only worked once (a bank taken at the paint and never again) would
fail, and which no stale press can pass, since each leg needs a button to
FIRE.

The package has no symbol map in the build, so this assembles
apps/timer/timer.asm into a scratch directory with one, and refuses to go on
unless the result is byte for byte the build's own timer.bin.

Red with the fix out (OS88UI_REBASE undefined): "Stop did not fire".
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, "tools")
import os88marty as M                                       # noqa: E402
import os88ui                                               # noqa: E402
import os88geom as geom                                     # noqa: E402
import os88build                                            # noqa: E402

SRC = "apps/timer/timer.asm"
START = 0                       # TM_START: the first rect of the record


def timer_syms():
    """tm_run and tm_rects, off a map of the build's own timer.bin."""
    want = open(os88build.at("build/timer.bin"), "rb").read()
    with tempfile.TemporaryDirectory() as td:
        out, mp = os.path.join(td, "timer.bin"), os.path.join(td, "timer.map")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "--before", "[map all %s]" % mp, "-o", out, SRC],
                       check=True)
        if open(out, "rb").read() != want:
            sys.exit("btnmove: %s assembles to something other than "
                     "build/timer.bin - `make` first" % SRC)
        syms = {}
        for line in open(mp):
            p = line.split()
            if len(p) == 3 and re.match(r"^[0-9A-F]+$", p[0]):
                syms[p[2]] = int(p[0], 16)
    for n in ("tm_run", "tm_rects"):
        if n not in syms:
            sys.exit("btnmove: no %s in the timer's map" % n)
    return syms


def main():
    for f in ("build/os8088-360.img", "build/apps360.img",
              "build/timer.bin"):
        if not os.path.exists(os88build.at(f)):
            sys.exit("btnmove: no %s - `make` first" % f)
    syms = timer_syms()
    with os88ui.boot("build/os8088-360.img",
                     apps="build/apps360.img") as ui:
        m = ui.m
        w = ui.path("B:/APPS/TIMER.O88")
        base = ui._S("wm_wins") + w.i * geom.WIN_SIZE
        seg = struct.unpack("<H", bytes(m.read(base + geom.W_SEG, 2)))[0]

        def run():
            return m.readseg(seg, syms["tm_run"], 1)[0]

        def rect():
            return struct.unpack("<4h", bytes(
                m.readseg(seg, syms["tm_rects"] + 8 * START, 8)))

        if run() != 1:
            sys.exit("btnmove: FAIL - the Timer opened STOPPED; it starts "
                     "itself (tm_entry), so this row's legs are wrong")
        M.until(m, lambda _: any(rect()), "the Timer's first paint to lay "
                "its buttons out", limit=20.0)
        x1, y1, x2, y2 = rect()
        # Start's centre, relative to the window's frame: where it is drawn
        # wherever the window goes
        rx, ry = (x1 + x2) // 2 - w.x, (y1 + y2) // 2 - w.y
        print("  Timer at %d,%d, Start %r" % (w.x, w.y, (x1, y1, x2, y2)))

        for leg, (dx, dy), want in (("one", (48, 24), 0), ("two", (-40, 16), 1)):
            x0, y0 = w.x, w.y
            w = ui._refresh(ui.drag_window(w, dx, dy))
            print("  leg %s: dragged %d,%d -> %d,%d (no repaint); Start's "
                  "rect still reads %r" % (leg, x0, y0, w.x, w.y, rect()))
            # leg one presses STOP, one button to Start's right (TM_BGAP 6);
            # leg two presses START. Both legs fire a button, so neither can
            # pass by the state merely staying where it was
            px = w.x + rx + (0 if want else (x2 - x1 + 1) + 6)
            ui.mo.click(px, w.y + ry)
            try:
                M.until(m, lambda _: run() == want,
                        "[tm_run] to read %d" % want, limit=20.0)
            except Exception as e:
                sys.exit("btnmove: FAIL - leg %s: %s did not fire at the "
                         "window's NEW place (%d,%d); the record's rect reads "
                         "%r - stale since the drag (SPEC.md 20.5.1.3.5): %s"
                         % (leg, "Start" if want else "Stop", px, w.y + ry,
                            rect(), e))
            print("  leg %s: %s fired; Start's rect now %r"
                  % (leg, "Start" if want else "Stop", rect()))
    print("btnmove: ok")


if __name__ == "__main__":
    main()
