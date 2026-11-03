#!/usr/bin/env python3
"""doswleave - what a windowed DOS run puts back when it ends (SPEC.md
96.54.12): PIT channel 0, the 8259's mask, and the vectors it pointed into
its own memory.

Galaxy Trek (TREKRUN.EXE, 1983 compiled BASIC) closed in its window in the
middle of a note left channel 0 at 32 times the tick rate - its note ISR's
rate - for the rest of the session: the owner's 5150 went very slow, file
loads failed and the floppy motor stayed on. DWLEAVE.COM
(tests/dostrap/dwleave.asm) is that without the program: it takes IVT 08h and
IRQ3's vector DIRECTLY, sets channel 0 to divisor 2048 and unmasks IRQ3, and
on `q` masks IRQ4 (Trek's exit does) and exits leaving everything.

  1  typed at B:'s prompt with the mode ticked, `s` (spin), then the window
     CLOSED: the BIOS tick is back to its own rate, IVT 0Bh and the mask are
     what the launch found, and the Task Manager then opens off A: by
     DOUBLE-CLICK - whose window is ticks, so a fast channel 0 makes every
     double-click two first clicks - and drive A's motor goes off.
  2  a new DOS window, DWLEAVE and `q`: the program's OWN exit, with nothing
     put back by it - the same three checks, and the mouse (IRQ4) is live.

**Break it on purpose** (docs/WRITING-TESTS.md 1): with dvw_fin's `call
dvw_mend` taken out, leg 1 fails on the tick rate (~32x) and IVT 0Bh.
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
PROBE = os.path.join(ROOT, "tests", "dostrap", "dwleave.asm")


def fail(msg):
    print("doswleave: FAIL: %s" % msg)
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
        scr = dosmap.cellread(self.m, self.dm, self.base(), "con_scr",
                              80 * 25 * 2)
        return ["".join(chr(scr[r * 160 + i]) if 32 <= scr[r * 160 + i] < 127
                        else " " for i in range(0, 160, 2)).rstrip()
                for r in range(25)]


def until(m, cond, what, limit):
    try:
        os88marty.until(m, lambda _m: cond(), what, poll=0.2, limit=limit)
        return True
    except os88marty.MartyError:
        return False


def rate(m, secs=1.0):
    """BIOS ticks (0040:006C) in `secs` GUEST seconds"""
    t0 = int.from_bytes(m.read(0x46C, 4), "little")
    os88marty.guest_sleep(m, secs)
    return (int.from_bytes(m.read(0x46C, 4), "little") - t0) / secs


def vec(m, n):
    return bytes(m.read(n * 4, 4))


def launch(m, dos, ui, what):
    dosmap.set_wmode(ui, True)
    os88marty.settle(m)
    m.type_text("B:\n")
    os88marty.settle(m)
    base = {"v0b": vec(m, 0x0B), "imr": m.inb(0x21), "rate": rate(m)}
    m.type_text("DWLEAVE\n")
    if not until(m, lambda: any("DWLEAVE ON" in r for r in dos.rows()),
                 "DWLEAVE ON", 30):
        fail("%s: DWLEAVE printed nothing: %r"
             % (what, [r for r in dos.rows() if r.strip()][-4:]))
    if dos.b("dos_inbr"):
        fail("%s: DWLEAVE took the full screen with the mode ticked" % what)
    fast = rate(m)
    if fast < 4 * base["rate"]:
        fail("%s: channel 0 never went fast (%.0f ticks a second against "
             "%.0f) - the probe did not take" % (what, fast, base["rate"]))
    return base, fast


def restored(m, ui, base, what, dosw=None):
    r = rate(m)
    v0b = vec(m, 0x0B)
    imr = m.inb(0x21)
    if r > 2 * base["rate"]:
        fail("%s: the BIOS tick runs at %.0f a second against %.0f before "
             "the run - channel 0 was left as the program set it"
             % (what, r, base["rate"]))
    if v0b != base["v0b"]:
        fail("%s: IVT 0Bh is %s against %s at the launch - IRQ3's vector "
             "still points into the freed arena"
             % (what, v0b.hex(), base["v0b"].hex()))
    if imr != base["imr"]:
        fail("%s: the 8259's mask is %02X against %02X at the launch"
             % (what, imr, base["imr"]))
    if dosw is not None:
        ui.close(dosw, limit=60)            # the drive zone is under it
    try:
        w = ui.path("A:/SYSTEM/TASKMGR.O88", limit=60)
    except Exception as e:                          # noqa: BLE001
        fail("%s: the Task Manager did not open off A: afterwards: %s"
             % (what, e))
    if not until(m, lambda: m.read(0x43F, 1)[0] & 0x0F == 0,
                 "drive A's motor off", 15):
        fail("%s: the floppy motor is still on (0040:003F = %02X)"
             % (what, m.read(0x43F, 1)[0]))
    ui.close(w)
    print("doswleave: %s: tick %.0f/s (it ran at %.0f), IVT 0Bh and the "
          "mask %02X as launched, the Task Manager opened and the motor "
          "went off" % (what, r, base["fast"], imr))


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    tmp = tempfile.mkdtemp(prefix="doswleave")
    try:
        com = os.path.join(tmp, "DWLEAVE.COM")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com, PROBE],
                       check=True)
        disk = os.path.join(tmp, "dwleave360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", "--no-pack", com],
                       check=True, stdout=subprocess.DEVNULL)
        run(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswleave: ok")


def run(disk):
    with os88ui.boot(SYS, apps=disk, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        # --- 1: closed while it runs ----------------------------------------
        if not ui.path("A:/APPS/DOS.O88"):
            fail("could not open A:/APPS/DOS.O88")
        dos = Dos(m)
        os88marty.settle(m)
        base, base["fast"] = launch(m, dos, ui, "the close")
        m.type_text("s")
        os88marty.guest_sleep(m, 1.0)
        ui.close(ui.window("DOS"), limit=60)
        restored(m, ui, base, "1 closed while it ran")

        # --- 2: its own exit, leaving everything ----------------------------
        if not ui.path("A:/APPS/DOS.O88"):
            fail("could not open A:/APPS/DOS.O88 a second time")
        dos = Dos(m)
        os88marty.settle(m)
        base, base["fast"] = launch(m, dos, ui, "the exit")
        m.type_text("q")
        if not until(m, lambda: dos.b("dvw_run") == 0, "the exit", 30):
            fail("the exit: DWLEAVE never ended ([dvw_run] %d)"
                 % dos.b("dvw_run"))
        os88marty.settle(m)
        restored(m, ui, base, "2 its own exit", ui.window("DOS"))


if __name__ == "__main__":
    main()
