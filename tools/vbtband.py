#!/usr/bin/env python3
"""vbtband - the Video Battle band as the engine draws it (SPEC.md 125.5),
for the builder's preview (VBATTLE-BUILDER-PLAN 4.4).

Drawn from the COMPILED rules and images (vbtrules.compile_rules and
os88vbt.images_of), by apps/vbattle/vbband.inc's layout: the band's picture
or the plain look (black, a cyan rule at the band's second row and above the
message line, a divider before the party); the open menu's picture over it;
the menu's lines at its column, row and pitch, its ink, an arrow cursor or an
inverse one, an action it cannot pay for in red; the party panel (the enemy
and its stance, then each character); the message line. The glyphs are an
8x8 face of the tree's (fonts/tallx.f8): the machine's own is its ROM's, so
the letters' shapes are close and their places exact.
"""
import os
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
COL_PARTY = 18
ROW_MSG = 191
_FONT = None


def font():
    """{code: 8 row bytes} of the preview's face"""
    global _FONT
    if _FONT is None:
        import os88font
        for p in (os.path.join(HERE, "tallx.f8"),
                  os.path.join(HERE, "..", "fonts", "tallx.f8")):
            if os.path.exists(p):
                _FONT = os88font.parse(p)
                break
        else:
            _FONT = {}
    return _FONT


class Band(object):
    """The band's pixels (indexes 0..3), BY..199, drawn as the engine does"""

    def __init__(self, rules, images, by):
        import vbtrules as V
        self.R = rules if isinstance(rules, V.Rules) else V.Rules(rules)
        self.img = images
        self.by = by
        self.px = np.zeros((200 - by, 320), np.uint8)

    def blit(self, k):
        b = self.R.b
        og = struct.unpack_from("<H", b, 20)[0]
        col, row, cells, rows, para = struct.unpack_from("<4BH", b, og + 8 * k)
        w = cells * 2
        for y in range(rows):
            src = self.img[para * 16 + y * w:para * 16 + (y + 1) * w]
            if row + y >= len(self.px):
                break
            for x, byte in enumerate(src):
                for p in range(4):
                    xx = (col * 2 + x) * 4 + p
                    if xx < 320:
                        self.px[row + y, xx] = (byte >> (6 - 2 * p)) & 3

    def ground(self, menu=None):
        """The buffer: the picture or the plain look, then the menu's
        picture (vb_band_frame)"""
        self.px[:] = 0
        if self.R.band_img != 0xFF:
            self.blit(self.R.band_img)
        else:
            self.px[1, :] = 1
            self.px[ROW_MSG - 2 - self.by, :] = 1
            for y in range(3, ROW_MSG - 3 - self.by):
                self.px[y, (COL_PARTY - 2) * 8] = 1     # (40h: the byte's first pixel)
        if menu is not None and self.R.menus[menu]["img"] != 0xFF:
            self.blit(self.R.menus[menu]["img"])
        self.bg = self.px.copy()

    def text(self, s, col, row, ink, ground=None):
        """S at cell COL, pixel ROW (the screen's); GROUND None: the
        buffer's (vb_text's F)"""
        f = font()
        y0 = row - self.by
        for i, ch in enumerate(s):
            c = col + i
            if c >= 40:
                break
            g = f.get(ord(ch), [0] * 8) if 32 <= ord(ch) <= 126 else [0] * 8
            for r in range(8):
                if not 0 <= y0 + r < len(self.px):
                    continue
                bits = g[r]
                for p in range(8):
                    on = bits & (0x80 >> p)
                    x = c * 8 + p
                    if on:
                        self.px[y0 + r, x] = ink
                    else:
                        self.px[y0 + r, x] = self.bg[y0 + r, x] if ground is None \
                            else ground


def render(proj, char=0, menu=None, cursor=0, target=None, message=None,
           state=None):
    """A PIL image of the band for character CHAR with MENU open (None: its
    top menu; "side", or a sub-menu's label), its cursor on item CURSOR,
    aiming at TARGET (a character) when not None, MESSAGE on the line"""
    from PIL import Image
    import os88vbt as B
    import vbtana as A
    import vbtrules as V
    d = proj.d
    idx = {c: i for i, c in enumerate(d["clips"])}
    images, places = B.images_of(proj)
    rules = V.Rules(V.compile_rules(proj, idx, places))
    by = d["target"]["band"][1]
    b = Band(rules, images, by)
    ch = rules.chars[char]
    mi = ch["top"]
    if menu == "side":
        mi = ch["side"]
    elif menu is not None:
        for kind, k in rules.menus[ch["top"]]["items"]:
            if kind == 1 and rules.menus[k]["label"] == menu:
                mi = k
    b.ground(mi)
    m = rules.menus[mi]
    st = state or V.Sim(rules)
    for i, (kind, k) in enumerate(m["items"]):
        line = ""
        if m["flags"] & V.MF_ARROW:
            line = ">" if i == cursor else " "
        ink = m["ink"]
        if kind == 1:
            line += rules.menus[k]["label"]
        else:
            a = rules.acts[k]
            line += a["label"]
            if a["cost"] == V.COST_MP:
                line = line.ljust(10) + "%dMP" % a["cost_n"]
            if not st.can_pay(k, char):
                ink = 2
        line = line.ljust(15)[:15]
        row = by + m["row"] + i * m["pitch"]
        if not (m["flags"] & V.MF_ARROW) and i == cursor:
            b.text(line, m["col"], row, 0, ground=3)
        else:
            b.text(line, m["col"], row, ink)
    top = rules.ename[:10]
    if rules.nstance > 1:
        top += " " + rules.stances[st.stance]["name"]
    b.text(top.ljust(22)[:22], COL_PARTY, by + 3, 2)
    for i, c in enumerate(rules.chars):
        hp, mx = st.hp[i], c["hp"]
        line = "%-6s%s%4d/%-6d%4d" % (c["name"][:6], "*" if st.lim[i] >= 100 else " ",
                                      hp, mx, st.mp[i])
        row = by + 12 + 9 * i
        if hp == 0:
            b.text(line[:22], COL_PARTY, row, 2)
        elif (target is not None and i == target) or (target is None and i == char):
            b.text(line[:22], COL_PARTY, row, 0, ground=3)
        else:
            b.text(line[:22], COL_PARTY, row, 3)
    msg = message if message is not None else "%s appears!" % rules.ename
    b.text(msg.ljust(38)[:38], 1, ROW_MSG, 3)
    rgb = np.array(A.palette_rgb(d["target"]["palette"]), np.uint8)[b.px]
    return Image.fromarray(rgb)
