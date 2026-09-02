#!/usr/bin/env python3
"""Does a window DRAGGED across the seam arrive on both displays?
(SPEC.md 11.96.14 / 39.14.8)

    make && python3 tests/dispdrag.py --dump /tmp/live.bin
    make DRAGCACHE=0 && python3 tests/dispdrag.py --image /tmp/ref.img \\
                                                  --expect /tmp/live.bin

Reported from the field as *"drag a window to straddle the two screens and
the primary draws and the secondary does not; drag it AGAIN while it
straddles and it draws correctly."*

Both halves are one mechanism. SPEC.md 11.96.12's drag cache banks the
window's content at the old position and replays it at the new one instead of
ordering a W_PAINT - and the replay is a `gfx_restore`, which is whole-shape
and cannot span two cards (39.14.8). The gate that used to refuse that,
`vid_span_one`, was replaced at 11.96.14 by a CLAMP so that a window dragged
off the bottom of the screen could still replay the rows it kept. A clamp is
right when what it removes is off the glass and wrong when what it removes is
the OTHER MONITOR: the restore then covers the primary's slice, `wm_draw_win`
reads that as "the content is already back" and skips both the white fill and
W_PAINT, and the secondary's slice is never drawn by anything.

The second drag works because its SOURCE straddles, and `wm_su_take` still
asks `vid_span_one` - so no cache is taken, and the window redraws in full.

THE ASSERTION IS THE REFERENCE BUILD, not a screenshot, because what a broken
run leaves on the second display is whatever was there before - desktop
dither, or another window - which is a picture rather than an error.
`make DRAGCACHE=0` removes the cache entirely, so the same scripted session
through both kernels must put the same pixels on both cards.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
from os88geom import (VID_CTX_SZ, VID_CTX_VX,          # noqa: E402
                      VID_CTX_VY, VID_CTX_CW, VID_CTX_CH)
# SPEC.md 39.14's per-display record: DERIVED from VID_CTX_W and never
# written down here. Nine scripts had `VID_CTX_SZ = 42` by hand and the
# record has grown TWICE under them - the last time silently, because the
# constant that moved was a DERIVED one and os88geom's scanner was only
# looking at the mirrored ones. It is looking at both now.

TITLE_H = 18
S = os88sym.linear


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def flat(rows):
    """`vram` answers rows of BIT VALUES - one entry per pixel, not packed -
    so this is a pixel per byte and every count below is a pixel count."""
    out = bytearray()
    for r in rows:
        out += bytes(r)
    return bytes(out)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--layout", default="right", choices=("right", "below"),
                    help="which seam to drag across - the two are separate "
                         "arms of wm_su_vset and only one of them is x")
    ap.add_argument("--dump", help="write the second card's framebuffer here")
    ap.add_argument("--expect", help="...and compare against this one")
    a = ap.parse_args(argv)

    fail = []
    say = lambda s: print("  " + s)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        cards = m.cards()
        if len(cards) != 2:
            sys.exit("dispdrag: %s has %d video card(s), not 2"
                     % (a.machine, len(cards)))
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, a.layout)
        dispcp.close_panel(m, mo, S, os88marty.settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("dispdrag: the Control Panel did not turn Extend on")

        kind = m.read(S("vid_kind"), 1)[0]
        pri = [c for c in cards if c["type"] == ("mda" if kind == 1
                                                 else "cga")][0]
        sec = [c for c in cards if c is not pri][0]
        skind = "herc" if sec["type"] == "mda" else "cga"
        ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
        sx = u16(ctx, VID_CTX_SZ + VID_CTX_VX)
        sy = u16(ctx, VID_CTX_SZ + VID_CTX_VY)
        say("secondary %s at (%d,%d), %dx%d"
            % (sec["type"], sx, sy, u16(ctx, VID_CTX_SZ + VID_CTX_CW),
               u16(ctx, VID_CTX_SZ + VID_CTX_CH)))
        # THE SEAM IS ONE NUMBER ON ONE AXIS, and which axis is the whole
        # difference between wm_su_vset's two arms: Right cuts a rect's x2
        # against the primary's last column, Below cuts its y2 against the
        # primary's last row. `across` is the window's own coordinate on that
        # axis, so everything below is written once.
        vert = a.layout == "below"
        seam = sy if vert else sx

        # ONE Disk window, and it is WF_SAVEU (SPEC.md 22.14) - which is what
        # makes it eligible for the drag cache at all.
        dispcp.open_drive(m, mo, S, os88marty.settle, "A", card=pri["idx"])
        w = dispcp.win_list(m, S)
        if not w:
            sys.exit("dispdrag: no Disk window opened")
        win = w[-1]
        x, y, ww, wh = dispcp.win_rect(m, S, win)
        say("Disk window at (%d,%d) %dx%d" % (x, y, ww, wh))

        span = wh if vert else ww           # the window's extent on that axis

        def grab(m):
            x, y, ww, wh = dispcp.win_rect(m, S, win)
            return (y if vert else x), (wh if vert else ww), (x, y, ww, wh)

        def move(to):
            """Take the title bar to wherever puts the window's own axis at
            `to`. The grip is the bar's middle either way - a window is moved
            by its title bar on both layouts."""
            _, _, (x, y, ww, wh) = grab(m)
            gx, gy = x + ww // 2, y + TITLE_H // 2
            mo.drag(gx, gy, gx if vert else to + ww // 2,
                    to + TITLE_H // 2 if vert else gy)

        # --- park it wholly on display 0, on an 8px phase ------------------
        # The bank is only taken from a source vid_span_one accepts, so the
        # window has to START clear of the seam - which is the field's own
        # first drag and not a contrivance. The phase is only load-bearing on
        # x (SPEC.md 11.96.13: rows are whole, so any dy replays) and rounding
        # both costs nothing.
        move((seam - span - 40) & ~7)
        os88marty.settle(m, card=pri["idx"])
        at, span, rect = grab(m)
        if at + span - 1 >= seam:
            sys.exit("dispdrag: the window did not park clear of the seam")
        say("parked wholly on display 0 at %s=%d"
            % ("y" if vert else "x", at))

        # --- and now ACROSS it ---------------------------------------------
        move((seam - span // 2) & ~7)       # about half of it past the seam
        os88marty.settle(m, card=sec["idx"])
        at, span, rect = grab(m)
        say("dragged to %s=%d, %dx%d - %d px of it past the seam"
            % ("y" if vert else "x", at, rect[2], rect[3], at + span - seam))
        if at >= seam or at + span - 1 < seam:
            sys.exit("dispdrag: the window does not straddle the seam (it is "
                     "at %d..%d and the seam is %d) - wm_strad_fit may have "
                     "shrunk it back (SPEC.md 39.16.3)"
                     % (at, at + span - 1, seam))

        sw, sh, rows = m.vram(skind)
        shot = flat(rows)
        say("second card: %dx%d, %d lit pixels" % (sw, sh, sum(shot)))

        if a.dump:
            with open(a.dump, "wb") as f:
                f.write(shot)
            say("written to %s" % a.dump)
        if a.expect:
            with open(a.expect, "rb") as f:
                ref = f.read()
            if len(ref) != len(shot):
                fail.append("the reference is %d bytes and this is %d - not "
                            "the same card" % (len(ref), len(shot)))
            else:
                d = sum(1 for p, q in zip(ref, shot) if p != q)
                say("%d px of %d differ from the reference build"
                    % (d, len(shot)))
                if d:
                    bad = [i for i, (p, q) in enumerate(zip(ref, shot))
                           if p != q]
                    say("bounding box on the second card: x %d..%d y %d..%d"
                        % (min(i % sw for i in bad), max(i % sw for i in bad),
                           min(i // sw for i in bad), max(i // sw for i in bad)))
                    fail.append("%d px differ on the SECOND display: the drag "
                                "cache replayed only the primary's slice and "
                                "nothing drew the rest (SPEC.md 11.96.14.1)"
                                % d)

    print()
    for f in fail:
        print("dispdrag: FAIL: %s" % f)
    if fail:
        return 1
    print("dispdrag: a window dragged across the seam arrives on both "
          "displays - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
