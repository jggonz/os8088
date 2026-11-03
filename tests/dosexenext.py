#!/usr/bin/env python3
"""dosexenext - the program typed after an .EXE is ITSELF (SPEC.md 96.8.3).

`[dos_isexe]` chooses `dos_prog_enter`'s entry: an .EXE's relocated CS:IP and
its own stack, or a .COM's PSP:0100. `dos_exe_setup` sets it and `dos_run` -
the prompt's launch - never cleared it, so in a DOS window that had run an
.EXE the next .COM was entered at the .EXE's entry point. IBM DOS 3.30's own
REPLACE (no arguments, exit 11) followed by SYS hung after `Starting...`.

tests/dostrap/exenext.asm is that pair with no owner binary in it: XFIRST.EXE
(entry 0x400 into an image of zeros, prints `XFIRST ran`, exit 88) then
CNEXT.COM (prints `CNEXT ran`, exit 67), both on a scratch copy of the 360KB
system disk, typed at A:\\>.

  1  the bracket: XFIRST then CNEXT - CNEXT prints its OWN line and ends 067
  2  the window (Run in Window, 96.54.10): the same pair, the same answers

**Break it on purpose** (docs/WRITING-TESTS.md 1), measured: with dos_run's
`.isCOM` store taken out, leg 1 fails - CNEXT prints `XFIRST ran` and ends
with exit code 088, the .EXE's.
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
SRC = os.path.join(ROOT, "tests", "dostrap", "exenext.asm")


def fail(msg):
    print("dosexenext: FAIL: %s" % msg)
    sys.exit(1)


class Dos(object):
    def __init__(self, m):
        self.m = m
        self.dm = dosmap.package()

    def base(self):
        return dosmap.lin(self.m, dosmap.instance(self.m))

    def b(self, name):
        return dosmap.cellread(self.m, self.dm, self.base(), name, 1)[0]

    def live(self):
        scr = self.m.read(self.base() + self.dm["con_scr"], 80 * 25 * 2)
        rows = ["".join(chr(scr[r * 160 + i]) if 32 <= scr[r * 160 + i] < 127
                        else " " for i in range(0, 160, 2)).rstrip()
                for r in range(25)]
        return [r for r in rows if r.strip()]


def run_one(m, dos, name, ext, says, code, what):
    """Type NAME, wait for its `ended` line and the prompt, and check the
    program's own line and exit code are between them."""
    m.type_text(name + "\n")
    end = "A:\\%s.%s ended" % (name, ext)

    def done(_m):
        rows = dos.live()
        return (rows and rows[-1] == "A:\\>" and not dos.b("dos_inbr")
                and not dos.b("dvw_run") and any(end in r for r in rows[-3:]))
    try:
        os88marty.until(m, done, "%s to end" % name, poll=0.2, limit=60)
    except os88marty.MartyError:
        fail("%s: %s never came back to A:\\> ([dos_inbr] %d, [dvw_run] %d): "
             "%r" % (what, name, dos.b("dos_inbr"), dos.b("dvw_run"),
                     dos.live()[-4:]))
    rows = dos.live()
    i = max(k for k, r in enumerate(rows) if end in r)
    said = rows[i - 1] if i else ""
    if said != says or ("exit code %03d" % code) not in rows[i]:
        fail("%s: %s printed %r and %r - it should print %r and end with "
             "exit code %03d (a .COM run as the last .EXE says `XFIRST ran` "
             "and 088)" % (what, name, said, rows[i], says, code))


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    tmp = tempfile.mkdtemp(prefix="dosexenext")
    try:
        disk = os.path.join(tmp, "sys360.img")
        shutil.copyfile(SYS, disk)
        for out, flags in (("XFIRST.EXE", ["-DEXE"]), ("CNEXT.COM", [])):
            path = os.path.join(tmp, out)
            subprocess.run(["nasm", "-f", "bin", "-w+error"] + flags +
                           ["-o", path, SRC], check=True)
            subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                         "os88fat.py"),
                            "add", disk, path], check=True,
                           stdout=subprocess.DEVNULL)
        with os88ui.boot(disk, apps=None, machine="os8088_5150_herc_gla") as ui:
            m = ui.m
            if not ui.path("A:/APPS/DOS.O88"):
                fail("could not open A:/APPS/DOS.O88")
            dos = Dos(m)
            os88marty.settle(m)
            if not dos.live() or dos.live()[-1] != "A:\\>":
                fail("DOS should open on A:\\> and reads %r" % dos.live()[-2:])
            for leg, mode, what in ((1, False, "the bracket"),
                                    (2, True, "the window")):
                if mode:
                    try:
                        dosmap.set_wmode(ui, True)
                    except RuntimeError as e:
                        fail(str(e))
                run_one(m, dos, "XFIRST", "EXE", "XFIRST ran", 88, what)
                run_one(m, dos, "CNEXT", "COM", "CNEXT ran", 67, what)
                print("dosexenext: %d in %s, XFIRST.EXE then CNEXT.COM each "
                      "ran as itself" % (leg, what))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("dosexenext: ok")


if __name__ == "__main__":
    main()
