#!/usr/bin/env python3
"""chromedown: the gate for SPEC.md 13.8.1 - the close/minimize DOWN state.

    python3 tools/chromedown.py                     # CGA (the default machine)
    python3 tools/chromedown.py --machine os8088_5150_herc
    python3 tools/chromedown.py --machine os8088_xt_vga

WHAT IT ASSERTS, and why each one needs its own gesture rather than a
screenshot. The feature is two halves and only the first is obvious:

  1. a held box INVERTS, and
  2. it comes back UP when the pointer slides off it while still held,

and it is the SECOND that the feature exists for (docs/MOUSEUP-PLAN.md 7
declined a static invert precisely because it cannot do it, and reads as
"held" about a gesture that is going to be cancelled). A test that only
presses and releases passes on the version that says the opposite of what is
true, so every case here holds the button down and looks at the glass BEFORE
letting go.

THE MEASUREMENT IS THE BOX'S OWN 9x9 INTERIOR AND NOTHING ELSE. Counting lit
pixels over the whole title bar folds in the pinstripes, the caption and the
mouse arrow - which is how a 42-pixel "text flash" in this tree turned out to
be the cursor blinking (PERFORMANCE.md Part 3.1) - so the window rect is read
out of the guest's own live record, the interior is derived by wm_chrome_rect's
arithmetic, and what is compared is that rect's lit count against itself.

THE ARROW IS STILL IN THE BOX, THOUGH, and it cannot be moved away without
ending the gesture, so a held box is never exactly 0 lit: the cursor is drawn
over it and its white outline lights a handful of pixels back. Measured, an
81-pixel interior reads 7 lit when down. So "inverted" is asserted as a
PROPORTION - four fifths of the interior flipped - and the exact figure is
printed rather than pinned.

AND EVERY MOVE WHILE THE BUTTON IS DOWN PASSES l=True. os88mouse's `to`
defaults to l=False, which is a Microsoft packet carrying the button's LEVEL,
so a plain `to` in the middle of a gesture RELEASES it. The whole test then
still runs and still reports numbers - the box comes up, the arm is spent -
and what it measures is a release, not a slide-off. That cost a round here.

THE 1bpp ADAPTERS ARE READ THROUGH `vram` AND VGA THROUGH `fbuf`, which is
os88marty's own default and not a preference. MartyPC's RENDERED Hercules
frame comes back 720x350 with alternating blank scan lines, so a solid white
title bar reads about 44% lit and a 9x9 box that really did inverte measures
36 lit before and 36 after - a pass on every equality check in this file and
a silent failure on every one that asks whether anything changed. `vram`
decodes SPEC.md 39.3's banked layout out of memory and is byte-comparable
with hercshot.py, which is what every "0 differing pixels" claim in this tree
already rests on. VGA has no flat framebuffer to read (mode 12h is four
planes behind the Graphics Controller), so there `fbuf` is the only route.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import os88marty
from os88marty import Marty, MartyError                      # noqa: F401
from os88mouse import Mouse

# wm.inc's record, and the two boxes' geometry from wm_draw_title. Mirrored
# here rather than derived, so that a change to either side has to be made
# twice deliberately - which is what a gate is for.
W_FLAGS, W_X, W_Y, W_W = 0, 2, 4, 6     # wm.inc, NOT the order the SDK's
                                        # comment block happens to list them in
from os88geom import WIN_SIZE   # NOT a local copy: os88geom is
                                # the one place, and it CHECKS
                                # itself against kernel/wm.inc at
                                # import - which is what caught
                                # 28 -> 30 on the next run
BOX_ROWS = (5, 13)                      # W_Y+5 .. W_Y+13, both boxes
CLOSE_COLS = (9, 17)                    # W_X+9 .. W_X+17


class Fb:
    """One rendered frame, with a lit-pixel count over a rect.

    m.fbuf() answers (w, h, rgb24), so a pixel is THREE bytes and "lit" is any
    of them non-zero. Indexing it as one byte per pixel reads a third of the
    frame at a third of the stride and produces a plausible number for the
    wrong rectangle - which is the shape of harness bug docs/MARTYPC-DEBUG.md
    records for the VGA framebuffer, met here on a CGA.
    """

    def __init__(self, m, mono):
        if mono:
            self.w, self.h, self.rows = m.vram()
            self.px = None
        else:
            self.w, self.h, self.px = m.fbuf()
            self.rows = None

    def lit(self, x1, y1, x2, y2):
        n = 0
        for y in range(max(0, y1), min(self.h, y2 + 1)):
            if self.rows is not None:
                r = self.rows[y]
                n += sum(r[x] for x in range(max(0, x1),
                                             min(self.w, x2 + 1)))
                continue
            base = y * self.w * 3
            px = self.px
            for x in range(max(0, x1), min(self.w, x2 + 1)):
                i = base + x * 3
                if px[i] or px[i + 1] or px[i + 2]:
                    n += 1
        return n


def front_window(m):
    """The frontmost visible window's record, as a LINEAR address.

    os88sym answers LINEAR (KERNEL_SEG * 16 + offset), which is what m.read
    takes - not m.readseg's offset. Mixing the two reads 0x600 bytes past
    every symbol and answers zero for a desktop with a window plainly on it.
    """
    zn = m.read(m.sym("wm_zn"), 1)[0]
    zord = m.sym("wm_zord")
    wins = m.sym("wm_wins")
    for i in range(zn - 1, -1, -1):
        idx = m.read(zord + i, 1)[0]
        rec = wins + idx * WIN_SIZE
        if _word(m, rec + W_FLAGS) & 2:
            return rec
    raise MartyError("no visible window - nothing to press")


def _word(m, addr):
    b = m.read(addr, 2)
    return b[0] | (b[1] << 8)


def _byte(m, addr):
    return m.read(addr, 1)[0]


def boxes(m, rec):
    """(close, minimize) interiors as (x1, y1, x2, y2), wm_chrome_rect's."""
    x, y, w = _word(m, rec + W_X), _word(m, rec + W_Y), _word(m, rec + W_W)
    y1, y2 = y + BOX_ROWS[0], y + BOX_ROWS[1]
    close = (x + CLOSE_COLS[0], y1, x + CLOSE_COLS[1], y2)
    mini = (x + w - 18, y1, x + w - 10, y2)
    return close, mini


def centre(r):
    return (r[0] + r[2]) // 2, (r[1] + r[3]) // 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--addr", default="127.0.0.1:9001")
    a = ap.parse_args()

    fails = []

    def check(name, ok, detail):
        print("  %-46s %s   %s" % (name, "ok " if ok else "FAIL", detail))
        if not ok:
            fails.append(name)

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          addr=a.addr) as m:
        mo = Mouse(marty=m)                 # ONE connection: a second Marty
                                            # hangs rather than erroring
        vt = m.video()["type"]
        mono = vt in ("cga", "mda", "herc")
        print("machine: %s   card %s, read through %s"
              % (a.machine, vt, "vram" if mono else "fbuf"))

        # A window to press on. The boot desktop has none (SPEC.md 29 - boot
        # is clean), so the first drive zone is double-clicked open. The zone
        # column is at the RIGHT of the screen and wraps LEFT as it fills
        # (SPEC.md 26.1), so the first one is the top of the rightmost column
        # on every adapter, which is what makes one coordinate serve all three.
        rec = None
        try:
            rec = front_window(m)
        except MartyError:
            pass
        if rec is None:
            zx, zy = (m.vram()[0] if mono else m.fbuf()[0]) - 44, 46
            print("no window on the desktop - opening drive zone (%d,%d)"
                  % (zx, zy))
            mo.dblclick(zx, zy)
            time.sleep(3)
            rec = front_window(m)

        close, mini = boxes(m, rec)
        armlit = m.sym("ui_armlit")
        armw = m.sym("ui_armw")
        print("front window record at %05X, close box %s, minimize %s"
              % (rec, close, mini))

        park = mo.where()[:2]           # where the baseline frame's arrow is
        base = Fb(m, mono)
        up_close = base.lit(*close)
        up_mini = base.lit(*mini)
        print("upright: close %d lit, minimize %d lit" % (up_close, up_mini))

        # --- 1. the close box inverts while HELD ---------------------------
        cx, cy = centre(close)
        mo.to(cx, cy)
        mo._edge(True)
        time.sleep(0.6)
        held = Fb(m, mono)
        down_close = held.lit(*close)
        lit_flag = _byte(m, armlit)
        check("close box inverts while held",
              down_close != up_close and lit_flag == 1,
              "lit %d -> %d, [ui_armlit] = %d" % (up_close, down_close,
                                                  lit_flag))
        area = (close[2] - close[0] + 1) * (close[3] - close[1] + 1)
        check("...and it is the INTERIOR that inverted",
              up_close > area * 4 // 5 and down_close < area // 5,
              "%d of %d lit upright, %d held (the rest is the arrow)"
              % (up_close, area, down_close))

        # --- 2. it comes back up when the pointer SLIDES OFF, still held ----
        # THE HALF THAT MATTERS. A static invert passes case 1 and fails here.
        mo.to(cx + 60, cy + 40, l=True)     # l=True: STILL HELD
        time.sleep(1.2)
        off = Fb(m, mono)
        check("...and comes back UP when the pointer slides off",
              off.lit(*close) == up_close and
              _byte(m, armlit) == 0,
              "lit %d (upright was %d), [ui_armlit] = %d"
              % (off.lit(*close), up_close,
                 _byte(m, armlit)))

        # --- 3. ...and DOWN AGAIN on the way back in -----------------------
        mo.to(cx, cy, l=True)
        time.sleep(1.2)
        back = Fb(m, mono)
        check("...and down again on sliding back in",
              back.lit(*close) < area // 5 and _byte(m, armlit) == 1,
              "lit %d (held was %d), [ui_armlit] = %d"
              % (back.lit(*close), down_close, _byte(m, armlit)))

        # --- 4. sliding onto the OTHER box does not light it ---------------
        # SPEC.md 13.5's release refuses a different region on the same title
        # bar, so the light has to refuse it too or it promises a fire that
        # will not happen.
        mx, my = centre(mini)
        mo.to(mx, my, l=True)
        time.sleep(1.2)
        other = Fb(m, mono)
        # The minimize box now has the ARROW on it, so it cannot be compared
        # to its own upright count; what has to be true is that it did not
        # INVERT (still mostly lit) and that the close box came back up whole.
        marea = (mini[2] - mini[0] + 1) * (mini[3] - mini[1] + 1)
        check("the OTHER box on the same bar never lights",
              other.lit(*mini) > marea * 3 // 5 and
              other.lit(*close) == up_close and _byte(m, armlit) == 0,
              "minimize %d of %d, close %d (upright %d), [ui_armlit] = %d"
              % (other.lit(*mini), marea, other.lit(*close), up_close,
                 _byte(m, armlit)))

        # --- 5. the release leaves nothing behind --------------------------
        # THE POINTER GOES BACK WHERE THE BASELINE FRAME HAD IT FIRST. The
        # arrow is drawn into the framebuffer like anything else, so comparing
        # a frame with it on the minimize box against one with it in the middle
        # of the window is a 24-pixel difference that has nothing to do with
        # the feature - PERFORMANCE.md Part 3.1's own trap, which cost this
        # tree a "text flash" that was the cursor.
        mo._edge(False)
        mo.to(*park)
        time.sleep(1.5)
        after = Fb(m, mono)
        check("the release leaves both boxes upright",
              after.lit(*close) == up_close and after.lit(*mini) == up_mini
              and _byte(m, armlit) == 0
              and _word(m, armw) == 0,
              "close %d, minimize %d, [ui_armlit] = %d, [ui_armw] = %04X"
              % (after.lit(*close), after.lit(*mini),
                 _byte(m, armlit), _word(m, armw)))

        # --- 6. the whole title bar is byte-identical afterwards -----------
        # The strongest statement available: a gesture that ends cancelled has
        # to leave the screen exactly as it found it, not merely leave the box
        # looking right. Two rows past the bar either way, so a stray pixel
        # under it is caught too.
        bx1 = _word(m, rec + W_X)
        by1 = _word(m, rec + W_Y)
        bw = _word(m, rec + W_W)
        strip = (bx1, by1, bx1 + bw - 1, by1 + 19)
        check("...and the title bar is pixel-identical to before",
              after.lit(*strip) == base.lit(*strip),
              "%d lit, was %d" % (after.lit(*strip), base.lit(*strip)))

        # --- 7. the minimize box does the same thing -----------------------
        mo.to(mx, my)
        mo._edge(True)
        time.sleep(0.6)
        mheld = Fb(m, mono)
        check("minimize box inverts while held",
              mheld.lit(*mini) != up_mini,
              "lit %d -> %d" % (up_mini, mheld.lit(*mini)))
        mo.to(mx, my + 50, l=True)      # slide off and release: cancelled, so
        time.sleep(1.2)                 # the window must still be there
        mo._edge(False)
        mo.to(*park)
        time.sleep(1.5)
        end = Fb(m, mono)
        check("...and a cancelled minimize leaves it up and the window open",
              end.lit(*mini) == up_mini and
              (_word(m, rec + W_FLAGS) & 2) != 0,
              "lit %d, W_FLAGS = %04X"
              % (end.lit(*mini), _word(m, rec + W_FLAGS)))

        # --- 8. A COMPLETED GESTURE STILL FIRES ----------------------------
        # Every case above is a CANCEL, and a kernel that had stopped closing
        # windows altogether would pass all of them. This is the one that says
        # the feature did not eat the feature it decorates.
        mo.to(cx, cy)
        mo._edge(True)
        time.sleep(0.6)
        mo._edge(False)
        time.sleep(2.0)
        check("...and a press-and-release ON the box still closes it",
              (_word(m, rec + W_FLAGS) & 2) == 0 and _byte(m, armlit) == 0,
              "W_FLAGS = %04X, [ui_armlit] = %d"
              % (_word(m, rec + W_FLAGS), _byte(m, armlit)))

    print()
    if fails:
        print("chromedown: %d FAILED - %s" % (len(fails), ", ".join(fails)))
        return 1
    print("chromedown: all cases pass on %s" % a.machine)
    return 0


if __name__ == "__main__":
    sys.exit(main())
