#!/usr/bin/env python3
"""WHAT DOES A STROKE SEGMENT'S SCREEN HALF COST? (SPEC.md 42.8, 42.23.8)

    make && python3 tests/paintstroke.py [--machine os8088_5150_herc_gla]

Paint rasterises a one-pixel stroke TWICE. `pt_lineseg` walks it into the
canvas and the undo image - Paint's own Bresenham, over RAM Paint owns - and
then `pt_lndraw` puts the same segment on the glass. This row measures the
SECOND of those, exactly, in guest cycles, by bracketing it between an exec
breakpoint on `pt_lndraw` and one on `pt_segdo.fpdone`, the instruction the
call returns to.

WHY THAT INTERVAL AND NOT THE STROKE. The stroke loop is TICK-BOUND (42.8.1,
tests/paintrate.py): `pt_stroke` polls the mouse and waits, so a rate measured
over the loop is the mouse's report rate and not the drawing's cost. The
bracket contains one far call and nothing else, so what it reads is the thing a
change to the screen half moves and nothing that a change to the screen half
does not.

WHAT IT IS FOR. docs/plans/completed/GFX-EMBEDDABLE-PLAN.md wave 4 asks whether the
screen half can stop being `OSAPI_GFX_LINE` - which it must before the line
family can leave the kernel at all (that plan's 8.1.5) - and the honest way to
answer it is to price both routes on the machine this OS is for rather than
from the table. PAINT-STROKE-PLAN 7 is the standing warning against quoting a
floor instead of measuring one.

WHAT IT MEASURED, on `os8088_5150_herc_gla`, same nudge schedule, same
segments, median of ~23:

    OSAPI_GFX_LINE (`PT_LNLINE`)          10,646 cycles   2,231 us
    pt_blit of the segment's rect         14,268          2,989   REFUSED
    OSAPI_GFX_BLIT1 direct (42.23.8)       9,243          1,937   SHIPPED

The middle row is the finding worth keeping: going through `pt_blit` - the
obvious spelling, and the one the plan assumed - is 34% WORSE than the line it
replaces, because `pt_blit` is the path for everything that cannot know what it
changed and pays a clip, an inked-table band walk and a decode setup before it
reaches a blit. A stroke segment DOES know what it changed. Going straight to
`OSAPI_GFX_BLIT1` with the band computed from the segment's own endpoints is
13% better than the line.

Note which route the default takes: `pt_lndraw` exists only under `PT_LNLINE`,
so the row prints the route it actually bracketed.

**IT IS A MEASUREMENT AND A FLOOR, not a calibrated figure.** The assertion is
only that the screen half is still bounded - a route that made it an order of
magnitude worse would be a regression nobody had noticed - and the number
printed is the one to quote.

**ON THE GLaBIOS TWIN** for tests/paintrate.py's reason: the IBM ROM is not in
this repository. The interval contains no `int 13h` and no BIOS call of any
kind, so the ROM cannot enter it.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispapps                                             # noqa: E402
import dispcp                                               # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HZ = 4772727.0
NUDGE_MS = 40.0                 # a mouse report is ~25ms; this is comfortably
                                # slower, so every nudge is its own segment
SEGMENTS = 24                   # ...and how many to collect
CEILING_US = 4000.0             # the screen half of ONE small segment must not
                                # cost 4 ms. Today it is ~0.6 and the point of
                                # the number is that it is not a target


def _moff(name):
    return dispapps._map("paint")[name]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    S = os88sym.linear

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        os88marty.settle(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        rows = [r[0] for r in dispcp.listing(m, S)]
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                               rows.index("PAINT.O88"))
        x, y = dispcp.row_xy(wx, wy, row)
        mo.dblclick(x, y)
        m.advance(frames=250)
        m.run()
        got = dispapps.pkg_seg(m, 0)
        if got is None:
            sys.exit("paintstroke: PAINT.O88 did not open")
        slot, seg = got[0], got[1]
        base = seg << 4
        mono = m.readseg(seg, dispapps.img_size("paint")
                         + dispapps.bss_off("paint", "pt_mono"), 1)[0]
        print("  paint in slot %d, segment %04x, canvas is %s"
              % (slot, seg, "1bpp (SPEC.md 42.23)" if mono else "4bpp"))
        if not mono:
            sys.exit("paintstroke: the canvas is not 1bpp - this row is about "
                     "the mono screen half, so run it on a 1bpp adapter")

        pr = dispcp.win_rect(m, S, slot)
        mo.to(pr[0] + 1 + 210, pr[1] + 18 + 24 + 105)       # the canvas
        m.advance(frames=30)
        m.run()
        mo._edge(True)                                      # button down
        m.advance(frames=10)
        m.run()

        # Two exec breakpoints, and their ORDER is the pairing: pt_lndraw is
        # the call and pt_segdo.fpdone the instruction it returns to, so a
        # stop at the second immediately after a stop at the first brackets
        # exactly one far call.
        # BOTH routes are bracketed, because the point of the row is to price
        # them against each other: pt_lndraw is the OSAPI_GFX_LINE arm and
        # pt_lnblit the band-out-of-the-canvas one (SPEC.md 42.23.8), and
        # which is compiled in is `PT_LNLINE`'s to say.
        # `pt_lndraw` is behind PT_LNLINE and a default build has none of it,
        # so a missing symbol is the ordinary case and not a failure.
        pm = dispapps._map("paint")
        line = (base + pm["pt_lndraw"]) if "pt_lndraw" in pm else None
        blit = base + pm["pt_lnblit"]
        leave = base + _moff("pt_segdo.fpdone")
        spans, pending, route = [], None, None

        arm = [a for a in (line, blit) if a is not None] + [leave]
        with os88marty.bp_trace(m, *arm) as tr:
            t0 = m.status()["cycles"]
            nxt = t0 + NUDGE_MS * HZ / 1000.0
            step = 0
            end = t0 + 12.0 * HZ
            while m.status()["cycles"] < end and len(tr.hits) < 4 * SEGMENTS:
                now = m.status()["cycles"]
                if now >= nxt:
                    step += 1
                    m.mouse(3, 3, l=True)   # a 45 degree chord; the
                                            # schedule above IS the spacing,
                                            # so not _pk and its GAP pause
                    nxt = now + NUDGE_MS * HZ / 1000.0
                time.sleep(0.001)

        for r in tr.hits:
            if r["addr"] in (line, blit):
                pending = r["cycles"]
                route = ("OSAPI_GFX_LINE (PT_LNLINE)" if r["addr"] == line
                         else "OSAPI_GFX_BLIT1 direct (SPEC.md 42.23.8)")
            elif r["addr"] == leave and pending is not None:
                spans.append(r["cycles"] - pending)
                pending = None

        if len(spans) < 4:
            sys.exit("paintstroke: only %d bracketed segment(s) - the stroke "
                     "did not run, or neither route is called from "
                     "pt_segdo" % len(spans))
        spans.sort()
        med = spans[len(spans) // 2]
        print("  %d segments bracketed, route = %s"
              % (len(spans), route))
        print("  screen half per segment, guest cycles:")
        print("    min %6d   median %6d   max %6d" % (spans[0], med, spans[-1]))
        print("    = %.0f us on a 4.77MHz 8088 (median)" % (med * 1e6 / HZ))
        us = med * 1e6 / HZ
        if us > CEILING_US:
            print()
            print("FAIL: the screen half of one small stroke segment is %.0f us"
                  " - over the %.0f us ceiling" % (us, CEILING_US))
            return 1
        print()
        print("ok: %.0f us a segment, under the %.0f us ceiling"
              % (us, CEILING_US))
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
