#!/usr/bin/env python3
"""SPEC.md 11.2 fullscreen with the window's CENTRE on the second display

    make && python3 tests/dispmcfs.py

Two field reports, both with Missile Command's centre on the Hercules of a
VGA-primary extended desktop:

  1. going fullscreen "gated the mouse to the right edge of the herc screen";
  2. coming out "only drew the vga side".

Missile's fullscreen is SPEC.md 11.2 AND SPEC.md 53.7's same-mode bracket
stacked (SPEC.md 48.13), so the path is wm_fullscreen -> fsx_run ->
vid_fsx_enter, and vid_fsx_enter makes the machine claim to be
single-display at the virtual origin (39.18). Everything that lives in
VIRTUAL coordinates and is not moved by that lie is a candidate.

What this measures, at each step: the pointer, the live video block, the
window rect, and BOTH cards' lit-pixel counts - the last because "only drew
the vga side" is a statement about one card being stale, which a screenshot
of the other cannot answer.
"""
import sys
import time

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88marty, os88mouse, os88sym, dispcp                # noqa: E402

S = os88sym.linear
TITLE_H = 18
GAMES_ROW, MISSILE_ROW = 1, 3
MBAR_H = 19


def u16(b, i=0): return b[i] | (b[i + 1] << 8)


def lit(m, card):
    """Lit pixels on one card, from what it RASTERISED - the two adapters
    have different framebuffer layouts and only the card knows its own, and
    on the VGA there is no flat framebuffer in guest memory at all."""
    w, h, d = m.fbuf(card=card)
    return sum(1 for i in range(0, len(d), 3) if d[i] or d[i + 1] or d[i + 2])


def shot(m, card, name):
    w, h, d = m.fbuf(card=card)
    os88marty.write_png_rgb("/tmp/mcfs-%s.png" % name, w, h, d)
    return w, h, d


def state(m, mo, label, win, cards):
    # PAUSE FIRST. vid_ctx_act writes [vid_ox] and [vid_oy] with two stores,
    # and Missile's worker draws continuously - read on a running guest they
    # disagree with each other and with [vid_cur] about which display is
    # live, which reads exactly like a kernel bug and is not one.
    m.pause()
    x, y = mo.where()[:2]
    v = {n: u16(m.read(S(n), 2)) for n in ("vid_w", "vid_wm1", "vid_ox",
                                           "vid_oy")}
    nd = m.read(S("vid_ndisp"), 1)[0]
    cur = m.read(S("vid_cur"), 1)[0]
    r = dispcp.win_rect(m, S, win)
    print("   %-22s mouse=(%d,%d)  ndisp=%d cur=%d w=%d wm1=%d ox=%d  "
          "win x %d..%d" % (label, x, y, nd, cur, v["vid_w"], v["vid_wm1"],
                            v["vid_ox"], r[0], r[0] + r[2] - 1))
    print("   %-22s oy=%d  ctx0 v=(%d,%d) ctx1 v=(%d,%d)"
          % ("", v["vid_oy"],
             u16(m.read(S("vid_ctx"), 84), 36), u16(m.read(S("vid_ctx"), 84), 38),
             u16(m.read(S("vid_ctx"), 84), 42 + 36),
             u16(m.read(S("vid_ctx"), 84), 42 + 38)))
    print("   %-22s lit: %s  win y %d..%d"
          % ("", {c: lit(m, c) for c in cards}, r[1], r[1] + r[3] - 1))
    out = {"x": x, "y": y, "nd": nd, "ox": v["vid_ox"], "rect": r,
           "fb": {c: m.fbuf(card=c)[2] for c in cards},
           "lit": {c: lit(m, c) for c in cards}}
    m.run()
    return out


def main():
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine="os8088_xt_vga_herc", boot=False) as m:
        cards = {c["idx"]: c for c in m.cards()}
        pri = [i for i, c in cards.items() if c["type"] == "vga"][0]
        sec = [i for i, c in cards.items() if c["type"] == "mda"][0]
        both = (pri, sec)
        m.run(); os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        ctx = m.read(S("vid_ctx"), 84)
        seam = u16(ctx, 42 + 36)
        org = (u16(ctx, 42 + 36), u16(ctx, 42 + 38))
        print("seam at x=%d; the Hercules sits at virtual %r" % (seam, org))

        dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri)
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_row(m, mo, S, os88marty.settle, bx, by, GAMES_ROW, card=pri)
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_row(m, mo, S, os88marty.settle, bx, by, MISSILE_ROW,
                        card=pri)
        time.sleep(3)
        g = [w for w in dispcp.win_list(m, S) if w != disk]
        if not g:
            sys.exit("missile did not launch")
        g = g[-1]

        wx, wy, ww, wh = dispcp.win_rect(m, S, g)
        mo.drag(wx + ww // 2, wy + TITLE_H // 2, seam + 300, wy + TITLE_H // 2)
        os88marty.settle(m, card=sec)
        wx, wy, ww, wh = dispcp.win_rect(m, S, g)
        print("missile x %d..%d, centre %d (%s of the seam)"
              % (wx, wx + ww - 1, wx + ww // 2,
                 "right" if wx + ww // 2 >= seam else "left"))
        park = (300, 300)                       # the pointer OFF the Hercules,
        mo.to(*park)                            # so the arrow is in neither
        os88marty.settle(m, card=sec)           # capture
        before = state(m, mo, "windowed", g, both)
        shot(m, sec, "1-windowed-herc")

        # --- into SPEC.md 11.2 fullscreen + the same-mode bracket -----------
        mo.to(wx + ww // 2, wy + wh // 2)       # the pointer on the game
        m.key("KeyF")
        time.sleep(4)
        os88marty.settle(m, card=sec)
        full = state(m, mo, "fullscreen", g, both)
        shot(m, sec, "2-fullscreen-herc")
        shot(m, pri, "2-fullscreen-vga")

        # can the pointer still move LEFT? (report 1)
        m.mouse(-60, 0)
        time.sleep(0.6)
        m.mouse(-60, 0)
        time.sleep(1.2)
        moved = mo.where()[:2]
        print("   after two -60 x moves: mouse=(%d,%d)  %s"
              % (moved[0], moved[1],
                 "MOVED" if moved[0] != full["x"] else "*** STUCK ***"))

        # --- and out ------------------------------------------------------
        m.key("Escape")
        time.sleep(4)
        os88marty.settle(m, card=sec)
        mo.to(*park)
        os88marty.settle(m, card=sec)
        after = state(m, mo, "back to windowed", g, both)
        shot(m, sec, "3-after-herc")
        shot(m, pri, "3-after-vga")

        # A THIRD CAPTURE, after a repaint nothing here asked for. It is the
        # discriminator: if the desktop then matches the FIRST one, the
        # post-exit screen was stale; if it still differs, the first capture
        # is the odd one and the assertion is measuring the wrong thing.
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.close_panel(m, mo, S, os88marty.settle)
        mo.to(*park)
        os88marty.settle(m, card=sec)
        forced = state(m, mo, "after a forced repaint", g, both)
        shot(m, sec, "4-forced-herc")
        # --- and the MODE-SETTING bracket still collapses (SPEC.md 39.18.3)
        # The collapse moved into fsx_mode, so this is the path that has to
        # keep it: back on the VGA, Mode X must still take the machine down
        # to one display and set the mode.
        wx, wy, ww, wh = dispcp.win_rect(m, S, g)
        mo.drag(wx + ww // 2, wy + TITLE_H // 2, 300, wy + TITLE_H // 2)
        os88marty.settle(m, card=pri)
        wx, wy, ww, wh = dispcp.win_rect(m, S, g)
        mo.to(wx + ww // 2, wy + wh // 2)
        os88marty.settle(m, card=pri)
        m.key("KeyM")
        time.sleep(5)
        m.pause()
        nd = m.read(S("vid_ndisp"), 1)[0]
        fc = m.read(S("fsx_cur"), 1)[0]
        print("   Mode X on the VGA: ndisp=%d fsx_cur=0x%02X  %s"
              % (nd, fc, "ok" if nd == 1 and fc == 8 else "*** NOT IN MODE X"))
        modex_ok = (nd == 1 and fc == 8)
        m.run()
        m.key("Escape")
        time.sleep(4)
        m.pause()
        nd2 = m.read(S("vid_ndisp"), 1)[0]
        print("   ...and back out: ndisp=%d  %s"
              % (nd2, "ok" if nd2 == 2 else "*** STILL COLLAPSED"))
        m.run()

        print()
        bad = []
        if not modex_ok:
            bad.append("Mode X did not take the machine")
        if nd2 != 2:
            bad.append("the desktop did not get its second display back")
        if full["x"] > full["ox"] + 720:
            pass
        if moved[0] == full["x"]:
            bad.append("the pointer is stuck")
        # THE SOUND ASSERTION, and it needs no exclusion box: what the screen
        # holds after the round trip must be what a FULL REPAINT would put
        # there. "Stale" means precisely "differs from a fresh repaint", and
        # comparing against a capture taken BEFORE the trip cannot say that -
        # the game keeps playing inside the bracket and its window can move,
        # so that comparison measures content, which is what the first two
        # versions of this test got wrong.
        for c, name, skip in ((pri, "VGA", MBAR_H), (sec, "Hercules", 0)):
            # The menu bar is the PRIMARY's and carries a clock, which moves
            # between two captures seconds apart - so the comparison starts
            # below it. The Hercules has no bar and is compared whole.
            w0 = m.fbuf(card=c)[0]
            base = skip * w0 * 3
            n = sum(1 for i in range(base, len(after["fb"][c]), 3)
                    if after["fb"][c][i:i + 3] != forced["fb"][c][i:i + 3])
            print("   %-9s after the round trip vs a forced repaint: "
                  "%d differing pixel(s)  %s"
                  % (name, n, "ok" if not n else "*** STALE ***"))
            if n:
                bad.append("%s is stale after the round trip" % name)
        print()
        print("FAIL: %s" % "; ".join(bad) if bad else
              "PASS: the pointer moves and both cards come back")
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
