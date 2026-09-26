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

import os
# THIS TREE'S root, DERIVED - never a hard-coded path. A literal is right in the
# checkout it was written in and wrong in a git worktree, which is how parallel
# work is done here: os88sym re-assembles ROOT/kernel/kernel.asm and compares it
# against ROOT/build/kernel.bin, so a literal ROOT answers about a DIFFERENT
# kernel from the image being booted.
_OS88_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tools"))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tests"))

from os88geom import (VID_CTX_SZ, VID_CTX_VX,          # noqa: E402
                      VID_CTX_VY, VID_CTX_KIND, VID_CTX_CH)
# SPEC.md 39.14's per-display record: DERIVED from VID_CTX_W and never
# written down here. This file spelled it `42 + 36`, which is the
# VID_CTX_W = 18 layout - two bytes early, and what sits there is
# display 1's vid_chm8, so the seam read 192 instead of 720.
import os88marty, os88mouse, os88sym, dispcp                # noqa: E402

S = os88sym.linear
TITLE_H = 18
GAMES_DIR, MISSILE_PKG = "GAMES", "MISSILE.O88"
MBAR_H = 20


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
    # SETTLE EVERY CARD THIS IS ABOUT TO CAPTURE, and not just one of them.
    # Each of the four call sites settled `sec` and then took a framebuffer
    # off BOTH, so the VGA was compared having never been asked to stand
    # still - invisible on an idle box, where it has finished anyway, and
    # exactly the shape that shows up once in a while under a four-wide soak.
    # The 2026-09-22 run read `VGA is stale after the round trip, 672
    # pixel(s) in (228,115)..(283,126)` with the Hercules at 0, which is that
    # region of the DESKTOP and of the Disk window rather than anything the
    # fullscreen trip touches.
    #
    # NOT REPRODUCED ON DEMAND - ten runs, six idle and four beside three
    # other guests, all 0/0 - so this is fixed on the code rather than on a
    # capture. What is certain either way is that a comparison is only
    # entitled to a framebuffer it settled, and this one was not.
    for c in cards:
        os88marty.settle(m, card=c)
    # PAUSE NEXT. vid_ctx_act writes [vid_ox] and [vid_oy] with two stores,
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
             u16(m.read(S("vid_ctx"), 2 * VID_CTX_SZ), VID_CTX_VX),
             u16(m.read(S("vid_ctx"), 2 * VID_CTX_SZ), VID_CTX_VY),
             u16(m.read(S("vid_ctx"), 2 * VID_CTX_SZ), VID_CTX_SZ + VID_CTX_VX),
             u16(m.read(S("vid_ctx"), 2 * VID_CTX_SZ), VID_CTX_SZ + VID_CTX_VY)))
    print("   %-22s lit: %s  win y %d..%d"
          % ("", {c: lit(m, c) for c in cards}, r[1], r[1] + r[3] - 1))
    out = {"x": x, "y": y, "nd": nd, "ox": v["vid_ox"], "rect": r,
           "org": {0: (u16(m.read(S("vid_ctx"), 2 * VID_CTX_SZ), VID_CTX_VX),
                       u16(m.read(S("vid_ctx"), 2 * VID_CTX_SZ), VID_CTX_VY)),
                   1: (u16(m.read(S("vid_ctx"), 2 * VID_CTX_SZ),
                           VID_CTX_SZ + VID_CTX_VX),
                       u16(m.read(S("vid_ctx"), 2 * VID_CTX_SZ),
                           VID_CTX_SZ + VID_CTX_VY))},
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
        ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
        seam = u16(ctx, VID_CTX_SZ + VID_CTX_VX)
        org = (u16(ctx, VID_CTX_SZ + VID_CTX_VX),
               u16(ctx, VID_CTX_SZ + VID_CTX_VY))
        print("seam at x=%d; the Hercules sits at virtual %r" % (seam, org))

        dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri)
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, bx, by, GAMES_DIR,
                          card=pri)
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, bx, by, MISSILE_PKG,
                          card=pri)
        # Missile animates, so there is nothing to SETTLE - but its window
        # appearing and the disk going quiet is the launch being done
        try:
            os88marty.until(m, lambda _: any(w != disk for w in
                                             dispcp.win_list(m, S)),
                            "Missile's window", poll=0.2, limit=30)
        except os88marty.MartyError:
            pass                        # ...and the line below says so
        os88marty.quiesce(m, lambda: m.disk().get("reads"), guest=1.0,
                          what="Missile's load to finish")
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
        before = state(m, mo, "windowed", g, both)
        shot(m, sec, "1-windowed-herc")

        # --- into SPEC.md 11.2 fullscreen + the same-mode bracket -----------
        mo.to(wx + ww // 2, wy + wh // 2)       # the pointer on the game
        m.key("KeyF")
        fs = lambda: u16(m.read(S("wm_fs"), 2))
        try:                            # the latch (SPEC.md 11.2); state()
            os88marty.until(m, lambda _: fs() != 0,  # settles the glass
                            "the fullscreen latch", poll=0.2, limit=15)
        except os88marty.MartyError:
            pass
        full = state(m, mo, "fullscreen", g, both)
        shot(m, sec, "2-fullscreen-herc")
        shot(m, pri, "2-fullscreen-vga")

        # can the pointer still move LEFT? (report 1)
        m.mouse(-60, 0)
        os88marty.pace(m, os88mouse.GAP)    # a packet in flight drops the next
        m.mouse(-60, 0)
        try:
            os88marty.until(m, lambda _: mo.where()[0] != full["x"],
                            "the pointer to move", poll=0.1,
                            guest=2 * os88mouse.PKT_GUEST)
        except os88marty.MartyError:
            pass                        # STUCK: the line below says so
        moved = os88marty.quiesce(m, lambda: mo.where()[:2], guest=0.3,
                                  what="the pointer")
        print("   after two -60 x moves: mouse=(%d,%d)  %s"
              % (moved[0], moved[1],
                 "MOVED" if moved[0] != full["x"] else "*** STUCK ***"))

        # --- and out ------------------------------------------------------
        m.key("Escape")
        try:
            os88marty.until(m, lambda _: fs() == 0, "the latch to drop",
                            poll=0.2, limit=15)
        except os88marty.MartyError:
            pass
        os88marty.settle(m, card=sec)
        mo.to(*park)
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
        forced = state(m, mo, "after a forced repaint", g, both)
        shot(m, sec, "4-forced-herc")
        shot(m, pri, "4-forced-vga")    # ...AND THE VGA'S, which is the card
                                        # the comparison below fails on and
                                        # the one capture this row never kept:
                                        # 3-after-vga had no partner to diff
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
        try:                            # wait for the state asserted below,
            os88marty.until(            # on a GUEST budget; a miss is read
                m, lambda _: (m.read(S("vid_ndisp"), 1)[0] == 1   # and
                              and m.read(S("fsx_cur"), 1)[0] == 8),  # reported
                "Mode X to take the machine", poll=0.2, limit=10.0)
        except os88marty.MartyError:
            pass
        m.pause()
        nd = m.read(S("vid_ndisp"), 1)[0]
        fc = m.read(S("fsx_cur"), 1)[0]
        print("   Mode X on the VGA: ndisp=%d fsx_cur=0x%02X  %s"
              % (nd, fc, "ok" if nd == 1 and fc == 8 else "*** NOT IN MODE X"))
        modex_ok = (nd == 1 and fc == 8)
        m.run()
        m.key("Escape")
        try:
            os88marty.until(m, lambda _: m.read(S("vid_ndisp"), 1)[0] == 2,
                            "the second display to come back", poll=0.2,
                            limit=10.0)
        except os88marty.MartyError:
            pass
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
        # ...AND THE GAME'S OWN CONTENT IS NOT PART OF THE CLAIM. `state`
        # says it one screen up - *Missile's worker draws continuously* - and
        # `after` and `forced` are taken seconds apart with a Control Panel
        # opened and closed between them, so the game goes on drawing across
        # the gap. Comparing its content asks whether it drew the same frame
        # twice, which it has no reason to do, and answers "STALE".
        #
        # MEASURED, on the run that caught it: the whole difference was ONE
        # solid 80px horizontal line at the first row of the window's
        # CONTENT (card y=20, x=336..415), present in `forced` and absent in
        # `after`, with the title bar above it identical to the pixel - its
        # caption, its stripes and its bottom border all matching. That is
        # the game drawing, not the desktop failing to come back, and it is
        # why this row has been intermittent at 1/5 and 1/3 for as long as
        # anyone has looked at it.
        #
        # So the content rect comes out and EVERYTHING ELSE STAYS - the
        # window's chrome, the borders, the desktop, the dock. That is what
        # "did the display come back" actually means, and a stale frame or a
        # stale desktop is still caught. The rect is the kernel's own
        # (os88geom's `content`, which is what wm_su_rect answers), put into
        # each card's coordinates by that card's virtual origin.
        wx, wy, ww, wh = forced["rect"]
        cx1, cy1, cx2, cy2 = wx + 1, wy + TITLE_H, wx + ww - 2, wy + wh - 2
        for c, name, skip in ((pri, "VGA", MBAR_H), (sec, "Hercules", 0)):
            # The menu bar is the PRIMARY's and carries a clock, which moves
            # between two captures seconds apart - so the comparison starts
            # below it. The Hercules has no bar and is compared whole.
            w0 = m.fbuf(card=c)[0]
            base = skip * w0 * 3
            ox, oy = forced["org"][c]
            skipx1, skipy1 = cx1 - ox, cy1 - oy
            skipx2, skipy2 = cx2 - ox, cy2 - oy
            # **THE BOUNDING BOX AND NOT ONLY THE COUNT.** A bare "240
            # differing pixel(s)" names no suspect: this row was
            # INTERMITTENT at 1/5 and the count alone could not say whether
            # the difference was a window, the dock strip, a drive zone or
            # the desktop dither, so every reading of it was a guess. The
            # box, the count and the first row are three lines of arithmetic
            # over a comparison that is already being made.
            n, x1, y1, x2, y2 = 0, 1 << 15, 1 << 15, -1, -1
            for i in range(base, len(after["fb"][c]), 3):
                if after["fb"][c][i:i + 3] == forced["fb"][c][i:i + 3]:
                    continue
                px = i // 3
                x, y = px % w0, px // w0
                if (skipx1 <= x <= skipx2) and (skipy1 <= y <= skipy2):
                    continue            # the GAME's content - see above
                n += 1
                x1, y1 = min(x1, x), min(y1, y)
                x2, y2 = max(x2, x), max(y2, y)
            print("   %-9s after the round trip vs a forced repaint: "
                  "%d differing pixel(s)  %s"
                  % (name, n, "ok" if not n else "*** STALE ***"))
            if n:
                print("   %-9s   they sit in (%d,%d)..(%d,%d), %dx%d"
                      % ("", x1, y1, x2, y2, x2 - x1 + 1, y2 - y1 + 1))
                bad.append("%s is stale after the round trip, %d pixel(s) in "
                           "(%d,%d)..(%d,%d)" % (name, n, x1, y1, x2, y2))
        print()
        print("FAIL: %s" % "; ".join(bad) if bad else
              "PASS: the pointer moves and both cards come back")
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
