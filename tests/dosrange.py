#!/usr/bin/env python3
"""INT 33h's COORDINATE WINDOW (SPEC.md 96.10.7).

`07h` and `08h` are a program telling the driver what its own screen is, and
every `03h` after that is an answer in those units. This box answered them as
no-ops for as long as they were in the dispatcher at all - *"we clamp nothing,
the host's pointer is already inside the screen"* - and the pointer being
inside THE SCREEN is not the claim the program made.

**BATTLE CHESS IS THE REPORT** (docs/FIELD-NOTES.md 56): a mode 13h game that
sets 0..319 x 0..199 and then polls `03h` for ever. It was handed **320** on
the first read, one past its own window, and ran off the rails into low memory
- a black screen, permanently, on the press of the key that starts the game.
With the pointer starting at (0,0) that first read was accidentally in range,
which is why §96.45.3's centring - six bytes, and correct - is what exposed it.

WHAT THIS WOULD CATCH, and each of these was seen:

  - 07h/08h answered as no-ops          -> POS3 reads 600 where the program
                                           asked for 0..319
  - the map cut from [dos_vw]/[dos_vh]  -> right on kern_dos and on a CGA,
    instead of INT 33h's virtual screen     where they are 640x200, and y
                                            scaled TWICE on anything else -
                                            measured: 60 answered as 25
  - the window not re-opened by 00h     -> a second program in the same
                                           session inherits the first's

**BOTH ARMS ARE THE ROW**, for `tests/dosmouse.py`'s reason one layer up: a
CGA desktop IS 640x200, so the host's own scale is the identity there and a
window map taken against the wrong pair reads perfectly. Hercules is 720x348
and nothing cancels.

It runs on MartyPC and must: the pointer is moved by driving a real serial
mouse into a real 8088.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmouse                                                # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
DISK = "build/dosrange360.img"

VW, VH = 640, 200                   # INT 33h's virtual screen (SPEC.md 96.10)
WX, WY = 320, 200                   # ...and the window MOURANGE.COM asks for:
                                    # 0..319 and 0..199, which is Battle
                                    # Chess's own (96.10.7)
MACHINES = ("os8088_5150_cga_gla", "os8088_5150_herc_gla")


def fail(msg):
    print("dosrange: FAIL: %s" % msg)
    sys.exit(1)


def pos(m, label):
    f = dosmouse.fields(dosmouse.wait_line(m, label))
    try:
        return int(f["x"]), int(f["y"])
    except (KeyError, ValueError):
        fail("could not read a position out of the %s line: %r" % (label, f))


def run(machine):
    print("dosrange: --- %s ---" % machine)
    with os88ui.boot(SYS, apps=DISK, machine=machine) as ui:
        m = ui.m
        if not ui.path("B:/MOURANGE.COM"):
            fail("double-clicking MOURANGE.COM opened no window")

        # --- 1. the full window is the IDENTITY -----------------------------
        # Nothing has called 07h yet, so this is the host's own scale and
        # `tests/dosmouse.py` is what proves it. All this row asks of it is
        # that the map has not quietly changed it.
        x1, y1 = pos(m, "POS1")
        w, h = ui._word("vid_w"), ui._word("vid_h")
        want = (ui._word("mouse_x") * VW // w, ui._word("mouse_y") * VH // h)
        print("dosrange: POS1 (full window) %s, the pointer scales to %s"
              % ((x1, y1), want))
        if abs(x1 - want[0]) > 2 or abs(y1 - want[1]) > 2:
            fail("POS1: the FULL window is not the identity - INT 33h "
                 "answered (%d,%d) where the pointer scales to (%d,%d). "
                 "Nothing has called 07h at this point (SPEC.md 96.10.7)"
                 % (x1, y1, want[0], want[1]))

        # --- 2. the window is set, with nothing moved -----------------------
        x2, y2 = pos(m, "POS2")
        print("dosrange: POS2 (window %dx%d) %s" % (WX, WY, (x2, y2)))
        if not (0 <= x2 < WX and 0 <= y2 < WY):
            fail("POS2: (%d,%d) is outside the 0..%d x 0..%d window the "
                 "program set two instructions earlier, which is the read "
                 "Battle Chess crashes on (SPEC.md 96.10.7)"
                 % (x2, y2, WX - 1, WY - 1))

        # --- 3. ...and now the pointer is put somewhere it CANNOT be --------
        # Three quarters of the way across is past the window's right edge in
        # the host's units, and that is the whole point: without the map this
        # reads ~600 and a program that indexes a table with it is gone.
        px, py = w * 3 // 4, h * 3 // 4
        ui.mo.to(px, py)
        m.type_text("x")
        x3, y3 = pos(m, "POS3")
        hx = ui._word("mouse_x") * VW // w
        hy = ui._word("mouse_y") * VH // h
        wx, wy = hx * WX // VW, hy * WY // VH
        print("dosrange: POS3 pointer (%d,%d) -> host (%d,%d) -> window "
              "(%d,%d); the program read (%d,%d)"
              % (px, py, hx, hy, wx, wy, x3, y3))
        if not (0 <= x3 < WX and 0 <= y3 < WY):
            fail("POS3: (%d,%d) is outside the program's own 0..%d x 0..%d "
                 "window with the pointer at (%d,%d) - which is %d in the "
                 "host's units (SPEC.md 96.10.7)"
                 % (x3, y3, WX - 1, WY - 1, px, py, hx))
        if abs(x3 - wx) > 2 or abs(y3 - wy) > 2:
            fail("POS3: the map is wrong - (%d,%d) against (%d,%d) for a "
                 "pointer at host (%d,%d). The window is cut from INT 33h's "
                 "VIRTUAL screen (%dx%d) and never from [dos_vw]/[dos_vh], "
                 "which are the real one (SPEC.md 96.10.7)"
                 % (x3, y3, wx, wy, hx, hy, VW, VH))

        # --- 4. 04h is still refused ----------------------------------------
        x4, y4 = pos(m, "POS4")
        if (x4, y4) != (x3, y3):
            fail("POS4: AX=4 moved the pointer to (%d,%d) from (%d,%d). "
                 "Warping the host's arrow is the kernel's, and a program "
                 "that borrowed the screen has not borrowed it (SPEC.md "
                 "96.10.2)" % (x4, y4, x3, y3))
        print("dosrange: POS4 (04h, refused) %s" % ((x4, y4),))

        # --- 5. ...and opening the window again is LIVE ---------------------
        # A real driver clamped its own stored position and cannot give the
        # overshoot back (§96.10.7, POS5 of the reference run). This box holds
        # no position at all - it maps the host's - so widening the window
        # answers the pointer where it actually is, immediately.
        x5, y5 = pos(m, "POS5")
        print("dosrange: POS5 (window re-opened) %s, host is (%d,%d)"
              % ((x5, y5), hx, hy))
        if abs(x5 - hx) > 2 or abs(y5 - hy) > 2:
            fail("POS5: re-opening the window to the whole virtual screen "
                 "answered (%d,%d) where the pointer is at (%d,%d). The map "
                 "holds no position of its own, so there is nothing for it "
                 "to be stuck on (SPEC.md 96.10.7)" % (x5, y5, hx, hy))
    print("dosrange: %s ok" % machine)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=None)
    a = ap.parse_args(argv)
    for mach in ([a.machine] if a.machine else MACHINES):
        run(mach)
    print("dosrange: the window 07h and 08h set is the window 03h answers in")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
