#!/usr/bin/env python3
"""Does it still boot, on every adapter, and get all the way to a desktop?

    python3 tests/bootsmoke.py                    # both 1bpp adapters
    python3 tests/bootsmoke.py --machine os8088_5150_herc_gla

The cheapest emulator test there is and the one with the widest reach: a boot
runs the boot sector, the FAT12 reader, the multi-sector `int 13h` splitter,
adapter detection, the mode set, the heap ladder, `drv_boot`, the instance
table and the whole first paint.  Anything that stops the machine reaching a
desktop is caught here in eight seconds, whatever subsystem it was in.

It is deliberately NOT a screenshot comparison.  A golden image would fail on
every legitimate pixel change - a new menu, a moved icon, the clock reading a
different minute - and a gate that cries wolf is turned off.  What it asserts
is the STRUCTURE the desktop must have on any build, taken from
`os88marty.settle`'s own boot gate (docs/MARTYPC-DEBUG.md):

  1. the menu bar's white field along the top,
  2. the black rule under it,
  3. the dock strip at the bottom,

...read from ONE framebuffer capture, because two reads can answer about a
state that never existed - the emulator runs the guest faster than real time.
Then two things a settled desktop must also be true of:

  4. a plausible amount of the screen is lit. An all-black screen settles
     perfectly well and is what a machine that died after `vid_setmode`
     looks like; an all-white one is a cleared framebuffer nothing drew on.
  5. the guest is still EXECUTING - cycles advance across a quiet interval.
     A hard-frozen machine holding the gfx lock draws a perfect desktop and
     never draws another (SPEC.md 59.7's wide toast is exactly that bug:
     `[ticks]` advances, every debug read answers, and the machine looks
     alive), so stillness alone cannot tell the two apart.

BOTH 1bpp ADAPTERS, out of one body, because they are the target class and
they differ in kind rather than in depth - 640x200 against 720x348, two
different banked layouts (SPEC.md 39.3) - and because a container without the
IBM ROM can run the GLaBIOS twins of both.
"""
import argparse
import sys
import time

sys.path.insert(0, "tools")
sys.path.insert(0, "tests/unit")
import os88marty                                          # noqa: E402
from harness import check, done                           # noqa: E402

# (machine, card, width, height) - the GLaBIOS twins, so this runs in a
# container with no IBM ROM in tools/martypc/roms/.
MACHINES = [
    ("os8088_5150_cga_gla",  "cga",  640, 200),
    ("os8088_5150_herc_gla", "herc", 720, 348),
]


# `Marty.vram()` hands back ONE BYTE PER PIXEL, already de-banked - so a row
# is `w` entries and not `w / 8` packed ones. Slicing it as if it were packed
# reads the first eighth of the row and reports it as the whole: measured, an
# all-white menu bar came back as "80 of 640 lit" and the smoke test failed on
# a perfectly good desktop. Counting non-zero is also right for the VGA path,
# where a byte is a colour index rather than a bit.
def lit(rows):
    return sum(1 for r in rows for v in r if v)


def row_lit(rows, y, w=None):
    return sum(1 for v in rows[y] if v)


def smoke(machine, card, want_w, want_h):
    t0 = time.time()
    try:
        _smoke(machine, card, want_w, want_h, t0)
    except os88marty.MartyError as e:
        # `launch`'s own boot gate raises when the desktop never appears, which
        # is exactly the failure this test exists to report - so it is reported,
        # not re-raised as a traceback the runner then prints twenty lines of.
        check(False, "%s: never reached a desktop" % machine,
              "the machine did not finish booting, so nothing else here would "
              "mean what it says", got=str(e).split(" - ")[0], want="a desktop")


def _smoke(machine, card, want_w, want_h, t0):
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine=machine) as m:
        w, h, rows = m.vram(card)
        check((w, h) == (want_w, want_h), "%s: geometry" % machine,
              "the adapter came up in a mode nobody expects",
              got=(w, h), want=(want_w, want_h))
        if len(rows) < h:
            check(False, "%s: framebuffer is short" % machine, got=len(rows))
            return

        # 1/2/3 - the desktop's structure, from ONE capture.
        bar = row_lit(rows, 4)
        check(bar > w * 0.9, "%s: the menu bar's white field is drawn" % machine,
              "row 4 should be almost solid white across the screen",
              got=bar, want="> %d of %d" % (int(w * 0.9), w))
        rule = row_lit(rows, 19)
        check(rule < w * 0.1, "%s: the black rule under the menu bar" % machine,
              "row 19 is the rule that separates the bar from the desktop",
              got=rule, want="< %d" % int(w * 0.1))
        dock = max(row_lit(rows, y) for y in range(h - 12, h - 2))
        check(dock > 0, "%s: the dock strip is drawn" % machine,
              "SPEC.md 30 - the bottom strip is painted at boot even with no "
              "instance on it", got=dock)

        # 4 - a plausible amount of the screen is lit.
        total = lit(rows)
        frac = total / float(w * h)
        check(0.15 < frac < 0.85, "%s: %.0f%% of the screen is lit" % (machine, frac * 100),
              "all-black is a machine that died after vid_setmode and settles "
              "perfectly; all-white is a cleared framebuffer nothing drew on",
              got="%.3f" % frac, want="0.15 .. 0.85")

        # 5 - and it is still executing.
        c0 = m.status().get("cycles", 0)
        time.sleep(0.4)
        c1 = m.status().get("cycles", 0)
        check(c1 > c0, "%s: the guest is still executing" % machine,
              "a machine frozen holding the gfx lock draws a perfect desktop and "
              "never draws another - stillness alone cannot see it",
              got="cycles %d -> %d" % (c0, c1), want="advancing")
        print("  %-24s %dx%d  %6d lit (%.0f%%)  boot %.1fs"
              % (machine, w, h, total, frac * 100, time.time() - t0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", help="just this one")
    a = ap.parse_args()
    rows = [r for r in MACHINES if not a.machine or r[0] == a.machine]
    if not rows:
        print("bootsmoke: no such machine", file=sys.stderr)
        return 2
    for machine, card, w, h in rows:
        smoke(machine, card, w, h)
    done("bootsmoke")


if __name__ == "__main__":
    sys.exit(main() or 0)
