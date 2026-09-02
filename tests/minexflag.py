#!/usr/bin/env python3
"""A wrong flag must not look like a mine (SPEC.md 23).

    make && python3 tests/minexflag.py

`mn_draw_wrongflag` draws the mine glyph and then an X over it, and the X
used to be `CBLACK`. Every one of its twenty pixels lands inside the glyph
underneath - the diagonals run (3,3)..(12,12) and (12,3)..(3,12), and
`mn_sh_mine`'s disc, spokes and corner nubs cover all twenty - so the X was
drawn black on black and NOTHING of it reached the screen. A cell you
flagged wrongly came out **pixel-identical to a real mine**: the lost board
showed an eleventh mine that was not there, and every digit beside it read
one too low. That was reported as the COUNTS being wrong, which they never
were.

So this is a PIXEL test and it has to be. The state is right either way -
`mn_mine` says no mine and `mn_state` says flagged in both builds - and a
test that reads the bss cannot tell the bug from the fix. What differs is
what the player can see, so the check is the two 16x16 cells' pixels
against each other: the wrongly flagged one and a genuinely mined one, off
the same lost board, and they must DIFFER.

A CGA, and that is the harder half rather than a convenience: SPEC.md 39.4
maps the X's light red (12) to WHITE at 1bpp, so a mono adapter is where a
colour that reduces to black would still be invisible after a "fix" that
looked right on VGA (CLAUDE.md's rule - look at a drawing change on a 1bpp
adapter before calling it done). `m.vram()` reads the card as flat memory,
so there is no screendump in this and no QEMU.
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
from os88geom import TITLE_H                                # noqa: E402

S = os88sym.linear
MN_STRIP_H, MN_CELLPX, MN_COLS = 20, 16, 9      # apps/mines' own constants
MN_CELLS = MN_COLS * MN_COLS
MN_S_COVER, MN_S_FLAG = 0, 1
MN_M_LOST = 2
GAMES_DIR, MINES_PKG = "GAMES", "MINES.O88"


def cell_xy(rect, cell):
    """The screen centre of a board cell - apps/mines' own layout."""
    row, col = divmod(cell, MN_COLS)
    x, y = rect[0] + 1, rect[1] + TITLE_H       # wm_content
    return (x + col * MN_CELLPX + MN_CELLPX // 2,
            y + MN_STRIP_H + row * MN_CELLPX + MN_CELLPX // 2)


def cell_rect(rect, cell):
    """The cell's top-left pixel - mn_cellxy, on the host side."""
    row, col = divmod(cell, MN_COLS)
    return (rect[0] + 1 + col * MN_CELLPX,
            rect[1] + TITLE_H + MN_STRIP_H + row * MN_CELLPX)


def bss(m, seg, name, n):
    """`n` raw bytes of one of the package's own bss arrays."""
    base = dispapps.img_size("mines") + dispapps.bss_off("mines", name)
    return bytes(m.read((seg << 4) + base + 0, n)) if n else b""


def block(rows, h, x, y):
    """One cell's 16x16 pixels out of the 1bpp framebuffer."""
    return tuple(tuple(rows[yy][x:x + MN_CELLPX])
                 for yy in range(y, min(y + MN_CELLPX, h)))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)

    fail = []
    say = lambda s: print("  " + s)
    settle = os88marty.settle

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        mo = os88mouse.Mouse(marty=m)
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
            sys.exit("minexflag: MINES.O88 did not open")
        rect = dispcp.win_rect(m, S, slot)
        say("mines: window %d, segment %04x, rect %r" % (slot, seg, rect))

        # --- 1. one click arms the board (first reveal is always safe) --------
        mo.click(*cell_xy(rect, 0))
        settle(m)
        mine = bss(m, seg, "mn_mine", MN_CELLS)
        state = bss(m, seg, "mn_state", MN_CELLS)
        if sum(mine) != 10:
            sys.exit("minexflag: the board has %d mines, not 10 - the click "
                     "did not arm it" % sum(mine))

        # A covered EMPTY cell to flag wrongly, and a covered MINE to step on.
        # Both must still be covered: the first click floods, and a cell the
        # flood opened can be neither flagged nor exploded.
        empty = [c for c in range(MN_CELLS)
                 if not mine[c] and state[c] == MN_S_COVER]
        mined = [c for c in range(MN_CELLS) if mine[c]]
        if not empty:
            sys.exit("minexflag: the first click opened every empty cell - "
                     "no covered cell left to flag")
        wrong, boom = empty[0], mined[0]
        say("wrong flag on cell %d (no mine), stepping on cell %d (a mine)"
            % (wrong, boom))

        # --- 2. plant the wrong flag, through flag mode ------------------------
        # 'f' and the left button rather than the right, because os88mouse has
        # no right press - tests/minesrc.py is the right button's own row and
        # it is a dispatch question. This one is about pixels, and flag mode
        # reaches mn_flag_toggle by the same path (SPEC.md 23).
        m.type_text("f")
        settle(m)
        mo.click(*cell_xy(rect, wrong))
        settle(m)
        if bss(m, seg, "mn_state", MN_CELLS)[wrong] != MN_S_FLAG:
            sys.exit("minexflag: cell %d did not take a flag - flag mode did "
                     "not arm" % wrong)
        m.type_text("f")                                # back out of flag mode
        settle(m)

        # --- 3. lose ----------------------------------------------------------
        mo.click(*cell_xy(rect, boom))
        settle(m)
        mode = m.read((seg << 4) + dispapps.img_size("mines")
                      + dispapps.bss_off("mines", "mn_mode"), 1)[0]
        if mode != MN_M_LOST:
            sys.exit("minexflag: the board is in mode %d, not lost - the "
                     "click on the mine did not land" % mode)

        # --- 4. the question --------------------------------------------------
        # A revealed mine that is NOT the one clicked: the boom cell sits on a
        # light-red ground and would differ from the wrong flag for a reason
        # that has nothing to do with the X.
        other = [c for c in mined if c != boom]
        w, h, rows = m.vram()
        wx0, wy0 = cell_rect(rect, wrong)
        got = block(rows, h, wx0, wy0)
        same = [c for c in other
                if block(rows, h, *cell_rect(rect, c)) == got]
        say("the wrongly flagged cell against %d revealed mines: %d identical"
            % (len(other), len(same)))
        if same:
            fail.append("cell %d is FLAGGED with no mine under it and is drawn "
                        "pixel-identical to the real mine at cell %d - the X "
                        "is invisible, so the lost board shows a mine that is "
                        "not there and the digits beside it read one too low "
                        "(SPEC.md 23)" % (wrong, same[0]))

        # ...and it is the X that differs, not the ground: every pixel the X
        # touches is one mn_sh_mine already painted, so the difference has to
        # be INSIDE the glyph rather than around it.
        xs = [(3 + i, 3 + i) for i in range(10)] + \
             [(12 - i, 3 + i) for i in range(10)]
        ref = block(rows, h, *cell_rect(rect, other[0])) if other else None
        if ref and not fail:
            lit = [(x, y) for x, y in xs
                   if y < len(got) and got[y][x] != ref[y][x]]
            say("X pixels differing from the plain mine: %d of 20" % len(lit))
            if not lit:
                fail.append("the two cells differ somewhere, but NOT on any of "
                            "the twenty pixels the X is drawn on - whatever "
                            "marks the wrong flag, it is not the X")

    for f in fail:
        print("FAIL: " + f)
    print("minexflag: %s" % ("FAILED" if fail else "ok"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
