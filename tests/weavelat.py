#!/usr/bin/env python3
"""SPEC.md 7.3's click-to-action bar, with a Weave form as the load.

    make weavedisk && python3 tests/weavelat.py
    python3 tests/weavelat.py --machine os8088_5150_cga_gla

WEAVE-SPEC 12.4 names this gate: "the SPEC.md 7.3 latency bar - 37-70 ms
click-to-action, measured by tests/uilat.py's cycle counting - the JS slice
design must not push a Weave form past it".

THE QUESTION IT ASKS, and it is one 4.10's design could get wrong in a way no
functional test in this family can see. A handler runs in W_ONWAKE, which is
the one callback that does NOT hold the gfx lock, and a slice is 51-154 ms at
the contracted 10-30k ops/s. A runtime that took the lock FOR THE SLICE - the
obvious thing to write, and the thing that makes the drawing code simplest -
would hold it for that long against a 37-70 ms bar, and every screenshot in
every other Weave row would still be perfect. What this runtime does instead
is take the lock once per slice around the DIRTY SET, for as long as the
repaint takes and no longer (apps/weave/wevent.c's w_flush). This row is the
only thing that can tell those two apart.

HOW IT MEASURES, which is uilat.py's mechanism verbatim and for its reasons.
`tools/os88mouse.py` cannot answer: its injection path costs ~0.51 guest
seconds between the packet and the guest seeing the button, flat, whatever the
machine is doing - so a number taken by clicking and watching carries a
half-second floor and cannot tell 1 ms from 400 (SPEC.md 7.3.1). So the
kernel's own path is bracketed with two memory breakpoints and `cycles` is
read at each: `evq_tail` moves when the mouse ISR queues the press, and
`menu_ent` is written by menu_track once the pull-down is up. The difference
is what the OS took and nothing else.

THE THREE LEGS. An idle desktop is the control; WEAVE open with a bundle
loaded and idle is the resting cost of an app that is doing nothing (4.10:
"the wake re-posts only while a handler is unfinished or the ring is
non-empty - an idle app costs zero CPU", and a spinning wake would show here
as the desktop getting slower for no reason); and WEAVE with a handler
RUNNING is the leg the design is about. The third is armed by clicking the
form's own button and measuring while its slices are still going.

UILAT_MAX is uilat's own: well above 7.3's 37-70 ms and far below the
1,382-14,722 ms the same click cost before the lock handover existed, so the
row fails loudly if the handover is lost and does not flake on the spread.

WHAT THE THIRD LEG DOES NOT PROVE, and it is worth reading before trusting
the number. FORM's `doGreet` is about fifty bytecode ops - one slice at any
budget - so "handler running" is really "immediately after a wake was
posted". That exercises the wake, the dispatch and the flush's lock hold,
which is where a runtime that took the lock for the whole slice would already
show; it does NOT exercise a handler that spans several slices, because wave
3 ships no bundle with one. A leg that did would need a `.WAB` built for it,
and building one here would mean the row testing a bundle that no user has -
so the honest thing is to measure what the demos actually do and say what
that leaves out. The `weavevm` corpus covers the multi-slice case for
CORRECTNESS (every case runs at a budget of one); nothing yet covers it for
LATENCY.
"""

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88fixture                                       # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import uilat                                                # noqa: E402
import weavesession                                         # noqa: E402
import weavesmoke                                           # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHIP = uilat.CHIP               # the chip menu's title cell - the same target
                                # uilat clicks, so the two rows' numbers are
                                # comparable rather than merely similar


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default=weavesmoke.DISK)
    ap.add_argument("--no-make", action="store_true")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    if not a.no_make:
        # THE SAME `make`, AND IT DOES NOTHING once the runner has built the
        # artefact: Row(wants=...) declares it and os88test's prebuild builds
        # it before any row starts. That is what lets this row drop
        # builds=True and share the emulator lane.
        os88fixture.need(a.apps)
        # runner has
        # already built the artefact (Row(wants=...) and os88test's prebuild).
        # That is what lets this row drop builds=True and share the lane.
    S = os88sym.linear
    bad = 0

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        os88marty.settle(m)
        mo = os88mouse.Mouse(marty=m)
        mo.to(*CHIP)
        m.advance(frames=40)
        m.run()
        bad += uilat.report("idle desktop", uilat.click_latency(m, mo, S))

        # WEAVE, open on FORM.WAB. The navigation is weavesmoke's, imported
        # and not copied - it carries the retry and the two `until` waits.
        before, after = weavesmoke._open_bundle(m, mo, S, a.machine)
        new = sorted(after - before)
        if not new:
            print("weavelat: %s did not open" % weavesmoke.BUNDLE)
            return 1
        x, y, w, h, seg, flags = weavesmoke._win(m, S, new[0])
        mo.to(*CHIP)
        m.advance(frames=40)
        m.run()
        bad += uilat.report("weave, idle", uilat.click_latency(m, mo, S))

        # ...and with a HANDLER RUNNING. The click on the form's button posts
        # a wake; the measurement starts immediately, so the slices are still
        # going while the pull-down is asked for. A runtime holding the gfx
        # lock across a slice shows up here and nowhere else.
        vw = m.vram()[0]                        # vram answers (w, h, rows);
                                                # screen() is TEXT rows
        cx, cy, cw, ch = weavesession._content(m, S, new[0], vw)
        mo.click(cx + 4, cy + 4)                # focus the card
        m.advance(frames=20)
        m.run()
        # THE BUTTON'S PLACE COMES FROM THE ORACLE, not from a guess. A
        # hard-coded pixel is what made weavesession's first real run press
        # the card's TITLE - "Weave GREETer" - instead of its button, and
        # report the miss as a runtime that ignored three clicks.
        bx, by = weavesession._find_label(m, a.machine.split("_")[2],
                                          cx, cy, "Greet")
        mo.click(bx, by)
        mo.to(*CHIP)
        bad += uilat.report("weave, handler running",
                            uilat.click_latency(m, mo, S))

    print()
    print("weavelat: %s (budget %.0f ms)"
          % ("FAIL (%d)" % bad if bad else "PASS", uilat.UILAT_MAX * 1000))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
