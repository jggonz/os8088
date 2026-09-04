#!/usr/bin/env python3
"""Can Minesweeper's bottom row be PLAYED on a CGA? (SPEC.md 11.93)

    make && python3 tests/dispmine.py
    make KEEPH=0 && python3 tests/dispmine.py --expect-cut     # the A/B

`mn_tpl` is 146x183 and a CGA's desktop band is 155 rows, so before `WF_KEEPH`
`wm_fit` shortened the frame to fit above the dock. The board did not shorten
with it - `mn_draw_board` draws all nine rows and the `gfx_*` primitives clip
to the SCREEN - so the bottom two rows were drawn through the bottom of the
frame and onto the dock strip, where `wm_hit` (which tests the RECORD) says
no window is. A press there fell past `.on_desktop` into `dock_click`.

THAT IS WHY THIS IS A CLICK TEST AND NOT A SCREENSHOT. The two halves of the
bug were reported separately - "it sits over the dock" and "I cannot interact
with the bottom set of mines" - and a screenshot shows only the first. The
board looks the same either way; what differs is whether pressing a cell
opens it or minimizes the window under the pointer.

The click lands at the centre of the bottom-left cell, which on a CGA is
BELOW `[vid_dock_y0]`: one press proves the frame reaches there and that the
window rather than the dock gets it.

WHAT THE PRESS HAS TO PRODUCE IS "the game decoded it", and it has to be asked
ON A FRESH BOARD. This file used to press a control cell first and then ask
whether the count of open cells had gone UP, and that is unanswerable one run
in four - by two routes, both of them in mines.asm's own reveal path. It
failed 1 run in 4 on an idle box, and was classified once as a contention
artefact on the strength of a single passing re-run.

--expect-cut asserts the opposite of every check, so `make KEEPH=0` must FAIL
this file's happy path and pass its inverse. Without that, "the click worked"
is a claim about the click path and not about the flag.
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
import dispapps                                             # noqa: E402

S = os88sym.linear
TITLE_H = 18
MN_STRIP_H, MN_CELLPX, MN_COLS = 20, 16, 9      # apps/mines' own constants
GAMES_DIR, MINES_PKG = "GAMES", "MINES.O88"
WATCH = ["mn_mode", "mn_revealed", "mn_flags"]


def cell_xy(rect, col, row):
    """The screen centre of board cell (col, row) - apps/mines' own layout."""
    x, y = rect[0] + 1, rect[1] + TITLE_H       # wm_content
    return (x + col * MN_CELLPX + MN_CELLPX // 2,
            y + MN_STRIP_H + row * MN_CELLPX + MN_CELLPX // 2)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--expect-cut", action="store_true",
                    help="assert the PRE-11.93 behaviour (make KEEPH=0)")
    a = ap.parse_args(argv)

    fail = []
    say = lambda s: print("  " + s)
    settle = os88marty.settle
    want = "cut" if a.expect_cut else "kept"
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        mo = os88mouse.Mouse(marty=m)
        dock_y0 = int.from_bytes(m.read(S("vid_dock_y0"), 2), "little")
        vid_h = int.from_bytes(m.read(S("vid_ph"), 2), "little")
        say("CGA: %d rows, dock at %d, desktop band %d"
            % (vid_h, dock_y0, dock_y0 - 20 - 1))

        dispcp.open_drive(m, mo, S, settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, settle, wx, wy, GAMES_DIR)
        seg = slot = None
        for _ in range(3):
            wx, wy = dispcp.win_rect(m, S, disk)[:2]
            mo.click(wx + 40, wy + TITLE_H // 2)        # raise the Disk window
            settle(m)
            dispcp.open_named(m, mo, S, settle, wx, wy, MINES_PKG)
            m.advance(frames=120)
            m.run()
            got = dispapps.pkg_seg(m, 0)
            if got is not None:
                slot, seg = got
                break
        if seg is None:
            sys.exit("dispmine: MINES.O88 did not open")

        rect = dispcp.win_rect(m, S, slot)
        say("mines: window %d, segment %04x, rect %r (bottom row %d)"
            % (slot, seg, rect, rect[1] + rect[3] - 1))

        # --- 1: the frame ------------------------------------------------------
        want_h = min(183, vid_h - 20 - 1)
        cut_h = dock_y0 - 20 - 1
        h = rect[3]
        if a.expect_cut:
            if h != cut_h:
                fail.append("KEEPH=0 should have shortened the frame to the "
                            "band's %d and it is %d" % (cut_h, h))
        else:
            if h != want_h:
                fail.append("the frame is %d rows, not the %d the display can "
                            "give it (SPEC.md 11.93)" % (h, want_h))
            if rect[1] + h <= dock_y0:
                fail.append("the frame does not reach the dock at all (%d + %d "
                            "against %d) - this run tested nothing"
                            % (rect[1], h, dock_y0))

        # --- 2: the press this row is about, ON A FRESH BOARD -----------------
        # The bottom-left cell's centre is below [vid_dock_y0] on a CGA, so
        # this single press asks both questions at once: does the frame reach
        # there, and does the WINDOW get a press the dock strip is under.
        #
        # **IT GOES FIRST, AND THE ORDER IS THE WHOLE FIX.** It used to go
        # second, after a top-left click that proved the click path - and that
        # control made this press unanswerable one run in four, by two routes,
        # both of them in mines.asm's own reveal path:
        #
        #   cmp byte [mn_state+bx], MN_S_COVER
        #   jne .out                    ; an OPEN cell is done: nothing happens
        #
        # so a control click whose flood reached the bottom-left cell - it
        # opens anywhere from 1 to 62 of the 81, measured - left this press
        # nothing to do; and if the cell was one of the 10 MINES, the press
        # ended the game and moved `mn_revealed` not at all. Both read as "it
        # went past the window to the dock" and blamed SPEC.md 11.93.
        #
        # On a FRESH board neither can happen, and mines.asm says so rather
        # than this comment hoping so: every cell is MN_S_COVER, and
        #
        #   cmp byte [mn_mode], MN_M_FRESH
        #   jne .armed
        #   call mn_place               ; lazy placement: first click always safe
        #
        # places the mines AROUND the press. So the first press on this cell
        # must open at least itself and must take the mode FRESH -> LIVE.
        def reached(a_, b_):
            """Did the press reach the GAME - not "did a cell open", which is a
            different question. Either half is proof, and neither can be
            produced by a press the dock swallowed: `mn_mode` moves only on a
            press mines.asm decoded."""
            return (b_["mn_revealed"] > a_["mn_revealed"]
                    or b_["mn_mode"] != a_["mn_mode"])

        def why(a_, b_):
            if b_["mn_revealed"] > a_["mn_revealed"]:
                return "opened %d cell(s)" % (b_["mn_revealed"] - a_["mn_revealed"])
            if b_["mn_mode"] != a_["mn_mode"]:
                return "ended the game (mn_mode %d -> %d)" % (a_["mn_mode"],
                                                             b_["mn_mode"])
            return "did nothing"

        cx, cy = cell_xy(rect, 0, MN_COLS - 1)
        if cy < dock_y0:
            fail.append("the bottom cell's centre (%d) is above the dock (%d) "
                        "- this machine cannot pose the question" % (cy, dock_y0))
        was = dispapps.words(m, seg, "mines", WATCH)
        mo.click(cx, cy)
        settle(m)
        got = dispapps.words(m, seg, "mines", WATCH)
        say("bottom cell (%d,%d) -> %r  ...the press %s"
            % (cx, cy, got, why(was, got)))

        # --- 3: the control, asked only when it has something to distinguish --
        # The TOP-left cell is inside the frame in BOTH builds, so a press that
        # reaches it says the click path works and the FRAME is what differs.
        # A control that runs when the answer is already yes is the control
        # that broke this row.
        if not reached(was, got) or a.expect_cut:
            tx, ty = cell_xy(rect, 0, 0)
            before = dispapps.words(m, seg, "mines", WATCH)
            mo.click(tx, ty)
            settle(m)
            top = dispapps.words(m, seg, "mines", WATCH)
            say("top cell    (%d,%d) -> %r  ...the press %s"
                % (tx, ty, top, why(before, top)))
            if not reached(before, top):
                sys.exit("dispmine: the TOP-left cell did not take a press "
                         "either - the click path is broken, not the frame")

        if a.expect_cut:
            if reached(was, got):
                fail.append("KEEPH=0 took a press at y=%d, which is %d rows "
                            "below its own frame (it %s) - the reference build "
                            "is not reproducing the bug"
                            % (cy, cy - (rect[1] + h - 1), why(was, got)))
        elif not reached(was, got):
            fail.append("the bottom row still cannot be played: a press at "
                        "(%d,%d) neither opened a cell nor moved the game's "
                        "mode, so it went past the window (frame %d..%d) to "
                        "the dock (SPEC.md 11.93)"
                        % (cx, cy, rect[1], rect[1] + h - 1))

        w, hgt, d = m.fbuf()
        os88marty.write_png_rgb("/tmp/dispmine-%s.png" % want, w, hgt, d)
        say("screen written to /tmp/dispmine-%s.png" % want)

    print()
    for f in fail:
        print("dispmine: FAIL: %s" % f)
    if fail:
        return 1
    if a.expect_cut:
        print("dispmine: KEEPH=0 reproduces the bug - the frame is cut to the "
              "band and the bottom row takes no clicks - PASS")
    else:
        print("dispmine: the board hangs over the dock and its bottom row "
              "plays - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
