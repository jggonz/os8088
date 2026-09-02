#!/usr/bin/env python3
"""Does a title bar STRADDLING the seam have one polarity? (SPEC.md 5.4.2.4)

    python3 tests/disptitle.py          # it builds the kernel it needs

THIS ROW BUILDS `make BAND=1` ITSELF and puts the default kernel back
afterwards, which tests/gfxlk.py's shape. The composer is a KNOB again since
SPEC.md 5.9.6 and no shipped kernel carries it, so on a default build there is
no band to emit the wrong way up and every assertion below passes by drawing
the fifteen-call bar instead - a green row for a defect it could no longer
see. PERFORMANCE.md Set 89 names that trap in as many words: a correctness
check on a path that has a correct fallback proves nothing on its own.

Reported from the field as *"the window title bar is drawing half inverted
when straddling on an extended desktop"*, off a VGA beside a real Hercules.

SPEC.md 11.101's title bar is composed into a 1bpp BAND and put down with one
`gfx_blit1`, and the band's storage polarity is `band_pen_x`'s answer to a
question about the CURRENT display (SPEC.md 5.9.3). `vid_dual_ok` takes only a
mono card beside a colour one, so on the one machine that can extend a desktop
the two displays never agree about `[vid_mono]` - and the emit resolved its pen
at the door, above the display it was going to. A straddling band goes per
8-pixel column and each column re-enters, so each column was answered off the
display the PREVIOUS one left current: one column right by accident at each
boundary, and the rest of the near half turned over. Which half inverts is the
one the composer did NOT choose the polarity for, and that is the far one -
`wm_title_band` fills the ragged ends before it composes, so the last of them
leaves the far display current.

THE ASSERTION IS THE SAME BAR SOMEWHERE ELSE, which is why this needs no
reference build and no palette table. A window is parked wholly on each display
in turn and the GROUND of its title bar - the commonest pixel over the bar's
interior - is recorded there; then it is dragged to straddle, and each half has
to have the ground that display already showed for it. The question is not how
many pixels are lit but which way up the picture is, and a bar that is white on
one monitor cannot be black on the other.

It is the only test in this tree that can see the defect: two cards of
DIFFERENT DEPTH is `os8088_xt_vga_herc` (docs/DUAL-DISPLAY-VGA.md 7.1), 86Box's
`xt-multimon` is a Hercules beside a CGA and those two agree about depth, and
one card cannot straddle anything.
"""
import argparse
import atexit
import os
import subprocess
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
from os88geom import VID_CTX_SZ, VID_CTX_VX, VID_CTX_CW     # noqa: E402

TITLE_H = 18
S = os88sym.linear


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def vga_surface(m, card):
    """The VGA's rasterised output, one byte per pixel.

    `vram` cannot read a planar card (os88marty says so), and `fbuf` asks the
    card what it actually put on the glass. RGB24 comes back; the red channel
    alone separates every colour this test compares, and it is compared to
    ITSELF on the same card, so no palette table is needed anywhere here.
    """
    w, h, px = m.fbuf(card=card)
    return w, h, bytes(px[i] for i in range(0, len(px), 3))


def mono_surface(m, kind):
    """...and `vram` for the 1bpp card, which is EXACT there where `fbuf`
    comes back cropped to a display aperture (docs/MARTYPC-DEBUG.md)."""
    w, h, rows = m.vram(kind)
    out = bytearray()
    for r in rows:
        out += bytes(r)
    return w, h, bytes(out)


def ground(surf, w, h, x0, x1, y0, y1):
    """The commonest pixel over an inclusive rect - a title bar's GROUND, which
    is the whole of what a polarity question asks. (value, count, total)."""
    c = Counter()
    for y in range(max(y0, 0), min(y1, h - 1) + 1):
        base = y * w
        c.update(surf[base + max(x0, 0):base + min(x1, w - 1) + 1])
    if not c:
        return None, 0, 0
    v, n = c.most_common(1)[0]
    return v, n, sum(c.values())


def cols_unlike(surf, w, h, x0, x1, y0, y1, want):
    """Which COLUMNS of that rect do not have `want` as their commonest pixel.

    An inverted band is a clean run of them and a lost one is not, so this is
    what says which of the two happened - and the first column past a seam
    being right while the rest are wrong is this defect's own signature.
    """
    out = []
    for x in range(max(x0, 0), min(x1, w - 1) + 1):
        col = [surf[y * w + x] for y in range(max(y0, 0), min(y1, h - 1) + 1)]
        if col and Counter(col).most_common(1)[0][0] != want:
            out.append(x)
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga_herc")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--dump", help="write the second card's title strip here")
    ap.add_argument("--define", action="append", default=[],
                    help="a knob the kernel under test was built with, so "
                         "os88sym maps the RIGHT kernel: --define BAND")
    ap.add_argument("--no-build", action="store_true",
                    help="do not `make BAND=1` first - for driving an image "
                         "built by hand, which must still be a BAND=1 one")
    a = ap.parse_args(argv)

    # SPEC.md 5.9.6: the composed bar is `make BAND=1` and nothing else, so
    # this row builds it. A knob kernel is a DIFFERENT kernel and os88sym
    # refuses a map that is not byte-identical to build/kernel.bin, so the
    # define goes in once here rather than at each of the lookups below.
    if not a.no_build:
        subprocess.check_call(["make", "BAND=1"], cwd=ROOT,
                              stdout=subprocess.DEVNULL)
        a.define = list(a.define) + ["BAND"]
        # ...AND THE DEFAULT KERNEL GOES BACK, whichever way this run ends. A
        # knob kernel left in build/ under the shipped names is CLAUDE.md's
        # own trap: the next row resolves symbols against it, os88sym refuses
        # a map that does not match, and the failure points at the kernel
        # rather than at what left it there. atexit rather than a try/finally
        # around the launch below, because half a dozen sys.exit()s inside it
        # are the normal way this row gives up on a machine it cannot drive.
        atexit.register(subprocess.check_call, ["make"], cwd=ROOT,
                        stdout=subprocess.DEVNULL)

    global S
    if a.define:
        defs = tuple(a.define)
        S = lambda n: os88sym.linear(n, defines=defs)       # noqa: E731

    fail = []
    say = lambda s: print("  " + s)                         # noqa: E731
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        cards = m.cards()
        if len(cards) != 2:
            sys.exit("disptitle: %s has %d video card(s), not 2"
                     % (a.machine, len(cards)))
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("disptitle: the Control Panel did not turn Extend on")

        kind = m.read(S("vid_kind"), 1)[0]
        pri = [c for c in cards if c["type"] == dispcp.KIND_CARD[kind]][0]
        sec = [c for c in cards if c is not pri][0]
        if pri["type"] != "vga" or sec["type"] == "vga":
            sys.exit("disptitle: this needs the VGA PRIMARY beside a 1bpp "
                     "secondary - the two displays disagreeing about depth is "
                     "the whole subject, and the 1bpp card is the one that "
                     "cannot be read back through fbuf (got %s + %s)"
                     % (pri["type"], sec["type"]))
        ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
        seam = u16(ctx, VID_CTX_SZ + VID_CTX_VX)
        secw = u16(ctx, VID_CTX_SZ + VID_CTX_CW)
        skind = "herc" if sec["type"] == "mda" else "cga"
        say("primary %s, secondary %s at x=%d, %d px wide"
            % (pri["type"], sec["type"], seam, secw))

        # ONE Disk window, moved by its title bar - the field's own three
        # positions (tests/dispdrag.py 11.96.14 does the same two drags).
        dispcp.open_drive(m, mo, S, os88marty.settle, "A", card=pri["idx"])
        w = dispcp.win_list(m, S)
        if not w:
            sys.exit("disptitle: no Disk window opened")
        win = w[-1]

        def rect():
            return dispcp.win_rect(m, S, win)

        def move(to, card):
            x, y, ww, wh = rect()
            mo.drag(x + ww // 2, y + TITLE_H // 2, to + ww // 2,
                    y + TITLE_H // 2)
            os88marty.settle(m, card=card)
            x, y, ww, wh = rect()
            # ...and the ARROW off the bar before anything is counted: it is
            # ~15 lit pixels sitting exactly where the ground is measured, and
            # a test that cannot tell its subject from its own pointer is
            # PERFORMANCE.md Part 3.1's trap.
            mo.to(x + ww // 2, y + wh + 8)
            os88marty.settle(m, card=card)
            return rect()

        x, y, ww, wh = rect()
        say("Disk window at (%d,%d) %dx%d" % (x, y, ww, wh))
        # The bar's INTERIOR: wm_draw_title fills y+1 .. y+TITLE_H-2 between
        # the frame's two columns, and everything outside that is border.
        rows = lambda yy: (yy + 1, yy + TITLE_H - 2)        # noqa: E731

        # --- 1. wholly on the primary: what its bar looks like THERE --------
        x, y, ww, wh = move((seam - ww - 40) & ~7, pri["idx"])
        if x + ww - 1 >= seam:
            sys.exit("disptitle: the window did not park clear of the seam")
        pw, ph, psurf = vga_surface(m, pri["idx"])
        r0, r1 = rows(y)
        pref, pn, pt = ground(psurf, pw, ph, x + 1, x + ww - 2, r0, r1)
        say("wholly on the primary at x=%d: ground %d (%d of %d px, %.0f%%)"
            % (x, pref, pn, pt, 100.0 * pn / max(pt, 1)))

        # --- 2. wholly on the secondary: and THERE --------------------------
        x, y, ww, wh = move((seam + 24) & ~7, sec["idx"])
        if x < seam:
            sys.exit("disptitle: the window did not reach display 1 (x=%d, "
                     "seam=%d)" % (x, seam))
        sw, sh, ssurf = mono_surface(m, skind)
        r0, r1 = rows(y)
        sref, sn, st = ground(ssurf, sw, sh, x + 1 - seam, x + ww - 2 - seam,
                              r0, r1)
        say("wholly on the secondary at x=%d: ground %d (%d of %d px, %.0f%%)"
            % (x, sref, sn, st, 100.0 * sn / max(st, 1)))

        # --- 3. ...and now HALF ON EACH, which is the report ----------------
        x, y, ww, wh = move((seam - ww // 2) & ~7, sec["idx"])
        if x >= seam or x + ww - 1 < seam:
            sys.exit("disptitle: the window does not straddle the seam (it is "
                     "at %d..%d and the seam is %d) - wm_strad_fit may have "
                     "shrunk it back (SPEC.md 39.16.3)"
                     % (x, x + ww - 1, seam))
        say("straddling: x=%d..%d, %d px of the bar past the seam"
            % (x, x + ww - 1, x + ww - seam))
        r0, r1 = rows(y)
        pw, ph, psurf = vga_surface(m, pri["idx"])
        sw, sh, ssurf = mono_surface(m, skind)
        pv, pn, pt = ground(psurf, pw, ph, x + 1, seam - 1, r0, r1)
        sv, sn, st = ground(ssurf, sw, sh, 0, x + ww - 2 - seam, r0, r1)
        if not pt or not st:
            sys.exit("disptitle: the bar has no columns on one of the two "
                     "displays (%d and %d px)" % (pt, st))
        say("primary   half: ground %d (%d of %d px, %.0f%%), was %d"
            % (pv, pn, pt, 100.0 * pn / pt, pref))
        say("secondary half: ground %d (%d of %d px, %.0f%%), was %d"
            % (sv, sn, st, 100.0 * sn / st, sref))

        if pv != pref:
            fail.append("the PRIMARY's half of the straddling bar has ground "
                        "%d where the same bar wholly on that display has %d "
                        "(SPEC.md 5.4.2.4)" % (pv, pref))
        if sv != sref:
            bad = cols_unlike(ssurf, sw, sh, 0, x + ww - 2 - seam, r0, r1,
                              sref)
            say("secondary columns unlike that ground: %d of %d, x %d..%d "
                "(display-local)"
                % (len(bad), x + ww - 1 - seam, min(bad), max(bad)))
            fail.append("the SECONDARY's half of the straddling bar has "
                        "ground %d where the same bar wholly on that display "
                        "has %d - the band was emitted the wrong way up on "
                        "the far display (SPEC.md 5.4.2.4)" % (sv, sref))

        if a.dump:
            strip = bytearray()
            for yy in range(max(r0, 0), min(r1, sh - 1) + 1):
                strip += ssurf[yy * sw:(yy + 1) * sw]
            with open(a.dump, "wb") as f:
                f.write(bytes(strip))
            say("second card's title strip written to %s" % a.dump)

    print()
    for f in fail:
        print("disptitle: FAIL: %s" % f)
    if fail:
        return 1
    print("disptitle: a title bar across the seam is one way up on both "
          "displays - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
