#!/usr/bin/env python3
"""Does a WIDE straddling Note Pad letter its whole row? (SPEC.md 27.2.1)

    make && python3 tests/dispnp.py

NP_MAXCOL was 91 - "720/8 is the widest screen this runs on" - and a window
straddling a display seam is not bounded by one screen: it may be the whole
virtual desktop wide, 1360 px on a Hercules beside a CGA. Past the clamp
np_rflush DROPS the cell, so every row stopped at column 90 and the cells
beyond it were neither written nor ERASED: text, a gap, then whatever those
cells held before.

So the assertion is INK BEYOND COLUMN 90, on the card that holds it. Reported
from the field with a photo of exactly that, and with the tell that makes the
diagnosis: a redraw shows the same gap WITHOUT the fragments, because the
white fill clears the stale pixels and the clamp still stops the text short.

OPEN, AND IT NEVER REACHES THE ASSERTION ABOVE. This row fails at its SETUP:
"it does not straddle the seam". The Note Pad opens at (55,60) 260x180, the
grow drags it to (199,60) and it stays 260 wide - about 32 cells - while the
seam is at x=720, so there is nothing straddling anything to measure.

It is NOT the stride bug that hid it, and it is not this branch. The stride
was wrong here too (VID_CTX_SZ 42 against the kernel's 44, fixed with the
other eight readers), and while it was wrong this row died on garbage geometry
long before getting here - which is why nobody had seen this. With the stride
right, the same failure reproduces at the branch's first commit AND at the
merge-base with `elendilon` using the ORIGINAL constant, where 42 was correct.
So the setup has been broken for longer than the branch and the row has never
tested what its docstring says it tests.

What to look at first: the grow. `grown to (199,60) 260x180` is a MOVE and not
a resize - the width did not change - so either the drag is landing on the
frame rather than the size box, or the size box is not where this script
thinks it is on this layout.
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
from os88geom import (VID_CTX_SZ, VID_CTX_VX,          # noqa: E402
                      VID_CTX_VY, VID_CTX_CW, VID_CTX_CH)
# SPEC.md 39.14's per-display record: DERIVED from VID_CTX_W and never
# written down here. Nine scripts had `VID_CTX_SZ = 42` by hand and the
# record has grown TWICE under them - the last time silently, because the
# constant that moved was a DERIVED one and os88geom's scanner was only
# looking at the mirrored ones. It is looking at both now.

MBAR_H, TITLE_H = 20, 18
NP_OLDMAX = 90                  # the clamp this exists to be past
S = os88sym.linear


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/npdisk.img")
    a = ap.parse_args(argv)

    if a.apps == "build/npdisk.img":
        os88marty.scratch_disk(a.apps, "build/notepad.o88")

    fail = []
    say = lambda s: print("  " + s)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        cards = m.cards()
        if len(cards) != 2:
            sys.exit("dispnp: %s is not a two-card machine" % a.machine)
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_primary(m, mo, S, os88marty.settle, 0)      # HERCULES
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        kind = m.read(S("vid_kind"), 1)[0]
        pri = [c for c in cards if c["type"] == ("mda" if kind == 1
                                                 else "cga")][0]
        sec = [c for c in cards if c is not pri][0]
        skind = "herc" if sec["type"] == "mda" else "cga"
        pkind = "cga" if skind == "herc" else "herc"
        ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
        seam = u16(ctx, VID_CTX_SZ + VID_CTX_VX)
        secy = u16(ctx, VID_CTX_SZ + VID_CTX_VY)
        secw = u16(ctx, VID_CTX_SZ + VID_CTX_CW)
        say("secondary %s at (%d,%d), %d wide" % (sec["type"], seam, secy,
                                                  secw))

        dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri["idx"])
        d = dispcp.win_list(m, S)[-1]
        dx, dy, dw, dh = dispcp.win_rect(m, S, d)
        dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "NOTEPAD.O88",
                          card=pri["idx"])
        w = dispcp.win_list(m, S)
        if w[-1] == d:
            sys.exit("dispnp: NOTEPAD.O88 did not launch from row 0")
        slot = w[-1]
        nx, ny, nw, nh = dispcp.win_rect(m, S, slot)
        say("Note Pad at (%d,%d) %dx%d" % (nx, ny, nw, nh))

        # left, then WIDE: the row has to be more than NP_OLDMAX cells for
        # there to be a case at all
        mo.drag(nx + nw // 2, ny + TITLE_H // 2, 200 + nw // 2,
                ny + TITLE_H // 2)
        os88marty.settle(m, card=pri["idx"])
        nx, ny, nw, nh = dispcp.win_rect(m, S, slot)
        # THE GROW TARGET HAS TO BE A ROW THE SECOND CARD HAS: past the seam
        # the CGA covers virtual secy..secy+ch-1, and mou_clamp refuses to put
        # the pointer below that (SPEC.md 39.19.3). Growing to ny+170 was
        # asking for row 230 of a card whose last is 219.
        mo.drag(nx + nw - 6, ny + nh - 6, seam + 420, secy + 170)
        os88marty.settle(m, card=sec["idx"])
        nx, ny, nw, nh = dispcp.win_rect(m, S, slot)
        cells = (nw - 2) // 8
        say("grown to (%d,%d) %dx%d - about %d cells wide" % (nx, ny, nw, nh,
                                                              cells))
        if not nx < seam < nx + nw - 1:
            sys.exit("dispnp: it does not straddle the seam")
        if cells <= NP_OLDMAX + 8:
            sys.exit("dispnp: only %d cells wide - the old clamp was %d, so "
                     "there is no case in this test" % (cells, NP_OLDMAX))

        # a long unbroken row: no spaces, so nothing can wrap early
        mo.click(nx + 60, ny + TITLE_H + 20)
        for i in range(150):
            m.key("Key" + "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[i % 26])
        os88marty.settle(m, card=sec["idx"])

        # WHERE DOES THE ROW FIRST BREAK, in CELLS, across BOTH cards.
        #
        # Not "is there ink past column 90" (the old build passes that with 42
        # pixels) and not "the last cell carrying ink" (it passes that too,
        # with 113) - because THE STALE FRAGMENTS ARE THEMSELVES INK, and both
        # of those measure the artefact as though it were the fix. What tells
        # them apart is that real text is CONTIGUOUS: 150 characters were
        # typed with no spaces, so every cell is lettered until the row stops.
        # So the honest number is the first UNLETTERED cell, and the two
        # builds are ~90 and the full width.
        pw, ph, prows = m.vram(pkind)
        sw, sh, srows = m.vram(skind)
        lastx = (nw - 2 - 18) // 8              # ...short of the scroll bar
        y0 = ny + TITLE_H + 2

        def lettered(c):
            vx = nx + 1 + c * 8
            if vx < seam:
                rws, w, h, lx, oy = prows, pw, ph, vx, 0
            else:
                rws, w, h, lx, oy = srows, sw, sh, vx - seam, secy
            if lx < 0 or lx + 7 >= w:
                return None                     # not on a card at all
            n = 0
            for y in range(y0 - oy, min(y0 - oy + 12, h)):
                if y < 0:
                    continue
                r = rws[y]
                n += sum(1 for x in range(lx, lx + 8) if not r[x])
            return n >= 4

        brk = None
        for c in range(2, lastx):
            v = lettered(c)
            if v is None:
                break
            if not v:
                brk = c
                break
        say("the row's first UNLETTERED cell: %s (band is %d cells)"
            % (brk, lastx))
        if brk is not None and brk <= NP_OLDMAX + 4:
            fail.append("the row stops at cell %d - NP_MAXCOL is still bounded "
                        "by ONE screen, so the tail is dropped and never "
                        "erased (SPEC.md 27.2.1)" % brk)

    print()
    for f in fail:
        print("dispnp: FAIL: %s" % f)
    if fail:
        return 1
    print("dispnp: a straddling Note Pad letters its whole row - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
