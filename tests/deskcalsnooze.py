#!/usr/bin/env python3
"""SPEC.md 110.3 (v2): the Desk Calendar's SNOOZE and DISMISS, the snooze
slot in the payload's spare words, and an old file read with nothing due.

    make && python3 tests/deskcalsnooze.py

The SHIPPED 360KB system disk, a DESKCAL.DAT that tools/os88dia.py wrote,
MartyPC's VGA XT, the clock set through the live `clk_*` fields. Today is
Wed 14 October 2026; the fixture is v1-SHAPED (no DUE, no SNOOZED, zeros at
payload +4/+6) and holds, with no lead:

    07:00  Old one     FIRED (v1): must read as already DISMISSED
    09:30  Dentist
    09:31  Call Bob
    23:57  Late call

  A  the launch at 09:29: nothing due, nothing snoozed, card 3 NOT ringing
     but NEXT - Dentist at 09:30 - and Old one's v1 FIRED is not DUE.
  B  09:30 Dentist rings, NO alert (wm_zn unchanged). `Snooze`, pressed on
     card 3: Dentist is SNOOZED and not DUE, the slot is (today, 09:35), and
     card 3 shows the NEXT one - Call Bob at 09:31, before the snooze's 09:35.
  C  09:31 Call Bob rings; `S` snoozes it - and ONE snooze is held, so
     Dentist goes back to DUE and card 3 rings Dentist; Call Bob's slot is
     09:36. `D` dismisses Dentist and card 3 shows Call Bob NEXT at its
     snooze minute, 09:36.
  D  at 09:36 the snooze RE-FIRES: Call Bob DUE, not SNOOZED, the chirp
     again (880 then 660 Hz), card 3 ringing; Dismiss, and card 3 shows
     the one still to come, Late call, NEXT.
  E  23:57 Late call rings; Snooze: the slot is TOMORROW 00:02 - the date
     at +6 is the 15th's - and the state file (flushed off A:) carries the
     SNOOZED bit and that slot, read back by tools/os88dia.py.

**BREAK IT ON PURPOSE**: take dc_snooze's "the other back to DUE" out and C
finds Call Bob ringing and Dentist quiet; make dc_rpass skip the snooze
check and D waits for a re-fire that never comes; drop dc_tomorrow and E's
slot date is the 14th.
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
from deskcaledit import Cal, disk_with, DIA_S_DIRTY   # noqa: E402
from deskcalremind import Rem, TONE_CELL      # noqa: E402

B = os88dia
TODAY = (2026, 10, 14)
fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def fixture():
    appts = [(TODAY, 420, "Old one", True, True),
             (TODAY, 570, "Dentist"),
             (TODAY, 571, "Call Bob"),
             (TODAY, 1437, "Late call")]
    return B.dat(B.deskcal(appts, lead=0))


def slot(c):
    p = bytes(c.bss(B.DESKCAL_DIA_BSS + B.DIA_HDR_SIZE + B.DESKCAL_SNZT, 4))
    t, d = struct.unpack("<HH", p)
    return B.unpack_date(d), t


def main():
    from deskcalperf import measure
    tmp = tempfile.mkdtemp(prefix="deskcalsnooze-")
    try:
        img = disk_with(tmp, "snooze.img", fixture())
        with os88ui.boot(img, machine="os8088_xt_vga") as ui:
            m = ui.m
            m.write(ui._S("clk_sec"), bytes([0, 29, 9]))
            c = Cal(ui, tmp, check=check)
            r = Rem(ui, c)
            snz = lambda t: r.appts().get(t, {}).get("snoozed")  # noqa: E731

            print("A: a v1-shaped file: nothing due, nothing snoozed")
            a = r.appts()
            check(not any(x["due"] or x["snoozed"] for x in a.values()),
                  "no appointment is DUE or SNOOZED")
            check(a["Old one"]["fired"] and r.ringing() is None,
                  "Old one's v1 FIRED reads as dismissed: card 3 is not "
                  "ringing (%r)" % (c.c3(),))
            check(c.c3() == (1, r.idx("Dentist"), 570),
                  "card 3 shows Dentist NEXT, at 09:30 (%r)" % (c.c3(),))

            print("B: Dentist rings; Snooze")
            zn = ui._byte("wm_zn")
            m.write(ui._S("clk_sec"), bytes([52, 29, 9]))
            r.wait_ring("Dentist", "Dentist to go off")
            check(r.ringing() == "Dentist" and ui._byte("wm_zn") == zn,
                  "Dentist rings on card 3 and no alert came up")
            c.front()
            c.press(c.link(0))
            ui._wait(lambda: snz("Dentist"), "Snooze to take", 20)
            check(not r.due("Dentist"), "Dentist is SNOOZED and not DUE")
            check(slot(c) == (TODAY, 575), "the slot is today at 09:35 (%r)"
                  % (slot(c),))
            check(c.c3() == (1, r.idx("Call Bob"), 571),
                  "card 3: Call Bob NEXT at 09:31, before the snooze's 09:35 "
                  "(%r)" % (c.c3(),))

            print("C: Call Bob rings; S snoozes it, and Dentist is back")
            r.wait_ring("Call Bob", "Call Bob to go off")
            c.key("KeyS")
            ui._wait(lambda: snz("Call Bob"), "S to snooze Call Bob", 20)
            check(r.due("Dentist") and not snz("Dentist"),
                  "ONE snooze is held: Dentist went back to DUE")
            check(r.ringing() == "Dentist", "...and card 3 rings it (%r)"
                  % (c.c3(),))
            check(slot(c) == (TODAY, 576), "Call Bob's slot is 09:36 (%r)"
                  % (slot(c),))
            c.key("KeyD")
            ui._wait(lambda: not r.due("Dentist"), "D to dismiss Dentist", 20)
            check(c.c3() == (1, r.idx("Call Bob"), 576),
                  "card 3: Call Bob NEXT at its snooze minute (%r)"
                  % (c.c3(),))

            print("D: the snooze re-fires at 09:36")
            m.write(ui._S("clk_sec"), bytes([52, 35, 9]))

            def refire():
                r.wait_ring("Call Bob", "Call Bob to ring again")
                os88marty.guest_sleep(m, 2.0)
            hits, _ = measure(m, refire, extra=(TONE_CELL,))
            tones = [(h["regs"]["ax"], h["regs"]["cx"]) for h in hits
                     if h["addr"] == TONE_CELL]
            check(not snz("Call Bob") and r.ringing() == "Call Bob",
                  "Call Bob DUE again, not SNOOZED, ringing")
            check(tones == [(880, 4), (660, 4)], "...with the chirp (%r)"
                  % (tones,))
            c.front()
            c.press(c.link(1))
            ui._wait(lambda: not r.due("Call Bob"), "Dismiss", 20)
            check(c.c3() == (1, r.idx("Late call"), 1437),
                  "card 3: Late call NEXT (%r)" % (c.c3(),))

            print("E: a snooze across midnight")
            m.write(ui._S("clk_sec"), bytes([52, 56, 23]))
            r.wait_ring("Late call", "Late call to go off")
            c.key("KeyS")
            ui._wait(lambda: snz("Late call"), "S on Late call", 20)
            check(slot(c) == ((2026, 10, 15), 2), "the slot is TOMORROW at "
                  "00:02 (%r)" % (slot(c),))
            cal = c.saved("the snooze's save") or {}
            lc = [x for x in cal.get("appts", []) if x["text"] == "Late call"]
            check(lc and lc[0]["snoozed"] and not lc[0]["due"]
                  and cal.get("snooze") == {"date": [2026, 10, 15],
                                            "time": 2},
                  "the state file on A: holds SNOOZED and the slot "
                  "(%r, %r)" % (lc, cal.get("snooze")))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("deskcalsnooze: %s" % ("PASS" if not fails else
                                 "FAIL (%d): %s" % (len(fails),
                                                    "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
