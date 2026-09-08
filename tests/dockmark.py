#!/usr/bin/env python3
"""Does the dock strip mark windows it did not draw under? (SPEC.md 30.3.3)

    make && python3 tests/dockmark.py

...and the A/B, because a gate that passes on the broken build is worth
nothing: `make REDRAWFULL=1` keeps the old one-compare test (SPEC.md 30.3.3),
so the same session through it must FAIL assertion 1 and still pass assertion
2 - the picture is identical either way and only the number of times it is
drawn changed.

    make REDRAWFULL=1 && OS88_DEFINES=REDRAWFULL python3 tests/dockmark.py
    make                                    # ...and put the shipped one back

Reported from the field as *"in extended desktop mode, if a window is
straddling the two monitors and you drag any other window, the straddling
window fully repaints even if the dragged window does not intersect it."*

**The straddle is not the bug, it is what makes the bug visible.** SPEC.md
11.91's marking pass asked `y + h >= [vid_dock_y0]` and marked the window on
that alone - no x, so a window over one end of the strip was redrawn because
something moved at the other end, and no display, so on an extended desktop a
window on the SECOND monitor was marked for reaching the primary's dock row.
A window across the seam can hold no raise cache (`vid_span_one` refuses a
straddling rect, SPEC.md 39.14.8), so where every other window is put back
with a ~10 ms blit and nobody notices the surplus mark, this one pays a full
W_PAINT - 578 ms of glyphs on a Disk window - on every drag anywhere.

TWO ASSERTIONS, and they pull in opposite directions on purpose.

  1. `wm_dmg_mask` after the drag: the window standing clear of what the strip
     PAINTED must not be in it. Read out of the guest rather than inferred
     from pixels, because an unnecessary repaint draws exactly the pixels the
     necessary one would - which is the whole reason this survived so long.
     It is checked for VACUITY too: the assertion means nothing unless the
     dragged window's own damage really did reach the strip.

  2. ...and the glass must still agree with a forced full repaint, on BOTH
     cards, after every step - including one with a window standing over the
     tiles' own columns while the active tile moves. A mark skipped too
     eagerly is the dock drawn THROUGH a window, and that is what shows here.

docs/history/WM-ARTIFACTS.md 0 is the method for the second one and its four rules
apply here unchanged: force the repaint from the guest, park the pointer,
exclude the clock, settle first.
"""
import argparse
import os
import sys
import time

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
MBAR_H = 20
S = os88sym.linear


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def word(m, name):
    return u16(m.read(S(name), 2))


def kind_of(card):
    """`cards` answers MDA for a Hercules and `vram` wants 'herc' - the two
    share a layout and an aperture, so the name is the only difference."""
    return "cga" if card["type"] == "cga" else "herc"


def frame(m, card):
    w, h, rows = m.vram(kind_of(card))
    return w, h, bytes(b for r in rows for b in r)


def full_repaint(m):
    """Make the GUEST repaint, and leave the machine paused (WM-ARTIFACTS 0)."""
    m.cmd(cmd="run")
    m.write(S("cp_dirty"), b"\x01")
    for _ in range(200):
        time.sleep(0.05)
        if m.read(S("cp_dirty"), 1)[0] == 0:
            break
    else:
        raise RuntimeError("ui_task never drained [cp_dirty]")
    os88marty.settle(m)
    m.cmd(cmd="pause")


def stale(m, cards, tag, say):
    """Pixels on which the glass and a forced repaint disagree, both cards."""
    os88marty.settle(m)
    m.cmd(cmd="pause")
    before = {c["idx"]: frame(m, c) for c in cards}
    full_repaint(m)
    after = {c["idx"]: frame(m, c) for c in cards}
    clk = word(m, "vid_clk_hx")
    bad = []
    for c in cards:
        w, h, b = before[c["idx"]]
        _, _, a = after[c["idx"]]
        d = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
        # the menu bar's clock is the primary's and changes once a minute
        d = [i for i in d if not (i // w < MBAR_H and i % w >= clk)]
        if d:
            bad.append("%s: %d px, x %d..%d y %d..%d"
                       % (kind_of(c), len(d), min(i % w for i in d),
                          max(i % w for i in d), min(i // w for i in d),
                          max(i // w for i in d)))
    m.cmd(cmd="run")
    say("%-44s %s"
        % (tag, "; ".join(bad) if bad else "0 px on both cards"))
    return bad


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)

    fail = []
    say = lambda s: print("  " + s)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        cards = m.cards()
        if len(cards) != 2:
            sys.exit("dockmark: %s has %d video card(s), not 2"
                     % (a.machine, len(cards)))
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("dockmark: the Control Panel did not turn Extend on")

        kind = m.read(S("vid_kind"), 1)[0]
        pri = [c for c in cards if c["type"] == ("mda" if kind == 1
                                                 else "cga")][0]
        ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
        if u16(ctx, VID_CTX_VX) or u16(ctx, VID_CTX_VY):
            sys.exit("dockmark: display 0 is not at the virtual origin - "
                     "SPEC.md 39.16's construction has changed and the strip's "
                     "rect is no longer (0,dock_y0)..(pwm1,phm1)")
        seam = u16(ctx, VID_CTX_SZ + VID_CTX_VX)
        dock, pwm1 = word(m, "vid_dock_y0"), word(m, "vid_pwm1")
        say("primary %dx%d, secondary at x=%d ; dock row %d, last column %d"
            % (u16(ctx, VID_CTX_CW), u16(ctx, VID_CTX_CH), seam, dock, pwm1))

        def drag_to(slot, gx, gy, tox, toy):
            """Grab window `slot` at ABSOLUTE (gx,gy) - its title bar has to be
            uncovered THERE, which is why no caller lets this pick the middle."""
            x, y, _, _ = dispcp.win_rect(m, S, slot)
            mo.drag(gx, gy, gx + (tox - x), gy + (toy - y))
            os88marty.settle(m, card=pri["idx"])
            return dispcp.win_rect(m, S, slot)

        dispcp.open_drive(m, mo, S, os88marty.settle, "A", card=pri["idx"])
        wa = dispcp.win_list(m, S)[-1]
        dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri["idx"])
        wb = [s for s in dispcp.win_list(m, S) if s != wa][-1]

        # B to the far left, A across the seam and low enough to reach the
        # primary's dock ROWS - which is what a tall window on a taller
        # secondary does by itself, and is the field's own arrangement.
        rb = dispcp.win_rect(m, S, wb)
        rb = drag_to(wb, rb[0] + rb[2] // 2, rb[1] + TITLE_H // 2, 0, 24)
        ra = dispcp.win_rect(m, S, wa)
        ra = drag_to(wa, max(ra[0] + ra[2] - 20, rb[0] + rb[2] + 8),
                     ra[1] + TITLE_H // 2, (seam - ra[2] // 2) & ~7, 40)
        say("A = slot %d at (%d,%d) %dx%d ; B = slot %d at (%d,%d) %dx%d"
            % ((wa,) + ra + (wb,) + rb))
        if not ra[0] < seam < ra[0] + ra[2]:
            sys.exit("dockmark: A does not straddle the seam (%d..%d, seam %d)"
                     % (ra[0], ra[0] + ra[2] - 1, seam))
        if ra[1] + ra[3] < dock:
            sys.exit("dockmark: A does not reach the dock row, so the test "
                     "cannot say anything")
        fail += stale(m, cards, "A across the seam, B parked", say)

        # --- the drag: B moves one step, at the OTHER END of the strip -----
        rb = drag_to(wb, rb[0] + rb[2] // 2, rb[1] + TITLE_H // 2, 0, 32)
        ra = dispcp.win_rect(m, S, wa)
        mask = word(m, "wm_dmg_mask")
        touching = not (rb[0] > ra[0] + ra[2] or rb[0] + rb[2] < ra[0]
                        or rb[1] > ra[1] + ra[3] or rb[1] + rb[3] < ra[1])
        say("B nudged to (%d,%d) %dx%d - overlaps A: %s ; reaches the strip: %s"
            % (rb + (touching, rb[1] + rb[3] >= dock)))
        say("wm_dmg_mask = 0x%04x (A = 0x%04x)" % (mask, 1 << wa))
        if touching:
            fail.append("B overlaps A, so this run proves nothing - the "
                        "windows did not go where the script put them")
        elif rb[1] + rb[3] < dock:
            fail.append("B's own damage never reached the dock strip, so the "
                        "marking rule under test never ran - VACUOUS")
        elif mask & (1 << wa):
            fail.append("A was marked and redrawn: the strip painted x "
                        "%d..%d and A stands at x %d..%d, so nothing was "
                        "drawn under it (SPEC.md 30.3.3)"
                        % (rb[0], rb[0] + rb[2], ra[0], ra[0] + ra[2]))
        else:
            say("A was NOT marked - the strip drew nothing under it")
        fail += stale(m, cards, "...after B was dragged", say)

        # --- and the other direction: a window really ON the tiles ---------
        # The tiles live at DOCK_X0 = 8 and step 28, so the strip's LEFT end
        # is the busy one. A window standing there while the active tile moves
        # must still be marked, and a diff is what says so: skip the mark and
        # the tile lands on top of the window.
        ra = drag_to(wa, ra[0] + 8, ra[1] + TITLE_H // 2, 0, 40)
        say("A moved onto the tiles at (%d,%d) %dx%d" % ra)
        mo.click(ra[0] + ra[2] // 2, ra[1] + TITLE_H // 2)   # raise A
        os88marty.settle(m, card=pri["idx"])
        fail += stale(m, cards, "...A raised over the tiles", say)
        mo.click(rb[0] + 40, rb[1] + TITLE_H // 2)           # ...and B back
        os88marty.settle(m, card=pri["idx"])
        fail += stale(m, cards, "...B raised again", say)

    print()
    for f in fail:
        print("dockmark: FAIL: %s" % f)
    if fail:
        return 1
    print("dockmark: the strip marks only the windows it was drawn under, "
          "and the glass matches a full repaint - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
