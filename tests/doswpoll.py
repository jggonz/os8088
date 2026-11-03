#!/usr/bin/env python3
"""doswpoll - a keystroke answered the way WordPerfect answers one, in a DOS
window and on the full screen (SPEC.md 96.54.5.1).

tests/dostrap/dwpoll.asm is WordPerfect 4.1's main loop with nothing else in
it: TopView's AH=FEh once, then for each key a burst of 2,000 passes, each an
INT 16h AH=01h poll, a cell of the buffer written and an INT 10h AH=FFh for
the WHOLE screen - then the key at row 0 and the word at PSP:0103h counted.
A key's LATENCY is guest cycles (MartyPC's counter, 4.77 MHz) from the key
to that count, taken for KEYS keys in the bracket and again in a window, and
the row holds the window's median at no more than RATIO times the bracket's.

MEASURED (MartyPC os8088_5150_herc_gla): see SPEC.md 96.54.5.1's table.
Before that section the window took the pass to its knees twice over: every
eighth empty AH=01h slept a tick, so 2,000 passes were ~14 s of sleeping, and
every AH=FFh marked all 25 rows. WordPerfect read
17 s a key there against 0.15 s on the full screen.

**Break it on purpose** (docs/WRITING-TESTS.md 1): MEASURED red on the tree
before 96.54.5.1 - 13.8 s a key in the window against 0.45 s, 30.7x. Either
half alone is DERIVED red, not measured: without DVW_KBUSY the 2,000 passes
run 8 a tick (~14 s); without the one-row cut in .tvupd each pass marks 25
rows (~25,000 cycles, ~10 s).

It also checks that the keys reached con_scr (TopView's buffer IS the
console's cells, which the band is drawn from) and that the window is in
TopView's capture mode.
"""

import os
import shutil
import statistics
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
PROBE = os.path.join(ROOT, "tests", "dostrap", "dwpoll.asm")
HZ = 4772727.0
KEYS = "abc"
RATIO = 3.0
LIMIT = 40.0            # guest seconds a key may take before it is a FAIL


def fail(msg):
    print("doswpoll: FAIL: %s" % msg)
    sys.exit(1)


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    tmp = tempfile.mkdtemp(prefix="doswpoll")
    try:
        com = os.path.join(tmp, "DWPOLL.COM")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com, PROBE],
                       check=True)
        disk = os.path.join(tmp, "dwpoll360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", com], check=True,
                       stdout=subprocess.DEVNULL)
        run(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswpoll: ok")


def run(disk):
    with os88ui.boot(SYS, apps=disk, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        dm = dosmap.package()

        def cyc():
            return int(m.status()["cycles"])

        def done():
            base = dosmap.lin(m, dosmap.instance(m))
            psp = int.from_bytes(m.read(base + dm["dos_ldpsp"], 2), "little")
            return int.from_bytes(m.read(psp * 16 + 0x103, 2), "little")

        def loaded(tag):
            """until the probe is in memory and waiting: a key typed while
            DOS is still loading it goes to the console's prompt instead"""
            c0 = cyc()
            while True:
                base = dosmap.lin(m, dosmap.instance(m))
                psp = int.from_bytes(m.read(base + dm["dos_ldpsp"], 2),
                                     "little")
                head = m.read(psp * 16 + 0x100, 5) if psp else b""
                if head == b"\xeb\x03\x90\x00\x00":   # and done = 0:
                    break                       # not the LAST run's image
                    break
                if (cyc() - c0) / HZ > LIMIT:
                    fail("%s: DWPOLL.COM never loaded" % tag)
                os88marty.guest_sleep(m, 0.1)
            os88marty.guest_sleep(m, 1)

        def keys(tag):
            loaded(tag)
            out = []
            for ch in KEYS:
                n0 = done()
                c0 = cyc()
                m.type_text(ch)
                while done() == n0:
                    if (cyc() - c0) / HZ > LIMIT:
                        if os.environ.get("DWPOLL_SHOT"):
                            wd, ht, px = m.fbuf()
                            os88marty.write_png_rgb(os.environ["DWPOLL_SHOT"],
                                                    wd, ht, px)
                        fail("%s: key %r not answered in %.0f guest seconds "
                             "- the 2,000 passes are being starved (SPEC.md "
                             "96.54.5.1)" % (tag, ch, LIMIT))
                    os88marty.guest_sleep(m, 0.01)
                t = (cyc() - c0) / HZ
                out.append(t)
                print("doswpoll: %-6s key %r answered in %.3f guest s"
                      % (tag, ch, t))
                os88marty.guest_sleep(m, 0.3)
            return statistics.median(out)

        if not ui.path("B:/DWPOLL.COM"):
            fail("could not launch DWPOLL.COM")
        os88marty.guest_sleep(m, 2)
        full = keys("full")
        m.type_text("q")
        os88marty.guest_sleep(m, 3)
        os88marty.settle(m)
        ui.raise_window(ui.window("DOS"))
        dosmap.run_in_window(ui)
        os88marty.guest_sleep(m, 3)
        win = keys("window")
        base = dosmap.lin(m, dosmap.instance(m))
        cm = dosmap.cellread(m, dm, base, "dvw_cmode", 1)[0]
        if cm != 1:
            fail("the probe asked AH=FEh and the window is in capture mode "
                 "%d, not TopView's (DVWC_TV = 1)" % cm)
        scr = m.read(base + dm["con_scr"], 2 * len(KEYS))
        if bytes(scr[0::2]) != KEYS.encode():
            fail("the keys did not land in con_scr's row 0 (%r)" % scr)
        m.type_text("q")
        r = win / full
        print("doswpoll: a key takes %.3f s in the window against %.3f s on "
              "the full screen, %.2fx (ceiling %.1fx)" % (win, full, r, RATIO))
        if r > RATIO:
            fail("%.2fx is over the %.1fx ceiling - a TopView program's "
                 "poll-and-update loop is being starved in the window "
                 "(SPEC.md 96.54.5.1)" % (r, RATIO))


if __name__ == "__main__":
    main()
