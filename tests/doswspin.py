#!/usr/bin/env python3
"""doswspin - how fast a program runs in a DOS window (SPEC.md 96.54.3).

tests/dostrap/dwspin.asm counts into PSP:0103h as fast as it can and never
yields, so counts per GUEST second (MartyPC's cycle counter, 4.77 MHz) are the
share of the CPU it was given. It is measured over ~13.5 guest seconds in the
bracket - the whole machine - and again in a window, where it is pre-empted
and its world is moved in and out of the IVT on every tick. The ratio is the
window's cost, and the row holds it at FLOOR.

MEASURED, both on this row: 81.1% when the world was EXCHANGED a word at a
time (15.3% of samples in that loop), 88.7% with two blocks and `rep movsw`.
FLOOR is between them, so the old loop fails and the copies pass.

**Break it on purpose** (docs/WRITING-TESTS.md 1): the exchange loop of
96.54's first build reads 81.1% here, under the floor.
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
PROBE = os.path.join(ROOT, "tests", "dostrap", "dwspin.asm")
HZ = 4772727.0
FLOOR = 85.0


def fail(msg):
    print("doswspin: FAIL: %s" % msg)
    sys.exit(1)


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    tmp = tempfile.mkdtemp(prefix="doswspin")
    try:
        com = os.path.join(tmp, "DWSPIN.COM")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com, PROBE],
                       check=True)
        disk = os.path.join(tmp, "dwspin360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", com], check=True,
                       stdout=subprocess.DEVNULL)
        run(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswspin: ok")


def run(disk):
    with os88ui.boot(SYS, apps=disk, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        dm = dosmap.package()

        def count():
            base = dosmap.lin(m, dosmap.instance(m))
            psp = int.from_bytes(m.read(base + dm["dos_ldpsp"], 2), "little")
            return int.from_bytes(m.read(psp * 16 + 0x103, 4), "little")

        def rate(tag):
            m.pause()
            c0, y0 = count(), int(m.status()["cycles"])
            m.run()
            os88marty.pace(m, 3)
            m.pause()
            c1, y1 = count(), int(m.status()["cycles"])
            m.run()
            secs = (y1 - y0) / HZ
            if c1 <= c0:
                fail("%s: the probe did not count (%d -> %d)" % (tag, c0, c1))
            r = (c1 - c0) / secs
            print("doswspin: %-8s %8.0f counts a guest second, over %.2f s"
                  % (tag, r, secs))
            return r

        if not ui.path("B:/DWSPIN.COM"):
            fail("could not launch DWSPIN.COM")
        os88marty.pace(m, 2)
        full = rate("bracket")
        m.type_text("q")
        os88marty.pace(m, 3)
        os88marty.settle(m)
        ui.raise_window(ui.window("DOS"))
        dosmap.run_in_window(ui)
        os88marty.pace(m, 2)
        win = rate("window")
        pct = 100.0 * win / full
        m.type_text("q")
        print("doswspin: a window runs it at %.1f%% of the bracket's speed "
              "(floor %.0f%%)" % (pct, FLOOR))
        if pct < FLOOR:
            fail("%.1f%% is under the %.0f%% floor - the per-tick world "
                 "exchange has got dearer (SPEC.md 96.54.3)" % (pct, FLOOR))


if __name__ == "__main__":
    main()
