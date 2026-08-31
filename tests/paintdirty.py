#!/usr/bin/env python3
"""DOES PAINT ASK BEFORE IT THROWS A PICTURE AWAY? (SPEC.md 42.16)

    make && python3 tests/paintdirty.py [--machine os8088_5150_herc_gla]

Paint answers SPEC.md 75.1's close negotiation the way Note Pad does: a
picture with unsaved work puts 'Save changes to ...?' up and REFUSES to close.
What it knows it by is a FLAG and not Note Pad's checksum, because the same
Fletcher walk over a 62,720-byte canvas is about half a second spent at the
moment of the click (SPEC.md 42.16).

A flag is only as good as the places that set and clear it, so this drives the
four that matter and reads [pt_dirty] at each:

  OPENED     0 - a fresh blank canvas owes nothing
  MAXIMIZED  0 - AND THIS IS THE ONE WORTH HAVING. A resize really does change
               the document (the BMP that comes out is a different size), so a
               flag set by every mutation would be right to fire and the
               question would then be asked about a blank picture nobody drew
               on. It stays 0 because the resize path reaches none of the seven
               writers - which is a fact about pt_ucopy, not a decision anyone
               enforces, so it is asserted here rather than assumed
  DRAWN      1 - one stroke
  CLOSE BOX  the window is STILL THERE afterwards, and an alert is up

The last is the feature. The three before it are what makes the last one mean
something: a flag stuck at 1 would pass a close test on its own.
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


def _boff(seg, name):
    return ((seg << 4) + dispapps.img_size("paint")
            + dispapps.bss_off("paint", name))


def _bss(m, seg, name, w=2):
    return int.from_bytes(m.read(_boff(seg, name), w), "little")


def _wins(m):
    return dispcp.win_list(m, S)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)

    fails, seen = [], []
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        os88marty.settle(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = _wins(m)[-1]
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
        pw = _wins(m)[-1]
        _wins0 = _wins(m)
        nwin = len(_wins0)

        def note(what):
            seen.append((what, _bss(m, seg, "pt_dirty", 1)))

        note("opened")

        wr = dispcp.win_rect(m, S, pw)                  # maximize and restore
        mo.dblclick(wr[0] + 60, wr[1] + 9)
        m.advance(frames=400)
        m.run()
        os88marty.settle(m)
        big = dispcp.win_rect(m, S, pw)
        mo.dblclick(big[0] + 60, big[1] + 9)
        m.advance(frames=400)
        m.run()
        os88marty.settle(m)
        note("maximized and restored")
        if big[2] == dispcp.win_rect(m, S, pw)[2]:
            fails.append("SETUP: the title double-click did not resize the "
                         "window, so the resize case was never exercised")

        cx0, cy0 = _bss(m, seg, "pt_cx0"), _bss(m, seg, "pt_cy0")
        mo.to(cx0 + 40, cy0 + 30)
        os88marty.settle(m)
        mo._edge(True)
        for _ in range(6):
            m.mouse(dx=8, dy=8, l=True)
            m.advance(frames=3)
            m.run()
        mo._edge(False)
        os88marty.settle(m)
        note("one stroke")

        wr = dispcp.win_rect(m, S, pw)                  # ...and the close box
        mo.click(wr[0] + 8, wr[1] + 9)
        m.advance(frames=400)
        m.run()
        os88marty.settle(m)
        after = _wins(m)
        still = pw in after
        extra = len(after) - nwin

        # --- AND THE ANSWER. The question refusing is half the feature; a
        # Discard that does not close leaves a window nothing can shut.
        # os88ui_arect's own formula, not a measured layout: three buttons of
        # OS88UI_ABW with OS88UI_ABG between them, the row centred.
        closed = None
        if still and extra >= 1:
            al = [w for w in after if w != pw and w not in _wins0][-1:]
            if al:
                ax_, ay_, aw_, _ah = dispcp.win_rect(m, S, al[0])
                row = 3 * (ABW + ABG) - ABG
                left = ax_ + (aw_ - row) // 2
                bx_ = left + (ABW + ABG) + ABW // 2      # button 1 = Discard
                by_ = ay_ + TITLE_H + ABTNY + ABH // 2
                print("   the alert: x=%d y=%d w=%d, Discard at %d,%d"
                      % (ax_, ay_, aw_, bx_, by_))
                mo.click(bx_, by_)
                m.advance(frames=400)
                m.run()
                os88marty.settle(m)
                closed = pw not in _wins(m)

    for (what, d) in seen:
        print("   %-24s pt_dirty=%d" % (what, d))
    print("   after the close box: Paint %s, %d window(s) more than before"
          % ("is still open" if still else "IS GONE", extra))
    if closed is not None:
        print("   after Discard:       Paint %s"
              % ("IS STILL OPEN" if not closed else "is gone"))

    want = {"opened": 0, "maximized and restored": 0, "one stroke": 1}
    for (what, d) in seen:
        if d != want[what]:
            fails.append("[pt_dirty] is %d after %r, wanted %d - SPEC.md 42.16"
                         % (d, what, want[what]))
    if not still:
        fails.append("the close box CLOSED a picture with unsaved work: the "
                     "negotiator did not refuse (SPEC.md 75.1)")
    elif extra < 1:
        fails.append("Paint refused to close but put no alert up, so the "
                     "window can never be closed - worse than not asking")
    elif closed is None:
        fails.append("SETUP: the alert went up but this could not find its "
                     "window to answer it")
    elif not closed:
        fails.append("Discard did not close the picture, so the question is a "
                     "window that cannot be dismissed (SPEC.md 75.3)")
    if fails:
        for f in fails:
            print("paintdirty: %s" % f)
        return 1
    print("paintdirty: PASS - a resize is not a change, a stroke is, and the "
          "close box asks")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
