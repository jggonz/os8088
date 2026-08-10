#!/usr/bin/env python3
"""The WF_OWNBG gate (SPEC.md 11.90.1) - does Paint still paint EVERY PIXEL?

    python3 tools/ptcheck.py capture DIR [machine]
    python3 tools/ptcheck.py diff DIR-A DIR-B

The flag skips `wm_draw_win`'s white fill, so the whole contract is that the
window covers its content itself. The only honest test is a pixel comparison
against a build that still gets the fill: any pixel Paint fails to write shows
whatever the fill would have whitened, and after a move that is another window's
pixels rather than white - so a run that only ever raises a window in place can
pass while the promise is broken.

The session therefore MOVES things: Paint is covered by a Disk window, raised
again, and then the Disk window is dragged clear across it, which is the case
where an unwritten pixel holds someone else's content.

A TEXTURED PICTURE, not a blank canvas. `assoc.inc` maps BMP -> PAINT, so
double-clicking one launches Paint with it (SPEC.md 54.5) and no file dialog has
to be driven. Blank is the case that hides everything: one run a row, 211 ms,
and a canvas of uniform white is exactly the colour the fill would have left.
Build the disk with:

    python3 tools/mkbmp.py 492 133 TEXTURE.BMP noise      # or any 16-colour BMP
    python3 tools/os88disk.py -o build/pttest.img --size 360 \
            APPS:build/paint.o88 TEXTURE.BMP
"""
import os
import struct
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty
from os88mouse import Mouse
import sucheck as su
import subcheck as sc

ROW_TEXTURE = 2                 # pttest.img root, sorted: APPS FINE.BMP TEXTURE.BMP
MASK_Y = 18


def shot(m, tag, out, log):
    w, bpp, data = su.fb(m)
    body = data[MASK_Y * w * bpp:]
    stem = os.path.join(out, "%02d-%s" % (len(log), tag))
    with open(stem + ".raw", "wb") as f:
        f.write(struct.pack("<HH", w, bpp) + body)
    geom = " ".join("%d:%d,%d,%d,%d" % (x.i, x.x, x.y, x.w, x.h)
                    for x in su.windows(m) if x.visible)
    with open(stem + ".txt", "w") as f:
        f.write(geom + "\n")
    log.append(tag)
    print("  %-16s %d x %s  %s" % (tag, w, bpp, geom))


def capture(out, machine):
    os.makedirs(out, exist_ok=True)
    log = []
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=os.path.join(ROOT, "build/pttest.img"),
                          machine=machine) as m:
        mo = Mouse(marty=m)
        mo.dblclick(*su.zone(m, 1)); time.sleep(4)
        disk = [w for w in su.windows(m) if w.visible][0]
        mo.dblclick(*su.row(disk, ROW_TEXTURE)); time.sleep(45)
        pt = [w for w in su.windows(m) if w.visible
              and w.title.upper().startswith("PAINT")]
        if not pt:
            raise SystemExit("ptcheck: Paint did not launch")
        pt = pt[0]
        os88marty.settle(m)
        shot(m, "loaded", out, log)

        sc.pclick(mo, *su.tile(m, disk))            # cover Paint
        os88marty.settle(m)
        shot(m, "covered", out, log)

        sc.pclick(mo, *su.tile(m, pt))              # ...and raise it: the paint
        os88marty.settle(m)                         # that used to be white first
        shot(m, "raised", out, log)

        # Drag the Disk window ACROSS Paint and off it. An unwritten pixel here
        # holds the DISK WINDOW's content, which white would not have hidden.
        sc.pclick(mo, *su.tile(m, disk))
        os88marty.settle(m)
        d = [w for w in su.windows(m) if w.visible and w.i == disk.i][0]
        ptn = sc.titlebar(m, d)
        if ptn:
            sc.pdrag(mo, ptn[0], ptn[1], ptn[0] + 60, ptn[1] + 40)
            os88marty.settle(m)
            shot(m, "dragged", out, log)
        sc.pclick(mo, *su.tile(m, pt))              # Paint back on top
        os88marty.settle(m)
        shot(m, "reraised", out, log)
        m.quit()
    print("captured %d step(s)" % len(log))


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "capture":
        capture(sys.argv[2],
                sys.argv[3] if len(sys.argv) > 3 else "os8088_5150_cga_gla")
    elif len(sys.argv) == 4 and sys.argv[1] == "diff":
        sc.diff(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
