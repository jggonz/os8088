#!/usr/bin/env python3
"""doswmode - `Run in Window` is a MODE, and the prompt stays in the window
(SPEC.md 96.54.10, and 96.33.6.1's refused drive).

The owner's requirement: *"setting the mode to 'run these in window', then
being able to use the command prompt to run (multiple) utilities while still
in the window."* It shipped as a one-shot - the menu item ran the path box's
program, and with the box empty it left one pending request, so the FIRST
typed utility ran in the window and the second took the whole screen.

The probes are on A: (a scratch copy of the 360KB system disk) and B: is
EMPTY, which is what leg 7 needs and what costs the rest nothing.

  1  a fresh DOS window opens on its prompt with the mode CLEARED
  2  Program > Run in Window TICKS it (`*Run in Window`) and runs nothing
  3  DWFS typed at the prompt runs IN THE WINDOW (on the worker, no bracket),
     the band SCROLLING, the folder's window is DRAGGED over it (the drag
     finishing is itself a check: the lock is not held by the band), DWFS
     counts on, not one pixel of that window - now in FRONT - changes under
     the band's paint, and the band is OWED; raised, the DOS window draws
     what it owes; `q` ends it and the next prompt is in the band
  4  ...and a SECOND, different program typed next runs in the window too
     (DWTICK, its two keys its exit code) - the leg today's one-shot fails
  5  DIR, a built-in, still answers in the band, and no run starts for it
  6  a graphics mode moves the live program to the full screen (96.54.1);
     when it ends there the prompt is back IN THE WINDOW, the mode still
     ticked, and the next typed program runs in the window again
  7  the mode CLEARED: the same typed program takes the bracket
  8  `B:` with no floppy in B: says `Invalid drive specification` and the
     prompt is still `A:\\>`, and DIR still lists A: (96.33.6.1)

**Break it on purpose** (docs/WRITING-TESTS.md 1), both measured: on the
one-shot (f4c112c6, the menu item `inc [dvw_want]` and falling into Run) leg
2 fails - the item does not tick a mode (`the item reads False`); with
dvw_wake's OSAPI_WM_CLIP_SET taken out, leg 3 fails with 788 bytes of the
window in front changed under the band; with only the clip and not the owe
(`OSAPI_WM_OBSCURED` replaced by a `clc`), leg 3's drag cannot finish - the
band's full redraws hold the gfx lock; with dos_con_run's `call dsh_home` replaced by a `nop`, leg 8 fails with
`[dos_vol] 1` - the refused B: moved the prompt.
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
PROBES = ("dwfs", "dwtick")
FSX_NONE = 0xFF
CK_ON = 10                          # OS88UI_CK_ON (apps/os88ui.inc)
SCROLLED = 26                       # DWFS lines: the 25-row band has scrolled


def fail(msg):
    print("doswmode: FAIL: %s" % msg)
    sys.exit(1)


class Dos(object):
    def __init__(self, m):
        self.m = m
        self.dm = dosmap.package()

    def base(self):
        return dosmap.lin(self.m, dosmap.instance(self.m))

    def b(self, name):
        return dosmap.cellread(self.m, self.dm, self.base(), name, 1)[0]

    def mode(self):
        return self.m.read(self.base() + self.dm["dos_mwin"] + CK_ON, 1)[0]

    def live(self):
        scr = self.m.read(self.base() + self.dm["con_scr"], 80 * 25 * 2)
        rows = ["".join(chr(scr[r * 160 + i]) if 32 <= scr[r * 160 + i] < 127
                        else " " for i in range(0, 160, 2)).rstrip()
                for r in range(25)]
        return [r for r in rows if r.strip()]

    def count(self):
        psp = int.from_bytes(self.m.read(self.base() + self.dm["dos_ldpsp"],
                                         2), "little")
        return int.from_bytes(self.m.read(psp * 16 + 0x103, 2), "little")


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
    tmp = tempfile.mkdtemp(prefix="doswmode")
    try:
        disk = os.path.join(tmp, "sys360.img")
        shutil.copyfile(SYS, disk)
        for p in PROBES:
            com = os.path.join(tmp, p.upper() + ".COM")
            subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com,
                            os.path.join(ROOT, "tests", "dostrap",
                                         p + ".asm")], check=True)
            subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                         "os88fat.py"),
                            "add", disk, com], check=True,
                           stdout=subprocess.DEVNULL)
        run(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswmode: ok")


def window_pixels(m, w):
    """A window's content below its title bar, out of Hercules VRAM - the
    pointer rests on the title bar after a drag, so it is not in here."""
    _w, _h, rows = m.vram("herc")
    return b"".join(bytes(rows[y][w.x + 2:w.x + w.w - 2])
                    for y in range(w.y + 20, min(w.y + w.h - 2, 348)))


def prompt_back(m, dos, what):
    if not until(m, lambda: dos.b("dvw_run") == 0 and not dos.b("dos_inbr")
                 and dos.live() and dos.live()[-1] == "A:\\>",
                 "%s's prompt" % what, 30):
        fail("%s: no A:\\> prompt in the band afterwards ([dvw_run] %d, "
             "[dos_inbr] %d): %r" % (what, dos.b("dvw_run"),
                                     dos.b("dos_inbr"), dos.live()[-3:]))
    if bracket(m) != FSX_NONE:
        fail("%s: the prompt is back and a bracket is still up (%02X)"
             % (what, bracket(m)))


def in_window(m, dos, cmd, what):
    m.type_text(cmd + "\n")
    if not until(m, lambda: dos.b("dvw_run") == 1 or dos.b("dos_inbr"),
                 "%s to start" % what, 30):
        fail("%s: typing %s started nothing: %r" % (what, cmd,
                                                    dos.live()[-3:]))
    if dos.b("dos_inbr") or not dos.b("dvw_run"):
        fail("%s: %s typed at the prompt took the FULL SCREEN ([dos_inbr] "
             "%d, [dvw_run] %d) with the mode ticked - the mode is a one-shot"
             % (what, cmd, dos.b("dos_inbr"), dos.b("dvw_run")))


def run(disk):
    with os88ui.boot(disk, apps=None, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        if not ui.path("A:/APPS/DOS.O88"):
            fail("could not open A:/APPS/DOS.O88")
        dos = Dos(m)
        os88marty.settle(m)

        # --- 1: a fresh window, the mode cleared ----------------------------
        if not dos.live() or dos.live()[-1] != "A:\\>":
            fail("DOS should open on A:\\> and reads %r" % dos.live()[-2:])
        try:
            first = dosmap.wmode(ui)
        except RuntimeError as e:
            fail(str(e))
        if first is not False or dos.mode():
            fail("a new DOS window should start with the mode CLEARED: the "
                 "item reads %r and the box's byte is %d" % (first, dos.mode()))
        print("doswmode: 1 a new DOS window opens on A:\\> with the mode "
              "cleared")

        # --- 2: the menu ticks it, and runs nothing -------------------------
        try:
            dosmap.set_wmode(ui, True)
        except RuntimeError as e:
            fail("%s - Program > Run in Window is not a mode (the item "
                 "reads %r)" % (e, dosmap.wmode(ui)))
        os88marty.settle(m)
        if dos.mode() != 1 or dos.b("dvw_run") or dos.b("dvw_want"):
            fail("the toggle should set the box's byte and nothing else: "
                 "mode %d, [dvw_run] %d, [dvw_want] %d"
                 % (dos.mode(), dos.b("dvw_run"), dos.b("dvw_want")))
        print("doswmode: 2 Program > Run in Window ticks the mode and runs "
              "nothing")

        # --- 3: a typed utility in the window, the desktop live -------------
        in_window(m, dos, "DWFS", "the first program")
        # SCROLLING before the drag: a scroll under a cover is the case the
        # clip alone got wrong (the lock held 99%, so the drag never ended)
        if not until(m, lambda: dos.count() >= SCROLLED, "DWFS scrolling", 40):
            fail("DWFS is on the worker and does not count (%d)" % dos.count())
        disk_w = [w for w in ui.windows() if w.title != "DOS"][0]
        x0, y0 = disk_w.x, disk_w.y
        try:
            ui.drag_window(disk_w, -96, 24)     # over the lines DWFS prints
        except os88ui.UIError as e:
            fail("the %s window could not be dragged over the scrolling band "
                 "- the band's paint is holding the desktop: %s"
                 % (disk_w.title, e))
        moved = ui.window(disk_w.title)
        if (moved.x, moved.y) == (x0, y0):
            fail("the %s window did not move while DWFS ran" % disk_w.title)
        c = dos.count()
        before = window_pixels(m, moved)
        if not until(m, lambda: dos.count() > c + 1, "DWFS after the drag", 20):
            fail("DWFS stopped counting while another window was dragged")
        after = window_pixels(m, moved)
        if before != after:
            fail("DWFS's band painted over the %s window in FRONT of it: "
                 "%d of its content bytes changed while it lay over the band"
                 % (moved.title, sum(1 for a, b in zip(before, after)
                                     if a != b)))
        if not dos.b("dvw_owe"):
            fail("DWFS scrolled under the %s window and the band is not "
                 "OWED ([dvw_owe] 0)" % moved.title)
        ui.raise_window(ui.window("DOS"))
        if not until(m, lambda: not dos.b("dvw_owe"), "the owed band", 20):
            fail("the DOS window is in front again and the owed band was "
                 "never drawn - dos_paint's kick?")
        m.type_text("q")
        prompt_back(m, dos, "DWFS")
        if not any("exit code 113" in r for r in dos.live()[-3:]):
            fail("DWFS's exit line is not in the band: %r" % dos.live()[-3:])
        print("doswmode: 3 DWFS ran in the window; the %s window moved "
              "%d,%d -> %d,%d while it counted; the prompt is back"
              % (disk_w.title, x0, y0, moved.x, moved.y))

        # --- 4: ...and the next one too -------------------------------------
        in_window(m, dos, "DWTICK", "the second program")
        if not until(m, lambda: any("key" in r.lower() for r in dos.live()[-2:])
                     or dos.count() >= 12, "DWTICK's question", 40):
            fail("DWTICK never got to its question: %r" % dos.live()[-3:])
        os88marty.pace(m, 2)
        m.type_text("xy")
        prompt_back(m, dos, "DWTICK")
        if not any("exit code 121" in r for r in dos.live()[-3:]):
            fail("DWTICK should end with exit code 121 ('y'): %r"
                 % dos.live()[-3:])
        if dos.mode() != 1:
            fail("the mode was cleared by a run")
        print("doswmode: 4 a second, different program typed next ran in the "
              "window too, and the mode stands")

        # --- 5: a built-in, in the band -------------------------------------
        m.type_text("DIR\n")
        if not until(m, lambda: "File(s)" in dos.live()[-2] and
                     any(r.startswith("DWTICK") for r in dos.live()) and
                     dos.live()[-1] == "A:\\>", "DIR's listing", 20):
            fail("DIR did not list A: into the band: %r" % dos.live()[-6:])
        if dos.b("dvw_run") or dos.b("dos_inbr"):
            fail("DIR started a run")
        foot = dos.live()[-2]               # A:'s, which leg 8 asks again
        print("doswmode: 5 DIR answered in the band")

        # --- 6: a graphics mode moves it, and the prompt comes home ---------
        in_window(m, dos, "DWFS", "the shunted program")
        os88marty.pace(m, 1)
        m.type_text("g")
        if not until(m, lambda: dos.b("dvw_fs") and bracket(m) != FSX_NONE,
                     "the full screen", 30):
            fail("`g` (a graphics mode) did not move DWFS to the full screen")
        os88marty.pace(m, 1)
        m.type_text("z")
        prompt_back(m, dos, "the shunted DWFS")
        if dos.mode() != 1:
            fail("a run that ended on the full screen cleared the mode")
        in_window(m, dos, "DWFS", "the program after the shunt")
        m.type_text("q")
        prompt_back(m, dos, "DWFS after the shunt")
        print("doswmode: 6 a graphics mode took the full screen; the prompt "
              "came back in the window and the next program ran there")

        # --- 7: cleared, the bracket ----------------------------------------
        dosmap.set_wmode(ui, False)
        m.type_text("DWFS\n")
        if not until(m, lambda: dos.b("dos_inbr"), "the bracket", 30):
            fail("with the mode cleared DWFS did not take the full screen "
                 "([dvw_run] %d)" % dos.b("dvw_run"))
        if dos.b("dvw_run"):
            fail("with the mode cleared DWFS is on the worker as well")
        os88marty.pace(m, 1)
        m.type_text("q")
        prompt_back(m, dos, "the bracketed DWFS")
        print("doswmode: 7 the mode cleared, the typed program took the "
              "bracket")

        # --- 8: a drive with no floppy does not move the prompt -------------
        m.type_text("B:\n")
        if not until(m, lambda: any("Invalid drive specification" in r
                                    for r in dos.live()[-2:]),
                     "B:'s refusal", 60):
            fail("B: with no floppy said nothing: %r" % dos.live()[-3:])
        os88marty.settle(m)
        if dos.live()[-1] != "A:\\>" or dos.b("dos_vol") != 0:
            fail("a refused B: moved the prompt: %r, [dos_vol] %d"
                 % (dos.live()[-1], dos.b("dos_vol")))
        m.type_text("DIR\n")
        if not until(m, lambda: dos.live()[-2] == foot and
                     dos.live()[-1] == "A:\\>", "DIR on A:", 30):
            fail("DIR after the refused B: did not list A:: %r"
                 % dos.live()[-6:])
        print("doswmode: 8 B: with no floppy was refused and the prompt "
              "stayed on A:")


if __name__ == "__main__":
    main()
