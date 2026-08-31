#!/usr/bin/env python3
"""TeXPad: the biggest window in the tree keeps its pixels (SPEC.md 11.96.17)

    make && python3 tests/tpsaveu.py [--machine os8088_5150_cga_gla]

628x400 - source text beside a typeset preview - is the largest and densest
window os8088 has, and its repaint is more lettering than any other. It is
also the easiest promise in the tree to make: TeXPad has NO WORKER at all
(its one asynchronous thing is an OSAPI_WM_ONWAKE that typesets the launch
document once), so SPEC.md 11.96.1's first question passes by construction -
"an app with no worker at all cannot fail this, because the window ABI has no
periodic hook".

And it is the window 11.96.17's one-plane cache exists for more than the
Browser was: at four planes its VGA content is 121,934 bytes against
wm_su_kb's 64,512 ceiling, which is a refusal by nearly double.

  DECLARED  it carries WF_SAVEU and WF_1BPP.
  TWO       ...and that second claim is TRUE: every pixel of its content is
            colour 0 or colour 15. Checked rather than trusted, because a
            declaration that is wrong does not crash - it silently loses the
            colour on the next raise.
  FITS      covered, it banks ONE plane, and the four-plane figure that
            replaces is past the ceiling.
  PIXELS    raised again, the glass agrees with a forced full repaint.

THE COVER IS THE CONTROL PANEL, off the chip menu, and that is not
arbitrary: TeXPad covers the whole desktop INCLUDING the drive icons, so a
double-click meant for one lands on TeXPad instead and nothing ever covers
it - on which every assertion here passes vacuously. The menu bar is the one
thing it never covers.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88geom, os88sym, dispcp, dispcorner

MACHINE = "os8088_xt_vga"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

S = os88sym.linear
BLACK, WHITE = b"\x00\x00\x00", b"\xff\xff\xff"
fails = []

with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    w = dispcp.win_list(m, S)
    wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
    w = dispcp.win_list(m, S)
    wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "TEXPAD.O88")
    os88marty.settle(m)

    tp = [x for x in os88geom.windows(m) if "eX" in x.title]
    if not tp:
        sys.exit("TeXPad did not open - windows: %r" % os88geom.windows(m))
    tp = tp[-1]
    print("DECLARED: %s (%d,%d) %dx%d  saveu=%s 1bpp=%s"
          % (tp.title, tp.x, tp.y, tp.w, tp.h, tp.promises, tp.mono))
    if not tp.promises:
        fails.append("DECLARED: TeXPad does not carry WF_SAVEU")
    if not tp.mono:
        fails.append("DECLARED: TeXPad does not carry WF_1BPP")

    # --- TWO ---------------------------------------------------------------
    mo.to(4, 8)                             # the arrow is drawn INTO the
    os88marty.settle(m)                     # framebuffer (SPEC.md 7.1)
    fw, fh, fb = m.fbuf()
    x1, y1, x2, y2 = tp.content
    odd = {}
    for y in range(y1, min(y2, fh - 1) + 1):
        for x in range(x1, min(x2, fw - 1) + 1):
            p = fb[(y * fw + x) * 3:(y * fw + x) * 3 + 3]
            if p not in (BLACK, WHITE):
                odd[p.hex()] = odd.get(p.hex(), 0) + 1
    print("TWO     : content %s - %d pixels neither 0 nor 15 %s"
          % ((x1, y1, x2, y2), sum(odd.values()), sorted(odd.items())[:3]))
    if odd:
        fails.append("TWO: %d content pixels are neither colour 0 nor 15 - "
                     "the WF_1BPP claim is not true" % sum(odd.values()))

    # --- FITS --------------------------------------------------------------
    mo.menu(8, 8, 8, 40)                    # the chip menu -> Control Panel
    os88marty.settle(m)
    b = m.read(S("wm_su_segs") + tp.i * 2, 2)
    seg = b[0] | (b[1] << 8)
    if not seg:
        fails.append("FITS: TeXPad was covered and got no cache at all")
    else:
        h = m.readseg(seg, 0, 16)
        pw = h[12] | (h[13] << 8)
        hx1, hy1 = h[0] | (h[1] << 8), h[2] | (h[3] << 8)
        hx2, hy2 = h[4] | (h[5] << 8), h[6] | (h[7] << 8)
        bpr = (hx2 // 8) - (hx1 // 8) + 1
        rows = hy2 - hy1 + 1
        one, four = 14 + (bpr + 2) * rows, 14 + (bpr + 2) * rows * 4
        print("FITS    : claim %04X WSU_PW=%d - %d bytes at one plane, %d at "
              "four (ceiling 64512)" % (seg, pw, one, four))
        if pw != 1:
            fails.append("FITS: the claim is %d planes, wanted 1" % pw)
        if MACHINE.endswith("vga") and four <= 0xFC00:
            fails.append("FITS: four planes would be %d, which FITS - TeXPad "
                         "is no longer the case this is for" % four)

    # --- PIXELS ------------------------------------------------------------
    mo.click(tp.x + 30, tp.y + 9)           # its title bar, clear of the
    mo.to(*dispcorner.PARK)                 # Control Panel below - the raise
    os88marty.settle(m)                     # SPENDS the cache
    cw, ch, cached = m.fbuf()
    dispcorner.repaint(m, mo, None)
    _, _, honest = m.fbuf()
    skip = 20 * cw * 3                      # below the menu bar: SPEC.md 37's
    diff = sum(1 for a, b in zip(cached[skip:], honest[skip:]) if a != b)
    print("PIXELS  : %dx%d below the bar - %d subpixels differ" % (cw, ch, diff))
    if diff:
        fails.append("PIXELS: %d subpixels differ between the cached raise and "
                     "the honest repaint" % diff)

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  the largest window in the tree keeps its pixels, one plane deep")
