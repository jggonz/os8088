#!/usr/bin/env python3
"""Does the raise cache work on the SECOND display? (SPEC.md 39.14.8)

    make && python3 tests/dispsave.py

`gfx_save`/`gfx_restore` took no display hook, and `wm_su_take`'s gate asked
`cx >= [vid_cw]` - which is the display LAST DRAWN ON (SPEC.md 39.14.3), not
the screen. Two things follow, and this measures both:

  1. THE CACHE WAS NEVER TAKEN for a window on the second display, because its
     virtual x is past any single display's width. `wm_su_segs[slot]` is a
     word per window and non-zero exactly when a cache is held, so the guest
     answers this directly - no breakpoints, no inference.
  2. AND THE PIXELS MUST STILL BE RIGHT once it is taken. A cache that is used
     is a repaint that did not happen, so the whole risk of turning it on is
     that it puts back something other than what was there. The reference is
     the screen before the covering window ever arrived.

Run it against a kernel with the hook removed and step 1 reads 0: that is the
A/B, and it is what says this test contains the case.
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


def wins(m):
    return dispcp.win_list(m, S)                # ...and so does the check


def win_by(m, slot):
    return dispcp.win_rect(m, S, slot)          # WIN_SIZE lives in dispcp


def cache_of(m, slot):
    return u16(m.read(S("wm_su_segs") + slot * 2, 2))


def surface(m, sec):
    """The secondary's pixels as (w, h, rows-of-bits), whatever card it is.

    `vram` is the right instrument for a 1bpp secondary and cannot be used at
    all for a VGA one: mode 12h is four planes behind the Graphics Controller's
    Read Map Select, so it is not flat-readable memory (see os88marty.vram).
    `fbuf` asks the card what it RASTERISED instead, which covers every
    adapter - and is not simply used for both, because MartyPC's MDA aperture
    in Hercules graphics is offset by (-16, +2) from the guest's own
    coordinates (docs/MARTYPC-DEBUG.md), so the two disagree about where a
    pixel is on exactly the card this test spends most of its time on.
    """
    if sec["type"] != "vga":
        return m.vram("herc" if sec["type"] == "mda" else "cga")
    w, h, px = m.fbuf(card=sec["idx"])
    rows = [[1 if px[(y * w + x) * 3:(y * w + x) * 3 + 3] != b"\x00\x00\x00"
             else 0 for x in range(w)] for y in range(h)]
    return w, h, rows


def region(rows, x0, x1, y0, y1):
    out = bytearray()
    for y in range(y0, y1 + 1):
        out += bytes(rows[y][x0:x1 + 1])
    return bytes(out)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--swap", action="store_true",
                    help="make the MONO card the primary after extending. On "
                         "a VGA+Hercules machine that is the mixed-depth case "
                         "with teeth (docs/DUAL-DISPLAY-VGA.md 4.3): the cache "
                         "is sized from one display and gfx_save writes with "
                         "the planes of another, so a 1-plane primary and a "
                         "4-plane secondary is a buffer sized x1 written x4. "
                         "A no-op on a machine whose displays agree about "
                         "depth, which is every one but that pairing.")
    a = ap.parse_args(argv)

    fail = []
    say = lambda s: print("  " + s)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        cards = m.cards()
        if len(cards) != 2:
            sys.exit("dispsave: %s has %d video card(s), not 2"
                     % (a.machine, len(cards)))
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        if a.swap:
            avail = m.read(S("vid_avail"), 1)[0]
            dispcp.set_primary(m, mo, S, os88marty.settle,
                               dispcp.adapter_row(avail, 1))     # 1 = VID_HERC
            say("primary swapped to the mono card")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("dispsave: the Control Panel did not turn Extend on")

        kind = m.read(S("vid_kind"), 1)[0]
        pri = [c for c in cards if c["type"] == dispcp.KIND_CARD[kind]][0]
        sec = [c for c in cards if c is not pri][0]
        skind = "herc" if sec["type"] == "mda" else sec["type"]
        ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
        seam = u16(ctx, VID_CTX_SZ + VID_CTX_VX)
        secw = u16(ctx, VID_CTX_SZ + VID_CTX_CW)
        secy = u16(ctx, VID_CTX_SZ + VID_CTX_VY)   # SPEC.md 39.19.3: NOT 0
        say("secondary %s at x=%d, %d px wide" % (sec["type"], seam, secw))

        # two Disk windows - WF_SAVEU is theirs (SPEC.md 22.14), and they
        # stand perfectly still, which is subcheck.py's reasoning too
        dispcp.open_drive(m, mo, S, os88marty.settle, "A", card=pri["idx"])
        dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri["idx"])
        w = wins(m)
        if len(w) < 2:
            sys.exit("dispsave: wanted two Disk windows, got %d" % len(w))
        back, front = w[0], w[1]

        # THE FRONT ONE GOES FIRST, and it is not tidiness: two Disk windows
        # open 16 px apart, so the front one is sitting on the back one's
        # title bar and a drag aimed at the back one moves the front one -
        # silently, and the run then measures a window that never left
        # display 0.
        fx, fy, fw, fh = win_by(m, front)
        mo.drag(fx + fw // 2, fy + TITLE_H // 2, seam + 380,
                fy + 100 + TITLE_H // 2)
        os88marty.settle(m, card=sec["idx"])

        # ...then the back one, onto the second display beside it
        bx, by, bw, bh = win_by(m, back)
        mo.drag(bx + bw // 2, by + TITLE_H // 2, seam + 30 + bw // 2,
                by + TITLE_H // 2)
        os88marty.settle(m, card=sec["idx"])
        bx, by, bw, bh = win_by(m, back)
        say("back window on display 1 at (%d,%d) %dx%d" % (bx, by, bw, bh))
        if bx < seam:
            sys.exit("dispsave: the back window did not reach display 1")

        # the reference: the back window, uncovered, as it is now
        sw, sh, rows = surface(m, sec)
        # BOTH axes come off the display's origin. y used to be taken raw,
        # which was right only while VY was 0 (SPEC.md 39.19.3) and then
        # compared two different bands and called it corruption.
        x0, x1 = bx - seam, min(bx + bw - 1, seam + secw - 1) - seam
        y0, y1 = by - secy, min(by + bh - 1 - secy, sh - 1)
        ref = region(rows, x0, x1, y0, y1)
        say("reference: its %d..%d x %d..%d on the second card, %d px"
            % (x0, x1, y0, y1, len(ref)))

        # ...now cover part of it and raise it back
        fx, fy, fw, fh = win_by(m, front)
        mo.drag(fx + fw // 2, fy + TITLE_H // 2, bx + 60 + fw // 2,
                by + 40 + TITLE_H // 2)
        os88marty.settle(m, card=sec["idx"])
        fx, fy, fw, fh = win_by(m, front)
        say("front window dropped over it at (%d,%d)" % (fx, fy))

        # --- 1: was a cache taken for a window on the SECOND display? -------
        seg = cache_of(m, back)
        say("wm_su_segs[%d] = 0x%04X after being covered" % (back, seg))

        # ...and the cache was SIZED FOR THE DISPLAY IT IS ON, not for whichever
        # one happens to be current (docs/DUAL-DISPLAY-VGA.md 4.3). gfx_save
        # takes GFXDENTER, so it writes with the planes of the display the rect
        # is on, while wm_su_flay used to size the claim from [vid_planes_w] -
        # the primary's, every hook restoring it. Same number on any machine
        # whose displays agree about depth; on a VGA beside a Hercules the two
        # differ by 4x, in the direction of a heap overrun when the mono card
        # is primary. This is the only check here that can tell them apart, and
        # it is worth more than the pixels: the safe direction merely wastes
        # memory and looks perfect.
        pw = u16(m.read(S("wm_su_pw"), 2))
        pl = u16(m.read(S("vid_planes_w"), 2))
        want = 4 if sec["type"] == "vga" else 1
        say("wm_su_pw = %d (the %s the window is on), vid_planes_w = %d"
            % (pw, sec["type"], pl))
        if pw != want:
            fail.append("the cache is sized %d planes for a window on the %s, "
                        "wanted %d" % (pw, sec["type"], want))
        elif pw != pl:
            say("  ...and they DIFFER, which is the case that discriminates")
        if seg == 0:
            fail.append("no raise cache was taken for a window on display 1 - "
                        "wm_su_take's gate is still asking about [vid_cw], "
                        "the display last drawn on (SPEC.md 39.14.8)")

        # --- 2: ...and does raising it put the right pixels back? -----------
        mo.click(bx + bw // 2, by + TITLE_H // 2)
        os88marty.settle(m, card=sec["idx"])
        nb = win_by(m, back)
        if nb[:2] != (bx, by):
            fail.append("the back window moved to %s - the comparison below "
                        "is not the same rectangle" % (nb[:2],))
        sw, sh, rows = surface(m, sec)
        after = region(rows, x0, x1, y0, y1)
        d = sum(1 for p, q in zip(ref, after) if p != q)
        say("after the raise: %d px of %d differ from the uncovered reference"
            % (d, len(ref)))
        if d > len(ref) // 200:
            fail.append("%d px came back wrong on the second display - a save "
                        "banked on one card and put back on another looks "
                        "exactly like this (SPEC.md 39.14.8)" % d)

    print()
    for f in fail:
        print("dispsave: FAIL: %s" % f)
    if fail:
        return 1
    print("dispsave: the raise cache is taken on the second display and puts "
          "back what was there - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
