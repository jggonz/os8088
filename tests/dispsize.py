#!/usr/bin/env python3
"""What happens to a window's SIZE when it is dragged between two displays?
(docs/FIELD-NOTES.md 26, docs/plans/completed/WINDOW-SIZING-PLAN.md)

    make && python3 tests/dispsize.py

Two measurements on the field machine's own pair of cards - a 720x348 Hercules
beside a 640x200 CGA, Hercules primary, Extend / Right:

  A  a RESIZABLE window (the Disk window) walked across the seam and back. The
     straddle rule (SPEC.md 39.16.3) gives back the rows only one display has,
     which is right; the question this asks is whether it gives them BACK when
     the window comes home. It does not, because ui_drag banks AFTER
     wm_strad_fit and so remembers the cut - and the natural bank
     (SPEC.md 39.11.2.1) is the only thing that could restore it.

  B  a FIXED-SIZE window that PUBLISHES NOTHING (Minesweeper) dropped clear
     across in ONE motion. It must come back the SAME SIZE, and this leg is
     the one whose verdict the field reversed: for one round it asserted the
     opposite, because the rows below the CGA look like a hole in the desktop
     and are not - there is no display there at all, exactly as there is none
     below the Hercules, and a window is allowed to hang off the bottom of
     either (SPEC.md 39.16.3.2). wm_strad_fit answers .none when the frame does
     not REACH the other display, which is what makes that true.

     IT USED TO BE SOLITAIRE, and that stopped working when Solitaire started
     publishing a size per adapter: a window that declares one is SUPPOSED to
     change, so the leg was asserting the opposite of the feature. The property
     under test needs a window in 12.5's THIRD row - not sizable, nothing
     published - and Minesweeper is one.

  D  a window dropped so low on the short display that what is left below the
     pointer is less than it wants. It takes the size the adapter calls for and
     the ORIGIN then follows it up until the frame ends on the last row that
     display has (SPEC.md 39.16.3.3) - because
     a user cannot see a window's floor while dragging it, so a placement that
     floors out crossing a seam was never an informed one. Dropped low on the
     display it is ALREADY on, the same window hangs, and that is 39.16.3.2.

  C  a package that DECLARES a size per adapter (SPEC.md 11.100.1) dragged
     wholly onto the other card. It must take the size it published for that
     adapter, take its old one back on the way home - and the CAPTURE is the
     half that matters, because the frame is not the pixels (SPEC.md
     39.16.3.1): a window can be handed a smaller box and go on drawing the
     larger face into it, which reads as correct in the record and as residue
     on the glass. apps/tracker is the consumer (SPEC.md 45.21.1), its two
     faces being the full and the compact windowed layout. It was ModPlug's
     (SPEC.md 56.4) until ModPlug was RETIRED (SPEC.md 56.15).

     THE CAPTURE IS THE ONE PART OF THIS FILE THAT IS NOT ARITHMETIC, and it
     was getting all three of a capture's questions wrong at once - see
     cardof() for the card, and the block itself for the tuple, the width and
     the pointer. What it reported was the menu bar's CLOCK on the card the
     window is NOT on, once a guest minute. steady() is the one thing it needs
     that the rest of the tree's capture gates do not: Tracker is not an inert
     subject and cannot be swapped for one, so the capture is taken with the
     drawing lock FREE rather than whenever the host asked.

  E  the facts a package takes from the ADAPTER, checked after a DRAG rather
     than after an Activate Mode. OSAPI_VIDEO answers about the primary
     (SPEC.md 39.2.1) and is right to, so an 11.98 handler that re-reads it is
     re-reading a screen its window may not be on any more - which is silent,
     and which only a drag can produce. apps/paint is the consumer and
     OSAPI_WM_DISPLAY (SPEC.md 39.16.4) is the answer.

**IT WAS A MEASUREMENT BEFORE THE FIX AND IS A GATE NOW.** A and B both read
as defects when this file was written, so it printed what it found and exited
0; `--measure` is still that mode and is worth keeping for reading the numbers
out. The header of docs/FIELD-NOTES.md 26 is what it used to print - and B is
why that note now carries a NOT A BUG beside its FIXED.

THE ASSERTION IS ARITHMETIC AND NOT A SCREENSHOT, for tests/dispstrad.py's
reason: the dead zone is where nothing is drawn, so a capture of a broken build
and a fixed one differ only in the part of the window that was never on either
monitor. It reads the window RECORD and the natural bank beside it.
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
import dispcorner                                           # noqa: E402
import dispapps                                             # noqa: E402
import os88geom                                             # noqa: E402
from os88geom import (VID_CTX_SZ, VID_CTX_VX,          # noqa: E402
                      VID_CTX_VY, VID_CTX_CW, VID_CTX_CH,
                      VID_CTX_KIND)
# SPEC.md 39.14's per-display record: DERIVED from VID_CTX_W and never
# written down here. Nine scripts had `VID_CTX_SZ = 42` by hand and the
# record has grown TWICE under them - the last time silently, because the
# constant that moved was a DERIVED one and os88geom's scanner was only
# looking at the mirrored ones. It is looking at both now.

TITLE_H, MBAR_H = 18, 20
FM_MIN_H = 92                               # kernel/files.inc, SPEC.md 11.100.2
NR_SIZE = 8                                 # the natural bank's stride (wm.inc)
PARK = (4, MBAR_H + 4)                      # dispcorner's own parking space
S = os88sym.linear


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def ctx(m, d):
    return m.read(S("vid_ctx") + d * VID_CTX_SZ, VID_CTX_SZ)


def bank(m, slot):
    """The rect this window goes back to when the screen changes (39.11.2.1)."""
    b = m.read(os88sym.wfield(slot, "W_NATR"), NR_SIZE)
    return u16(b, 0), u16(b, 2), u16(b, 4), u16(b, 6)


def cardof(m, cards, c):
    """The EMULATOR CARD showing this kernel DISPLAY - never the same index.

    A `vid_ctx` record is the kernel's display, numbered by where it sits on
    the virtual desktop; `card=` is MartyPC's slot, numbered by
    `[[machine.video]]` order. On os8088_5150_both_gla the CGA is listed first
    and the Hercules second, and this file's layout makes the HERCULES display
    0 - so the two numberings are exactly INVERTED here, and every `card=1` in
    this file meaning "the CGA" was watching the Hercules.

    Legs A, B, D, E and F survived that because they assert ARITHMETIC (the
    file's own docstring says so); leg C is the one that captures pixels, and
    it compared a card the window is not on. What it caught there was the menu
    bar's CLOCK - SPEC.md 12.1, in [vid_clk_hx], which is (720-206) & ~7 = 512
    on this display. Decoded at the capture's real width of 720 the pixels it
    reported are x=680..694, y=8..14: the minutes field. menu.inc's own note
    says the string changes once a MINUTE, and a settle is long enough on the
    guest's clock to straddle that - MEASURED at 3 of 8 rounds, which is why
    this arrived as an intermittent rather than as a row that simply failed.

    Resolved off the DISPLAY's own kind rather than [vid_kind], which answers
    about the boot adapter: this machine comes up as a CGA (the 5150's display
    switches), so reading it after an Extend / primary 0 names the wrong card
    again.
    """
    want = dispcp.KIND_CARD[c[VID_CTX_KIND]]
    got = [i for i, k in sorted(cards.items()) if k["type"] == want]
    if not got:
        sys.exit("dispsize: no %s card for a display of kind %d - the machine "
                 "does not have the adapter the kernel says it is driving"
                 % (want, c[VID_CTX_KIND]))
    return got[0]


def steady(m, card, tries=60):
    """A capture taken with the DRAWING LOCK FREE, which is the only kind
    worth diffing when the subject is not inert.

    WRITTEN FOR MODPLUG, which was leg C's subject until it was RETIRED
    (SPEC.md 56.15); Tracker is now, and the account below is ModPlug's. The
    lock-free capture is kept because it is exact whatever the subject draws.

    ModPlug cannot be swapped for an inert subject the way dispcorner swapped
    in the Calculator - it IS the package that declares a size per adapter
    (SPEC.md 56.4), which is the whole of what leg C is about. Its worker
    invalidates the visualiser pane every frame whether or not anything is
    playing (SPEC.md 56.7: "stopped, it animates like anywhere else"), so
    `mppu_well` re-bevels and re-fills that pane ~18 times a second, and a
    plain capture can be taken between the two bevel runs that meet at a
    corner. Measured on os8088_5150_both_gla with nothing loaded: two pixels,
    the well's (zx2,zy1) and (zx1,zy2), and a plain capture catches them
    MID-DRAW about once in fifteen - which is a false FAIL for this leg once
    in twenty-five runs.

    [gfx_lock_flag] is 0 exactly when no task is inside a drawing primitive
    (kernel/vga12.inc: "strictly 0 or 1 and never a count"), which is SPEC.md
    7.4.2.1's own argument - a free lock PROVES it - and ModPlug's frame holds
    it across the whole of `mppu_frame`. So this is EXACT rather than a
    classification, and the leg can then ask for zero differing pixels and
    mean it. Measured against the plain capture it replaces: 0 of 30 against
    2 of 30, for 0.77 retries a capture.

    dispcorner.shot() has the same exposure and is deliberately left alone:
    its own subject is chosen to be inert, so it has nothing to be exposed to.
    """
    for _ in range(tries):
        m.pause()
        if m.read(S("gfx_lock_flag"), 1)[0] == 0:
            out = m.fbuf(card=card)
            m.run()
            return out
        m.run()                     # ...somebody is mid-primitive. Let them
        m.advance(frames=1)         # finish rather than photograph them.
    sys.exit("dispsize: the drawing lock was held for %d tries - something is "
             "painting without end, and no capture of this machine says "
             "anything" % tries)


def extend(m, mo, settle):
    """Hercules primary, extended to the right - the field machine's layout."""
    dispcp.open_panel(m, mo, S, settle)
    dispcp.set_primary(m, mo, S, settle, 0)
    dispcp.set_mode(m, mo, S, settle, "right")
    dispcp.close_panel(m, mo, S, settle)
    if m.read(S("vid_ndisp"), 1)[0] != 2:
        sys.exit("dispsize: the Control Panel did not turn Extend on")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--measure", action="store_true",
                    help="print the numbers and exit 0 rather than asserting")
    a = ap.parse_args(argv)
    a.gate = not a.measure

    fail = []
    say = lambda s: print("  " + s)
    settle = os88marty.settle
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        if len(m.cards()) != 2:
            sys.exit("dispsize: %s is not a two-card machine" % a.machine)
        m.run()
        settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        extend(m, mo, settle)

        c0, c1 = ctx(m, 0), ctx(m, 1)
        seam = u16(c1, VID_CTX_VX)
        bot1 = u16(c1, VID_CTX_VY) + u16(c1, VID_CTX_CH)
        if u16(c0, VID_CTX_CH) <= u16(c1, VID_CTX_CH):
            sys.exit("dispsize: display 1 is not the SHORTER one, so neither "
                     "measurement has anything to say")
        say("display 0 %dx%d at (%d,%d), display 1 %dx%d at (%d,%d)"
            % (u16(c0, VID_CTX_CW), u16(c0, VID_CTX_CH),
               u16(c0, VID_CTX_VX), u16(c0, VID_CTX_VY),
               u16(c1, VID_CTX_CW), u16(c1, VID_CTX_CH), seam,
               u16(c1, VID_CTX_VY)))
        cards = {c["idx"]: c for c in m.cards()}
        pri, sec = cardof(m, cards, c0), cardof(m, cards, c1)
        if pri == sec:
            sys.exit("dispsize: both displays resolved to card %d - the two "
                     "cards are the same kind, so no capture can say which "
                     "one it is of" % pri)
        say("...display 0 is emulator card %d (%s), display 1 is card %d (%s)"
            % (pri, cards[pri]["type"], sec, cards[sec]["type"]))

        # --- A: a resizable window, across and back -------------------------
        print("\nA. a RESIZABLE window walked across the seam and home again")
        dispcp.open_drive(m, mo, S, settle, "B", card=pri)
        slot = dispcp.win_list(m, S)[-1]

        def step(tag):
            x, y, w, h = dispcp.win_rect(m, S, slot)
            bx, by, bw, bh = bank(m, slot)
            say("%-30s rect (%4d,%3d) %3dx%-3d  bank (%4d,%3d) %3dx%d"
                % (tag, x, y, w, h, bx, by, bw, bh))
            return x, y, w, h

        def hauln(x, y, w, tox, card):
            """Drag by the TITLE BAR, re-read every time.

            The y is re-derived per drag and must be: SPEC.md 39.16.3.3's
            snap moves a window UP when its floor beats the room, so a cy
            banked once from the first rect lands in the CONTENT after the
            first landing - the press then hits a file row, the drag does
            nothing at all, and the leg reads as a window that would not come
            home."""
            cy = y + TITLE_H // 2
            mo.drag(x + w // 2, cy, tox + w // 2, cy)
            settle(m, card=card)

        x, y, w, h0 = step("opened on the Hercules")
        hauln(x, y, w, seam - w // 3, sec)
        x, y, w, h = step("straddling the seam")
        if h >= h0:
            sys.exit("dispsize: the straddle gave nothing back, so this run "
                     "never had anything to restore (dispstrad.py is the gate "
                     "for that half)")
        hauln(x, y, w, seam + 40, sec)
        x, y, w, hcga = step("wholly on the CGA")
        hauln(x, y, w, 200, pri)
        x, y, w, hend = step("dragged back to the Hercules")

        if hend == h0:
            say("...and its height came home: %d -> %d -> %d -> %d"
                % (h0, h, hcga, hend))
        else:
            say("...and its height did NOT come home: %d -> %d -> %d -> %d"
                % (h0, h, hcga, hend))
            if a.gate:
                fail.append("A: the window went out at %d rows and came back "
                            "at %d. ui_drag banks AFTER wm_strad_fit, so the "
                            "natural bank remembers the CUT and nothing can "
                            "restore the size (WINDOW-SIZING-PLAN 3.5)"
                            % (h0, hend))

        # --- B: a fixed-size window, dropped clear across --------------------
        print("\nB. a FIXED-SIZE window dropped clear across in ONE motion")
        wx, wy, _, _ = dispcp.win_rect(m, S, slot)
        dispcp.open_named(m, mo, S, settle, wx, wy, "GAMES", card=pri)
        settle(m, card=pri)
        wx, wy, _, _ = dispcp.win_rect(m, S, slot)
        dispcp.open_named(m, mo, S, settle, wx, wy, "MINES.O88", card=pri)
        settle(m, card=pri)
        sl = [s for s in dispcp.win_list(m, S) if s != slot]
        if not sl:
            sys.exit("dispsize: Minesweeper did not open")
        sl = sol = sl[-1]
        x, y, w, h = dispcp.win_rect(m, S, sl)
        h0 = h
        say("Mines on the Hercules          (%4d,%3d) %3dx%d, last row %d"
            % (x, y, w, h, y + h))
        cy = y + TITLE_H // 2
        mo.drag(x + w // 2, cy, seam + 30 + w // 2, cy)
        settle(m, card=sec)
        x, y, w, h = dispcp.win_rect(m, S, sl)
        say("...wholly on the CGA           (%4d,%3d) %3dx%d, last row %d "
            "against display 1's %d" % (x, y, w, h, y + h, bot1 - 1))
        if y + h > bot1:
            say("...so %d row(s) of it hang off the bottom of display 1, which "
                "is what a window low on the PRIMARY does too (39.16.3.2)"
                % (y + h - bot1 + 1))
        if h != h0:
            if a.gate:
                fail.append("B: a %d-row frame came back %d rows having "
                            "crossed nothing. A release wholly on one display "
                            "meets no seam and must not be resized "
                            "(SPEC.md 39.16.3.2)" % (h0, h))
        else:
            say("...and it is the %d rows it left with: nothing was crossed, "
                "so nothing was cut" % h)

        # --- C: a declared per-adapter size, adopted on landing --------------
        print("\nC. a package that DECLARES a size per adapter (SPEC.md 11.100.4)")
        wx, wy, ww, _ = dispcp.win_rect(m, S, slot)
        mo.click(wx + ww // 2, wy + TITLE_H // 2)   # the Disk window to the
        settle(m, card=pri)                         # front: a background one
                                                    # re-lists on FOCUS
                                                    # (SPEC.md 22.8), so a row
                                                    # clicked before that is
                                                    # resolved against the
                                                    # listing it still holds
        wx, wy, _, _ = dispcp.win_rect(m, S, slot)
        dispcp.open_named(m, mo, S, settle, wx, wy, "..", card=pri)
        settle(m, card=pri)
        wx, wy, _, _ = dispcp.win_rect(m, S, slot)
        dispcp.open_named(m, mo, S, settle, wx, wy, "APPS", card=pri)
        settle(m, card=pri)
        wx, wy, _, _ = dispcp.win_rect(m, S, slot)
        dispcp.open_named(m, mo, S, settle, wx, wy, "TRACKER.O88", card=pri)
        settle(m, card=pri)
        sl = [s for s in dispcp.win_list(m, S) if s not in (slot, sol)]
        if not sl:
            sys.exit("dispsize: Tracker did not open")
        sl = sl[-1]
        x, y, w, h = dispcp.win_rect(m, S, sl)
        say("Tracker on the Hercules        (%4d,%3d) %3dx%d" % (x, y, w, h))
        full = h
        cy = y + TITLE_H // 2
        mo.drag(x + w // 2, cy, seam + 30 + w // 2, cy)
        settle(m, card=sec)
        x2, y2, w2, h2 = dispcp.win_rect(m, S, sl)
        say("...dragged onto the CGA        (%4d,%3d) %3dx%d" % (x2, y2, w2, h2))
        mo.drag(x2 + w2 // 2, y2 + TITLE_H // 2, 120 + w2 // 2, y2 + TITLE_H // 2)
        settle(m, card=pri)
        x3, y3, w3, h3 = dispcp.win_rect(m, S, sl)
        say("...and back to the Hercules    (%4d,%3d) %3dx%d" % (x3, y3, w3, h3))
        # ...and the FRAME is not the pixels (SPEC.md 39.16.3.1), so the only
        # thing that says the face followed is a capture against a forced full
        # repaint of the card it landed on - THE CARD, resolved by cardof(),
        # and not the kernel's display number, which is a different numbering
        # and is inverted on this machine.
        #
        # Three things this comparison has to be told, and it was told none of
        # them. The capture is a (w, h, bytes) TUPLE, so handing it whole to
        # dispcorner.diff compares two 3-tuples and reports at most one
        # "pixel", which means only "these two captures are not identical" -
        # it is not a count and its coordinates are nobody's. The width is the
        # CAPTURE's, not the kernel's idea of the display's. And the POINTER
        # has to be in the same place for both, because dispcorner.repaint
        # parks it before the second one.
        before = steady(m, sec)[2]
        mo.drag(x3 + w3 // 2, y3 + TITLE_H // 2, seam + 30 + w3 // 2,
                y3 + TITLE_H // 2)
        settle(m, card=sec)
        x4, y4, w4, h4 = dispcp.win_rect(m, S, sl)
        mo.to(*PARK)
        settle(m, card=sec)
        cw, _, inc = steady(m, sec)
        dispcorner.repaint(m, mo, pri)
        fullshot = steady(m, sec)[2]
        # ...and it must have SEEN the arrival, or the figure below is the
        # vacuous 0 of a capture pointed somewhere else (dispcorner.probe's
        # `prev` guard, and the reason this leg could report a verdict for a
        # year without ever looking at the CGA).
        moved = len(dispcorner.diff(before, inc, cw))
        resid = dispcorner.diff(inc, fullshot, cw)
        say("...on the CGA at %dx%d, incremental against a full repaint: "
            "%d differing pixel(s) (the drag moved %d)"
            % (w4, h4, len(resid), moved))
        if not moved and a.gate:
            fail.append("C: the drag onto the CGA moved NO pixel of card %d, "
                        "so the count above is vacuous - this capture is not "
                        "of the display the window landed on" % sec)
        elif resid and a.gate:
            fail.append("C: %d pixel(s) of the CGA disagree with a full "
                        "repaint after the adoption %r - the window took the "
                        "size and the face did not follow it"
                        % (len(resid), dispcorner.bbox(resid)))

        if h2 < full and h3 == full:
            say("...it took the compact face on the CGA and the full one home:"
                " %d -> %d -> %d" % (full, h2, h3))
        else:
            say("...it did NOT follow the card: %d -> %d -> %d"
                % (full, h2, h3))
            if a.gate:
                fail.append("C: Tracker declares two faces and stayed %d rows "
                            "on a card that cannot show them "
                            "(WINDOW-SIZING-PLAN 3.4)" % h2)

        # --- D: the floor, and the snap that keeps it on screen -------------
        print("\nD. a MINIMUM the clamp may not cut through (SPEC.md 11.100.2)")
        mo.click(x3 + w3 // 2, y3 + TITLE_H // 2)   # Tracker out of the way
        settle(m, card=pri)
        mo.drag(x3 + w3 // 2, y3 + TITLE_H // 2, 60 + w3 // 2,
                y3 + TITLE_H // 2)
        settle(m, card=pri)
        dx, dy, dw, dh = dispcp.win_rect(m, S, slot)
        mo.click(dx + dw // 2, dy + TITLE_H // 2)   # the Disk window to the
        settle(m, card=pri)                         # front, then DROP IT LOW
        dx, dy, dw, dh = dispcp.win_rect(m, S, slot)
        low = bot1 - 40                             # 40 rows of room, and its
        mo.drag(dx + dw // 2, dy + TITLE_H // 2,    # floor is FM_MIN_H = 92
                seam + 40 + dw // 2, low)
        settle(m, card=sec)
        dx, dy, dw, dh = dispcp.win_rect(m, S, slot)
        say("dropped with 40 rows of room below it: (%4d,%3d) %3dx%d, "
            "ending on row %d" % (dx, dy, dw, dh, dy + dh))
        if dh < FM_MIN_H:
            fail.append("D: the Disk window came out %d rows tall against a "
                        "declared floor of %d - the clamp cut through the "
                        "minimum (SPEC.md 11.100.2)" % (dh, FM_MIN_H))
        elif dy + dh > bot1:
            fail.append("D: %d rows tall at y=%d ends on row %d, past display "
                        "1's %d. A window that will not fit from where it was "
                        "dropped is SNAPPED up to end on the last row the "
                        "display has (SPEC.md 39.16.3.3)"
                        % (dh, dy, dy + dh, bot1 - 1))
        elif dy >= low:
            fail.append("D: it stayed at y=%d, so nothing snapped and this leg "
                        "measured nothing" % dy)
        else:
            say("...it took %d rows - the whole band, which is more than the "
                "40 it was dropped with - and the ORIGIN followed the size up "
                "from %d to %d, ending on row %d against display 1's %d"
                % (dh, low, dy, dy + dh, bot1 - 1))
            say("...(the %d-row floor itself is not what bound here: the band "
                "is bigger than it. dispprefer.py leg E is the floor's gate, "
                "where a GROW is dragged into TeXPad's 262x103)" % FM_MIN_H)

        # --- E: the facts a package takes from the ADAPTER, on a DRAG -------
        print("\nE. packages ask about the card they are ON (SPEC.md 39.16.4)")
        SWEEP = [("paint", "APPS", "PAINT.O88", "PAINT",
                  ["pt_scrw", "pt_scrh", "pt_dockr", "pt_chmax"]),
                 ("arkanoid", "GAMES", "ARKANOID.O88", "ARKANOID",
                  ["ark_scrw", "ark_dock"])]

        for app, folder, fname, hdr, names in SWEEP:
            pbase = dispapps.img_size(app)

            def facts(seg, app=app, names=names, pbase=pbase):
                return dict((n, u16(m.read((seg << 4) + pbase +
                                           dispapps.bss_off(app, n), 2)))
                            for n in names)

            dx, dy, dw, dh = dispcp.win_rect(m, S, slot)
            mo.click(dx + dw // 2, dy + TITLE_H // 2)   # the Disk window to
            settle(m, card=pri)                         # the front, and home
            dx, dy, dw, dh = dispcp.win_rect(m, S, slot)
            if dx > seam - dw:
                mo.drag(dx + dw // 2, dy + TITLE_H // 2, 40 + dw // 2, 80)
                settle(m, card=pri)
                dx, dy, dw, dh = dispcp.win_rect(m, S, slot)
            for step in ("..", folder, fname):      # up, in, and launch - the
                if step is None:                    # window is left wherever
                    continue                        # the last app came from
                dispcp.open_named(m, mo, S, settle, dx, dy, step, card=pri)
                settle(m, card=pri)
                dx, dy, dw, dh = dispcp.win_rect(m, S, slot)
            pslot = pseg = None
            for n in range(os88geom.MAX_WIN):
                got = dispapps.pkg_seg(m, n)
                if got is None:
                    break
                if m.read((got[1] << 4) + 16, 16).split(b"\0")[0] == \
                        hdr.encode():
                    pslot, pseg = got   # (SLOT, seg) - `n` is the ordinal among
                    break               # package windows and is NOT the slot
            if pseg is None:
                sys.exit("dispsize: no %s package window - is it open?" % hdr)

            def pshow(tag, f):
                x, y, w, h = dispcp.win_rect(m, S, pslot)
                say("%-30s (%4d,%3d) %3dx%-3d pkind=%d  %s"
                    % (tag, x, y, w, h, m.read(os88sym.wfield(pslot, "W_PKIND"), 1)[0],
                       "  ".join("%s=%d" % (k, f[k]) for k in names)))
                return x, y, w, h

            onherc = facts(pseg)
            px, py, pw, ph = pshow("%s on the Hercules" % hdr.title(), onherc)

            # THE STRADDLE IS A STEP OF ITS OWN, and it was missing: this leg
            # dragged straight across, so the one transition a user actually
            # makes first - push the window until it reaches the other card -
            # was never exercised here at all. A straddling window reads as
            # the MORE RESTRICTIVE display (wm_disp_rest), so the facts must
            # already be the CGA's here, and the drag on from the straddle is
            # then not a change of kind and must move nothing.
            mo.drag(px + pw // 2, py + TITLE_H // 2, seam - pw // 3,
                    py + TITLE_H // 2)
            settle(m, card=sec)
            strad = facts(pseg)
            sx, sy, sw, sh = pshow("...pushed onto the SEAM", strad)
            if sx + sw <= seam or sx >= seam:
                say("...(it did not land on the seam - the straddle "
                    "assertion is skipped)")
                strad = None
            elif strad == onherc:
                fail.append("E: %s still believes every one of %s with its "
                            "frame across the seam. A straddling window reads "
                            "as the MORE RESTRICTIVE card (SPEC.md "
                            "39.16.4.1), so this is the transition, not the "
                            "drag that follows it" % (hdr, ", ".join(names)))

            mo.drag(sx + sw // 2, sy + TITLE_H // 2, seam + 30 + pw // 2,
                    py + TITLE_H // 2)
            settle(m, card=sec)
            oncga = facts(pseg)
            pshow("...dragged fully onto the CGA", oncga)
            if strad is not None and oncga != strad:
                fail.append("E: %s changed its mind again going from the seam "
                            "to the CGA (%s). The straddle already read as the "
                            "restrictive card, so that drag is NOT a change of "
                            "kind and nothing may resize twice (SPEC.md "
                            "39.16.4.1)"
                            % (hdr, ", ".join("%s %d->%d" % (k, strad[k],
                                                            oncga[k])
                                              for k in names
                                              if strad[k] != oncga[k])))
            cga_w, cga_h = u16(c1, VID_CTX_CW), u16(c1, VID_CTX_CH)
            moved = [k for k in names if oncga[k] != onherc[k]]
            if not moved:
                fail.append("E: %s still believes every one of %s after a drag "
                            "onto a %dx%d card. OSAPI_VIDEO answers about the "
                            "PRIMARY; an 11.98 handler on a two-card machine "
                            "has to ask OSAPI_WM_DISPLAY (SPEC.md 39.16.4)"
                            % (hdr, ", ".join(names), cga_w, cga_h))
            elif oncga[names[0]] != cga_w:
                fail.append("E: %s reads %s=%d on a card that is %d wide"
                            % (hdr, names[0], oncga[names[0]], cga_w))
            else:
                say("...%d of %d followed the WINDOW, not the primary"
                    % (len(moved), len(names)))

        # --- F: the Task Manager's two-column CGA mode ----------------------
        print("\nF. the Task Manager reaches its two-column mode (SPEC.md 28.1)")
        mo.menu(12, 8, 60, 60)              # the chip menu's Task Manager item
        m.advance(frames=150)               # NOT settle: it repaints twice a
        m.run()                             # second, so the screen never stops
        tslot = tseg = None
        for n in range(os88geom.MAX_WIN):
            got = dispapps.pkg_seg(m, n)
            if got is None:
                break
            if m.read((got[1] << 4) + 16, 16).split(b"\0")[0] == b"TaskMgr":
                tslot, tseg = got           # the PACKAGE HEADER's name field
                break                       # (SPEC.md 20.2) - not the menu
        if tseg is None:                    # item's text, and not the 8.3 stem.
            sys.exit("dispsize: the Task Manager did not open - the chip "
                     "menu's item did not land")   # ...and NO fallback to "the
                                            # first package window": leg B left
                                            # Solitaire open, so that read a
                                            # stranger's segment at taskmgr's
                                            # bss offsets and reported zeros
        tbase = dispapps.img_size("taskmgr")

        def tf(n):
            return u16(m.read((tseg << 4) + tbase +
                              dispapps.bss_off("taskmgr", n), 2))

        def tshow(tag):
            x, y, w, h = dispcp.win_rect(m, S, tslot)
            say("%-30s (%4d,%3d) %3dx%-3d  vw=%d cols=%d"
                % (tag, x, y, w, h, tf("tm_vw"), tf("tm_cols")))
            return x, y, w, h

        tx, ty, tw, th = tshow("Task Manager on the Hercules")
        cols0 = tf("tm_cols")
        mo.drag(tx + tw // 2, ty + TITLE_H // 2, seam + 30 + tw // 2,
                ty + TITLE_H // 2)
        m.advance(frames=200)
        m.run()
        tx, ty, tw, th = tshow("...dragged onto the CGA")
        cols1 = tf("tm_cols")
        if tf("tm_vw") < u16(c1, VID_CTX_CW):
            fail.append("F: tm_vw is %d on a display %d wide - the column test "
                        "asks whether there is room for a SECOND column, so "
                        "handing it the FRAME's width answers no for ever"
                        % (tf("tm_vw"), u16(c1, VID_CTX_CW)))
        elif cols1 != 2:
            fail.append("F: %d column(s) on a %d-row CGA. The list is %d rows "
                        "deep there and TM_COL2_MIN is 12, so this is the "
                        "adapter the two-column mode exists for (SPEC.md 28.1)"
                        % (cols1, u16(c1, VID_CTX_CH), th))
        else:
            say("...%d column(s) -> %d, and the frame followed at %dx%d"
                % (cols0, cols1, tw, th))

    print()
    for f in fail:
        print("dispsize: FAIL: %s" % f)
    if fail:
        return 1
    if a.gate:
        print("dispsize: a window keeps its size across the seam, lands on "
              "rows the display has, and takes the face it published - PASS")
    else:
        print("dispsize: measurement only - see docs/FIELD-NOTES.md 26")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
