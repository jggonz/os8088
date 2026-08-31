#!/usr/bin/env python3
"""Is apps/paint's brush stroke TICK-BOUND? (SPEC.md 42.8.1)

    make && python3 tests/paintrate.py [--machine os8088_5150_cga_gla]

pt_stroke polls OSAPI_MOUSE, draws the segment, and waits. The wait it takes
when the pointer is where it already was used to be pt_wait_tick - a spin to
the next [ticks] boundary, up to 55 ms. At 1200 baud a mouse reports every
~25 ms and that loop turns over in about a task switch, so "not moved" almost
always meant "the next report has not arrived yet", and sleeping a whole tick
for it aliased a 40 Hz mouse down to 18.2 Hz. Every facet in a hand-drawn
curve was one of those.

MEASURED two ways on os8088_5150_herc, both matched pairs. With a counter in
gfx_lock's take and a scripted circle: 20 samples a second before - the tick,
to the digit - and 88 after. With THIS row's breakpoint and nudge schedule:
83 before, 882 after. The absolute numbers differ because the two drive the
pointer differently; the separation is an order of magnitude either way, which
is what PAINTRATE_MIN sits in the middle of.

**IT RUNS ON THE GLaBIOS TWIN**, `os8088_5150_herc_gla` - the same machine with
one field changed (same Ibm5150v256K, same 640KB, same overlays, same Hercules
MDA, same clock mode, `rom_set = "glabios_pc"`). It had to: `ibm5150_82_v4` is
the reader's own dump of the 27 OCT 82 BIOS and this repo cannot ship it, so on
a clean tree this row died inside `os88marty.launch` - "ROM set ibm5150_82_v4
not found in ROM set map" - having never reached its subject at all.

**AND THE SWAP IS MEASURED, NOT ARGUED.** With the IBM ROM dropped in beside
GLaBIOS, three runs each, same build, same disk:

    IBM 5150 82 v4     567   554   553   samples/s
    GLaBIOS            666   570   591

The two overlap. What the machines' own caveat covers is the DISK and part of
the CGA path (tools/martypc/configs/os8088_machines.toml says which), and this
window contains neither: it is WINDOW guest seconds off the guest's own cycle
counter, opened after Paint is up and the button is already down, with no int
13h in it and Hercules rather than a CGA on the bus. What turns in it is
`pt_stroke`'s poll/draw/wait and the mouse ISR - os8088's own code, same 4.77
MHz 8088, same PIT.

**NEITHER MACHINE REPRODUCES THE 882 ABOVE**, and that is a finding rather than
a rounding: 882 was taken on the IBM ROM machine and that machine now reads
553-567. The row still passes with room - the floor is 300 and tick-bound is 83
- so what drifted is the headroom, not the verdict, and the separation this
asserts is intact. Do not quietly restate 882 as 558; if the stroke loop is
meant to be where it was, the gap is the question.

The spread also has a harness component. `hits` counts breakpoint stops, and
those can be dropped when the host is loaded, so a busy machine reads LOW. That
is one more reason this row asserts a separation and not a figure.

WHAT THIS CANNOT DO is show you the curve. os88mouse's injection path costs
~0.51 guest seconds per report (SPEC.md 7.3.1), so a scripted circle is drawn
at about two reports a second where a hand manages forty; paint is then
oversampled by both builds and both pictures come out smooth. The RATE is the
evidence, and it is why this row asserts a number rather than diffing pixels.

A memory breakpoint on gfx_lock_own counts the stroke's turns - it is
written once per ACQUIRE and read almost nowhere (gfx_lock_flag would do too,
but the mouse ISR reads THAT on every packet and swamps the signal). pt_wait
brackets every one of them with an unlock and a lock. The pointer is nudged
on a GUEST-TIME schedule, not once per breakpoint, so a faster loop does not
get fed more input and inflate its own score.
"""
import argparse
import math
import os
import sys

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
PAINTRATE_MIN = 300.0           # samples/s; between 83 and 882 - the docstring
NUDGE_MS = 25.0                 # a 1200-baud report interval, in guest time
WINDOW = 4.0                    # guest seconds of stroke to measure over


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
            sys.exit("paintrate: PAINT.O88 did not open")
        slot = got[0]
        pr = dispcp.win_rect(m, S, slot)
        mo.to(pr[0] + 1 + 210, pr[1] + 18 + 24 + 105)       # the canvas
        m.advance(frames=30)
        m.run()
        mo._edge(True)
        m.advance(frames=10)
        m.run()

        m.breakpoints([{"type": "mem", "addr": S("gfx_lock_own")}])
        t0 = m.status()["cycles"]
        nxt = t0 + NUDGE_MS * HZ / 1000.0
        end = t0 + WINDOW * HZ
        hits, step = 0, 0
        while True:
            st = m.status()
            now = st["cycles"]
            if now >= end:
                break
            if st["state"] == "breakpoint":
                hits += 1
                m.run()
            if now >= nxt:                  # a report, on the guest's clock
                step += 1
                mo._pk(dx=int(6 * math.cos(step / 4.0)),
                       dy=int(6 * math.sin(step / 4.0)), l=True)
                nxt = now + NUDGE_MS * HZ / 1000.0
        dt = (m.status()["cycles"] - t0) / HZ
        m.breakpoints([])
        m.run()
        mo._edge(False)
        m.advance(frames=40)
        m.run()

    rate = hits / dt if dt else 0.0
    bad = rate < PAINTRATE_MIN
    print("  stroke turns: %d in %.2f guest s -> %.0f samples/s%s"
          % (hits, dt, rate, "   <-- FAIL" if bad else ""))
    print()
    print("paintrate: %s (floor %.0f/s; tick-bound measures 83)"
          % ("FAIL" if bad else "PASS", PAINTRATE_MIN))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
