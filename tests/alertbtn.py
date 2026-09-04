#!/usr/bin/env python3
"""DOES THE STANDARD ALERT'S BUTTON ROW BEHAVE? (SPEC.md 75.3.0)

    make && python3 tests/alertbtn.py [--machine os8088_xt_vga]

Two defects reported against Paint's 'Save changes?' and both of them the
SHARED control's rather than Paint's, which is the point of the control being
shared - in both directions, so this row is os88ui's and not Paint's. Paint is
only the alert that is easiest to raise.

  ONE PRESS IS ONE BUTTON    os88ui_adn redrew the whole ROW for a state change
                             that can only touch two buttons, so a press on an
                             OS88UI_ASAVE alert lettered three where one was
                             needed - PERFORMANCE.md rule 1, in the control
                             every package's dialog goes through.

  IT TRACKS                  the alert installed W_ONMOUSEUP and nothing
                             between the press and the release, so a button
                             held and dragged off stayed filled. SPEC.md
                             13.8.2 built W_ONDRAG for exactly this: the
                             un-fill is what says the gesture is cancelled
                             BEFORE the finger commits.

[os88ui_adown] is the assertion for the second - 0xFF is "none down" - because
it is the state both the drawing and the release read, so a button that looks
right by luck still fails here.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(HERE)
S = os88sym.linear
TITLE_H = 18                    # kernel.asm; the rest are os88ui.inc's own
ABW, ABG, ABH, ABTNY = 72, 12, 13, 46


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)

    fails = []
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        os88marty.settle(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        rows = [r[0] for r in dispcp.listing(m, S)]
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                               rows.index("PAINT.O88"))
        x, y = dispcp.row_xy(wx, wy, row)
        mo.dblclick(x, y)
        m.advance(frames=250)
        m.run()
        os88marty.no_saver(m)
        seg = dispapps.pkg_seg(m, 0)[1]
        pw = dispcp.win_list(m, S)[-1]
        pm = dispapps._map("paint")
        base = seg << 4

        def boff(n):
            return ((seg << 4) + dispapps.img_size("paint")
                    + dispapps.bss_off("paint", n))

        sfmt = m.read(boff("pt_sfmt"), 1)[0]
        print("   [pt_sfmt] at launch = %d   (SPEC.md 42.16.1: 1 = GIF)" % sfmt)
        if sfmt != 1:
            fails.append("a new canvas defaults to format %d, not GIF - "
                         "SPEC.md 42.16.1" % sfmt)

        cx0 = int.from_bytes(m.read(boff("pt_cx0"), 2), "little")
        cy0 = int.from_bytes(m.read(boff("pt_cy0"), 2), "little")
        mo.to(cx0 + 30, cy0 + 25)                   # ink, so the close ASKS
        os88marty.settle(m)
        mo._edge(True)
        for _ in range(4):
            m.mouse(dx=8, dy=6, l=True)
            m.advance(frames=3)
            m.run()
        mo._edge(False)
        os88marty.settle(m)
        wr = dispcp.win_rect(m, S, pw)
        mo.click(wr[0] + 8, wr[1] + 9)
        m.advance(frames=400)
        m.run()
        os88marty.settle(m)
        al = [w for w in dispcp.win_list(m, S) if w not in (pw, disk)]
        if not al:
            print("alertbtn: SETUP - no alert came up to press")
            return 1
        ax_, ay_, aw_, _h = dispcp.win_rect(m, S, al[-1])
        rowx = ax_ + (aw_ - (3 * (ABW + ABG) - ABG)) // 2
        by = ay_ + TITLE_H + ABTNY + ABH // 2
        disc = rowx + (ABW + ABG) + ABW // 2        # button 1 = Discard
        DOWN = base + pm["os88ui_adown"]
        BTN = base + pm["os88ui_btn"]

        def counted(label, act):
            """os88ui_btn calls over one gesture, and the state it left.

            THE COUNTER IS os88marty.bp_count NOW, and it used to be this loop
            written out here - counting any state that was not "running" and
            counting each report rather than each stop. Both are wrong and
            both inflate: the driving thread's own advance(frames=) pauses the
            guest, and a resume has not always landed by the next status().
            This row asserts an EXACT count of 1, so either was a spurious
            failure of a control that was behaving. Its docstring is the
            account; tests/alertanim.py is where they were measured.
            """
            n = os88marty.bp_count(m, BTN, act)
            d = m.read(DOWN, 1)[0]
            print("   %-24s %d os88ui_btn call(s), [os88ui_adown] = %d"
                  % (label, n, d))
            return n, d

        n, d = counted("press on Discard",
                       lambda: (mo.to(disc, by), mo._edge(True)))
        if d != 1:
            fails.append("the press did not arm button 1 (adown = %d), so the "
                         "gesture under test never started" % d)
        elif n != 1:
            fails.append("one press cost %d os88ui_btn calls: os88ui_adn is "
                         "redrawing buttons whose state did not change - "
                         "SPEC.md 75.3.0" % n)
        _, d = counted("drag OFF it, held",
                       lambda: (m.mouse(dx=0, dy=-30, l=True),
                                m.advance(frames=8), m.run()))
        if d != 0xFF:
            fails.append("the button stayed DOWN (adown = %d) with the pointer "
                         "dragged off it: the alert is not tracking - SPEC.md "
                         "75.3.0 / 13.8.2" % d)
        _, d = counted("drag back ON, held",
                       lambda: (m.mouse(dx=0, dy=30, l=True),
                                m.advance(frames=8), m.run()))
        if d != 1:
            fails.append("the button did not come back DOWN (adown = %d) with "
                         "the pointer back on it - SPEC.md 75.3.0" % d)
        mo._edge(False)
        os88marty.settle(m)

    if fails:
        for f in fails:
            print("alertbtn: %s" % f)
        return 1
    print("alertbtn: PASS - one press draws one button, and it tracks off and "
          "back on")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
