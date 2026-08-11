#!/usr/bin/env python3
"""The WF_OWNBG gate (SPEC.md 11.90.1) - does Paint still paint EVERY PIXEL?

    python3 tools/ptcheck.py capture DIR [machine [-D...]]
    python3 tools/ptcheck.py diff DIR-A DIR-B

It is SPEC.md 11.96.10's gate as well, and for the same reason: the raise this
session drives is where a covered window is told what it owes, and Paint is the
consumer that acts on it. `make REDRAWFULL=1` is the reference either way (pass
REDRAWFULL as the third argument for that run, or every symbol lookup answers
for the wrong binary).

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

# pttest.img root, sorted: APPS FINE.BMP TEXTURE.BMP. TEXTURE (runs of 3-8
# pixels) is the default because it is a picture; PTROW=1 opens FINE instead -
# runs of 1-2, so every run sits inside ONE framebuffer byte and touches neither
# edge of it, which is SPEC.md 5.4.1's narrowest case and the one a picture
# barely exercises.
ROW_TEXTURE = int(os.environ.get("PTROW", "2"))

# SPEC.md 5.4.1.2 has TWO aligned bodies and which one runs is decided by the
# PARITY of the canvas's x - so a gate that never moves Paint proves the pixels
# of one of them. Paint's template lands its canvas on an EVEN x, and `PTNUDGE`
# is an odd sideways drag before the cover/raise, which flips it. Both phases
# are gated; tools/os88span.py reads the same variable to price them.
PTNUDGE = int(os.environ.get("PTNUDGE", "0"))
MASK_Y = 18


# subcheck's, because it MASKS THE MOUSE ARROW and this gate needs that for the
# same reason: the same build captured twice differed by 45 arrow-shaped pixels
# over a dock tile, the cursor being erased under the gfx lock and put back at
# the unlock (SPEC.md 7.1.4).
shot = sc.shot


def capture(out, machine, defines=()):
    os.makedirs(out, exist_ok=True)
    log = []
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=os.path.join(ROOT, "build/pttest.img"),
                          machine=machine) as m:
        # subcheck's rule: the reference kernel is a DIFFERENT binary, so every
        # symbol has to be looked up with the knob that built it - os88sym
        # asserts its map against build/kernel.bin and refuses rather than
        # answering with a plausible wrong address.
        if defines:
            plain = m.sym
            m.sym = lambda n, d=tuple(defines): plain(n, d)
        mo = Mouse(marty=m)
        mo.dblclick(*su.zone(m, 1)); time.sleep(4)
        disk = [w for w in su.windows(m) if w.visible][0]
        mo.dblclick(*su.row(disk, ROW_TEXTURE)); time.sleep(45)
        pt = [w for w in su.windows(m) if w.visible
              and w.title.upper().startswith("PAINT")]
        if not pt:
            raise SystemExit("ptcheck: Paint did not launch")
        pt = pt[0]
        if PTNUDGE:
            tb = sc.titlebar(m, pt)
            if not tb:
                raise SystemExit("ptcheck: no title bar to nudge Paint by")
            sc.pdrag(mo, tb[0], tb[1], tb[0] + PTNUDGE, tb[1])
            os88marty.settle(m)
            pt = [w for w in su.windows(m) if w.visible and w.i == pt.i][0]
        os88marty.settle(m)
        shot(m, "loaded", out, log, mo)

        sc.pclick(mo, *su.tile(m, disk))            # cover Paint
        os88marty.settle(m)
        shot(m, "covered", out, log, mo)

        sc.pclick(mo, *su.tile(m, pt))              # ...and raise it: the paint
        os88marty.settle(m)                         # that used to be white first
        shot(m, "raised", out, log, mo)

        # Drag the Disk window ACROSS Paint and off it. An unwritten pixel here
        # holds the DISK WINDOW's content, which white would not have hidden.
        sc.pclick(mo, *su.tile(m, disk))
        os88marty.settle(m)
        d = [w for w in su.windows(m) if w.visible and w.i == disk.i][0]
        ptn = sc.titlebar(m, d)
        if ptn:
            sc.pdrag(mo, ptn[0], ptn[1], ptn[0] + 60, ptn[1] + 40)
            os88marty.settle(m)
            shot(m, "dragged", out, log, mo)
        sc.pclick(mo, *su.tile(m, pt))              # Paint back on top
        os88marty.settle(m)
        shot(m, "reraised", out, log, mo)
        m.quit()
    print("captured %d step(s)" % len(log))


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "capture":
        capture(sys.argv[2],
                sys.argv[3] if len(sys.argv) > 3 else "os8088_5150_cga_gla",
                sys.argv[4:])         # any -D the kernel was built with
    elif len(sys.argv) == 4 and sys.argv[1] == "diff":
        sc.diff(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
