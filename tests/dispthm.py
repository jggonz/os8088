#!/usr/bin/env python3
"""Does SPEC.md 76's THEME meet the extended desktop honestly?

    make && python3 tests/dispthm.py

`thm_set` refuses Color on a machine with no colour in it, and
`cp_thm_colgrey` greys the row there - both by asking `[vid_kind]`, which is
**the PRIMARY's adapter**. On a two-card machine whose primary is a VGA that
is the right answer for the primary and says nothing at all about the other
monitor: Color is accepted, and every window dragged onto a 1bpp secondary
draws its chrome through SPEC.md 39.4's reduction.

What that reduction makes of the Color row is the question, and it is not
"Bright with extra steps" - `CLGRAY` chrome ground becomes the 50% dither and
`CBLACK` chrome ink stays black, which is pixel-for-pixel what SPEC.md 47
draws a DISABLED control as. So this measures the second monitor rather than
arguing about the table: how much of a window's chrome there is dithered
ground, under each of the three themes.

It asserts nothing about which answer is right - that is the fork owner's
call - and prints the three so the choice is made against numbers.
"""
import argparse
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sym                                          # noqa: E402
import dispcp                                           # noqa: E402
import dispsize                                         # noqa: E402

S = os88sym.linear
u16 = dispsize.u16

TITLE_H = 18
CP_RX, CP_PGX = 96, 4
CPH_ROWS = {"Bright": 22, "Dark": 38, "Color": 55}      # pane-relative y
CP_ITHM = 5                                             # kernel/ctrl.inc

# SPEC.md 39.14's kind byte, from the ONE place that mirrors the record. It
# was `VID_CTX_KIND = 40` here, against a kernel that has said 42 since
# 6.1.10's vid_tseg widened the run - so this file read `vid_tseg` and called
# it an adapter kind, and got a framebuffer segment (0xB000/0xB800) low byte
# rather than a 0 or 1. It passed anyway, which is why it went unseen.
from os88geom import VID_CTX_KIND                        # noqa: E402


def theme_page(m, mo, S, settle_):
    """Open the Control Panel and select the Theme row - which is NOT the last
    one and is NOT a fixed row either: [cp_hide] decides (kernel/ctrl.inc's
    cp_v2r), so the drawn row is the number of SHOWN records below it."""
    dispcp.open_panel(m, mo, S, settle_, page=CP_ITHM)


def pick(m, mo, S, settle_, name):
    wx, wy = dispcp._cp_win(m, S)
    mo.click(wx + 1 + CP_RX + CP_PGX + 6, wy + TITLE_H + 1 + CPH_ROWS[name])
    settle_(m)
    return m.read(S("thm_kind"), 1)[0]


def dithered(rows):
    """Is this band SPEC.md 26's 50% dither rather than a solid ground?

    The pattern alternates every pixel in x AND every row in y, so on a 1bpp
    card exactly half the pixels of a solid area are lit and neighbours never
    agree. Counting lit pixels alone cannot tell a dither from text (a caption
    is dark pixels on white too), so this asks the sharper question: what
    fraction of horizontally adjacent pairs DIFFER. A solid ground answers ~0,
    a dither ~1, and a line of glyphs lands in between.
    """
    diff = tot = 0
    for r in rows:
        for i in range(len(r) - 1):
            tot += 1
            diff += 1 if r[i] != r[i + 1] else 0
    return (diff / tot) if tot else 0.0


def band(m, x, y, w, h, card):
    """h rows of w pixels of `card`'s rendered framebuffer, as 0/1.

    fbuf answers RGB24 rather than palette indices - it asks the CARD what it
    rasterised - so on a 1bpp adapter a lit pixel is simply a non-zero byte.
    """
    fw, fh, fb = m.fbuf(card=card)
    out = []
    for row in range(max(0, y), min(y + h, fh)):
        base = row * fw * 3
        out.append([1 if fb[base + c * 3] else 0
                    for c in range(max(0, x), min(x + w, fw))])
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga_herc")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--out", default="build/thm")
    a = ap.parse_args(argv)

    fail = []
    settle = os88marty.settle
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        if len(m.cards()) != 2:
            sys.exit("dispthm: %s is not a two-card machine" % a.machine)
        m.run()
        settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispsize.extend(m, mo, settle)

        c0, c1 = dispsize.ctx(m, 0), dispsize.ctx(m, 1)
        seam = u16(c1, dispsize.VID_CTX_VX)
        k0 = m.read(S("vid_kind"), 1)[0]
        k1 = c1[VID_CTX_KIND]                  # kernel/vidsel.inc
        print("  primary kind %d, secondary kind %d, seam at %d"
              % (k0, k1, seam))
        if k0 != 0:
            sys.exit("dispthm: the PRIMARY must be the VGA - Color is refused "
                     "outright otherwise and there is nothing to measure")

        # A window on the SECONDARY, which is the whole question.
        dispcp.open_drive(m, mo, S, settle, "B", card=0)
        slot = dispcp.win_list(m, S)[-1]
        x, y, w, h = dispcp.win_rect(m, S, slot)
        mo.drag(x + w // 2, y + TITLE_H // 2, seam + 40 + w // 2, 60)
        settle(m, card=1)
        x, y, w, h = dispcp.win_rect(m, S, slot)
        if x < seam:
            sys.exit("dispthm: the window did not reach the second display")
        print("  a Disk window on the SECONDARY at (%d,%d) %dx%d\n"
              % (x, y, w, h))

        os.makedirs(a.out, exist_ok=True)
        # ...and a patch of BARE SECONDARY DESKTOP, well clear of the window
        # and of the seam: this is what SPEC.md 76.2.1's bug lived in, and the
        # measurement is trivial once it is being taken at all.
        dx0 = 20
        dy0 = u16(c1, dispsize.VID_CTX_CH) - 40
        rows = []
        for name in ("Bright", "Dark", "Color"):
            theme_page(m, mo, S, settle)
            got = pick(m, mo, S, settle, name)
            # the CHROME GROUND, sampled inside the window's frame and above
            # its content: the title bar band, minus the two boxes and the
            # caption, so what is measured is the FIELD a caption sits on
            bx = x - seam + w // 2 - 24
            band_ = band(m, bx, y - u16(c1, dispsize.VID_CTX_VY) + 4, 48,
                         TITLE_H - 8, 1)
            frac = dithered(band_)
            lit = sum(sum(r) for r in band_)
            tot = sum(len(r) for r in band_)
            # THE SECOND DISPLAY'S OWN GROUND (SPEC.md 76.2.1). Bright is
            # SPEC.md 26's dither and Dark is solid black, so the two are
            # unmistakable - and the bug this exists for is that the
            # secondary kept the DITHER whatever the theme, which is exactly
            # what a build with vid_disp_desk still calling gfx_fill_gray
            # answers here.
            gnd = band(m, dx0, dy0, 64, 16, 1)
            gfrac = dithered(gnd)
            glit = sum(sum(r) for r in gnd)
            gtot = sum(len(r) for r in gnd)
            rows.append((name, got, frac, lit, tot, gfrac, glit, gtot))
            print("  %-7s [thm_kind]=%d   title-bar ground: %.2f of adjacent "
                  "pairs differ, %d of %d lit"
                  % (name, got, frac, lit, tot))
            print("           bare desktop on the SECONDARY: %.2f differ, "
                  "%d of %d lit" % (gfrac, glit, gtot))
            if name == "Dark" and got == 1 and glit > gtot // 4:
                fail.append("Dark leaves the SECOND display's desktop %d of "
                            "%d lit - it is still Bright's ground. thm_desk is "
                            "the themed fill and vid_disp_desk is the site "
                            "(SPEC.md 76.2.1)" % (glit, gtot))
            png = os.path.join(a.out, "secondary-%s.png" % name.lower())
            fw, fh, fb = m.fbuf(card=1)
            os88marty.write_png_rgb(png, fw, fh, fb)
            print("           %s" % png)

        print("\n  a solid ground answers ~0.00 and SPEC.md 26's 50% dither "
              "~1.00; anything between is glyphs.")
        for name, got, frac, lit, tot, gfrac, glit, gtot in rows:
            if name == "Color" and got != 2:
                print("  ...Color was REFUSED on this machine ([thm_kind]=%d), "
                      "so there is nothing to look at" % got)

    print()
    for f in fail:
        print("dispthm: FAIL: " + f)
    if fail:
        return 1
    print("dispthm: the theme reaches the SECOND display, and Color's chrome "
          "there is the dither SPEC.md 76.12.4 says it is - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
