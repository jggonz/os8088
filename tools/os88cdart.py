#!/usr/bin/env python3
"""os88cdart: CDRack's pictures and fonts, made on the host (SPEC.md 119.6).

    python3 tools/os88cdart.py --out build/cdart.inc [--png DIR]
    python3 tools/os88cdart.py --selfcheck

Every picture the CD player draws that is not a filled rectangle or a glyph
of the system font is made HERE, from definitions in this file - no image is
read, no service is asked, and the same run writes the same bytes - and
emitted as an asm include of 1bpp bitmaps the package puts down with
OSAPI_GFX_BLIT1 under a pen (one call a picture, any colour pair, and the
mono adapters' lit-is-white handled by the package). The design it follows
is docs/cdrack/design/, the owner's three mockups.

  LCD DIGITS   seven segments, slanted like the mockup's, in three sets:
               `dl` the full layout's (VGA/EGA/Hercules rows), `cb`/`cs` the
               compact layout's big and small on the CGA (rows a 2.4:1 pixel
               tall), and `cb2`/`cs2` the same doubled for the compact
               layout on square-ish pixels.
  SMALL CAPS   a 5x7 face for every label ("COMPACT DISC PLAYER", PREV,
               ELAPSED...): the 80s panel lettering the system face is not.
  THE LOGO     "CDRack" in heavy letters, drawn as pixel art.
  ICONS        the transport (prev, rew, play, pause, stop, ff, next, eject),
               the speaker, a check, the expand arrow, the small play mark,
               the disc mark - each at 13 rows and at the CGA's 7.

The bitmap record (cdart_* labels): dw width; db height; db stride; then
height rows of stride bytes, bit 7 leftmost, 1 = ink.
"""
import argparse
import os
import struct
import sys
import zlib

# =============================================================================
# a bitmap is a list of rows, each a list of 0/1
# =============================================================================


def blank(w, h):
    return [[0] * w for _ in range(h)]


def from_art(lines):
    w = max(len(l) for l in lines)
    return [[1 if c == '#' else 0 for c in l.ljust(w)] for l in lines]


def scale(b, sx, sy):
    return [[px for px in row for _ in range(sx)] for row in b for _ in range(sy)]


def rows_every(b, keep):
    """Sample rows: `keep` of the source's, nearest - the CGA's half height."""
    h = len(b)
    out = []
    for i in range(keep):
        src = min(h - 1, int((i + 0.5) * h / keep))
        # a row of the CGA form keeps any ink of the two rows it stands for
        # (OR), so a 1-pixel stroke never vanishes
        lo = int(i * h / keep)
        hi = max(lo + 1, int((i + 1) * h / keep))
        out.append([max(b[r][x] for r in range(lo, hi)) for x in range(len(b[0]))])
    return out


def fill_poly(b, pts):
    """Scanline fill of a polygon, pixel centres inside - no library."""
    h, w = len(b), len(b[0])
    for y in range(h):
        yc = y + 0.5
        xs = []
        n = len(pts)
        for i in range(n):
            x1, y1 = pts[i]
            x2, y2 = pts[(i + 1) % n]
            if (y1 <= yc < y2) or (y2 <= yc < y1):
                xs.append(x1 + (yc - y1) * (x2 - x1) / (y2 - y1))
        xs.sort()
        for i in range(0, len(xs) - 1, 2):
            for x in range(w):
                if xs[i] <= x + 0.5 < xs[i + 1]:
                    b[y][x] = 1


def rect(b, x, y, w, h, v=1):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            if 0 <= yy < len(b) and 0 <= xx < len(b[0]):
                b[yy][xx] = v


# =============================================================================
# THE LCD DIGITS - seven segments, slanted
# =============================================================================
SEGS = {          # a b c d e f g, the usual order
    '0': 'abcdef', '1': 'bc', '2': 'abged', '3': 'abgcd', '4': 'fgbc',
    '5': 'afgcd', '6': 'afgedc', '7': 'abc', '8': 'abcdefg',
    '9': 'abcdfg', ' ': '', '-': 'g',
}
DIGIT_ORDER = '0123456789 -'


def seg_digit(ch, w, h, t, slant, style='mitre'):
    """One digit: w x h before the slant, stroke t, the top shifted right by
    `slant` px; each segment a hexagon with mitred ends, a gap between.

    'mitre' tucks every segment inside the corners, which needs room: at 16
    rows and a 3-pixel stroke a vertical segment had no straight part left
    and the compact LCD's digits were two dots and a bar. 'block' runs the
    verticals the full half-height and fits the horizontals between them -
    the cheap calculator's LCD, legible down to 12 rows."""
    if style == 'block':
        return seg_block(ch, w, h, t, slant)
    W = w + slant
    b = blank(W, h)
    mid = (h - t) / 2.0

    def sx(x, y):                       # the slant: higher rows lean right
        return x + slant * (1 - y / float(h))

    def hseg(y):
        g = 1.0
        pts = [(g + t / 2, y + t / 2), (g + t, y), (w - g - t, y),
               (w - g - t / 2, y + t / 2), (w - g - t, y + t), (g + t, y + t)]
        return [(sx(px, py), py) for px, py in pts]

    def vseg(x, y0, y1):
        g = 1.0
        pts = [(x + t / 2, y0 + g + t / 2), (x + t, y0 + g + t), (x + t, y1 - g - t),
               (x + t / 2, y1 - g - t / 2), (x, y1 - g - t), (x, y0 + g + t)]
        return [(sx(px, py), py) for px, py in pts]

    shapes = {
        'a': hseg(0), 'g': hseg(mid), 'd': hseg(h - t),
        'f': vseg(0, 0, mid + t / 2), 'b': vseg(w - t, 0, mid + t / 2),
        'e': vseg(0, mid + t / 2, h), 'c': vseg(w - t, mid + t / 2, h),
    }
    for s in SEGS[ch]:
        fill_poly(b, shapes[s])
    return b


def seg_block(ch, w, h, t, slant):
    """seg_digit's 'block' style: full-height verticals, horizontals between."""
    b = blank(w + slant, h)
    mid = (h - t) / 2.0
    g, e = 0.6, t / 2.0

    def sx(x, y):
        return x + slant * (1 - y / float(h))

    def hseg(y):
        x0, x1 = t + g, w - t - g
        pts = [(x0, y + e), (x0 + e, y), (x1 - e, y), (x1, y + e),
               (x1 - e, y + t), (x0 + e, y + t)]
        return [(sx(px, py), py) for px, py in pts]

    def vseg(x, y0, y1):
        y0, y1 = y0 + g, y1 - g
        pts = [(x + e, y0), (x + t, y0 + e), (x + t, y1 - e), (x + e, y1),
               (x, y1 - e), (x, y0 + e)]
        return [(sx(px, py), py) for px, py in pts]

    c = mid + e
    shapes = {
        'a': hseg(0), 'g': hseg(mid), 'd': hseg(h - t),
        'f': vseg(0, 0, c), 'b': vseg(w - t, 0, c),
        'e': vseg(0, c, h), 'c': vseg(w - t, c, h),
    }
    for sg in SEGS[ch]:
        fill_poly(b, shapes[sg])
    return b


def colon(h, t, slant, w=None):
    w = w or t + slant + 1
    b = blank(w, h)
    for yc in (h * 0.30, h * 0.70):
        y0 = int(round(yc - t / 2.0))
        x0 = int(round(slant * (1 - yc / h)))
        rect(b, x0, y0, t, t)
    return b


DIGIT_SETS = {
    # name: (w, h, stroke, slant) - the full layout's, then the compact's
    'dl': (18, 30, 4, 4, 'mitre'),      # VGA / EGA / Hercules
    'cb': (18, 16, 3, 3, 'block'),      # the CGA's compact time (2.4:1 rows)
    'cs': (11, 12, 2, 2, 'block'),      # ...and its track's length
    'cb2': (18, 32, 4, 4, 'mitre'),     # the same two where the compact
    'cs2': (11, 24, 3, 2, 'block'),     # player's rows are doubled
}


# =============================================================================
# THE SMALL CAPS - 5 x 7
# =============================================================================
CAPS = {
    'A': ['.###.', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    'B': ['####.', '#...#', '#...#', '####.', '#...#', '#...#', '####.'],
    'C': ['.###.', '#...#', '#....', '#....', '#....', '#...#', '.###.'],
    'D': ['####.', '#...#', '#...#', '#...#', '#...#', '#...#', '####.'],
    'E': ['#####', '#....', '#....', '####.', '#....', '#....', '#####'],
    'F': ['#####', '#....', '#....', '####.', '#....', '#....', '#....'],
    'G': ['.###.', '#...#', '#....', '#.###', '#...#', '#...#', '.####'],
    'H': ['#...#', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    'I': ['.###.', '..#..', '..#..', '..#..', '..#..', '..#..', '.###.'],
    'J': ['..###', '...#.', '...#.', '...#.', '...#.', '#..#.', '.##..'],
    'K': ['#...#', '#..#.', '#.#..', '##...', '#.#..', '#..#.', '#...#'],
    'L': ['#....', '#....', '#....', '#....', '#....', '#....', '#####'],
    'M': ['#...#', '##.##', '#.#.#', '#.#.#', '#...#', '#...#', '#...#'],
    'N': ['#...#', '#...#', '##..#', '#.#.#', '#..##', '#...#', '#...#'],
    'O': ['.###.', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
    'P': ['####.', '#...#', '#...#', '####.', '#....', '#....', '#....'],
    'Q': ['.###.', '#...#', '#...#', '#...#', '#.#.#', '#..#.', '.##.#'],
    'R': ['####.', '#...#', '#...#', '####.', '#.#..', '#..#.', '#...#'],
    'S': ['.####', '#....', '#....', '.###.', '....#', '....#', '####.'],
    'T': ['#####', '..#..', '..#..', '..#..', '..#..', '..#..', '..#..'],
    'U': ['#...#', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
    'V': ['#...#', '#...#', '#...#', '#...#', '#...#', '.#.#.', '..#..'],
    'W': ['#...#', '#...#', '#...#', '#.#.#', '#.#.#', '#.#.#', '.#.#.'],
    'X': ['#...#', '#...#', '.#.#.', '..#..', '.#.#.', '#...#', '#...#'],
    'Y': ['#...#', '#...#', '.#.#.', '..#..', '..#..', '..#..', '..#..'],
    'Z': ['#####', '....#', '...#.', '..#..', '.#...', '#....', '#####'],
    '0': ['.###.', '#...#', '#..##', '#.#.#', '##..#', '#...#', '.###.'],
    '1': ['..#..', '.##..', '..#..', '..#..', '..#..', '..#..', '.###.'],
    '2': ['.###.', '#...#', '....#', '...#.', '..#..', '.#...', '#####'],
    '3': ['####.', '....#', '....#', '.###.', '....#', '....#', '####.'],
    '4': ['...#.', '..##.', '.#.#.', '#..#.', '#####', '...#.', '...#.'],
    '5': ['#####', '#....', '####.', '....#', '....#', '#...#', '.###.'],
    '6': ['.###.', '#....', '#....', '####.', '#...#', '#...#', '.###.'],
    '7': ['#####', '....#', '...#.', '..#..', '.#...', '.#...', '.#...'],
    '8': ['.###.', '#...#', '#...#', '.###.', '#...#', '#...#', '.###.'],
    '9': ['.###.', '#...#', '#...#', '.####', '....#', '....#', '.###.'],
    '.': ['.....', '.....', '.....', '.....', '.....', '.##..', '.##..'],
    ':': ['.....', '.##..', '.##..', '.....', '.##..', '.##..', '.....'],
    '%': ['##..#', '##..#', '...#.', '..#..', '.#...', '#..##', '#..##'],
    '/': ['....#', '....#', '...#.', '..#..', '.#...', '#....', '#....'],
    '-': ['.....', '.....', '.....', '#####', '.....', '.....', '.....'],
    '*': ['.....', '.....', '.###.', '.###.', '.###.', '.....', '.....'],
    ' ': ['.....'] * 7,
}
CAPS_ORDER = ' %*-./0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZ'


# =============================================================================
# THE LOGO - "CDRack", heavy, pixel art at 2x
# =============================================================================
LOGO_LETTERS = {                        # 10 rows; caps full height, the
    'C': [".#######.",                  # lower case on a 7-row x-height
          "#########",
          "###...###",
          "###......",
          "###......",
          "###......",
          "###......",
          "###...###",
          "#########",
          ".#######."],
    'D': ["########.",
          "#########",
          "###...###",
          "###...###",
          "###...###",
          "###...###",
          "###...###",
          "###...###",
          "#########",
          "########."],
    'R': ["########.",
          "#########",
          "###...###",
          "###...###",
          "#########",
          "########.",
          "###.###..",
          "###..###.",
          "###...###",
          "###...###"],
    'a': ["........",
          "........",
          "........",
          ".######.",
          "......##",
          ".#######",
          "###...##",
          "###...##",
          "########",
          ".####.##"],
    'c': ["........",
          "........",
          "........",
          ".######.",
          "########",
          "###.....",
          "###.....",
          "###.....",
          "########",
          ".######."],
    'k': ["###.....",
          "###.....",
          "###.....",
          "###..###",
          "###.###.",
          "######..",
          "#####...",
          "######..",
          "###.###.",
          "###..###"],
}
LOGO_WORD = 'CDRack'


def logo():
    """'CDRack', the letters 2 px apart, at twice the drawn size: 20 rows."""
    h = 10
    w = sum(len(LOGO_LETTERS[c][0]) for c in LOGO_WORD) + 2 * (len(LOGO_WORD) - 1)
    b = blank(w, h)
    x = 0
    for c in LOGO_WORD:
        paste(b, from_art(LOGO_LETTERS[c]), x, 0)
        x += len(LOGO_LETTERS[c][0]) + 2
    return scale(b, 2, 2)


# =============================================================================
# ICONS - 15 x 13 (13 rows; the CGA's are the same sampled to 7)
# =============================================================================
def icon(draw, w=15, h=13):
    b = blank(w, h)
    draw(b)
    return b


def tri_right(b, x0, x1, ymid, half):
    fill_poly(b, [(x0, ymid - half), (x1, ymid), (x0, ymid + half)])


def tri_left(b, x0, x1, ymid, half):
    fill_poly(b, [(x1, ymid - half), (x0, ymid), (x1, ymid + half)])


ICONS = {
    'prev': lambda b: (rect(b, 2, 1, 3, 11), tri_left(b, 4, 13, 6.5, 5.5)),
    'rew': lambda b: (tri_left(b, 0, 7.5, 6.5, 5.5), tri_left(b, 7, 14.5, 6.5, 5.5)),
    'play': lambda b: tri_right(b, 3, 13, 6.5, 6),
    'pause': lambda b: (rect(b, 3, 1, 3, 11), rect(b, 9, 1, 3, 11)),
    'stop': lambda b: rect(b, 2, 1, 11, 11),
    'ff': lambda b: (tri_right(b, 0, 7.5, 6.5, 5.5), tri_right(b, 7, 14.5, 6.5, 5.5)),
    'next': lambda b: (tri_right(b, 1, 10, 6.5, 5.5), rect(b, 10, 1, 3, 11)),
    'eject': lambda b: (fill_poly(b, [(1.5, 8), (7.5, 1.5), (13.5, 8)]), rect(b, 2, 10, 11, 2)),
}

SPEAKER = from_art([
    ".........#......",
    "........##...#..",
    ".......###....#.",
    "##....####..#..#",
    "####.#####...#.#",
    "##########.#.#.#",
    "##########.#.#.#",
    "##########.#.#.#",
    "##########.#.#.#",
    "####.#####...#.#",
    "##....####..#..#",
    ".......###....#.",
    "........##...#..",
    ".........#......",
])

CHECK = from_art([
    "#########",
    "#.......#",
    "#......##",
    "#.....###",
    "##...##.#",
    "###.##..#",
    "#.###...#",
    "#..#....#",
    "#########",
])

UNCHECK = from_art([
    "#########",
    "#.......#",
    "#.......#",
    "#.......#",
    "#.......#",
    "#.......#",
    "#.......#",
    "#.......#",
    "#########",
])

EXPAND = from_art([
    "############",
    "#..........#",
    "#.....######",
    "#......##..#",
    "#.....###..#",
    "#....###.#.#",
    "#...###....#",
    "#..###.....#",
    "#.###......#",
    "#..#.......#",
    "#..........#",
    "############",
])

PLAYMARK = from_art([
    "##.....",
    "####...",
    "######.",
    "#######",
    "######.",
    "####...",
    "##.....",
])

DISC_LOWER = {                          # "disc" - bold, 7 rows, outlined below
    'd': ['....##', '....##', '.#####', '##..##', '##..##', '##..##', '.#####'],
    'i': ['##', '..', '##', '##', '##', '##', '##'],
    's': ['......', '......', '.####.', '##....', '.####.', '....##', '#####.'],
    'c': ['.....', '.....', '.####', '##...', '##...', '##...', '.####'],
}


def caps_text(text, gap=1):
    """A string of the small caps as one bitmap (7 rows)."""
    cols = []
    for ch in text:
        g = CAPS.get(ch, CAPS[' '])
        for x in range(5):
            cols.append([1 if g[y][x] == '#' else 0 for y in range(7)])
        for _ in range(gap):
            cols.append([0] * 7)
    w = len(cols)
    return [[cols[x][y] for x in range(w)] for y in range(7)]


def outline(b):
    """The edge of an inked shape, 1 px, inside it: an OUTLINED letter."""
    h, w = len(b), len(b[0])
    o = blank(w, h)
    for y in range(h):
        for x in range(w):
            if b[y][x] and any(not (0 <= x + dx < w and 0 <= y + dy < h)
                               or not b[y + dy][x + dx]
                               for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                o[y][x] = 1
    return o


def paste(dst, src, x0, y0):
    for y, row in enumerate(src):
        for x, v in enumerate(row):
            if v and 0 <= y0 + y < len(dst) and 0 <= x0 + x < len(dst[0]):
                dst[y0 + y][x0 + x] = 1


def discmark():
    """'COMPACT' over an outlined 'disc' over 'DIGITAL AUDIO': the panel's
    mark, set from this file's own faces."""
    word = []
    for ch in 'disc':
        word.append(scale(from_art([r.replace('.', ' ') for r in DISC_LOWER[ch]]), 2, 2))
    w = sum(len(g[0]) for g in word) + 2 * (len(word) - 1) + 2
    big = blank(w, 16)
    x = 1
    for g in word:
        paste(big, g, x, 1)
        x += len(g[0]) + 2
    big = outline(big)
    top = caps_text('COMPACT')
    bot = caps_text('DIGITAL AUDIO')
    W = max(len(big[0]), len(bot[0]))
    b = blank(W, 7 + 1 + 16 + 1 + 7)
    paste(b, top, W - len(top[0]), 0)
    paste(b, big, 0, 8)
    paste(b, bot, 0, 25)
    return b


def all_bitmaps():
    """name -> bitmap, in the order they are emitted."""
    bm = {}
    for name, (w, h, t, sl, st) in DIGIT_SETS.items():
        for i, ch in enumerate(DIGIT_ORDER):
            bm['%s_%d' % (name, i)] = seg_digit(ch, w, h, t, sl, st)
        bm['%s_col' % name] = colon(h, max(2, t - 1), sl)
    bm['logo'] = logo()
    for n, f in ICONS.items():
        b = icon(f)
        bm['i_' + n] = b
        bm['h_' + n] = rows_every(b, 7)          # the CGA's
    bm['speaker'] = SPEAKER
    bm['h_speaker'] = rows_every(SPEAKER, 7)
    bm['check'] = CHECK
    bm['uncheck'] = UNCHECK
    bm['h_check'] = rows_every(CHECK, 6)
    bm['h_uncheck'] = rows_every(UNCHECK, 6)
    bm['expand'] = EXPAND
    bm['h_expand'] = rows_every(EXPAND, 7)
    bm['playmark'] = PLAYMARK
    bm['discmark'] = discmark()
    return bm


def pack(b):
    w, h = len(b[0]), len(b)
    stride = (w + 7) // 8
    out = bytearray()
    for row in b:
        for x0 in range(0, stride * 8, 8):
            v = 0
            for k in range(8):
                x = x0 + k
                if x < w and row[x]:
                    v |= 0x80 >> k
            out.append(v)
    return w, h, stride, bytes(out)


def emit(bm):
    lines = ["; GENERATED by tools/os88cdart.py - do not edit (SPEC.md 119.6)",
             "; the record: dw width; db height; db stride; rows x stride bytes",
             ""]
    for name, b in bm.items():
        w, h, stride, data = pack(b)
        lines.append("cdart_%s:" % name)
        lines.append("    dw %d" % w)
        lines.append("    db %d, %d" % (h, stride))
        for i in range(0, len(data), 16):
            lines.append("    db " + ", ".join("0x%02X" % v for v in data[i:i + 16]))
    # the digit tables: one word per digit of each set, DIGIT_ORDER's order
    for name in DIGIT_SETS:
        lines.append("cdart_%s_tab:" % name)
        lines.append("    dw " + ", ".join("cdart_%s_%d" % (name, i)
                                         for i in range(len(DIGIT_ORDER))))
    # each digit set's sizes, so the package's layout arithmetic is the art's
    for name in DIGIT_SETS:
        d, c = bm[name + '_0'], bm[name + '_col']
        lines.append("CDART_%s_W equ %d" % (name.upper(), len(d[0])))
        lines.append("CDART_%s_H equ %d" % (name.upper(), len(d)))
        lines.append("CDART_%s_CW equ %d" % (name.upper(), len(c[0])))
    # the small caps: an ASCII index table from ' ' (32) to 'Z' (90), 0 = none,
    # and the glyphs, 7 bytes each, bits 7..3
    lines.append("CDART_CAPS_FIRST equ 32")
    lines.append("CDART_CAPS_LAST equ 90")
    lines.append("cdart_caps_ix:")
    ix = []
    for c in range(32, 91):
        ch = chr(c)
        ix.append(CAPS_ORDER.index(ch) + 1 if ch in CAPS else 0)
    lines.append("    db " + ", ".join(str(v) for v in ix))
    lines.append("cdart_caps:")
    for ch in CAPS_ORDER:
        rows = []
        for r in CAPS[ch]:
            v = 0
            for k, c in enumerate(r):
                if c == '#':
                    v |= 0x80 >> k
            rows.append(v)
        lines.append("    db " + ", ".join("0x%02X" % v for v in rows) + "  ; %r" % ch)
    return "\n".join(lines) + "\n"


def write_png(path, b, scale_=3):
    """A plain greyscale PNG of one bitmap, ink black on white - no PIL."""
    w, h = len(b[0]) * scale_, len(b) * scale_
    raw = bytearray()
    for row in b:
        line = bytearray([0])
        for px in row:
            line += bytes([0 if px else 255]) * scale_
        for _ in range(scale_):
            raw += line
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def selfcheck():
    bad = 0
    bm = all_bitmaps()
    # every digit set: the right size, each digit different, 8 the union
    for name, (w, h, t, sl, st) in DIGIT_SETS.items():
        ds = [bm['%s_%d' % (name, i)] for i in range(10)]
        one = bm['%s_1' % name]             # a '1' is two vertical segments:
        lit = sum(1 for r in one if any(r))  # every row but the three strokes'
        if lit < h - 3 * t - 1:             # is ink, or the segments collapsed
            print("digit set %s: a '1' inks %d of %d rows - its vertical "
                  "segments have no length" % (name, lit, h))
            bad += 1
        if any((len(d), len(d[0])) != (h, w + sl) for d in ds):
            print("digit set %s is not %dx%d" % (name, w + sl, h))
            bad += 1
        if len(set(tuple(map(tuple, d)) for d in ds)) != 10:
            print("digit set %s has two digits alike" % name)
            bad += 1
        eight = bm['%s_8' % name]
        for i in range(10):
            d = bm['%s_%d' % (name, i)]
            if any(d[y][x] and not eight[y][x] for y in range(h) for x in range(w + sl)):
                print("%s: digit %d has ink 8 lacks" % (name, i))
                bad += 1
    for ch, rows in CAPS.items():
        if len(rows) != 7 or any(len(r) != 5 for r in rows):
            print("caps glyph %r is not 5x7" % ch)
            bad += 1
    for c, rows in LOGO_LETTERS.items():
        if len(rows) != 10 or len(set(len(r) for r in rows)) != 1:
            print("logo letter %r is not 10 even rows" % c)
            bad += 1
    for n in ICONS:
        if not any(any(r) for r in bm['h_' + n]):
            print("the CGA's %s icon is empty" % n)
            bad += 1
    if emit(bm) != emit(all_bitmaps()):
        print("two runs differ")
        bad += 1
    print("os88cdart: selfcheck %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0


def main():
    if "--selfcheck" in sys.argv[1:]:
        sys.exit(selfcheck())
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--png", help="a directory: one preview PNG a picture")
    a = ap.parse_args()
    bm = all_bitmaps()
    text = emit(bm)
    with open(a.out, "w") as f:
        f.write(text)
    if a.png:
        os.makedirs(a.png, exist_ok=True)
        for n, b in bm.items():
            write_png(os.path.join(a.png, n + ".png"), b)
    size = sum(4 + len(pack(b)[3]) for b in bm.values())
    print("os88cdart: %s - %d pictures, %d bytes" % (a.out, len(bm), size))


if __name__ == "__main__":
    main()
