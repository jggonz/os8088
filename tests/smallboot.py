#!/usr/bin/env python3
"""Does kern_small still reach a desktop now that it has no VGA renderer?

    make small && python3 tests/smallboot.py

`bootsmoke.py` asks this of the SHIPPED kernel and cannot ask it of the small
one: `all` never builds kern_small, and the only thing that does is `make
small` and test-full's `buildmatrix` row - which assembles it and stops there.
So the build that has been *discovered* broken three times rather than
reported broken (docs/KERNEL-MEMORY.md moves 22, 23 and 32) has never had a
boot behind it at all.

That mattered less while kern_small was the same code with fewer features. It
matters now: SPEC.md 39's VGA renderer is gated out of it entirely
(docs/MONO-RECLAIM-PLAN.md 2), which is ~1,800 bytes of `.text` that the
assembler proves is unreferenced and nothing else proves is unreachable. An
%ifdef that takes out one body too many assembles perfectly and dies at the
first paint.

THREE MACHINES, and the third is the point.

  * CGA and Hercules are the target class, `bootsmoke`'s pair.
  * **A VGA MACHINE**, which kern_small has no renderer for and must come up
    on anyway. vid_detect's VGA and EGA probes are gated out, so the walk
    falls through to int 11h's equipment word - which a VGA in colour mode
    answers 10b, VID_CGA - and the card is driven in BIOS mode 6, which every
    VGA BIOS serves because a VGA is a CGA superset. The desktop it draws
    should be the CGA machine's, pixel for pixel; the assertion below is
    weaker than that, but the LIT FRACTION printed beside each row is not,
    and the two agreeing to a tenth of a percent is what says the fallback is
    a real CGA path rather than a coincidence that lit up.

The assertions are bootsmoke.py's own and for its reasons - the menu bar's
white field, the black rule under it, the dock strip, a plausible lit
fraction, and the guest still executing - read from ONE capture each.

`boot=30` AND NOT `settle`, which is the one thing this cannot share with
bootsmoke. `os88marty.settle`'s gate watches for the screen to stop changing,
and on this path it never quite does: the menu-bar CLOCK ticks, which
PERFORMANCE.md Part 4 already names as the one difference the kernel is right
to make. Two runs were spent on that before it was written down.
"""
import argparse
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, "tools")
sys.path.insert(0, "tests/unit")
import os88marty                                          # noqa: E402
from harness import check, done                           # noqa: E402

# (machine, card, width, height, what it is for)
MACHINES = [
    ("os8088_5150_cga_gla",  "cga",  640, 200, "the target class"),
    ("os8088_5150_herc_gla", "herc", 720, 348, "the other 1bpp layout"),
    ("os8088_xt_vga",        "cga",  640, 200, "no renderer for this card"),
]

IMAGE = "build/small360.img"


def smoke(machine, card, want_w, want_h, why):
    t0 = time.time()
    try:
        _smoke(machine, card, want_w, want_h, why, t0)
    except os88marty.MartyError as e:
        check(False, "%s: never reached a desktop" % machine,
              "the machine did not finish booting, so nothing else here would "
              "mean what it says", got=str(e).split(" - ")[0], want="a desktop")


def _smoke(machine, card, want_w, want_h, why, t0):
    with os88marty.launch(IMAGE, apps="build/apps360.img", machine=machine,
                          boot=30, card=card) as m:
        w, h, rows = m.vram(card)
        check((w, h) == (want_w, want_h), "%s: geometry" % machine,
              "the adapter came up in a mode nobody expects - on the VGA row "
              "this is the fallback failing to be a CGA at all",
              got=(w, h), want=(want_w, want_h))
        if len(rows) < h:
            check(False, "%s: framebuffer is short" % machine, got=len(rows))
            return

        bar = sum(1 for v in rows[4] if v)
        check(bar > w * 0.9, "%s: the menu bar's white field" % machine,
              "row 4 should be almost solid white across the screen",
              got=bar, want="> %d of %d" % (int(w * 0.9), w))
        rule = sum(1 for v in rows[19] if v)
        check(rule < w * 0.1, "%s: the black rule under it" % machine,
              "row 19 separates the bar from the desktop",
              got=rule, want="< %d" % int(w * 0.1))
        dock = max(sum(1 for v in rows[y] if v) for y in range(h - 12, h - 2))
        check(dock > 0, "%s: the dock strip" % machine,
              "SPEC.md 30 - painted at boot even with no instance on it",
              got=dock)

        total = sum(1 for r in rows for v in r if v)
        frac = total / float(w * h)
        check(0.15 < frac < 0.85, "%s: the screen is plausibly lit" % machine,
              "all-black is a machine that died after vid_setmode and settles "
              "perfectly; all-white is a cleared framebuffer nothing drew on",
              got="%.3f" % frac, want="0.15 .. 0.85")

        c0 = m.status().get("cycles", 0)
        time.sleep(0.4)
        c1 = m.status().get("cycles", 0)
        check(c1 > c0, "%s: the guest is still executing" % machine,
              "a machine frozen holding the gfx lock draws a perfect desktop "
              "and never draws another", got="cycles %d -> %d" % (c0, c1),
              want="advancing")
        print("  %-22s %dx%d  %6d lit (%.1f%%)  boot %.1fs  - %s"
              % (machine, w, h, total, frac * 100, time.time() - t0, why))


def main():
    # IT BUILDS ITS OWN IMAGE, disptitle.py's shape and for the same reason:
    # `all` does not build kern_small and there is no capability to probe for,
    # so a row that needed one to be lying about would never run. `make small`
    # is idempotent and leaves build/kernel.bin alone - it builds into
    # build/smallk/ - so nothing this does disturbs the default tree.
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-build", action="store_true",
                    help="use build/small360.img as it stands")
    a = ap.parse_args()
    if not a.no_build:
        subprocess.check_call(["make", "small"], cwd=ROOT,
                              stdout=subprocess.DEVNULL)
    if not os.path.exists(os.path.join(ROOT, IMAGE)):
        check(False, "the kern_small image exists",
              "`make small` did not produce it, so there is nothing to boot",
              got="missing", want=IMAGE)
        return done("smallboot")

    for machine, card, w, h, why in MACHINES:
        smoke(machine, card, w, h, why)
    done("smallboot")


if __name__ == "__main__":
    sys.exit(main() or 0)
