#!/usr/bin/env python3
"""Reload's predicate and its action have to be the same question
(BROWSER-PLAN 14.5).

    make && make browsertest && python3 tests/brreload.py [--adapter cga|herc]

Reported from the field as three faults after a screen saver - the address bar
came back with no text, a later backspace left caret copies behind, and the
Reload button had greyed itself - and the repro is two clicks: open the
browser and press Reload before fetching anything.

**IT IS ONE DESYNC.** `br_okrel` greys Reload on whether the BAR holds
anything; the button's action was `br_hgo`, which goes to history entry
`[br_histi]` - a different question, with no answer until something has been
fetched. So `br_hgo` copied a ZEROED history slot over `br_ubuf`, which is the
location bar's own buffer, and `br_go` refused the empty URL before reaching
the `os88line_set` that would have told the control its text had changed. The
bar was then claiming `LN_LEN` characters that were no longer there:

  * its opaque `font_run` stops at the NUL now at offset 0, so no text draws;
  * `br_okrel` reads that same byte, so Reload greys itself;
  * the cells between the NUL and `LN_LEN` are painted by neither the run nor
    the strip fill past it, so every caret drawn in them stays.

The saver is not the bug - it is what forced the full repaint that made the
stale pixels go. So this drives the CLICK and asserts the state directly, and
runs the saver afterwards only to prove the repaint is clean.
"""
import argparse
import sys
import time

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import dispcp                                          # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
from brclick import browser_syms, MACHINE              # noqa: E402

S = os88sym.linear
u16 = lambda b: b[0] | (b[1] << 8)                     # noqa: E731
LN_X1, LN_Y1, LN_X2, LN_Y2 = 0, 2, 4, 6
LN_LEN, LN_CAR = 12, 14
SEA = 8                                                # ss_modes' sea-life bit


def say(*a):
    print(*a)
    sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="herc", choices=sorted(MACHINE))
    a = ap.parse_args()
    card = "herc" if a.adapter == "herc" else "cga"
    sy, fails = browser_syms(), []

    with os88marty.launch("build/os8088-360.img", apps="build/brtest360.img",
                          machine=MACHINE[a.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        before = dispcp.win_list(m, S)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "DEMO.HTM")
        wins = dispcp.win_list(m, S)
        if len(wins) <= len(before):
            sys.exit("brreload: DEMO.HTM opened no window")
        rec = m.read(S("wm_wins") + wins[-1] * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        rw = lambda n: u16(m.readseg(pseg, sy[n], 2))   # noqa: E731

        def state():
            txt = bytes(m.readseg(pseg, sy["br_ubuf"], 64)).split(b"\0")[0]
            ln = u16(m.readseg(pseg, sy["br_loc"] + LN_LEN, 2))
            return txt.decode("latin-1"), ln

        def bar_rect():
            return [u16(m.readseg(pseg, sy["br_loc"] + o, 2))
                    for o in (LN_X1, LN_Y1, LN_X2, LN_Y2)]

        def frame():
            # PARK THE POINTER FIRST: the arrow is drawn INTO the
            # framebuffer, so a frame taken with it over the button compares
            # a cursor against a button and fails for the wrong reason.
            mo.to(4, 190)
            os88marty.settle(m)
            w, h, rows = m.vram(card)
            return w, h, [bytes(r) for r in rows]

        def region(fb, r):
            return [bytes(fb[y][r[0]:r[2] + 1]) for y in range(r[1], r[3] + 1)]

        txt0, len0 = state()
        say("open:         bar=%r LN_LEN=%d" % (txt0, len0))
        if txt0 != "http://" or len0 != 7:
            sys.exit("brreload: the bar did not open holding 'http://' - "
                     "nothing below would mean what it says")
        bar = bar_rect()
        r3 = [u16(m.readseg(pseg, sy["br_r3"] + i * 2, 2)) for i in range(4)]
        _, _, fb0 = frame()
        bar0, btn0 = region(fb0, bar), region(fb0, r3)
        ink0 = sum(1 for row in bar0 for px in row if not px)
        say("bar ink at open: %d" % ink0)

        # --- the repro: click Reload with nothing fetched ----------------
        mo.click((r3[0] + r3[2]) // 2, (r3[1] + r3[3]) // 2)
        os88marty.settle(m)
        time.sleep(1)
        os88marty.settle(m)
        txt1, len1 = state()
        say("after Reload: bar=%r LN_LEN=%d" % (txt1, len1))

        if len1 != len(txt1):
            fails.append("the bar says it holds %d characters and its buffer "
                         "holds %d (%r) - Reload wrote the control's own "
                         "buffer behind its back, and the cells past the NUL "
                         "are painted by nothing" % (len1, len(txt1), txt1))
        if txt1 != txt0:
            fails.append("Reload changed the bar from %r to %r - it re-fetches "
                         "what the bar says and must not empty it"
                         % (txt0, txt1))

        # The bar legitimately LOSES ITS CARET here - a click on the chrome
        # takes focus off it (SPEC.md 71.7) - so the picture may differ by
        # that one bar. What may not happen is the TEXT going: an emptied
        # buffer draws nothing at all, which is a collapse of the ink and not
        # a difference of eight pixels.
        _, _, fb1 = frame()
        bar1, btn1 = region(fb1, bar), region(fb1, r3)
        ink1 = sum(1 for row in bar1 for px in row if not px)
        say("bar ink after Reload: %d" % ink1)
        if ink1 < ink0 - 12:
            fails.append("the location bar went from %d ink pixels to %d "
                         "across a Reload click - its text stopped being "
                         "drawn, which is an emptied buffer and not a lost "
                         "caret" % (ink0, ink1))
        if btn1 != btn0:
            fails.append("the Reload button's pixels changed across its own "
                         "click - it greyed itself out")

        # --- and the saver, which is what made the field notice ----------
        m.write(m.sym("ss_modes"), bytes([SEA]))
        m.write(m.sym("ss_secs"), b"\xff")
        m.write(m.sym("ss_idle"), b"\x1c\x00")
        t = time.time()
        while time.time() - t < 90 and m.read(m.sym("blk_sv"), 1)[0] != 1:
            time.sleep(0.2)
        if m.read(m.sym("blk_sv"), 1)[0] != 1:
            fails.append("the saver never started - the repaint half of this "
                         "row did not run")
        else:
            time.sleep(2)
            os88marty.no_saver(m)
            m.key("Escape")
            os88marty.settle(m)
            time.sleep(1)
            os88marty.settle(m)
            txt2, len2 = state()
            say("after wake:   bar=%r LN_LEN=%d" % (txt2, len2))
            # ...and the REPAINT half, against the frame taken just before
            # the saver rather than against the focused one at open: both of
            # these are the same unfocused state, so they must be identical
            # pixel for pixel.
            _, _, fb2 = frame()
            if region(fb2, bar) != bar1:
                fails.append("the location bar did not come back as it was "
                             "after the saver: its pixels differ from the "
                             "frame taken immediately before it")
            if region(fb2, r3) != btn1:
                fails.append("the Reload button did not come back as it was "
                             "after the saver")

            # --- the caret half: empty the bar and count what is left ----
            # A clean bar holds ONE 1px caret bar, 8 rows tall. Every caret
            # the redraw failed to erase is another 8 pixels of ink, which is
            # exactly what the field saw as "cursor copies".
            # **THE CARET IS WALKED RIGHTWARD, not backspaced.** A backspace
            # shrinks LN_LEN, so the strip fill past the text moves LEFT and
            # covers the cell the caret just left - it cannot show this. A
            # click further along moves the caret right INTO cells the redraw
            # is not painting, and each one it leaves behind stays.
            ymid = (bar[1] + bar[3]) // 2
            for cell in (1, 3, 5):
                mo.click(bar[0] + 4 + cell * 8, ymid)
                os88marty.settle(m)
            _, _, fbs = frame()
            inks = sum(1 for y in range(bar[1] + 1, bar[3])   # INTERIOR only:
                       for px in fbs[y][bar[0] + 1:bar[2]]    # the rect's own
                       if not px)                             # frame is ~1010
            say("bar ink (interior) after clicking the caret along it: %d"
                % inks)
            if inks < 60:
                fails.append("the bar holds %d ink pixels after the caret was "
                             "clicked along it - its text is gone and what is "
                             "left is caret bars in cells the redraw does not "
                             "paint (the field's `cursor copies`)" % inks)
            m.key("End")
            os88marty.settle(m)
            for _ in range(len2 + 2):
                m.key("Backspace")
                os88marty.settle(m)
            txt3, len3 = state()
            say("after %d backspaces: bar=%r LN_LEN=%d"
                % (len2 + 2, txt3, len3))
            if len3 != 0 or txt3 != "":
                fails.append("backspacing past the start left the bar at %r/%d"
                             % (txt3, len3))
            _, _, fb3 = frame()
            ink = sum(1 for row in region(fb3, bar) for px in row if not px)
            # the frame itself is the rect's border; count only the interior
            inner = [bytes(fb3[y][bar[0] + 1:bar[2]])
                     for y in range(bar[1] + 1, bar[3])]
            ink = sum(1 for row in inner for px in row if not px)
            say("ink inside the emptied bar: %d pixel(s)" % ink)
            if ink > 12:
                fails.append("an emptied location bar has %d ink pixels "
                             "inside it - one caret is 8, so the rest are "
                             "caret copies the redraw never erased" % ink)

    for f in fails:
        print("FAIL:", f)
    print("brreload: %d assertion(s) failed" % len(fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
