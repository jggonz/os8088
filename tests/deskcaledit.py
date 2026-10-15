#!/usr/bin/env python3
"""SPEC.md 110.3: the Desk Calendar's APPOINTMENTS - typed, kept in order,
edited, deleted, refused at the limits - read back off the guest's own disk.

    make && python3 tests/deskcaledit.py

The SHIPPED 360KB system disk copied to a scratch file, with a DESKCAL.DAT
that tools/os88dia.py wrote put into SYSTEM\\APPDATA by mtools, on MartyPC's
VGA XT. The machine's clock is set to Wed 14 October 2026 before the
calendar opens, so "the selected day" is a date this file knows. Every
assertion about the data is about the STATE FILE as the guest wrote it -
flushed off the emulated drive and parsed by tools/os88dia.py, the
independent reader - and not about the package's memory.

Boot 1, a DAMAGED file (its appointment count is 60, past the table):

  A  it opens EMPTY (the payload is not trusted, SPEC.md 110.3), and the
     first edit - a task - puts `Replace damaged calendar?` up instead of
     writing: the file on A: is the fixture byte for byte. Yes replaces it.
  B  the one-line entry: `N` and `9:30 Dentist` Enter, then `1230 Lunch`,
     `815 Gym` and `Mum's birthday` - the state file holds four
     appointments on 2026-10-14 IN ORDER: the all-day one first, then 08:15,
     09:30 (570, its reminder armed), 12:30 - so the three time forms (H:MM,
     HHMM, HMM) and the all-day rule are each asserted by what the guest
     wrote.
  C  a press on Dentist's line and Enter opens the field HOLDING `09:30
     Dentist`; seven Backspaces and `Doctor` make it 09:30 Doctor, its
     reminder kept.
  D  a click on Lunch's line and Del ask `Delete this appointment?`; Enter
     (Yes, the default) takes it out of the file.
  E  a line whose text is 26 characters is REFUSED, not cut: `At most 25
     letters`, the field still open, and Esc shuts it with nothing written.
  G  Calendar > Delete Item picked while the field is OPEN shuts it and asks
     on a bar that is the question's own: nothing inked between its app
     name and the clock (dc_fclose had left the clip armed, SPEC.md 110.9).

  H  v2's FOCUS (SPEC.md 110.3, 11.103.4), with a Disk window in front all
     along: a press on a card's margin hands the widget the bar and the
     Disk window stays wm_top; `+` opens the field for EITHER in card 2's
     heading row - `Walk the dog` is a TASK and `1700 Tea` an appointment;
     a press on the Disk window - losing the bar - shuts an open field as
     Esc does and keeps nothing; Esc gives the bar back (RELEASE).

The appointments are selected the user's way on v2's two-line agenda:
paged by its `+N more` line until the one wanted is on show, then pressed.

Boot 2, a FULL table (48 appointments):

  F  Calendar > New Appointment is GREYED as `New Appointment (full)` (SPEC.md
     47), and `N` answers `Calendar is full (48)` and opens no field.

**BREAK IT ON PURPOSE**: make dc_parse refuse three digits and B's 08:15
comes back all-day; take dc_check out of dc_entry and A opens the junk
and never asks; drop the `jbe .fits` test and E's line is stored; take the
OSAPI_WM_CLIP_CLEAR out of dc_askdel and G reads 305 ink pixels.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88marty   # noqa: E402
import os88geom as geom   # noqa: E402
import os88flush   # noqa: E402
import os88dia     # noqa: E402

ROOT = os.path.join(HERE, "..")
BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
TITLE = "Desk Calendar"
TODAY = (2026, 10, 14)
DIA_ST = os88dia.dia_st_off(os88dia.DESKCAL_DIA_BSS, os88dia.DESKCAL_PAYLEN)
DIA_S_DIRTY, DIA_S_DAMAGED = 16, 8

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def image_size():
    b = open(os.path.join(BDIR, "deskcal.bin"), "rb").read()
    return struct.unpack_from("<H", b, 8)[0]


def disk_with(tmp, name, dat):
    img = os.path.join(tmp, name)
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), img)
    src = os.path.join(tmp, "DESKCAL.DAT")
    open(src, "wb").write(dat)
    subprocess.run(["mcopy", "-o", "-i", img, src, "::/SYSTEM/APPDATA/"],
                   check=True, stdout=subprocess.DEVNULL,
                   env=dict(os.environ, MTOOLS_SKIP_CHECK="1"))
    return img


class Cal(object):
    """One running calendar: its bss, its state file on A:, its keys - and
    its GEOMETRY, v2's three cards (SPEC.md 110.3), from tools/os88dia.py's
    DESKCAL_GEO, which t_dia holds to the package's own dc_geos."""

    def __init__(self, ui, tmp, check=check, before=None, today=TODAY,
                 bare=True):
        """`before`, when given, runs with SYSTEM\\DIA open and the
        double-click not yet made - so a caller can set the clock to the
        very minute the entry proc will read. `bare` closes the folder
        window the launch was made from: a widget is UNDER every window, so
        the folder would cover the presses aimed at its cards."""
        self.ui, self.m, self.tmp = ui, ui.m, tmp
        self.image = image_size()
        m = self.m
        m.write(ui._S("clk_day"), bytes([today[2], today[1]]))
        m.write(ui._S("clk_year"), struct.pack("<H", today[0]))
        if before is None:
            w = ui.path("A:/SYSTEM/DIA/DESKCAL.O88")
        else:
            ui.path("A:/SYSTEM/DIA")
            before()
            w = ui.open("DESKCAL.O88")
        check(w is not None and w.title == TITLE, "the calendar opened")
        self.slot = w.i
        self.wseg = struct.unpack_from("<H", m.read(
            ui._S("wm_wins") + w.i * geom.WIN_SIZE + geom.W_SEG, 2))[0]
        self.woff = os88dia.widget_off(os88dia.DESKCAL_DIA_BSS,
                                       os88dia.DESKCAL_PAYLEN)
        if bare:
            for o in list(ui.windows()):
                if o.i != self.slot and o.visible:
                    ui.close(o)

    # --- the guest's state ---------------------------------------------------
    def bss(self, off, n=1):
        return self.m.readseg(self.wseg, self.image + off, n)

    def word(self, off):
        return struct.unpack("<H", bytes(self.bss(off, 2)))[0]

    def wb(self, name):
        """A byte of os88dia.inc's widget state (dia_li, dia_hv, ...)."""
        return self.bss(self.woff[name])[0]

    def st(self):
        return self.bss(DIA_ST)[0]

    def ed(self):
        return self.bss(os88dia.DESKCAL_BSS_ED)[0]

    def c3(self):
        """Card 3: (kind 0 none / 1 next / 2 RINGING, appointment, time)."""
        return (self.bss(os88dia.DESKCAL_BSS_C3K)[0],
                self.bss(os88dia.DESKCAL_BSS_C3A)[0],
                self.word(os88dia.DESKCAL_BSS_C3T))

    def pane(self):
        return self.bss(os88dia.DESKCAL_BSS_PANE)[0]

    def win(self):
        for o in self.ui.windows():
            if o.i == self.slot:
                return o
        return None

    def desk(self):
        return bool(self.win().flags & geom.WF_DESK)

    # --- the geometry --------------------------------------------------------
    def li(self):
        return self.wb("dia_li")

    def g(self):
        return os88dia.DESKCAL_GEO[self.li()]

    def face(self):
        w = self.win()
        return w.x + 1, w.y + (0 if self.desk() else geom.TITLE_H)

    def origin(self):
        return self.face()

    def at(self, card, x, y):
        """Card `card`'s point (x, y) on the glass."""
        fx, fy = self.face()
        return fx + x, fy + os88dia.deskcal_card_top(self.li(), card) + y

    def dayc(self):
        """Card 2's index: the day card (TALL / MID) or the slot (COMPACT)."""
        return 1

    def remc(self):
        return 2 if self.li() < 2 else 1

    def line(self, k):
        g = self.g()
        return self.at(1, 40, g["A0"] + g["AP"] * k + 3)

    def task_box(self, k):
        g = self.g()
        return self.at(1, 8 + g["BOX"] // 2, g["T0"] + g["TP"] * k
                       + g["BOX"] // 2)

    def task_text(self, k):
        g = self.g()
        return self.at(1, g["TX"] + 24, g["T0"] + g["TP"] * k + g["TTY"] + 3)

    def plus(self):
        return self.at(1, 163, self.g()["HY"] + 3)

    def heading(self):
        return self.at(1, 20, self.g()["HY"] + 3)

    def nav(self, which):
        g = self.g()
        x = g["NAVL"] if which < 0 else g["NAVR"]
        return self.at(0, x + 3, g["NAVY"] + 3)

    def day_xy(self, day):
        g = self.g()
        i = list(self.bss(os88dia.DESKCAL_BSS_GRID, 42)).index(day)
        return self.at(0, 4 + 24 * (i % 7) + 12,
                       g["W0"] + g["WP"] * (i // 7) + 3)

    def link(self, which):
        """`Snooze` (0) or `Dismiss` (1) on card 3 - the slot's reminder
        page on COMPACT."""
        g = self.g()
        return self.at(self.remc(), g["LKX"] + (20 if not which else 96),
                       g["LKY"] + 3)

    # --- gestures ------------------------------------------------------------
    def front(self):
        """The widget takes the bar and the keys (a press on a card margin,
        ui.widget_activate) - or, titled (kern_small), comes to the front."""
        self.ui.raise_window(self.win())

    def key(self, name):
        self.front()
        self.m.key(name)

    def type(self, s):
        """In fours, a pace between: the BIOS buffer holds fifteen keys and
        a shifted character is two."""
        for i in range(0, len(s), 4):
            self.m.type_text(s[i:i + 4])
            os88marty.pace(self.m, 0.6)

    def press(self, pt):
        self.ui.mo.click(pt[0], pt[1], settle=0)
        os88marty.pace(self.m, 1.0)

    def click_line(self, k):
        self.press(self.line(k))

    def show_appt(self, k):
        """Page the agenda until the day's appointment k is on line 0, by
        its `+N more` pager - the user's own route - and return its line."""
        g = self.g()
        for _ in range(64):
            top = self.bss(os88dia.DESKCAL_BSS_ATOP)[0]
            n = self.bss(os88dia.DESKCAL_BSS_ACNT)[0]
            page = g["AN"] if n <= g["AN"] else g["AN"] - 1
            if top <= k < top + page:
                return k - top
            print("        DBG top %d n %d page %d pager at %r lit %d" % (
                top, n, page, self.line(g["AN"] - 1),
                self.bss(os88dia.DESKCAL_BSS_LIT)[0]))
            self.press(self.line(g["AN"] - 1))
        raise SystemExit("the agenda never paged to appointment %d" % k)

    def select_appt(self, k):
        self.click_line(self.show_appt(k))

    def answer_yes(self, what):
        """Enter on the alert (Yes is its default, SPEC.md 75.3), and wait
        for it to go - the completion has run by then, so a save it owes is
        owed before saved() looks."""
        self.m.key("Enter")
        self.ui._wait(lambda: len([w for w in self.ui.windows()
                                   if w.title == TITLE]) == 1,
                      what + " to be answered", 30)
        os88marty.pace(self.m, 0.5)

    def field_shut(self, what):
        self.ui._wait(lambda: self.ed() == 0, what, 30,
                      snapshot=lambda: "dc_ed = %d" % self.ed())

    def saved(self, what):
        """Wait for the edit's save, then the state file off A:."""
        os88marty.guest_sleep(self.m, 1.0)
        self.ui._wait(lambda: not self.st() & DIA_S_DIRTY, what, 60,
                      snapshot=lambda: "dia_st = %#x" % self.st())
        return self.dat()

    def raw(self):
        out = os.path.join(self.tmp, "flushed.img")
        self.m.pause()
        try:
            self.m.flush(0, out)
        finally:
            self.m.run()
        v = os88flush.Volume(open(out, "rb").read())
        try:
            return v.read("SYSTEM/APPDATA/DESKCAL.DAT")
        except Exception:
            return None

    def dat(self):
        d = self.raw()
        if d is None:
            return None
        p = os88dia.parse_dat(d, os88dia.DESKCAL_PAYLEN,
                              os88dia.DESKCAL_PAYVER)
        return os88dia.parse_deskcal(p["payload"])


def appts_of(cal):
    return [(tuple(a["date"]), a["time"], a["text"], a["remind"])
            for a in (cal or {}).get("appts", [])]


def boot_damaged(tmp):
    pay = bytearray(os88dia.deskcal([(TODAY, 600, "Not trusted")]))
    pay[0] = 60                 # past the 48-record table
    fixture = os88dia.dat(bytes(pay), x=335, y=40, screen=(640, 480))
    img = disk_with(tmp, "damaged.img", fixture)    # a v1 place: converted
    with os88ui.boot(img, machine="os8088_xt_vga") as ui:
        c = Cal(ui, tmp)
        print("A: a DAMAGED file opens empty and is not written unasked")
        check(c.st() & DIA_S_DAMAGED, "the load called it damaged (dia_st %#x)"
              % c.st())
        check(c.bss(os88dia.DIA_HDR_SIZE + os88dia.DESKCAL_DIA_BSS)[0] == 0,
              "...and runs empty: no appointment in memory")
        c.key("KeyK")
        c.type("First")
        c.key("Enter")
        ui._wait(lambda: len([w for w in ui.windows() if w.title == TITLE])
                 == 2, "the damaged-file question", 30)
        check(c.raw() == fixture, "the file on A: is the fixture byte for "
              "byte while the question is up")
        c.answer_yes("the damaged-file question")
        cal = c.saved("the replacing save")
        check(cal is not None and [t["text"] for t in cal["tasks"]]
              == ["First"], "Yes replaced it: one task, `First` (%r)"
              % ((cal or {}).get("tasks"),))

        print("B: four lines, three time forms and an all-day one")
        for line in ("9:30 Dentist", "1230 Lunch", "815 Gym",
                     "Mum's birthday"):
            c.key("KeyN")
            c.type(line)
            c.key("Enter")
            c.field_shut("the field to shut after %r" % line)
        cal = c.saved("the save of four appointments")
        want = [(TODAY, None, "Mum's birthday", False),
                (TODAY, 495, "Gym", True),
                (TODAY, 570, "Dentist", True),
                (TODAY, 750, "Lunch", True)]
        got = appts_of(cal)
        check(got == want, "the file holds them in order, all-day first, "
              "times 08:15 / 09:30 / 12:30 armed\n        got  %r\n        "
              "want %r" % (got, want))

        print("C: Dentist edited in the field")
        c.select_appt(2)
        check(c.bss(os88dia.DESKCAL_BSS_SELK)[0] == 1
              and c.bss(os88dia.DESKCAL_BSS_SELI)[0] == 2,
              "a click on its line selected appointment 2")
        c.key("Enter")
        ui._wait(lambda: c.ed() == 2, "the field to open on it", 30)
        buf = bytes(c.bss(os88dia.DESKCAL_BSS_EBUF, 32)).split(b"\0")[0].decode("latin-1")
        check(buf == "09:30 Dentist", "the field holds %r" % buf)
        for _ in range(7):
            c.m.key("Backspace")
            os88marty.pace(c.m, 0.2)
        c.type("Doctor")
        c.key("Enter")
        c.field_shut("the field to shut")
        got = appts_of(c.saved("the edit's save"))
        check((TODAY, 570, "Doctor", True) in got and len(got) == 4,
              "09:30 Doctor, its reminder kept (%r)" % (got,))

        print("D: Lunch deleted, after asking")
        c.select_appt(3)
        c.key("Delete")
        ui._wait(lambda: len([w for w in ui.windows() if w.title == TITLE])
                 == 2, "the Delete question", 30)
        c.answer_yes("the Delete question")
        got = appts_of(c.saved("the delete's save"))
        check([g[2] for g in got] == ["Mum's birthday", "Gym", "Doctor"],
              "Lunch is gone from the file (%r)" % ([g[2] for g in got],))

        print("E: 26 characters are refused, not cut")
        before = c.raw()
        c.key("KeyN")
        c.type("930 abcdefghijklmnopqrstuvwxyz")
        c.key("Enter")
        txt = ui.wait_toast(says="At most")
        check(txt == "At most 25 letters", "the toast says %r" % txt)
        check(c.ed() == 1, "the field is still open (dc_ed %d)" % c.ed())
        c.key("Escape")
        c.field_shut("Esc to shut the field")
        os88marty.guest_sleep(c.m, 3.0)
        check(c.raw() == before and not c.st() & DIA_S_DIRTY,
              "nothing was written")

        print("G: Delete Item picked with the field OPEN asks on a clean bar")
        # dc_oncmd shuts the field first, and dc_fclose ARMS the calendar's
        # clip to redraw the heading; the question's create recomposes the
        # bar under whatever clip is armed, and through ours every cell was
        # recorded drawn and dropped - the `Calendar  Remind` titles stayed
        # on the question's bar (SPEC.md 110.9). dc_askdel disarms first.
        # With the toast and the disk widget down, everything right of the
        # app name is ground
        c.select_appt(0)
        c.key("Enter")
        ui._wait(lambda: c.ed() != 0, "the field to open on line 0", 30)
        os88marty.pace(c.m, 1.0)
        ui.menu_pick("Calendar", "Delete Item")
        ui._wait(lambda: len([w for w in ui.windows() if w.title == TITLE])
                 == 2, "the Delete question (the field was open)", 30)
        ui._wait(lambda: not ui.toast()[1] and not ui._byte("fpg_on"),
                 "the toast and the disk widget to go", 30)
        os88marty.pace(c.m, 1.0)
        names = [mm for mm in ui.menus() if mm[0] == TITLE]
        x1 = names[0][2] if names else 151
        hx = ui._word("vid_clk_hx")
        fw, fh, fb = c.m.fbuf(0)
        inked = [(x, y) for y in range(2, 16) for x in range(x1 + 8, hx)
                 if fb[3 * (y * fw + x)] < 128]
        check(not inked, "the question's bar: nothing between its app name "
              "and the clock (%d ink pixels%s)" % (len(inked),
              ", first at %r" % (inked[0],) if inked else ""))
        c.m.key("Escape")
        ui._wait(lambda: len([w for w in ui.windows() if w.title == TITLE])
                 == 1, "No to the Delete question", 30)
        check(c.ed() == 0, "the pick shut the field (dc_ed %d)" % c.ed())

        print("H: the WIDGET's focus - an application in front all along")
        disk = ui.open_drive("A")
        check(ui.front() is not None and ui.front().i == disk.i,
              "a Disk window is the front window")
        c.front()
        rec = ui._wrec(c.win())
        check(ui._word("menu_win") == rec, "a press on a card's margin "
              "handed the widget the bar (menu_win %#06x)"
              % ui._word("menu_win"))
        check(ui.front().i == disk.i, "...and the Disk window is still "
              "wm_top: a widget is activated, never raised")
        c.press(c.plus())
        ui._wait(lambda: c.ed() == 5, "`+` to open the field for EITHER", 20,
                 snapshot=lambda: "dc_ed = %d" % c.ed())
        g = c.g()
        y1 = struct.unpack("<H", bytes(c.bss(160 + 2, 2)))[0]
        check(y1 == c.at(1, 0, g["HY"] - 3)[1], "the field is in card 2's "
              "heading row (LN_Y1 %d)" % y1)
        c.type("Walk the dog")
        c.m.key("Enter")
        c.field_shut("the plain line to commit")
        cal = c.saved("the task's save")
        check([t["text"] for t in cal["tasks"]][-1:] == ["Walk the dog"],
              "a line with no time is a TASK (%r)" % ([t["text"] for t
                                                         in cal["tasks"]],))
        c.press(c.plus())
        ui._wait(lambda: c.ed() == 5, "`+` again", 20)
        c.type("1700 Tea")
        c.m.key("Enter")
        c.field_shut("the timed line to commit")
        got = appts_of(c.saved("the appointment's save"))
        check((TODAY, 1020, "Tea", True) in got, "a line starting with a "
              "time is an APPOINTMENT (%r)" % (got,))
        check(ui.front().i == disk.i, "the Disk window stayed in front "
              "through all of it")
        c.press(c.plus())
        ui._wait(lambda: c.ed() == 5, "`+` a third time", 20)
        c.type("Never")
        before = c.raw()
        x, y = disk.x + disk.w // 2, disk.y + disk.h // 2
        ui.mo.click(x, y, settle=0)
        ui._wait(lambda: c.ed() == 0, "losing the bar to shut the field "
                 "(DIA_ONFOCUS 0, as Esc)", 20)
        os88marty.guest_sleep(c.m, 3.0)
        check(c.raw() == before, "...and nothing typed there was kept")
        c.front()
        c.m.key("Escape")
        ui._wait(lambda: ui._word("menu_win") == ui._wrec(disk),
                 "Esc to give the bar back (OSAPI_WM_DESK RELEASE)", 20,
                 snapshot=lambda: "menu_win %#06x" % ui._word("menu_win"))
        check(True, "Esc RELEASED: the Disk window owns the bar again")


def boot_full(tmp):
    days = [(2026, 10, d) for d in range(1, 25)]
    appts = [(d, 600 + i, "Busy %d" % i) for d in days for i in (0, 1)]
    fixture = os88dia.dat(os88dia.deskcal(appts), x=335, y=40,
                          screen=(640, 480))
    img = disk_with(tmp, "full.img", fixture)
    with os88ui.boot(img, machine="os8088_xt_vga") as ui:
        c = Cal(ui, tmp)
        print("F: 48 appointments: New Appointment greyed, N refused")
        c.front()
        cal = [mm for mm in ui.menus() if mm[0] == "Calendar"]
        items = cal[0][3] if cal else []
        check(items[:1] == [("New Appointment (full)", False)],
              "the item is greyed with its reason (%r)" % (items[:1],))
        check(items[1:2] == [("New Task  K", True)],
              "...and New Task is live (%r)" % (items[1:2],))
        c.key("KeyN")
        txt = ui.wait_toast(says="full")
        check(txt == "Calendar is full (48)", "N says %r" % txt)
        os88marty.pace(c.m, 1.0)
        check(c.ed() == 0, "and opened no field")


def main():
    tmp = tempfile.mkdtemp(prefix="deskcaledit-")
    try:
        boot_damaged(tmp)
        boot_full(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("deskcaledit: %s" % ("PASS" if not fails else
                               "FAIL (%d): %s" % (len(fails),
                                                  "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
