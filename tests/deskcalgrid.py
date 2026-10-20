#!/usr/bin/env python3
"""SPEC.md 110.3: the Desk Calendar's month is the CALENDAR's month.

    make && python3 tests/deskcalgrid.py

The grid is 42 bytes at the head of the package's bss (`DC_GRID`): byte i is
the day in week row i/7, column i%7 - MONDAY first since v2 (SPEC.md 110.3,
`dc_build`'s (dow + 6) % 7) - or 0. This reads it out
of a running guest and compares it with Python's own `calendar` - an
independent second implementation of the same date maths - for the months
that exercise it hardest:

  Oct 2026   the reference month (starts on a Thursday, 31 days, 5 rows)
  Feb 2028   a LEAP February (29 days)
  Jan 1980   the FIRST month the range has - and PgUp there is REFUSED
  Dec 2099   the LAST - and PgDn there is refused
  Mar 2028   reached from Feb 2028 by PgDn, so the navigation's own
             arithmetic is checked, not only the jump

The keys reach the WIDGET once it owns the bar: `ui.raise_window` on a desk
window is `ui.widget_activate`, a press on a card's margin (SPEC.md
11.103.4). Each month is reached the way a user reaches it: the machine's CLOCK is set
(the live `clk_*` fields poked, tests/kdreturn.py's reader inverted), the
calendar's tick sees today change (OSAPI_CLK_GET, every 36 ticks), and `T`
goes to today. So OSAPI_CLK_GET is asserted by every leg, and the selected
day and today's index come back with it.

**BREAK IT ON PURPOSE**: put dc_build back on `dow` (Sunday first) and every
leg fails; make dc_dim_of answer 28 for every February and the
Feb 2028 leg fails; drop dc_atfirst's test from dc_month and PgUp at Jan 1980
walks to Dec 1979, which the refusal leg fails.
"""
import calendar
import os
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88marty   # noqa: E402
import os88geom as geom   # noqa: E402
import os88stik    # noqa: E402

ROOT = os.path.join(HERE, "..")
BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
TITLE = "Desk Calendar"
DAY = 15                         # the day the clock is set to

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def want_grid(y, m):
    rows = calendar.Calendar(firstweekday=0).monthdayscalendar(y, m)
    flat = [d for r in rows for d in r]
    return flat + [0] * (42 - len(flat))


def main():
    tmp = tempfile.mkdtemp(prefix="deskcalgrid-")
    sysimg = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), sysimg)
    b = open(os.path.join(BDIR, "deskcal.bin"), "rb").read()
    image = struct.unpack_from("<H", b, 8)[0]

    with os88ui.boot(sysimg, machine="os8088_xt_vga") as ui:
        m = ui.m
        w = ui.path("A:/STIKAPPS/DESKCAL.O88")
        wseg = struct.unpack_from("<H", m.read(
            ui._S("wm_wins") + w.i * geom.WIN_SIZE + geom.W_SEG, 2))[0]

        def bss():
            raw = m.readseg(wseg, image, 50)
            return {"grid": list(raw[:42]),
                    "y": struct.unpack_from("<H", raw,
                                            os88stik.DESKCAL_BSS_Y)[0],
                    "m": raw[os88stik.DESKCAL_BSS_M],
                    "sel": raw[os88stik.DESKCAL_BSS_SEL],
                    "today": (struct.unpack_from(
                        "<H", raw, os88stik.DESKCAL_BSS_TY)[0],
                        raw[os88stik.DESKCAL_BSS_TM],
                        raw[os88stik.DESKCAL_BSS_TD])}

        def set_clock(y, mo, d):
            m.write(ui._S("clk_day"), bytes([d, mo]))
            m.write(ui._S("clk_year"), struct.pack("<H", y))

        def key(name):
            ui.raise_window(ui.window(TITLE))
            m.key(name)

        def shown(y, mo):
            ui._wait(lambda: (bss()["y"], bss()["m"]) == (y, mo),
                     "%d-%02d on show" % (y, mo), 30,
                     snapshot=lambda: "on show %d-%d" % (bss()["y"],
                                                         bss()["m"]))

        def leg(y, mo, how):
            got = bss()
            check(got["grid"] == want_grid(y, mo),
                  "%d-%02d (%s): the grid is calendar.monthcalendar's"
                  % (y, mo, how))
            if got["grid"] != want_grid(y, mo):
                print("        got  %r\n        want %r"
                      % (got["grid"], want_grid(y, mo)))

        for y, mo in ((2026, 10), (2028, 2), (1980, 1), (2099, 12)):
            print("%d-%02d: the clock set, then T" % (y, mo))
            set_clock(y, mo, DAY)
            ui._wait(lambda: bss()["today"] == (y, mo, DAY),
                     "the tick to see today become %d-%d-%d" % (y, mo, DAY),
                     30, snapshot=lambda: "today %r" % (bss()["today"],))
            key("KeyT")
            shown(y, mo)
            leg(y, mo, "T")
            check(bss()["sel"] == DAY, "the selected day is today's (%d)"
                  % bss()["sel"])
            if (y, mo) == (2028, 2):
                print("2028-03: PgDn from February")
                key("PageDown")
                shown(2028, 3)
                leg(2028, 3, "PgDn")
                check(bss()["sel"] == DAY, "the day is kept across the "
                      "month (%d)" % bss()["sel"])
            if (y, mo) == (1980, 1):
                key("PageUp")
                os88marty.pace(m, 3)
                check((bss()["y"], bss()["m"]) == (1980, 1),
                      "PgUp at January 1980 is refused (%d-%d)"
                      % (bss()["y"], bss()["m"]))
                key("PageDown")         # ...and the key DID arrive: the
                shown(1980, 2)          # other way still moves
                leg(1980, 2, "PgDn")
            if (y, mo) == (2099, 12):
                key("PageDown")
                os88marty.pace(m, 3)
                check((bss()["y"], bss()["m"]) == (2099, 12),
                      "PgDn at December 2099 is refused (%d-%d)"
                      % (bss()["y"], bss()["m"]))
                key("PageUp")
                shown(2099, 11)
                leg(2099, 11, "PgUp")

    shutil.rmtree(tmp, ignore_errors=True)
    print("deskcalgrid: %s" % ("PASS" if not fails else
                               "FAIL (%d): %s" % (len(fails),
                                                  "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
