#!/usr/bin/env python3
"""os88wrart: WaveRack's pictures, fonts, meter table and layouts (SPEC.md 123.11).

    python3 tools/os88wrart.py --out build/wrart.inc [--png DIR]
    python3 tools/os88wrart.py --selfcheck

Every picture WaveRack draws that is not a filled rectangle or a glyph of the
system font is made HERE, from definitions in this file - no image is read and
no service is asked, so the same run writes the same bytes - and emitted as an
asm include of 1bpp bitmaps the package puts down with OSAPI_GFX_BLIT1 under a
pen. CDRack's generator (tools/os88cdart.py, SPEC.md 119.6.3) is the shape;
the design is the owner's three mockups in docs/waverack/design/.

  LCD DIGITS   seven segments, slanted, one set per layout's timer, each made
               NATIVELY at its size: `df` FULL's (mitre, 32 rows), `dm` MID's
               (block, 24 rows - a mitred 24 collapsed its verticals) and `dc` COMPACT's on the CGA (block, 16 rows
               of 2.4:1 pixels). Each set has '0'-'9', ' ', '-', the colon and
               the decimal point.
  SMALL CAPS   a 5x7 face for the panel lettering: LEVEL, INPUT GAIN
               (DIGITAL), MIN, MAX, PEAK, the meter's scale, VOLUME, the keys'
               captions and DIGITAL SOUND RECORDER.
  THE LOGO     "WaveRack" in heavy letters, pixel art at 2x.
  PICTURES     the transport and file keys (record, stop, pause, play, open,
               save, minus, plus), the speakers, the check box and the screw,
               each at 13 rows and at the CGA's 7.
  THE METER    WRART_DB: 129 bytes, a peak 0..128 -> the lit cells 0..24 of
               a -30..0 dBFS bar at 1.25 dB a cell, worked out with log10 HERE
               so the target never takes a logarithm (SPEC.md 123.8).
  THE LAYOUTS  FULL, MID and COMPACT as DATA (tools/os88wrlay.py): every key,
               slider, drop-down, field and panel, emitted as layout records
               and static-part op lists, and measured by --selfcheck - every
               rect inside its content box, no two controls overlapping, every
               caption fitting its control, COMPACT inside a 640 x 200 screen.

The bitmap record (wrart_* labels): dw width; db height; db stride; then
height rows of stride bytes, bit 7 leftmost, 1 = ink.
"""
import argparse
import math
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88wrlay as LAY  # noqa: E402

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
    """`keep` rows standing for the source's - the CGA's half height. A row
    keeps any ink of the rows it stands for (OR), so a 1-pixel stroke never
    vanishes (CDRack's rule)."""
    h = len(b)
    out = []
    for i in range(keep):
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


def disc(b, cx, cy, r):
    for y in range(len(b)):
        for x in range(len(b[0])):
            if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r * r:
                b[y][x] = 1


def paste(dst, src, x0, y0):
    for y, row in enumerate(src):
        for x, v in enumerate(row):
            if v and 0 <= y0 + y < len(dst) and 0 <= x0 + x < len(dst[0]):
                dst[y0 + y][x0 + x] = 1


# =============================================================================
# THE LCD DIGITS - seven segments, slanted (os88cdart.py's two styles)
# =============================================================================
SEGS = {
    '0': 'abcdef', '1': 'bc', '2': 'abged', '3': 'abgcd', '4': 'fgbc',
    '5': 'afgcd', '6': 'afgedc', '7': 'abc', '8': 'abcdefg',
    '9': 'abcdfg', ' ': '', '-': 'g',
}
DIGIT_ORDER = '0123456789 -'


def seg_digit(ch, w, h, t, slant, style):
    W = w + slant
    b = blank(W, h)
    mid = (h - t) / 2.0

    def sx(x, y):                       # the slant: higher rows lean right
        return x + slant * (1 - y / float(h))

    if style == 'mitre':
        g = 1.0

        def hseg(y):
            pts = [(g + t / 2, y + t / 2), (g + t, y), (w - g - t, y),
                   (w - g - t / 2, y + t / 2), (w - g - t, y + t), (g + t, y + t)]
            return [(sx(px, py), py) for px, py in pts]

        def vseg(x, y0, y1):
            pts = [(x + t / 2, y0 + g + t / 2), (x + t, y0 + g + t),
                   (x + t, y1 - g - t), (x + t / 2, y1 - g - t / 2),
                   (x, y1 - g - t), (x, y0 + g + t)]
            return [(sx(px, py), py) for px, py in pts]
        c = mid + t / 2
    else:                               # 'block': the cheap calculator's LCD
        g, e = 0.6, t / 2.0

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
    for s in SEGS[ch]:
        fill_poly(b, shapes[s])
    return b


def colon(h, t, slant):
    w = t + slant + 1
    b = blank(w, h)
    for yc in (h * 0.30, h * 0.70):
        y0 = int(round(yc - t / 2.0))
        x0 = int(round(slant * (1 - yc / h)))
        rect(b, x0, y0, t, t)
    return b


def point(h, t):
    """The decimal point: a t x t block on the baseline."""
    b = blank(t + 1, h)
    rect(b, 0, h - t, t, t)
    return b


# name: (w, h, stroke, slant, style, gap) - LAY reads the sizes back
DIGIT_SETS = {
    'df': (18, 32, 4, 4, 'mitre', 3),   # FULL: a VGA's 480 lines
    'dm': (14, 24, 3, 3, 'block', 3),   # MID: EGA's 350, Hercules' 348
    'dc': (14, 16, 3, 3, 'block', 3),   # COMPACT: the CGA's 2.4:1 rows
}


def digit_metrics(name):
    """(digit cell width, height, colon width, point width, gap) - what the
    package's arithmetic and the layout check both need."""
    w, h, t, sl, st, gap = DIGIT_SETS[name]
    return (w + sl, h, len(colon(h, max(2, t - 1), sl)[0]),
            len(point(h, max(2, t - 1))[0]), gap)


def time_width(name, text='00:00.00'):
    dw, h, cw, pw, gap = digit_metrics(name)
    x = 0
    for ch in text:
        x += cw if ch == ':' else pw if ch == '.' else dw
        x += gap
    return x - gap


# =============================================================================
# THE SMALL CAPS - 5 x 7 (os88cdart.py's face, with the punctuation WaveRack
# needs: the parentheses of INPUT GAIN (DIGITAL), the comma of 11,025, the
# plus of a gain)
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
    ',': ['.....', '.....', '.....', '.....', '.##..', '..#..', '.#...'],
    ':': ['.....', '.##..', '.##..', '.....', '.##..', '.##..', '.....'],
    '/': ['....#', '....#', '...#.', '..#..', '.#...', '#....', '#....'],
    '-': ['.....', '.....', '.....', '#####', '.....', '.....', '.....'],
    '+': ['.....', '..#..', '..#..', '#####', '..#..', '..#..', '.....'],
    '(': ['...#.', '..#..', '.#...', '.#...', '.#...', '..#..', '...#.'],
    ')': ['.#...', '..#..', '...#.', '...#.', '...#.', '..#..', '.#...'],
    ' ': ['.....'] * 7,
}
CAPS_ORDER = ' ()+,-./0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZ'
CAPS_FIRST, CAPS_LAST = 32, 90


def caps_width(text, gap=0):
    """The width of a caps string as wd_capsw measures it: 6 + gap a letter,
    less the last letter's advance."""
    n = len(text)
    return 0 if n == 0 else n * (6 + gap) - 1 - gap


def caps_text(text, gap=0):
    cols = []
    for ch in text.upper():
        g = CAPS.get(ch, CAPS[' '])
        for x in range(5):
            cols.append([1 if g[y][x] == '#' else 0 for y in range(7)])
        for _ in range(1 + gap):
            cols.append([0] * 7)
    w = max(1, len(cols) - 1 - gap)
    return [[cols[x][y] for x in range(w)] for y in range(7)]


# =============================================================================
# THE LOGO - "WaveRack", heavy, pixel art at 2x (10 rows; the capitals full
# height, the lower case on a 7-row x-height)
# =============================================================================
LOGO_LETTERS = {
    'W': ["###.......###",
          "###.......###",
          "###.......###",
          "###...#...###",
          "###..###..###",
          "###.#####.###",
          "#############",
          "#####.#.#####",
          "####.....####",
          "###.......###"],
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
    'v': [".........",
          ".........",
          ".........",
          "###...###",
          "###...###",
          "###...###",
          ".###.###.",
          ".#######.",
          "..#####..",
          "...###..."],
    'e': ["........",
          "........",
          "........",
          ".######.",
          "###..###",
          "########",
          "###.....",
          "###.....",
          "########",
          ".######."],
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
LOGO_WORD = 'WaveRack'


def logo():
    h = 10
    w = sum(len(LOGO_LETTERS[c][0]) for c in LOGO_WORD) + 2 * (len(LOGO_WORD) - 1)
    b = blank(w, h)
    x = 0
    for c in LOGO_WORD:
        paste(b, from_art(LOGO_LETTERS[c]), x, 0)
        x += len(LOGO_LETTERS[c][0]) + 2
    return scale(b, 2, 2)


# =============================================================================
# PICTURES - 13 rows (the CGA's: the same sampled to 7)
# =============================================================================
def pic(draw, w, h=13):
    b = blank(w, h)
    draw(b)
    return b


def p_folder(b):
    rect(b, 0, 2, 6, 2)                 # the tab
    rect(b, 0, 3, 16, 1)
    rect(b, 0, 3, 1, 10)                # the body, outlined
    rect(b, 15, 3, 1, 10)
    rect(b, 0, 12, 16, 1)
    rect(b, 1, 5, 14, 1)                # the flap's edge


def p_floppy(b):
    rect(b, 0, 0, 13, 1)                # the case
    rect(b, 0, 0, 1, 13)
    rect(b, 12, 1, 1, 12)
    rect(b, 0, 12, 13, 1)
    rect(b, 3, 0, 7, 4)                 # the shutter, with its window
    rect(b, 7, 1, 2, 2, 0)
    rect(b, 2, 7, 9, 6)                 # the label
    rect(b, 3, 8, 7, 4, 0)


def p_speaker(b):                       # the VOLUME mark: a cone and waves
    rect(b, 0, 4, 3, 5)
    fill_poly(b, [(3, 4), (8, 0), (8, 13), (3, 9)])
    for x, y0, y1 in ((10, 4, 9), (12, 2, 11)):
        rect(b, x, y0, 1, y1 - y0)


PICTURES = {                            # name: (draw, width)
    'rec':    (lambda b: disc(b, 6.5, 6.5, 5.6), 13),
    'stop':   (lambda b: rect(b, 1, 1, 11, 11), 13),
    'pause':  (lambda b: (rect(b, 2, 1, 3, 11), rect(b, 8, 1, 3, 11)), 13),
    'play':   (lambda b: fill_poly(b, [(2, 0.5), (12, 6.5), (2, 12.5)]), 13),
    'open':   (p_folder, 16),
    'save':   (p_floppy, 13),
    'minus':  (lambda b: rect(b, 1, 5, 11, 3), 13),
    'plus':   (lambda b: (rect(b, 1, 5, 11, 3), rect(b, 5, 1, 3, 11)), 13),
    'speaker': (p_speaker, 13),
}

SPK_LO = from_art([                     # the slider's quiet end
    "...#",
    "..##",
    "####",
    "####",
    "####",
    "####",
    "####",
    "..##",
    "...#",
])

SPK_HI = from_art([                     # ...and its loud one
    "...#........",
    "..##...#....",
    "####....#.#.",
    "####..#.#..#",
    "####...#.#.#",
    "####...#.#.#",
    "####..#.#..#",
    "..##....#.#.",
    "...#...#....",
])

SCREW = from_art([
    "...#####...",
    ".##.....##.",
    ".#.......#.",
    "#.......#.#",
    "#......#..#",
    "#.....#...#",
    "#....#....#",
    "#...#.....#",
    ".##.......#",
    ".#.......#.",
    "...#####...",
])

CHECK = from_art([
    "###########",
    "#.........#",
    "#........##",
    "#.......###",
    "#......##.#",
    "##....##..#",
    "###..##...#",
    "#.####....#",
    "#..##.....#",
    "#.........#",
    "###########",
])

UNCHECK = from_art([
    "###########",
    "#.........#",
    "#.........#",
    "#.........#",
    "#.........#",
    "#.........#",
    "#.........#",
    "#.........#",
    "#.........#",
    "#.........#",
    "###########",
])

# the package's 16x16 icon: a rack panel with a waveform on its LCD
ICON = [
    "################",
    "#..............#",
    "#.############.#",
    "#.#..........#.#",
    "#.#....#.....#.#",
    "#.#...##..#..#.#",
    "#.#.#.###.##.#.#",
    "#.###########..#",
    "#.#.#.###.##.#.#",
    "#.#...##..#..#.#",
    "#.#....#.....#.#",
    "#.############.#",
    "#..............#",
    "#.##.##.##..#..#",
    "#..............#",
    "################",
]


def all_bitmaps():
    """name -> bitmap, in the order they are emitted."""
    bm = {}
    for name, (w, h, t, sl, st, gap) in DIGIT_SETS.items():
        for i, ch in enumerate(DIGIT_ORDER):
            bm['%s_%d' % (name, i)] = seg_digit(ch, w, h, t, sl, st)
        bm['%s_col' % name] = colon(h, max(2, t - 1), sl)
        bm['%s_dot' % name] = point(h, max(2, t - 1))
    bm['logo'] = logo()
    for n, (f, w) in PICTURES.items():
        b = pic(f, w)
        bm['i_' + n] = b
        bm['h_' + n] = rows_every(b, 7)          # the CGA's
    bm['spklo'] = SPK_LO
    bm['spkhi'] = SPK_HI
    bm['screw'] = SCREW
    bm['check'] = CHECK
    bm['uncheck'] = UNCHECK
    bm['h_check'] = rows_every(CHECK, 7)
    bm['h_uncheck'] = rows_every(UNCHECK, 7)
    return bm


def db_table():
    """WRART_DB[p], p = a peak 0..128 (|s - 80h| of 8-bit, |s| >> 8 of
    16-bit): how many of the meter's 24 cells are lit. Cell i covers
    [-30 + 1.25 i, -30 + 1.25 (i + 1)) dBFS, so n cells are lit when the
    level reaches the top of cell n-1's floor: n = ceil((dB + 30) / 1.25),
    clamped to 0..24 (SPEC.md 123.8)."""
    out = []
    for p in range(129):
        if p == 0:
            out.append(0)
            continue
        db = 20.0 * math.log10(p / 128.0)
        n = int(math.ceil((db + 30.0) / 1.25 - 1e-9))
        out.append(max(0, min(24, n)))
    return out


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


def icon_words():
    data = []
    for r in ICON:
        v = 0
        for k, c in enumerate(r):
            if c == '#':
                v |= 0x8000 >> k
        data.append(v)
    return data


def emit(bm):
    lines = ["; GENERATED by tools/os88wrart.py - do not edit (SPEC.md 123.11)",
             "; the picture record: dw width; db height; db stride; rows x stride",
             ""]
    for name, b in bm.items():
        w, h, stride, data = pack(b)
        lines.append("wrart_%s:" % name)
        lines.append("    dw %d" % w)
        lines.append("    db %d, %d" % (h, stride))
        for i in range(0, len(data), 16):
            lines.append("    db " + ", ".join("0x%02X" % v for v in data[i:i + 16]))
    for name in DIGIT_SETS:
        lines.append("wrart_%s_tab:" % name)
        lines.append("    dw " + ", ".join("wrart_%s_%d" % (name, i)
                                         for i in range(len(DIGIT_ORDER))))
    for name in DIGIT_SETS:
        dw, h, cw, pw, gap = digit_metrics(name)
        N = name.upper()
        lines.append("WRART_%s_W equ %d" % (N, dw))
        lines.append("WRART_%s_H equ %d" % (N, h))
        lines.append("WRART_%s_CW equ %d" % (N, cw))
        lines.append("WRART_%s_PW equ %d" % (N, pw))
        lines.append("WRART_%s_GAP equ %d" % (N, gap))
        lines.append("WRART_%s_TIMEW equ %d" % (N, time_width(name)))
    lines.append("WRART_CAPS_FIRST equ %d" % CAPS_FIRST)
    lines.append("WRART_CAPS_LAST equ %d" % CAPS_LAST)
    lines.append("wrart_caps_ix:")
    ix = [CAPS_ORDER.index(chr(c)) + 1 if chr(c) in CAPS else 0
          for c in range(CAPS_FIRST, CAPS_LAST + 1)]
    lines.append("    db " + ", ".join(str(v) for v in ix))
    lines.append("wrart_caps:")
    for ch in CAPS_ORDER:
        rows = []
        for r in CAPS[ch]:
            v = 0
            for k, c in enumerate(r):
                if c == '#':
                    v |= 0x80 >> k
            rows.append(v)
        lines.append("    db " + ", ".join("0x%02X" % v for v in rows) + "  ; %r" % ch)
    lines.append("wrart_db:                           ; peak 0..128 -> lit cells")
    t = db_table()
    for i in range(0, len(t), 16):
        lines.append("    db " + ", ".join(str(v) for v in t[i:i + 16]))
    lines.append("%macro WRART_ICON16 0")
    lines.append("    dw " + ", ".join("0xFFFF" for _ in range(8)))
    lines.append("    dw " + ", ".join("0xFFFF" for _ in range(8)))
    words = icon_words()
    lines.append("    dw " + ", ".join("0x%04X" % v for v in words[:8]))
    lines.append("    dw " + ", ".join("0x%04X" % v for v in words[8:]))
    lines.append("%endmacro")
    lines.append("")
    lines.extend(LAY.emit(bm, sys.modules[__name__]))
    # two halves under one name: the constants and the icon macro, which the
    # package needs before its first instruction, and the data, which it
    # includes where its data goes (%define WRART_DATA first)
    defs, data, inmac = [], [], False
    for ln in lines[3:]:
        if ln.startswith("%macro"):
            inmac = True
        if inmac or " equ " in ln.split(";")[0]:
            defs.append(ln)
        else:
            data.append(ln)
        if ln.startswith("%endmacro"):
            inmac = False
    out = lines[:3] + ["%ifndef WRART_DATA"] + defs + ["%else"] + data + ["%endif"]
    return "\n".join(out) + "\n"


def write_png(path, rows, w, h, palette):
    """A palette PNG of `rows` (lists of palette indices) - no PIL."""
    raw = bytearray()
    for row in rows:
        raw.append(0)
        raw.extend(row)

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
    png += chunk(b"PLTE", bytes(c for rgb in palette for c in rgb))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def write_bitmap_png(path, b, sc=4):
    rows = []
    for row in b:
        line = bytearray()
        for px in row:
            line += bytes([0 if px else 1]) * sc
        for _ in range(sc):
            rows.append(line)
    write_png(path, rows, len(b[0]) * sc, len(b) * sc, [(0, 0, 0), (255, 255, 255)])


def selfcheck():
    bad = 0
    bm = all_bitmaps()
    for name, (w, h, t, sl, st, gap) in DIGIT_SETS.items():
        ds = [bm['%s_%d' % (name, i)] for i in range(10)]
        one = bm['%s_1' % name]
        lit = sum(1 for r in one if any(r))
        if lit < h - 3 * t - 1:
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
        if h < 20 and st != 'block':
            print("digit set %s is %d rows and mitred - below ~20 rows use "
                  "block segments (retro-face LESSONS)" % (name, h))
            bad += 1
    for ch, rows in CAPS.items():
        if len(rows) != 7 or any(len(r) != 5 for r in rows):
            print("caps glyph %r is not 5x7" % ch)
            bad += 1
    if sorted(CAPS_ORDER) != sorted(CAPS):
        print("CAPS_ORDER and CAPS disagree")
        bad += 1
    for c, rows in LOGO_LETTERS.items():
        if len(rows) != 10 or len(set(len(r) for r in rows)) != 1:
            print("logo letter %r is not 10 even rows" % c)
            bad += 1
    for n in PICTURES:
        if len(bm['i_' + n]) != 13 or len(bm['h_' + n]) != 7:
            print("picture %s is not 13 rows with a 7-row CGA form" % n)
            bad += 1
        if not any(any(r) for r in bm['h_' + n]):
            print("the CGA's %s picture is empty" % n)
            bad += 1
    if len(ICON) != 16 or any(len(r) != 16 for r in ICON):
        print("the icon is not 16x16")
        bad += 1
    t = db_table()
    if len(t) != 129 or t[0] != 0 or t[128] != 24:
        print("the dB table's ends are wrong")
        bad += 1
    if any(t[i] > t[i + 1] for i in range(128)):
        print("the dB table is not monotonic")
        bad += 1
    for p in range(1, 129):                  # every entry against 20 log10
        db = 20 * math.log10(p / 128.0)
        n = t[p]
        lo = -30 + 1.25 * (n - 1) if n else -999
        hi = -30 + 1.25 * n
        if not (n == 0 and db <= -30 or lo < db + 1e-9 and db <= hi + 1e-9):
            print("dB table: peak %d (%.2f dB) lights %d cells" % (p, db, n))
            bad += 1
            break
    bad += LAY.selfcheck(bm, sys.modules[__name__])
    if emit(bm) != emit(all_bitmaps()):
        print("two runs differ")
        bad += 1
    print("os88wrart: selfcheck %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0


def main():
    if "--selfcheck" in sys.argv[1:]:
        sys.exit(selfcheck())
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--png", help="a directory: a preview PNG per picture and layout")
    a = ap.parse_args()
    bm = all_bitmaps()
    text = emit(bm)
    with open(a.out, "w") as f:
        f.write(text)
    if a.png:
        os.makedirs(a.png, exist_ok=True)
        for n, b in bm.items():
            write_bitmap_png(os.path.join(a.png, n + ".png"), b)
        LAY.previews(a.png, bm, sys.modules[__name__])
    size = sum(4 + len(pack(b)[3]) for b in bm.values())
    print("os88wrart: %s - %d pictures, %d bytes of pictures" % (a.out, len(bm), size))


if __name__ == "__main__":
    main()
