#!/usr/bin/env python3
"""WEAVE plays PONG.WAB and runs SHEET.WAB's handlers on kern_mega with NO
write through CS (docs/plans/KERN-MEGA-PLAN.md 14.163, WEAVE-SPEC 4.12).

    python3 tests/megaweavetrap.py                  the row
    python3 tests/megaweavetrap.py --weave IMG      ...with another WEAVE disk

kern_mega COMPLETES a store through a read-only code selector instead of
faulting on it (mega_gpw, ~880 clocks on a 286), so a package that does one
on a hot path is SLOW and looks right. WEAVE did it everywhere its VM keeps a
variable: wvm.inc staged every native call (GETP, SETP, CALLM, an impure
builtin) as eight `mov [cs:...]` stores, every handler's start and end, every
CALL and RET, every string descriptor - and WEAVE.WSM, the canvas core, took
DS from CS at each of its ~110 verbs a second. 86Box's mr286 (`faults`, its
CPU exceptions with CS:IP) counted 99,873 completed #GPs in one minute of
PONG with the ball in play: 1,663 a second, a seventh of the machine.

This boots kern_mega on 86Box's at286_vga_sb with build/weave.img in B:,
opens PONG.WAB, serves and steers (a, z) for PLAY guest seconds, then
closes it and clicks SHEET.WAB's two buttons, and:

  1. the ball is in play: found in two captures, at two places (the worker
     and its canvas core run);
  2. not ONE completed write fixup (#GP error 0000) lands in that minute of
     PONG - nor in SHEET's handlers, whose grid must change on the click;
  3. WEAVE is still open after each - a fault kern_mega answered by closing
     the package would have taken its window.

The open itself is counted but not held: a bundle's load is one-off, and
it is 0 too since 14.163.

RED (docs/WRITING-TESTS.md 1), measured: `--weave` with the WEAVE disk of
the commit before fails both 2s - 45,282 fixups in 20.6 s of PONG (wsm_entry's
pair and CS: store, wvm_nat's, wvm_getp's) and 221 in SHEET's two clicks; and
the shipped tree with wvm_nat's first `mov [es:wvm_pc], si` put back as
`mov [cs:wvm_pc], si` fails both with that one CS:IP alone, 1,743 in the
PONG window (a native call's worth: ~81 a second) and 4 in SHEET's. ~22 s.
"""
import argparse
import collections
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.environ.setdefault("OS88_MEGA", "1")
os.environ.setdefault("OS88_MEGA_EMU", "86box")
import os88build  # noqa: E402
import os88marty as M  # noqa: E402
import os88ui  # noqa: E402

MACHINE = "at286_vga_sb"
PLAY = 20.0                     # guest seconds of rally
SERVE = (27, 173)               # PONG's Serve button, VGA
SHEET_BTN = ((144, 141), (47, 141))   # SHEET's "Cider +1" and "Show total"
GPW = re.compile(r"^#GP\s+([0-9A-F]{4}):([0-9A-F]{4}) err 0000")

fails = []


def check(what, ok, more=""):
    print("%s %s%s" % ("ok  " if ok else "FAIL", what,
                       (" - " + more) if more else ""), flush=True)
    if not ok:
        fails.append(what)
    return ok


class Faults:
    """The write fixups 86Box saw since the last call, by CS:IP."""

    def __init__(self, m):
        self.m, self.seen = m, 0

    def take(self):
        lines = self.m.faults()
        new, self.seen = lines[self.seen:], len(lines)
        c = collections.Counter()
        for line in new:
            g = GPW.match(line.strip())
            if g and "mega_strcal" not in line:
                c["%s:%s" % g.groups()] += 1
        return c


def gtime(m):
    return m.status()["cycles"] / 4772727.0


def ball(path):
    """PONG's ball: the one yellow blob in the canvas (VGA)."""
    from PIL import Image
    im = Image.open(path).convert("RGB")
    px = im.load()
    for y in range(50, 170):
        for x in range(0, 240):
            r, g, b = px[x, y]
            if r > 200 and g > 200 and b < 100:
                return (x, y)
    return None


def crop(path, box):
    from PIL import Image
    return Image.open(path).convert("RGB").crop(box).tobytes()


def hold(m, key, secs):
    m.key(key, down=True, up=False)
    M.guest_sleep(m, secs)
    m.key(key, down=False, up=True)


def summary(c):
    return ", ".join("%d at %s" % (n, k) for k, n in c.most_common(6)) or "none"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--weave", default="build/weave.img",
                    help="the WEAVE disk for B: (default build/weave.img)")
    ap.add_argument("--keep", help="a directory for the captures")
    a = ap.parse_args()
    out = a.keep or os.path.join(ROOT, "build", "megaweavetrap")
    os.makedirs(out, exist_ok=True)
    disk = os88build.at(a.weave)
    with os88ui.boot(os88build.at("build/mega.img"), apps=disk,
                     machine=MACHINE, label="weavetrap") as ui:
        m = ui.m
        f = Faults(m)
        f.take()                                    # the boot's own
        w = ui.path("B:/WEAVE/PONG.WAB", limit=180)
        M.guest_sleep(m, 3)
        c = f.take()
        print("     the open: %d fixups (%s)" % (sum(c.values()), summary(c)))
        ui.mo.click(*SERVE)
        M.guest_sleep(m, 0.3)
        t0, n, seen = gtime(m), 0, []
        while gtime(m) - t0 < PLAY:
            if n % 6 == 0:
                ui.mo.click(*SERVE)
                M.guest_sleep(m, 0.3)
            if n in (3, 4):
                p = os.path.join(out, "pong%d.png" % n)
                m.b.shot(p)
                seen.append(ball(p))
            hold(m, "KeyA" if n % 2 else "KeyZ", 0.5)
            n += 1
        secs = gtime(m) - t0
        c = f.take()
        alive = ["Weave" in ui.titles()]
        check("1 the ball is in play (two captures, two places)",
              len(seen) == 2 and None not in seen and seen[0] != seen[1],
              "ball at %s" % seen)
        check("2 no write through CS in %.1f guest s of PONG" % secs,
              not c, "%d fixups: %s" % (sum(c.values()), summary(c)))
        ui.close(w)
        M.guest_sleep(m, 2)
        f.take()
        w = ui.path("B:/WEAVE/SHEET.WAB", limit=180)
        M.guest_sleep(m, 3)
        f.take()
        p0 = os.path.join(out, "sheet0.png")
        m.b.shot(p0)
        for xy in SHEET_BTN:
            ui.mo.click(*xy)
            M.guest_sleep(m, 1.5)
        p1 = os.path.join(out, "sheet1.png")
        m.b.shot(p1)
        c = f.take()
        alive.append("Weave" in ui.titles())
        grid = (0, 60, 320, 140)
        check("2 SHEET's handlers ran (the grid changed) with no write "
              "through CS", crop(p0, grid) != crop(p1, grid) and not c,
              "%d fixups: %s" % (sum(c.values()), summary(c)))
        check("3 WEAVE is still open after PONG and after SHEET", all(alive),
              "titles %s" % ui.titles())
        ui.close(w)
    if fails:
        print("megaweavetrap: FAILED: %s" % "; ".join(fails))
        return 1
    print("megaweavetrap: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
