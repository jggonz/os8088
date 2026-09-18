#!/usr/bin/env python3
"""Does the SECOND display survive an fsx bracket on the first?
(SPEC.md 39.18.1.1)

    make && python3 tests/dispfsxcga.py

Reported off an XT with a CGA and a Hercules in it, the **Hercules primary**:
open the DOS box on the Hercules, Alt+Enter into full screen, come back out,
and *"the second cga display turns green and flickers and has corrupted gfx"*.

**IT IS ONE BYTE AND IT IS THE ROM'S.** 40:65h is the BIOS's shadow of the CRT
mode register and a BIOS keeps ONE of them, so it describes whichever card the
ROM last set a mode on. `vid_unblank_kind`'s CGA arm sourced 3D8h from it, and
`fsx_mode(FSXM_TEXT80)` on a Hercules display is `int 10h AX=0007h` - so the
byte the CGA got on the way out was **mode 7's 0x29**: 80x25 text, video on,
**blink on**, over a 6845 still timed for mode 6. The desktop's 50% dither then
decodes as attribute bytes of 0xAA - background green, foreground light green,
blinking - and 66% of the card measured RGB (0,170,0) on the run that wrote
this file.

**SO THE ASSERTION IS THE CARD'S MODE, NOT ITS PIXELS.** Green is what a person
sees; a mode register is what is wrong, and `video(card=1)` answers it in one
word. The ground is asserted too, in leg 4, and as a DIFF against the same card
before the bracket rather than against a golden image - the second display
carries no chrome (SPEC.md 39.14.4), so what `wm_paint_all` puts back there has
to be what was there, with no clock cell to move under it. It reads the
framebuffer BYTES and not `fbuf`, which on a secondary card in a graphics mode
this emulator does not rasterise faithfully - see `ground()` below, and
docs/MARTYPC-DEBUG.md, which carries the measurement.

**LEG 2 IS WHAT KEEPS THE ROW HONEST.** Every other leg here passes vacuously
on a ROM that does not stamp 40:65h, or on a build where the bracket somehow
never sets a mode - and both would look exactly like a fix. So the row asserts
that the poison IS present: the BIOS byte must MOVE across `fsx_mode`, and move
to a value whose bit 0 is set, which is to say to a TEXT mode. That is the
defect's own input, measured rather than assumed, and the card's mode is then
asserted against it.

Measured, on `os8088_5150_both_gla_mono` (a Hercules primary with a CGA beside
it - `os8088_5150_both_gla` is the other orientation and is NOT this case: a
bracket on a CGA primary sends the Hercules arm, which writes constants, and
`fsx_restore`'s own `vid_setmode` re-banks the CGA's byte on the way out):

    before the fix   40:65h 1E -> 29,  card 1  Mode6HiResGraphics -> Mode3TextCo80
    after  the fix   40:65h 1E -> 29,  card 1  Mode6HiResGraphics -> Mode6HiResGraphics

- the BIOS byte moves in BOTH arms, which is the point: the ROM is doing
  nothing wrong, and what changed is that the kernel stopped reading it.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)

import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402

MACHINE = "os8088_5150_both_gla_mono"
FSX_NONE = 0xFF                 # [fsx_cur]: no mode set, so no bracket is up
CGA = 1                         # the secondary card's index on this machine


def shadow(m):
    """The BIOS's CRT mode shadow - the byte the kernel must NOT be using."""
    return m.readseg(0x0040, 0x0065, 1)[0]


def cardmode(m, card=CGA):
    return str(m.video(card=card).get("mode"))


CGA_VRAM = 0xB8000              # mode 6: 16,000 bytes, two interleaved banks


def ground(m):
    """The CGA's framebuffer BYTES - the desktop the kernel actually wrote.

    NOT `fbuf(card=1)`, and that is a finding rather than a preference: on
    this machine MartyPC's rasterisation of the SECOND card is not faithful.
    Measured - the VRAM here is a perfect 50% dither, 8,000 bytes of 0xAA and
    8,000 of 0x55 (SPEC.md 39.4's ground), which mode 6 can only draw as
    uniform vertical stripes over the whole 640x200; what `fbuf` hands back
    has black bands and a solid blue block in it. So the picture is the
    emulator's and the bytes are ours, and leg 4 asks about ours.

    The card's own STATE is still asserted, by legs 1-3 - which is the right
    split, because this row's defect is a mode register and leaves the
    framebuffer perfect. `tests/dispfsxherc.py` is the mirror and needs the
    opposite instrument for the opposite reason: its defect is the CRTC, so
    the bytes cannot see it and only the rasterisation can.
    """
    return m.read(CGA_VRAM, 16000)


def bracket(m):
    return m.read(m.sym("fsx_cur"), 1)[0]


def wait_bracket(m, want, limit=10.0):
    """Wait for [fsx_cur] on the GUEST's own state, not a host sleep."""
    end = time.time() + limit
    while time.time() < end:
        if bracket(m) == want:
            return want
        time.sleep(0.15)
    return bracket(m)


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
    end = time.time() + limit
    while time.time() < end:
        if m.read(S("fsx_vndisp"), 1)[0] == 1:
            return True
        time.sleep(0.1)
    return False


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
        if len(cards) != 2:
            sys.exit("dispfsxcga: %s is not a two-card machine" % a.machine)
        if cards[CGA]["type"] != "cga":
            sys.exit("dispfsxcga: card %d of %s is %r, not a CGA - this row is "
                     "about the CGA arm of vid_unblank_kind"
                     % (CGA, a.machine, cards[CGA]["type"]))
        m.run()
        settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        # --- the extended desktop, which is what puts the CGA in mode 6 ----
        dispcp.open_panel(m, mo, S, settle)
        dispcp.set_mode(m, mo, S, settle, "right")
        dispcp.close_panel(m, mo, S, settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("dispfsxcga: the Control Panel did not turn Extend on")

        # --- the DOS box, on the PRIMARY (the display the bracket runs on) --
        dispcp.open_drive(m, mo, S, settle, "A", card=0)
        w = dispcp.win_list(m, S)
        if not w:
            sys.exit("dispfsxcga: no Disk window")
        x, y, ww, wh = dispcp.win_rect(m, S, w[-1])
        dispcp.open_named(m, mo, S, settle, x, y, "APPS", card=0)
        x, y, ww, wh = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        dispcp.open_named(m, mo, S, settle, x, y, "DOS.O88", card=0)
        settle(m, card=CGA)

        # --- leg 1: the CGA is in a GRAPHICS mode before any of this -------
        before_mode = cardmode(m)
        before_shadow = shadow(m)
        before_px = ground(m)
        if "Graphics" not in before_mode:
            sys.exit("dispfsxcga: the CGA is in %r before the bracket, so this "
                     "row never had the case in it" % before_mode)
        print("  leg 1: card %d is %s, 40:65h=%02X"
              % (CGA, before_mode, before_shadow))

        # --- into full screen ----------------------------------------------
        m.alt("Enter")
        if wait_bracket(m, 0) == FSX_NONE:          # FSXM_TEXT80 is 0
            sys.exit("dispfsxcga: Alt+Enter did not take the DOS box full "
                     "screen ([fsx_cur]=%02X) - nothing below is testable"
                     % bracket(m))
        inside_shadow = shadow(m)
        print("  leg 1: full screen up, 40:65h=%02X" % inside_shadow)

        # [fsx_cur] is set by fsx_mode, which is the FIRST thing the box does
        # inside the bracket - so the poll loop that reads the leaving key is
        # not running yet, and a key pressed here is pressed at nothing. Let
        # the fullscreen screen come up first.
        settle(m, card=0)

        # --- leg 2: THE POISON IS PRESENT (see the docstring) --------------
        if inside_shadow == before_shadow:
            fail("leg 2: fsx_mode did NOT move the BIOS's 40:65h shadow "
                 "(%02X throughout). On this ROM the defect's own input is "
                 "absent, so legs 3 and 4 below cannot see it and a pass here "
                 "says nothing - SPEC.md 39.18.1.1" % before_shadow)
        elif not inside_shadow & 1:
            fail("leg 2: 40:65h moved %02X -> %02X, but bit 0 is clear, so it "
                 "is not a TEXT mode and writing it to 3D8h would not produce "
                 "the reported failure. The row needs re-deriving against this "
                 "ROM" % (before_shadow, inside_shadow))
        else:
            print("  leg 2: 40:65h %02X -> %02X - a TEXT mode, blink %s: the "
                  "poison IS in the row"
                  % (before_shadow, inside_shadow,
                     "ON" if inside_shadow & 0x20 else "off"))

        # --- back out ------------------------------------------------------
        m.alt("Enter", hold=0.08)
        if wait_bracket(m, FSX_NONE) != FSX_NONE:
            sys.exit("dispfsxcga: Alt+Enter did not leave full screen "
                     "([fsx_cur]=%02X)" % bracket(m))
        if not wait_restored(m, S):
            sys.exit("dispfsxcga: vid_fsx_unblank never ran - the second "
                     "display is still dark and nothing below can be read")
        settle(m, card=CGA)

        # --- leg 3: the CGA's MODE is where it was -------------------------
        after_mode = cardmode(m)
        if after_mode != before_mode:
            fail("leg 3: the second display came back in %r where it was %r. "
                 "vid_unblank_kind wrote the BIOS's shadow of the OTHER card's "
                 "mode set to 3D8h (SPEC.md 39.18.1.1) - on the field machine "
                 "this is the green flickering screen" % (after_mode,
                                                          before_mode))
        else:
            print("  leg 3: card %d is still %s" % (CGA, after_mode))

        # --- leg 4: ...and so is the DESKTOP it draws ----------------------
        # quiesce and not settle: the next read is guest MEMORY, and the
        # repaint is still running when [fsx_vndisp] says the cards are lit
        # (os88marty.quiesce's own rule).
        after_px = os88marty.quiesce(m, lambda: ground(m),
                                     what="the CGA's framebuffer")
        diff = sum(1 for i in range(len(before_px))
                   if before_px[i] != after_px[i])
        if diff:
            bad = [i for i in range(len(before_px))
                   if before_px[i] != after_px[i]]
            fail("leg 4: %d of %d framebuffer bytes on the second display "
                 "differ across the bracket, first at +%d. SPEC.md 53.6 "
                 "repaints the whole desktop and SPEC.md 39.14.4 puts no "
                 "chrome on a secondary, so the ground owed is the one that "
                 "was there" % (diff, len(before_px), bad[0]))
        else:
            print("  leg 4: all %d framebuffer bytes identical across the "
                  "bracket" % len(before_px))

    print("dispfsxcga: %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
