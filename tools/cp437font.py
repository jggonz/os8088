#!/usr/bin/env python3
"""Generate apps/os88cp437.inc - the CP437 glyphs the machine cannot be
asked for.

WHY THIS EXISTS. `OSAPI_FONT_GLYPHS` answers the kernel's own face and
kernel/font.inc keeps 32..126 only, so a terminal - which needs 0..255, in
CP437, whatever a `make FONT=` kernel did to the system letters - has to build
its own table. It builds it out of two halves (SPEC.md 70.8.6): 32..127 come
off the machine, because they are ASCII and every ROM agrees about them, and
0..31 and 128..255 are shipped, because they are CP437's own and a clone ROM
is free to differ. This tool draws the shipped half: 160 glyphs, 8 bytes each,
1,280 bytes.

IT IS CLEAN-ROOM AND THAT IS THE POINT. No font file is fetched, read or
transcribed. Every glyph here is either DRAWN BY ARITHMETIC - the shades are
dither predicates, the blocks are half-planes, the box-drawing set is an
(arms, weight) table with the pixels DERIVED from it - or authored below as
eight lines of ASCII art, which is a font a person can read in a diff and
argue with in a review.

  python3 tools/cp437font.py             rewrite apps/os88cp437.inc
  python3 tools/cp437font.py --check     exit 1 if it would change (the gate)
  python3 tools/cp437font.py --show      print all 160 glyphs, to be LOOKED at
  python3 tools/cp437font.py --selfcheck the shape assertions on their own

--check runs in the default build beside checkdocs.py and os88index.py, for
their reason: a generated file that is not checked is a generated file that
gets hand-edited once and then lies about where it came from.

--show is not decoration. A glyph nobody has looked at is a glyph that is
wrong, and nothing in a byte table says so.
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "apps", "os88cp437.inc")

# The order the table ships in, and the only order: CP437's own codes, the two
# ranges the ASCII half leaves out.
CODES = list(range(0x00, 0x20)) + list(range(0x80, 0x100))


# --- the cell ----------------------------------------------------------------
# Eight rows of eight. Row 0 is the TOP scanline and bit 7 is the LEFTMOST
# pixel, because that is what an 8086 shifting a byte out of the left draws.

BLANK = "........"


def rows_to_bytes(rows):
    """Eight 8-character rows of '#' and '.' -> eight bytes."""
    if len(rows) != 8:
        raise ValueError("a glyph is 8 rows, not %d" % len(rows))
    out = []
    for r in rows:
        if len(r) != 8:
            raise ValueError("a row is 8 columns, not %d: %r" % (len(r), r))
        b = 0
        for x, ch in enumerate(r):
            if ch == "#":
                b |= 0x80 >> x
            elif ch != ".":
                raise ValueError("a cell is '#' or '.', not %r" % ch)
        out.append(b)
    return out


def art(*rows):
    """Author a glyph as ASCII art. Short art is padded at the bottom."""
    rows = list(rows) + [BLANK] * (8 - len(rows))
    return rows_to_bytes(rows)


def place(pat, top, left=1):
    """A narrow pattern dropped into a cell at (top, left). This is how a base
    letter and its accent are authored ONCE each and composed: the letter sits
    at row 2, the accent at row 0, the cedilla at row 7."""
    grid = [list(BLANK) for _ in range(8)]
    for i, line in enumerate(pat):
        for x, ch in enumerate(line):
            if ch == "#":
                grid[top + i][left + x] = "#"
    return rows_to_bytes(["".join(g) for g in grid])


def over(*layers):
    """OR glyphs together - a base and its accent."""
    out = [0] * 8
    for g in layers:
        for i in range(8):
            out[i] |= g[i]
    return out


def invert(g):
    return [(~b) & 0xFF for b in g]


def mirror(g):
    """Left-right mirror. An arrow's other direction is the same drawing."""
    out = []
    for b in g:
        m = 0
        for x in range(8):
            if b & (0x80 >> x):
                m |= 0x80 >> (7 - x)
        out.append(m)
    return out


def flip(g):
    """Top-bottom flip."""
    return list(reversed(g))


def pixels(pred):
    """A glyph from a predicate on (x, y) - the shades and the blocks are
    arithmetic, not drawings, so they are written as arithmetic."""
    out = []
    for y in range(8):
        b = 0
        for x in range(8):
            if pred(x, y):
                b |= 0x80 >> x
        out.append(b)
    return out


def show(g):
    """Eight rows of '#' and '.', for --show."""
    return ["".join("#" if b & (0x80 >> x) else "." for x in range(8)) for b in g]


# --- geometry: the shades ----------------------------------------------------
# A shade is a dither, and a dither is a predicate. 0xB1 is the checkerboard;
# 0xB0 is a quarter of the pixels on a 4x2 lattice, offset row to row so it
# does not read as vertical stripes; 0xB2 is 0xB0's complement, which is what
# makes light-then-dark a ramp rather than two unrelated textures.

def _light(x, y):
    return (x % 4) == (0 if (y % 2) == 0 else 2)


SHADE_LIGHT = pixels(_light)
SHADE_MEDIUM = pixels(lambda x, y: (x + y) % 2 == 0)
SHADE_DARK = invert(SHADE_LIGHT)

# --- geometry: the blocks ----------------------------------------------------
# Half-planes, and a full one.

BLOCK_FULL = pixels(lambda x, y: True)
BLOCK_LOWER = pixels(lambda x, y: y >= 4)
BLOCK_UPPER = pixels(lambda x, y: y < 4)
BLOCK_LEFT = pixels(lambda x, y: x < 4)
BLOCK_RIGHT = pixels(lambda x, y: x >= 4)

# --- geometry: dots and the square -------------------------------------------
# One shape at three sizes, centred: 2x2, 4x4, 6x6. Concentric on purpose -
# 0xFA, 0xF9 and 0xFE are read side by side in a menu rule and have to be
# telling apart at a glance.

def _centred(n):
    lo, hi = (8 - n) // 2, (8 + n) // 2
    return pixels(lambda x, y: lo <= x < hi and lo <= y < hi)


DOT_SMALL = _centred(2)
DOT_BIG = _centred(4)
SQUARE = _centred(6)

# --- geometry: the box-drawing set -------------------------------------------
# Every one of 0xB3..0xDA is FOUR ARM WEIGHTS - up, down, left, right, each 0
# (absent), 1 (single) or 2 (double) - and the pixels are derived. The table
# below is assignments, which is knowledge about CP437; the drawing is
# arithmetic, which is not.
#
# A single arm is one line on the centre row 3 / centre column 3. A double arm
# is two lines straddling it, at 2 and 4. Where they meet, one rule decides
# everything:
#
#   * a SINGLE arm runs to the FAR perpendicular line, so it crosses whatever
#     is there and comes out the other side;
#   * a DOUBLE arm at a TEE - both perpendicular arms present - stops both its
#     lines at the NEAR perpendicular line, which is what leaves the little
#     square hole in the middle of a double cross and breaks the inner line of
#     a double tee;
#   * a DOUBLE arm at a CORNER - one perpendicular arm - runs its OUTER line
#     (the one on the far side from that arm) to the FAR perpendicular line and
#     stops its INNER line at the NEAR one. Outer meets outer, inner meets
#     inner, and the corner closes.
#
# That is the whole of it. Every junction, mixed weights included, falls out.

SINGLE_LINES = [3]
DOUBLE_LINES = [2, 4]


def _lines(w):
    return [] if w == 0 else (SINGLE_LINES if w == 1 else DOUBLE_LINES)


def _stop(w, coord, perp, toward_high, low_arm, high_arm):
    """Where an arm's line at `coord` gives up, against the perpendicular
    lines `perp`. `toward_high` is the direction the arm grows in."""
    if not perp:
        return 3                       # nothing to meet: stop at the centre
    far = min(perp) if toward_high else max(perp)
    near = max(perp) if toward_high else min(perp)
    if w == 1:
        return far
    if len(perp) == 1:
        return perp[0]
    if low_arm and high_arm:
        return near
    outer_is_low = bool(high_arm)      # the far side from the arm that is there
    is_outer = (coord == min(DOUBLE_LINES)) == outer_is_low
    return far if is_outer else near


def box(up, down, left, right):
    grid = [[False] * 8 for _ in range(8)]
    vcols = sorted(set(_lines(up) + _lines(down)))
    hrows = sorted(set(_lines(left) + _lines(right)))

    for w, toward_high in ((left, False), (right, True)):
        for row in _lines(w):
            s = _stop(w, row, vcols, toward_high, up, down)
            span = range(s, 8) if toward_high else range(0, s + 1)
            for x in span:
                grid[row][x] = True

    for w, toward_high in ((up, False), (down, True)):
        for col in _lines(w):
            s = _stop(w, col, hrows, toward_high, left, right)
            span = range(s, 8) if toward_high else range(0, s + 1)
            for y in span:
                grid[y][col] = True

    return rows_to_bytes(["".join("#" if c else "." for c in r) for r in grid])


# (up, down, left, right) per code. CP437's own assignments, and the one place
# in this file where being wrong is being wrong about a standard rather than
# about a drawing.
BOX_ARMS = {
    0xB3: (1, 1, 0, 0), 0xB4: (1, 1, 1, 0), 0xB5: (1, 1, 2, 0),
    0xB6: (2, 2, 1, 0), 0xB7: (0, 2, 1, 0), 0xB8: (0, 1, 2, 0),
    0xB9: (2, 2, 2, 0), 0xBA: (2, 2, 0, 0), 0xBB: (0, 2, 2, 0),
    0xBC: (2, 0, 2, 0), 0xBD: (2, 0, 1, 0), 0xBE: (1, 0, 2, 0),
    0xBF: (0, 1, 1, 0), 0xC0: (1, 0, 0, 1), 0xC1: (1, 0, 1, 1),
    0xC2: (0, 1, 1, 1), 0xC3: (1, 1, 0, 1), 0xC4: (0, 0, 1, 1),
    0xC5: (1, 1, 1, 1), 0xC6: (1, 1, 0, 2), 0xC7: (2, 2, 0, 1),
    0xC8: (2, 0, 0, 2), 0xC9: (0, 2, 0, 2), 0xCA: (2, 0, 2, 2),
    0xCB: (0, 2, 2, 2), 0xCC: (2, 2, 0, 2), 0xCD: (0, 0, 2, 2),
    0xCE: (2, 2, 2, 2), 0xCF: (1, 0, 2, 2), 0xD0: (2, 0, 1, 1),
    0xD1: (0, 1, 2, 2), 0xD2: (0, 2, 1, 1), 0xD3: (2, 0, 0, 1),
    0xD4: (1, 0, 0, 2), 0xD5: (0, 1, 0, 2), 0xD6: (0, 2, 0, 1),
    0xD7: (2, 2, 1, 1), 0xD8: (1, 1, 2, 2), 0xD9: (1, 0, 1, 0),
    0xDA: (0, 1, 0, 1),
}


# --- drawn by hand: 0x00..0x1F -----------------------------------------------
# The dingbats. Eight lines each, '#' lit, and no apology for the ones that are
# a compromise: a spade at 8x8 is a compromise.

SMILE = art(
    "..####..",
    ".#....#.",
    "#.#..#.#",
    "#......#",
    "#.#..#.#",
    "#..##..#",
    ".#....#.",
    "..####..")

BULLET = art(
    BLANK,
    BLANK,
    "..###...",
    ".#####..",
    ".#####..",
    "..###...")

CIRCLE = art(
    BLANK,
    "..####..",
    ".##..##.",
    ".#....#.",
    ".#....#.",
    ".##..##.",
    "..####..")

ARROW_UP = art(
    "...#....",
    "..###...",
    ".#.#.#..",
    "...#....",
    "...#....",
    "...#....",
    "...#....")

ARROW_RIGHT = art(
    BLANK,
    "....#...",
    ".....#..",
    "#######.",
    ".....#..",
    "....#...")

TRI_RIGHT = art(
    "#.......",
    "###.....",
    "#####...",
    "#######.",
    "#####...",
    "###.....",
    "#.......")

TRI_UP = art(
    BLANK,
    BLANK,
    "...#....",
    "..###...",
    ".#####..",
    "#######.")

CTRL = {
    0x00: art(),
    0x01: SMILE,
    # The filled face is the outline's own ring, filled, with the eyes and the
    # mouth punched back out - one drawing, two glyphs.
    0x02: art(
        "..####..",
        ".######.",
        "##.##.##",
        "########",
        "##.##.##",
        "###..###",
        ".######.",
        "..####.."),
    0x03: art(
        BLANK,
        ".##.##..",
        "#######.",
        "#######.",
        "#######.",
        ".#####..",
        "..###...",
        "...#...."),
    0x04: art(
        "...#....",
        "..###...",
        ".#####..",
        "#######.",
        ".#####..",
        "..###...",
        "...#...."),
    0x05: art(
        "...##...",
        "..####..",
        "..####..",
        "##.##.##",
        "########",
        "...##...",
        "..####.."),
    0x06: art(
        "...#....",
        "..###...",
        ".#####..",
        "#######.",
        "#######.",
        "...#....",
        "..###..."),
    0x07: BULLET,
    0x08: invert(BULLET),
    0x09: CIRCLE,
    0x0A: invert(CIRCLE),
    0x0B: art(                       # male: a ring with an arrow out of it
        ".....###",
        ".......#",
        "......##",
        ".###.#..",
        "#...#...",
        "#...#...",
        "#...#...",
        ".###...."),
    0x0C: art(                       # female: a ring on a cross
        "..###...",
        ".#...#..",
        ".#...#..",
        ".#...#..",
        "..###...",
        "...#....",
        ".#####..",
        "...#...."),
    0x0D: art(                       # one note
        "....####",
        "....#..#",
        "....#...",
        "....#...",
        "....#...",
        ".####...",
        ".###....",
        BLANK),
    0x0E: art(                       # two notes, beamed
        "..######",
        "..#....#",
        "..#....#",
        "..#....#",
        "..#....#",
        "###..###",
        "###..###",
        BLANK),
    0x0F: art(                       # sun: a body, four rays, four diagonals
        "...#....",
        ".#...#..",
        "..###...",
        "#.###.#.",
        "..###...",
        ".#...#..",
        "...#...."),
    0x10: TRI_RIGHT,
    0x11: mirror(TRI_RIGHT),
    0x12: art(                       # up-down
        "...#....",
        "..###...",
        ".#.#.#..",
        "...#....",
        "...#....",
        ".#.#.#..",
        "..###...",
        "...#...."),
    0x13: art(
        ".##.##..",
        ".##.##..",
        ".##.##..",
        ".##.##..",
        ".##.##..",
        BLANK,
        ".##.##..",
        BLANK),
    0x14: art(                       # pilcrow
        ".#####..",
        "##.#.#..",
        "##.#.#..",
        ".###.#..",
        "...#.#..",
        "...#.#..",
        "...#.#..",
        BLANK),
    0x15: art(                       # section
        "..####..",
        ".##..##.",
        "..##....",
        "..####..",
        "....##..",
        ".##..##.",
        "..####..",
        BLANK),
    0x16: art(                       # the thick bar
        BLANK,
        BLANK,
        BLANK,
        "########",
        "########",
        "########",
        BLANK,
        BLANK),
    0x17: over(art(                  # up-down on a base
        "...#....",
        "..###...",
        ".#.#.#..",
        "...#....",
        ".#.#.#..",
        "..###...",
        "...#....",
        ".#####..")),
    0x18: ARROW_UP,
    0x19: flip(ARROW_UP),
    0x1A: ARROW_RIGHT,
    0x1B: mirror(ARROW_RIGHT),
    0x1C: art(                       # the right angle
        BLANK,
        BLANK,
        "#.......",
        "#.......",
        "#.......",
        "#.......",
        "######..",
        BLANK),
    0x1D: art(                       # left-right
        BLANK,
        "..#..#..",
        ".#....#.",
        "########",
        ".#....#.",
        "..#..#..",
        BLANK,
        BLANK),
    0x1E: TRI_UP,
    0x1F: flip(TRI_UP),
}


# --- drawn by hand: the accented letters -------------------------------------
# A base letter is authored ONCE, five rows tall, sitting at row 2. An accent
# is authored ONCE, in the two rows above it. The glyph is the OR of the two,
# which is less typing than thirty-one letters and, more to the point, makes
# every acute in the font the same acute. Row 7 is left clear so consecutive
# terminal rows do not touch; y spends it on its descender and the cedilla
# spends it on its hook, which is what row 7 is for.

BASE = {
    "a": [".###.", "....#", ".####", "#...#", ".####"],
    "e": [".###.", "#...#", "#####", "#....", ".###."],
    "i": ["..#..", "..#..", "..#..", "..#..", ".###."],
    "o": [".###.", "#...#", "#...#", "#...#", ".###."],
    "u": ["#...#", "#...#", "#...#", "#...#", ".####"],
    "c": [".###.", "#....", "#....", "#....", ".###."],
    "n": ["####.", "#...#", "#...#", "#...#", "#...#"],
    "y": ["#...#", "#...#", "#...#", ".####", "....#", ".###."],
    "A": [".###.", "#...#", "#####", "#...#", "#...#"],
    "E": ["#####", "#....", "####.", "#....", "#####"],
    "O": [".###.", "#...#", "#...#", "#...#", ".###."],
    "U": ["#...#", "#...#", "#...#", "#...#", ".###."],
    "N": ["#...#", "##..#", "#.#.#", "#..##", "#...#"],
    "C": [".###.", "#...#", "#....", "#...#", ".###."],
}

ACCENT = {
    "acute": (["...#.", "..#.."], 0),
    "grave": ([".#...", "..#.."], 0),
    "circ":  (["..#..", ".#.#."], 0),
    "trema": ([".#.#."], 0),
    "ring":  ([".###.", ".#.#."], 0),
    "tilde": (["..##.", "##..#"], 0),
    "cedil": (["..##."], 7),
}

# code -> (base, accent). The whole accented half of CP437, in one table.
ACCENTED = {
    0x80: ("C", "cedil"), 0x81: ("u", "trema"), 0x82: ("e", "acute"),
    0x83: ("a", "circ"),  0x84: ("a", "trema"), 0x85: ("a", "grave"),
    0x86: ("a", "ring"),  0x87: ("c", "cedil"), 0x88: ("e", "circ"),
    0x89: ("e", "trema"), 0x8A: ("e", "grave"), 0x8B: ("i", "trema"),
    0x8C: ("i", "circ"),  0x8D: ("i", "grave"), 0x8E: ("A", "trema"),
    0x8F: ("A", "ring"),  0x90: ("E", "acute"), 0x93: ("o", "circ"),
    0x94: ("o", "trema"), 0x95: ("o", "grave"), 0x96: ("u", "circ"),
    0x97: ("u", "grave"), 0x98: ("y", "trema"), 0x99: ("O", "trema"),
    0x9A: ("U", "trema"), 0xA0: ("a", "acute"), 0xA1: ("i", "acute"),
    0xA2: ("o", "acute"), 0xA3: ("u", "acute"), 0xA4: ("n", "tilde"),
    0xA5: ("N", "tilde"),
}


def accented(base, accent):
    pat, top = ACCENT[accent]
    return over(place(BASE[base], 2), place(pat, top))


# --- drawn by hand: the rest of 0x80..0xAF -----------------------------------

GUILLEMET = art(
    BLANK,
    "..#..#..",
    ".#..#...",
    "#..#....",
    ".#..#...",
    "..#..#..")

# The 1 and the slash are shared by both fractions; only the denominator moves.
FRAC = art(
    "##......",
    ".#...#..",
    ".#..#...",
    "###.#...",
    "...#....",
    "...#....",
    "..#.....",
    BLANK)

LATIN = {
    0x91: art(                       # ae: an a and an e sharing a stem
        BLANK,
        BLANK,
        ".##..##.",
        "...##..#",
        ".#######",
        "#..##...",
        ".###.###",
        BLANK),
    0x92: art(                       # AE
        BLANK,
        BLANK,
        "...#####",
        "..##....",
        ".######.",
        "##.#....",
        "#..#####",
        BLANK),
    0x9B: art(                       # cent
        BLANK,
        "...#....",
        "..####..",
        ".#.#....",
        ".#.#....",
        "..####..",
        "...#....",
        BLANK),
    0x9C: art(                       # pound
        BLANK,
        "..###...",
        ".#...#..",
        ".#......",
        "####....",
        ".#......",
        "#####...",
        BLANK),
    0x9D: art(                       # yen
        "#...#...",
        ".#.#....",
        "..#.....",
        "#####...",
        "..#.....",
        "#####...",
        "..#.....",
        BLANK),
    0x9E: art(                       # peseta: a P and a t
        BLANK,
        "###.....",
        "#..#....",
        "###..#..",
        "#...###.",
        "#....#..",
        "#....##.",
        BLANK),
    0x9F: art(                       # florin
        "...####.",
        "...#....",
        "...#....",
        ".#####..",
        "...#....",
        "...#....",
        "#..#....",
        ".##....."),
    0xA6: art(                       # feminine ordinal
        ".##.....",
        "...#....",
        ".###....",
        "#.##....",
        BLANK,
        "####....",
        BLANK,
        BLANK),
    0xA7: art(                       # masculine ordinal
        ".##.....",
        "#..#....",
        "#..#....",
        ".##.....",
        BLANK,
        "####....",
        BLANK,
        BLANK),
    0xA8: art(                       # inverted question
        BLANK,
        "..##....",
        BLANK,
        "..##....",
        ".##.....",
        "##......",
        "##..##..",
        ".####..."),
    0xA9: art(                       # reversed not
        BLANK,
        BLANK,
        BLANK,
        "######..",
        "#.......",
        BLANK,
        BLANK,
        BLANK),
    0xAA: art(                       # not
        BLANK,
        BLANK,
        BLANK,
        "######..",
        ".....#..",
        BLANK,
        BLANK,
        BLANK),
    0xAB: over(FRAC, art(            # one half
        BLANK, BLANK, BLANK, BLANK,
        ".....##.",
        ".......#",
        "......#.",
        ".....###")),
    0xAC: over(FRAC, art(            # one quarter
        BLANK, BLANK, BLANK, BLANK,
        ".....#.#",
        ".....#.#",
        ".....###",
        ".......#")),
    0xAD: art(                       # inverted exclamation
        BLANK,
        "...##...",
        BLANK,
        "...##...",
        "...##...",
        "...##...",
        "...##...",
        BLANK),
    0xAE: GUILLEMET,
    0xAF: mirror(GUILLEMET),
}


# --- drawn by hand: the Greek and the maths ----------------------------------
# 0xE0..0xF8 and 0xFB..0xFD. The shapes a board draws a formula with, and the
# ones a status line draws a degree sign with.

GREEK = {
    0xE0: art(                       # alpha
        BLANK, BLANK, BLANK,
        ".##..#..",
        "#..#.#..",
        "#..#.#..",
        ".##..##.",
        BLANK),
    0xE1: art(                       # sharp s
        BLANK,
        ".###....",
        "#...#...",
        "#.##....",
        "#...#...",
        "#...#...",
        "#.##....",
        BLANK),
    0xE2: art(                       # Gamma
        BLANK,
        "#####...",
        "#.......",
        "#.......",
        "#.......",
        "#.......",
        "#.......",
        BLANK),
    0xE3: art(                       # pi
        BLANK, BLANK, BLANK,
        "######..",
        ".#..#...",
        ".#..#...",
        ".#..##..",
        BLANK),
    0xE4: art(                       # Sigma
        BLANK,
        "######..",
        ".#......",
        "..#.....",
        ".#......",
        "#.......",
        "######..",
        BLANK),
    0xE5: art(                       # sigma
        BLANK, BLANK, BLANK,
        ".#######",
        "#..#....",
        "#..#....",
        ".##.....",
        BLANK),
    0xE6: art(                       # mu
        BLANK, BLANK, BLANK,
        "#...#...",
        "#...#...",
        "#...#...",
        "#####...",
        "#......."),
    0xE7: art(                       # tau
        BLANK, BLANK, BLANK,
        "######..",
        "..#.....",
        "..#.....",
        "..##....",
        BLANK),
    0xE8: art(                       # Phi
        BLANK,
        "...#....",
        ".#####..",
        "#..#..#.",
        "#..#..#.",
        ".#####..",
        "...#....",
        BLANK),
    0xE9: art(                       # Theta
        BLANK,
        "..###...",
        ".#...#..",
        ".#####..",
        ".#...#..",
        "..###...",
        BLANK,
        BLANK),
    0xEA: art(                       # Omega
        BLANK,
        "..###...",
        ".#...#..",
        ".#...#..",
        ".#...#..",
        "..#.#...",
        "###.###.",
        BLANK),
    0xEB: art(                       # delta
        "..###...",
        ".#...#..",
        "..##....",
        ".####...",
        "#....#..",
        "#....#..",
        ".####...",
        BLANK),
    0xEC: art(                       # infinity
        BLANK, BLANK, BLANK,
        ".##.##..",
        "#..#..#.",
        ".##.##..",
        BLANK,
        BLANK),
    0xED: art(                       # phi
        BLANK, BLANK,
        "...#....",
        "..###...",
        ".#.#.#..",
        ".#.#.#..",
        "..###...",
        "...#...."),
    0xEE: art(                       # epsilon
        BLANK, BLANK,
        "..###...",
        ".#......",
        ".###....",
        ".#......",
        "..###...",
        BLANK),
    0xEF: art(                       # intersection
        BLANK, BLANK,
        "..###...",
        ".#...#..",
        ".#...#..",
        ".#...#..",
        ".#...#..",
        BLANK),
    0xF0: art(                       # identical to
        BLANK, BLANK,
        "######..",
        BLANK,
        "######..",
        BLANK,
        "######..",
        BLANK),
    0xF1: art(                       # plus-minus
        BLANK,
        "...#....",
        "...#....",
        "#######.",
        "...#....",
        BLANK,
        "#######.",
        BLANK),
    0xF2: art(                       # greater or equal
        "##......",
        "..##....",
        "....##..",
        "..##....",
        "##......",
        BLANK,
        "######..",
        BLANK),
    0xF4: art(                       # integral, top half
        "...###..",
        "..##....",
        "..##....",
        "..##....",
        "..##....",
        "..##....",
        "..##....",
        "..##...."),
    0xF6: art(                       # division
        BLANK, BLANK,
        "...#....",
        BLANK,
        "#######.",
        BLANK,
        "...#....",
        BLANK),
    0xF7: art(                       # approximately equal
        BLANK, BLANK,
        ".##..#..",
        "#..##...",
        BLANK,
        ".##..#..",
        "#..##...",
        BLANK),
    0xF8: art(                       # degree
        ".###....",
        ".#.#....",
        ".###....",
        BLANK, BLANK, BLANK, BLANK, BLANK),
    0xFB: art(                       # radical
        "..######",
        "..#.....",
        "..#.....",
        "..#.....",
        "#.#.....",
        "#.#.....",
        ".##.....",
        BLANK),
    0xFC: art(                       # superscript n
        "###.....",
        "#..#....",
        "#..#....",
        "#..#....",
        BLANK, BLANK, BLANK, BLANK),
    0xFD: art(                       # superscript 2
        ".##.....",
        "#..#....",
        "..##....",
        "####....",
        BLANK, BLANK, BLANK, BLANK),
}
GREEK[0xF3] = mirror(GREEK[0xF2])    # less or equal is greater or equal, turned
GREEK[0xF5] = art(                   # integral, bottom half - the foot hooks
    "..##....",                      # the OTHER way, so that stacking the two
    "..##....",                      # draws one stroke and not an S
    "..##....",
    "..##....",
    "..##....",
    "..##....",
    "..##....",
    "###.....")


# --- the table ---------------------------------------------------------------

NAMES = {
    0x00: "blank", 0x01: "smiling face", 0x02: "filled smiling face",
    0x03: "heart", 0x04: "diamond", 0x05: "club", 0x06: "spade",
    0x07: "bullet", 0x08: "inverse bullet", 0x09: "circle",
    0x0A: "inverse circle", 0x0B: "male sign", 0x0C: "female sign",
    0x0D: "eighth note", 0x0E: "beamed notes", 0x0F: "sun",
    0x10: "right triangle", 0x11: "left triangle", 0x12: "up-down arrow",
    0x13: "double exclamation", 0x14: "pilcrow", 0x15: "section sign",
    0x16: "thick bar", 0x17: "up-down arrow with base", 0x18: "up arrow",
    0x19: "down arrow", 0x1A: "right arrow", 0x1B: "left arrow",
    0x1C: "right angle", 0x1D: "left-right arrow", 0x1E: "up triangle",
    0x1F: "down triangle",
    0x80: "C cedilla", 0x81: "u diaeresis", 0x82: "e acute",
    0x83: "a circumflex", 0x84: "a diaeresis", 0x85: "a grave",
    0x86: "a ring", 0x87: "c cedilla", 0x88: "e circumflex",
    0x89: "e diaeresis", 0x8A: "e grave", 0x8B: "i diaeresis",
    0x8C: "i circumflex", 0x8D: "i grave", 0x8E: "A diaeresis",
    0x8F: "A ring", 0x90: "E acute", 0x91: "ae", 0x92: "AE",
    0x93: "o circumflex", 0x94: "o diaeresis", 0x95: "o grave",
    0x96: "u circumflex", 0x97: "u grave", 0x98: "y diaeresis",
    0x99: "O diaeresis", 0x9A: "U diaeresis", 0x9B: "cent", 0x9C: "pound",
    0x9D: "yen", 0x9E: "peseta", 0x9F: "florin", 0xA0: "a acute",
    0xA1: "i acute", 0xA2: "o acute", 0xA3: "u acute", 0xA4: "n tilde",
    0xA5: "N tilde", 0xA6: "feminine ordinal", 0xA7: "masculine ordinal",
    0xA8: "inverted question", 0xA9: "reversed not", 0xAA: "not sign",
    0xAB: "one half", 0xAC: "one quarter", 0xAD: "inverted exclamation",
    0xAE: "left guillemet", 0xAF: "right guillemet",
    0xB0: "light shade", 0xB1: "medium shade", 0xB2: "dark shade",
    0xDB: "full block", 0xDC: "lower half block", 0xDD: "left half block",
    0xDE: "right half block", 0xDF: "upper half block",
    0xE0: "alpha", 0xE1: "sharp s", 0xE2: "Gamma", 0xE3: "pi",
    0xE4: "Sigma", 0xE5: "sigma", 0xE6: "mu", 0xE7: "tau", 0xE8: "Phi",
    0xE9: "Theta", 0xEA: "Omega", 0xEB: "delta", 0xEC: "infinity",
    0xED: "phi", 0xEE: "epsilon", 0xEF: "intersection",
    0xF0: "identical to", 0xF1: "plus-minus", 0xF2: "greater or equal",
    0xF3: "less or equal", 0xF4: "integral top", 0xF5: "integral bottom",
    0xF6: "division sign", 0xF7: "approximately equal", 0xF8: "degree",
    0xF9: "centre dot, large", 0xFA: "centre dot", 0xFB: "radical",
    0xFC: "superscript n", 0xFD: "superscript 2", 0xFE: "centred square",
    0xFF: "blank",
}

# The box-drawing names are derived from the arms, so a wrong assignment in
# BOX_ARMS shows up in the comment as well as in the pixels.
_ARM_NAMES = {(1, 1, 0, 0): "vertical", (0, 0, 1, 1): "horizontal",
              (1, 1, 1, 1): "cross", (0, 1, 0, 1): "down-right",
              (0, 1, 1, 0): "down-left", (1, 0, 0, 1): "up-right",
              (1, 0, 1, 0): "up-left", (1, 1, 0, 1): "right tee",
              (1, 1, 1, 0): "left tee", (0, 1, 1, 1): "down tee",
              (1, 0, 1, 1): "up tee"}


def _box_name(arms):
    shape = tuple(1 if a else 0 for a in arms)
    weights = sorted(set(a for a in arms if a))
    if weights == [1]:
        weight = "single"
    elif weights == [2]:
        weight = "double"
    else:
        vert = "double" if max(arms[0], arms[1]) == 2 else "single"
        horiz = "double" if max(arms[2], arms[3]) == 2 else "single"
        weight = "%s vertical, %s horizontal" % (vert, horiz)
    return "box %s %s" % (_ARM_NAMES[shape], weight)


def name(code):
    if code in BOX_ARMS:
        return _box_name(BOX_ARMS[code])
    return NAMES[code]


def glyph(code):
    if code in CTRL:
        return CTRL[code]
    if code in ACCENTED:
        return accented(*ACCENTED[code])
    if code in LATIN:
        return LATIN[code]
    if code in GREEK:
        return GREEK[code]
    if code in BOX_ARMS:
        return box(*BOX_ARMS[code])
    return {0xB0: SHADE_LIGHT, 0xB1: SHADE_MEDIUM, 0xB2: SHADE_DARK,
            0xDB: BLOCK_FULL, 0xDC: BLOCK_LOWER, 0xDD: BLOCK_LEFT,
            0xDE: BLOCK_RIGHT, 0xDF: BLOCK_UPPER, 0xF9: DOT_BIG,
            0xFA: DOT_SMALL, 0xFE: SQUARE, 0xFF: art()}[code]


def table():
    return [(c, name(c), glyph(c)) for c in CODES]


# --- the file ----------------------------------------------------------------

BANNER = """\
; os88cp437.inc - the CP437 glyphs a machine cannot be asked for.
;
; GENERATED by tools/cp437font.py, and COMMITTED. Every `make` regenerates it
; and diffs it, the way it does docs/INDEX.md and for that reason: a generated
; file that is not checked is a generated file that is hand-edited once and
; then lies about where it came from. DO NOT EDIT THIS FILE - edit the tool,
; run `python3 tools/cp437font.py`, look at `--show`, and commit both.
;
; SPEC.md 70.8.6 is the contract. The package builds con_glyf, 0..255, at
; launch: 32..127 come off the machine because they are ASCII and every ROM
; agrees about them, and these 160 - 0x00..0x1F and 0x80..0xFF - ship, because
; they are CP437's own and a clone ROM is free to differ.
;
; Eight bytes a glyph, ROW 0 FIRST (the top scanline), BIT 7 LEFTMOST, 1 lit.
; Clean-room: the shades, the blocks and the box-drawing set are drawn by
; arithmetic and everything else is eight lines of ASCII art in the tool. No
; font file was read to make this one.
"""


def build():
    out = [BANNER, "",
           "CON_CP437_N equ 160               ; 0..31 then 128..255", "",
           "con_cp437:"]
    for code, nm, g in table():
        out.append("    db " + ", ".join("0x%02X" % b for b in g)
                   + "    ; 0x%02X %s" % (code, nm))
    return "\n".join(out) + "\n"


# --- the modes ---------------------------------------------------------------

def selfcheck():
    """The shape assertions. 160 rows of 8 bytes is 1,280 bytes, and the check
    is made against the TEXT this tool emits rather than against the table it
    emits it from - a bug in the emitter is exactly what a check against the
    table cannot see."""
    ok = True
    rows = [l for l in build().splitlines() if l.startswith("    db ")]
    if len(rows) != 160:
        print("cp437font: %d db lines, expected 160" % len(rows))
        ok = False
    total = 0
    for i, line in enumerate(rows):
        operands = line.split(";")[0].strip()[3:].split(",")
        vals = [int(v.strip(), 16) for v in operands]
        if len(vals) != 8:
            print("cp437font: row %d has %d bytes, expected 8" % (i, len(vals)))
            ok = False
        if any(v < 0 or v > 0xFF for v in vals):
            print("cp437font: row %d is not eight bytes of eight bits" % i)
            ok = False
        total += len(vals)
    if total != 1280:
        print("cp437font: %d bytes of glyph data, expected 1,280" % total)
        ok = False
    codes = [c for c, _, _ in table()]
    if codes != CODES:
        print("cp437font: the table is not in 0x00..0x1F then 0x80..0xFF order")
        ok = False
    return ok


def show_all():
    """All 160, four across, to be LOOKED at. A glyph nobody has looked at is
    a glyph that is wrong."""
    entries = table()
    for i in range(0, len(entries), 4):
        band = entries[i:i + 4]
        print("  ".join(("0x%02X %s" % (c, n))[:18].ljust(18) for c, n, _ in band))
        pics = [show(g) for _, _, g in band]
        for row in range(8):
            print("  ".join(p[row].ljust(18) for p in pics))
        print()


def main():
    if not selfcheck():
        return 1
    text = build()
    if "--selfcheck" in sys.argv:
        return 0
    if "--show" in sys.argv:
        show_all()
        return 0
    if "--check" in sys.argv:
        try:
            with open(OUT, encoding="utf-8") as f:
                cur = f.read()
        except FileNotFoundError:
            cur = None
        if cur != text:
            print("cp437font: apps/os88cp437.inc is stale - run "
                  "tools/cp437font.py")
            if cur is not None:
                want = text.splitlines()
                have = cur.splitlines()
                for i in range(max(len(want), len(have))):
                    w = want[i] if i < len(want) else "(end of file)"
                    h = have[i] if i < len(have) else "(end of file)"
                    if w != h:
                        print("  line %d:" % (i + 1))
                        print("    in the tree: %s" % h)
                        print("    generated:   %s" % w)
                        break
            return 1
        return 0
    # newline="\n": the file is LF in the tree, and Windows would otherwise
    # rewrite it CRLF and show every line as changed.
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("cp437font: wrote apps/os88cp437.inc (160 glyphs, 1,280 bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
