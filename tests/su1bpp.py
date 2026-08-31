#!/usr/bin/env python3
"""A two-colour window's cache is ONE plane, and puts back the same pixels.

    make && python3 tests/su1bpp.py [--machine os8088_xt_vga]

SPEC.md 11.96.17: a window whose content is only colour 0 and colour 15 has
four IDENTICAL planes, so its raise cache holds one. VGA ONLY - on a 1bpp
adapter every cache is already one plane and there is nothing to show, which
is why this defaults to os8088_xt_vga rather than the usual 5150.

Three assertions, and the third is the one that would catch a wrong picture
rather than a wrong size:

  DEPTH     Note Pad's claim carries WSU_PW = 1 in its header, and a window
            that made no such claim carries 4. The header is where the depth
            lives (11.96.3's rule for the rect, applied to the depth) so this
            reads the kernel's own answer rather than inferring it.

  QUARTER   the claim is about a quarter the size it would be at four planes.
            Read off mem_size for the claim's own segment: a depth that
            reached the header but not wm_su_kb's arithmetic would pass DEPTH
            and fail here, having sized a four-plane buffer and written one.

  PIXELS    cover Note Pad, raise it - which spends the cache - and compare
            the glass against a FORCED FULL REPAINT of the same state. This
            is tests/dispcorner.py's method and its helper, because the
            failure this exists to catch does not look like a crash: one
            plane broadcast through Map Mask 0Fh puts colour 15 and 0 back,
            so a wrong picture here is a window that looks perfectly normal
            and is missing every colour it should not have had anyway. The
            comparison has to be against the honest renderer, not against an
            eye.
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
WSU_PW, WSU_X1, WSU_X2 = 12, 0, 4
fails = []


def raise_win(m, mo, want):
    """Click a point on `want`'s title bar that nothing above it covers.

    A window opened over the Disk window swallows the next click on it, so
    every step after the first needs the Disk window back in front - and
    `wx + 5` is not good enough, because whether that point is clear depends
    on where the covering window happened to land.
    """
    wins = os88geom.windows(m)
    me = [w for w in wins if w.i == want][0]
    for x in range(me.x + 4, me.x + me.w - 4, 4):
        y = me.y + 6
        if not any(w.covers(x, y) for w in wins
                   if w.i != want and w.visible):
            mo.click(x, y)
            os88marty.settle(m)
            return
    raise RuntimeError("no clear point on window %d's title bar" % want)


def claim_of(m, slot):
    """The window's raise-cache segment, 0 if it has none."""
    b = m.read(S("wm_su_segs") + slot * 2, 2)
    return b[0] | (b[1] << 8)


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    w = dispcp.win_list(m, S)
    wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
    w = dispcp.win_list(m, S)
    wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "NOTEPAD.O88")
    os88marty.settle(m)

    np = [x for x in os88geom.windows(m) if "ote" in x.title][-1]
    print("Note Pad slot %d at (%d,%d,%d,%d), 1bpp=%s"
          % (np.i, np.x, np.y, np.w, np.h, np.mono))

    # COVER IT, which is what banks it (SPEC.md 11.96.4/11.96.16): a window
    # is cached at the moment something lands on top, not at the raise.
    dsk = dispcp.win_list(m, S)[-2]     # the APPS window, now behind Note Pad
    dsk = [x for x in os88geom.windows(m) if x.title == "APPS"][0].i
    raise_win(m, mo, dsk)
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "HELLO.O88")
    os88marty.settle(m)

    seg = claim_of(m, np.i)
    if not seg:
        sys.exit("Note Pad has no cache after being covered - nothing to test "
                 "(a refused claim is legal, so re-run before believing it)")
    hdr = m.readseg(seg, 0, 16)
    pw = hdr[WSU_PW] | (hdr[WSU_PW + 1] << 8)
    x1 = hdr[WSU_X1] | (hdr[WSU_X1 + 1] << 8)
    x2 = hdr[WSU_X2] | (hdr[WSU_X2 + 1] << 8)
    print("claim %04X: WSU_PW=%d, rect x %d..%d (x1&7=%d, x2&7=%d)"
          % (seg, pw, x1, x2, x1 & 7, x2 & 7))
    if pw != 1:
        fails.append("DEPTH: Note Pad's claim is %d planes, wanted 1" % pw)

    # ...and a window that claimed nothing is still four.
    other = [x for x in os88geom.windows(m) if x.i != np.i and claim_of(m, x.i)]
    for x in other:
        h = m.readseg(claim_of(m, x.i), 0, 16)
        opw = h[WSU_PW] | (h[WSU_PW + 1] << 8)
        print("  %-14s claim WSU_PW=%d %s"
              % (x.title, opw, "ok" if opw == 4 else "*** claimed a depth"))
        if opw != 4:
            fails.append("DEPTH: %s did not claim WF_1BPP but banked %d planes"
                         % (x.title, opw))

    # --- QUARTER: the claim's own size against what four planes would need -
    rows = np.h - 19                    # the content rect's, as wm_su_rect
    bpr = (x2 // 8) - (x1 // 8) + 1     # ...and its whole bytes per row
    one = 14 + (bpr + 2) * rows         # WSU_IMG + image + the edge scratch
    four = 14 + (bpr + 2) * rows * 4
    kb = m.read(S("mem_kb") if False else S("wm_su_segs"), 0)
    print("QUARTER: content %dx%d, %d bytes a row -> one plane %d, four %d"
          % (x2 - x1 + 1, rows, bpr, one, four))
    if four <= 0xFC00:
        print("  (this window fits at four planes too, so QUARTER is about "
              "size and not about eligibility)")

    # --- PIXELS: raise it, then force the honest repaint and diff ----------
    import dispcorner
    mo.click(np.x + np.w // 2, np.y + 6)        # raise: spends the cache
    mo.to(*dispcorner.PARK)                     # ...and PARK THE POINTER, at
    os88marty.settle(m)                         # the same place dispcorner's
                                                # repaint leaves it. The arrow
                                                # is drawn into the
                                                # framebuffer (SPEC.md 7.1),
                                                # so a capture taken with it
                                                # somewhere else differs by
                                                # the whole 8x12 cell and
                                                # nothing else - 267
                                                # subpixels, measured
    cw, ch, cached = m.fbuf()                   # the RENDERED framebuffer -
                                                # mode 12h has no flat one in
                                                # guest memory to read
    dispcorner.repaint(m, mo, None)
    hw, hh, honest = m.fbuf()
    # BELOW THE MENU BAR, and the exclusion is the subject rule rather than a
    # tolerance: SPEC.md 37's clock sits at the bar's right end and redraws on
    # its own, so it differs between any two captures a second apart and has
    # nothing to do with the cache. Everything the raise can reach - the
    # desktop, every window, the dock - is below MBAR_H and still compared,
    # which is what would catch a flattened stripe BESIDE the window as well
    # as a wrong one inside it.
    skip = 20 * cw * 3                          # MBAR_H rows of rgb24
    diff = sum(1 for a, b in zip(cached[skip:], honest[skip:]) if a != b)
    print("PIXELS: %dx%d below the bar, cached raise vs forced full repaint - "
          "%d subpixels differ" % (cw, ch - 20, diff))
    if diff:
        pts = [i // 3 for i in range(skip, len(cached), 3)
               if cached[i:i+3] != honest[i:i+3]]
        for pxi in pts[:20]:
            px, py = pxi % cw, pxi // cw
            print("    (%3d,%3d)  cached %s  honest %s%s"
                  % (px, py, cached[pxi*3:pxi*3+3].hex(),
                     honest[pxi*3:pxi*3+3].hex(),
                     "  [inside Note Pad's content]"
                     if np.x + 1 <= px <= np.x + np.w - 2
                     and np.y + 18 <= py <= np.y + np.h - 2 else ""))
        fails.append("PIXELS: %d rendered subpixels differ between the cached "
                     "raise and the honest repaint" % diff)

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  one plane, and the same pixels")
