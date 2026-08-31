#!/usr/bin/env python3
"""DOES A BRUSH CHORD END WHERE IT WAS TOLD TO? (SPEC.md 42.8.3)

    make && python3 tests/paintwalk.py [--machine os8088_5150_herc_gla]

`pt_seg` walks the brush from [pt_wx],[pt_wy] to [pt_tox],[pt_toy] and is
trusted to LAND there - nothing assigns the destination at the end, the
Bresenham is simply expected to arrive.  So the assertion is the arrival:

    the brush at the head of each chord == the target of the one before it

It needs no picture, no hand, and no opinion about what a stroke should look
like, and - unlike counting the primitive calls, which is what this row did
first - it says nothing about HOW the walk emits its rects.

**Only the LIVE chords count.** Since SPEC.md 42.8.8 there is a second
consumer of `pt_segdo`: the deferred canvas replays the banked stroke through
the identical walk, so every chord reaches the breakpoint twice and a naive
reading compares a replay chord's start against the previous live chord's
target - ten "failures" of nineteen, all of them the same artifact.
`[pt_noscr]` is set for exactly the replay's duration and is the discriminator.  That mattered
one commit later: SPEC.md 42.8.5 banks a run of leading edges into one
`pt_rect` and stops calling `pt_bar_y` at all on the banked path, which would
have failed a call-count gate for being FASTER.

WHAT IT CAUGHT.  `pt_seg` kept `steps` - the denominator the error term wraps
against - in CX, which is also what `loop` counts down, so the denominator
shrank by one every iteration and the minor axis stepped ever more often as
the chord went on.  A chord of dx=1, dy=8 stepped x **four** times and landed
3 px past its own target; the next sample pulled it back and it overshot the
other way, so a wide nib drew a self-sustaining zig-zag rather than a line.
Measured here before the fix, against a hand that never deviated by more than
one pixel: bar_x 4/8/11/12 where 1/3/5/6 was owed.

It scales with chord length, so it scales with SPEED - which is why it read
from the field as "lines squiggle", why a slow drag only wobbled a little, and
why a faster machine appeared to fix it.  A one-pixel nib on a 1bpp adapter
goes to `pt_lineseg` (SPEC.md 42.8) and never reached this code at all, so the
thin pen was always straight and the wide one never was.

THE HAND IS PACED ON THE GUEST CLOCK, and it has to be.  os88mouse's
wall-clock path costs ~0.51 guest seconds a report (SPEC.md 7.3.1), so a
scripted stroke there arrives at about two reports a second, every chord is
one or two pixels long, the shrinking denominator never gets going and both
builds pass.  One packet per 25 ms of GUEST time is what 1200 baud actually
carries, and it reproduces the defect on the first chord.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(HERE)
S = os88sym.linear
HZ = 4772727.0
REPORT_MS = 25.0                # a 1200-baud report interval, in guest time
NIB = 3                         # the 8 px pencil - the widest, so the longest
                                # walk and the one a chord can drift furthest on
DY = 8                          # px a report: 320 px/s, an ordinary fast hand
REPORTS = 10


def _moff(name):
    return dispapps._map("paint")[name]


def _boff(seg, name):
    return ((seg << 4) + dispapps.img_size("paint")
            + dispapps.bss_off("paint", name))


def _bss(m, seg, name):
    return int.from_bytes(m.read(_boff(seg, name), 2), "little")


def _sw(v):
    return v - 65536 if v > 32767 else v


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)

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
            sys.exit("paintwalk: PAINT.O88 did not open")
        seg = got[1]
        cx0, cy0 = _bss(m, seg, "pt_cx0"), _bss(m, seg, "pt_cy0")

        # The widest nib, set in the app's own bss rather than by aiming at a
        # 16x11 button in the strip: what is under test is the walk, and a
        # missed click would report it as "no chords" instead of as a miss.
        m.write(_boff(seg, "pt_thick"), bytes([NIB]))
        m.advance(frames=4)
        m.run()

        mo.to(cx0 + 60, cy0 + 12)
        os88marty.settle(m)
        if mo.where()[2] & 1:
            mo._edge(False)
        mo._edge(True)
        m.advance(frames=6)
        m.run()

        base = seg << 4
        A_SEG = base + _moff("pt_segdo")
        A_RECT = base + _moff("pt_rect")
        m.bp_exec(A_SEG, A_RECT)
        t0 = m.status()["cycles"]
        per = REPORT_MS * HZ / 1000.0
        nxt, end = t0 + per, t0 + per * (REPORTS + 5)
        sent, nr, cur, chords = 0, 0, None, []
        m.run()
        while True:
            st = m.status()
            now = st["cycles"]
            if now >= end:
                break
            if st["state"] == "breakpoint":
                ip = (m.regs()["cs"] << 4) + m.regs()["ip"]
                if ip == A_SEG:
                    if m.read(_boff(seg, "pt_noscr"), 1)[0]:
                        m.run()                 # the deferred canvas replaying
                        continue                # (42.8.8), not the live walk
                    here = (_sw(_bss(m, seg, "pt_wx")), _sw(_bss(m, seg, "pt_wy")),
                            _sw(_bss(m, seg, "pt_tox")), _sw(_bss(m, seg, "pt_toy")))
                    if cur:
                        chords.append(cur + (here[0], here[1], nr))
                    nr = 0
                    cur = here
                elif ip == A_RECT:
                    nr += 1
                m.run()
            if now >= nxt and sent < REPORTS:
                # +/-1 px of wobble on a straight drag: the smallest deviation
                # a hand can have, so any excursion in the ink is this code's.
                m.mouse(dx=(1 if (sent & 1) else -1), dy=DY, l=True)
                sent += 1
                nxt = now + per
        # `cur` is the chord still being walked when the window closed - its
        # bar counts are a partial tally, not a wrong one. Only a chord CLOSED
        # by the next pt_segdo hit has been counted to the end, so the one in
        # flight is dropped rather than asserted on.
        m.breakpoints([])
        m.run()
        mo._edge(False)
        os88marty.settle(m)

    print("   chord                     landed on   pt_rect")
    bad = []
    for (wx_, wy_, tox, toy, ex, ey, nrect) in chords:
        note = ""
        if (ex, ey) != (tox, toy):
            note = "   <-- OFF BY %+d,%+d" % (ex - tox, ey - toy)
            bad.append((tox, toy, ex, ey))
        print("   %4d,%-4d -> %4d,%-4d   %4d,%-4d   %5d%s"
              % (wx_, wy_, tox, toy, ex, ey, nrect, note))
    print()
    if len(chords) < 4:
        print("paintwalk: FAIL - only %d chord(s); the drag never reached the "
              "canvas, so this run proves nothing" % len(chords))
        return 1
    if bad:
        print("paintwalk: FAIL - %d of %d chords did not land on their own "
              "target.\n  The walk is stepping an axis more often than the "
              "chord asked for, so the\n  ink is going somewhere the hand "
              "never went (SPEC.md 42.8.3)." % (len(bad), len(chords)))
        return 1
    print("paintwalk: PASS - %d chords, every one landing exactly on the "
          "point it\n  was asked for" % len(chords))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
