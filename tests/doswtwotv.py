#!/usr/bin/env python3
"""doswtwotv - TWO TopView programs in two DOS windows on a LONE HERCULES,
each drawing in its OWN window (SPEC.md 96.54.13).

Reported off the owner's 5150 (86Box pc5150, a lone Hercules): WordPerfect
4.1 in one DOS window worked; a second copy in a second window drew nothing.
The first window's run held the Hercules's own page 1 (96.54.6.1), whose
decode its slices leave on, so the second window's capture test took that
page for a CGA, handed its `int 10h` to the ROM, and the ROM - which picks
its segment from the equipment word - drew the second program into B000,
the desktop's framebuffer.

tests/dostrap/dwtvb.asm is WordPerfect's shape: it asks `int 10h AH=FEh`
once and, told its own B800 back, falls to the video BIOS.

  1  two DOS windows, each with DWTVB.COM in its path box (a double-click
     opens each and runs it once in the bracket; `q` ends it)
  2  Run in Window in both: both on their workers
  3  EACH is a TopView window ([dvw_cmode] = DVWC_TV) and was told a buffer
     of its own (PSP:0105h is not B800), the first holding the page and the
     second refused it
  4  each window's console shows its own `DWTB` lines, and the desktop's
     framebuffer (B000, 32 KB) carries no `DWTB` text
  5  `q` into each window ends its own program

**Break it on purpose** (docs/WRITING-TESTS.md 1): with the holder check in
`dvw_cdetect` taken out, leg 3 fails - the second window reads DVWC_SEG and
its program was told B800.
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
PROBE = os.path.join(ROOT, "tests", "dostrap", "dwtvb.asm")
DVWC_TV = 1
MARK = b"D\x07W\x07T\x07B\x07"


def fail(msg):
    print("doswtwotv: FAIL: %s" % msg)
    sys.exit(1)


class Dos(object):
    def __init__(self, m, slot, dm):
        self.m, self.slot, self.dm = m, slot, dm

    def base(self):
        return dosmap.lin(self.m, dosmap.instance(self.m, self.slot))

    def b(self, name):
        return dosmap.cellread(self.m, self.dm, self.base(), name, 1)[0]

    def w(self, name):
        return int.from_bytes(dosmap.cellread(self.m, self.dm, self.base(),
                                              name, 2), "little")

    def rows(self):
        scr = self.m.read(self.base() + self.dm["con_scr"], 4000)
        return ["".join(chr(scr[r * 160 + i]) if 32 <= scr[r * 160 + i] < 127
                        else " " for i in range(0, 160, 2)).rstrip()
                for r in range(25)]

    def psp(self, off, n=2):
        return int.from_bytes(self.m.read(self.w("dos_ldpsp") * 16 + off, n),
                              "little")


def until(m, cond, what, limit):
    try:
        os88marty.until(m, lambda _m: cond(), what, poll=0.2, limit=limit)
        return True
    except os88marty.MartyError:
        return False


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    tmp = tempfile.mkdtemp(prefix="doswtwotv")
    try:
        com = os.path.join(tmp, "DWTVB.COM")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com, PROBE],
                       check=True)
        disk = os.path.join(tmp, "dwtvb360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", com], check=True,
                       stdout=subprocess.DEVNULL)
        run(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswtwotv: ok")


def bracket_once(ui, m, first):
    if not first:
        ui.raise_window(ui.window("Disk"))
    if not ui.path("B:/DWTVB.COM" if first else "DWTVB.COM"):
        fail("could not launch DWTVB.COM")
    os88marty.pace(m, 2)
    m.type_text("q")
    os88marty.pace(m, 3)
    os88marty.settle(m)


def run(disk):
    with os88ui.boot(SYS, apps=disk, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        dm = dosmap.package()

        # --- 1: two DOS windows ---------------------------------------------
        bracket_once(ui, m, True)
        bracket_once(ui, m, False)
        wins = sorted([w for w in ui.windows() if w.title == "DOS"],
                      key=lambda w: w.i)
        if len(wins) != 2:
            fail("two double-clicks should leave two DOS windows; there are "
                 "%d" % len(wins))
        dd = (Dos(m, 0, dm), Dos(m, 1, dm))
        print("doswtwotv: 1 two DOS windows, each named DWTVB.COM")

        # --- 2: both in their windows ----------------------------------------
        for w, d in zip(wins, dd):
            ui.raise_window(w)
            dosmap.run_in_window(ui)
            if not until(m, lambda: d.b("dvw_run") == 1 and d.psp(0x103) > 2,
                         "a run, counting", 30):
                fail("Run in Window in DOS window %d did not start the probe "
                     "([dos_err] %d)" % (d.slot, d.b("dos_err")))
        print("doswtwotv: 2 both run in their windows")

        # --- 3: each a TopView window, each told a buffer of its own --------
        for d in dd:
            cm = d.b("dvw_cmode")
            vs = d.psp(0x105)
            if cm != DVWC_TV or vs == 0xB800:
                fail("DOS window %d is [dvw_cmode] %d and its program was "
                     "told segment %04X - a second window took the first's "
                     "capture card for its own (SPEC.md 96.54.13)"
                     % (d.slot, cm, vs))
        holds = [d.b("dvw_chp") for d in dd]
        if holds != [1, 0]:
            fail("the first window should hold the Hercules's page and the "
                 "second be refused it; [dvw_chp] reads %r" % holds)
        print("doswtwotv: 3 both TopView windows (buffers %04X, %04X), the "
              "page held by the first alone"
              % (dd[0].psp(0x105), dd[1].psp(0x105)))

        # --- 4: each its own lines; none on the desktop ----------------------
        for d in dd:
            n0 = d.psp(0x103)
            until(m, lambda: d.psp(0x103) > n0 + 3, "more lines", 20)
            if not any(r.startswith("DWTB") for r in d.rows()):
                fail("DOS window %d's console shows none of its program's "
                     "lines: %r" % (d.slot, d.rows()[:6]))
        fb = m.read(0xB0000, 0x8000)
        if MARK in fb:
            fail("`DWTB` text is in the desktop's framebuffer at B000:%04X - "
                 "a program's int 10h reached the ROM" % fb.index(MARK))
        print("doswtwotv: 4 each window shows its own lines, the desktop "
              "untouched")

        # --- 5: each ends, and the page is given back ------------------------
        for w, d in zip(wins, dd):
            ui.raise_window(w)
            m.type_text("q")
            if not until(m, lambda: d.b("dvw_run") == 0, "the end", 20):
                fail("`q` did not end DOS window %d's program" % d.slot)
        os88marty.settle(m)
        print("doswtwotv: 5 both ended")


if __name__ == "__main__":
    main()
