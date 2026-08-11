#!/usr/bin/env python3
"""The Solitaire repaint gate (SPEC.md §43.9) — same pixels, fewer fills.

    python3 tools/solcheck.py capture DIR IMG [machine]
    python3 tools/solcheck.py diff DIR-A DIR-B

Two changes have to be proved and neither can be proved by looking:

  §43.9.1 `WF_OWNBG` — the kernel stops white-filling the content in front of
  `W_PAINT`, so any pixel `sol_drawall` fails to write now shows whatever was
  there before. After a MOVE that is another window's pixels, which is why the
  session below drags the window rather than only repainting it in place.

  §43.9.2 the pile wipes inside a full `sol_drawall` are skipped, on the
  reasoning that the whole-content felt fill already covered every pile rect
  and pile rects are disjoint. If that reasoning is wrong the symptom is one
  pile's cards left standing inside another pile's slot — a real picture, not a
  blank one, so nothing about the screen will look broken.

**THE DEAL IS RANDOM, AND THAT IS THE WHOLE DIFFICULTY.** `sol_entry` seeds
from `OSAPI_GET_TICKS`, so two runs of the same binary deal differently and a
pixel diff between two builds says nothing. `sol_newgame` takes its entropy
from `OSAPI_RAND` and nowhere else, so this writes the kernel's own
`[osapi_seed]` to a fixed word and presses N: both builds then deal the SAME
game and every pixel is comparable.

Captures are cropped to Solitaire's own frame, which is the scope of the claim
and also keeps the menu bar's clock out of the comparison.

    python3 tools/os88disk.py -o build/soltest.img --size 360 build/solitair.o88
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

SEED = 0x2A17                       # any fixed word; both builds get this one
TITLE_H = 18


def shot(m, win, path, tag):
    """The window's frame, out of the CARD's rasterised framebuffer."""
    w, h, px = m.fbuf()
    x1, y1 = max(0, win.x), max(0, win.y)
    x2, y2 = min(w, win.x + win.w), min(h, win.y + win.h)
    rows = []
    for y in range(y1, y2):
        rows.append(bytes(px[(y * w + x1) * 3:(y * w + x2) * 3]))
    blob = b"".join(rows)
    with open(path, "wb") as f:
        f.write(struct.pack("<4H", x1, y1, x2 - x1, y2 - y1))
        f.write(blob)
    print("  %-10s %dx%d at (%d,%d), %d bytes"
          % (tag, x2 - x1, y2 - y1, x1, y1, len(blob)))


def capture(outdir, img, machine):
    os.makedirs(outdir, exist_ok=True)
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=img, machine=machine) as m:
        mo = Mouse(marty=m)
        print("machine %s / %s -> %s" % (machine, img, outdir))
        mo.dblclick(*su.zone(m, 1))
        time.sleep(4)
        disk = [w for w in su.windows(m) if w.visible][0]
        mo.dblclick(*su.row(disk, 0))
        time.sleep(30)
        sol = [w for w in su.windows(m) if w.visible
               and w.title.upper().startswith("SOL")]
        if not sol:
            raise SystemExit("solcheck: Solitaire did not launch off %s" % img)
        sol = sol[0]
        print("solitaire %r" % (sol,))

        # the same deal in both builds
        m.write(m.sym("osapi_seed"), struct.pack("<H", SEED))
        sc.pclick(mo, sol.x + sol.w // 2, sol.y + sol.h - 30)
        os88marty.settle(m)
        m.write(m.sym("osapi_seed"), struct.pack("<H", SEED))
        m.key("KeyN")
        time.sleep(3)
        os88marty.settle(m)
        s = [w for w in su.windows(m) if w.visible and w.i == sol.i][0]
        shot(m, s, os.path.join(outdir, "deal.raw"), "deal")

        # ...and a MOVE, which is a kernel-driven full repaint: the raise
        # cache is dropped for the front window, so W_PAINT runs - with no
        # white fill in front of it once WF_OWNBG is promised
        p = sc.titlebar(m, s)
        sc.pdrag(mo, p[0], p[1], p[0] - 40, p[1] + 24)
        os88marty.settle(m)
        s = [w for w in su.windows(m) if w.visible and w.i == sol.i][0]
        print("moved to %r" % (s,))
        shot(m, s, os.path.join(outdir, "moved.raw"), "moved")

        # ...and once more in place, so a pile left inside another pile's slot
        # is compared against the same position drawn a second time
        m.key("KeyR")                       # Restart Deal: the SAME deal
        time.sleep(3)
        os88marty.settle(m)
        s = [w for w in su.windows(m) if w.visible and w.i == sol.i][0]
        shot(m, s, os.path.join(outdir, "restart.raw"), "restart")
        m.quit()


def diff(a, b):
    bad = 0
    for name in ("deal.raw", "moved.raw", "restart.raw"):
        pa = os.path.join(a, name)
        pb = os.path.join(b, name)
        if not (os.path.exists(pa) and os.path.exists(pb)):
            print("%-12s MISSING" % name)
            bad += 1
            continue
        da, db = open(pa, "rb").read(), open(pb, "rb").read()
        ha, hb = struct.unpack("<4H", da[:8]), struct.unpack("<4H", db[:8])
        if ha != hb:
            print("%-12s GEOMETRY DIFFERS %s vs %s" % (name, ha, hb))
            bad += 1
            continue
        ba, bb = da[8:], db[8:]
        n = sum(1 for k in range(0, min(len(ba), len(bb)), 3)
                if ba[k:k + 3] != bb[k:k + 3])
        print("%-12s %dx%d  %d differing pixels" % (name, ha[2], ha[3], n))
        bad += 1 if n else 0
    print("PASS" if not bad else "FAIL")
    return 0 if not bad else 1


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "capture":
        machine = sys.argv[4] if len(sys.argv) > 4 else "os8088_5150_cga_gla"
        capture(sys.argv[2], sys.argv[3], machine)
        return 0
    if len(sys.argv) == 4 and sys.argv[1] == "diff":
        return diff(sys.argv[2], sys.argv[3])
    raise SystemExit(__doc__)


if __name__ == "__main__":
    sys.exit(main())
