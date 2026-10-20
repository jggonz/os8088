"""Minesweeper's screenshots, for tools/guide/manuals/MINES_O88.md.

The board is read out of the game's own memory (one byte a cell, bit 7 a
mine, bit 6 open, bit 5 flagged), so the moves below are chosen rather than
guessed: a game in progress with two correct flags, flag mode on, a lost
board with one wrong flag, and a won board.
"""
import os
import struct
import sys

import os88geom as geom
import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "..", "tests"))
import dispapps  # noqa: E402

COLS, CELLS, PX, STRIP = 9, 81, 16, 20


def capture(ui, shot):
    ui.path("B:/GAMES/MINES.O88")
    w = ui.window("Minesweeper")
    seg = struct.unpack("<H", ui.m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE
                                        + geom.W_SEG, 2))[0]
    base = (seg << 4) + dispapps.img_size("mines")
    off_cell = dispapps.bss_off("mines", "mn_cell")

    def board():
        return bytes(ui.m.read(base + off_cell, CELLS))

    def xy(c):
        w2 = ui._refresh(w)
        cx, cy = w2.content[0], w2.content[1]
        r, k = divmod(c, COLS)
        return cx + k * PX + PX // 2, cy + STRIP + r * PX + PX // 2

    def click(c):
        ui.mo.click(*xy(c))
        os88marty.guest_sleep(ui.m, 0.3)

    def rclick(c):
        x, y = xy(c)
        ui.mo.rmenu(x, y, x, y)
        os88marty.guest_sleep(ui.m, 0.3)

    def near(c, d):
        r, k = divmod(c, COLS)
        r2, k2 = divmod(d, COLS)
        return max(abs(r - r2), abs(k - k2)) == 1

    # First click: always safe, and it floods if it lands on an empty cell.
    click(40)
    b = board()
    opened = [c for c in range(CELLS) if b[c] & 0x40]
    # Open a few more safe cells next to what is open, and flag two mines
    # that border it.
    safe = [c for c in range(CELLS) if not b[c] & 0xC0
            and any(near(c, o) for o in opened)]
    for c in safe[:3]:
        click(c)
    b = board()
    opened = [c for c in range(CELLS) if b[c] & 0x40]
    mines = [c for c in range(CELLS) if b[c] & 0x80]
    edge = [c for c in mines if any(near(c, o) for o in opened)] or mines
    for c in edge[:2]:
        rclick(c)
    ui.mo.to(w.x + w.w + 40, w.y + 40)
    shot("mines-window", window="Minesweeper", pad=10)

    # Flag mode: F, and a left click plants a flag. This one is wrong.
    ui.m.type_text("f")
    b = board()
    empty = [c for c in range(CELLS) if not b[c] & 0xE0]
    click(empty[-1])
    ui.mo.to(w.x + w.w + 40, w.y + 40)
    shot("mines-flagmode", window="Minesweeper", pad=10)
    ui.m.type_text("f")

    # Step on a mine that is not flagged.
    b = board()
    boom = [c for c in range(CELLS) if b[c] & 0x80 and not b[c] & 0x20][0]
    click(boom)
    ui.mo.to(w.x + w.w + 40, w.y + 40)
    shot("mines-lost", window="Minesweeper", pad=10)

    # A new game, played to the end with every safe cell opened.
    ui.m.type_text("n")
    os88marty.guest_sleep(ui.m, 0.3)
    click(40)
    for _ in range(CELLS):
        b = board()
        left = [c for c in range(CELLS) if not b[c] & 0xC0]
        if not left:
            break
        click(left[0])
    ui.mo.to(w.x + w.w + 40, w.y + 40)
    shot("mines-won", window="Minesweeper", pad=10)
