#!/usr/bin/env python3
"""Does a saver session dark the SECOND monitor? (SPEC.md 79.1.1)

    make && python3 tests/dispsaver.py

`kernel/blank.inc`'s own header says why this matters, and said it before the
code was right: *"blanking the primary alone would protect one monitor and
leave the other showing a frozen desktop all night."* The BLANKER honoured
that - it walks every display (SPEC.md 64.3) - and the ANIMATION did not:
`blk_set` returned on `ssf_set`'s CF = 0 before it ever reached the walk, so
on an extended desktop (SPEC.md 39.19) a session saved one tube and left the
other lit with the last desktop frame until morning. Phosphor burn-in is the
whole reason the feature exists, so the second monitor was getting the exact
treatment the first one was being spared.

It is one number, and the A/B on the kernel before the fix reads it directly:
display 1 renders **64,000 lit pixels during a session** where it now renders
**0**.

Four assertions, in the order they can fail:

  1. **IT IS A SESSION AND NOT THE BLANKER.** `[blk_sv]` = 1. Every refusal
     ss_start_x can answer falls through to the walk that always worked
     (SPEC.md 79.1), so a row that quietly took the blanker would pass on the
     broken kernel and prove nothing at all.
  2. **THE PRIMARY IS STILL LIT**, and drawing: the saver owns display 0 and
     its signal must stay on. A fix that gated every card would "pass" a
     dark-second-monitor test by darking the machine.
  3. **DISPLAY 1 RENDERS NOTHING.** The assertion the bug fails.
  4. **AND ITS FRAMEBUFFER IS UNTOUCHED.** This is what makes it a GATE and
     not a clear (SPEC.md 64.3, 39.18.1): the CRTC keeps running, the monitor
     never re-acquires, and the wake costs no repaint of that card's memory.
     A saver that BLACKED the second display instead would satisfy 3 and is a
     different mechanism with a different wake - the same distinction
     tests/saver.py draws for the blanker's own fallbacks.

**THE HERCULES IS THE PRIMARY HERE, AND THAT IS THE WHOLE SETUP.** MartyPC
models the CGA's video-enable bit and NOT the Hercules' - `vid_blank_kind`'s
header records the same thing from the other side - so on the default
CGA-primary arrangement display 1 is the mono card and `fbuf` reads a
constant 122,496 whatever the kernel does. Measured, on this machine: the
BLANKER, which has gated every display since SPEC.md 64.3, moves card 1 not
one pixel. So the instrument is blind there, and a row written that way is
green on both kernels. Setting the Hercules primary puts the CGA at display 1
and the assertion back on a gate the emulator can see.
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

S = os88sym.linear

IDLE_SOON = b"\x1c\x00"          # 28 ticks, about a second and a half
IDLE_NEVER = b"\x00\x40"         # ...and a quarter of an hour
MODE_STARS = b"\x02"             # ONE mode ticked, and the cheapest: this row
                                 # is about the walk and not about the picture
SECS_LONG = b"\xc8"              # one long turn, so no re-pick mid-reading


def rendered(m, card):
    """Lit pixels the CARD RASTERISED - not what is in its memory.

    `vram` says the kernel wrote the right bytes; this says the machine put
    them on a screen, and a gated card is exactly the case where those two
    answers differ (SPEC.md 39.18.1). It is the only route that can see a
    gate at all.
    """
    w, h, d = m.fbuf(card=card)
    return sum(1 for i in range(0, len(d), 3) if d[i] or d[i + 1] or d[i + 2])


def stored(m, kind):
    """...and the complement: lit pixels in that card's FRAMEBUFFER."""
    w, h, rows = m.vram(kind)
    return sum(sum(r) for r in rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args()

    fail = []
    say = lambda s: print("  " + s)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        cards = m.cards()
        if len(cards) != 2:
            sys.exit("dispsaver: %s has %d video card(s), not 2"
                     % (a.machine, len(cards)))
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        # --- Hercules primary, then extend. See the module docstring: the
        # order matters only in that both are done before anything is read.
        dispcp.open_panel(m, mo, S, os88marty.settle)
        herc = dispcp.adapter_row(m.read(S("vid_avail"), 1)[0], 1)  # VID_HERC
        dispcp.set_primary(m, mo, S, os88marty.settle, herc)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("dispsaver: the Control Panel did not turn Extend on")
        if m.read(S("vid_kind"), 1)[0] != 1:
            sys.exit("dispsaver: the Hercules is not the primary, so display "
                     "1 is the mono card and fbuf cannot see its gate")
        d1 = [c for c in cards if c["type"] == "cga"][0]["idx"]
        say("Hercules primary, extended; display 1 is the CGA (card %d)" % d1)

        lit0 = stored(m, "herc")
        lit1, mem1 = rendered(m, d1), stored(m, "cga")
        say("desktop:  primary %d lit | display 1 renders %d, holds %d"
            % (lit0, lit1, mem1))
        if lit1 < 1000 or lit0 < 1000:
            sys.exit("dispsaver: that is not an extended desktop")

        # --- a session ------------------------------------------------------
        m.write(m.sym("ss_modes"), MODE_STARS)
        m.write(m.sym("ss_secs"), SECS_LONG)
        m.write(m.sym("ss_idle"), IDLE_SOON)
        m.key("Space")                          # the idle clock from HERE
        os88marty.guest_sleep(m, 0.4)
        try:
            os88marty.until(m, lambda mm: mm.read(mm.sym("blk_sv"), 1)[0] == 1,
                            "a saver session to start", poll=0.05, guest=20.0)
        except os88marty.MartyError as e:
            sys.exit("dispsaver: no session (%s) - every refusal falls through "
                     "to the blanker, which has always walked every display, "
                     "so this row would prove nothing" % e)
        os88marty.guest_sleep(m, 2.5)

        run0 = stored(m, "herc")
        run1, runmem = rendered(m, d1), stored(m, "cga")
        say("session:  primary %d lit | display 1 renders %d, holds %d"
            % (run0, run1, runmem))

        if not run0 < lit0 // 2:
            fail.append("the PRIMARY did not become a saver: %d lit, was %d"
                        % (run0, lit0))
        if run1:
            fail.append("DISPLAY 1 IS STILL LIT: %d pixels rendered, where a "
                        "gated card renders 0. The second monitor is showing "
                        "the frozen desktop (SPEC.md 79.1.1)" % run1)
        # A TOLERANCE AND NOT AN EQUALITY, because an equality is flaky for
        # a reason worth writing down: measured on the pre-fix kernel, this
        # read 63,999 against 64,000 - ONE pixel, the arrow's own save-under
        # being put back as `ss_set_x` hides it. What the assertion is for is
        # a CLEAR, and a clear takes 64,000 to nearly nothing, so a hundred
        # is far below anything it must catch and far above the cursor.
        if abs(runmem - mem1) > 100:
            fail.append("display 1's FRAMEBUFFER moved: %d, was %d. A gate "
                        "leaves memory alone (SPEC.md 64.3) - this was "
                        "cleared, not gated" % (runmem, mem1))

        # --- and back -------------------------------------------------------
        m.write(m.sym("ss_idle"), IDLE_NEVER)
        m.key("Space")
        try:
            os88marty.until(m, lambda mm: mm.read(mm.sym("blk_on"), 1)[0] == 0,
                            "the wake", poll=0.05, guest=20.0)
        except os88marty.MartyError as e:
            fail.append("it never woke: %s" % e)
        os88marty.guest_sleep(m, 2.5)

        back1 = rendered(m, d1)
        say("woken:    primary %d lit | display 1 renders %d"
            % (stored(m, "herc"), back1))
        if back1 < lit1 // 2:
            fail.append("display 1 did not come back: %d rendered, was %d"
                        % (back1, lit1))

    for f in fail:
        print("  FAIL: " + f)
    if fail:
        return 1
    print("dispsaver: a session darks the second monitor and leaves its "
          "framebuffer alone; both come back on the wake")
    return 0


if __name__ == "__main__":
    sys.exit(main())
