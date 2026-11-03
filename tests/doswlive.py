#!/usr/bin/env python3
"""doswlive - a DOS program running LIVE in its window (SPEC.md 96.54).

docs/plans/DOS-WINDOWED-PLAN.md wave 1. `Program > Run in Window` runs the
named program on the DOS instance's worker instead of in the fullscreen
bracket (SPEC.md 96.2), so the scheduler pre-empts it and its text reaches the
console band AS IT RUNS. The probe, tests/dostrap/dwtick.asm, never yields on
its own: it waits by spinning on the BIOS tick, so everything below that needs
the rest of the machine to run is a statement about pre-emption.

  1  the probe runs once in the BRACKET, the way a double-click runs it -
     which is the user's own flow, and leaves the program in the path box
  2  Run in Window: lines arrive in the band WHILE IT RUNS (more lines on a
     later read than an earlier one, with the program not yet finished) -
     through INT 21h AND through the video BIOS's teletype
  3  ...and the desktop is not frozen behind it: another window comes to
     the front while it ticks, and it goes on printing behind it
  4  the key it waits for comes from the WINDOW's keys; what it prints next
     is PAINTED while it waits for the second, with nothing else repainting
     the window (the Prince of Persia installer report); and the second key
     is its exit code
  5  ...and when it has gone, IVT 08h is the kernel's again and the DOS
     block is not in the table: the world was exchanged back out (96.54)
  6  a second run, closed WHILE IT SPINS: the window goes, the machine keeps
     ticking, and the whole IVT is what it was before the run

**Break it on purpose** (docs/WRITING-TESTS.md 1): with `dvw_start` made to
answer CF=1 the run falls back to the bracket and leg 2's "while it runs"
read finds the band unchanged until the end; with the exchange skipped in
`dvw_fin` leg 5 reads dvw_irq0 still at 08h (done: `IVT 08h is 07510993 after
the run and was 5b396000`). THE MAKEFILE RULE MUST LIST apps/dos/dosw.inc:
before it did, an edit there rebuilt nothing, the row ran yesterday's DOS
against a map of today's source and failed at leg 1 on an EMPTY band - a
one-byte shift of every bss cell - which read exactly like a parity bug.
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
PROBE = os.path.join(ROOT, "tests", "dostrap", "dwtick.asm")
LINES = 12


def fail(msg):
    print("doswlive: FAIL: %s" % msg)
    sys.exit(1)


class Dos(object):
    """The live instance's console, read per access (tests/doscon.py's
    reason: the region moves)."""

    def __init__(self, m):
        self.m = m
        self.dm = dosmap.package()

    def base(self):
        return dosmap.lin(self.m, dosmap.instance(self.m))

    def b(self, name):
        return dosmap.cellread(self.m, self.dm, self.base(), name, 1)[0]

    def rows(self):
        scr = self.m.read(self.base() + self.dm["con_scr"], 80 * 25 * 2)
        out = []
        for r in range(25):
            row = scr[r * 160:(r + 1) * 160]
            out.append("".join(chr(row[i]) if 32 <= row[i] < 127 else " "
                               for i in range(0, 160, 2)).rstrip())
        return out

    def count(self):
        """Lines the probe has printed: its own word at PSP:0103h."""
        psp = int.from_bytes(self.m.read(self.base() + self.dm["dos_ldpsp"],
                                         2), "little")
        return int.from_bytes(self.m.read(psp * 16 + 0x103, 2), "little")

    def ticks(self):
        return [r for r in self.rows() if r.startswith("DWTICK ")
                and ("DOS" in r or "BIOS" in r)]


def until(m, cond, what, limit):
    try:
        os88marty.until(m, lambda _m: cond(), what, poll=0.25, limit=limit)
        return True
    except os88marty.MartyError:
        return False


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    tmp = tempfile.mkdtemp(prefix="doswlive")
    try:
        com = os.path.join(tmp, "DWTICK.COM")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com, PROBE],
                       check=True)
        disk = os.path.join(tmp, "dwtick360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", com], check=True,
                       stdout=subprocess.DEVNULL)
        run(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswlive: ok")


def run(disk):
    with os88ui.boot(SYS, apps=disk, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        k08 = m.read(0x20, 4)

        # --- 1: the bracket, as a double-click runs it ------------------------
        if not ui.path("B:/DWTICK.COM"):
            fail("could not launch DWTICK.COM")
        dos = Dos(m)
        if not until(m, lambda: any("PRESS A KEY" in r
                                    for r in (m.screen() or [])),
                     "the bracketed run's prompt", 60):
            fail("DWTICK.COM never asked for its key inside the bracket")
        m.type_text("xy")
        if not until(m, lambda: not dos.b("dos_inbr"), "the bracket down", 48):
            fail("the bracket never came down")
        os88marty.settle(m)
        if not any("exit code 121" in r for r in dos.rows()):
            fail("the bracketed run's exit line is not in the band: %r"
                 % dos.rows()[-4:])
        print("doswlive: 1 the bracket ran it, exit code 121")

        # --- 2: Run in Window, and lines arrive WHILE it runs ----------------
        w = ui.window("DOS")
        ui.raise_window(w)
        dosmap.run_in_window(ui)
        if not until(m, lambda: dos.b("dvw_run") == 1, "the worker's run", 20):
            fail("Run in Window did not put a program on the worker "
                 "([dvw_run] never 1)")
        if not until(m, lambda: dos.count() >= 3, "three lines", 30):
            fail("a program on the worker printed %d lines in 30 guest "
                 "seconds - the band holds %r" % (dos.count(), dos.rows()[-6:]))
        mid = dos.count()
        last = [r for r in dos.rows() if r.strip()][-1]
        if not last.startswith("DWTICK "):
            fail("the probe has printed %d lines and the band's last line is "
                 "%r: the output is not reaching the window as it is printed"
                 % (mid, last))
        if not dos.b("dvw_run"):
            fail("the program finished before its output could be seen "
                 "arriving, so this read proves nothing about LIVE")
        print("doswlive: 2 %d of %d lines in the band while it runs"
              % (mid, LINES))

        # --- 3: the desktop runs beside it ----------------------------------
        # Leg 1's ui.path left B:'s Disk window open, so the leg RAISES it -
        # a repaint by the UI task, confirmed by the window order - and then
        # reads the band again: still running and still advancing.
        ui.raise_window(ui.window("Disk"))
        if not dos.b("dvw_run"):
            fail("the program ended while the Disk window was raised - the "
                 "leg must overlap a running program to say anything")
        n3 = dos.count()
        if not until(m, lambda: dos.count() > n3, "a line after the raise",
                     20):
            fail("no line arrived after the Disk window was raised")
        print("doswlive: 3 the Disk window came to the front while it ran, "
              "and it kept printing")

        # --- 4: its key, from the window ------------------------------------
        if not until(m, lambda: any("PRESS A KEY" in r for r in dos.rows()),
                     "the windowed prompt", 60):
            fail("the windowed run never reached its key prompt: %r"
                 % dos.rows()[-6:])
        got = dos.ticks()
        if not any("BIOS" in r for r in got) or not any("DOS" in r
                                                       for r in got):
            fail("the band holds only one of the two output paths: %r" % got)
        ui.raise_window(ui.window("DOS"))
        m.type_text("q")
        if not until(m, lambda: any("DWTICK AGAIN" in r for r in dos.rows()),
                     "the second question", 30):
            fail("the probe never asked its second question after 'q': %r"
                 % dos.rows()[-4:])
        # **AND IT REACHED THE GLASS WITHOUT ANYTHING ELSE REPAINTING** -
        # the owner's report off Prince of Persia's installer: after the first
        # answer the window "only updates after dragging it". A program that
        # prints and then waits on a key spends every tick asleep, so the
        # paint dvw_irq0 posts never came; dvw_nap posts it now. The marks are
        # the witness: con_drb is what a paint SPENDS.
        drb = lambda: m.read(dos.base() + dos.dm["con_drb"], 4)
        if not until(m, lambda: drb() == b"\0\0\0\0", "the band painted",
                     10):
            fail("the second question is in the console and its rows are "
                 "still marked dirty (%s) with the program waiting on a key: "
                 "nobody posted the paint" % drb().hex())
        m.type_text("z")
        if not until(m, lambda: dos.b("dvw_run") == 0, "the program's end",
                     30):
            fail("the windowed program never ended after its second key")
        os88marty.settle(m)
        if not any("exit code 122" in r for r in dos.rows()):
            fail("exit code 122 ('z') is not in the band - the key did not "
                 "reach the program's INT 21h AH=08h: %r" % dos.rows()[-4:])
        print("doswlive: 4 its second question was painted while it waited, "
              "and the window's 'z' was its exit code, 122")

        # --- 5: the world went back out --------------------------------------
        # SAMPLED, because a world left in is not a constant: the stub swaps
        # it out and back around every tick it catches, so one read can land
        # on an "out" and pass. Twelve reads across three guest seconds of an
        # idle worker see it either way it is wrong.
        for i in range(12):
            got = m.read(0x20, 4)
            if got != k08:
                fail("IVT 08h is %s after the run (read %d) and was %s before "
                     "it: the instance's stub is still installed"
                     % (got.hex(), i, k08.hex()))
            if dos.b("dvw_in"):
                fail("[dvw_in] says the instance's world is in the IVT after "
                     "the program has gone (read %d)" % i)
            os88marty.pace(m, 0.25)
        print("doswlive: 5 IVT 08h is the kernel's again, twelve reads over "
              "three guest seconds")

        # --- 6: closing the window STOPS a running program ------------------
        # dvw_onclose refuses, the next tick in program code rewrites the
        # interrupted CS:IP to `mov ax, 4CFFh / int 21h`, and dvw_wake asks
        # for the close once the run has ended (SPEC.md 96.54). Closed WHILE
        # IT SPINS IN ITS OWN CODE - the foreign-stack arm. The order is the
        # point: letting the close through freed the region under the UI
        # task, and the machine ran off into the heap with the ticks stopped.
        ivt = m.read(0, 1024)
        ui.raise_window(ui.window("DOS"))
        dosmap.run_in_window(ui)
        if not until(m, lambda: dos.count() >= 2, "two lines", 30):
            fail("the second windowed run printed nothing")
        ui.close(ui.window("DOS"))          # raises if it never goes
        t0 = int.from_bytes(m.read(0x46C, 2), "little")
        os88marty.pace(m, 1.0)
        dt = int.from_bytes(m.read(0x46C, 2), "little") - t0
        if dt < 20:
            fail("the BIOS tick moved %d in a guest second after the close - "
                 "the machine stopped when the instance went" % dt)
        left = [v for v in range(256)
                if m.read(v * 4, 4) != ivt[v * 4:v * 4 + 4]]
        if left:
            fail("vectors %s differ from before the run, with no DOS left to "
                 "own them" % ["%02Xh" % v for v in left])
        print("doswlive: 6 closing the window stopped it; the tick runs on "
              "(%d a guest second) and the IVT is as it was" % dt)


if __name__ == "__main__":
    main()
