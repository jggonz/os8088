#!/usr/bin/env python3
"""Does the one cell a display SEAM crosses still reach the glass?
(SPEC.md 39.14.11)

    make && python3 tests/dispseam.py

THIS ROW BUILDS `make NOSEAMCUT=1` ITSELF and puts the default kernel back
afterwards, which is tests/disptitle.py's shape and it is here for the reason
SPEC.md 39.14.6 wrote down at length: **a null A/B is evidence about the TEST
until the test is shown to contain the case.** This one can be null in a way
that looks exactly like a pass, and was, twice, while it was being written -
see "the case has to be arranged" below.

Reported from the field as *"on an extended desktop, if you drag a window into
a straddle, unaligned text like the title bar will lose a character."*
`font_char` enters on the display holding the cell's top-left corner and then
asks that display's `cmp cx, [vid_cwm8] / ja .done`, which draws nothing: the
cell is not clipped to this display and not re-issued on the other, so it is
dropped whole. §39.14.11 cuts it instead.

THE ASSERTION IS A DIFF AND NOT A SCREENSHOT. The same window, at two x
positions **a multiple of 8 apart in the caption's pen**, has a title bar that
is the same picture twice - the caption is centred on the window and its
sub-pixel phase does not move - so the bar drawn wholly on display 0 and the
bar reassembled across the seam have to be identical, pixel for pixel. Nothing
here photographs a title bar and squints at it; what it counts is differing
pixels, and the number owed is zero.

**The case has to be ARRANGED, and that is most of this file.** §11.94 snaps a
window's origin so that `W_X` is 7 modulo 8 on every machine, and
`wm_draw_title` centres the caption at `W_X + ((W_W - 8n) >> 1)` - so the pen's
phase is fixed by the window's WIDTH and the title's LENGTH, and dragging the
window cannot change it. The Disk window opens 322 wide with a 4-character
title, which puts the caption exactly on the 8px grid: the seam falls BETWEEN
two cells, nothing straddles, and both kernels then draw the identical picture.
So the window is GROWN by 8 first, which moves the pen by 4 and puts it off the
grid, and the phase is asserted before anything is measured.

Both orientations run, because the seam moves with the primary and §2's
arithmetic depends on it: `os8088_5150_both_gla` is a CGA primary with the seam
at 640, `os8088_5150_both_gla_mono` a Hercules one with it at 720.
"""
import argparse
import atexit
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
from os88geom import (VID_CTX_SZ, VID_CTX_VX, VID_CTX_VY,   # noqa: E402
                      VID_CTX_CW, VID_CTX_CH, WIN_SIZE, W_TITLE, KERNEL_SEG)

TITLE_H = 18
PARK = (5, 45)                  # the pointer, off every rect measured here:
                                # left of the window in both positions and
                                # below the menu bar. An arrow left ON the bar
                                # is worth five differing pixels and reads
                                # exactly like the defect


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def ctx(m, S, d):
    return m.read(S("vid_ctx") + d * VID_CTX_SZ, VID_CTX_SZ)


def title_of(m, S, slot):
    """The window's caption, read through the OWNER's segment - the kernel's,
    every window this gate opens being one of ours."""
    r = m.read(S("wm_wins") + slot * WIN_SIZE, WIN_SIZE)
    tp = u16(r, W_TITLE)
    if not tp:
        return ""
    return m.readseg(KERNEL_SEG, tp, 40).split(b"\0")[0].decode("latin-1")


def bar(m, disp, rect):
    """The virtual rect as rows of 0/1, taken from whichever display holds each
    pixel - so a straddling bar comes back as ONE picture.

    A pixel no display has reads 2 rather than 0, because "the dead zone" and
    "black" are different answers and a rect that wandered into one must not
    quietly compare equal to a rect that is merely dark."""
    x0, y0, x1, y1 = rect
    grid = [[2] * (x1 - x0) for _ in range(y1 - y0)]
    for kind, ox, oy, cw, ch in disp:
        _, _, rows = m.vram(kind=kind)
        for vy in range(y0, y1):
            ly = vy - oy
            if not (0 <= ly < ch):
                continue
            src = rows[ly]
            dst = grid[vy - y0]
            for vx in range(x0, x1):
                lx = vx - ox
                if 0 <= lx < cw:
                    dst[vx - x0] = src[lx]
    return grid


def diff(a, b):
    return sum(1 for ra, rb in zip(a, b) for x, y in zip(ra, rb) if x != y)


def lit(g):
    return sum(1 for r in g for p in r if p == 1)


def leg(image, apps, machine, defs, say):
    """One kernel on one machine: the bar drawn solo, and the same bar
    straddling. Returns (solo, straddle, phase, seam)."""
    S = lambda n: os88sym.linear(n, defines=defs)           # noqa: E731
    settle = os88marty.settle
    with os88marty.launch(image, apps=apps, machine=machine, boot=False) as m:
        if len(m.cards()) != 2:
            sys.exit("dispseam: %s is not a two-card machine" % machine)
        m.run()
        settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_panel(m, mo, S, settle)
        dispcp.set_mode(m, mo, S, settle, "right")
        dispcp.close_panel(m, mo, S, settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("dispseam: the Control Panel did not turn Extend on")

        c0, c1 = ctx(m, S, 0), ctx(m, S, 1)
        seam = u16(c1, VID_CTX_VX)
        disp = [("cga" if m.cards()[0]["type"] == "cga" else "herc",
                 0, 0, u16(c0, VID_CTX_CW), u16(c0, VID_CTX_CH)),
                ("herc" if m.cards()[1]["type"] != "cga" else "cga",
                 seam, u16(c1, VID_CTX_VY),
                 u16(c1, VID_CTX_CW), u16(c1, VID_CTX_CH))]
        say("display 0 %dx%d at (0,0), display 1 %dx%d at (%d,%d), seam %d"
            % (disp[0][3], disp[0][4], disp[1][3], disp[1][4],
               seam, disp[1][2], seam))

        dispcp.open_drive(m, mo, S, settle, "B", card=0)
        w = dispcp.win_list(m, S)
        if not w:
            sys.exit("dispseam: no Disk window")
        slot = w[-1]
        cap = title_of(m, S, slot)

        def rect():
            return dispcp.win_rect(m, S, slot)

        def pen(wx, ww):            # wm_draw_title's own arithmetic
            return wx + ((ww - 8 * len(cap)) >> 1)

        def phase(wx, ww):          # 0 = the seam falls BETWEEN two cells
            return (seam - pen(wx, ww)) % 8

        def rest():
            mo.to(*PARK)
            settle(m, card=0)
            settle(m, card=1)

        def move_to(tx):
            x, y, ww, wh = rect()
            mo.drag(x + ww // 2, y + TITLE_H // 2, tx + ww // 2,
                    y + TITLE_H // 2)
            settle(m, card=1)
            rest()
            return rect()

        def grow(dw, dh=0):
            x, y, ww, wh = rect()
            mo.drag(x + ww - 6, y + wh - 6, x + ww - 6 + dw, y + wh - 6 + dh)
            settle(m)
            rest()
            return rect()

        x, y, ww, wh = rect()
        say("title %r (%d cells), window %dx%d, pen phase %d"
            % (cap, len(cap), ww, wh, phase(x, ww)))

        # --- IT HAS TO FIT DISPLAY 1 FIRST, or the drag is not the only thing
        # that happens. SPEC.md 39.16.3 narrows a straddling window to the rows
        # the second display actually has, and on a Hercules primary the Disk
        # window is 60 rows too tall for it: the drop then RESIZED as well as
        # moved, and the window came to rest at an x the origin snap had not
        # touched - 555, not the 7-modulo-8 §11.94 leaves - which moved the
        # caption's pen phase from 4 to 0 and made the two bars different
        # pictures. That reads as a failed assertion about the kernel and is
        # nothing of the kind, which is why it is dealt with here rather than
        # tolerated below.
        d1y, d1h = disp[1][2], disp[1][4]
        room = d1y + d1h - y
        if wh > room:
            x, y, ww, wh = grow(0, room - wh)
            say("...shrunk to %dx%d, which display 1 has the rows for"
                % (ww, wh))
            if wh > d1y + d1h - y:
                sys.exit("dispseam: it will not shrink to display 1's %d rows "
                         "(still %d), so the drag below would resize as well "
                         "as move" % (d1y + d1h - y, wh))
        if phase(x, ww) == 0:
            x, y, ww, wh = grow(8)      # +8 of width moves the pen by 4
            say("...grown to %dx%d, pen phase now %d" % (ww, wh, phase(x, ww)))
        if phase(x, ww) == 0:
            sys.exit("dispseam: the caption is ON the 8px grid, so the seam "
                     "falls between two cells and NOTHING STRADDLES - this "
                     "run does not contain the case (SPEC.md 39.14.6)")

        r = move_to(200)                # wholly on display 0
        if r[0] + r[2] >= seam:
            sys.exit("dispseam: the reference position is not clear of the "
                     "seam")
        solo = bar(m, disp, (r[0], r[1], r[0] + r[2], r[1] + TITLE_H))
        ph = phase(r[0], r[2])
        say("solo     at x=%d, pen %d: %d lit" % (r[0], pen(r[0], r[2]),
                                                  lit(solo)))

        t = move_to(seam - r[2] // 2)   # ...and across the seam
        if not t[0] < seam < t[0] + t[2] - 1:
            sys.exit("dispseam: the window does not straddle the seam")
        if phase(t[0], t[2]) != ph:
            sys.exit("dispseam: the drag moved the pen's PHASE (%d -> %d): "
                     "solo (%d,%d) %dx%d against straddling (%d,%d) %dx%d, so "
                     "the two bars are not the same picture and a diff between "
                     "them means nothing"
                     % (ph, phase(t[0], t[2]), r[0], r[1], r[2], r[3],
                        t[0], t[1], t[2], t[3]))
        strad = bar(m, disp, (t[0], t[1], t[0] + t[2], t[1] + TITLE_H))
        say("straddle at x=%d, pen %d (%d px of the cell past the seam): "
            "%d lit" % (t[0], pen(t[0], t[2]), ph, lit(strad)))
        return solo, strad, ph, seam


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--machine", action="append",
                    help="repeatable; defaults to BOTH orientations")
    ap.add_argument("--no-build", action="store_true",
                    help="skip the NOSEAMCUT=1 leg and assert only the fixed "
                         "kernel - for driving an image built by hand")
    a = ap.parse_args(argv)
    machines = a.machine or ["os8088_5150_both_gla",
                             "os8088_5150_both_gla_mono"]
    fail = []
    say = lambda s: print("  " + s)                         # noqa: E731

    for mach in machines:
        print("\n=== %s: the fixed kernel ===\n" % mach)
        solo, strad, ph, seam = leg(a.image, a.apps, mach, (), say)
        d = diff(solo, strad)
        if d:
            fail.append("%s: the straddling title bar differs from the same "
                        "bar on one display by %d pixel(s) - the cell the seam "
                        "crosses is not whole (SPEC.md 39.14.11)" % (mach, d))
        else:
            say("...identical to the solo bar: 0 differing pixels")

        if a.no_build:
            continue
        print("\n=== %s: NOSEAMCUT=1, the drop it replaces ===\n" % mach)
        subprocess.check_call(["make", "NOSEAMCUT=1"], cwd=ROOT,
                              stdout=subprocess.DEVNULL)
        # Put the default kernel back however this exits, so a failure here
        # does not leave a knob kernel in build/ for the next row to drive -
        # atexit rather than a try/finally because sys.exit inside leg() is
        # how this file reports an unusable machine.
        atexit.register(subprocess.check_call, ["make"], cwd=ROOT,
                        stdout=subprocess.DEVNULL)
        bsolo, bstrad, bph, _ = leg(a.image, a.apps, mach, ("NOSEAMCUT",), say)
        bd = diff(bsolo, bstrad)
        if not bd:
            fail.append("%s: NOSEAMCUT=1 draws the SAME bar straddling as on "
                        "one display, so this run never contained the case "
                        "and the green above is about the test, not the "
                        "kernel (SPEC.md 39.14.6)" % mach)
        else:
            say("...loses %d pixel(s) of the straddling cell, which is the "
                "defect and is what the fixed kernel does not do" % bd)
        subprocess.check_call(["make"], cwd=ROOT, stdout=subprocess.DEVNULL)

    print()
    for f in fail:
        print("dispseam: FAIL: %s" % f)
    if fail:
        return 1
    print("dispseam: the cell a seam crosses is drawn on both displays and "
          "the bar is the same picture straddling as whole - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
