#!/usr/bin/env python3
"""Does TeXPad's INCREMENTAL source redraw draw what a full repaint draws?
(SPEC.md 69.8)

    make && python3 tests/tpdraw.py

The source pane stopped repainting itself whole: a caret move is two XORs, a
typed character re-letters one line from the column that changed, a Return or
a joining Backspace moves the rows under it with one `OSAPI_GFX_SCROLL` and
letters the row the blit vacated, and a scroll blits the pane and letters the
band that came in. Every one of those is a second opinion about what is on the
glass, and a second opinion is a thing that can be WRONG - by four pixel
columns, by one row, by the characters just typed.

So each leg here edits or scrolls by the incremental path, then makes the
GUEST do a `wm_paint_all` (tests/dispcalc.py's `[cp_dirty]` poke) and compares
the two frames. **0 differing pixels or the leg fails**, and the failure
prints the bands it found, because "50 pixels at x 12..15 down four rows" and
"111 pixels across five cells of one row" are two different bugs and neither
is legible as a bounding box.

IT WAS A MEASUREMENT BEFORE IT WAS A GATE. Written against the incremental
redraw as contributed, it found four defects in one run, each invisible in a
screenshot of a still machine:

  - the characters just typed were not drawn (the tail started at the caret,
    which an insert has already moved past what it inserted)
  - a Return left four stale pixel columns down the whole pane (the blit
    rounded x1 up, and the text pen is not on a byte boundary)
  - the status strip's fill erased the GROW BOX and nothing put it back
  - typing over a multi-line selection redrew one line, leaving every row the
    deletion had moved

The document is `MEDIA/PAPER.TEX`, opened by DOUBLE-CLICK rather than through
APPS/: it is longer than the pane, which is what makes a scroll bar have a
thumb and a PageDown have somewhere to go, and the double-click is also the
association path (SPEC.md 69.6) - so the deferred load that lands on the first
paint is under test here too, for free.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = os88sym.linear
MACHINE = "os8088_xt_vga"
TITLE_H = 18
TP_BAR_H = 22                   # apps/texpad's own three
TP_STAT_H = 14
TP_SPLIT0 = 200
MBAR_H = 20

fails = []
shots = None


def frame(m):
    """The screen, as one byte a pixel. VGA is not vram()-able (mode 12h is
    four planes behind the Graphics Controller), so this asks the CARD what it
    rasterised - which is the stronger assertion anyway."""
    w, h, px = m.fbuf()
    return w, h, bytes(px[i] for i in range(0, len(px), 3))


def shot(m, name):
    if not shots:
        return
    w, h, d = m.fbuf()
    p = os.path.join(shots, name)
    os88marty.write_png_rgb(p, w, h, d)
    print("      wrote %s" % p)


def shot_gray(name, w, h, g):
    """One of the two frames a failing leg compared, from the one-byte-a-pixel
    form the comparison itself uses - so what is written out is what was
    diffed rather than a third capture of a machine that has moved on."""
    if not shots:
        return
    p = os.path.join(shots, name)
    os88marty.write_png_rgb(p, w, h, bytes(b for v in g for b in (v, v, v)))
    print("      wrote %s" % p)


def full_repaint(m):
    """tests/dispcalc.py's: poke `[cp_dirty]` and let `ui_task` do the
    `wm_paint_all` itself, on its own stack and under the kernel's own lock.
    Parking the CPU on a stub instead corrupts the guest - dispcalc.py's
    comment is the long version."""
    m.cmd(cmd="run")
    m.write(S("cp_dirty"), b"\x01")
    for _ in range(200):
        time.sleep(0.05)
        if m.read(S("cp_dirty"), 1)[0] == 0:
            break
    else:
        raise RuntimeError("ui_task never drained [cp_dirty]")
    os88marty.settle(m)
    m.cmd(cmd="pause")


def agrees(m, mo, what):
    """0 = the incremental drawing and a full repaint are the same pixels."""
    px, py = mo.where()[:2]
    m.cmd(cmd="pause")
    w, h, before = frame(m)
    full_repaint(m)
    _, _, after = frame(m)
    d = [i for i in range(len(before)) if before[i] != after[i]]
    # The CLOCK is excluded and it is the harness's own doing: the forced
    # repaint takes wall time, so a run straddles the minute edge sooner or
    # later and two glyph cells at the top right differ for a reason no
    # window can reach (dispcalc.py measured the same 42 pixels).
    clk = int.from_bytes(m.read(S("vid_clk_hx"), 2), "little")
    d = [i for i in d if not (i // w < MBAR_H and i % w >= clk)]
    # ...and the ARROW's own cell, which is recomposed over whatever the
    # repaint drew under it.
    d = [i for i in d
         if not (px - 1 <= i % w <= px + 8 and py - 1 <= i // w <= py + 12)]
    print("   %-32s %d differing pixels" % (what + ":", len(d)))
    if d:
        rows, bands = {}, []
        for i in d:
            rows.setdefault(i // w, []).append(i % w)
        band = None
        for y in sorted(rows):
            if band and y == band[1] + 1:
                band[1] = y
                band[2] = min(band[2], min(rows[y]))
                band[3] = max(band[3], max(rows[y]))
                band[4] += len(rows[y])
            else:
                band = [y, y, min(rows[y]), max(rows[y]), len(rows[y])]
                bands.append(band)
        for b in bands:
            print("      band y %d..%d  x %d..%d  %d px"
                  % (b[0], b[1], b[2], b[3], b[4]))
        tag = what.replace(" ", "-")
        shot_gray("tpdraw-%s-incremental.png" % tag, w, h, before)
        shot_gray("tpdraw-%s-repainted.png" % tag, w, h, after)
        fails.append("%s: %d pixels a full repaint disagrees with"
                     % (what, len(d)))
    m.cmd(cmd="run")
    os88marty.settle(m)
    return len(d)


def main(argv):
    global shots
    ap = argparse.ArgumentParser()
    ap.add_argument("--shots", metavar="DIR",
                    help="write a PNG per failing leg here")
    a = ap.parse_args(argv[1:])
    shots = a.shots
    if shots:
        os.makedirs(shots, exist_ok=True)

    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=os.path.join(ROOT, "build/apps360.img"),
                          machine=MACHINE) as m:
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "MEDIA")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "PAPER.TEX")
        os88marty.settle(m)
        tx, ty, tw, th = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        print("   TeXPad window %d,%d %dx%d" % (tx, ty, tw, th))
        if tw < 400:
            raise RuntimeError("that is not TeXPad's window - the "
                               "double-click on PAPER.TEX did not launch it")
        shot(m, "tpdraw-open.png")

        cx0, cy0 = tx + 1, ty + TITLE_H + 1     # the content origin

        def row(r):
            return cy0 + TP_BAR_H + 4 + r * 8 + 4

        def col(c):
            return cx0 + 4 + c * 8

        # THE FIRST PAINT ALREADY DID THE LOAD (SPEC.md 69.6): a window
        # showing an empty document here is that regression, and every leg
        # below would still pass on it.
        agrees(m, mo, "opened on a .TEX")
        w, _, px = frame(m)
        ink = sum(1 for i, b in enumerate(px)
                  if b < 128 and cx0 < i % w < cx0 + TP_SPLIT0
                  and cy0 + TP_BAR_H < i // w < ty + th - TP_STAT_H)
        print("   source pane: %d lit pixels" % ink)
        if ink < 2000:
            fails.append("the source pane is empty: PAPER.TEX did not load")

        mo.click(col(10), row(6))
        os88marty.settle(m)
        agrees(m, mo, "click into the source")

        m.type_text("HELLO")
        os88marty.settle(m)
        agrees(m, mo, "type five characters")

        m.key("Tab")
        os88marty.settle(m)
        agrees(m, mo, "Tab")

        m.key("Backspace")
        m.key("Backspace")
        os88marty.settle(m)
        agrees(m, mo, "Backspace inside a line")

        m.key("Delete")
        os88marty.settle(m)
        agrees(m, mo, "Delete inside a line")

        m.key("Enter")
        os88marty.settle(m)
        agrees(m, mo, "Return mid-document")

        m.key("Backspace")
        os88marty.settle(m)
        agrees(m, mo, "Backspace joins two lines")

        for k in ("ArrowDown", "ArrowDown", "ArrowUp", "ArrowRight",
                  "ArrowLeft", "Home", "End"):
            m.key(k)
        os88marty.settle(m)
        agrees(m, mo, "caret keys")

        m.key("PageDown")
        os88marty.settle(m)
        agrees(m, mo, "PageDown")
        m.key("PageUp")
        os88marty.settle(m)
        agrees(m, mo, "PageUp")
        for _ in range(6):
            m.key("ArrowDown")
        os88marty.settle(m)
        agrees(m, mo, "ArrowDown into a scroll")

        # the source bar's two arrow cells (SPEC.md 13.10's shared element),
        # and the preview's, which is against the window's right edge
        sb_x = cx0 + TP_SPLIT0 - 7
        sb_p = tx + tw - 2 - 14 // 2 - 2
        mo.click(sb_x, cy0 + TP_BAR_H + 6)
        os88marty.settle(m)
        agrees(m, mo, "scroll bar up arrow")
        mo.click(sb_x, ty + th - TP_STAT_H - TP_BAR_H)
        os88marty.settle(m)
        agrees(m, mo, "scroll bar down arrow")

        # a bar button, which acts on the RELEASE (SPEC.md 13.7)
        mo.click(cx0 + 40, cy0 + 10)
        os88marty.settle(m)
        agrees(m, mo, "bar button on the release")

        # --- the PREVIEW pane: its scroll is a blit and a band too ---------
        mo.click(sb_p, cy0 + TP_BAR_H + 6)          # its up arrow
        os88marty.settle(m)
        agrees(m, mo, "preview bar up arrow")
        mo.click(sb_p, ty + th - TP_STAT_H - TP_BAR_H)
        os88marty.settle(m)
        agrees(m, mo, "preview bar down arrow")
        mo.click(sb_p, ty + th - TP_STAT_H - TP_BAR_H)
        os88marty.settle(m)
        agrees(m, mo, "preview bar down again")
        mo.click(sb_p, (cy0 + TP_BAR_H + ty + th) // 2)   # the track: a page
        os88marty.settle(m)
        agrees(m, mo, "preview bar track")
        m.key("PageDown")                            # the preview has focus
        os88marty.settle(m)
        agrees(m, mo, "preview PageDown")
        m.key("PageUp")
        os88marty.settle(m)
        agrees(m, mo, "preview PageUp")
        shot(m, "tpdraw-preview.png")

        # a dragged selection, then a character typed over it: the deletion
        # moves every row below it, so this is the leg that catches an
        # incremental path claiming an edit it cannot describe
        mo.drag(col(2), row(3), col(20), row(5))
        os88marty.settle(m)
        shot(m, "tpdraw-selection.png")
        m.type_text("X")
        os88marty.settle(m)
        agrees(m, mo, "type over a selection")

    print()
    for f in fails:
        print("FAIL", f)
    if fails:
        return 1
    print("tpdraw: every leg agrees with a full repaint")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
