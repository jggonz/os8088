#!/usr/bin/env python3
"""doswfs - a LIVE DOS program between its window and the full screen
(SPEC.md 96.54.1, docs/plans/DOS-WINDOWED-PLAN.md wave 3).

The probe, tests/dostrap/dwfs.asm, prints a line every half second WAITING BY
SPINNING, and counts its lines at PSP:0103h - so "running" and "frozen" are
read off the program itself and not off any screen.

  1  the bracket: it runs and quits on `q` (which also names it in the path box)
  2  Run in Window, and it counts
  3  Alt+Enter: the full screen comes UP WITH THE PROGRAM STILL RUNNING - the
     count keeps rising under the bracket, and its lines are on the glass
  4  Alt+Enter: back to the window, FROZEN - the bracket down, the count
     standing still for two guest seconds, and its last line in the band
  5  Alt+Enter: thawed onto the full screen, and counting again
  6  `g` sets graphics mode 4: the ROM's, on the glass ([dvw_gfx]) - and an
     Alt+Enter now does NOT come back, there being no text to bring
  7  a key: mode 3, `DWFS BACK`, exit - and the console holds the program's
     last screen and DOS's exit line, as a bracketed run's does
  8  THE SHUNT: run in the window again and press `g` there - the bracket
     comes up by itself, before the mode set, and the run ends cleanly

**Break it on purpose** (docs/WRITING-TESTS.md 1): with `.gfx`'s shunt
replaced by a straight jump to the ROM, leg 8 fails as it should - "a
windowed program set a GRAPHICS mode and no bracket came up for it" (done).
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
import os88ui                                                  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SYS = os.path.join(ROOT, "build", "os8088-360.img")
PROBE = os.path.join(ROOT, "tests", "dostrap", "dwfs.asm")
FSX_NONE = 0xFF


def fail(msg):
    print("doswfs: FAIL: %s" % msg)
    sys.exit(1)


class Dos(object):
    def __init__(self, m):
        self.m = m
        self.dm = dosmap.package()

    def base(self):
        return dosmap.lin(self.m, dosmap.instance(self.m))

    def b(self, name):
        return dosmap.cellread(self.m, self.dm, self.base(), name, 1)[0]

    def rows(self):
        scr = self.m.read(self.base() + self.dm["con_scr"], 80 * 25 * 2)
        return ["".join(chr(scr[r * 160 + i]) if 32 <= scr[r * 160 + i] < 127
                        else " " for i in range(0, 160, 2)).rstrip()
                for r in range(25)]

    def word(self, off):
        psp = int.from_bytes(self.m.read(self.base() + self.dm["dos_ldpsp"],
                                         2), "little")
        return int.from_bytes(self.m.read(psp * 16 + off, 2), "little")

    def count(self):
        return self.word(0x103)


def until(m, cond, what, limit):
    try:
        os88marty.until(m, lambda _m: cond(), what, poll=0.2, limit=limit)
        return True
    except os88marty.MartyError:
        return False


def bracket(m):
    return m.read(m.sym("fsx_cur"), 1)[0]


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    tmp = tempfile.mkdtemp(prefix="doswfs")
    try:
        com = os.path.join(tmp, "DWFS.COM")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com, PROBE],
                       check=True)
        disk = os.path.join(tmp, "dwfs360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", com], check=True,
                       stdout=subprocess.DEVNULL)
        run(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswfs: ok")


def rising(m, dos, what, limit=20):
    c0 = dos.count()
    if not until(m, lambda: dos.count() >= c0 + 2, what, limit):
        fail("%s: the count stood at %d - the program is not running"
             % (what, dos.count()))


def run(disk):
    with os88ui.boot(SYS, apps=disk, machine="os8088_5150_herc_gla") as ui:
        m = ui.m

        # --- 1: the bracket, which also names it ----------------------------
        if not ui.path("B:/DWFS.COM"):
            fail("could not launch DWFS.COM")
        dos = Dos(m)
        os88marty.pace(m, 2)
        m.type_text("q")
        if not until(m, lambda: not dos.b("dos_inbr"), "the bracket down", 40):
            fail("the bracketed run never came down after `q`")
        os88marty.settle(m)
        print("doswfs: 1 the bracket ran it and `q` ended it")

        # --- 2: in the window ------------------------------------------------
        ui.raise_window(ui.window("DOS"))
        dosmap.run_in_window(ui)
        if not until(m, lambda: dos.b("dvw_run") == 1, "the worker's run", 20):
            fail("Run in Window put nothing on the worker")
        rising(m, dos, "the windowed run")
        if bracket(m) != FSX_NONE:
            fail("a bracket is up during a windowed run")
        print("doswfs: 2 it counts in the window")

        # --- 3: Alt+Enter, live ---------------------------------------------
        m.alt("Enter", hold=0.08)
        if not until(m, lambda: dos.b("dvw_fs") == 1 and
                     bracket(m) != FSX_NONE, "the full screen", 20):
            fail("Alt+Enter on a running windowed program brought up no "
                 "bracket ([dvw_fs] %d, [fsx_cur] %02X)"
                 % (dos.b("dvw_fs"), bracket(m)))
        rising(m, dos, "the program under the bracket")
        if not until(m, lambda: any(r.startswith("DWFS ")
                                    for r in (m.screen() or [])),
                     "its lines on the glass", 10):
            fail("the program runs under the bracket and no DWFS line is on "
                 "the text screen: %r" % (m.screen() or [])[-4:])
        print("doswfs: 3 Alt+Enter took it to the full screen, still counting")

        # --- 4: Alt+Enter back: frozen ----------------------------------------
        os88marty.pace(m, 0.5)
        m.alt("Enter", hold=0.08)
        if not until(m, lambda: bracket(m) == FSX_NONE and
                     dos.b("dvw_frz") == 1, "back in the window", 20):
            fail("Alt+Enter in the full screen did not come back frozen "
                 "([fsx_cur] %02X, [dvw_frz] %d)"
                 % (bracket(m), dos.b("dvw_frz")))
        c = dos.count()
        os88marty.pace(m, 2)
        if dos.count() != c:
            fail("the program counted %d -> %d in two guest seconds while "
                 "FROZEN in the window" % (c, dos.count()))
        last = "DWFS %d" % (c & 7)
        live = [r for r in dos.rows() if r.strip()]
        if not live or live[-1] != last:
            fail("frozen at line %d, the band's last line should be %r and is "
                 "%r - the glass was not captured" % (c, last,
                                                      live[-1:] or None))
        print("doswfs: 4 back in the window, frozen at line %d with %r last"
              % (c, last))

        # --- 5: thawed --------------------------------------------------------
        m.alt("Enter", hold=0.08)
        if not until(m, lambda: dos.b("dvw_fs") == 1, "the thaw", 20):
            fail("Alt+Enter on a frozen program did not take it back up")
        rising(m, dos, "the thawed program")
        print("doswfs: 5 Alt+Enter thawed it onto the full screen")

        # --- 6: graphics, and no way back -------------------------------------
        m.type_text("g")
        if not until(m, lambda: dos.word(0x105) == 1, "mode 4", 20):
            fail("`g` never reached the program on the full screen")
        if not dos.b("dvw_gfx"):
            fail("the program set mode 4 and [dvw_gfx] does not say so")
        m.alt("Enter", hold=0.08)
        os88marty.pace(m, 2)
        if bracket(m) == FSX_NONE or not dos.b("dvw_fs"):
            fail("Alt+Enter brought a GRAPHICS program back to the window")
        print("doswfs: 6 mode 4 on the glass, and Alt+Enter stays put")

        # --- 7: it ends on the full screen -----------------------------------
        m.type_text("x")
        if not until(m, lambda: dos.b("dvw_run") == 0, "the end", 30):
            fail("the program never ended after its key")
        if not until(m, lambda: bracket(m) == FSX_NONE, "the bracket down",
                     20):
            fail("the program ended and the bracket stayed up")
        os88marty.settle(m)
        rows = dos.rows()
        if not any("DWFS BACK" in r for r in rows):
            fail("the program's last screen is not in the console: %r"
                 % rows[-5:])
        if not any("exit code 120" in r for r in rows):
            fail("no `exit code 120` ('x') in the band: %r" % rows[-4:])
        print("doswfs: 7 it ended on the full screen; its last screen and "
              "exit code 120 are in the band")

        # --- 8: the shunt ----------------------------------------------------
        ui.raise_window(ui.window("DOS"))
        dosmap.run_in_window(ui)
        if not until(m, lambda: dos.b("dvw_run") == 1, "the second run", 20):
            fail("the second Run in Window put nothing on the worker")
        os88marty.pace(m, 0.5)              # its PSP is built: read from here
        rising(m, dos, "the second windowed run")
        if bracket(m) != FSX_NONE:
            fail("a bracket is up before anything asked for one")
        m.type_text("g")
        if not until(m, lambda: dos.word(0x105) == 1, "mode 4", 20):
            fail("`g` in the window never set the mode")
        if bracket(m) == FSX_NONE or not dos.b("dvw_fs"):
            fail("a windowed program set a GRAPHICS mode and no bracket came "
                 "up for it - the shunt (SPEC.md 96.54.1)")
        m.type_text("y")
        if not until(m, lambda: dos.b("dvw_run") == 0 and
                     bracket(m) == FSX_NONE, "the end", 30):
            fail("the shunted program never ended")
        os88marty.settle(m)
        if not any("exit code 121" in r for r in dos.rows()):
            fail("no `exit code 121` ('y') in the band: %r" % dos.rows()[-4:])
        print("doswfs: 8 `g` in the window shunted it to the full screen "
              "first; it ended with 121")


if __name__ == "__main__":
    main()
