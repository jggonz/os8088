#!/usr/bin/env python3
"""A ROUND TRIP BETWEEN TWO CARDS (SPEC.md 11.96.11.4, 42.13.1.3)

    make && python3 tests/paintback.py

Paint is opened, dragged clear onto the other card and dragged home, and the
canvas is compared against the file at the end. Two legs, because a window can
be born on either card and the two are not the same case:

**vga** - VGA primary. Paint opens four planes, the Hercules refuses them
(SPEC.md 42.13.1) and pt_topacked converts, and the drag home has to leave a
picture behind.

**herc** - HERCULES primary, VGA secondary, which the Control Panel reaches by
making the second adapter primary. pt_geom decides the format from the display
a window is BORN on, so every Paint on such a machine opens packed - and
nothing else in this directory covers a machine whose colour adapter is not the
primary.

**WHAT IT CAUGHT IS NOT A DRAWING BUG.** The drag home fires
OSAPI_WM_ONRESIZE, Paint re-derived its screen, and pt_sucache then handed the
kernel 480 - its screen's HEIGHT, left in BX by OSAPI_WM_DISPLAY (SPEC.md
42.13.4). The kernel wrote through it: a bit set inside the API jump table,
and a word of zeros into wm_su_fnil's `xor ax, ax`, which the 8086 then read
as `add [bx+si], al` and which walked three more jump-table entries. The next
OSAPI_SET_COLOR jumped into the middle of mem_shed_one, the stack went with
it, and the UI task walked off holding the gfx lock - so nothing repainted
again and the canvas sat blank on a machine that was otherwise alive.

**Sixteen bytes of kernel .text turned it on and off**, which is why the first
assertion here is the PICTURE and not any internal flag: the corruption lands
wherever the layout puts it, and only one of the places it can land shows up
as a symptom. A test that watched wm_su_fnil would have passed on the build
that shipped it.

**AND THEN [pt_planar], because the canvas has to come HOME as four planes**
(SPEC.md 42.13.1.3). At 164 ms a repaint against 1,161 a canvas that stayed
packed is the VGA running at a seventh of its speed for the life of the
window - and on the herc leg it is worse than that, because there the planes
were never taken in the first place and the colour adapter would be the slow
one for ever. The flag is asserted only when the build has pt_wantpl, so this
row still means something on a kernel without the probe.

The VGA's canvas pixels are compared against the file; the mono half never is
(MartyPC's MDA aperture is offset from the guest origin, so a compare in guest
coordinates there samples the wrong pixels).
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, dispcp                 # noqa: E402
import dispapps                                              # noqa: E402


def _diskname(path):
    """What os88disk called it: colour_gif already answers in 8.3 upper case."""
    return os.path.basename(path)
from os88geom import (VID_CTX_SZ, VID_CTX_VX, VID_CTX_VY,    # noqa: E402
                      VID_CTX_CW, VID_CTX_CH, W_SEG, W_TITLE,
                      WIN_SIZE)
from blitpair import gif_pixels                              # noqa: E402
from paintmove import pkg_syms                               # noqa: E402

S = os88sym.linear
TITLE_H = 18


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def word(m, at):
    b = m.read(at, 2)
    v = b[0] | (b[1] << 8)
    return v - 65536 if v > 32767 else v


def compare(m, base, sym, D, iw, ih, px, card):
    """The canvas, where it is on the COLOUR display, against the file.

    The canvas's screen origin is read out of Paint ([pt_cx0]/[pt_cy0], the
    virtual position of canvas pixel 0,0) rather than derived from a window
    rect and an assumed inset - the window has been through a kind change and
    a re-fit by this point, so the inset is exactly the thing not to assume.
    """
    cx, cy = word(m, base + sym["pt_cx0"]), word(m, base + sym["pt_cy0"])
    fw, fh, fb = m.fbuf(card=card)
    dx, dy, dw, dh = D
    bad = seen = 0
    for r in range(ih):
        for c in range(iw):
            vx, vy = cx + c, cy + r
            if not (dx <= vx < dx + dw and dy <= vy < dy + dh):
                continue
            seen += 1
            if (fb[((vy - dy) * fw + (vx - dx)) * 3] < 128) != (px[r * iw + c] == 1):
                bad += 1
    return seen, bad


def run(a, case, iw, ih, px, sym):
    settle = os88marty.settle
    prim = 0 if case == "vga" else 1        # slot 0 is the VGA, 1 the Hercules
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        if len(m.cards()) != 2:
            sys.exit("paintback: %s is not a two-card machine" % a.machine)
        m.run()
        settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, settle)
        dispcp.set_primary(m, mo, S, settle, prim)
        dispcp.set_mode(m, mo, S, settle, "right")
        dispcp.close_panel(m, mo, S, settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("paintback: the Control Panel did not turn Extend on")

        def ctx(i):
            return m.read(S("vid_ctx") + i * VID_CTX_SZ, VID_CTX_SZ)
        D = [(u16(ctx(i), VID_CTX_VX), u16(ctx(i), VID_CTX_VY),
              u16(ctx(i), VID_CTX_CW), u16(ctx(i), VID_CTX_CH))
             for i in (0, 1)]
        # the VGA is 640 wide; the Hercules is 720. That is how the harness
        # tells them apart without assuming which display index went where
        vga = 0 if D[0][2] == 640 else 1
        home = 1 - vga if case == "herc" else vga
        print("   display 0 %dx%d at (%d,%d), display 1 %dx%d at (%d,%d); "
              "VGA is display %d, Paint is born on %d"
              % (D[0][2], D[0][3], D[0][0], D[0][1], D[1][2], D[1][3],
                 D[1][0], D[1][1], vga, home))
        # MartyPC's card order is the machine's, not the OS's: card 0 is the
        # VGA in os8088_xt_vga_mda whichever display the OS calls primary
        vcard = 0

        # --- open the picture, on whichever display is the PRIMARY (that is
        # where the drive column and so the file windows are)
        pcard = vcard if prim == 0 else 1
        dispcp.open_drive(m, mo, S, settle, "B", card=pcard)
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, settle, bx, by, "MEDIA", card=pcard)
        settle(m, card=pcard)
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        rx, ry = dispcp.row_xy(bx, by,
                               dispcp.scroll_to(m, mo, S, settle, bx, by,
                                                dispcp.row_of(m, S,
                                                              _diskname(a.gif)),
                                                card=pcard))
        mo.to(rx, ry)
        settle(m, card=pcard)
        mo.dblclick(rx, ry)
        time.sleep(10)
        settle(m, card=pcard)

        # ...and find it. gfx_blitp is the tell for a planar canvas, but on the
        # Hercules leg there is no gfx_blitp to break on, so the package base
        # comes off the window list and pkg_syms instead
        pw = [w for w in dispcp.win_list(m, S) if w != disk][-1]
        if win_title(m, pw) != "Paint":
            sys.exit("paintback: the newest window is titled %r, not Paint - "
                     "the picture did not open" % win_title(m, pw))
        base = pkg_base(m, pw)
        planar = m.read(base + sym["pt_planar"], 1)[0]
        wx, wy, ww, wh = dispcp.win_rect(m, S, pw)
        print("   born: [pt_planar] = %d, window (%d,%d) %dx%d"
              % (planar, wx, wy, ww, wh))
        if home == vga:                 # ...and the compare itself, checked
            try:                        # PARK THE POINTER FIRST: the arrow is
                mo.to(D[vga][0] + 4, D[vga][1] + 4)   # 23 canvas pixels and
                settle(m, card=vcard)   # they are not the picture's
            except Exception as e:
                print("   (parking the pointer: %s)" % e)
            s0, b0 = compare(m, base, sym, D[vga], iw, ih, px, vcard)
            print("   born: %d canvas pixels on the VGA, %d differ" % (s0, b0))
            if b0:
                sys.exit("paintback: the canvas is %d pixels wrong BEFORE "
                         "anything moved, so nothing below means anything"
                         % b0)
        want = 1 if home == vga else 0
        if planar != want:
            sys.exit("paintback: Paint was born on display %d and holds "
                     "planar=%d, wanted %d - the leg is not set up"
                     % (home, planar, want))

        if case == "vga":
            # --- out to the Hercules, which must REFUSE and convert
            far = D[1 - vga][0] + 20
            mo.drag(wx + ww // 2, wy + TITLE_H // 2,
                    far + ww // 2, wy + TITLE_H // 2)
            settle(m, card=1 - vcard)
            time.sleep(8)
            gone = m.read(base + sym["pt_planar"], 1)[0]
            print("   on the Hercules: [pt_planar] = %d" % gone)
            if gone:
                sys.exit("paintback: the canvas is still four planes on a "
                         "1bpp display, so the refusal never reached Paint")
            wx, wy, ww, wh = dispcp.win_rect(m, S, pw)

        # --- and home to the VGA, in ONE drag, so it passes through the
        # straddle that reads as a colour display (SPEC.md 39.16.4.2)
        tgt = D[vga][0] + 8
        mo.drag(wx + ww // 2, wy + TITLE_H // 2,
                tgt + ww // 2, wy + TITLE_H // 2)
        settle(m, card=vcard)
        time.sleep(10)
        back = m.read(base + sym["pt_planar"], 1)[0]
        nest = m.read(S("gfx_dnest"), 1)[0]
        armed = m.read(base + sym["pt_wantpl"], 1)[0] \
            if "pt_wantpl" in sym else -1
        wx2, wy2, ww2, wh2 = dispcp.win_rect(m, S, pw)
        print("   home on the VGA: [pt_planar] = %d, [pt_wantpl] = %d, "
              "[gfx_dnest] = %d, window at x=%d %dx%d"
              % (back, armed, nest, wx2, ww2, wh2))
        if wx2 < D[vga][0] or wx2 + ww2 > D[vga][0] + D[vga][2]:
            sys.exit("paintback: the window ended at x=%d %d wide, which is "
                     "not clear on the VGA (%d..%d) - nothing was under test"
                     % (wx2, ww2, D[vga][0], D[vga][0] + D[vga][2]))
        try:                            # park the pointer clear of the canvas,
            mo.to(D[vga][0] + 4, D[vga][1] + 4)   # a tidiness, not a check
            settle(m, card=vcard)
        except Exception as e:
            print("   (parking the pointer: %s)" % e)
        seen, bad = compare(m, base, sym, D[vga], iw, ih, px, vcard)
        print("   %s: %d canvas pixels on the VGA, %d differ from the file"
              % (case, seen, bad))

    if nest:
        sys.exit("paintback: FAILED - [gfx_dnest] is %d, so a display nest "
                 "was left unbalanced" % nest)
    if "pt_wantpl" in sym and not back:
        sys.exit("paintback: FAILED - the window is wholly on the VGA and the "
                 "canvas is still NIBBLES, so it repaints at a seventh of the "
                 "speed the planes would give it")
    if not seen:
        sys.exit("paintback: none of the canvas is on the VGA, so the compare "
                 "proves nothing")
    if bad:
        sys.exit("paintback: FAILED - the canvas came back planar and WRONG")
    print("   %s: crossed, came home, and the picture is still the picture"
          % case)
    return 0


def win_title(m, slot):
    """...and its title, so "the newest window" is checked rather than hoped."""
    r = m.read(S("wm_wins") + slot * WIN_SIZE, WIN_SIZE)
    tp = r[W_TITLE] | (r[W_TITLE + 1] << 8)
    seg = r[W_SEG] | (r[W_SEG + 1] << 8)
    raw = m.readseg(seg or os88sym.KERNEL_SEG, tp, 24)
    return bytes(raw).split(b"\0")[0].decode("latin-1")


def pkg_base(m, slot):
    """The segment window `slot`'s package was loaded at, out of W_SEG.

    dispblitp.py takes it off a gfx_blitp call's return address, which only a
    PLANAR canvas ever makes - and the whole point of the Hercules leg is a
    Paint that starts packed. W_SEG is the same answer with no blit in it, and
    it is 0 for a kernel window, which a package's never is.

    `slot` is an INDEX, which is what dispcp.win_list returns - every other
    caller there passes it straight to win_rect and never sees the difference.
    Read as an address it is 2, and W_SEG off that is a plausible-looking
    segment in the middle of the interrupt table: the first version of this
    reported Paint as holding a packed canvas on a VGA primary.
    """
    at = S("wm_wins") + slot * WIN_SIZE + W_SEG
    seg = m.read(at, 2)
    seg = seg[0] | (seg[1] << 8)
    if not seg:
        sys.exit("paintback: window %d has W_SEG = 0, so it is a KERNEL "
                 "window and not the Paint we opened" % slot)
    return seg << 4


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/paintback.img")
    ap.add_argument("--machine", default="os8088_xt_vga_mda")
    ap.add_argument("--gif", default=None,
                    help="the picture to open (default: a COLOUR derivation "
                         "of build/OS8088.GIF - see below)")
    ap.add_argument("--case", choices=("vga", "herc"), default=None)
    a = ap.parse_args()

    # A COLOUR PICTURE. This row's subject is [pt_planar] coming HOME as four
    # planes, and SPEC.md 42.23.6 opens a two-entry GIF one bit deep on any
    # adapter - build/OS8088.GIF being exactly two entries. So the fixture had
    # to become a colour one or the leg could never be set up; the failure
    # read "Paint was born on display 0 and holds planar=0", which is true and
    # is not this row's subject. dispapps.colour_gif changes no pixel.
    if a.gif is None:
        a.gif = dispapps.colour_gif()

    if a.apps == "/tmp/paintback.img":
        os88marty.scratch_disk(a.apps, "APPS:build/paint.o88",
                               "MEDIA:" + a.gif)

    iw, ih, px = gif_pixels(a.gif)
    sym = pkg_syms("apps/paint/paint.asm")
    for case in ([a.case] if a.case else ["vga", "herc"]):
        run(a, case, iw, ih, px, sym)
    print("paintback: a window crosses to the other card and home again with "
          "its picture intact, and its planes back")
    return 0


if __name__ == "__main__":
    sys.exit(main())
