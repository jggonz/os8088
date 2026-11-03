#!/usr/bin/env python3
"""doswbasic - what a compiled BASIC program does to a live DOS window
(SPEC.md 96.54.11): a TEXT mode set, a wake posted from the program's own
stack, and a close in the middle of a tone.

Galaxy Trek (TREKRUN.EXE, 1983 compiled BASIC) was reported off the owner's
5150 in the window: nothing drawn, "File not found" after a minimize, and a
close that would not close and left the PC speaker sounding. Three defects,
and DWMODE.COM (tests/dostrap/dwmode.asm) is each of them without the
program, on a lone Hercules with the mode ticked and the probe TYPED at the
prompt, as the owner ran it:

  1  DWMODE sets text mode 3 with int 10h AH=00h and prints: it runs IN THE
     WINDOW, no bracket asked for, and `DWMODE TEXT` is in the band. A text
     mode set fell into the graphics shunt and waited for a full screen.
  2  `g` sets CGA mode 4 - the shunt, for real - and back to mode 3; the
     probe's own A5h block (PSP:0400h-F000h) is intact (`exit code 0`). The
     shunt posted its wake on the PROGRAM'S stack, and evq_push writes the
     ring through SS: the record landed in the program's memory and the
     window went deaf.
  3  `b` turns the speaker on and spins; closing the window ends the program,
     closes the window, and port 61h has bits 0-1 clear.

**Break it on purpose** (docs/WRITING-TESTS.md 1): with the text arm's
`pop ax / jmp .out` taken out, leg 1 fails ([dvw_fsreq] 1, no DWMODE TEXT);
with dvw_shunt's `OSAPI_WM_WAKE` put back, leg 2 fails; with dvw_kexit's
three port instructions taken out, leg 3 fails with 61h bits 0-1 set.
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
import os88sym                                                 # noqa: E402
import os88ui                                                  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SYS = os.path.join(ROOT, "build", "os8088-360.img")
PROBE = os.path.join(ROOT, "tests", "dostrap", "dwmode.asm")
FSX_NONE = 0xFF


def fail(msg):
    print("doswbasic: FAIL: %s" % msg)
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


def bracket(m):
    return m.read(m.sym("fsx_cur"), 1)[0]


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    # leg 2 needs the ring's offset inside the probe's A5h block: the record
    # lands at SS:evq_buf, SS being the .COM's own segment
    ring = os88sym.syms()["evq_buf"]
    if not 0x400 <= ring < 0xF000 - 128:
        fail("evq_buf is at %04X, outside DWMODE's A5h block - widen it"
             % ring)
    tmp = tempfile.mkdtemp(prefix="doswbasic")
    try:
        com = os.path.join(tmp, "DWMODE.COM")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com, PROBE],
                       check=True)
        disk = os.path.join(tmp, "dwmode360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", "--no-pack", com],
                       check=True, stdout=subprocess.DEVNULL)
        run(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswbasic: ok")


def start(m, dos, what):
    m.type_text("DWMODE\n")
    if not until(m, lambda: dos.b("dvw_run") == 1 or dos.b("dos_inbr"),
                 "%s to start" % what, 30):
        fail("%s: typing DWMODE started nothing: %r" % (what, [r for r in dos.rows() if r.strip()][-4:]))
    if dos.b("dos_inbr"):
        fail("%s: DWMODE took the full screen with the mode ticked" % what)


def ended(m, dos, what, code):
    if not until(m, lambda: dos.b("dvw_run") == 0 and
                 bracket(m) == FSX_NONE, "%s to end" % what, 40):
        fail("%s: the program never ended ([dvw_run] %d, [fsx_cur] %02X, "
             "[dvw_fsreq] %d)" % (what, dos.b("dvw_run"), bracket(m),
                                  dos.b("dvw_fsreq")))
    os88marty.settle(m)
    if not any("exit code %d" % code in r for r in dos.rows()):
        fail("%s: no `exit code %d` in the band: %r"
             % (what, code, [r for r in dos.rows() if r.strip()][-4:]))


def run(disk):
    with os88ui.boot(SYS, apps=disk, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        if not ui.path("A:/APPS/DOS.O88"):
            fail("could not open A:/APPS/DOS.O88")
        dos = Dos(m)
        os88marty.settle(m)
        try:
            dosmap.set_wmode(ui, True)
        except RuntimeError as e:
            fail(str(e))
        os88marty.settle(m)
        m.type_text("B:\n")                # the owner's `c:` - a drive typed
        os88marty.settle(m)                 # at the prompt, then the program

        # --- 1: a TEXT mode set stays in the window --------------------------
        start(m, dos, "the text run")
        if not until(m, lambda: any("DWMODE TEXT" in r for r in dos.rows()),
                     "DWMODE TEXT", 30):
            fail("a text mode set in the window: no `DWMODE TEXT` in the band "
                 "([dvw_fsreq] %d, [dvw_fs] %d, [fsx_cur] %02X) - it waits "
                 "for a full screen nothing asked for"
                 % (dos.b("dvw_fsreq"), dos.b("dvw_fs"), bracket(m)))
        if dos.b("dvw_fsreq") or bracket(m) != FSX_NONE:
            fail("a text mode set asked for the full screen ([dvw_fsreq] %d, "
                 "[fsx_cur] %02X)" % (dos.b("dvw_fsreq"), bracket(m)))
        print("doswbasic: 1 int 10h AH=00h mode 3 clears the band and the "
              "program prints in the window")

        # --- 2: the shunt's wake leaves the program's memory alone -----------
        m.type_text("g")
        if not until(m, lambda: dos.b("dvw_fs") == 1 and
                     bracket(m) != FSX_NONE, "the shunt's full screen", 30):
            fail("a graphics mode set brought up no bracket ([dvw_fsreq] %d,"
                 " [dvw_fs] %d) - the window is deaf to its wake"
                 % (dos.b("dvw_fsreq"), dos.b("dvw_fs")))
        if not until(m, lambda: any("DWMODE MEM" in r
                                    for r in (m.screen() or [])),
                     "the memory check", 20):
            fail("no DWMODE MEM line on the full screen: %r"
                 % (m.screen() or [])[-4:])
        m.type_text("x")
        ended(m, dos, "the shunted run", 0)
        print("doswbasic: 2 the shunt reached the full screen and the "
              "program's own memory is untouched (exit code 0)")

        # --- 3: a close in the middle of a tone ------------------------------
        start(m, dos, "the tone run")
        if not until(m, lambda: any("DWMODE TEXT" in r for r in dos.rows()),
                     "DWMODE TEXT", 30):
            fail("the second run printed nothing")
        m.type_text("b")
        if not until(m, lambda: m.inb(0x61) & 3 == 3, "the tone", 20):
            fail("`b` never turned the speaker on (61h = %02X)" % m.inb(0x61))
        ui.close(ui.window("DOS"), limit=60)
        p61 = m.inb(0x61)
        if p61 & 3:
            fail("the window closed with the speaker still on (61h = %02X)"
                 % p61)
        print("doswbasic: 3 the close ended the program, closed the window "
              "and silenced the speaker (61h = %02X)" % p61)


if __name__ == "__main__":
    main()
