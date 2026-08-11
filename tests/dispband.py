#!/usr/bin/env python3
"""Can a window use the SECOND display's top rows? (SPEC.md 39.16.2)

    make && python3 tests/dispband.py

`MBAR_H .. [vid_dock_y0]-1` is the band the chrome leaves free, and it is the
PRIMARY's band: no second card carries a menu bar or a dock. Two sites used to
clamp against those constants in VIRTUAL coordinates, so on a two-card machine
the top `MBAR_H` rows of the second monitor could hold no window at all - 640
x 20 = 12,800 px of a CGA, 6.4% of it - and the damage path never re-dithered
them either.

Three things, in the order they can fail:

  1. A WINDOW REACHES y = 0 on the second display. This is the assertion that
     would have failed before: the drag floor was MBAR_H wherever the window
     was, so `W_Y` stopped at 20.
  2. AND ITS PIXELS ARE THERE. A record saying y = 0 with nothing drawn in
     those rows is the same bug one layer down, and the second card is the
     only place it shows.
  3. THE ROWS COME BACK when it leaves. wm_paint_dmg's dither floored at
     MBAR_H too, so a window closing up there left its own pixels behind -
     which is invisible until step 1 lets anything get there.

It runs on the DEFAULT build: MartyPC's two-card machine comes up CGA-primary,
so display 1 is the Hercules at (640, 0) and its vy is 0.
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

MBAR_H, MENU_ITEM_H, TITLE_H = 20, 16, 18
S = os88sym.linear


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def win_by(m, slot):
    """The window in SLOT, which is the only stable handle there is.

    `wins(m)[-1]` is "whatever is last in wm_wins", and that is a different
    window as soon as anything else is open - it silently became the Control
    Panel here and made a drag that never happened look like a repaint that
    never repaired.
    """
    return dispcp.win_rect(m, S, slot)


def wins(m):
    return [(i,) + dispcp.win_rect(m, S, i) for i in dispcp.win_list(m, S)]


def band(rows, x0, x1, y0, y1):
    """One rect of the card's own VRAM, as bytes.

    VRAM AND NOT `fbuf`: MartyPC's MDA aperture is offset (-16, +2) from the
    guest origin in Hercules graphics (docs/MARTYPC-DEBUG.md), so a band
    sampled off the rendered framebuffer at y = 0 is not the guest's row 0 at
    all - it is two rows above the picture, which is border. The offset
    cancels in a same-coordinates comparison right up until the thing being
    compared is a 19-row band at the very top of the screen, which is exactly
    what this test is about.
    """
    out = bytearray()
    for y in range(y0, y1 + 1):
        r = rows[y]
        for x in range(x0, x1 + 1):
            out.append(r[x])
    return bytes(out)


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
            sys.exit("dispband: %s has %d video card(s), not 2"
                     % (a.machine, len(cards)))
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("dispband: the Control Panel did not turn Extend on")

        kind = m.read(S("vid_kind"), 1)[0]
        pri = [c for c in cards if c["type"] == ("mda" if kind == 1
                                                 else "cga")][0]
        sec = [c for c in cards if c is not pri][0]
        ctx = m.read(S("vid_ctx"), 2 * 42)
        d1vx, d1vy = u16(ctx, 42 + 36), u16(ctx, 42 + 38)
        say("display 1 (%s) at (%d,%d); its top row is %d, not MBAR_H"
            % (sec["type"], d1vx, d1vy, d1vy))

        # The About box: the cheapest window in the machine, no package load.
        mo.menu(12, 8, 12, MBAR_H + 1 + 8)
        os88marty.settle(m, card=pri["idx"])
        w = wins(m)
        if not w:
            sys.exit("dispband: no window after About")
        slot, wx, wy, ww, wh = w[-1]
        say("About at (%d,%d) %dx%d, slot %d" % (wx, wy, ww, wh, slot))

        # ...the rows it is going to sit in, BEFORE it ever goes there, so
        # step 3 has a reference that is definitely desktop.
        tx = d1vx + 40
        skind = "herc" if sec["type"] == "mda" else "cga"
        sw, sh, srows = m.vram(skind)
        clean = band(srows, 40, 40 + ww - 1, 0, TITLE_H)

        mo.drag(wx + ww // 2, wy + TITLE_H // 2, tx + ww // 2,
                wy + TITLE_H // 2)
        os88marty.settle(m, card=sec["idx"])
        wx, wy, ww, wh = win_by(m, slot)
        say("...dragged onto display 1: (%d,%d)" % (wx, wy))

        # --- 1: drive it UP as hard as the clamp allows --------------------
        # ...to d1vy and not to 0: past the seam, the rows above the second
        # display are the DEAD ZONE now (SPEC.md 39.19.3) and mou_clamp
        # correctly refuses to put the pointer in one.
        mo.drag(wx + ww // 2, wy + TITLE_H // 2, wx + ww // 2, d1vy)
        os88marty.settle(m, card=sec["idx"])
        wx, wy, ww, wh = win_by(m, slot)
        say("pushed to the top of display 1: W_Y = %d (wanted %d)"
            % (wy, d1vy))
        if wy != d1vy:
            fail.append("W_Y stopped at %d, not %d - the drag floor is still "
                        "the primary's MBAR_H (SPEC.md 39.16.2)"
                        % (wy, d1vy))

        # --- 2: ...and the pixels are actually up there --------------------
        sw, sh, srows = m.vram(skind)
        top = band(srows, wx - d1vx, wx - d1vx + ww - 1, wy - d1vy,
                   wy - d1vy + TITLE_H - 1)
        litn = sum(1 for b in top if b)
        say("its title bar rows on display 1: %d of %d px lit"
            % (litn, len(top)))
        if litn < len(top) // 8:
            fail.append("the window says it is at y=%d but those rows on the "
                        "second card are nearly empty - the record moved and "
                        "the pixels did not" % wy)

        # --- 3: and they come back when it leaves --------------------------
        mo.drag(wx + ww // 2, wy + TITLE_H // 2, wx + ww // 2, 120)
        os88marty.settle(m, card=sec["idx"])
        ax2, ay2, aw2, ah2 = win_by(m, slot)
        say("...and it ended up at (%d,%d) %dx%d" % (ax2, ay2, aw2, ah2))
        if ay2 < 60:
            fail.append("the window did not move away (y=%d) - step 3 is "
                        "measuring a drag that did not happen" % ay2)
        sw, sh, srows = m.vram(skind)
        after = band(srows, 40, 40 + ww - 1, 0, TITLE_H)
        # a control: the same rows where the window has never been, so a
        # "restored" band can be checked against real desktop rather than
        # against a number that looks about right
        ctl = band(srows, 400, 699, 0, TITLE_H)
        say("control band (never visited, local x 400..699): %d of %d lit"
            % (sum(1 for b in ctl if b), len(ctl)))
        d = sum(1 for x, y in zip(clean, after) if x != y)
        say("the rows it vacated: %d px of %d differ from the desktop it "
            "found there (lit: was %d, now %d)"
            % (d, len(clean), sum(1 for b in clean if b),
               sum(1 for b in after if b)))
        if d > len(clean) // 20:
            fail.append("%d px of the vacated rows are not desktop - "
                        "wm_paint_dmg's dither is still floored at MBAR_H "
                        "(SPEC.md 39.16.2)" % d)

    print()
    for f in fail:
        print("dispband: FAIL: %s" % f)
    if fail:
        return 1
    print("dispband: the second display's top rows hold a window, draw it, "
          "and come back - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
