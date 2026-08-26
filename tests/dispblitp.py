#!/usr/bin/env python3
"""DOES gfx_blitp's REFUSAL SURVIVE ITS OWN TEARDOWN? (SPEC.md 5.4.3, 39.14.7.1)

    make && python3 tests/dispblitp.py

`gfx_blitp`'s entire output is the carry flag, and its teardown opened with a
`cmp` - which writes CF. So `stc` lived two instructions and **every refusal
came back as "drawn"**. The success path survived by luck, `cmp 0, 0` clearing
CF being the same answer `clc` had just given.

It was invisible for a release because none of the six refusals is reachable
on a one-display VGA: the adapter is not 1bpp, SPEC.md 42.13 aligns the x by
construction, and there is no seam. An EXTENDED DESKTOP has all three. What
the field saw was Paint dragged across the seam - or clear onto the mono
monitor - with a canvas that drew NOTHING, because Paint had been told it was
drawn and SPEC.md 42.13.1's fallback therefore never ran.

THE ASSERTION IS [pt_planar], not a screenshot, and that is the point: the
refusal is a fact inside the two programs, and a picture of it is a picture of
whatever else the repaint happened to leave. Paint's canvas is four planes on
the VGA; the first blit that straddles must be refused, and Paint must answer
by converting to nibbles once and for all. If the flag is still 1 afterwards
the refusal was lost, whatever the screen shows.

The VGA's half of the canvas is compared against the file as well. The mono
half is NOT: MartyPC's MDA aperture is offset from the guest origin (SPEC.md
5.4.1.1 says so, and 15,054 pixels of a two-colour logo differ if you forget),
so a compare in guest coordinates there is sampling the wrong pixels.
"""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, dispcp                 # noqa: E402
from os88geom import (VID_CTX_SZ, VID_CTX_VX, VID_CTX_VY,    # noqa: E402
                      VID_CTX_CW, VID_CTX_CH)
from blitpair import gif_pixels                              # noqa: E402
from paintmove import pkg_syms                               # noqa: E402

S = os88sym.linear
TITLE_H = 18


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def run(a, case, iw, ih, px, sym):
    settle = os88marty.settle
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        if len(m.cards()) != 2:
            sys.exit("dispblitp: %s is not a two-card machine" % a.machine)
        m.run()
        settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, settle)
        dispcp.set_primary(m, mo, S, settle, 0)
        dispcp.set_mode(m, mo, S, settle, "right")
        dispcp.close_panel(m, mo, S, settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("dispblitp: the Control Panel did not turn Extend on")

        def ctx(i):
            return m.read(S("vid_ctx") + i * VID_CTX_SZ, VID_CTX_SZ)
        D = [(u16(ctx(i), VID_CTX_VX), u16(ctx(i), VID_CTX_VY),
              u16(ctx(i), VID_CTX_CW), u16(ctx(i), VID_CTX_CH))
             for i in (0, 1)]
        print("   display 0 %dx%d at (%d,%d), display 1 %dx%d at (%d,%d)"
              % (D[0][2], D[0][3], D[0][0], D[0][1],
                 D[1][2], D[1][3], D[1][0], D[1][1]))
        seam = max(D[0][0], D[1][0])

        dispcp.open_drive(m, mo, S, settle, "B", card=0)
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, settle, bx, by, "MEDIA", card=0)
        settle(m, card=0)
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        rx, ry = dispcp.row_xy(bx, by,
                               dispcp.scroll_to(m, mo, S, settle, bx, by,
                                                dispcp.row_of(m, S,
                                                              "OS8088.GIF"),
                                                card=0))
        mo.to(rx, ry)
        settle(m, card=0)
        m.bp_exec("gfx_blitp")
        mo.dblclick(rx, ry)
        if not m.wait_stop(limit=300.0):
            sys.exit("dispblitp: no gfx_blitp at all - the canvas is not "
                     "planar, so there is nothing here to refuse")
        r = m.regs()
        base = int.from_bytes(
            m.read((r["ss"] << 4) + r["sp"] + 2, 2), "little") << 4
        ox0, oy0 = r["ax"], r["bx"]      # ...where Paint puts its canvas, ASKED
        m.bp_exec()
        m.run()
        time.sleep(6)
        planar = base + sym["pt_planar"]
        if not m.read(planar, 1)[0]:
            sys.exit("dispblitp: Paint's canvas is not four planes on the "
                     "VGA, so the refusal under test cannot happen")
        pw = [w for w in dispcp.win_list(m, S) if w != disk][-1]
        wx, wy, ww, wh = dispcp.win_rect(m, S, pw)
        # the canvas's inset in the FRAME, derived from the blit rather than
        # assumed: the frame border and SPEC.md 42.13's PT_CV_X are both in it
        ix, iy = ox0 - wx, oy0 - wy
        print("   canvas is four planes at +%d,+%d in the frame; window "
              "(%d,%d) %dx%d, seam at x=%d" % (ix, iy, wx, wy, ww, wh, seam))

        # STRADDLE: two thirds over the far card, so it is unambiguous and
        # the title bar stays reachable on the near one. DIRECT: clear onto
        # the far display in one move, which never straddles - the outline
        # drag repaints nothing on the way, so the first blit after it is a
        # whole canvas on a MONO display, and gfx_blitp refuses on its very
        # first guard rather than on the seam. That is a different exit from
        # the same routine and it had a different bug behind it.
        tgt = (seam - ww // 3) if case == "straddle" else (D[1][0] + 20)
        mo.drag(wx + ww // 2, wy + TITLE_H // 2,
                tgt + ww // 2, wy + TITLE_H // 2)
        settle(m, card=0)
        time.sleep(8)
        still = m.read(planar, 1)[0]
        nest = m.read(S("gfx_dnest"), 1)[0]
        wx2, wy2, _, _ = dispcp.win_rect(m, S, pw)
        print("   %s: window at x=%d, [pt_planar] = %d, [gfx_dnest] = %d"
              % (case, wx2, still, nest))
        mo.to(4, 4)
        settle(m, card=0)
        fw, fh, fb = m.fbuf(card=0)

    if case == "straddle" and (wx2 >= seam or wx2 + ww <= seam):
        sys.exit("dispblitp: the window ended at x=%d, which does not "
                 "straddle the seam at %d - nothing was under test"
                 % (wx2, seam))
    if case == "direct" and wx2 < seam:
        sys.exit("dispblitp: the window ended at x=%d, which is not clear of "
                 "the seam at %d - this leg has to NOT straddle" % (wx2, seam))
    # **A LEAKED DISPLAY NEST IS WORSE THAN A LOST BLIT**, and it is what the
    # direct move exposed: gfx_blitp cleared [gfx_bp_hk] AFTER six of its own
    # refusals, so a refusal inherited the last call's answer and the teardown
    # brought a nest down that this call never put up. At 255 every primitive
    # afterwards believes it is nested and treats a virtual x as a local one.
    if nest:
        sys.exit("dispblitp: FAILED - [gfx_dnest] is %d after the move, so a "
                 "display nest was left unbalanced" % nest)
    if still:
        sys.exit("dispblitp: FAILED - gfx_blitp was handed a STRADDLING block "
                 "and Paint still holds four planes, so the refusal never "
                 "reached it")

    # ...and the half of the canvas the VGA still shows really is the picture
    cx, cy = wx2 + ix, wy2 + iy
    bad = seen = 0
    for rw in range(ih):
        for c in range(iw):
            vx, vy = cx + c, cy + rw
            if not (D[0][0] <= vx < D[0][0] + D[0][2]):
                continue
            if not (D[0][1] <= vy < D[0][1] + D[0][3]):
                continue
            seen += 1
            got = fb[((vy - D[0][1]) * fw + (vx - D[0][0])) * 3] < 128
            if got != (px[rw * iw + c] == 1):
                bad += 1
    # The MONO half is deliberately not compared: MartyPC's MDA aperture is
    # offset from the guest origin, so a compare in guest coordinates there
    # samples the wrong pixels. The direct move puts the whole canvas on it,
    # so that leg's evidence is [gfx_dnest] and [pt_planar] and nothing else -
    # which is the right evidence anyway, both being the bugs' own signatures.
    print("   %s: the VGA's %d canvas pixels, %d differ from the file"
          % (case, seen, bad))
    if case == "straddle" and not seen:
        sys.exit("dispblitp: none of the straddled canvas is on the VGA, so "
                 "the compare proves nothing")
    if bad:
        sys.exit("dispblitp: FAILED - the canvas came back converted but "
                 "wrong")
    print("   %s: refused, converted, and the picture is still the picture"
          % case)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/dispblitp.img")
    ap.add_argument("--machine", default="os8088_xt_vga_mda")
    ap.add_argument("--gif", default="build/OS8088.GIF")
    ap.add_argument("--case", choices=("straddle", "direct"), default=None)
    a = ap.parse_args()

    if a.apps == "/tmp/dispblitp.img" and not os.path.exists(a.apps):
        subprocess.check_call(
            [sys.executable, "tools/os88disk.py", "-o", a.apps, "--size",
             "360", "APPS:build/paint.o88", "MEDIA:" + a.gif])

    iw, ih, px = gif_pixels(a.gif)
    sym = pkg_syms("apps/paint/paint.asm")
    # TWO BOOTS, because pt_planar is one-way: once a straddle has converted
    # the canvas there is no second refusal to watch, so the direct move needs
    # a Paint that has never crossed a seam.
    for case in ([a.case] if a.case else ["straddle", "direct"]):
        run(a, case, iw, ih, px, sym)
    print("dispblitp: gfx_blitp refuses a seam and a mono display, says so, "
          "and leaves the nest where it found it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
