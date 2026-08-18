#!/usr/bin/env python3
"""rczex_ocr: read RUNCPM's terminal rows off a QEMU screendump (SPEC.md 71).

    from rczex_ocr import Screen
    scr = Screen(w, h, rgb)              # a P6 screendump (tools/shot.py's reader)
    scr.learn(row, "known text")         # harvest 8x8 glyphs from a row you know
    scr.read(row)                        # -> the row's text, '?' for a glyph
                                         #    never learned, ' ' for a blank cell

The terminal is 80 columns of 8x8 cells at a fixed origin (the window's
content: RUNCPM's window sits at (7,20) with no left border, so its content
starts at x = 0, y = 20 + TITLE_H = 38 - and the origin is FOUND rather than
assumed, see below). There is no font to read: the kernel takes the 8x8 face
from the BIOS at boot (kernel/font.inc), so the glyphs are HARVESTED from
rows whose text is known - the banner (SPEC.md 71's authority table) and
ZEXDOC's own group names, which are string constants in ZEXDOC.COM - and a
row is then read cell by cell against what was learned. A cell is a
64-bit pattern (a set bit is a dark pixel); reverse video (the cursor) is
recognised by inverting; a pattern never seen is '?'.

The origin is found by CONSISTENCY: for a candidate (x0, y0), every cell of
a known row that holds the same character must hold the same pattern and
every space must be blank; the banner's first row has enough repeats
('a' four times, 'l' three, ' ' many) that only the true grid scores clean.
"""


class Screen:
    def __init__(self, w, h, rgb, x0=None, y0=None):
        self.w, self.h = w, h
        self.rgb = rgb
        self.x0, self.y0 = x0, y0
        self.glyphs = {}          # pattern -> char
        self.chars = {}           # char -> pattern

    def dark(self, x, y):
        if x < 0 or y < 0 or x >= self.w or y >= self.h:
            return 0
        i = (y * self.w + x) * 3
        return 1 if (self.rgb[i] + self.rgb[i + 1] + self.rgb[i + 2]) < 384 else 0

    def cell(self, col, row):
        """The 8x8 pattern of a cell, MSB = top-left."""
        x = self.x0 + col * 8
        y = self.y0 + row * 8
        p = 0
        for r in range(8):
            for c in range(8):
                p = (p << 1) | self.dark(x + c, y + r)
        return p

    def find_origin(self, row, text, xs=range(0, 24), ys=range(20, 80)):
        """Set x0, y0 to the grid where `text` on `row` reads consistently."""
        best, best_score = None, -1
        nchars = sum(1 for ch in text if ch != " ")
        for y0 in ys:
            for x0 in xs:
                self.x0, self.y0 = x0, y0
                seen, ok, dark_cells = {}, 0, 0
                for col, ch in enumerate(text):
                    p = self.cell(col, row)
                    if ch == " ":
                        if p == 0:
                            ok += 1
                        continue
                    if p == 0:
                        continue
                    dark_cells += 1
                    if seen.setdefault(ch, p) == p:
                        ok += 1
                # a real grid has every non-space cell dark and consistent
                score = ok if dark_cells >= nchars - 1 else 0
                if score > best_score:
                    best, best_score = (x0, y0), score
        self.x0, self.y0 = best
        return best, best_score

    def learn(self, row, text):
        for col, ch in enumerate(text):
            p = self.cell(col, row)
            if ch == " " or p == 0:
                continue
            self.glyphs[p] = ch
            self.chars[ch] = p

    def read(self, row, cols=80):
        out = []
        for col in range(cols):
            p = self.cell(col, row)
            if p == 0:
                out.append(" ")
            elif p in self.glyphs:
                out.append(self.glyphs[p])
            elif (~p) & ((1 << 64) - 1) in self.glyphs:   # reverse video
                out.append(self.glyphs[(~p) & ((1 << 64) - 1)])
            elif p == (1 << 64) - 1:
                out.append(" ")                            # a blank in reverse
            else:
                out.append("?")
        return "".join(out).rstrip()
