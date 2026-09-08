#!/usr/bin/env python3
"""Nothing draws or BANKS with the arrow on the glass (SPEC.md 12.8.4, 11.101.2).

    make && python3 tests/gfxlk.py

SPEC.md 7 says two things that only hold together, and the second is what
makes the first load-bearing:

  * "Every task-level drawing burst is wrapped in gfx_lock/gfx_unlock."
  * "Only when the lock is free AND cur_level >= 0 does the ISR itself move
    the cursor (restore save-under at old position, save + draw at new)."

So [gfx_lock_flag] = 0 is not "nobody is drawing" - it is the one state in
which IRQ4 draws. The file-operation progress widget is the one painter in
the machine that draws there (fprog.inc, on every file operation the UI task
runs outside a window callback - a package load, a folder open, a mount), and
for as long as it has existed a fill and the cursor have been racing over
gfx_rect_setup's module scratch, over the Graphics Controller's registers
(cur_draw's precondition is "default GC state on entry") and over the glass.
What it leaves behind is written into WINDOW CONTENT that nothing repaints and
the raise cache then keeps for the session: docs/FIELD-NOTES.md 34, reported
as "the cursor is getting written into save unders".

THE GATE IS A COUNTER IN THE KERNEL, not pixels. `make GFXAUDIT=1` counts the
ISR reaching its draw with [fpg_on] set, which SPEC.md 12.8.4's compare makes
unreachable and which a kernel without it does once per mouse packet for the
length of every file operation. [gfx_aud_def] is the control - the deferrals
that compare bought - so a zero cannot be a session that never armed the
widget. A screendump gate would be far weaker: the DAMAGE needs a packet to
land inside a primitive that runs for about a millisecond, which on real
hardware is often and under MartyPC (injected deltas arrive on frame
boundaries) is essentially never.

The site table is printed rather than asserted, because the widget still draws
unlocked by design - the fix is that the ISR stands off while it does. A new
name in that table is a new unlocked painter, and one the ISR gate does not
cover.

THE SECOND ROW IS THE SAVE-UNDER's (SPEC.md 11.101.2). `wm_show`, `wm_hide`
and `wm_front` are API slots and a package's entry proc runs with the gfx lock
FREE, which is exactly where a package shows the window it just made
(apps/cyclone does). They did not take the lock, so `wm_su_precover`'s bank
read the screen with the arrow standing on it and stored it as the covered
window's content - and it appeared, permanently, the next time that window was
uncovered. `[gfx_aud_bank]` counts every `gfx_save` taken with `cur_level` >= 0,
which is that defect at its cause rather than at its symptom, and the scenario
below is the field's own: park the pointer where the window is going to land,
open it WITHOUT moving the hand, close it, and compare the list against a
repaint of itself.

It builds its own kernel and puts a plain one back afterwards, because the
knob moves every symbol and the rest of the suite reads build/.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

import os88build                                            # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
from os88geom import TITLE_H                                # noqa: E402

DEFS = ("GFXAUDIT",)
N = 12                                                      # GFXAUD_N
PARK = (560, 400)                   # bare desktop, well off every window
FM_BTN_W, FM_BTN_H = 63, 14         # tests/fmbtn.py's, for the Refresh button
FM_REFRESH_BACK = 67
fails = []


def brect(w):
    x1 = w[0] + 1 + w[2] - 2 - 1 - FM_REFRESH_BACK
    y1 = w[1] + TITLE_H + 2
    return (x1, y1, x1 + FM_BTN_W - 1, y1 + FM_BTN_H - 1)


def crop(m, rect):
    """An INCLUSIVE rect, which is what this file's rects are - and the size,
    because every caller here goes on to write a PNG of it.  The pixels come
    from tools/os88marty.py, shared with tests/ftpdflick.py and ftpdfocus.py.
    """
    x1, y1, x2, y2 = rect
    w, h = x2 - x1 + 1, y2 - y1 + 1
    return os88marty.crop_rgb(m, x1, y1, w, h), (w, h)


def S(name):
    return os88sym.linear(name, DEFS)


def check(name, cond, note=""):
    print("  [%s] %s %s" % ("PASS" if cond else "FAIL", name, note))
    if not cond:
        fails.append(name)


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def word(m, name):
    return u16(m.read(S(name), 2))


def sites(m):
    """Every call site that drew with the lock free, name+offset and count."""
    syms, sect = os88sym.syms(DEFS), os88sym.sections(DEFS)
    text = sorted((v, k) for k, v in syms.items() if sect.get(k) == ".text")
    ra, cnt = m.read(S("gfx_aud_ra"), N * 2), m.read(S("gfx_aud_cnt"), N * 2)
    out = []
    for i in range(N):
        a, c = u16(ra, i * 2), u16(cnt, i * 2)
        if not a:
            continue
        best, lo = "?", 0
        for v, k in text:
            if v <= a:
                best, lo = k, v
            else:
                break
        out.append("%s+0x%x x%d" % (best, a - lo, c))
    return out


def main():
    # THE AUDIT KERNEL BUILDS IN A TREE OF ITS OWN (tools/os88build.py). It
    # used to be `make GFXAUDIT=1` into build/ with a `finally` putting the
    # plain kernel back - two builds, and in between the shared tree held a
    # kernel nobody else had asked for, which is what made this row
    # `builds=True` and unable to share the box.
    t = os88build.tree("GFXAUDIT=1").apply()
    settle = os88marty.settle
    with os88marty.launch(t.img("os8088-360.img"),
                          apps=t.img("apps360.img"),
                          machine="os8088_xt_vga") as m:
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_drive(m, mo, S, settle, "B")        # a mount and a walk
        disk = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, settle, wx, wy, "GAMES")

        wx, wy, ww, wh = dispcp.win_rect(m, S, disk)
        entry = dispcp.row_of(m, S, "MINES.O88")
        row = dispcp.scroll_to(m, mo, S, settle, wx, wy, entry)
        rx, ry = dispcp.row_xy(wx, wy, row)
        mo.dblclick(rx, ry, settle=0)

        # THE HAND, MOVING THROUGH THE WAIT - it is what turns the exposure
        # into a defect, and what this counts. But it only counts while the
        # CHROME IS UP: [gfx_aud_def] is the ISR reaching its draw with
        # [fpg_on] set, so a hand that moves after the load has finished
        # produces zero deferrals and the control below fires.
        #
        # THAT IS WHY THIS ROW WAS INTERMITTENT - 2 runs in 5, measured, and
        # on the control leg rather than the assertion. A fixed 40 packets at
        # two frames each is ~1.3 guest seconds racing a package load, and
        # sometimes the load won. Waiting for [fpg_on] and then moving WHILE
        # it is set makes the row establish its own precondition instead of
        # hoping for it; the cap is a bound, not a plan.
        os88marty.until(m, lambda mm: word(mm, "fpg_on"),
                        "the progress chrome to come up", poll=0.02, guest=30.0)
        i = 0
        while word(m, "fpg_on") and i < 600:
            m.mouse(dx=(3 if i % 8 < 4 else -3), dy=(2 if i % 6 < 3 else -2))
            m.advance(frames=2)
            m.run()
            i += 1
        print("  moved the hand %d time(s) while the chrome was up" % i)
        m.advance(frames=200)
        m.run()

        print("  drew with the lock free: %d call(s) - %s"
              % (word(m, "gfx_aud_tot"), ", ".join(sites(m)) or "none"))
        print("  ISR cursor moves: %d, of them deferred by the gate: %d"
              % (word(m, "gfx_aud_mv") + word(m, "gfx_aud_def"),
                 word(m, "gfx_aud_def")))

        check("the widget went up", word(m, "gfx_aud_tot") > 0
              or word(m, "gfx_aud_def") > 0,
              "a session that never armed it proves nothing")
        # **THE CONTROL IS "THE ISR REACHED ITS DRAW WITH THE WIDGET UP", not
        # "it was DEFERRED".** Those were the same thing when this was
        # written: SPEC.md 12.8.4's compare made the draw unreachable while
        # [fpg_on] was set, so every ISR arrival in the window was a deferral
        # and counting them proved the session had armed the widget.
        #
        # SPEC.md 7.4 REVERSED THAT ON PURPOSE. The mouse ISR now draws the
        # arrow through a disk transfer - that is the whole feature, and
        # `NOCURDISK=1` is the knob that puts the old freeze back - so an
        # arrival with the lock free is a DRAW and not a deferral. Deferrals
        # now need the gfx lock to be held at the instant the packet lands,
        # which is a race SPEC.md 7.4.2 made narrow by design: measured, 16
        # hand moves inside the freeze produced 15 ISR moves and 0 deferrals,
        # and this row was INTERMITTENT 2 runs in 5 on exactly this leg.
        #
        # The sum is what the docstring above actually asks for - "a zero
        # cannot be a session that never armed the widget" - and it is
        # unambiguous under either kernel.
        check("the gate fired at all",
              word(m, "gfx_aud_mv") + word(m, "gfx_aud_def") > 0,
              "%d ISR arrival(s) in the window, %d of them deferred - the "
              "control for the row below"
              % (word(m, "gfx_aud_mv") + word(m, "gfx_aud_def"),
                 word(m, "gfx_aud_def")))
        check("no ISR draw with the widget up", word(m, "gfx_aud_gate") == 0,
              "%d - SPEC.md 12.8.4's compare is what makes this 0"
              % word(m, "gfx_aud_gate"))
        check("no ISR draw inside a primitive", word(m, "gfx_aud_race") == 0,
              "%d - the collision caught in the act; 0 here is weak "
              "evidence under MartyPC, see the header"
              % word(m, "gfx_aud_race"))
        check("no save-under banked the arrow", word(m, "gfx_aud_bank") == 0,
              "%d gfx_save calls with the cursor still on the glass"
              % word(m, "gfx_aud_bank"))

        # --- SPEC.md 11.101.2: the window that opens ABOVE the pointer ----
        # apps/cyclone shows its own window from its entry proc, where the
        # gfx lock is free (SPEC.md 20.2). The hand never moves: the row is
        # SELECTED, the pointer is parked where the window is about to
        # land, and Enter opens it - which is the field's own recipe.
        wx, wy, ww, wh = dispcp.win_rect(m, S, disk)
        entry = dispcp.row_of(m, S, "CYCLONE.O88")
        row = dispcp.scroll_to(m, mo, S, settle, wx, wy, entry)
        rx, ry = dispcp.row_xy(wx, wy, row)
        mo.dblclick(rx, ry)                 # once, to learn where it lands
        m.advance(frames=300)
        m.run()
        opened = [w for w in dispcp.win_list(m, S) if w != disk]
        if not opened:
            sys.exit("gfxlk: CYCLONE did not open")
        nx, ny, nw, nh = dispcp.win_rect(m, S, opened[-1])
        mo.click(nx + 8, ny + 9)            # its close box
        m.advance(frames=90)
        m.run()
        px, py = max(rx, nx + 12), max(min(ry, ny + nh - 12), ny + 12)
        rect = (wx + 2, wy + TITLE_H + 20, wx + ww - 4, wy + wh - 24)

        mo.click(rx, ry)                    # SELECT the row - no launch
        settle(m)
        mo.to(px, py)                       # ...and park ON the landing site
        settle(m)
        m.key("Enter")                      # fm_onkey opens it, and the
        m.advance(frames=300)               # hand never moves again
        m.run()
        opened = [w for w in dispcp.win_list(m, S) if w != disk]
        if not opened:
            sys.exit("gfxlk: Enter opened nothing")
        nx, ny, nw, nh = dispcp.win_rect(m, S, opened[-1])
        mo.click(nx + 8, ny + 9)
        m.advance(frames=90)
        m.run()
        mo.to(*PARK)
        settle(m)
        before, dim = crop(m, rect)
        b = brect((wx, wy, ww, wh))
        mo.click((b[0] + b[2]) // 2, (b[1] + b[3]) // 2)    # Refresh
        settle(m)
        # ...AND SCROLLED BACK TO WHERE `before` WAS TAKEN. Refresh re-reads
        # the directory and returns the view to the TOP, so without this the
        # two captures are the same files at different offsets and every row
        # differs - 3971 pixels of "the list did not survive" about a list
        # that survived perfectly. It only started mattering when GAMES grew
        # past the window: scroll_to was a no-op while CYCLONE.O88 was
        # visible from the top, and PACMAN.O88 and SKIES.O88 pushed it down.
        row2 = dispcp.scroll_to(m, mo, S, settle, wx, wy, entry)
        settle(m)
        if row2 != row:
            sys.exit("gfxlk: the refreshed view puts CYCLONE.O88 on row %d "
                     "and it was on row %d - the two captures would be of "
                     "different scroll positions" % (row2, row))
        mo.click(rx, ry)                    # ...and the same row selected,
        settle(m)                           # or the highlight is the diff
        mo.to(*PARK)
        settle(m)
        after, _ = crop(m, rect)
        n = sum(1 for i in range(0, min(len(before), len(after)), 3)
                if before[i:i + 3] != after[i:i + 3])
        if n and os.environ.get("GFXLK_DUMP"):
            w = rect[2] - rect[0]
            for nm, buf in (("before", before), ("after", after)):
                os88marty.write_png_rgb("/tmp/gfxlk-%s.png" % nm, w,
                                        len(buf) // (3 * w), buf)
            print("  dumped /tmp/gfxlk-before.png and -after.png")
        check("the list survived a window opening over the pointer", n == 0,
              "%d pixels the list has that a repaint of it does not" % n)
        check("...and no bank saw the arrow", word(m, "gfx_aud_bank") == 0,
              "%d" % word(m, "gfx_aud_bank"))

    print("\ngfxlk: %s" % ("FAILED: " + ", ".join(fails) if fails else "all pass"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
