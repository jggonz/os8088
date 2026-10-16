#!/usr/bin/env python3
"""SPEC.md 110.3: the Desk Calendar's REMINDERS go off - at their minute, one
a minute - and RING ON CARD 3 until dismissed; a launch sums up what it
missed. v2: there is NO ALERT on a firing any more.

    make && python3 tests/deskcalremind.py

The SHIPPED 360KB system disk copied to a scratch file, with a DESKCAL.DAT
that tools/os88stik.py wrote put into SYSTEM\\APPDATA by mtools, on MartyPC's
VGA XT (TALL: card 3 is its own card). The machine's clock is SET by writing
the live `clk_*` fields - the same block OSAPI_CLK_GET reads (SPEC.md 37.96)
- so each minute the test needs is ten guest seconds away rather than an
hour. Today is Wed 14 October 2026; the fixture holds, all with no lead:

    all day  Mum's birthday        never a reminder
    07:00    Old one               FIRED already (a v1 FIRED: DISMISSED)
    08:00    Gym                   due before the launch: MISSED
    09:30    Dentist               the first to go off
    09:31    Call Bob              due while Dentist rings
    09:33    Quarterly budget review   due too
    09:36    Typing test           due while the entry field is open
    00:00    Midnight (the 15th)   due at the rollover

  A  launched at 09:28: ONE toast, `1 missed reminder`, still the bar's line
     once the first beat's registration has written SYSTEM.CFG; Gym is FIRED
     and DUE in memory and CARD 3 RINGS IT; Old one is FIRED and NOT due (a
     v1 file's fired reminder reads as dismissed); `D` dismisses Gym and
     card 3 shows the NEXT one, Dentist at 09:30.
  B  at 09:30 Dentist goes off: the toast `09:30 Dentist`, the speaker asked
     for 880 Hz then 660 Hz four ticks each, FIRED and DUE, card 3 ringing
     it - drawn as card 3's two slabs, two BLIT1s and nothing else of the
     calendar's - and NO ALERT: the calendar's window is still the only
     one with its title, and wm_zn did not move.
  C  ONE RINGING AT A TIME: 09:31 fires Call Bob too (DUE) and card 3 still
     rings Dentist, the first due; the save the firings owe lands, held by
     nothing; Dismiss (pressed on card 3) and Call Bob rings; at 09:33
     Quarterly goes off - its toast the text last - and `D` brings it up.
  D  AN OPEN ENTRY FIELD NO LONGER HOLDS A FIRING: with the field open
     across 09:36 the Typing test goes off - and the field is still open
     and keeps the keys.
  E  THIS MINUTE IS NOT PAST: `0940 Right now` typed at 09:40 goes off on
     the next beat. A MOMENT ALREADY PAST: `0930 Late entry` typed then is
     created FIRED and NOT due - nothing rings for it; `2300 Later` is armed.
  F  the state file, after the saves: FIRED and DUE as the guest wrote them.
  G  A LAUNCH AT A REMINDER'S OWN MINUTE, and NO RTC: closed (its handle's
     x) and opened again at 23:00 with the RTC flag down: the toast is `No
     RTC - check the time`, and Later goes off on the beat after it.
  H  MIDNIGHT: at 23:59:52 the date rolls to the 15th - today's box moves -
     and Midnight, on the new day, goes off at 00:00.

**BREAK IT ON PURPOSE**: put the os88ui_ask back in dc_fire and B finds two
windows; make dc_missed set FIRED alone and A's card 3 is not ringing; drop
dc_stale's call in dc_cappt and E's Late entry rings at once; fire this
minute's moment silently in dc_stale and E's Right now never goes off.
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
import os88stik    # noqa: E402
import os88geom as geom   # noqa: E402
from deskcaledit import Cal, TITLE, disk_with, STIK_S_DIRTY  # noqa: E402
STIK_S_ROW = 2                           # os88stik.inc's stik_st: row granted
from deskcaltask import pkg_sym                 # noqa: E402

TODAY = (2026, 10, 14)
TOMORROW = (2026, 10, 15)
TONE_CELL = 0x0060 * 16 + 0x00D3        # OSAPI_SND_TONE (os88api.inc)
TOAST_AT = 0x0060 * 16 + 0x02AA         # OSAPI_TOAST
B = os88stik

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
    return B.dat(B.deskcal(appts, lead=0), x=335, y=40, screen=(640, 480))


class Rem(object):
    def __init__(self, ui, c):
        self.ui, self.c, self.m = ui, c, ui.m

    def clock(self, h, mi, s):
        self.m.write(self.ui._S("clk_sec"), bytes([s, mi, h]))

    def minute(self):
        return self.m.read(self.ui._S("clk_min"), 2)

    def appts(self):
        """The payload as it is in MEMORY, by text."""
        p = bytes(self.c.bss(B.DESKCAL_STIK_BSS + B.STIK_HDR_SIZE,
                             B.DESKCAL_PAYLEN))
        return dict((a["text"], a) for a in B.parse_deskcal(p)["appts"])

    def fired(self, text):
        return self.appts().get(text, {}).get("fired")

    def due(self, text):
        return self.appts().get(text, {}).get("due")

    def idx(self, text):
        p = bytes(self.c.bss(B.DESKCAL_STIK_BSS + B.STIK_HDR_SIZE,
                             B.DESKCAL_PAYLEN))
        return [a["text"] for a in B.parse_deskcal(p)["appts"]].index(text)

    def ringing(self):
        """The text card 3 RINGS, or None."""
        k, a, _ = self.c.c3()
        if k != 2:
            return None
        p = bytes(self.c.bss(B.DESKCAL_STIK_BSS + B.STIK_HDR_SIZE,
                             B.DESKCAL_PAYLEN))
        return B.parse_deskcal(p)["appts"][a]["text"]

    def wait_ring(self, text, what, limit=60):
        self.ui._wait(lambda: self.due(text), what, limit,
                      snapshot=lambda: "card 3 %r, clock %r"
                      % (self.c.c3(), self.minute()))

    def close(self):
        """The widget's own close - its handle's x (ui.close of a desk
        window is ui.widget_close) - and a wait long enough for what it
        does: the settings core read, the row removed, SYSTEM.CFG and the
        state file written."""
        self.ui.close(self.ui.window(TITLE), limit=90)

    def windows(self):
        return len([w for w in self.ui.windows() if w.title == TITLE])

    def past(self, h, mi, extra=4.0):
        """Run until the clock reads h:mi, then `extra` guest seconds - two
        beats - more."""
        self.ui._wait(lambda: tuple(self.minute()) == (mi, h),
                      "the clock to reach %02d:%02d" % (h, mi), 60,
                      snapshot=lambda: "clock %r" % (self.minute(),))
        os88marty.guest_sleep(self.m, extra)


def main():
    from deskcalperf import measure, Gesture
    tmp = tempfile.mkdtemp(prefix="deskcalremind-")
    try:
        img = disk_with(tmp, "remind.img", fixture())
        with os88ui.boot(img, machine="os8088_xt_vga") as ui:
            m = ui.m
            m.write(ui._S("clk_sec"), bytes([0, 28, 9]))
            c = Cal(ui, tmp, check=check)       # sets the date, opens it
            r = Rem(ui, c)

            print("A: the launch sums up what it missed")
            txt = ui.wait_toast(says="missed", limit=60)
            check(txt == "1 missed reminder", "the toast says %r" % txt)
            ui._wait(lambda: c.st() & STIK_S_ROW, "the registration", 60,
                     snapshot=lambda: "stik_st = %#x" % c.st())
            os88marty.guest_sleep(m, 1.0)
            txt, _ = ui.toast()
            check(txt == "1 missed reminder", "after the registration the "
                  "bar still says %r" % txt)
            check(r.fired("Gym") and r.due("Gym"),
                  "Gym is FIRED and DUE in memory")
            check(r.ringing() == "Gym", "...and card 3 RINGS it (%r)"
                  % (c.c3(),))
            check(r.fired("Old one") and not r.due("Old one"),
                  "Old one is FIRED and not due: a v1 fired reminder reads "
                  "as dismissed")
            check(not r.fired("Dentist"), "Dentist is not, yet")
            c.key("KeyD")
            ui._wait(lambda: not r.due("Gym"), "D to dismiss Gym", 20)
            check(c.c3() == (1, r.idx("Dentist"), 570),
                  "card 3 shows the NEXT one: Dentist at 09:30 (%r)"
                  % (c.c3(),))

            print("B: 09:30, Dentist - and no alert")
            zn = ui._byte("wm_zn")
            c3y = c.at(2, 0, 0)[1]
            def fire():
                r.clock(9, 29, 50)
                r.wait_ring("Dentist", "Dentist to go off")
                os88marty.guest_sleep(m, 2.0)   # the second note's beat
            hits, _ = measure(m, fire, extra=(TONE_CELL,))
            tones = [(h["regs"]["ax"], h["regs"]["cx"]) for h in hits
                     if h["addr"] == TONE_CELL]
            check(tones == [(880, 4), (660, 4)], "the chirp: 880 Hz then "
                  "660 Hz, four ticks each (%r)" % (tones,))
            txt, _ = ui.toast()
            check(txt == "09:30 Dentist", "the toast says %r" % txt)
            check(r.fired("Dentist") and r.ringing() == "Dentist",
                  "Dentist is FIRED, DUE, and card 3 rings it (%r)"
                  % (c.c3(),))
            gst = Gesture(hits, c.wseg)
            in3 = [h for h in gst.mine if h.get("regs")
                   and c3y <= h["regs"]["bx"] < c3y + 57]
            check(gst.n == 2 and len(in3) == 2 and gst.by == {"GFX_BLIT1": 2},
                  "the calendar drew card 3's two slabs and nothing else (%s)"
                  % gst)
            check(r.windows() == 1 and ui._byte("wm_zn") == zn,
                  "NO ALERT: one window with its title, wm_zn %d as before"
                  % ui._byte("wm_zn"))

            print("C: one ringing at a time")
            r.clock(9, 30, 50)
            r.past(9, 31)
            check(r.due("Call Bob") and r.ringing() == "Dentist",
                  "09:31: Call Bob went off too (DUE) and card 3 still rings "
                  "Dentist, the first due (%r)" % (r.ringing(),))
            ui._wait(lambda: not c.st() & STIK_S_DIRTY,
                     "the save the firings owed (nothing holds it)", 60,
                     snapshot=lambda: "stik_st %#x" % c.st())
            c.front()
            c.press(c.link(1))
            check(not r.due("Dentist") and r.ringing() == "Call Bob",
                  "Dismiss, pressed on card 3: Call Bob rings (%r)"
                  % (r.ringing(),))
            r.clock(9, 32, 52)
            r.wait_ring("Quarterly budget review", "Quarterly to go off")
            txt, _ = ui.toast()
            check(txt.startswith("09:33 Quarterly"),
                  "at 09:33 Quarterly goes off, the toast the text last (%r)"
                  % txt)
            c.key("KeyD")
            ui._wait(lambda: r.ringing() == "Quarterly budget review",
                     "D to bring Quarterly up", 20)
            check(True, "D dismissed Call Bob: Quarterly rings")

            print("D: an open entry field does not hold a firing")
            c.key("KeyN")
            ui._wait(lambda: c.ed() == 1, "the field to open", 30)
            r.clock(9, 35, 52)
            r.wait_ring("Typing test", "the Typing test to go off under the "
                        "open field")
            check(c.ed() == 1, "...and the field is still open (dc_ed %d)"
                  % c.ed())
            c.m.key("Escape")
            c.field_shut("Esc to shut the field")
            c.key("KeyD")
            ui._wait(lambda: not r.due("Quarterly budget review"),
                     "D on Quarterly", 20)
            c.key("KeyD")
            ui._wait(lambda: not r.due("Typing test"), "D on the Typing test",
                     20)

            print("E: this minute is not past; a moment already past is")
            r.clock(9, 40, 0)
            os88marty.guest_sleep(m, 2.5)       # a beat sees 09:40 first
            c.key("KeyN")
            c.type("0940 Right now")
            c.key("Enter")
            c.field_shut("the field to shut after 0940 Right now")
            r.wait_ring("Right now", "Right now to go off", 30)
            check(tuple(r.minute()) == (40, 9) and r.ringing() == "Right now",
                  "typed at 09:40, 09:40 goes off at once and rings")
            c.key("KeyD")
            ui._wait(lambda: not r.due("Right now"), "D on Right now", 20)
            for line in ("0930 Late entry", "2300 Later"):
                c.key("KeyN")
                c.type(line)
                c.key("Enter")
                c.field_shut("the field to shut after %r" % line)
            os88marty.guest_sleep(m, 3.0)
            a = r.appts()
            check(a["Late entry"]["fired"] and not a["Late entry"]["due"]
                  and r.ringing() is None,
                  "Late entry (09:30, typed at 09:40) is FIRED and not due: "
                  "nothing rings (%r)" % (c.c3(),))
            check(a["Later"]["remind"] and not a["Later"]["fired"],
                  "Later (23:00) is armed and not fired")
            check(c.c3() == (1, r.idx("Later"), 1380),
                  "...and card 3 shows it NEXT (%r)" % (c.c3(),))

            print("F: the state file")
            cal = c.saved("the save the firings owed") or {}
            got = dict((a["text"], (a["fired"], a["due"]))
                       for a in cal.get("appts", []))
            want = {"Mum's birthday": (False, False),
                    "Old one": (True, False), "Gym": (True, False),
                    "Dentist": (True, False), "Call Bob": (True, False),
                    "Quarterly budget review": (True, False),
                    "Typing test": (True, False), "Right now": (True, False),
                    "Late entry": (True, False), "Later": (False, False),
                    "Midnight": (False, False)}
            check(got == want, "FIRED / DUE as the file has it\n        got  "
                  "%r\n        want %r" % (got, want))

            print("G: a launch AT Later's minute, with no RTC")
            rtc = ui._S("clk_rtc")
            was = m.read(rtc, 1)
            r.close()
            m.write(rtc, b"\0")
            ui.m.write(ui._S("toast_buf"), b"\0")
            # bare=False: the folder's close would run inside the few
            # seconds the launch's toast is up (a loaded soak missed it);
            # nothing here presses a card the folder covers
            # EVERY toast is read at the OSAPI_TOAST cell itself, its ES:SI
            # string read while the guest is stopped: Later fires on the beat
            # after the one that says the launch's line, and its own toast
            # overwrites toast_buf before a poll of the buffer may look

            def said(mm, rec):
                rg = rec.get("regs") or mm.regs()
                raw = bytes(mm.readseg(rg["es"], rg["si"], 32))
                return raw.split(b"\0")[0].decode("latin-1")
            with os88marty.bp_trace(m, TOAST_AT, regs=True,
                                    on_hit=said) as tt:
                c = Cal(ui, tmp, check=check, bare=False,
                        before=lambda: m.write(ui._S("clk_sec"),
                                               bytes([0, 0, 23])))
                r = Rem(ui, c)
                ui._wait(lambda: any("RTC" in (h.get("hit") or "")
                                     for h in tt.hits),
                         "the launch's toast", 30)
            toasts = [h.get("hit") for h in tt.hits]
            txt = [t for t in toasts if t and "RTC" in t][0]
            now = struct.unpack("<H", bytes(c.bss(B.DESKCAL_BSS_NOW, 2)))[0]
            check(now == 23 * 60, "the entry proc read 23:00, Later's own "
                  "minute (%02d:%02d)" % divmod(now, 60))
            check(txt == "No RTC - check the time", "the toast says %r - "
                  "Later, due THIS minute, is not missed" % txt)
            m.write(rtc, bytes(was))
            r.wait_ring("Later", "Later to go off", 30)
            check(r.ringing() == "Later", "...and it goes off and rings on "
                  "the next beat")
            c.key("KeyD")
            ui._wait(lambda: not r.due("Later"), "D on Later", 20)

            print("H: midnight")
            r.clock(23, 59, 52)
            r.wait_ring("Midnight", "Midnight to go off")
            td = (struct.unpack_from("<H", bytes(c.bss(B.DESKCAL_BSS_TY,
                                                        2)))[0],
                  c.bss(B.DESKCAL_BSS_TM)[0], c.bss(B.DESKCAL_BSS_TD)[0])
            check(td == TOMORROW, "today is the 15th (%r)" % (td,))
            check(r.ringing() == "Midnight", "Midnight goes off at 00:00 and "
                  "rings")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("deskcalremind: %s" % ("PASS" if not fails else
                                 "FAIL (%d): %s" % (len(fails),
                                                    "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
