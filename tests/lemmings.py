#!/usr/bin/env python3
"""LEMMINGS boots, plays Fun 1 on VGA, and the pointer leaves nothing behind.

    make lemmingsdisk && python3 tests/lemmings.py
    python3 tests/lemmings.py --png build/port-shots/   # ...and LOOK at it

SPEC.md 116.12's boot-and-screendump row, in SOAK: LEMMINGS is a PACKAGE and
`full` carries nothing app-specific. It boots QEMU with a SCRATCH COPY of
build/lemmings.img in B: (a played level writes SYSTEM/APPDATA/LEMMINGS.SAV
there, and the build's own image must not change under the next row), and
then, every step asserted rather than assumed:

  1. the Disk window opens B:, LEMMINGS/ and LEMMINGS.O88 by NAME
     (dispcp.open_named reads the listing before it clicks), and a window
     called "Lemmings" is up - the launcher;
  2. Enter on the list is Play on Fun 1: the PREVIEW comes up inside the
     bracket in the screens' second foreign mode - a 640x480 dump on VGA
     (mode 12h, SPEC.md 116.4.3), the desktop gone;
  3. Enter again plays: the dump becomes mode 0Dh's 640x400 and stays it;
  4. THE POINTER LEAVES THE WORLD AND NOTHING STAYS BEHIND (wave 5's
     verifier, carried finding A of wave 6). With the game PAUSED (F11) the
     world cannot move, so the world's rows are dumped with the pointer
     resting in the air, the pointer is swept from a dozen places in the
     world diagonally into the skill panel and into the bottom-right corner
     - start points chosen so the box's x & 7 takes every phase, which is
     what the defect needed - and brought back to the same resting place,
     and the world's rows are dumped again. They must be IDENTICAL: the
     defect was an 8x14 block of another place's terrain restored into the
     air by a save-under whose tail byte a compose had not brought up to
     date (lemdraw.c ovl_vframe, lemsprite.inc lb_clipx). The glass is
     compared with the world it showed, pixel for pixel;
  5. Esc ends the level and the POSTVIEW comes up in 12h - 640x480 again,
     the brown tile and the result text on it, not black and not the game;
  6. Esc there is the menu: the bracket returns and the launcher window is
     the one the desktop shows again (SPEC.md 116.4.6's "leave the bracket"
     row - one kernel paint, no fullscreen latch since wave 6).

IT IS QEMU AND NOT MartyPC on purpose: this is a behaviour row, and QEMU
draws mode 0Dh's split screen and pel panning faithfully enough to compare
pixels (the wave-5 verifier found the defect there). It asserts no speed.

SKIPS - exit 0, saying why - when build/lemmings.img is not there: the 26
data files are fetched and never committed (SPEC.md 116.2), so a fresh clone
without `tools/getlemmings.py` cannot build the disk, and `wants=` in
tests/suite.py is what builds it when it can.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import ethernet as E                                    # noqa: E402
import os88qemu                                         # noqa: E402
Q = os88qemu
import os88sym                                          # noqa: E402
import os88geom as G                                    # noqa: E402
import dispcp                                           # noqa: E402
import shot as SH                                       # noqa: E402

S = os88sym.linear
IMG = os.path.join(ROOT, "build", "lemmings.img")
SCRATCH = os.path.join(ROOT, "build", "lemsoak.img")

# the world is the dump's rows 0..319 in mode 0Dh (200 lines doubled to 400):
# game rows 0..159; the panel is 320..399
WORLD_ROWS = 320


def dump(m, path):
    """One screendump through QMP, read back as (w, h, rgb bytes)."""
    m.hmp('screendump "%s"' % path)
    for _ in range(50):
        if os.path.exists(path) and os.path.getsize(path) > 0:
            break
        time.sleep(0.05)
    time.sleep(0.1)
    w, h, pix = SH.read_ppm(path)
    os.unlink(path)
    return w, h, pix


def top_black(w, h, pix):
    """Is the dump's top band black? Mode 12h centres the original's 640x350
    surface in 480 lines, so its first 65 rows are the black margin; the
    desktop's are the white menu bar. That - and not the size, which the
    desktop and both screens share - is what tells the bracket's screens from
    the desktop on VGA."""
    if (w, h) != (640, 480):
        return False
    band = pix[:60 * 640 * 3]
    return band.count(0) > len(band) * 98 // 100


def wait_for(m, tmp, pred, secs, what):
    """Poll the dump until pred(w, h, pix) holds."""
    end = time.monotonic() + secs
    w = h = 0
    while time.monotonic() < end:
        w, h, pix = dump(m, tmp)
        if pred(w, h, pix):
            return pix
        Q.pace(m, 0.5)
    raise SystemExit("lemmings: FAIL - %s never came up (last dump %dx%d)"
                     % (what, w, h))


class Walk(object):
    """The pointer, moved by RELATIVE steps from a known place and tracked:
    tools/mouse.py's `to` pins against the bottom-right clamp first, and a
    pin from the right half of the world reaches the world's right column -
    the edge scroll - before the panel. Each step is at most 60 mickeys, as
    tools/mouse.py's are, and a walk goes diagonally only as far as the
    shorter axis allows."""

    def __init__(self, m, at):
        self.m = m
        self.x, self.y = at

    def walk(self, x, y):
        while (self.x, self.y) != (x, y):
            dx = max(-60, min(60, x - self.x))
            dy = max(-60, min(60, y - self.y))
            self.m.hmp("mouse_move %d %d" % (dx, dy))
            time.sleep(0.06)
            self.x += dx
            self.y += dy


def save(png, name, w, h, pix):
    if png:
        os.makedirs(png, exist_ok=True)
        SH.png(os.path.join(png, name), w, h, pix)


def fail(msg):
    print("lemmings: FAIL - " + msg)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--png", default=None,
                    help="write the screens it judged into this folder")
    a = ap.parse_args()

    if not os.path.exists(IMG):
        print("lemmings: SKIP - build/lemmings.img is not there; it needs "
              "the fetched data (tools/getlemmings.py) and `make "
              "lemmingsdisk`")
        return 0
    shutil.copyfile(IMG, SCRATCH)
    tmp = os.path.join(ROOT, "build", "lemsoak.ppm")

    os88qemu.own()                  # the teardown: make test daemonises QEMU
    subprocess.run(["make", "test", "TESTAPPS=build/lemsoak.img"], cwd=ROOT,
                   check=True, stdout=subprocess.DEVNULL)
    m = E.Qemu()
    mo = E.Mouse()
    try:
        Q.pace(m, 8)
        E.settle(m)

        # --- 1. the launcher, by name ------------------------------------
        dispcp.open_drive(m, mo, S, E.settle, "B")
        disk = next(w for w in G.windows(m, S) if w.visible and
                    w.title == "Disk")
        dispcp.open_named(m, mo, S, E.settle, disk.x, disk.y, "LEMMINGS")
        disk = next(w for w in G.windows(m, S) if w.visible and
                    w.title == "LEMMINGS")
        dispcp.open_named(m, mo, S, E.settle, disk.x, disk.y, "LEMMINGS.O88")
        Q.pace(m, 3)
        if not any(w.visible and w.title == "Lemmings"
                   for w in G.windows(m, S)):
            fail("no window called Lemmings after LEMMINGS.O88 was opened")
        print("lemmings: the launcher is up")

        # --- 2. Play: the preview in mode 12h ----------------------------
        m.hmp("sendkey ret")
        wait_for(m, tmp, top_black, 60, "the preview (mode 12h)")
        Q.pace(m, 2)
        w, h, pix = dump(m, tmp)
        save(a.png, "lemsoak-preview.png", w, h, pix)
        lit = sum(1 for i in range(0, len(pix), 3) if pix[i] | pix[i + 1] |
                  pix[i + 2])
        if lit < 640 * 480 // 4:
            fail("the preview screen is mostly black (%d lit pixels)" % lit)
        print("lemmings: the preview is up in 640x480, %d lit pixels" % lit)

        # --- 3. play: mode 0Dh -------------------------------------------
        m.hmp("sendkey ret")
        wait_for(m, tmp, lambda w, h, p: (w, h) == (640, 400), 120,
                 "the level (mode 0Dh)")
        Q.pace(m, 4)                    # the lemmings out of the trap door
        m.hmp("sendkey f11")            # PAUSED: the world cannot move
        Q.pace(m, 1)

        # --- 4. the pointer leaves the world, and nothing stays ----------
        # THE PATHS NEVER TOUCH THE WORLD'S LEFT OR RIGHT COLUMN, which is the
        # edge scroll (lemmings.c ovl_mouse_input - Lemmix's own X <= 0 /
        # X >= Img.Width - 1, and it works while paused): a moved view is a
        # changed world for a reason that is not this row's. So once the
        # pointer is placed at rest - any scroll tools/mouse.py's pin causes
        # happens BEFORE the first dump - every move is a relative walk this
        # row tracks, entering the panel vertically and leaving it the same
        # way, and crossing the screen only inside the panel.
        rest = (40, 40)                 # Fun 1's rock, top left of the view
        mo.run("to", str(rest[0]), str(rest[1]))
        Q.pace(m, 1)
        ptr = Walk(m, rest)
        w, h, before = dump(m, tmp)
        save(a.png, "lemsoak-before.png", w, h, before)
        sweeps = 0
        for gx in range(40, 300, 21):          # x & 7 takes every phase
            for gy in (70, 120):
                x, y = gx * 2, gy * 12 // 5
                if ptr.y >= 400:
                    ptr.walk(x, ptr.y)         # along the panel...
                ptr.walk(x, y)                 # ...and up into the world
                Q.pace(m, 0.2)
                ptr.walk(min(x + 90, 630), 450)   # diagonally into the panel
                Q.pace(m, 0.2)
                ptr.walk(639, 479)             # ...and into the corner
                Q.pace(m, 0.2)
                sweeps += 1
        ptr.walk(rest[0], ptr.y)
        ptr.walk(rest[0], rest[1])
        Q.pace(m, 1)
        w, h, after = dump(m, tmp)
        save(a.png, "lemsoak-after.png", w, h, after)
        if (w, h) != (640, 400):
            fail("the level's mode was not up for the sweep (%dx%d)" % (w, h))
        n = WORLD_ROWS * 640 * 3
        diff = sum(1 for i in range(0, n, 3) if before[i:i + 3] !=
                   after[i:i + 3])
        print("lemmings: %d sweeps from the world into the panel and the "
              "corner; %d world pixels differ from before" % (sweeps, diff))
        if diff:
            ys = sorted({(i // 3) // 640 for i in range(0, n, 3)
                         if before[i:i + 3] != after[i:i + 3]})
            fail("the pointer left %d pixels of the world changed, rows "
                 "%d..%d of the dump - a save-under restored over the "
                 "world (carried finding A)" % (diff, ys[0], ys[-1]))

        # --- 5. Esc: the postview in 12h ---------------------------------
        m.hmp("sendkey f11")
        Q.pace(m, 0.5)
        m.hmp("sendkey esc")
        wait_for(m, tmp, top_black, 30, "the postview (mode 12h)")
        Q.pace(m, 2)
        w, h, pix = dump(m, tmp)
        save(a.png, "lemsoak-postview.png", w, h, pix)
        mid = [pix[(y * 640 + x) * 3:(y * 640 + x) * 3 + 3]
               for y in range(150, 330, 7) for x in range(0, 640, 11)]
        coloured = sum(1 for p in mid if p != b"\0\0\0")
        # THE TILE IS A QUARTER BLACK AND MORE: MAIN.DAT's brown tile is
        # colours 0..3 and 0 is black (lemfont.inc), so a postview samples
        # about half lit (751 of 1,534 on the first run) and a blank 12h page
        # samples none
        if coloured < len(mid) // 4:
            fail("the postview's middle is mostly black (%d of %d samples "
                 "lit) - not the brown tile" % (coloured, len(mid)))
        print("lemmings: the postview is up in 640x480, %d of %d samples "
              "on the tile" % (coloured, len(mid)))

        # --- 6. Esc: the menu, which is the launcher window --------------
        m.hmp("sendkey esc")
        wait_for(m, tmp, lambda w, h, p: (w, h) == (640, 480) and
                 not top_black(w, h, p), 30, "the desktop")
        Q.pace(m, 3)
        E.settle(m)
        w, h, pix = dump(m, tmp)
        save(a.png, "lemsoak-back.png", w, h, pix)
        if not any(w2.visible and w2.title == "Lemmings"
                   for w2 in G.windows(m, S)):
            fail("the launcher window is not back after the postview's Esc")
        print("lemmings: PASS - launcher, preview, play, %d pointer sweeps "
              "with the world unchanged, postview, and back" % sweeps)
        return 0
    finally:
        Q.kill()
        try:
            os.unlink(SCRATCH)
        except OSError:
            pass


if __name__ == "__main__":
    sys.exit(main())
