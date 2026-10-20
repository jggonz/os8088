"""Solitaire's screenshots, for tools/guide/manuals/SOLITAIR_O88.md.

The deal is read out of the game's own memory (one byte a card: rank in the
low four bits, suit in bits 4-5, bit 6 face up), so the moves played before
the first shot are real, legal Klondike moves rather than guesses.
"""
import os
import struct
import sys

import os88geom as geom
import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "..", "tests"))
import dispapps  # noqa: E402

UP = 0x40
TAB, FND, SW = 19, 13, 24
POFS = [i * TAB for i in range(7)] + [7 * TAB + j * FND for j in range(4)] \
    + [7 * TAB + 4 * FND, 7 * TAB + 4 * FND + SW]
STOCK, WASTE = 11, 12


def red(c):
    s = (c >> 4) & 3
    return s in (1, 2)


class Game(object):
    def __init__(self, ui, w):
        self.ui, self.w = ui, w
        seg = struct.unpack("<H", ui.m.read(ui._S("wm_wins")
                                            + w.i * geom.WIN_SIZE
                                            + geom.W_SEG, 2))[0]
        self.base = seg << 4
        self.map = dispapps._map("solitaire")

    def rd(self, name, n):
        return bytes(self.ui.m.read(self.base + self.map[name], n))

    def word(self, name):
        return struct.unpack("<h", self.rd(name, 2))[0]

    def read(self):
        cnt = self.rd("sol_pcnt", 13)
        raw = self.rd("sol_pile", 233)
        self.piles = [list(raw[POFS[p]:POFS[p] + cnt[p]]) for p in range(13)]
        for k in ("ox", "oy", "cw", "ch", "marg", "topy", "taby", "pitch",
                  "wstep", "fand", "fanu"):
            setattr(self, k, self.word("sol_" + k))
        self.wfan = self.rd("sol_wfan", 1)[0]

    # --- where things are on the screen ----------------------------------
    def yoff(self, col, k):
        p = self.piles[col]
        nd = sum(1 for c in p if not c & UP)
        if k < nd:
            return k * self.fand
        return nd * self.fand + (k - nd) * self.fanu

    def tab_xy(self, col, k):
        return (self.ox + self.marg + col * self.pitch + self.cw // 2,
                self.oy + self.taby + self.yoff(col, k) + 5)

    def waste_xy(self):
        n = len(self.piles[WASTE])
        shown = min(max(self.wfan, 1), n)
        return (self.ox + self.marg + self.pitch + (shown - 1) * self.wstep
                + self.cw // 2, self.oy + self.topy + self.ch // 2)

    def stock_xy(self):
        return (self.ox + self.marg + self.cw // 2,
                self.oy + self.topy + self.ch // 2)

    # --- rules -------------------------------------------------------------
    def fits_tab(self, card, col):
        p = self.piles[col]
        if not p:
            return card & 15 == 12
        top = p[-1]
        return (top & UP and (top & 15) == (card & 15) + 1
                and red(top) != red(card))

    def fits_fnd(self, card):
        return (card & 15) == len(self.piles[7 + ((card >> 4) & 3)])

    def moves(self):
        """(kind, args) for every useful legal move, best first."""
        out = []
        for col in range(7):
            p = self.piles[col]
            if p and self.fits_fnd(p[-1]):
                out.append(("fnd", self.tab_xy(col, len(p) - 1)))
        if self.piles[WASTE] and self.fits_fnd(self.piles[WASTE][-1]):
            out.append(("fnd", self.waste_xy()))
        for col in range(7):
            p = self.piles[col]
            ups = [k for k, c in enumerate(p) if c & UP]
            if not ups:
                continue
            k = ups[0]
            if k == 0 and p[0] & 15 == 12:
                continue                     # a king already at the top
            for to in range(7):
                if to != col and self.fits_tab(p[k], to) \
                        and (self.piles[to] or k > 0):
                    out.append(("run", (col, k, to)))
                    break
        if self.piles[WASTE]:
            card = self.piles[WASTE][-1]
            for to in range(7):
                if self.fits_tab(card, to):
                    out.append(("waste", to))
                    break
        return out

    def target_xy(self, to):
        p = self.piles[to]
        if not p:
            return (self.ox + self.marg + to * self.pitch + self.cw // 2,
                    self.oy + self.taby + self.ch // 2)
        x, y = self.tab_xy(to, len(p) - 1)
        return x, y + self.ch // 2

    def play(self, mv):
        mo = self.ui.mo
        kind, a = mv
        if kind == "fnd":
            mo.click(*a)
        elif kind == "run":
            col, k, to = a
            x0, y0 = self.tab_xy(col, k)
            mo.drag(x0, y0, x0 + (to - col) * self.pitch, y0)
        else:
            x0, y0 = self.waste_xy()
            mo.drag(x0, y0, *self.target_xy(a))
        os88marty.guest_sleep(self.ui.m, 0.5)


def menu_open(ui, menu, item):
    """Drop a menu and hold the pointer on an item, button down."""
    cells = ui.menus()
    cell = [c for c in cells if c[0] == menu][0]
    k = [t for t, _ in cell[3]].index(item)
    ui.mo.to((cell[1] + cell[2]) // 2, geom.MBAR_H // 2)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 0.5)
    y1 = ui._word("menu_y1")
    ui.mo.to(ui._word("menu_x1") + 8,
             y1 + 1 + k * geom.MENU_ITEM_H + geom.MENU_ITEM_H // 2, l=True)
    os88marty.guest_sleep(ui.m, 0.5)
    return cell[1], y1


def menu_drop(ui, x, y):
    ui.mo.to(x, y, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 0.5)


def capture(ui, shot):
    ui.path("B:/GAMES/SOLITAIR.O88")
    w = ui.window("Solitaire")
    g = Game(ui, w)

    played, deals = 0, 0
    while played < 9 and deals < 40:
        g.read()
        mv = g.moves()
        if mv:
            g.play(mv[0])
            played += 1
        else:
            if not g.piles[STOCK] and not g.piles[WASTE]:
                break
            ui.mo.click(*g.stock_xy())
            os88marty.guest_sleep(ui.m, 0.3)
            deals += 1
    g.read()
    ui.mo.to(w.x + w.w + 30, w.y + 30)
    shot("solitaire-window", window="Solitaire", pad=10)

    # A drag in flight: the outline follows the pointer, the cards stay put.
    g.read()
    mv = [m for m in g.moves() if m[0] in ("run", "waste")]
    if mv and mv[0][0] == "run":
        col, k, to = mv[0][1]
        x0, y0 = g.tab_xy(col, k)
        x1, y1 = x0 + (to - col) * g.pitch, y0 + 30
    else:
        x0, y0 = g.waste_xy() if g.piles[WASTE] else g.tab_xy(0, len(g.piles[0]) - 1)
        x1, y1 = (g.target_xy(mv[0][1]) if mv else (x0 + 2 * g.pitch, y0 + 60))
    ui.mo.to(x0, y0)
    ui.mo._edge(True)
    ui.mo.to((x0 + x1) // 2 + 9, (y0 + y1) // 2 + 25, l=True)
    os88marty.guest_sleep(ui.m, 0.6)
    shot("solitaire-drag", window="Solitaire", pad=10, settle=False)
    ui.mo.to(x1, y1, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 0.5)

    # The Deal menu: the mode in force is the greyed one.
    x, y1 = menu_open(ui, "Deal", "Draw Three")
    shot("solitaire-deal", rect=(x - 10, 0, 170, y1 + 2 * geom.MENU_ITEM_H + 8),
         settle=False)
    menu_drop(ui, w.x + w.w + 60, w.y + w.h - 20)

    # Draw Three, and a few deals: the waste fans out three cards.
    ui.menu_pick("Deal", "Draw Three")
    g.read()
    for _ in range(2):
        ui.mo.click(*g.stock_xy())
        os88marty.guest_sleep(ui.m, 0.3)
    ui.mo.to(w.x + w.w + 30, w.y + 30)
    w = ui._refresh(w)
    shot("solitaire-three", rect=(w.x - 6, w.y - 6, w.w + 12, 110))
