#!/usr/bin/env python3
"""HERCULES PRIMARY, VGA SECOND (SPEC.md 39.19.2's other arrangement)

    make && python3 tests/dispherc1.py

Reported from the field: choosing "Herc main + VGA second" put the HERCULES
into TEXT mode - the screenshot is 80x25 of whatever the graphics framebuffer
holds, blink attributes and all - and turned the VGA OFF.

Both halves are about the same moment, so both are captured: the live video
block, what each card says it is rasterising, and a PNG of each. A Hercules
in text mode is not a blank screen and not a wrong picture; it is the 6845
programmed for characters over memory holding pixels, which is what
`vid_setmode`'s graphics bit exists to prevent and what `vid_blank_kind`
writing a bare 0 to 3B8h used to cause (SPEC.md 64).

IT DOES NOT REPRODUCE HERE, AND THAT IS THE RESULT RATHER THAN A FAILURE OF
THE TEST. On `os8088_xt_vga_herc` the arrangement comes up CORRECT: the
Hercules rasterises 720x350 of graphics carrying the menu bar, the drive
column and the desktop, and the VGA rasterises 640x480 of which exactly
153,600 pixels - half, the desktop dither - are lit, which is what a
secondary with no windows on it should be. `[vid_kind]` is HERC, `ndisp` 2,
`mono` 1, and the two records read HERC at (0,0) and VGA at (720,20).

So the field symptom needs the REAL CARDS, and there is a documented reason
to expect exactly that: MartyPC's MDA answers "text mode" forever and its
`mode`/`text` query is dead (docs/MARTYPC-DEBUG.md), so a card wrongly left
in text is the one class of Hercules defect this emulator cannot show. 86Box
with the real ROM sets, or the 5150, is where that question lands. Keep this
script anyway - it is the whole arrangement in one run, and it is the control
that says the KERNEL-SIDE state is right when the glass is not.
"""
import sys
import time

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")

from os88geom import (VID_CTX_SZ, VID_CTX_VX,          # noqa: E402
                      VID_CTX_VY, VID_CTX_KIND, VID_CTX_CH)
# SPEC.md 39.14's per-display record: DERIVED from VID_CTX_W and never
# written down here. This file spelled it `42 + 36`, which is the
# VID_CTX_W = 18 layout - two bytes early, and what sits there is
# display 1's vid_chm8, so the seam read 192 instead of 720.
import os88marty, os88mouse, os88sym, dispcp                # noqa: E402

S = os88sym.linear
VID_VGA, VID_HERC, VID_CGA = 0, 1, 2
KINDS = {0: "VGA", 1: "HERC", 2: "CGA"}


def u16(b, i=0): return b[i] | (b[i + 1] << 8)


def lit(m, card):
    w, h, d = m.fbuf(card=card)
    return sum(1 for i in range(0, len(d), 3) if d[i] or d[i + 1] or d[i + 2])


def report(m, label, cards):
    print("  %s" % label)
    print("    kind=%s ndisp=%d cur=%d mono=%d planes=%d w=%d h=%d"
          % (KINDS.get(m.read(S("vid_kind"), 1)[0], "?"),
             m.read(S("vid_ndisp"), 1)[0], m.read(S("vid_cur"), 1)[0],
             m.read(S("vid_mono"), 1)[0], m.read(S("vid_planes"), 1)[0],
             u16(m.read(S("vid_w"), 2)), u16(m.read(S("vid_h"), 2))))
    ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
    print("    ctx0 kind=%s v=(%d,%d)   ctx1 kind=%s v=(%d,%d)"
          % (KINDS.get(ctx[VID_CTX_KIND], "?"), u16(ctx, VID_CTX_VX),
             u16(ctx, VID_CTX_VY),
             KINDS.get(ctx[VID_CTX_SZ + VID_CTX_KIND], "?"),
             u16(ctx, VID_CTX_SZ + VID_CTX_VX),
             u16(ctx, VID_CTX_SZ + VID_CTX_VY)))
    for c in cards:
        print("    card %d (%s): %d lit" % (c, cards[c]["type"], lit(m, c)))


def main():
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine="os8088_xt_vga_herc", boot=False) as m:
        cards = {c["idx"]: c for c in m.cards()}
        pri = [i for i, c in cards.items() if c["type"] == "vga"][0]
        sec = [i for i, c in cards.items() if c["type"] == "mda"][0]
        m.run(); os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        report(m, "at the desktop (VGA primary, single)", cards)

        avail = m.read(S("vid_avail"), 1)[0]
        slot = dispcp.adapter_row(avail, VID_HERC)
        print("  vid_avail=0x%02X, the Hercules is list row %d" % (avail, slot))

        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        report(m, "extended right, VGA still primary", cards)
        dispcp.set_primary(m, mo, S, os88marty.settle, slot, card=sec)
        report(m, "...and the HERCULES made primary", cards)
        dispcp.close_panel(m, mo, S, os88marty.settle, card=sec)
        time.sleep(2)
        report(m, "panel closed", cards)

        # --- and a DRAG must not put the dock on the second monitor --------
        # SPEC.md 39.19.5. The primary's dock ROW is inside the secondary's
        # extent on this arrangement (the Hercules is 350 tall and its strip
        # is at 326; the VGA sits at virtual y 20 and covers to 499), so a
        # damage span running past the primary drew a full-width white band
        # across the wrong card. The assertion is the sound one: what the
        # second card holds after the drag must be what a forced full repaint
        # would put there.
        dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=sec)
        disk = dispcp.win_list(m, S)[-1]
        wx, wy, ww, wh = dispcp.win_rect(m, S, disk)
        mo.drag(wx + ww // 2, wy + 9, wx + ww // 2 + 200, wy + 9 + 40)
        os88marty.settle(m, card=sec)
        mo.to(60, 300)
        os88marty.settle(m, card=sec)
        after = m.fbuf(card=pri)[2]
        report(m, "after dragging a window", cards)
        dispcp.open_panel(m, mo, S, os88marty.settle, card=sec)
        dispcp.close_panel(m, mo, S, os88marty.settle, card=sec)
        mo.to(60, 300)
        os88marty.settle(m, card=sec)
        forced = m.fbuf(card=pri)[2]
        n = sum(1 for i in range(0, len(after), 3)
                if after[i:i + 3] != forced[i:i + 3])
        print("  SECOND CARD after the drag vs a forced repaint: %d differing "
              "pixel(s)  %s" % (n, "ok" if not n else "*** BAND ***"))

        for c in cards:
            w, h, d = m.fbuf(card=c)
            os88marty.write_png_rgb("/tmp/herc1-card%d.png" % c, w, h, d)
        print("  PNGs in /tmp/herc1-card*.png")
        return 1 if n else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
