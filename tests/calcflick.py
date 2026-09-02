#!/usr/bin/env python3
"""Does the Calculator FLASH? (PERFORMANCE.md Part 3.1, SPEC.md 65.4)

    make && python3 tests/calcflick.py [--machine os8088_xt_vga]

A "double-draw flash" - anything erased and then drawn again inside one lock
hold - is one of the three defects a fast emulator cannot show: the work is
identical, only the glass differs. The instrument samples the card's RENDERED
framebuffer once per DISPLAYED frame, which is how often an eye samples it,
and counts the pixels whose value before an operation and after it are the
SAME but which showed something else in between. That count is the flash, as
arithmetic.

Four operations are priced, and each is a place the obvious implementation
would have flashed:

  a DIGIT          - a fill of the field then the glyphs would blank it
  an OPERATOR      - the same, on the second field as well
  EQUALS           - which also moves the whole history band
  FOLDING the pane - a resize, so a full repaint is legitimate here and the
                     number is reported rather than asserted

`transient` is the flash; `changed` is the honest before/after difference,
which a digit is entitled to have. `settled` must be true or every count was
measured against a moving target.
"""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88sym, dispcp

S = os88sym.linear
MACHINE = "os8088_5150_cga_gla"
REF = "--ref" in sys.argv          # `make calcref`: the build that letters
                                   # every history row instead of scrolling
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]
APPS = "build/calcref.img" if REF else "build/apps360.img"
PKG = "CALCREF.O88" if REF else "CALC.O88"   # the reference disk holds
                                            # CALCREF alone

# '+' is the NUMPAD plus, not Shift+Equal, and that is not cosmetic: a key
# injected while the machine is PAUSED lands in the emulator's queue with no
# time between the events, and a three-event shifted key then did not arrive
# at all - measured as an operator that changed 0 pixels, which reads as the
# app not drawing rather than as the key never reaching it. Scancode 0x4E is
# unshifted and NumLock does not touch it.
PLAIN = {".": "Period", "/": "Slash", "-": "Minus", "=": "Equal",
         "+": "NumpadAdd"}
SHIFTED = {"*": "Digit8", "%": "Digit5"}


def press(m, ch):
    if ch in SHIFTED:
        m.key("ShiftLeft", down=True, up=False)
        m.key(SHIFTED[ch])
        m.key("ShiftLeft", down=False, up=True)
    elif ch in PLAIN:
        m.key(PLAIN[ch])
    else:
        m.type_text(ch)


def typed(m, text):
    for ch in text:
        press(m, ch)
        time.sleep(0.15)
    os88marty.settle(m)


rows = []
with os88marty.launch("build/os8088-360.img", apps=APPS,
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    w = dispcp.win_list(m, S)
    wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
    w = dispcp.win_list(m, S)
    wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, PKG)
    time.sleep(2)
    slot = dispcp.win_list(m, S)[-1]
    print("CALC%s at %s on %s\n"
          % (" (no-scroll reference)" if REF else "",
             dispcp.win_rect(m, S, slot), MACHINE))

    # The pointer well CLEAR of the window, to its LEFT: the arrow is drawn
    # into the framebuffer (SPEC.md 7.1), so every lock hold takes it off and
    # puts it back and that is a flash of the harness's own making. Parking it
    # to the RIGHT is not enough - the instrument answers ONE bounding box per
    # frame, so a frame holding both the arrow and something of ours comes
    # back as one rect spanning them, and 36 pixels of cursor then read as
    # ours. To the left of x = W_X there is nothing to merge with.
    kx, ky, kw, kh = dispcp.win_rect(m, S, slot)   # NOT `ch`: the
    park = (kx - 60, ky + 4)                       # `for ch in keys`
    mo.to(*park)                                   # loop below shadows it
    os88marty.settle(m)

    typed(m, "c12")

    def measure(what, keys, frames=50):
        os88marty.settle(m)
        m.pause()
        for ch in keys:
            press(m, ch)
        r = m.flicker(frames=frames)
        m.run()
        os88marty.settle(m)
        moved = max(f["changed"] for f in r["per_frame"])
        # THE FLASH THAT COUNTS IS THE ONE INSIDE OUR WINDOW. The mouse arrow
        # is drawn INTO the framebuffer (SPEC.md 7.1) and gfx_lock takes it off
        # for the length of the hold, so any hold longer than one displayed
        # frame shows the pointer's own 8x12 cell as transient - measured here
        # at exactly 36 of its 96 pixels, at the parked position, outside the
        # window. That is the lock's documented cost and every app in this
        # system pays it; what this test is about is whether the CALCULATOR
        # puts one of its own pixels on the glass twice.
        mine, ptr, mbb = 0, 0, None
        for f in r["per_frame"]:
            b = f["bbox"]
            if not b:
                continue
            if b[0] > kx + kw or b[2] < kx or b[1] > ky + kh or b[3] < ky:
                ptr = max(ptr, f["transient"])
            elif f["transient"] > mine:
                mine, mbb = f["transient"], b
        # HOW LONG THE PICTURE WAS MOVING, which is the number the scroll is
        # actually for. PERFORMANCE.md's first defect is a VISIBLE REDRAW - on
        # a 4.77MHz machine you watch it happen - and the flicker instrument
        # prices the second one (the double-draw flash) and cannot see it. The
        # span between the first and last frame that changed anything is the
        # first defect as arithmetic.
        mv = [f for f in r["per_frame"] if f["changed"]]
        span = ((mv[-1]["cycles"] - mv[0]["cycles"]) / (r["cpu_mhz"] * 1000.0)
                if len(mv) > 1 else 0.0)
        rows.append((what, moved, mine, ptr, mbb, span, r["moved_frames"]))
        print("%-22s changed %5d over %-2d frame(s) = %6.1f ms   FLASH %4d%s%s"
              % (what, moved, r["moved_frames"], span, mine,
                 "  at %s" % mbb if mbb else "",
                 "   (+%d cursor)" % ptr if ptr else ""))
        if not r["settled"]:
            print("      (NOT SETTLED - every count above was measured "
                  "against a moving target)")

    measure("a digit", "3")
    measure("an operator", "+")
    measure("a digit again", "7")
    measure("equals", "=")
    # THE SHARP ROW. Every key above flashes its own pad key for CAL_FLASH
    # ticks (SPEC.md 65.9), which is a deliberate 51x14 rect on the glass and
    # `q` is CK_SQRT, which has NO key on the pad at all, so it changes the
    # display field and flashes nothing - putting that field's
    # compose-against-a-shadow path (65.4) back under the arrow's own 96-pixel
    # bound, where it was before the flash existed.
    #
    # It is `q` and NOT `n`: SPEC.md 65.9 names "Q, R, %, N" as the verbs with
    # no pad key and is WRONG about N - cal_keytab carries `cal_k_neg, CK_NEG`,
    # so a typed `n` presses the plus/minus key like any other and measures
    # 588 transient px, exactly as a digit does. Q, R and % really have none.
    measure("sqrt: no pad key", "q")
    typed(m, "h")                       # ...and again with the pane open
    time.sleep(1.5)
    os88marty.settle(m)
    mo.to(*park)
    os88marty.settle(m)
    typed(m, "c45")
    measure("a digit, pane open", "6")
    typed(m, "x2")                      # ONE key per capture: the instrument
                                        # prices ONE operation, and three of
                                        # them in one window read as a flash
                                        # wherever the display legitimately
                                        # showed 45, then 2, then 90
    measure("equals, pane open", "=", frames=70)
    measure("folding the pane", "h", frames=90)

print()
# WHAT THIS CAN AND CANNOT SEPARATE, because the difference decides the bound.
# The instrument answers ONE bounding box per frame, so a frame in which both
# the mouse arrow and something of ours moved comes back as a single rect
# spanning them and the two cannot be told apart - and whether they land in
# the same frame is timing, so the same operation reads clean on one run and
# not on the next. What it CAN say is how MANY pixels were transient, and the
# arrow is 8 x 12: anything at or under 96 is inside what the cursor alone
# accounts for, and a field of 13 glyph cells drawn twice would be 832.
#
# So the bound is the arrow's own cell, and it is not a fudge - it is the
# largest number the harness can produce on its own. What rules out the
# in-between sizes is not this test but tests/dispcalc.py, which finds 0
# differing pixels against a forced full repaint on all three adapters.
#
# A SCROLL AND A RESIZE ARE ENTITLED TO A TEAR; NOTHING ELSE IS. Both move
# more pixels than a frame can hold. The scroll is measured against the
# reference build (`make calcref`, then --ref), which letters every row
# instead: SPEC.md 65.4.1 is that comparison.
CUR_CELL = 8 * 12
# ...AND A TYPED KEY IS ENTITLED TO ONE PAD KEY (SPEC.md 65.9), which landed
# after this test was written and is why it started failing: `cal_flash`
# presses the key the keystroke came from and `cal_ontimer` releases it
# CAL_FLASH = 3 ticks later. That is 51x14 = 714 pixels of deliberate
# inversion, and measured it reads 600-624 with the flash's 165 ms showing up
# as the row's "moving" span - which is CAL_FLASH exactly.
#
# The bound below is therefore NOT sharp for those rows: one pad key plus the
# arrow is 810, and this test's own defect signal - a 13-cell field erased and
# lettered again - is 832. Twenty-two pixels of margin is not a gate, so the
# gate is the "sqrt: no pad key" row instead, which changes the same display
# field with no key to flash and is held to the arrow's cell. What rules out
# the in-between sizes for the rest is tests/dispcalc.py, which finds 0
# differing pixels against a forced full repaint on all three adapters.
CAL_BTN_W, CAL_BTN_H = 51, 14           # apps/calc/calc.asm's own constants
KEY_FLASH = CAL_BTN_W * CAL_BTN_H + CUR_CELL
EXEMPT = ("equals, pane open", "folding the pane")
FLASHES = ("a digit", "an operator", "a digit again", "equals",
           "a digit, pane open")
def bound(name):
    return KEY_FLASH if name in FLASHES else CUR_CELL
bad = [r for r in rows if r[2] > bound(r[0]) and r[0] not in EXEMPT]
for r in rows:
    tag = ""
    if r[0] in EXEMPT:
        tag = "   <- a scroll/resize: entitled to one"
    elif r[2] > bound(r[0]):
        tag = "   <- MORE than %d px can account for" % bound(r[0])
    elif r[0] in FLASHES and r[2] > CUR_CELL:
        tag = "   <- one pad key flashing (SPEC.md 65.9)"
    elif r[2]:
        tag = "   <- inside the arrow's 8x12 cell"
    print("   %-22s %6.1f ms moving, %4d transient px%s" % (r[0], r[5], r[2], tag))
if bad:
    print("\ncalcflick: FAIL - %s put more on the glass than the cursor can "
          "account for" % ", ".join(r[0] for r in bad))
    sys.exit(1)
print("\ncalcflick: %s - PASS"
      % ("the no-scroll reference: compare its `equals, pane open` time "
         "against the shipped build's" if REF else
         "no operation drawn as an opaque run puts a pixel on the glass "
         "twice; the scroll and the resize do, and are measured"))
