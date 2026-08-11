#!/usr/bin/env python3
"""The SPEC.md 11.97 gate - how much of a drag's repaint is written and then
immediately overwritten.

    python3 tools/chromeflick.py [machine [frames]]

PERFORMANCE.md Part 3.1's `flicker` counts the defect this exists for: pixels
whose value BEFORE an operation and AFTER it are the same but which showed
something else in between. That is the double-draw flash as arithmetic, and it
is the one os8088 defect a pixel diff can never see - a 0-differing-pixels gate
compares two settled screens and is silent about everything between them.

The session is the field report's: two Disk windows, the front one dragged
ACROSS the back one, and the RELEASE captured. The release is where the work is
- while the button is down the mover is an XOR outline (SPEC.md 11.3 rule 2) and
nothing repaints - so the drag is driven with nothing armed and only the release
lands inside the capture window.

WHAT THE THREE NUMBERS MEAN, because only one of them is this feature's:

  VISIBLE REDRAW is how many frames changed at all. 11.97 does not move it and
    must not: the same windows are still drawn, the same pixels still end up on
    the glass.
  FRAMES WITH TRANSIENT PIXELS is how many frames flashed. It does not move
    either, because what is left flashing - the content restore, off 11.3's
    clipped list by design - is spread over the same frames.
  TOTAL TRANSIENT is the number: every pixel written and then overwritten,
    summed over the operation. **Not `worst`** - the same binary measured 891,
    280 and 902 in three consecutive runs, because which intermediate states a
    once-per-frame sampler catches depends on where the repaint lands relative
    to the raster, and one frame's peak is therefore a coin toss at this sample
    size. The sum is what survives that; quote it, and quote it over several
    runs.

Four things cost a run each here, and three of them are subcheck's:

  INJECT WHILE PAUSED, or the release races the capture window and lands in
    frame 0 or not at all.
  CHECK `settled`. False means the machine was still drawing when the frames
    ran out, and every count above it was measured against a moving target.
  READ THE BBOX, never the count alone. PERFORMANCE.md Part 3.1: 42 pixels of
    "text flash" turned out to be the mouse pointer blinking under the gfx lock,
    and the bbox said so at once.
"""
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty
from os88mouse import Mouse
import sucheck as su
import subcheck as sc


def main():
    machine = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
    frames = int(sys.argv[2]) if len(sys.argv) > 2 else 50
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=os.path.join(ROOT, "build/apps360.img"),
                          machine=machine) as m:
        mo = Mouse(marty=m)
        mo.dblclick(*su.zone(m, 1)); time.sleep(4)
        mo.dblclick(*su.zone(m, 0)); time.sleep(4)
        w = [x for x in su.windows(m) if x.visible]
        z = [i for i in sc.zorder(m) if i in [x.i for x in w]]
        if len(z) < 2:
            raise SystemExit("chromeflick: wanted two windows, got %r" % (w,))
        by = {x.i: x for x in w}
        back, front = by[z[0]], by[z[-1]]

        # Clear of the other one first, so the front window has a title bar to
        # grab and the two overlap the way the report describes.
        p = sc.titlebar(m, front)
        sc.pdrag(mo, p[0], p[1], p[0] + 90, p[1] + 60)
        f = [x for x in su.windows(m) if x.i == front.i][0]
        p = sc.titlebar(m, f)

        # THE DIRECTION IS THE WHOLE EXPERIMENT, and getting it wrong measures
        # a different operation while looking identical. SPEC.md 11.91.2 marks
        # the window underneath on the rect the mover VACATED, so the mover has
        # to leave ground inside it AND still cover part of it afterwards -
        # otherwise the lower window is not marked, is not drawn, and has no
        # chrome to flash. Dragged the other way this session redraws the mover
        # ALONE, which measures the desktop dither and says nothing at all about
        # 11.97; it read as a 4x win and then as noise, over three runs of one
        # binary.
        dx = min(70, (back.x + back.w - f.x) // 2)
        if dx < 16:
            raise SystemExit("chromeflick: %r leaves no room to vacate inside "
                             "%r - the session would measure nothing"
                             % (f, back))
        print("dragging %r +%d across %r (vacating x %d..%d, still covering "
              "x %d..%d)" % (f, dx, back, f.x, f.x + dx - 1,
                             f.x + dx, back.x + back.w))

        # Press and move with NOTHING armed; the release is the measurement.
        mo.to(*p)
        mo._edge(True)
        mo.to(p[0] + dx, p[1], l=True)
        os88marty.settle(m)
        m.pause()
        m.mouse(0, 0, l=False)
        r = m.flicker(frames=frames)

        per = r["cycles"] / r["frames"] / (r["cpu_mhz"] * 1000.0)
        total = sum(f["transient"] for f in r["per_frame"])
        print("%dx%d, %d frames, %.1f ms/frame, settled=%s"
              % (r["w"], r["h"], r["frames"], per, r["settled"]))
        print("  VISIBLE REDRAW : %2d frames = %6.0f ms"
              % (r["moved_frames"], r["moved_frames"] * per))
        print("  FLASH          : %2d frames = %6.0f ms, TOTAL %d px, worst %d"
              % (r["flash_frames"], r["flash_frames"] * per, total,
                 r["worst_transient"]))
        if not r["settled"]:
            print("  ...NOT SETTLED: ask for more frames; every count above is "
                  "measured against a moving target.")
        for fr in r["per_frame"]:
            if fr["changed"] or fr["transient"]:
                print("   frame %3d  changed %6d  transient %6d  %s"
                      % (fr["frame"], fr["changed"], fr["transient"],
                         fr["bbox"] or ""))


if __name__ == "__main__":
    main()
