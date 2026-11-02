#!/usr/bin/env python3
"""doswcap - a program that writes the screen ITSELF, live in a DOS window
(SPEC.md 96.54.5, 96.54.6; docs/plans/DOS-WINDOWED-PLAN.md waves 4 and 6).

Two machines, because the two halves are about two kinds of machine.

On `os8088_5150_both_gla_mono` - a Hercules desktop with a CGA beside it, the
owner's 5150 - the CGA's B800 is RAM nobody shows, so a windowed run CAPTURES
it (DVWC_SEG):
  1  tests/dostrap/dwdirect.asm (Works' shape: AH=0Fh, then direct writes)
     runs in the window: [dvw_cmode] = DVWC_SEG on B800, its lines reach the
     band WHILE IT RUNS, and not one of them is in the desktop's own
     framebuffer at B000
  2  `q` ends it with exit code 113

On `os8088_5150_herc_gla` - a LONE Hercules - nothing answers at B800
until 3BFh bit 1 turns the card's own second page on, so the capture is that
page (SPEC.md 96.54.6.1):
  3  DWDIRECT runs there: [dvw_cmode] DVWC_HP on B800 with int 10h still
     the window's own, the one-row probe promoted by the direct writes, its
     lines in the band while it runs and not in page 0 (B000, the desktop's
     pixels), and a status port that moves (0040:0063 = 3B4h)
  4  it ends, and the page's decode is OFF again: B800 reads FFh, as
     vid_setmode leaves it

On `os8088_xt_vga` - mode 12h, where neither B000 nor B800 is memory -
there is nothing to capture, and a TopView-aware program is the one that
still works:
  5  tests/dostrap/dwtv.asm asks int 10h AH=FEh and is given the window's own
     buffer (our segment); [dvw_cseg] is 0 - no capture was taken
  6  its lines reach the band while it runs - the even ones with no AH=FFh,
     found by the row sums, the odd ones marked by AH=FFh

**Break it on purpose** (docs/WRITING-TESTS.md 1): with dvw_cdetect forced to
answer CF=1, leg 1 reads [dvw_cmode] 0 and no DWDIRECT line in the band; with
`.miss`'s 3BFh retry taken out, leg 3 reads no capture on the lone Hercules.
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
DVWC_TV, DVWC_SEG, DVWC_HP = 1, 2, 3
XB_VID = (2 + 4 + 0x30 * 2) * 2 + 2     # dosw.inc's DVW_XB_VID


def fail(msg):
    print("doswcap: FAIL: %s" % msg)
    sys.exit(1)


class Dos(object):
    def __init__(self, m):
        self.m = m
        self.dm = dosmap.package()

    def seg(self):
        return int(dosmap.instance(self.m))

    def base(self):
        return dosmap.lin(self.m, dosmap.instance(self.m))

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

    def pword(self, off):
        psp = self.w("dos_ldpsp")
        return int.from_bytes(self.m.read(psp * 16 + off, 2), "little")


def until(m, cond, what, limit):
    try:
        os88marty.until(m, lambda _m: cond(), what, poll=0.2, limit=limit)
        return True
    except os88marty.MartyError:
        return False


def build(tmp, names):
    coms = []
    for n in names:
        com = os.path.join(tmp, n.upper() + ".COM")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com,
                        os.path.join(ROOT, "tests", "dostrap", n + ".asm")],
                       check=True)
        coms.append(com)
    disk = os.path.join(tmp, "dwcap360.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", disk, "--size", "360"] + coms, check=True,
                   stdout=subprocess.DEVNULL)
    return disk


def live_lines(m, dos, tag, want):
    """`want` of the probe's lines in the band, with the program still on."""
    got = lambda: [r for r in dos.rows() if r.startswith(tag)]
    if not until(m, lambda: len(got()) >= want, "%d %s lines" % (want, tag),
                 30):
        fail("the band holds %d %s lines (want %d) with the program "
             "running: %r" % (len(got()), tag, want, dos.rows()[:6]))
    if not dos.b("dvw_run"):
        fail("the program ended before its lines were read - this says "
             "nothing about LIVE")
    return got()


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    tmp = tempfile.mkdtemp(prefix="doswcap")
    try:
        disk = build(tmp, ["dwdirect", "dwtv"])
        capture(disk)
        hpage(disk)
        topview(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswcap: ok")


def capture(disk):
    with os88ui.boot(SYS, apps=disk,
                     machine="os8088_5150_both_gla_mono") as ui:
        m = ui.m
        if not ui.path("B:/DWDIRECT.COM"):
            fail("could not launch DWDIRECT.COM")
        dos = Dos(m)
        os88marty.pace(m, 2)
        m.type_text("q")
        if not until(m, lambda: not dos.b("dos_inbr"), "the bracket down",
                     40):
            fail("DWDIRECT's bracketed run never came down after `q`")
        os88marty.settle(m)
        ui.raise_window(ui.window("DOS"))
        dosmap.run_in_window(ui)
        if not until(m, lambda: dos.b("dvw_run") == 1, "the windowed run", 20):
            fail("Run in Window put nothing on the worker")

        # --- 1: captured, live, and not on the desktop ----------------------
        if dos.b("dvw_cmode") != DVWC_SEG or dos.w("dvw_cseg") != 0xB800:
            fail("a Hercules desktop with a CGA beside it should capture B800 "
                 "([dvw_cmode] %d, [dvw_cseg] %04X)"
                 % (dos.b("dvw_cmode"), dos.w("dvw_cseg")))
        got = live_lines(m, dos, "DWDIRECT ", 4)
        hb = m.read(0xB0000, 0x8000)
        if b"D\x07W\x07D\x07I\x07R" in hb:
            fail("the program's text is in B000 - the desktop's own "
                 "framebuffer")
        print("doswcap: 1 B800 captured: %d DWDIRECT lines in the band while "
              "it runs, none on the desktop" % len(got))

        # --- 2: it ends --------------------------------------------------------
        m.type_text("q")
        if not until(m, lambda: dos.b("dvw_run") == 0, "its end", 20):
            fail("`q` did not end the captured program")
        os88marty.settle(m)
        if not any("exit code 113" in r for r in dos.rows()):
            fail("no `exit code 113` in the band: %r" % dos.rows()[-4:])
        print("doswcap: 2 `q` ended it, exit code 113")


def windowed(ui, name):
    m = ui.m
    if not ui.path("B:/%s.COM" % name):
        fail("could not launch %s.COM" % name)
    dos = Dos(m)
    os88marty.pace(m, 2)
    m.type_text("q")
    if not until(m, lambda: not dos.b("dos_inbr"), "the bracket down", 40):
        fail("%s's bracketed run never came down after `q`" % name)
    os88marty.settle(m)
    ui.raise_window(ui.window("DOS"))
    dosmap.run_in_window(ui)
    if not until(m, lambda: dos.b("dvw_run") == 1, "the windowed run", 20):
        fail("Run in Window put nothing on the worker")
    return dos


def hpage(disk):
    with os88ui.boot(SYS, apps=disk, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        if m.read(0xB8000, 2) != b"\xff\xff":
            fail("a lone Hercules decodes B800 before anything allowed it - "
                 "this machine cannot show the page being turned on")
        dos = windowed(ui, "DWDIRECT")

        # --- 3: its own page 1 ---------------------------------------------
        if dos.b("dvw_cmode") != DVWC_HP or dos.w("dvw_cseg") != 0xB800 \
                or dos.b("dvw_chp") != 1:
            fail("a lone Hercules should WATCH its own page 1 ([dvw_cmode] "
                 "%d, [dvw_cseg] %04X, [dvw_chp] %d)"
                 % (dos.b("dvw_cmode"), dos.w("dvw_cseg"), dos.b("dvw_chp")))
        if dos.b("dos_inbr"):
            fail("int 10h went to the ROM on a lone Hercules - it stays the "
                 "window's own there, and only a direct writer touches page 1")
        if not until(m, lambda: dos.b("dvw_hprow") == 0xFF,
                     "the probe's promotion", 30):
            fail("a direct writer ran for 30 guest seconds and the one-row "
                 "probe never found page 1 moved ([dvw_hprow] %02X)"
                 % dos.b("dvw_hprow"))
        # rows 0..15 cycle A..P; a letter that moves is a live capture and
        # not the console the bracketed run left behind
        c0 = dos.pword(0x103)
        if not until(m, lambda: dos.pword(0x103) >= c0 + 3, "three lines", 20):
            fail("DWDIRECT stopped writing")
        row = (dos.pword(0x103) - 1) & 15
        want = "DWDIRECT %s" % chr(ord("A") + row)
        if not until(m, lambda: dos.rows()[row].startswith(want),
                     "row %d live" % row, 10):
            fail("page 1's row %d holds the program's %r and the band shows "
                 "%r" % (row, want, dos.rows()[row]))
        got = live_lines(m, dos, "DWDIRECT ", 4)
        if b"D\x07W\x07D\x07I\x07R" in m.read(0xB0000, 0x8000):
            fail("the program's text is in page 0 - the desktop's pixels")
        crtc = int.from_bytes(m.read(dosmap.cell(m, dos.dm, dos.base(),
                                                 "dvw_xblk") +
                                     XB_VID + 0x1A, 2), "little")
        if crtc != 0x3B4:
            fail("the program was told a CRTC at %04X - on a lone Hercules "
                 "a CGA's 3DAh reads FFh and a retrace wait on it never ends; "
                 "its own 3B4h is the one that moves" % crtc)
        print("doswcap: 3 page 1 captured on a lone Hercules: %d DWDIRECT "
              "lines in the band while it runs, page 0 untouched" % len(got))

        # --- 4: it ends, and the decode is off again ---------------------------
        m.type_text("q")
        if not until(m, lambda: dos.b("dvw_run") == 0, "its end", 20):
            fail("`q` did not end the captured program")
        os88marty.settle(m)
        if not any("exit code 113" in r for r in dos.rows()):
            fail("no `exit code 113` in the band: %r" % dos.rows()[-4:])
        if m.read(0xB8000, 2) != b"\xff\xff":
            fail("the run ended and page 1's decode is still on (B800 reads "
                 "%r) - 3BFh was not put back" % m.read(0xB8000, 2))
        print("doswcap: 4 `q` ended it, and B800 floats again")


def topview(disk):
    with os88ui.boot(SYS, apps=disk, machine="os8088_xt_vga") as ui:
        m = ui.m
        if not ui.path("B:/DWTV.COM"):
            fail("could not launch DWTV.COM")
        dos = Dos(m)
        os88marty.pace(m, 2)
        m.type_text("q")
        if not until(m, lambda: not dos.b("dos_inbr"), "the bracket down",
                     40):
            fail("DWTV's bracketed run never came down after `q`")
        os88marty.settle(m)
        ui.raise_window(ui.window("DOS"))
        dosmap.run_in_window(ui)
        if not until(m, lambda: dos.b("dvw_run") == 1, "the windowed run", 20):
            fail("Run in Window put nothing on the worker")
        os88marty.pace(m, 0.5)

        # --- 5: no capture here; TopView answers our buffer -----------------
        if dos.w("dvw_cseg"):
            fail("a capture was taken on a VGA in mode 12h, which has no text "
                 "memory to take ([dvw_cseg] %04X)" % dos.w("dvw_cseg"))
        if dos.b("dvw_cmode") != DVWC_TV:
            fail("AH=FEh in a window should set DVWC_TV; [dvw_cmode] is %d"
                 % dos.b("dvw_cmode"))
        if dos.pword(0x105) != dos.seg() or \
                dos.pword(0x107) != dos.dm["con_scr"]:
            fail("AH=FEh answered %04X:%04X; the window's buffer is %04X:%04X"
                 % (dos.pword(0x105), dos.pword(0x107), dos.seg(),
                    dos.dm["con_scr"]))
        print("doswcap: 5 no capture on a VGA desktop; AH=FEh answered the "
              "window's own buffer")

        # --- 6: its lines, both kinds -------------------------------------------
        got = live_lines(m, dos, "DWTV ", 4)
        letters = set(r[5] for r in got if len(r) > 5)
        even = [c for c in letters if (ord(c) - ord("A")) % 2 == 0]
        odd = [c for c in letters if (ord(c) - ord("A")) % 2 == 1]
        if not even or not odd:
            fail("the band holds DWTV lines %r - want both the AH=FFh ones "
                 "(odd) and the summed ones (even)" % sorted(letters))
        m.type_text("q")
        if not until(m, lambda: dos.b("dvw_run") == 0, "its end", 20):
            fail("`q` did not end the TopView program")
        print("doswcap: 6 its lines reached the band, marked (%s) and summed "
              "(%s)" % ("".join(sorted(odd)), "".join(sorted(even))))


if __name__ == "__main__":
    main()
