#!/usr/bin/env python3
"""vbtband - the Video Battle band as the engine draws it (SPEC.md 126.5),
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
        self.lay = self.R.layout or V.layout_defaults(by)   # (126.5.1)

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
        elif self.lay["lines_show"]:    # THE PLAIN LOOK: a rule at its
            L, h = self.lay, len(self.px)   # second row and above the line,
            self.px[1, :] = 1               # the divider between
            if 0 <= L["msg_row"] - 2 < h:
                self.px[L["msg_row"] - 2, :] = 1
            for y in range(3, min(h, L["msg_row"] - 3)):
                self.px[y, L["divider_col"] * 8] = 1    # (40h: its first pixel)
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


def party_line(L, name, star, hp, most, mp):
    """A character's line of the party, laid out (126.5.1): (its text, its
    first column) - its fields at their columns, spaces between, so that a
    highlight is one bar from its first field to its last. The engine's
    vb_draw_party composes the same"""
    f = []
    if L["name_show"]:
        f.append((L["name_col"], name[:6].ljust(6)))
    if L["limit_show"]:
        f.append((L["limit_col"], "*" if star else " "))
    if L["hp_show"]:
        f.append((L["hp_col"], "%4d" % hp))
        if L["hpmax_show"]:
            f.append((L["hp_col"] + 4, ("/%d" % most).ljust(7)))
    if L["mp_show"]:
        f.append((L["mp_col"], "%4d" % mp))
    if not f:
        return "", 0
    lo = min(c for c, _ in f)
    hi = min(40, max(c + len(t) for c, t in f))
    line = [" "] * (hi - lo)
    for c, t in sorted(f):          # (left to right: a later one wins)
        for j, ch in enumerate(t):
            if c + j < hi:
                line[c - lo + j] = ch
    return "".join(line), lo


def render(proj, char=0, menu=None, cursor=0, target=None, message=None,
           state=None, raw=False):
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
    rules = V.Rules(V.compile_rules(proj, idx, places, preview=True))
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
    L = b.lay
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
            if a["cost"] == V.COST_MP and L["mp_show"]:
                line = line.ljust(10) + "%dMP" % a["cost_n"]
            elif a["cost"] == V.COST_ITEM and L.get("item_count_show"):
                line = line.ljust(10) + "x%d" % st.items[a["cost_item"]]
            if not st.can_pay(k, char):
                ink = 2
        line = line.ljust(15)[:15]
        col, row = m["col"], by + m["row"] + i * m["pitch"]
        if (mi, i) in rules.places:     # AN ITEM PLACED of its own (126.5.1)
            col, prow = rules.places[(mi, i)]
            row = by + prow
        if not (m["flags"] & V.MF_ARROW) and i == cursor:
            b.text(line, col, row, 0, ground=3)
        else:
            b.text(line, col, row, ink)
    if L["enemy_show"]:
        top = rules.ename[:10]
        if rules.nstance > 1:
            top += " " + rules.stances[st.stance]["name"]
        b.text(top.ljust(22)[:22], L["enemy_col"], by + L["enemy_row"], 2)
    for i, c in enumerate(rules.chars):
        line, lo = party_line(L, c["name"], st.lim[i] >= 100, st.hp[i],
                              c["hp"], st.mp[i])
        if not line:
            continue
        row = by + L["party_row"] + L["party_pitch"] * i
        if st.hp[i] == 0:
            b.text(line, lo, row, 2)
        elif (target is not None and i == target) or (target is None and i == char):
            b.text(line, lo, row, 0, ground=3)
        else:
            b.text(line, lo, row, 3)
    if L["msg_show"]:
        msg = message if message is not None else "%s appears!" % rules.ename
        b.text(msg.ljust(38)[:38], L["msg_col"], by + L["msg_row"], 3)
    if raw:                         # (the pixels' indexes, 0..3: a test's)
        return b.px
    rgb = np.array(A.palette_rgb(d["target"]["palette"]), np.uint8)[b.px]
    return Image.fromarray(rgb)


def overlaps(proj):
    """The band's text parts that SHARE CELLS (126.5.1): [(what, what)].
    The engine redraws only what changed, so a cursor moved redraws a menu
    over the message line it overlaps where the whole band drew the message
    last - a layout's mistake, said by the check rather than drawn in a
    fixed order"""
    import os88vbt as B
    import vbtrules as V
    d = proj.d
    idx = {c: i for i, c in enumerate(d["clips"])}
    try:
        rules = V.Rules(V.compile_rules(proj, idx, B.images_of(proj)[1],
                                        preview=True))
    except Exception:           # noqa: BLE001 (the rules' own errors say it)
        return []
    L = rules.layout or V.layout_defaults(d["target"]["band"][1])
    parts = []                  # (what, col, row, cells): rows are 8 high
    if L["msg_show"]:
        parts.append(("the message line", L["msg_col"], L["msg_row"], 38))
    if L["enemy_show"]:
        parts.append(("the enemy's line", L["enemy_col"], L["enemy_row"],
                      22))
    for i, c in enumerate(rules.chars):
        line, lo = party_line(L, c["name"], False, c["hp"], c["hp"], c["mp"])
        if line:
            parts.append(("%s's party line" % c["name"], lo,
                          L["party_row"] + L["party_pitch"] * i, len(line)))
    menus = []
    seen = set()
    for ch in rules.chars:
        for mi in [ch["top"], ch["side"]] + [
                k for kind, k in rules.menus[ch["top"]]["items"] if kind == 1]:
            if mi is None or mi in seen or mi >= len(rules.menus):
                continue
            seen.add(mi)
            m = rules.menus[mi]
            for i in range(len(m["items"])):
                col, row = m["col"], m["row"] + i * m["pitch"]
                if (mi, i) in rules.places:
                    col, row = rules.places[(mi, i)]
                menus.append(("the %s menu's item %d" % (m["label"], i + 1),
                              col, row, 15))

    def hit(a, b):
        return (a[1] < b[1] + b[3] and b[1] < a[1] + a[3] and
                a[2] < b[2] + 8 and b[2] < a[2] + 8)
    out = []
    for i, a in enumerate(parts):
        for b in parts[i + 1:] + menus:
            if hit(a, b):
                out.append((a[0], b[0]))
    return out
