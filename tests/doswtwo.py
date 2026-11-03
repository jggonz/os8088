#!/usr/bin/env python3
"""doswtwo - TWO DOS windows, each running its own program, at once
(SPEC.md 96.54.2, docs/plans/DOS-WINDOWED-PLAN.md wave 5's first half).

Each DOS window is its own instance with its own worker (SPEC.md 20.6), so
two windowed runs are two pre-empted tasks - and what kept that from working
was memory: a run in the window was given the WHOLE arena, exactly as the
bracket's is, so the second window had nothing to run in. A window is given
DVW_DEFKB (128 KB) now.

  1  two DOS windows, each with tests/dostrap/dwfs.asm in its path box
     (a double-click opens each and runs it once in the bracket)
  2  Run in Window in both: both are on their workers, and BOTH COUNT across
     the same stretch of guest time - two programs, neither of which ever
     yields, both moving
  3  each was given 128 KB, the window's default, and not the machine
  4  each takes its keys from its OWN window: `q` into A ends A while B
     counts on, and B's own `q` ends B

**Break it on purpose** (docs/WRITING-TESTS.md 1): with `DVW_DEFKB` set to
0 - no cap, the whole arena as before - the second window's run fails to
claim and leg 2 never sees it on its worker (done: `[dos_err] 1`).
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
WANT_KB = 128


def fail(msg):
    print("doswtwo: FAIL: %s" % msg)
    sys.exit(1)


class Dos(object):
    """One DOS instance, by its place among the package windows."""

    def __init__(self, m, slot, dm):
        self.m, self.slot, self.dm = m, slot, dm

    def base(self):
        return dosmap.lin(self.m, dosmap.instance(self.m, self.slot))

    def b(self, name):
        return dosmap.cellread(self.m, self.dm, self.base(), name, 1)[0]

    def w(self, name):
        return int.from_bytes(dosmap.cellread(self.m, self.dm, self.base(), name, 2),
                              "little")

    def rows(self):
        scr = self.m.read(self.base() + self.dm["con_scr"], 4000)
        return ["".join(chr(scr[r * 160 + i]) if 32 <= scr[r * 160 + i] < 127
                        else " " for i in range(0, 160, 2)).rstrip()
                for r in range(25)]

    def count(self):
        psp = self.w("dos_ldpsp")
        return int.from_bytes(self.m.read(psp * 16 + 0x103, 2), "little")


def until(m, cond, what, limit):
    try:
        os88marty.until(m, lambda _m: cond(), what, poll=0.2, limit=limit)
        return True
    except os88marty.MartyError:
        return False


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    tmp = tempfile.mkdtemp(prefix="doswtwo")
    try:
        com = os.path.join(tmp, "DWFS.COM")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com, PROBE],
                       check=True)
        disk = os.path.join(tmp, "dwtwo360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", com], check=True,
                       stdout=subprocess.DEVNULL)
        run(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswtwo: ok")


def dos_windows(ui):
    return sorted([w for w in ui.windows() if w.title == "DOS"],
                  key=lambda w: w.i)


def bracket_once(ui, m, dm, first):
    """A double-click on the .COM: a NEW DOS instance runs it in the bracket;
    two keys end it (the probe asks twice). The second time B:'s Disk window
    is already open, and its zone is behind the first DOS window."""
    if not first:
        ui.raise_window(ui.window("Disk"))
    if not ui.path("B:/DWFS.COM" if first else "DWFS.COM"):
        fail("could not launch DWFS.COM")
    os88marty.pace(m, 2)
    m.type_text("q")                    # the probe's own way out
    os88marty.pace(m, 3)
    os88marty.settle(m)


def run(disk):
    with os88ui.boot(SYS, apps=disk, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        dm = dosmap.package()

        # --- 1: two DOS windows ---------------------------------------------
        bracket_once(ui, m, dm, True)
        bracket_once(ui, m, dm, False)
        wins = dos_windows(ui)
        if len(wins) != 2:
            fail("two double-clicks should leave two DOS windows; there are "
                 "%d" % len(wins))
        a, b = Dos(m, 0, dm), Dos(m, 1, dm)
        print("doswtwo: 1 two DOS windows, each named DWFS.COM")

        # --- 2: both in their windows, both counting ------------------------
        for w, d in zip(wins, (a, b)):
            ui.raise_window(w)
            dosmap.run_in_window(ui)
            if not until(m, lambda: d.b("dvw_run") == 1, "a run", 30):
                fail("Run in Window in DOS window %d put nothing on its "
                     "worker - its arena claim failed? [dos_err] %d"
                     % (d.slot, d.b("dos_err")))
        os88marty.pace(m, 0.5)
        ca, cb = a.count(), b.count()
        if not until(m, lambda: a.count() > ca and b.count() > cb,
                     "both counting", 20):
            fail("over the same guest time window A counted %d -> %d and "
                 "window B %d -> %d - both should move"
                 % (ca, a.count(), cb, b.count()))
        print("doswtwo: 2 both run at once (A %d -> %d, B %d -> %d)"
              % (ca, a.count(), cb, b.count()))

        # --- 3: 128 KB each ---------------------------------------------------
        for d in (a, b):
            if d.w("dos_akb") != WANT_KB:
                fail("DOS window %d's program was given %d KB; a window's "
                     "default is %d (SPEC.md 96.54.2)"
                     % (d.slot, d.w("dos_akb"), WANT_KB))
        print("doswtwo: 3 each was given %d KB" % WANT_KB)

        # --- 4: each its own keys --------------------------------------------
        # `q` into A's window ends A - and B, which was never sent it, keeps
        # counting; then B's own `q` ends B.
        ui.raise_window(wins[0])
        m.type_text("q")
        if not until(m, lambda: a.b("dvw_run") == 0, "A's end", 20):
            fail("`q` typed into window A did not end A's program")
        cb = b.count()
        if not b.b("dvw_run") or not until(m, lambda: b.count() > cb,
                                            "B still counting", 10):
            fail("ending A stopped B, or A's key reached B")
        ui.raise_window(wins[1])
        m.type_text("q")
        if not until(m, lambda: b.b("dvw_run") == 0, "B's end", 20):
            fail("`q` typed into window B did not end B's program")
        os88marty.settle(m)
        for d in (a, b):
            if not any("exit code 113" in r for r in d.rows()):
                fail("window %d should say exit code 113 ('q'): %r"
                     % (d.slot, d.rows()[-3:]))
        print("doswtwo: 4 each window's `q` ended its own program, the "
              "other running on")


if __name__ == "__main__":
    main()
