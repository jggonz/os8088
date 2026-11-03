#!/usr/bin/env python3
"""doswmouse - INT 33h for a DOS program running in its window (SPEC.md
96.54.4, docs/plans/DOS-WINDOWED-PLAN.md wave 5).

A program in its window sees an 80x25 text screen that is the window's
console band, so its mouse must be on THAT screen: column * 8 and row * 8 of
INT 33h's 640x200, from the band's top-left and the first buffer row shown.
The probe, tests/dostrap/mwin.asm, polls 03h and 05h and has an 0Ch event
handler, and keeps what each said at PSP:0103h. On a Hercules, where the
desktop is 720x348 and the band has a 40-pixel margin, so a desktop answer
scaled for a full screen is wrong on both axes.

  1  the probe runs once in the BRACKET, the way a double-click runs it
  2  Run in Window: the pointer on a known band cell reads that cell's
     virtual position through 03h AND through the event handler, and the
     shown cursor is drawn NOWHERE ([dos_m33dv] stays 0)
  3  a click on the band is a 03h button and a 05h press, at that position
  4  off the band (the window's own bar) the position clamps to the band's
     edge and a press there is NOT the program's
  5  with another window in front, the click that RAISES the DOS window is
     the desktop's: no press reaches the program; the next click does
  6  Alt+Enter: on the full screen the bracket's whole-screen translation
     applies (desktop pixels scaled to 640x200)
  7  a key ends it

**Break it on purpose** (docs/WRITING-TESTS.md 1): with `dwm_start`'s two
hook stores taken out (the window back on the bracket's scaled desktop
answer), leg 2 fails - done: `03h says (123,45), the cell is (99,21)`. With
`dvw_nap`'s `call dwm_kick` taken out, leg 2 fails on the event handler -
done: `called 1 times and last said (48,19)` - because the probe waits in
`int 16h` and so sleeps through the ticks that dispatch it.
"""

import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SYS = os.path.join(ROOT, "build", "os8088-360.img")
PROBE = os.path.join(ROOT, "tests", "dostrap", "mwin.asm")
VW, VH = 640, 200


def fail(msg):
    print("doswmouse: FAIL: %s" % msg)
    sys.exit(1)


class Dos(object):
    """The live instance, read per access (the region moves)."""

    def __init__(self, m):
        self.m = m
        self.dm = dosmap.package()

    def base(self):
        return dosmap.lin(self.m, dosmap.instance(self.m))

    def b(self, name):
        return dosmap.cellread(self.m, self.dm, self.base(), name, 1)[0]

    def w(self, name):
        return int.from_bytes(dosmap.cellread(self.m, self.dm, self.base(), name, 2),
                              "little")

    def rec(self):
        """The probe's twelve words at PSP:0103h."""
        psp = self.w("dos_ldpsp")
        raw = self.m.read(psp * 16 + 0x103, 22)
        v = [int.from_bytes(raw[i:i + 2], "little") for i in range(0, 22, 2)]
        k = ("polls", "btn", "x", "y", "npress", "px", "py",
             "nev", "ex", "ey", "ebtn")
        return dict(zip(k, v))


def until(m, cond, what, limit):
    try:
        os88marty.until(m, lambda _m: cond(), what, poll=0.2, limit=limit)
        return True
    except os88marty.MartyError:
        return False


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    tmp = tempfile.mkdtemp(prefix="doswmouse")
    try:
        com = os.path.join(tmp, "MWIN.COM")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com, PROBE],
                       check=True)
        disk = os.path.join(tmp, "mwin360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", com], check=True,
                       stdout=subprocess.DEVNULL)
        run(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswmouse: ok")


def run(disk):
    with os88ui.boot(SYS, apps=disk, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        mo = os88mouse.Mouse(marty=m)

        # --- 1: the bracket -------------------------------------------------
        if not ui.path("B:/MWIN.COM"):
            fail("could not launch MWIN.COM")
        dos = Dos(m)
        if not until(m, lambda: any("MWIN READY" in r
                                    for r in (m.screen() or [])),
                     "the bracketed run", 60):
            fail("MWIN.COM never started inside the bracket")
        m.type_text("x")
        if not until(m, lambda: not dos.b("dos_inbr"), "the bracket down", 48):
            fail("the bracket never came down")
        os88marty.settle(m)
        print("doswmouse: 1 the bracket ran it")

        # --- 2: in the window, the pointer on a known cell ------------------
        ui.raise_window(ui.window("DOS"))
        dosmap.run_in_window(ui)
        if not until(m, lambda: dos.b("dvw_run") == 1, "the worker's run", 20):
            fail("Run in Window put nothing on the worker")
        if not until(m, lambda: dos.rec()["polls"] > 50, "the polls", 20):
            fail("the probe is not polling INT 33h in the window")
        os88marty.settle(m)
        bx, by = dos.w("dos_conx"), dos.w("dos_cony")
        cols, rows = dos.w("dos_concols"), dos.w("dos_conrows")
        vtop = dos.w("con_vtop")
        if rows < 4 or cols < 20:
            fail("the band is %dx%d cells - too small to aim at" % (cols, rows))
        print("doswmouse: band at (%d,%d), %dx%d cells, first row %d"
              % (bx, by, cols, rows, vtop))

        def aim(col, row, ox=3, oy=5):
            """The desktop pixel over buffer cell (col, row), and the virtual
            position INT 33h must answer for it."""
            return ((bx + col * 8 + ox, by + (row - vtop) * 8 + oy),
                    (col * 8 + ox, row * 8 + oy))

        (sx, sy), (vx, vy) = aim(12, vtop + 2)
        mo.to(sx, sy)
        if not until(m, lambda: (dos.rec()["x"], dos.rec()["y"]) == (vx, vy),
                     "03h at the cell", 10):
            r = dos.rec()
            fail("pointer at desktop (%d,%d): 03h says (%d,%d), the cell is "
                 "(%d,%d)" % (sx, sy, r["x"], r["y"], vx, vy))
        if not until(m, lambda: (dos.rec()["ex"], dos.rec()["ey"]) == (vx, vy),
                     "the handler at the cell", 10):
            r = dos.rec()
            fail("the event handler was called %d times and last said "
                 "(%d,%d), the cell is (%d,%d)"
                 % (r["nev"], r["ex"], r["ey"], vx, vy))
        if dos.rec()["btn"] != 0:
            fail("03h reports buttons %d with none pressed" % dos.rec()["btn"])
        if dos.w("dos_m33dv") != 0:
            fail("the shown cursor was drawn at %04X - a window has the "
                 "desktop's arrow and no text cursor" % dos.w("dos_m33dv"))
        # ...and a second cell, so a constant offset cannot pass
        (sx2, sy2), (vx2, vy2) = aim(40, vtop + rows - 1, 7, 0)
        mo.to(sx2, sy2)
        if not until(m, lambda: (dos.rec()["x"], dos.rec()["y"]) == (vx2, vy2),
                     "03h at the second cell", 10):
            r = dos.rec()
            fail("pointer at desktop (%d,%d): 03h says (%d,%d), the cell is "
                 "(%d,%d)" % (sx2, sy2, r["x"], r["y"], vx2, vy2))
        t0 = int(m.status().get("cycles", 0))
        p0 = dos.rec()["polls"]
        os88marty.pace(m, 2.0)
        dc = int(m.status().get("cycles", 0)) - t0
        dp = dos.rec()["polls"] - p0
        print("doswmouse: 2 03h and the handler say (%d,%d) and (%d,%d) on the "
              "cells; no cursor drawn; %.0f polls a guest second"
              % (vx, vy, vx2, vy2, dp * os88marty.GUEST_HZ / max(dc, 1)))

        # --- 3: a click on the band ------------------------------------------
        mo.to(sx, sy)
        n0 = dos.rec()["npress"]
        mo._edge(True)
        if not until(m, lambda: dos.rec()["btn"] & 1, "03h's left button", 10):
            fail("the left button is down on the band and 03h says %d"
                 % dos.rec()["btn"])
        mo._edge(False)
        if not until(m, lambda: dos.rec()["npress"] > n0, "05h's press", 10):
            fail("a click on the band: 05h counted no press")
        r = dos.rec()
        if (r["px"], r["py"]) != (vx, vy):
            fail("05h put the press at (%d,%d), the cell is (%d,%d)"
                 % (r["px"], r["py"], vx, vy))
        print("doswmouse: 3 a click on the band is 03h's button and 05h's "
              "press at (%d,%d)" % (vx, vy))

        # --- 4: off the band --------------------------------------------------
        mo.to(sx, by - 6)                   # the window's own bar
        if not until(m, lambda: (dos.rec()["x"], dos.rec()["y"])
                     == (vx, vtop * 8), "the clamp", 10):
            r = dos.rec()
            fail("on the bar above the band 03h says (%d,%d); clamped to the "
                 "band it is (%d,%d)" % (r["x"], r["y"], vx, vtop * 8))
        n0 = dos.rec()["npress"]
        mo._edge(True)
        os88marty.pace(m, 0.5)
        if dos.rec()["btn"]:
            fail("a press on the bar reached the program as button %d"
                 % dos.rec()["btn"])
        mo._edge(False)
        os88marty.pace(m, 0.3)
        if dos.rec()["npress"] != n0:
            fail("a press on the bar was counted by 05h")
        print("doswmouse: 4 off the band it clamps to the edge, and a press "
              "there is not the program's")

        # --- 5: the click that raises the window is the desktop's ------------
        disk_w = ui.window("Disk")
        ui.raise_window(disk_w)
        d = ui.window("Disk")
        spot = None
        for row in range(vtop, vtop + rows):
            for col in range(0, cols, 4):
                (x, y), v = aim(col, row)
                if not (d.x <= x <= d.x + d.w + 1 and d.y <= y <= d.y + d.h + 1):
                    spot = ((x, y), v)
                    break
            if spot:
                break
        if not spot:
            fail("the Disk window covers the whole band - nowhere to click")
        (x5, y5), v5 = spot
        mo.to(x5, y5)
        os88marty.pace(m, 0.3)
        n0 = dos.rec()["npress"]
        mo._edge(True)
        if not until(m, lambda: ui.front() and ui.front().title.upper()
                     .startswith("DOS"), "the raise", 10):
            fail("a press on the DOS band did not raise the DOS window")
        os88marty.pace(m, 0.5)
        if dos.rec()["btn"]:
            fail("the raising press reached the program as button %d"
                 % dos.rec()["btn"])
        mo._edge(False)
        os88marty.pace(m, 0.3)
        if dos.rec()["npress"] != n0:
            fail("the click that raised the window was counted by 05h")
        mo._edge(True)
        mo._edge(False)
        if not until(m, lambda: dos.rec()["npress"] > n0, "the next click", 10):
            fail("the click after the raise was not counted")
        print("doswmouse: 5 the raising click was the desktop's; the next one "
              "the program's, at (%d,%d)" % v5)

        # --- 6: the full screen keeps the bracket's translation --------------
        m.alt("Enter", hold=0.08)
        if not until(m, lambda: dos.b("dvw_fs") == 1, "the full screen", 20):
            fail("Alt+Enter brought up no full screen")
        os88marty.settle(m)
        dw, dh = mo.where()[0], mo.where()[1]
        tx, ty = 360, 174
        mo.to(tx, ty)
        want = (tx * VW // 720, ty * VH // 348)
        if not until(m, lambda: (dos.rec()["x"], dos.rec()["y"]) == want,
                     "the full screen's scale", 10):
            r = dos.rec()
            fail("on the full screen 03h says (%d,%d) at desktop (%d,%d); the "
                 "bracket's scale says %r" % (r["x"], r["y"], tx, ty, want))
        print("doswmouse: 6 on the full screen 03h is the bracket's (%d,%d)"
              % want)

        # --- 7: a key ends it --------------------------------------------------
        m.type_text("q")
        if not until(m, lambda: dos.b("dvw_run") == 0, "the end", 30):
            fail("the probe did not end on its key")
        print("doswmouse: 7 a key ended it (from desktop (%d,%d))" % (dw, dh))


if __name__ == "__main__":
    main()
