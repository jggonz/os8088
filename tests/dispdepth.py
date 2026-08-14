#!/usr/bin/env python3
"""Does a window dragged BACK from a different-depth display arrive intact?
(SPEC.md 11.96.12.1, docs/DUAL-DISPLAY-VGA.md 4.3.1)

    make && python3 tests/dispdepth.py --dump /tmp/live.bin
    make DRAGCACHE=0 && python3 tests/dispdepth.py --expect /tmp/live.bin

`tests/dispdrag.py`'s sibling, and separate because it is a different fault
on a pairing that one cannot reach: dispdrag asks whether a window STRADDLING
the seam draws on both cards, and both of its displays are 1bpp. This asks
what happens when the two displays disagree about DEPTH.

SPEC.md 11.96.12's drag cache banks the window's content at the old position
and RELABELS the header by the drag's delta so the replay lands at the new
one. `gfx_save` laid that buffer out with the planes of the display the pixels
were on - so a window banked on a Hercules is w*h/8 bytes of ONE plane, and
replaying it onto a VGA writes four. Mode 12h then renders 1bpp bytes as
colour, which is what the field saw: a Disk window whose frame is perfect and
whose content is magenta and cyan noise.

THE ROUND TRIP IS THE TEST, and only its last leg can fail. A drag is offered
the cache only when its SOURCE is wholly on one display (wm_su_take asks
vid_span_one), so:

    park on display 0        no cache - it has not moved yet
    drag onto display 1      source wholly on 0, destination on 1  <- refused
                             by the depth test; before it, this replayed
                             1bpp-shaped bytes onto whichever card
    drag back onto 0         source wholly on 1                    <- the one
                             the field reported

which is why the field's first two drags looked right and the third did not.

THE ASSERTION IS THE REFERENCE BUILD, not a screenshot, for dispdrag's reason:
what a broken run leaves is a picture rather than an error, and nothing in it
says "wrong". `make DRAGCACHE=0` removes the cache entirely, so the same
scripted session through both kernels must put the same pixels on the primary.
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

TITLE_H = 18
VID_CTX_SZ, VID_CTX_CW, VID_CTX_VX = 42, 14, 36

# The reference leg runs a KNOB BUILD, and os88sym re-assembles the kernel to
# read its map - so it has to be told the same define the Makefile passed, or
# it correctly refuses on "the map describes a DIFFERENT kernel". DRAGCACHE=0
# is -DNODRAGCACHE (Makefile).
S = os88sym.linear


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def surface(m, card):
    """The card's RASTERISED output, one byte per pixel.

    `fbuf` rather than `vram` because the primary here is a VGA in mode 12h -
    four planes behind the Graphics Controller, so its memory is not
    flat-readable (os88marty.vram says so). It also asks the card what it
    actually put on the glass, which is the thing the field looked at.
    """
    w, h, px = m.fbuf(card=card)
    return w, h, bytes(px[i] for i in range(0, len(px), 3))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga_herc")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--dump", help="write the PRIMARY's framebuffer here")
    ap.add_argument("--expect", help="...and compare against this one")
    ap.add_argument("--define", action="append", default=[],
                    help="a knob the kernel under test was built with, so "
                         "os88sym maps the RIGHT kernel: --define NODRAGCACHE")
    a = ap.parse_args(argv)

    global S
    if a.define:
        defs = tuple(a.define)
        S = lambda n: os88sym.linear(n, defines=defs)

    fail = []
    say = lambda s: print("  " + s)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        cards = m.cards()
        if len(cards) != 2:
            sys.exit("dispdepth: %s has %d video card(s), not 2"
                     % (a.machine, len(cards)))
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("dispdepth: the Control Panel did not turn Extend on")

        kind = m.read(S("vid_kind"), 1)[0]
        pri = [c for c in cards
               if c["type"] == dispcp.KIND_CARD[kind]][0]
        sec = [c for c in cards if c is not pri][0]
        if pri["type"] == sec["type"]:
            sys.exit("dispdepth: both cards are %s - this test needs two "
                     "displays of DIFFERENT depth" % pri["type"])
        ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
        seam = u16(ctx, VID_CTX_SZ + VID_CTX_VX)
        secw = u16(ctx, VID_CTX_SZ + VID_CTX_CW)
        say("primary %s, secondary %s at x=%d, %d px wide"
            % (pri["type"], sec["type"], seam, secw))

        dispcp.open_drive(m, mo, S, os88marty.settle, "A", card=pri["idx"])
        w = dispcp.win_list(m, S)
        if not w:
            sys.exit("dispdepth: no Disk window opened")
        win = w[-1]

        def rect():
            return dispcp.win_rect(m, S, win)

        def move(to, card):
            x, y, ww, wh = rect()
            mo.drag(x + ww // 2, y + TITLE_H // 2, to + ww // 2,
                    y + TITLE_H // 2)
            os88marty.settle(m, card=card)

        x, y, ww, wh = rect()
        say("Disk window at (%d,%d) %dx%d" % (x, y, ww, wh))

        # 1. park wholly on the primary, on an 8px phase (11.96.13)
        move((seam - ww - 40) & ~7, pri["idx"])
        x, y, ww, wh = rect()
        if x + ww - 1 >= seam:
            sys.exit("dispdepth: the window did not park clear of the seam")
        say("parked wholly on display 0 at x=%d" % x)

        # 2. wholly onto the secondary - source on 0, destination on 1
        move((seam + 24) & ~7, sec["idx"])
        x, y, ww, wh = rect()
        if x < seam:
            sys.exit("dispdepth: the window did not reach display 1 (x=%d, "
                     "seam=%d)" % (x, seam))
        say("dragged wholly onto display 1 at x=%d" % x)

        # 3. ...and back. THIS is the leg the field reported.
        move((seam - ww - 40) & ~7, pri["idx"])
        x, y, ww, wh = rect()
        say("dragged back onto display 0 at x=%d" % x)
        if x + ww - 1 >= seam:
            fail.append("the window did not come back clear of the seam")

        pw, ph, shot = surface(m, pri["idx"])
        lit = sum(1 for p in shot if p)
        say("primary: %dx%d, %d lit" % (pw, ph, lit))

        # A cheap independent read on the same pixels, because a reference
        # build is not always to hand: this window is black, white and grey,
        # and 1bpp bytes rendered as four planes are not. Reported, never
        # failed on - the palette is a property of the Disk window's chrome
        # and would make this test a hostage to a theme change.
        if 0 <= x and x + ww <= pw:
            inside = set()
            for row in range(y + TITLE_H, min(y + wh, ph)):
                base = row * pw
                inside.update(shot[base + x:base + x + ww])
            say("%d distinct colours inside the window's content"
                % len(inside))

        if a.dump:
            with open(a.dump, "wb") as f:
                f.write(shot)
            say("written to %s" % a.dump)
        if a.expect:
            with open(a.expect, "rb") as f:
                ref = f.read()
            if len(ref) != len(shot):
                fail.append("the reference is %d bytes and this is %d - not "
                            "the same machine" % (len(ref), len(shot)))
            else:
                d = sum(1 for i in range(len(ref)) if ref[i] != shot[i])
                say("%d differing pixels of %d against the reference"
                    % (d, len(ref)))
                if d:
                    fail.append("%d pixels differ from the no-cache build - "
                                "the drag cache put back something else "
                                "(SPEC.md 11.96.12.1)" % d)

    if fail:
        print("\ndispdepth: FAIL")
        for f in fail:
            print("  - " + f)
        return 1
    print("\ndispdepth: a window dragged back across a depth change arrives "
          "intact - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
