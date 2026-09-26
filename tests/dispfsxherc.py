#!/usr/bin/env python3
"""Does the PRIMARY survive an fsx bracket on the SECOND display?
(SPEC.md 39.19.4.1)

    make && python3 tests/dispfsxherc.py

`tests/dispfsxcga.py` is this row's mirror and the two are a pair: that one
takes the bracket on the Hercules PRIMARY and watches the CGA, this one takes
it on the CGA SECOND display and watches the Hercules. Reported off the same
two-card XT - *"move the dos window over to cga, go fullscreen, return: the
herc screen is corrupted, the CGA screen is fine"*.

**IT IS THE EQUIPMENT FLAG AND NOT A MODE BYTE.** The ROM's mode set is
equipment-driven (SPEC.md 39.19.4), so `vid_text` asking for mode 3 while
`40:10` still says mono - which it does, the desktop's primary being the
Hercules - makes the ROM force **mode 7 and the 3B4h CRTC**: the Hercules is
retimed for 80x25 MDA text over its own graphics framebuffer, and the CGA is
never touched. `vid_setmode` had this and fixed it by moving `vid_cga_equip`
above its own `VID_CGA` test; `vid_text` has the identical arm and never got
the call.

**`40:65h` CANNOT SEE IT, SO THIS ROW MUST NOT WATCH THAT BYTE.** IBM's
mode-register table gives mode 3 and mode 7 the *same* value, `0x29`, so the
BIOS shadow reads identically whether the ROM honoured the request or forced
it. What moves is the mono card's own RASTER, and leg 2 is that: 912 wide with
the Hercules' graphics timings kept, **882** once the ROM has retimed it for
text. An integer, exact, no threshold.

FIVE LEGS, measured before and after the fix on `os8088_5150_both_gla_mono`:

  1. the case is PRESENT - the drag put the window's centre on display 1, so
     `[fsx_wdisp]` is 1 once the bracket is up. Without this every leg below
     is about a bracket on the primary, which is the OTHER row
  2. the Hercules' raster is unchanged inside the bracket   912 vs **882**
  3. ...and its picture comes back                  1,322 vs **134,951** of
     252,000 pixels differing across the round trip
  4. the FULL SCREEN reached the CGA - the half nobody reported. Without the
     fix the CGA kept its 640x200 bitmap while the teletype wrote character
     cells into B8000, so the monitor showed those cells as PIXELS: coloured
     blocks where an 80x25 text screen belongs.       0 coloured pixels vs
     **20,320** of 128,000
  5. `40:10` is back to the primary's kind afterwards. `vid_cga_equip` leaves
     it saying colour and for the length of the bracket that is REQUIRED
     (SPEC.md 96.33's teletype reads it), so the restore belongs to
     `vid_fsx_leave` - and leg 5 is what says it happened

Leg 3 is the only one with a BOUND rather than an equality, and the residual
is named rather than tolerated: the menu bar's clock ticks, the pointer moves,
and the DOS window straddles the seam so its own console content - which a
full-screen session is entitled to change - is on the Hercules too. The two
arms are two orders of magnitude apart, so the bound is not a tuned number.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)

import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
from os88geom import VID_CTX_SZ, VID_CTX_VX, VID_CTX_CW     # noqa: E402

MACHINE = "os8088_5150_both_gla_mono"
FSX_NONE = 0xFF                 # [fsx_cur]: no mode set, so no bracket is up
HERC = 0                        # the primary's card index on this machine
CGA = 1
DIRTY_MAX = 20000               # leg 3's bound - see the docstring. The arms
                                # are 1,322 and 134,951


def raster(m, card):
    v = m.video(card=card)
    return int(v.get("field_w") or 0)


def vidbits(m):
    """40:10 bits 5:4 - 3 is mono at B000, 2 is 80x25 colour."""
    return (m.readseg(0x0040, 0x0010, 1)[0] >> 4) & 3


def steady_fbuf(m, settle, card, tries=6):
    """The card's rasterised frame, once it has STOPPED changing.

    `[fsx_vndisp]` says the cards are lit and `settle` says the screen is
    still, and neither is enough on its own: measured, the first settle after
    a restore returns mid-repaint and the capture differs from the next one by
    ~4,500 pixels, on an idle box. So this settles until two consecutive
    captures agree, which is `settle`'s own signal one level up and costs one
    extra round when it was already converged.

    It has to be the RASTERISATION and not the framebuffer bytes here, which
    is the opposite of `tests/dispfsxcga.py`'s leg 4: the defect this row is
    about retimes the 6845 and never touches a byte of VRAM, so the bytes are
    identical in both arms and only what the card SCANS can see it.
    """
    prev = m.fbuf(card=card)
    for _ in range(tries):
        settle(m, card=card)
        cur = m.fbuf(card=card)
        if cur == prev:
            return cur
        prev = cur
    return prev


def coloured(px):
    """Pixels that are not grey. A text screen drawn as TEXT has none."""
    return sum(1 for i in range(0, len(px), 3)
               if not (px[i] == px[i + 1] == px[i + 2]))


def bracket(m):
    return m.read(m.sym("fsx_cur"), 1)[0]


def wait_bracket(m, want, limit=12.0):
    try:                                # the limit is GUEST time
        os88marty.until(m, lambda _: bracket(m) == want,
                        "[fsx_cur] = %02X" % want, poll=0.15, limit=limit)
    except os88marty.MartyError:
        return bracket(m)               # the caller reads what it is instead
    return want


def wait_restored(m, S, limit=25.0):
    """Wait until `vid_fsx_unblank` has run, and only THEN look at pixels.

    `[fsx_cur]` going back to 0xFF is NOT the end of the restore - SPEC.md
    53.6 clears it, then repaints the desktop, and only after that lights the
    cards `vid_fsx_enter` darked. So a `settle` on the second display in
    between is satisfied by two identical BLACK frames and returns with the
    picture still to come.

    `[fsx_vndisp]` is the exact gate: `vid_fsx_unblank` writes 1 to it as its
    one-shot, after the repaint. Measured - this row passed standing alone and
    read thousands of changed pixels under a four-lane soak, which is
    docs/WRITING-TESTS.md's own warning about a wait sized on an idle box.
    """
    try:                                # the limit is GUEST time
        os88marty.until(m, lambda _: m.read(S("fsx_vndisp"), 1)[0] == 1,
                        "vid_fsx_unblank", poll=0.1, limit=limit)
    except os88marty.MartyError:
        return False
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args()

    S = lambda n: os88sym.linear(n)                          # noqa: E731
    settle = os88marty.settle
    ok = True

    def fail(msg):
        nonlocal ok
        print("FAIL: " + msg)
        ok = False

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        cards = m.cards()
        if len(cards) != 2 or cards[HERC]["type"] != "mda" \
                or cards[CGA]["type"] != "cga":
            sys.exit("dispfsxherc: %s is not a mono-primary + CGA machine "
                     "(%s)" % (a.machine, [c["type"] for c in cards]))
        m.run()
        settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_panel(m, mo, S, settle)
        dispcp.set_mode(m, mo, S, settle, "right")
        dispcp.close_panel(m, mo, S, settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("dispfsxherc: the Control Panel did not turn Extend on")

        # --- the DOS box, then DRAGGED onto the second display -------------
        dispcp.open_drive(m, mo, S, settle, "A", card=HERC)
        x, y, ww, wh = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        dispcp.open_named(m, mo, S, settle, x, y, "APPS", card=HERC)
        x, y, ww, wh = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        dispcp.open_named(m, mo, S, settle, x, y, "DOS.O88", card=HERC)
        slot = dispcp.win_list(m, S)[-1]
        x, y, ww, wh = dispcp.win_rect(m, S, slot)

        c1 = m.read(S("vid_ctx") + VID_CTX_SZ, VID_CTX_SZ)
        seam = c1[VID_CTX_VX] | (c1[VID_CTX_VX + 1] << 8)
        cw1 = c1[VID_CTX_CW] | (c1[VID_CTX_CW + 1] << 8)
        # wm_disp_of takes the CENTRE first (SPEC.md 39.17), and SPEC.md 11.94
        # will not let a 642-wide window sit wholly inside a 640-wide display -
        # so this aims the centre and lets the edge straddle, which is exactly
        # what the report describes.
        mo.drag(x + ww // 2, y + 9, seam + cw1 // 2, y + 9)
        settle(m, card=CGA)
        x, y, ww, wh = dispcp.win_rect(m, S, slot)
        print("  display 1 at x=%d (%d wide); DOS window centre now x=%d"
              % (seam, cw1, x + ww // 2))

        before_raster = raster(m, HERC)
        before_bits = vidbits(m)
        before_px = steady_fbuf(m, settle, HERC)
        print("  Hercules raster %d, 40:10 video bits %d"
              % (before_raster, before_bits))
        if before_bits != 3:
            sys.exit("dispfsxherc: 40:10 says %d, not mono - the ROM is not "
                     "being told the Hercules is the primary, so the defect's "
                     "own input is absent" % before_bits)

        # --- into full screen ----------------------------------------------
        m.alt("Enter")
        if wait_bracket(m, 0) == FSX_NONE:          # FSXM_TEXT80 is 0
            sys.exit("dispfsxherc: Alt+Enter did not go full screen "
                     "([fsx_cur]=%02X)" % bracket(m))

        # --- leg 1: THE CASE IS PRESENT ------------------------------------
        wdisp = m.read(S("fsx_wdisp"), 1)[0]
        if wdisp != 1:
            sys.exit("dispfsxherc: the bracket ran on display %d, not the "
                     "second one - the drag did not move the window's centre "
                     "onto the CGA, so this row is testing dispfsxcga's case "
                     "and not its own" % wdisp)
        print("  leg 1: bracket is on display %d, [vid_kind]=%d - the case is "
              "in the row" % (wdisp, m.read(S("vid_kind"), 1)[0]))

        settle(m, card=CGA)

        # --- leg 2: the Hercules' RASTER (see the docstring) ---------------
        inside_raster = raster(m, HERC)
        if inside_raster != before_raster:
            fail("leg 2: the Hercules' raster moved %d -> %d inside the "
                 "bracket. The ROM's equipment-driven mode set forced mode 7 "
                 "onto the MONO card instead of mode 3 onto the CGA "
                 "(SPEC.md 39.19.4.1) - on the field machine this is the "
                 "corrupted Hercules" % (before_raster, inside_raster))
        else:
            print("  leg 2: Hercules raster still %d inside the bracket"
                  % inside_raster)

        # --- leg 4: ...and the full screen reached the CGA ------------------
        cw, ch, cpx = m.fbuf(card=CGA)
        col = coloured(cpx)
        if col:
            fail("leg 4: %d of %d pixels on the full screen are COLOURED, so "
                 "the CGA is still scanning its 640x200 bitmap while the "
                 "teletype writes character cells into B8000 - the full "
                 "screen itself is unreadable (SPEC.md 39.19.4.1)"
                 % (col, cw * ch))
        else:
            print("  leg 4: the full screen is text on the CGA - 0 of %d "
                  "pixels coloured" % (cw * ch))

        # --- back out ------------------------------------------------------
        m.alt("Enter", hold=0.08)
        if wait_bracket(m, FSX_NONE) != FSX_NONE:
            sys.exit("dispfsxherc: Alt+Enter did not leave full screen "
                     "([fsx_cur]=%02X)" % bracket(m))
        if not wait_restored(m, S):
            sys.exit("dispfsxherc: vid_fsx_unblank never ran - the Hercules "
                     "is still dark and nothing below can be read")

        # --- leg 3: the Hercules' PICTURE comes back -----------------------
        after = steady_fbuf(m, settle, HERC)
        if after[:2] != before_px[:2]:
            fail("leg 3: the Hercules changed SIZE, %dx%d -> %dx%d"
                 % (before_px[0], before_px[1], after[0], after[1]))
        else:
            diff = sum(1 for i in range(0, len(after[2]), 3)
                       if after[2][i:i + 3] != before_px[2][i:i + 3])
            tot = after[0] * after[1]
            if diff > DIRTY_MAX:
                fail("leg 3: %d of %d pixels on the Hercules differ across "
                     "the bracket (bound %d). A graphics framebuffer scanned "
                     "on the text timings the ROM left behind is what the "
                     "field photographed" % (diff, tot, DIRTY_MAX))
            else:
                print("  leg 3: %d of %d Hercules pixels differ, under the "
                      "%d bound - the clock, the pointer and the straddling "
                      "window's own console" % (diff, tot, DIRTY_MAX))

        # --- leg 5: the equipment flag is the primary's again --------------
        bits = vidbits(m)
        if bits != 3:
            fail("leg 5: 40:10's video bits are %d, not 3 - the machine is "
                 "left claiming a COLOUR primary, which is the flag SPEC.md "
                 "39.20's Restart reads. vid_fsx_leave owes the vid_equip "
                 "that vid_disp_init's extend arm already makes" % bits)
        else:
            print("  leg 5: 40:10 video bits back to %d (mono)" % bits)

    print("dispfsxherc: %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
