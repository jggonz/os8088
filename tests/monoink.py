#!/usr/bin/env python3
"""The Browser on a VGA: two colours, still legible, and a cache that FITS.

    make && python3 tests/monoink.py [--machine os8088_xt_vga]

SPEC.md 11.96.17's one-plane cache needs a window whose content is only
colour 0 and colour 15, and the Browser's default VGA content is about 92KB
at four planes against wm_su_kb's 64,512 ceiling - so it is the window the
feature exists for and the one that could not make the claim. What stopped it
was SPEC.md 47 rule 1: os88ui_btn's disabled pen is CDGRAY, which is colour 8,
and it reached both the button's FRAME and its CAPTION.

Three assertions, and the middle one is the reason this took three goes:

  TWO       every pixel of the Browser's content is colour 0 or colour 15.

  LEGIBLE   ...and a greyed control still SAYS SO. Rounding the pen to black
            is easy and makes a dead button pixel-identical to a live one,
            which is 47 rule 1's own failure with a new way in - it was tried,
            measured and withdrawn once already. What carries the signal now
            is the checkerboard, so this asserts the SHAPE of the ink rather
            than its colour: every lit pixel of a greyed caption falls on ONE
            parity of (x + y), which is what a 0AAh/055h mask means and what
            a solid glyph can never be. Reload is live and is the control -
            its ink must use BOTH parities.

  FITS      the cache the kernel actually took is one plane deep, and the
            four-plane figure it replaces is past the ceiling. Read out of
            the claim's own header, not computed here.

VGA only. On a 1bpp adapter every cache is one plane already and every greyed
caption is a checkerboard already, so there is nothing to show.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88geom, os88sym, dispcp

MACHINE = "os8088_xt_vga"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

S = os88sym.linear
BLACK, WHITE = b"\x00\x00\x00", b"\xff\xff\xff"
fails = []


def px(fb, w, x, y):
    return fb[(y * w + x) * 3:(y * w + x) * 3 + 3]


def buttons(fb, w, x0, x1, y):
    """The toolbar's buttons, as x ranges, from the black run of each FRAME top.

    Grouping the caption's own ink instead finds eleven things, because the
    buttons' vertical frame edges pass through the caption's rows and each is
    a group of one solid column. The frame is what delimits a button, so the
    frame is what to look for.
    """
    out, run = [], None
    for x in range(x0, x1):
        if px(fb, w, x, y) == BLACK:
            run = (run[0], x) if run else (x, x)
        elif run:
            if run[1] - run[0] > 12:
                out.append(run)
            run = None
    if run and run[1] - run[0] > 12:
        out.append(run)
    return out


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    w = dispcp.win_list(m, S)
    wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
    w = dispcp.win_list(m, S)
    wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "BROWSER.O88")
    os88marty.settle(m)

    br = [x for x in os88geom.windows(m) if "rowser" in x.title]
    if not br:
        sys.exit("the Browser did not open - windows: %r" % os88geom.windows(m))
    br = br[-1]
    if not br.mono:
        sys.exit("the Browser does not declare WF_1BPP - nothing to test")
    mo.to(4, 24)                            # the arrow is drawn INTO the
    os88marty.settle(m)                     # framebuffer (SPEC.md 7.1)
    fw, fh, fb = m.fbuf()
    cx1, cy1, cx2, cy2 = br.content

    # --- TWO ---------------------------------------------------------------
    odd = {}
    for y in range(cy1, cy2 + 1):
        for x in range(cx1, cx2 + 1):
            p = px(fb, fw, x, y)
            if p not in (BLACK, WHITE):
                odd.setdefault(p.hex(), 0)
                odd[p.hex()] += 1
    print("TWO    : content %s - %d pixels that are neither 0 nor 15 %s"
          % ((cx1, cy1, cx2, cy2), sum(odd.values()), sorted(odd.items())))
    if odd:
        fails.append("TWO: %d content pixels are neither colour 0 nor colour "
                     "15 - a one-plane cache would lose them" % sum(odd.values()))

    # --- LEGIBLE -----------------------------------------------------------
    band = (cy1 + 5, cy1 + 13)              # the toolbar captions' rows
    btns = buttons(fb, fw, cx1, cx2, cy1 + 3)
    print("LEGIBLE: %d buttons at %s" % (len(btns), btns))
    if len(btns) < 3:
        fails.append("LEGIBLE: found %d buttons, wanted Back / Fwd / Reload "
                     "- the toolbar's row or layout moved" % len(btns))
    for n, g in enumerate(btns[:3]):
        par = set()
        for x in range(g[0] + 2, g[1] - 1):     # INSIDE the frame: its own
            for y in range(*band):              # columns are solid black and
                if px(fb, fw, x, y) != WHITE:   # belong to no caption
                    par.add((x + y) & 1)
        one = len(par) == 1
        want = one if n < 2 else not one    # Back, Fwd greyed; Reload live
        print("    caption %d  x %d..%d  parities %s  %s"
              % (n, g[0], g[1], sorted(par),
                 "checkerboard" if one else "solid"))
        if not want:
            fails.append("LEGIBLE: caption %d is %s and should be %s - a "
                         "greyed control must still say so (SPEC.md 47 rule 1)"
                         % (n, "a checkerboard" if one else "solid",
                            "a checkerboard" if n < 2 else "solid"))

    # --- FITS --------------------------------------------------------------
    dispcp.open_drive(m, mo, S, os88marty.settle, "A")   # a NEW window lands
    os88marty.settle(m)                                   # on it: wm_su_precover
    b = m.read(S("wm_su_segs") + br.i * 2, 2)
    seg = b[0] | (b[1] << 8)
    if not seg:
        fails.append("FITS: the Browser was covered and got no cache at all")
    else:
        h = m.readseg(seg, 0, 16)
        pw = h[12] | (h[13] << 8)
        x1 = h[0] | (h[1] << 8)
        x2 = h[4] | (h[5] << 8)
        y1 = h[2] | (h[3] << 8)
        y2 = h[6] | (h[7] << 8)
        bpr = (x2 // 8) - (x1 // 8) + 1
        rows = y2 - y1 + 1
        one, four = 14 + (bpr + 2) * rows, 14 + (bpr + 2) * rows * 4
        print("FITS   : claim %04X WSU_PW=%d - %d bytes at one plane, %d at "
              "four, ceiling 64512" % (seg, pw, one, four))
        if pw != 1:
            fails.append("FITS: the claim is %d planes, wanted 1" % pw)
        if four <= 0xFC00:
            fails.append("FITS: four planes would be %d, which FITS - this "
                         "window is no longer the case the feature is for"
                         % four)

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  two colours, still legible, and a cache four planes could not fund")
