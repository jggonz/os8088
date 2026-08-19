#!/usr/bin/env python3
"""btndown: the gate for SPEC.md 13.8's OS88UI_DOWN - a PACKAGE's button.

    python3 tools/btndown.py
    python3 tools/btndown.py --machine os8088_5150_herc_gla
    python3 tools/btndown.py --machine os8088_xt_vga

tools/chromedown.py gates the kernel's half (the close and minimize boxes,
SPEC.md 13.8.1). This gates the other half: the flag on the SHARED control,
apps/os88ui.inc's os88ui_btn, driven through the package that uses it -
Calculator, whose keypad is twenty of them and whose press/release pair
(SPEC.md 13.7) is what a down state needs to hang off.

THE ASSERTION THAT MATTERS IS THE LAST ONE: a CANCELLED gesture leaves the
window PIXEL-IDENTICAL to before it started. That is what says the state is
idempotent, which is the whole argument for drawing the button down instead
of XOR-ing its interior (SPEC.md 13.8): an XOR that is undone by a redraw, or
a redraw that is undone by an XOR, both leave a button that looks pressed for
the rest of the session, and both would pass an "it inverts" check.

It navigates rather than being handed a window, so it also happens to be the
only end-to-end launch test in tools/: drive zone, APPS, CALC.O88.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import os88marty
from os88marty import MartyError
from os88mouse import Mouse

W_FLAGS, W_X, W_Y, W_W, W_H = 0, 2, 4, 6, 8
from os88geom import WIN_SIZE   # NOT a local copy: os88geom is
                                # the one place, and it CHECKS
                                # itself against kernel/wm.inc at
                                # import - which is what caught
                                # 28 -> 30 on the next run

# The Disk window's list rows: 16px pitch from the first row's centre, which
# is fm_layout's geometry seen from outside. The root of the system disk sorts
# APPS first (SPEC.md 19.4), and APPS holds `..` then CALC.O88 (SPEC.md 19.5's
# synthesized parent is always slot 0).
ROW0_DY, ROW_H = 46, 16


class Fb:
    def __init__(self, m, mono):
        if mono:
            self.w, self.h, self.rows = m.vram()
            self.px = None
        else:
            self.w, self.h, self.px = m.fbuf()
            self.rows = None

    def bits(self, x1, y1, x2, y2):
        """The rect as a list of 0/1, for counting AND for comparing."""
        out = []
        for y in range(max(0, y1), min(self.h, y2 + 1)):
            if self.rows is not None:
                r = self.rows[y]
                out.extend(r[x] for x in range(max(0, x1),
                                               min(self.w, x2 + 1)))
                continue
            base = y * self.w * 3
            for x in range(max(0, x1), min(self.w, x2 + 1)):
                i = base + x * 3
                out.append(1 if (self.px[i] or self.px[i + 1]
                                 or self.px[i + 2]) else 0)
        return out


def _word(m, addr):
    b = m.read(addr, 2)
    return b[0] | (b[1] << 8)


def top_window(m):
    zn = m.read(m.sym("wm_zn"), 1)[0]
    if not zn:
        raise MartyError("no window")
    idx = m.read(m.sym("wm_zord") + zn - 1, 1)[0]
    return m.sym("wm_wins") + idx * WIN_SIZE


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
        mo = Mouse(marty=m)
        vt = m.video()["type"]
        mono = vt in ("cga", "mda", "herc")
        print("machine: %s   card %s" % (a.machine, vt))

        wide = (m.vram()[0] if mono else m.fbuf()[0])
        mo.dblclick(wide - 44, 46)              # the first drive zone
        time.sleep(3)
        disk = top_window(m)
        dx, dy = _word(m, disk + W_X), _word(m, disk + W_Y)
        mo.dblclick(dx + 57, dy + ROW0_DY)      # APPS
        time.sleep(4)
        mo.dblclick(dx + 57, dy + ROW0_DY + ROW_H)      # CALC.O88
        time.sleep(9)

        rec = top_window(m)
        if rec == disk:
            raise MartyError("Calculator never opened - the Disk window is "
                             "still frontmost, so a row click missed")
        cx, cy = _word(m, rec + W_X), _word(m, rec + W_Y)
        cw, ch = _word(m, rec + W_W), _word(m, rec + W_H)
        print("Calculator at (%d,%d) %dx%d" % (cx, cy, cw, ch))
        content = (cx + 1, cy + 18, cx + cw - 2, cy + ch - 2)

        # A keypad key, a little in from the window's left edge and down in
        # the button grid. It is found by PRESSING rather than by reading the
        # package's own cal_rects, because the point of the test is the
        # pixels: a rect taken from the app would agree with the app by
        # construction, which is the drift os88ui.inc's rect pointer exists to
        # stop and not something a gate should re-introduce.
        kx, ky = cx + 35, cy + 52
        park = mo.where()[:2]

        base = Fb(m, mono)
        before = base.bits(*content)
        up = sum(before)
        print("upright: %d lit of %d in the content" % (up, len(before)))

        # --- 1. a keypad button draws DOWN under the press -----------------
        mo.to(kx, ky)
        mo._edge(True)
        time.sleep(1.0)
        held = Fb(m, mono).bits(*content)
        moved = sum(1 for p, q in zip(before, held) if p != q)
        check("a keypad button changes under the press",
              moved > 100,
              "%d pixels differ from upright (the arrow is ~15 of them)"
              % moved)

        # --- 2. ...and it is DARKER, which is what "inverted" means here ---
        # A white button with a black caption going to a black button with a
        # white caption is a large NET loss of lit pixels. A button that
        # merely redrew itself, or one whose frame moved, is not.
        check("...and it went dark rather than merely redrawing",
              sum(held) < up - 100,
              "%d lit held, %d upright (%d fewer)"
              % (sum(held), up, up - sum(held)))

        # --- 3. the release ON the button leaves it upright ----------------
        mo._edge(False)
        mo.to(*park)
        time.sleep(2.0)
        after = Fb(m, mono).bits(*content)
        # The key was pressed, so the DISPLAY has changed - that is the app
        # working. What must be back is the BUTTON, so the comparison is the
        # keypad band only, below the display.
        keypad = (cx + 1, cy + 40, cx + cw - 2, cy + ch - 2)
        kb = Fb(m, mono).bits(*keypad)
        base_kb = base.bits(*keypad)
        check("the fired button is upright again",
              kb == base_kb,
              "%d of %d keypad pixels differ"
              % (sum(1 for p, q in zip(base_kb, kb) if p != q), len(kb)))
        pre_track = Fb(m, mono).bits(*content)

        # --- 4. THE TRACKING EDGE (SPEC.md 13.8.2) -------------------------
        # The half chromedown.py proves for the chrome, now proved for a
        # PACKAGE: the key follows the pointer between the two edges. A build
        # without W_ONDRAG passes every other case in this file and fails
        # exactly these three, which is what makes them worth their runtime.
        mo.to(kx, ky)
        mo._edge(True)
        time.sleep(0.9)
        down = Fb(m, mono).bits(*content)
        mo.to(cx - 40, ky, l=True)      # off the key, STILL HELD
        time.sleep(1.2)
        away = Fb(m, mono).bits(*content)
        check("the pressed key comes UP when the pointer slides off",
              sum(1 for p, q in zip(pre_track, away) if p != q) <
              sum(1 for p, q in zip(pre_track, down) if p != q) // 4,
              "%d pixels differ from upright with the pointer off it, %d with "
              "it on" % (sum(1 for p, q in zip(pre_track, away) if p != q),
                         sum(1 for p, q in zip(pre_track, down) if p != q)))
        mo.to(kx, ky, l=True)           # ...and back on, STILL HELD
        time.sleep(1.2)
        back = Fb(m, mono).bits(*content)
        check("...and goes back DOWN on sliding back on",
              sum(1 for p, q in zip(down, back) if p != q) == 0,
              "%d pixels differ from the first held frame"
              % sum(1 for p, q in zip(down, back) if p != q))
        mo.to(cx - 40, ky, l=True)      # off again, then release: CANCELLED
        time.sleep(1.0)
        mo._edge(False)
        mo.to(*park)
        time.sleep(2.0)
        endt = Fb(m, mono).bits(*content)
        check("...and a slide-off release fires nothing",
              endt == pre_track,
              "%d of %d pixels differ from before the gesture"
              % (sum(1 for p, q in zip(pre_track, endt) if p != q),
                 len(endt)))

        # --- 5. THE ONE THAT MATTERS: a CANCELLED gesture changes nothing --
        # Press the button, slide off it, release. Nothing has happened, so
        # the whole content must be exactly what it was - and it is this that
        # an XOR-based down state gets wrong the moment anything repaints
        # between the two edges (SPEC.md 13.8).
        pre = Fb(m, mono).bits(*content)
        mo.to(kx, ky)
        mo._edge(True)
        time.sleep(0.8)
        mo.to(cx - 40, ky, l=True)      # l=True: STILL HELD. A plain `to`
        time.sleep(1.0)                 # releases and measures the wrong
                                        # thing. Sideways off the window
                                        # rather than downwards: the CGA
                                        # desktop is 200 rows and a slide
                                        # below the window runs off it
        mo._edge(False)
        mo.to(*park)
        time.sleep(2.0)
        post = Fb(m, mono).bits(*content)
        diff = sum(1 for p, q in zip(pre, post) if p != q)
        check("a CANCELLED press leaves the content pixel-identical",
              diff == 0,
              "%d of %d pixels differ" % (diff, len(pre)))

        # --- 6. THE TYPED KEY FLASHES (SPEC.md 13.9/65.9) ------------------
        # A key pressed from the KEYBOARD has no release edge, so the kernel's
        # one-shot timer is what puts it back up. Both halves are asserted -
        # that it goes down, and that it comes back up ON ITS OWN with no
        # further input, which is the half a worker task or a stuck flag would
        # fail. The pointer is parked away from the pad throughout so nothing
        # here can be the mouse arrow.
        mo.to(*park)
        time.sleep(1.0)
        idle = Fb(m, mono).bits(*keypad)
        m.key("Digit7", down=True, up=False)
        lit_seen = 0
        for _ in range(6):              # CAL_FLASH is 3 ticks (165ms); sample
            f = Fb(m, mono).bits(*keypad)   # faster than that and take the max
            d = sum(1 for p, q in zip(idle, f) if p != q)
            lit_seen = max(lit_seen, d)
        m.key("Digit7", down=False, up=True)
        check("a TYPED key presses its button",
              lit_seen > 100,
              "%d keypad pixels differed at the peak" % lit_seen)

        # ...and it lets go by itself. A full second is six flash windows.
        time.sleep(1.5)
        rested = Fb(m, mono).bits(*keypad)
        check("...and the timer lets it back up with no further input",
              rested == idle,
              "%d of %d keypad pixels differ from before the keystroke"
              % (sum(1 for p, q in zip(idle, rested) if p != q), len(idle)))

    print()
    if fails:
        print("btndown: %d FAILED - %s" % (len(fails), ", ".join(fails)))
        return 1
    print("btndown: all cases pass on %s" % a.machine)
    return 0


if __name__ == "__main__":
    sys.exit(main())
