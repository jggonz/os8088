#!/usr/bin/env python3
"""SPEC.md 107.3: the Desk Calendar's REMINDERS go off - at their minute, one
at a time, never under a typed line - and a launch sums up what it missed.

    make && python3 tests/deskcalremind.py

The SHIPPED 360KB system disk copied to a scratch file, with a DESKCAL.DAT
that tools/os88dia.py wrote put into SYSTEM\\APPDATA by mtools, on MartyPC's
VGA XT. The machine's clock is SET by writing the live `clk_*` fields - the
same block OSAPI_CLK_GET reads (SPEC.md 37.96) - so each minute the test
needs is ten guest seconds away rather than an hour. Today is Wed 14 October
2026; the fixture holds, all with no lead:

    all day  Mum's birthday        never a reminder
    07:00    Old one               FIRED already
    08:00    Gym                   due before the launch: MISSED
    09:30    Dentist               the first to go off
    09:31    Call Bob              due while Dentist's alert is up
    09:33    Quarterly budget review   23 letters: the prefix must go
    09:36    Typing test           due while the entry field is open
    00:00    Midnight (the 15th)   due at the rollover

  A  launched at 09:28: ONE toast, `1 missed reminder`; Gym is FIRED in
     memory, Old one still is, Dentist is not.
  B  at 09:30 Dentist goes off: the alert's line is `Reminder 09:30 Dentist`
     (OK alone), the toast `09:30 Dentist`, the speaker asked for 880 Hz and
     then 660 Hz four ticks each (a breakpoint on the OSAPI_SND_TONE cell,
     its AX and CX read at the stop), FIRED in memory - and the calendar drew
     exactly ONE run, Dentist's agenda line with its mark gone from `!` to
     `-` (the line buffer read back).
  C  ONE AT A TIME: 09:31 and 09:32 pass with Dentist's alert still up and
     Call Bob does not go off; OK, and at 09:33 it does - and only it, though
     Quarterly is due by then too; OK, and at 09:34 Quarterly's alert reads
     `09:33 Quarterly budget review`, the prefix dropped so the user's text
     is all there (OS88UI_AMAX is 34).
  D  NEVER UNDER A TYPED LINE: with the entry field open across 09:36 the
     Typing test does not go off; Esc, and at 09:37 it does.
  E  A MOMENT ALREADY PAST: `0930 Late entry` typed at 09:37 is created
     FIRED - no alert; `2300 Later` is armed and not fired.
  F  the state file, after the save the firings owed: Gym, Dentist, Call
     Bob, Quarterly, Typing test and Late entry FIRED; Later and Midnight not.
  G  NO RTC: closed and opened again with the clock's RTC flag down and
     nothing missed - Later is still to come - the toast is `No RTC - check
     the time`.
  H  MIDNIGHT: at 23:59:52 the date rolls to the 15th - today's inverse
     moves (TWO runs: the old field and the new; the 14th is the selected
     day so its frame is redrawn and is not a run) - and Midnight, on the
     new day, goes off at 00:00 - Later (23:00) having gone off, and been
     answered, at 23:59 first.

**BREAK IT ON PURPOSE**: take the `os88ui_awin` test out of dc_rpass and C
sees Call Bob go off at 09:31 (or the alert refused and its FIRED lost);
make dc_missed compare with `<=` instead of `<` and A misses nothing that
matters but a launch AT 09:30 would miss Dentist; drop dc_stale's call in
dc_cappt and E's Late entry goes off at once.
"""
import os
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88marty   # noqa: E402
import os88dia     # noqa: E402
import os88geom as geom   # noqa: E402
from deskcaledit import Cal, TITLE, disk_with   # noqa: E402
from deskcaltask import pkg_sym                 # noqa: E402

TODAY = (2026, 10, 14)
TOMORROW = (2026, 10, 15)
TONE_CELL = 0x0060 * 16 + 0x00D3        # OSAPI_SND_TONE (os88api.inc)
B = os88dia

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def fixture():
    appts = [(TODAY, None, "Mum's birthday", False),
             (TODAY, 420, "Old one", True, True),
             (TODAY, 480, "Gym"),
             (TODAY, 570, "Dentist"),
             (TODAY, 571, "Call Bob"),
             (TODAY, 573, "Quarterly budget review"),
             (TODAY, 576, "Typing test"),
             (TOMORROW, 0, "Midnight")]
    return B.dat(B.deskcal(appts, lead=0), x=359, y=40, screen=(640, 480))


class Rem(object):
    def __init__(self, ui, c):
        self.ui, self.c, self.m = ui, c, ui.m
        self.amsg = None

    def clock(self, h, mi, s):
        self.m.write(self.ui._S("clk_sec"), bytes([s, mi, h]))

    def minute(self):
        return self.m.read(self.ui._S("clk_min"), 2)

    def alert(self):
        return self.c.bss(B.DESKCAL_BSS_ALERT)[0]

    def rmsg(self):
        """The line THE ALERT SHOWS: os88ui.inc's own message pointer, read
        out of the package (its inline data), and the string it names."""
        if self.amsg is None:
            self.amsg = pkg_sym(self.c.tmp, "os88ui_amsg")
        at = struct.unpack("<H", bytes(self.m.readseg(self.c.wseg,
                                                      self.amsg, 2)))[0]
        raw = bytes(self.m.readseg(self.c.wseg, at, 42))
        return raw.split(b"\0")[0].decode("latin-1")

    def appts(self):
        """The payload as it is in MEMORY: {text: (remind, fired)}."""
        p = bytes(self.c.bss(B.DESKCAL_DIA_BSS + B.DIA_HDR_SIZE,
                             B.DESKCAL_PAYLEN))
        return dict((a["text"], (a["remind"], a["fired"]))
                    for a in B.parse_deskcal(p)["appts"])

    def fired(self, text):
        return self.appts().get(text, (None, None))[1]

    def close(self):
        """ONE click on the close box, and a wait long enough for what it
        does: the settings core read, the row removed, SYSTEM.CFG and the
        state file written - seconds of disk, where os88ui's close re-clicks
        after three (and the second click would land on whatever is under)."""
        w = self.ui.window(TITLE)
        self.ui.raise_window(w)
        w = self.ui.window(TITLE)
        x, y = geom.close_xy(w.x, w.y)
        self.ui.mo.click(x, y, settle=0)
        self.ui._wait(lambda: self.windows() == 0,
                      "the calendar to close", 90)

    def windows(self):
        return len([w for w in self.ui.windows() if w.title == TITLE])

    def wait_alert(self, what, limit=60):
        self.ui._wait(lambda: self.alert() == 1 and self.windows() == 2,
                      what, limit,
                      snapshot=lambda: "dc_alert %d, %d windows, clock %r"
                      % (self.alert(), self.windows(), self.minute()))

    def ok(self, what):
        self.m.key("Enter")
        self.ui._wait(lambda: self.alert() == 0 and self.windows() == 1,
                      what + " to be answered", 30)
        os88marty.pace(self.m, 0.5)

    def past(self, h, mi, extra=4.0):
        """Run until the clock reads h:mi, then `extra` guest seconds - two
        beats - more."""
        self.ui._wait(lambda: tuple(self.minute()) == (mi, h),
                      "the clock to reach %02d:%02d" % (h, mi), 60,
                      snapshot=lambda: "clock %r" % (self.minute(),))
        os88marty.guest_sleep(self.m, extra)


def main():
    tmp = tempfile.mkdtemp(prefix="deskcalremind-")
    try:
        img = disk_with(tmp, "remind.img", fixture())
        with os88ui.boot(img, machine="os8088_xt_vga") as ui:
            m = ui.m
            m.write(ui._S("clk_sec"), bytes([0, 28, 9]))
            c = Cal(ui, tmp, check=check)       # sets the date, opens it
            r = Rem(ui, c)
            run_at = c.wseg * 16 + pkg_sym(tmp, "dc_run")

            print("A: the launch sums up what it missed")
            txt = ui.wait_toast(says="missed")
            check(txt == "1 missed reminder", "the toast says %r" % txt)
            check(r.fired("Gym") is True, "Gym is FIRED in memory")
            check(r.fired("Old one") is True, "Old one still is")
            check(r.fired("Dentist") is False, "Dentist is not, yet")

            print("B: 09:30, Dentist")
            with os88marty.bp_trace(m, TONE_CELL, run_at, regs=True) as tr:
                r.clock(9, 29, 50)
                r.wait_alert("Dentist's alert")
                os88marty.guest_sleep(m, 2.0)   # the second note's beat
            tones = [(h["regs"]["ax"], h["regs"]["cx"]) for h in tr.hits
                     if h["addr"] == TONE_CELL]
            runs = len([h for h in tr.hits if h["addr"] == run_at])
            check(tones == [(880, 4), (660, 4)], "the chirp: 880 Hz then "
                  "660 Hz, four ticks each (%r)" % (tones,))
            check(r.rmsg() == "Reminder 09:30 Dentist",
                  "the alert's line is %r" % r.rmsg())
            txt, _ = ui.toast()     # STAGED: it may be down by now, and the
            check(txt == "09:30 Dentist",   # buffer said `1 missed` before
                  "the toast says %r" % txt)
            check(r.fired("Dentist") is True, "Dentist is FIRED")
            check(runs == 1, "the calendar drew ONE run, its line (%d)" % runs)
            line = bytes(c.bss(70, 27)).split(b"\0")[0].decode("latin-1")
            check(line.startswith(" 09:30- Dentist"),
                  "...and the line reads %r, the mark `-`" % line)

            print("C: one alert at a time")
            r.clock(9, 31, 50)
            r.past(9, 32)
            check(r.fired("Call Bob") is False and r.windows() == 2,
                  "09:31 and 09:32 passed under Dentist's alert: Call Bob "
                  "has not gone off, and one alert is up")
            r.ok("Dentist's alert")
            r.clock(9, 32, 52)
            r.wait_alert("Call Bob's alert")
            check(r.rmsg() == "Reminder 09:31 Call Bob",
                  "at 09:33 Call Bob goes off (%r)" % r.rmsg())
            check(r.fired("Quarterly budget review") is False,
                  "...and only it, though Quarterly is due too")
            r.ok("Call Bob's alert")
            r.clock(9, 33, 52)
            r.wait_alert("Quarterly's alert")
            check(r.rmsg() == "09:33 Quarterly budget review",
                  "at 09:34 the alert drops the prefix to keep the text: %r"
                  % r.rmsg())
            txt, _ = ui.toast()
            check(txt.startswith("09:33 Quarterly"),
                  "the toast, the text last (%r)" % txt)
            r.ok("Quarterly's alert")

            print("D: never under a typed line")
            c.key("KeyN")
            ui._wait(lambda: c.ed() == 1, "the field to open", 30)
            r.clock(9, 35, 52)
            r.past(9, 36)
            check(r.fired("Typing test") is False and r.alert() == 0,
                  "09:36 passed with the field open: nothing went off")
            c.key("Escape")
            c.field_shut("Esc to shut the field")
            r.clock(9, 36, 52)
            r.wait_alert("Typing test's alert")
            check(r.rmsg() == "Reminder 09:36 Typing test",
                  "at 09:37 it goes off (%r)" % r.rmsg())
            r.ok("Typing test's alert")

            print("E: a moment already past")
            for line in ("0930 Late entry", "2300 Later"):
                c.key("KeyN")
                c.type(line)
                c.key("Enter")
                c.field_shut("the field to shut after %r" % line)
            os88marty.guest_sleep(m, 3.0)
            check(r.appts().get("Late entry") == (True, True)
                  and r.alert() == 0,
                  "Late entry (09:30, typed at 09:37) is armed and FIRED, "
                  "and no alert (%r)" % (r.appts().get("Late entry"),))
            check(r.appts().get("Later") == (True, False),
                  "Later (23:00) is armed and not fired")

            print("F: the state file")
            cal = c.saved("the save the firings owed") or {}
            got = dict((a["text"], a["fired"]) for a in cal.get("appts", []))
            want = {"Mum's birthday": False, "Old one": True, "Gym": True,
                    "Dentist": True, "Call Bob": True,
                    "Quarterly budget review": True, "Typing test": True,
                    "Late entry": True, "Later": False, "Midnight": False}
            check(got == want, "FIRED as the file has it\n        got  %r\n"
                  "        want %r" % (got, want))

            print("G: no RTC, nothing missed, a reminder to come")
            rtc = ui._S("clk_rtc")
            was = m.read(rtc, 1)
            r.close()
            m.write(rtc, b"\0")
            ui.m.write(ui._S("toast_buf"), b"\0")
            c = Cal(ui, tmp, check=check)
            r = Rem(ui, c)
            run_at = c.wseg * 16 + pkg_sym(tmp, "dc_run")
            txt = ui.wait_toast(says="RTC")
            check(txt == "No RTC - check the time", "the toast says %r" % txt)
            m.write(rtc, bytes(was))

            print("H: midnight")
            r.clock(23, 59, 20)         # Later (23:00) is due first, and is
            r.wait_alert("Later's alert")   # answered before the rollover
            check(r.rmsg() == "Reminder 23:00 Later",
                  "Later goes off first (%r)" % r.rmsg())
            r.ok("Later's alert")
            with os88marty.bp_trace(m, run_at) as tr:
                r.clock(23, 59, 52)
                r.wait_alert("Midnight's alert")
                os88marty.guest_sleep(m, 1.0)
            td = (struct.unpack_from("<H", bytes(c.bss(B.DESKCAL_BSS_TY,
                                                        2)))[0],
                  c.bss(B.DESKCAL_BSS_TM)[0], c.bss(B.DESKCAL_BSS_TD)[0])
            check(td == TOMORROW, "today is the 15th (%r)" % (td,))
            check(tr.n == 2, "the rollover drew TWO runs, the old today and "
                  "the new (%d)" % tr.n)
            check(r.rmsg() == "Reminder 00:00 Midnight",
                  "Midnight goes off at 00:00 (%r)" % r.rmsg())
            r.ok("Midnight's alert")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("deskcalremind: %s" % ("PASS" if not fails else
                                 "FAIL (%d): %s" % (len(fails),
                                                    "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
