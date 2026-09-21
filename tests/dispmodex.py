#!/usr/bin/env python3
"""Which display does Missile Command ask about Mode X? (SPEC.md 39.18.1)

    make && python3 tests/dispmodex.py

Reported from the field: *"Missile Command would not go into Mode X"* on an
extended VGA + Hercules desktop.

Mode X is gated on the ADAPTER and on nothing else - `fsx_capstab` is indexed
by `VID_*` (VGA 0x01EF has bit 8, HERC 0x0011 and CGA 0x000F do not) and
neither `fsx_caps` nor `fsx_mode` consults the CPU tier. On a two-display
machine `fsx_caps` answers about **the display the asking window is on**
(§39.18.1), which it finds through `wm_top`.

**And `mc_entry` asks BEFORE its window exists.** `mc_adapter` runs at the top
of the entry proc, several dozen instructions before `OSAPI_WM_CREATE`, so
`wm_top` is still the window the game was launched FROM. Launch Missile from a
Disk window sitting on the Hercules and the answer describes the Hercules -
the menu item renames itself `Mode X (Vga)` and `mc_cmd_modex` refuses - even
though the game's own window then opens on the VGA.

This measures `mc_caps` itself, out of the package's own bss, for a launch
from each display.
"""
import sys
import time

import os
# THIS TREE'S root, DERIVED - never a hard-coded path. A literal is right in the
# checkout it was written in and wrong in a git worktree, which is how parallel
# work is done here: os88sym re-assembles ROOT/kernel/kernel.asm and compares it
# against ROOT/build/kernel.bin, so a literal ROOT answers about a DIFFERENT
# kernel from the image being booted.
_OS88_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tools"))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tests"))
import os88marty, os88mouse, os88sym, dispcp, dispapps      # noqa: E402

# SPEC.md 39.14's per-display record, from the ONE place that mirrors it.
# This file indexed it with bare literals - `42 + 36` for the seam and
# ctx[40]/ctx[82] for the two kind bytes - on the VID_CTX_W = 18 layout the
# kernel left behind at 6.1.10. Two bytes early puts `vid_tseg` where the kind
# is, so the row printed a FRAMEBUFFER SEGMENT as an adapter kind and read the
# seam out of the middle of the copied run.
from os88geom import (VID_CTX_SZ, VID_CTX_VX,               # noqa: E402
                      VID_CTX_KIND)

S = os88sym.linear
TITLE_H = 18
FSXM_MODEX = 8
VGA_CAPS = 0x01EF
HERC_CAPS = 0x0011
GAMES_DIR, MISSILE_PKG = "GAMES", "MISSILE.O88"


def u16(b, i=0): return b[i] | (b[i + 1] << 8)


def pkgs(m):
    """Every visible package window, so reading the WRONG one is visible."""
    out, i = [], 0
    while True:
        p = dispapps.pkg_seg(m, i)
        if p is None:
            return out
        out.append(p)
        i += 1


def facts(m):
    """mc_caps and mc_mono out of the running package, or None if it is up."""
    ps = pkgs(m)
    if not ps:
        return None
    if len(ps) != 1:
        print("   (%d package windows up: %s - reading the last)"
              % (len(ps), [hex(sg) for _, sg in ps]))
    return dispapps.words(m, ps[-1][1], "missile", ["mc_caps", "mc_mono"])


def report(m, where, want_caps, want_mono, bad):
    f = facts(m)
    if f is None:
        print("   %-34s MISSILE IS NOT UP" % where)
        bad.append(where)
        return
    ok = f["mc_caps"] == want_caps and f["mc_mono"] == want_mono
    print("   %-34s mc_caps=0x%04X mono=%d  Mode X %-9s %s"
          % (where, f["mc_caps"], f["mc_mono"],
             "live" if f["mc_caps"] & (1 << FSXM_MODEX) else "greyed",
             "ok" if ok else "EXPECTED 0x%04X/%d" % (want_caps, want_mono)))
    if not ok:
        bad.append(where)


def row(m, mo, disk, card, name):
    bx, by, bw, bh = dispcp.win_rect(m, S, disk)
    dispcp.open_named(m, mo, S, os88marty.settle, bx, by, name, card=card)


def launch(m, mo, disk, card, up=False):
    """Open GAMES and run MISSILE.

    `up` first takes the '..' row (SPEC.md 19.5 synthesizes it at slot 0),
    because a previous launch leaves the window INSIDE GAMES - without it the
    second pass opens row 1 of GAMES (ARKANOID) and row 3 (SOLITAIR), and
    reading missile's bss offsets out of another package's segment answers a
    plausible 0x0000 rather than erroring."""
    if up:
        row(m, mo, disk, card, "..")       # SPEC.md 19.5's synthesized parent
    row(m, mo, disk, card, GAMES_DIR)
    row(m, mo, disk, card, MISSILE_PKG)
    time.sleep(3)


def game_win(m, disk):
    w = [x for x in dispcp.win_list(m, S) if x != disk]
    return w[-1] if w else None


def alive(m, win, bad, where):
    """Is there still a machine, and is MISSILE still on it?

    Both questions, because the row spent a long time reporting neither. A
    guest that has stopped servicing IRQ0 presents as a pointer that will not
    move, and `Mouse.to` then RAISES about a target it could not reach - which
    names the coordinate and says nothing about the machine. A package that
    has gone away presents as a window record that still reads its last
    geometry, so the next drag grabs 876,29 where there is no longer a window.
    """
    t0 = int.from_bytes(m.read(S("ticks"), 2), "little")
    c0 = m.status()["cycles"]
    while (m.status()["cycles"] - c0) / os88marty.GUEST_HZ < 1.0:
        if int.from_bytes(m.read(S("ticks"), 2), "little") != t0:
            break
        time.sleep(0.02)
    else:
        print("   %-34s THE GUEST HAS STOPPED: [ticks] stuck at %d for a "
              "whole GUEST second, so IRQ0 is not being serviced" % (where, t0))
        bad.append(where + " (guest stopped)")
        return False
    if win not in dispcp.win_list(m, S):
        print("   %-34s MISSILE'S WINDOW IS GONE (windows: %s)"
              % (where, dispcp.win_list(m, S)))
        bad.append(where + " (window gone)")
        return False
    return True


def move_to(m, mo, win, x, card, bad):
    """Drag WIN so its CENTRE lands at x - which is what decides the display
    (wm_disp_of: centre, then origin, then the primary).

    **NOTHING HERE SETTLES THE SCREEN**, and that is not a shortcut. This used
    to `settle(card=card)` after the drag, and MISSILE IS A GAME: with a salvo
    in flight its window never stops changing, so the wait cannot succeed on
    whichever card the window is on. It read, on the second move,
    `the screen was still changing after 361 GUEST seconds` - which is a true
    sentence about an animating playfield and says nothing about the drag.
    What this function is about to read is the caps and the rect, so those are
    what it waits for.
    """
    wx, wy, ww, wh = dispcp.win_rect(m, S, win)
    mo.drag(wx + ww // 2, wy + TITLE_H // 2, x, wy + TITLE_H // 2)
    # **THE APP'S ANSWER, NOT A HOST SLEEP.** This was `time.sleep(1.5)` with
    # the comment "the worker asks once a frame" - a HOST wait for a GUEST
    # event, which is wrong at some guest speed by construction. The caps the
    # caller is about to assert are written by MISSILE's own worker when it
    # next calls fsx_caps, so the thing to wait for is those words settling:
    # `quiesce` wants them unchanged over GUEST seconds, which a loaded box
    # cannot shorten and a fast one cannot outrun.
    #
    # It read `mc_caps=0x01EF mono=0` for a window whose centre was already
    # 236 pixels the far side of the seam - the right answer to the question
    # asked a moment too early, reported as the kernel failing to move the
    # caps with the window.
    #
    # **AND THE CAPS ARE NOT THE LAST THING TO SETTLE.** A move onto the
    # Hercules changes `mc_mono`, and MISSILE then RE-LAYS-OUT: measured here,
    # the window goes 562 wide to 634 and 435 tall to 303. `mc_caps` is
    # written the moment the worker next asks `fsx_caps` - well before that
    # re-layout finishes - so a quiesce on the caps alone returns while the
    # package is still rebuilding, and the next drag's press lands in the
    # middle of it. That is measurable rather than theoretical: back-to-back
    # drags across the seam with no wait between them leave the window where
    # it was on 13 of 14 round trips and take the guest down on about one run
    # in four, while the same loop with the geometry settled first did 12 of
    # 12 and never lost a machine. So the RECT is quiesced with the caps.
    # THREE guest seconds of stillness rather than one, because what is
    # settling here is a whole playfield repaint on a 1bpp adapter with the
    # window straddling the seam - `mc_full` is what `mc_onresize` sets - and
    # not a couple of words being stored.
    os88marty.quiesce(m, lambda: (facts(m), dispcp.win_rect(m, S, win)),
                      guest=1.0, stable=3, budget=90.0,
                      what="MISSILE's caps AND its geometry to settle")
    return dispcp.win_rect(m, S, win)


def main():
    bad = []
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine="os8088_xt_vga_herc", boot=False) as m:
        cards = {c["idx"]: c for c in m.cards()}
        pri = [i for i, c in cards.items() if c["type"] == "vga"][0]
        sec = [i for i, c in cards.items() if c["type"] == "mda"][0]
        m.run(); os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
        seam = u16(ctx, VID_CTX_SZ + VID_CTX_VX)
        print("seam at x=%d; display 0 kind=%d (VGA), display 1 kind=%d (HERC)"
              % (seam, ctx[VID_CTX_KIND], ctx[VID_CTX_SZ + VID_CTX_KIND]))

        dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri)
        disk = dispcp.win_list(m, S)[-1]
        launch(m, mo, disk, pri)
        report(m, "launched from the VGA", VGA_CAPS, 0, bad)

        # --- the window MOVES, and the facts have to move with it ----------
        g = game_win(m, disk)
        if g is None:
            print("   missile did not launch"); return 1
        r = move_to(m, mo, g, seam + 200, sec, bad)
        print("   dragged to x %d..%d (centre %d, right of the seam)"
              % (r[0], r[0] + r[2] - 1, r[0] + r[2] // 2))
        report(m, "...its centre on the Hercules", HERC_CAPS, 1, bad)
        if not alive(m, g, bad, "after the move to the Hercules"):
            return 1

        r = move_to(m, mo, g, 300, pri, bad)
        print("   dragged back to x %d..%d (centre %d)"
              % (r[0], r[0] + r[2] - 1, r[0] + r[2] // 2))
        if r[0] + r[2] // 2 > seam:
            print("   ...THE WINDOW DID NOT COME BACK: its centre is still "
                  "%d, right of the seam at %d" % (r[0] + r[2] // 2, seam))
            bad.append("the drag back was a no-op")
        report(m, "...and back on the VGA", VGA_CAPS, 0, bad)
        if not alive(m, g, bad, "after the move back to the VGA"):
            return 1

        # --- launched FROM the Hercules: the entry-time answer is corrected -
        gx, gy, gw, gh = dispcp.win_rect(m, S, g)
        mo.click(gx + 9, gy + TITLE_H // 2)         # close box, LEFT (SPEC 11)
        os88marty.settle(m, card=pri)
        dx, dy, dw, dh = dispcp.win_rect(m, S, disk)
        mo.drag(dx + dw // 2, dy + TITLE_H // 2, seam + 200, dy + TITLE_H // 2)
        os88marty.settle(m, card=sec)
        dx, dy, dw, dh = dispcp.win_rect(m, S, disk)
        print("   disk window now at x %d..%d" % (dx, dx + dw - 1))
        launch(m, mo, disk, sec, up=True)
        report(m, "launched from the Hercules", VGA_CAPS, 0, bad)

    print()
    if bad:
        print("FAIL: %s" % ", ".join(bad))
        return 1
    print("PASS: the caps and the depth follow the window, and a launch from "
          "the other display is corrected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
