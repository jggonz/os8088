#!/usr/bin/env python3
"""Wire Arena's Rotation window stays above the dock (SPEC.md 128.6): one
column when it fits, two when one column would reach the dock - the owner,
2026-11-15: "Free-for-all's rotation window overflows on CGA into the dock.
It can become two columns."

On MartyPC's 5150 with a CGA (a 200-line screen, the dock from the kernel's
own `vid_dock_y0`), off the game's variables and the window table:

  - FREE-FOR-ALL, all eight arenas: R opens the window in TWO columns of
    four (`wa_rrows`), its frame ending above the dock's first row, and
    every box's rect inside the window - the fifth at the top of the second
    column, beside the first;
  - LEFT AND RIGHT move the focus between the columns on its row, and
    Space on the far column's box toggles that arena (`wa_srot`);
  - TEAM DEATHMATCH, five arenas: ONE column - five rows end above the
    dock on a CGA, so the window stays as narrow as it was.

Registered as warotcga in soak; make wirearenadisk first.
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, 'tools'), os.path.join(ROOT, 'tests')]
import os88marty                                                # noqa: E402
import os88sym                                                  # noqa: E402
import os88ui                                                   # noqa: E402
from os88fixture import need                                    # noqa: E402
from walift import amap, SYS, APPS                              # noqa: E402

CK = 12                         # a check box's record (WR_CKSZ)
fails = []


def check(ok, what):
    print('  %s %s' % ('ok  ' if ok else 'FAIL', what), flush=True)
    if not ok:
        fails.append(what)


def main():
    need(APPS)
    sym = amap()
    with os88ui.boot(SYS, apps=APPS, machine="os8088_5150_cga",
                     quick=True) as ui:
        m = ui.m
        w = ui.path("B:/WIRERENA.O88")
        p = os88marty.Pkg(m, w)
        dock = struct.unpack('<H', m.read(os88sym.linear("vid_dock_y0"),
                                          2))[0]

        def opened(mode, want_rows, n):
            if mode:                    # (the mode as a user picks it: the
                for _ in range(6):      # focus down to Mode - past Detail,
                    m.key("ArrowDown")  # the slot's - Right: the setup's
                                        # Arena list follows it)
                m.key("ArrowRight")
            m.key("KeyR")
            os88marty.until(m, lambda _: p.word(sym["wa_rwin"]) != 0,
                            "the Rotation window", guest=5)
            ui.settle()
            rw = [x for x in ui.windows() if x.title == "Rotation"][0]
            rows = p.byte(sym["wa_rrows"])
            recs = [struct.unpack('<hhhh', p.read(sym["wa_rck"] + i * CK, 8))
                    for i in range(n)]
            inside = all(rw.x <= r[0] and r[2] < rw.x + rw.w and
                         rw.y <= r[1] and r[3] < rw.y + rw.h for r in recs)
            check(rows == want_rows and p.byte(sym["wa_rn"]) == n and
                  rw.y + rw.h <= dock and inside,
                  "mode %d: %d arenas in %d rows a column, the window y %d..%d "
                  "above the dock at %d, every box inside it"
                  % (mode, n, rows, rw.y, rw.y + rw.h, dock))
            return rw, recs

        rw, recs = opened(0, 4, 8)
        check(recs[4][1] == recs[0][1] and recs[4][0] > recs[3][2],
              "free-for-all: the fifth box at the top of the second column, "
              "beside the first (%r, %r)" % (recs[0][:2], recs[4][:2]))
        rot0 = p.read(sym["wa_srot"], 1)[0]
        m.key("ArrowRight")
        m.advance(cycles=4772727 // 10)
        f_right = p.byte(sym["wa_rfoc"])
        m.key("Space")
        m.advance(cycles=4772727 // 10)
        rot1 = p.read(sym["wa_srot"], 1)[0]
        m.key("ArrowLeft")
        m.advance(cycles=4772727 // 10)
        f_left = p.byte(sym["wa_rfoc"])
        check(f_right == 4 and f_left == 0 and rot1 != rot0,
              "Right to the fifth box (%d), Space toggles its arena (%02x -> "
              "%02x), Left back to the first (%d)"
              % (f_right, rot0, rot1, f_left))
        m.key("Space")                  # (both checked again)
        m.key("Escape")
        m.go()
        os88marty.until(m, lambda _: p.word(sym["wa_rwin"]) == 0,
                        "the Rotation window gone", guest=5)
        opened(2, 5, 5)
        m.key("Escape")
    if fails:
        sys.exit("warotcga: %d failure(s)" % len(fails))
    print("warotcga: ok - the Rotation window above the dock, in two columns")


if __name__ == "__main__":
    main()
