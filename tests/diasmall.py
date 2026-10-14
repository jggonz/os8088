#!/usr/bin/env python3
"""SPEC.md 107.7: the Desk Calendar on `kern_small`, end to end - the WORKER
arm is its tick, its reminders go off through it, and the place and the data
come back with no autostart at all.

    make && make small && python3 tests/diasmall.py

`make small`'s 360KB system disk (build/small360.img, which carries
SYSTEM\\DIA\\DESKCAL.O88 like every system disk, SPEC.md 24.5.1) copied to a
scratch file, on MartyPC's 128KB 5150 with a CGA - the floor machine, COMPACT.

  A  double-clicked out of SYSTEM\\DIA it opens; kern_small has no
     OSAPI_WM_TIMER, so the first paint HIRES THE WORKER (dia_st WORKER |
     HIRED) and registration is never TRIED - kern_small's OSAPI_DESK_ITEM is
     the refusal - and NOTHING IS SAID: no toast six guest seconds on.
  B  a drag is saved by the worker's beat: DESKCAL.DAT on A: holds the new
     place on 640x200 (the wake takes the lock round the whole beat, so the
     save is kern_big's own).
  C  an appointment typed at 09:59:50, `1000 Small`, goes off at 10:00
     through the same beat: the alert `Reminder 10:00 Small` and ONE note,
     880 Hz for 8 ticks (the worker's sleep for the next beat has begun
     before this one runs, so there is no second note to time) - a
     breakpoint on the OSAPI_SND_TONE cell, its AX and CX read at the stop.
  D  OK, and the close box closes it at once (no row to remove) with the
     state written.
  E  a RESET brings no calendar back: no autostart on kern_small.
  F  opened again by hand it is WHERE IT WAS DRAGGED, with `Small` in it,
     FIRED.

**BREAK IT ON PURPOSE**: make dia_made ignore the timer's CF (never set
DIA_S_WORKER) and A finds no worker and B never saves; take the `test
[dia_st], DIA_S_WORKER` out of dc_fire and C hears two requests, the second
never made.
"""
import os
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88build as _B   # noqa: E402

PKGDIR = _B.at("build")         # the package is the shipped one, in build/
_B.use_build("build/smallk")    # ...the kernel kern_small's (os88sym)
os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")

import os88ui      # noqa: E402
import os88marty   # noqa: E402
import os88geom as geom   # noqa: E402
import os88flush   # noqa: E402
import os88dia     # noqa: E402

MACHINE = "os8088_5150_cga_128k"
TITLE = "Desk Calendar"
TODAY = (2026, 10, 14)
TONE_CELL = 0x0060 * 16 + 0x00D3        # OSAPI_SND_TONE (os88api.inc)
D = os88dia
DIA_ST = D.dia_st_off(D.DESKCAL_DIA_BSS, D.DESKCAL_PAYLEN)
HDR = D.dia_hdr_off(D.DESKCAL_DIA_BSS)
S_TRIED, S_ROW, S_DIRTY, S_WORKER, S_HIRED = 1, 2, 16, 64, 128

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def image_size():
    b = open(os.path.join(PKGDIR, "deskcal.bin"), "rb").read()
    return struct.unpack_from("<H", b, 8)[0]


class Cal(object):
    def __init__(self, ui, tmp):
        self.ui, self.m, self.tmp = ui, ui.m, tmp
        self.image = image_size()
        w = ui.path("A:/SYSTEM/DIA/DESKCAL.O88")
        check(w is not None and w.title == TITLE, "the calendar opened")
        self.w = w
        self.wseg = struct.unpack_from("<H", self.m.read(
            ui._S("wm_wins") + w.i * geom.WIN_SIZE + geom.W_SEG, 2))[0]

    def bss(self, off, n=1):
        return self.m.readseg(self.wseg, self.image + off, n)

    def st(self):
        return self.bss(DIA_ST)[0]

    def windows(self):
        return len([w for w in self.ui.windows() if w.title == TITLE])

    def front(self):
        self.ui.raise_window(self.ui.window(TITLE))

    def key(self, name):
        self.front()
        self.m.key(name)

    def type(self, s):
        for i in range(0, len(s), 4):
            self.m.type_text(s[i:i + 4])
            os88marty.pace(self.m, 0.6)

    def appts(self):
        p = bytes(self.bss(D.DESKCAL_DIA_BSS + D.DIA_HDR_SIZE,
                           D.DESKCAL_PAYLEN))
        return dict((a["text"], (a["remind"], a["fired"]))
                    for a in D.parse_deskcal(p)["appts"])

    def dat(self):
        out = os.path.join(self.tmp, "flushed.img")
        self.m.pause()
        try:
            self.m.flush(0, out)
        finally:
            self.m.run()
        v = os88flush.Volume(open(out, "rb").read())
        try:
            return D.parse_dat(v.read("SYSTEM/APPDATA/DESKCAL.DAT"),
                               D.DESKCAL_PAYLEN, D.DESKCAL_PAYVER)
        except Exception:
            return None

    def saved(self, what):
        os88marty.guest_sleep(self.m, 1.0)
        self.ui._wait(lambda: not self.st() & S_DIRTY, what, 90,
                      snapshot=lambda: "dia_st = %#x" % self.st())
        return self.dat()


def main():
    tmp = tempfile.mkdtemp(prefix="diasmall-")
    img = os.path.join(tmp, "small360.img")
    shutil.copy(os.path.join(PKGDIR, "small360.img"), img)
    try:
        with os88ui.boot(img, machine=MACHINE) as ui:
            m = ui.m
            m.write(ui._S("clk_day"), bytes([TODAY[2], TODAY[1]]))
            m.write(ui._S("clk_year"), struct.pack("<H", TODAY[0]))
            m.write(ui._S("clk_sec"), bytes([0, 30, 9]))
            m.write(ui._S("toast_buf"), b"\0")

            print("A: it opens, hires its worker, and says nothing")
            c = Cal(ui, tmp)
            ui._wait(lambda: c.st() & S_HIRED, "the worker to be hired", 60,
                     snapshot=lambda: "dia_st = %#x" % c.st())
            os88marty.guest_sleep(m, 6.0)
            st = c.st()
            check(st & S_WORKER and st & S_HIRED,
                  "the worker arm: WORKER | HIRED (dia_st %#x)" % st)
            check(not st & (S_TRIED | S_ROW),
                  "registration never tried - OSAPI_DESK_ITEM is the refusal "
                  "here (dia_st %#x)" % st)
            txt, on = ui.toast()
            check(txt == "" and not on, "and nothing was said (toast %r, %s)"
                  % (txt, "up" if on else "down"))

            print("B: a drag, saved by the worker's beat")
            w0 = ui.window(TITLE)
            ui.drag_window(w0, -200, 0)
            w = ui.window(TITLE)
            moved = (w.x, w.y)
            check(moved != (w0.x, w0.y), "the frame moved to %r" % (moved,))
            def hdr():                  # the header IN MEMORY: the place
                b = bytes(c.bss(HDR, 8))    # recorded once two polls agree
                return b[3] & 1, struct.unpack_from("<HH", b, 4)
            ui._wait(lambda: hdr() == (1, moved) and not c.st() & S_DIRTY,
                     "the drag's save (two polls, then the write)", 120,
                     snapshot=lambda: "dia_st %#x, header %r"
                     % (c.st(), hdr()))
            d = c.dat() or {}
            check(d.get("pos") and (d.get("x"), d.get("y")) == moved
                  and tuple(d.get("screen") or ()) == (640, 200),
                  "DESKCAL.DAT holds %r on 640x200 (%r)"
                  % (moved, {k: v for k, v in d.items() if k != "payload"}))

            print("C: a reminder, through the same beat")
            m.write(ui._S("clk_sec"), bytes([45, 59, 9]))
            c.key("KeyN")
            ui._wait(lambda: c.bss(D.DESKCAL_BSS_ED)[0] == 1,
                     "the field to open", 30)
            c.type("1000 Small")
            c.key("Enter")
            ui._wait(lambda: c.bss(D.DESKCAL_BSS_ED)[0] == 0,
                     "the field to shut", 30)
            check(c.appts().get("Small") == (True, False),
                  "Small is armed and not fired (%r)"
                  % (c.appts().get("Small"),))
            with os88marty.bp_trace(m, TONE_CELL, regs=True) as tr:
                ui._wait(lambda: c.bss(D.DESKCAL_BSS_ALERT)[0] == 1
                         and c.windows() == 2, "Small's alert", 90,
                         snapshot=lambda: "clock %r" % (
                             m.read(ui._S("clk_min"), 2),))
                os88marty.guest_sleep(m, 3.0)
            tones = [(h["regs"]["ax"], h["regs"]["cx"]) for h in tr.hits]
            check(tones == [(880, 8)], "ONE note, 880 Hz for 8 ticks (%r)"
                  % (tones,))
            msg = bytes(c.bss(D.DESKCAL_BSS_RMSG, 42)).split(b"\0")[0]
            check(msg == b"Reminder 10:00 Small",
                  "the alert's line is %r" % msg)
            check(c.appts().get("Small") == (True, True), "Small is FIRED")

            print("D: OK, and the close box")
            m.key("Enter")
            ui._wait(lambda: c.windows() == 1, "the alert to go", 30)
            c.front()
            w = ui.window(TITLE)
            x, y = geom.close_xy(w.x, w.y)
            ui.mo.click(x, y, settle=0)
            ui._wait(lambda: c.windows() == 0, "the calendar to close", 90)
            d = c.dat() or {}
            fired = [a for a in D.parse_deskcal(d.get("payload", bytes(
                D.DESKCAL_PAYLEN)))["appts"] if a["text"] == "Small"]
            check(fired and fired[0]["fired"],
                  "the state file has Small, FIRED")

            print("E: a reset brings nothing back")
            t0 = struct.unpack("<H", bytes(m.read(0x46C, 2)))[0]
            m.reset()
            m.run()
            ui._wait(lambda: struct.unpack("<H", bytes(m.read(0x46C, 2)))[0]
                     < t0, "the BIOS tick to restart", 60)
            ui.ready()
            os88marty.guest_sleep(m, 15.0)
            check(TITLE not in ui.titles(),
                  "no calendar after the boot (%r)" % (ui.titles(),))

            print("F: opened again: its place and its data")
            m.write(ui._S("clk_day"), bytes([TODAY[2], TODAY[1]]))
            m.write(ui._S("clk_year"), struct.pack("<H", TODAY[0]))
            c = Cal(ui, tmp)
            w = ui.window(TITLE)
            check((w.x, w.y) == moved, "it opens at %r, where it was dragged "
                  "(%r)" % ((w.x, w.y), moved))
            check(c.appts().get("Small") == (True, True),
                  "...with Small in it, FIRED (%r)" % (c.appts().get("Small"),))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("diasmall: %s" % ("PASS" if not fails else
                            "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
