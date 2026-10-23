#!/usr/bin/env python3
"""os88x3art: XTMP3's pictures and fonts, made on the host (SPEC.md 121.8).

    python3 tools/os88x3art.py --out build/x3art.inc [--png DIR]
    python3 tools/os88x3art.py --selfcheck

Every picture the MP3 player draws that is not a filled rectangle or a glyph
of the system font comes from HERE, and the same run writes the same bytes.
Two kinds of source, both committed:

  CODE          the fonts, the seven-segment digits, the transport icons,
                the screws, the grilles and every line table - definitions in
                this file, the way tools/os88cdart.py makes CDRack's.
  MASTERS       apps/xtmp3/art/{knob,vu,logo}.png, pixel art the codex image
                generator drew once (apps/xtmp3/art/PROMPT.md has the prompts).
                They are DERIVED here deterministically: each master is pixel
                art at a whole-number block size, so the logical grid is read
                back from the block centres, mapped to the sixteen colours,
                cropped, resampled for the shorter rows of EGA and Hercules,
                and hand-finished with pixel patches written down below as
                data. The PNG reader is this file's own (zlib and the five
                row filters), so `make` needs python3 and nothing else.

THE PICTURES ARE 1BPP and the package composes them (apps/xtmp3/x3draw.inc):
a band of ink bits under a two-colour pen (OSAPI_GFX_BLIT1), or, for the two
colour pictures, a 4bpp canvas built from PLANES at the moment it is drawn
(OSAPI_GFX_BLIT4):

  THE KNOB      two planes, p0 and p1, whose pair is an index into black,
                dark grey, light grey and white - the four greys the master
                was drawn in - plus an explicit 1bpp picture for the black and
                white face, shaded by an ordered dither. Its indicator is NOT
                in the picture: it is a line the package draws at the
                volume's angle, from a table here.
  THE VU DIAL   a BLACK mask (frame, arc, ticks, pivot) and a RED one (the
                overload zone); the cream face is the yellow-and-white
                checker the master was drawn in, made by the package. The
                black and white face is a third, explicit picture: the black
                mask and the red zone HATCHED. The needle is a line too.

The bitmap record (x3art_* labels): dw width; db height; db stride; then
height rows of stride bytes, bit 7 leftmost, 1 = ink.
"""
import argparse
import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ARTDIR = os.path.join(ROOT, "apps", "xtmp3", "art")

# the sixteen colours, by index (EGA/VGA's default palette)
PAL = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
       (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
       (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
       (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255)]
BLACK, RED, LGRAY, DGRAY, YELLOW, WHITE = 0, 4, 7, 8, 14, 15

# =============================================================================
# a minimal PNG reader: 8-bit palette, 1/8-bit grey, 8-bit RGB(A)
# =============================================================================


def read_png(path):
    """-> (w, h, rows of (r, g, b))."""
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("%s: not a PNG" % path)
    i, idat, plte = 8, b"", None
    w = h = depth = ctype = None
    while i < len(d):
        n, = struct.unpack(">I", d[i:i + 4])
        t, body = d[i + 4:i + 8], d[i + 8:i + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype, _, _, ilace = struct.unpack(">IIBBBBB", body)
            if ilace:
                raise SystemExit("%s: interlaced PNG" % path)
        elif t == b"PLTE":
            plte = [tuple(body[k:k + 3]) for k in range(0, len(body), 3)]
        elif t == b"IDAT":
            idat += body
        i += 12 + n
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, chans * depth // 8)
    stride = (w * chans * depth + 7) // 8
    raw = zlib.decompress(idat)
    rows, prev, p = [], bytearray(stride), 0
    for _ in range(h):
        f, line = raw[p], bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        prev = line
        px = []
        for x in range(w):
            if depth == 8:
                v = line[x * chans:(x + 1) * chans]
            else:                               # sub-byte: palette or grey
                bit = x * depth
                v = [(line[bit // 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1)]
            if ctype == 3:
                px.append(plte[v[0]])
            elif ctype in (0, 4):
                g = v[0] * 255 // ((1 << depth) - 1)
                px.append((g, g, g))
            else:
                px.append(tuple(v[:3]))
        rows.append(px)
    return w, h, rows


def nearest(rgb):
    return min(range(16), key=lambda k: sum((a - b) ** 2 for a, b in zip(PAL[k], rgb)))


def master(name, block):
    """A master's LOGICAL grid: the centre of every block, as colour indices.
    The masters are drawn on a whole-number block, and a block that is not one
    colour is a master that was resized - refused, not guessed at."""
    w, h, rows = read_png(os.path.join(ARTDIR, name + ".png"))
    if w % block or h % block:
        raise SystemExit("%s.png is %dx%d, not a whole number of %d-px blocks"
                         % (name, w, h, block))
    grid = []
    for by in range(h // block):
        row = []
        for bx in range(w // block):
            c = rows[by * block + block // 2][bx * block + block // 2]
            for yy in range(by * block, by * block + block):
                for xx in range(bx * block, bx * block + block):
                    if rows[yy][xx] != c:
                        raise SystemExit("%s.png: block (%d,%d) is not one colour"
                                         % (name, bx, by))
            row.append(nearest(c))
        grid.append(row)
    return grid


# =============================================================================
# a bitmap is a list of rows, each a list of 0/1; a pixmap the same of 0..15
# =============================================================================


def blank(w, h, v=0):
    return [[v] * w for _ in range(h)]


def from_art(lines):
    w = max(len(l) for l in lines)
    return [[1 if c == '#' else 0 for c in l.ljust(w)] for l in lines]


def crop(b, x, y, w, h):
    return [row[x:x + w] for row in b[y:y + h]]


def rows_or(b, keep):
    """`keep` rows of b's, each the OR of the rows it stands for, so a
    1-pixel stroke never vanishes (a mask's shorter form)."""
    h = len(b)
    out = []
    for i in range(keep):
        lo = int(i * h / keep)
        hi = max(lo + 1, int((i + 1) * h / keep))
        out.append([max(b[r][x] for r in range(lo, hi)) for x in range(len(b[0]))])
    return out


def rows_near(b, keep):
    """`keep` rows of b's, the nearest one each (a pixmap's shorter form)."""
    h = len(b)
    return [list(b[min(h - 1, int((i + 0.5) * h / keep))]) for i in range(keep)]


def fill_poly(b, pts):
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


def paste(dst, src, x0, y0):
    for y, row in enumerate(src):
        for x, v in enumerate(row):
            if v and 0 <= y0 + y < len(dst) and 0 <= x0 + x < len(dst[0]):
                dst[y0 + y][x0 + x] = 1


def line_px(x1, y1, x2, y2):
    """Bresenham, both ends included - the same walk x3draw.inc's x3d_line
    makes, so a table here and a line there agree to the pixel."""
    pts = []
    dx, dy = abs(x2 - x1), -abs(y2 - y1)
    sx = 1 if x1 < x2 else -1
    sy = 1 if y1 < y2 else -1
    err = dx + dy
    while True:
        pts.append((x1, y1))
        if x1 == x2 and y1 == y2:
            return pts
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x1 += sx
        if e2 <= dx:
            err += dx
            y1 += sy


# =============================================================================
# THE SEVEN-SEGMENT DIGITS (tools/os88cdart.py's two styles, upright here)
# =============================================================================
SEGS = {'0': 'abcdef', '1': 'bc', '2': 'abged', '3': 'abgcd', '4': 'fgbc',
        '5': 'afgcd', '6': 'afgedc', '7': 'abc', '8': 'abcdefg',
        '9': 'abcdfg', ' ': '', '-': 'g'}
DIGIT_ORDER = '0123456789 -'


def seg_mitre(ch, w, h, t):
    b = blank(w, h)
    mid = (h - t) / 2.0
    g = 1.0

    def hseg(y):
        return [(g + t / 2, y + t / 2), (g + t, y), (w - g - t, y),
                (w - g - t / 2, y + t / 2), (w - g - t, y + t), (g + t, y + t)]

    def vseg(x, y0, y1):
        return [(x + t / 2, y0 + g + t / 2), (x + t, y0 + g + t), (x + t, y1 - g - t),
                (x + t / 2, y1 - g - t / 2), (x, y1 - g - t), (x, y0 + g + t)]

    shapes = {'a': hseg(0), 'g': hseg(mid), 'd': hseg(h - t),
              'f': vseg(0, 0, mid + t / 2), 'b': vseg(w - t, 0, mid + t / 2),
              'e': vseg(0, mid + t / 2, h), 'c': vseg(w - t, mid + t / 2, h)}
    for s in SEGS[ch]:
        fill_poly(b, shapes[s])
    return b


def seg_trap(ch, w, h, t):
    """The mockup's LCD segment: each one a trapezoid whose 45-degree ends
    meet its neighbour's along the corner's diagonal, a pixel's gap between,
    and the middle bar pointed at both ends - long strokes that read as a
    lit glass segment rather than a pill."""
    b = blank(w, h)
    g = 0.9
    m = h / 2.0
    e = t / 2.0
    shapes = {
        'a': [(g, 0), (w - g, 0), (w - t - g, t), (t + g, t)],
        'd': [(t + g, h - t), (w - t - g, h - t), (w - g, h), (g, h)],
        'g': [(e + g, m), (t + g, m - e), (w - t - g, m - e), (w - e - g, m),
              (w - t - g, m + e), (t + g, m + e)],
        'f': [(0, g), (t, t + g), (t, m - e - g), (e, m - g), (0, m - g)],
        'b': [(w, g), (w, m - g), (w - e, m - g), (w - t, m - e - g), (w - t, t + g)],
        'e': [(0, m + g), (e, m + g), (t, m + e + g), (t, h - t - g), (0, h - g)],
        'c': [(w, m + g), (w, h - g), (w - t, h - t - g), (w - t, m + e + g),
              (w - e, m + g)],
    }
    for sg in SEGS[ch]:
        fill_poly(b, shapes[sg])
    return b


def seg_block(ch, w, h, t):
    b = blank(w, h)
    mid = (h - t) / 2.0
    g, e = 0.6, t / 2.0

    def hseg(y):
        x0, x1 = t + g, w - t - g
        return [(x0, y + e), (x0 + e, y), (x1 - e, y), (x1, y + e),
                (x1 - e, y + t), (x0 + e, y + t)]

    def vseg(x, y0, y1):
        y0, y1 = y0 + g, y1 - g
        return [(x + e, y0), (x + t, y0 + e), (x + t, y1 - e), (x + e, y1),
                (x, y1 - e), (x, y0 + e)]

    c = mid + e
    shapes = {'a': hseg(0), 'g': hseg(mid), 'd': hseg(h - t),
              'f': vseg(0, 0, c), 'b': vseg(w - t, 0, c),
              'e': vseg(0, c, h), 'c': vseg(w - t, c, h)}
    for sg in SEGS[ch]:
        fill_poly(b, shapes[sg])
    return b


def seg_thin(ch, w, h):
    """A ONE-pixel segment: the 'thin' total's look, whatever its height -
    straight strokes with a pixel's gap at every joint."""
    b = blank(w, h)
    m = (h - 1) // 2
    segs = {'a': [(x, 0) for x in range(1, w - 1)],
            'g': [(x, m) for x in range(1, w - 1)],
            'd': [(x, h - 1) for x in range(1, w - 1)],
            'f': [(0, y) for y in range(1, m)],
            'b': [(w - 1, y) for y in range(1, m)],
            'e': [(0, y) for y in range(m + 1, h - 1)],
            'c': [(w - 1, y) for y in range(m + 1, h - 1)]}
    for s in SEGS[ch]:
        for x, y in segs[s]:
            b[y][x] = 1
    return b


def colon(h, t, w):
    b = blank(w, h)
    for yc in (h * 0.30, h * 0.70):
        y0 = int(round(yc - t / 2.0))
        rect(b, (w - t) // 2, y0, t, t)
    return b


def slash(h, w):
    b = blank(w, h)
    for x, y in line_px(0, h - 1, w - 1, 0):
        b[y][x] = 1
    return b


def tilde(h, w):
    """'~' for an ESTIMATED total (SPEC.md 121.2), on the digits' middle."""
    b = blank(w, h)
    m = h // 2
    pts = [(0, m), (1, m - 1), (2, m - 1), (3, m), (4, m + 1), (5, m + 1), (6, m)]
    for x, y in pts:
        if x < w:
            b[y][x] = 1
    return b


# name: (w, h, stroke, style, gap) - the elapsed time's and the total's sets
DIGIT_SETS = {
    'tf': (22, 36, 5, 'trap', 8),      # FULL, and the doubled compact player
    'tm': (22, 27, 4, 'trap', 8),      # MID: EGA's 350 lines, Hercules' 348
    'tc': (20, 18, 3, 'trap', 6),       # COMPACT on the CGA (2.4:1 rows)
    'nf': (11, 18, 1, 'thin', 3),       # the thin total: FULL and doubled
    'nm': (11, 14, 1, 'thin', 3),       # ...MID
    'nc': (11, 9, 1, 'thin', 3),        # ...the CGA's
}


def digit(name, ch):
    w, h, t, style, _ = DIGIT_SETS[name]
    if style == 'mitre':
        return seg_mitre(ch, w, h, t)
    if style == 'trap':
        return seg_trap(ch, w, h, t)
    if style == 'block':
        return seg_block(ch, w, h, t)
    return seg_thin(ch, w, h)


# =============================================================================
# THE SMALL CAPS - 5 x 7, the panel's lettering; lower case only where the
# mockup prints it ('kHz')
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
    ',': ['.....', '.....', '.....', '.....', '.##..', '.##..', '.#...'],
    ':': ['.....', '.##..', '.##..', '.....', '.##..', '.##..', '.....'],
    '%': ['##..#', '##..#', '...#.', '..#..', '.#...', '#..##', '#..##'],
    '/': ['....#', '....#', '...#.', '..#..', '.#...', '#....', '#....'],
    '-': ['.....', '.....', '.....', '#####', '.....', '.....', '.....'],
    '+': ['.....', '..#..', '..#..', '#####', '..#..', '..#..', '.....'],
    '~': ['.....', '.....', '.#...', '#.#.#', '...#.', '.....', '.....'],
    "'": ['..#..', '..#..', '.#...', '.....', '.....', '.....', '.....'],
    '&': ['.##..', '#..#.', '#.#..', '.#...', '#.#.#', '#..#.', '.##.#'],
    '!': ['..#..', '..#..', '..#..', '..#..', '..#..', '.....', '..#..'],
    '?': ['.###.', '#...#', '....#', '...#.', '..#..', '.....', '..#..'],
    '(': ['...#.', '..#..', '.#...', '.#...', '.#...', '..#..', '...#.'],
    ')': ['.#...', '..#..', '...#.', '...#.', '...#.', '..#..', '.#...'],
    'k': ['#....', '#....', '#..#.', '#.#..', '##...', '#.#..', '#..#.'],
    'z': ['.....', '.....', '#####', '...#.', '..#..', '.#...', '#####'],
    ' ': ['.....'] * 7,
}
CAPS_FIRST, CAPS_LAST = 32, 122


def caps_order():
    return [c for c in map(chr, range(CAPS_FIRST, CAPS_LAST + 1)) if c in CAPS]


def caps_glyph(ch):
    return [[1 if c == '#' else 0 for c in r] for r in CAPS[ch]]


# --- the LCD's title face: the caps made BOLD and TALL ------------------------
# A 5x7 letter ORed one pixel right is a 6x7 letter with two-pixel stems; the
# rows that carry its horizontal strokes (0, 3 and 6 in nearly every letter)
# are doubled to make 10 rows, so the bars are two pixels as well - the
# chunky LCD lettering of the mockup's '01 NEON DRIVE'. MID's 8 rows double
# only the crossbar's.
LCD_TALL = {10: (0, 3, 6), 8: (3,)}


def lcd_glyph(ch, h):
    g = caps_glyph(ch)
    bold = [[(r[x] if x < 5 else 0) | (r[x - 1] if x >= 1 else 0) for x in range(6)]
            for r in g]
    out = []
    for i, r in enumerate(bold):
        out.append(r)
        if i in LCD_TALL[h]:
            out.append(list(r))
    return out


# --- the VU dial's printed scale: 3 x 5 ------------------------------------------
DIALDIG = {
    '0': ['###', '#.#', '#.#', '#.#', '###'],
    '1': ['.#.', '##.', '.#.', '.#.', '###'],
    '2': ['###', '..#', '###', '#..', '###'],
    '3': ['###', '..#', '.##', '..#', '###'],
    '5': ['###', '#..', '###', '..#', '###'],
    '-': ['...', '...', '###', '...', '...'],
    '+': ['...', '.#.', '###', '.#.', '...'],
    '%': ['#.#', '..#', '.#.', '#..', '#.#'],
}
DIALDIG_ORDER = '%+-01235'


# =============================================================================
# THE ICONS - 15 x 13; the CGA's are the same ORed down to 7 rows
# =============================================================================
def tri_right(b, x0, x1, ymid, half):
    fill_poly(b, [(x0, ymid - half), (x1, ymid), (x0, ymid + half)])


def tri_left(b, x0, x1, ymid, half):
    fill_poly(b, [(x1, ymid - half), (x0, ymid), (x1, ymid + half)])


ICONS = {
    'rew': lambda b: (tri_left(b, 0, 7.5, 6.5, 5.5), tri_left(b, 7, 14.5, 6.5, 5.5)),
    'play': lambda b: tri_right(b, 3, 13, 6.5, 6),
    'pause': lambda b: (rect(b, 3, 1, 3, 11), rect(b, 9, 1, 3, 11)),
    'stop': lambda b: rect(b, 2, 1, 11, 11),
    'ff': lambda b: (tri_right(b, 0, 7.5, 6.5, 5.5), tri_right(b, 7, 14.5, 6.5, 5.5)),
    'eject': lambda b: (fill_poly(b, [(1.5, 8), (7.5, 1.5), (13.5, 8)]), rect(b, 2, 10, 11, 2)),
}


def icon(draw, w=15, h=13):
    b = blank(w, h)
    draw(b)
    return b


SCREW = from_art([                      # a cross-head screw, 9 x 9
    "..#####..",
    ".#.....#.",
    "#...#...#",
    "#...#...#",
    "#.#####.#",
    "#...#...#",
    "#...#...#",
    ".#.....#.",
    "..#####..",
])
SCREW_MID = from_art([                  # ...and on the shorter rows, 9 x 7
    "..#####..",
    ".#..#..#.",
    "#...#...#",
    "#.#####.#",
    "#...#...#",
    ".#..#..#.",
    "..#####..",
])


def grille(w, h, pitch, on=2):
    """The speaker grilles beside the knob: `on` dark rows every `pitch`."""
    return [[1 if y % pitch < on else 0] * w for y in range(h)]


# =============================================================================
# THE MASTERS
# =============================================================================

def logo_pictures():
    """'XTMP3' out of logo.png: the ink's bounding box, 92 x 16, and MID's
    16 rows ORed to 12."""
    g = master("logo", 8)
    ink = [[1 if c == BLACK else 0 for c in row] for row in g]
    ys = [y for y, r in enumerate(ink) if any(r)]
    xs = [x for r in ink for x, v in enumerate(r) if v]
    b = crop(ink, min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1)
    return b, rows_or(b, 12)


# THE KNOB. The master is 64 x 64; its black outline is a 52-pixel circle with
# its centre at (31.5, 31.5), the drop shadow lower right. The picture is the
# 56 x 56 square from (4, 4), so the centre is at (27.5, 27.5).
KNOB_X0, KNOB_SZ = 4, 56
KNOB_C = 31.5 - KNOB_X0
KNOB_R = 26.0
# THE HAND FINISH: the master's indicator (a white groove, outlined, at 12
# o'clock - rows 19..32, columns 30..33) is taken out, since the package
# draws it at the volume's angle. Its pixels are refilled from the same rows
# four columns away on its own side - an even offset, so the dither's phase
# is kept - and the patch is written down here rather than drawn by hand.
KNOB_GROOVE = (30, 19, 33, 32)          # x1, y1, x2, y2, inclusive, master grid
KNOB_FIX = [(32, 31, LGRAY)]            # ...and a black speck the refill carried


def knob_master():
    g = master("knob", 8)
    x1, y1, x2, y2 = KNOB_GROOVE
    out = [list(r) for r in g]
    for y in range(y1, y2 + 1):
        for x in range(x1, x2 + 1):
            src = x - 4 if x < (x1 + x2 + 1) / 2 else x + 4
            out[y][x] = g[y][src]
    for x, y, c in KNOB_FIX:
        out[y][x] = c
    return out


def knob_inside(x, y):
    """Is the master-grid pixel (x, y) inside the knob's outline?"""
    return (x + 0.5 - 32) ** 2 + (y + 0.5 - 32) ** 2 <= 27.5 ** 2


# 4 x 4 Bayer, for the black and white knob's greys
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
LUM = {BLACK: 0, DGRAY: 85, LGRAY: 170, WHITE: 255}


def knob_bwlevel(g):
    """The black and white knob's darkness, sixteenths, per master pixel: the
    mono mockup's face - a white knob, a solid rim, the shading only as a
    light dither growing to the lower right. Inside the rim the master's
    four greys are AVERAGED over 3 x 3 before the dither, because its own
    dither is a texture of black specks that a second dither turns into a
    comb; the rim and the indicator stay solid."""
    out = blank(64, 64)
    for y in range(64):
        for x in range(64):
            c = g[y][x]
            r = math.hypot(x + 0.5 - 32, y + 0.5 - 32)
            if not knob_inside(x, y):
                out[y][x] = 8 if c in (DGRAY, BLACK) and r < 30 else 0
            elif c == BLACK and r > 24.0:
                out[y][x] = 16                  # the rim
            else:
                n = [LUM[g[yy][xx]] for yy in range(y - 1, y + 2)
                     for xx in range(x - 1, x + 2) if knob_inside(xx, yy)
                     and math.hypot(xx + 0.5 - 32, yy + 0.5 - 32) <= 24.0]
                lum = sum(n) / float(len(n)) if n else 255
                f = max(0.0, min(1.0, (215.0 - lum) / 215.0))
                out[y][x] = int(round(f * 13))
    return out


def knob_pictures(rows):
    """-> (p0, p1, bw) at `rows` rows: two planes of the four-grey index
    (0 black, 1 dark grey, 2 light grey, 3 white) and the dithered 1bpp face."""
    g = knob_master()
    sq = crop(g, KNOB_X0, KNOB_X0, KNOB_SZ, KNOB_SZ)
    lv = crop(knob_bwlevel(g), KNOB_X0, KNOB_X0, KNOB_SZ, KNOB_SZ)
    idx = {BLACK: 0, DGRAY: 1, LGRAY: 2, WHITE: 3}
    if rows != KNOB_SZ:
        sq = rows_near(sq, rows)
        lv = rows_near(lv, rows)
    p0 = [[idx[c] & 1 for c in r] for r in sq]
    p1 = [[idx[c] >> 1 for c in r] for r in sq]
    bw = [[1 if lv[y][x] > BAYER[y & 3][x & 3] else 0 for x in range(KNOB_SZ)]
          for y in range(rows)]
    return p0, p1, bw


# THE VU DIAL. The master is 84 x 56: a black frame, the cream checker, the
# arc and its ticks, the red zone, the pivot. Its pivot is (41.5, 55).
DIAL_W, DIAL_H = 84, 56
DIAL_PX, DIAL_PY = 41.5, 55.0


def dial_pictures(rows):
    """-> (black mask, red mask, black-and-white face) at `rows` rows. The
    black and white face's red zone is a diagonal hatch, the mono mockup's."""
    g = master("vu", 6)
    k = [[1 if c == BLACK else 0 for c in r] for r in g]
    r = [[1 if c == RED else 0 for c in row] for row in g]
    if rows != DIAL_H:
        k, r = rows_or(k, rows), rows_or(r, rows)
    bw = [[k[y][x] | (r[y][x] & (1 if (x + y) % 3 == 0 else 0))
           for x in range(DIAL_W)] for y in range(rows)]
    return k, r, bw


# --- the lines the package draws: the needle, the knob's indicator, its ring ----
NPOS = 25                               # needle positions, rest to full
NEEDLE_A = 48.0                         # degrees each side of upright
NEEDLE_L = 38.0                         # its length, in FULL's pixels
KSTEPS = 11                             # the knob's detents, MIN to MAX
KNOB_A = 135.0                          # degrees each side of 12 o'clock


def needle_ends(rows):
    """NPOS (x, y) ends, from the pivot, on a dial `rows` tall."""
    ys = rows / float(DIAL_H)
    py = (DIAL_PY + 0.5) * ys - 0.5
    out = []
    for i in range(NPOS):
        a = math.radians(-NEEDLE_A + 2 * NEEDLE_A * i / (NPOS - 1))
        out.append((int(round(DIAL_PX + NEEDLE_L * math.sin(a))),
                    int(round(py - NEEDLE_L * math.cos(a) * ys))))
    return int(round(DIAL_PX)), int(round(py)), out


# the knob's CANVAS: the picture with its tick ring round it, drawn as one
KC_W = 80
KC = {56: (80, 68, 12, 10), 42: (80, 52, 12, 8)}   # rows: canvas w, h, knob x, y
RING_R1, RING_R2 = 31.0, 36.0           # the tick ring, FULL's pixels
IND_R1, IND_R2 = 7.0, 20.0              # the indicator: a black line, the
KNOB_PEN = ((0, 0), (1, 0), (0, 1))     # mockups' - three passes make it two
                                        # pixels thick at any angle


def knob_lines(rows):
    """-> (indicator lines, ring lines), each KSTEPS (x1, y1, x2, y2) in the
    canvas, for a knob `rows` tall."""
    cw, ch, kx, ky = KC[rows]
    ys = rows / float(KNOB_SZ)
    cx = kx + KNOB_C
    cy = ky + (KNOB_C + 0.5) * ys - 0.5
    ind, ring = [], []
    for i in range(KSTEPS):
        a = math.radians(-KNOB_A + 2 * KNOB_A * i / (KSTEPS - 1))
        s, c = math.sin(a), -math.cos(a) * ys

        def at(r):
            return int(round(cx + r * s)), int(round(cy + r * c))
        ind.append(at(IND_R1) + at(IND_R2))
        ring.append(at(RING_R1) + at(RING_R2))
    return ind, ring


# the dial's printed scale, by mode and by face: (text, x, y). VU: -20 -10 0
# +3 over the arc. BUFFER: 0 50 100 - and its zone is drawn GREEN, not red,
# since a full buffer is health and not an overload (SPEC.md 121.8)
DIAL_SCALE = {
    (56, 'vu'): [('-20', 6, 15), ('-10', 22, 6), ('0', 47, 3), ('+3', 66, 9)],
    (56, 'buf'): [('0', 7, 15), ('50', 39, 3), ('100', 66, 12)],
    (40, 'vu'): [('-20', 6, 9), ('-10', 20, 3), ('0', 41, 1), ('+3', 64, 5)],
    (40, 'buf'): [('0', 7, 10), ('50', 39, 1), ('100', 65, 6)],
}


def dial_scale(mode, rows):
    return DIAL_SCALE[(rows, mode)]


# =============================================================================
# EVERYTHING, in the order emitted
# =============================================================================

def all_bitmaps():
    bm = {}
    for name in DIGIT_SETS:
        w, h, t, style, _ = DIGIT_SETS[name]
        for i, ch in enumerate(DIGIT_ORDER):
            bm['%s_%d' % (name, i)] = digit(name, ch)
        cw = max(2, t + 1) if style != 'thin' else 3
        bm['%s_col' % name] = colon(h, max(1, t - 1) if style != 'thin' else 1, cw)
        if style == 'thin':
            bm['%s_sl' % name] = slash(h, max(5, w - 3))
            bm['%s_tl' % name] = tilde(h, 7)
    for n, f in ICONS.items():
        b = icon(f)
        bm['i_' + n] = b
        bm['h_' + n] = rows_or(b, 7)
    bm['screw'] = SCREW
    bm['screwm'] = SCREW_MID
    bm['grillf'] = grille(42, 38, 5)
    bm['grillm'] = grille(42, 26, 4)
    bm['logof'], bm['logom'] = logo_pictures()
    for tag, rows in (('f', 56), ('m', 42)):
        p0, p1, bw = knob_pictures(rows)
        bm['knob%s_p0' % tag], bm['knob%s_p1' % tag], bm['knob%s_bw' % tag] = p0, p1, bw
    for tag, rows in (('f', 56), ('m', 40)):
        k, r, bw = dial_pictures(rows)
        bm['dial%s_k' % tag], bm['dial%s_r' % tag], bm['dial%s_bw' % tag] = k, r, bw
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


def rowbits(r, w):
    v = 0
    for k in range(w):
        if r[k]:
            v |= 0x80 >> k
    return v


def emit_font(lines, name, order, glyph, h):
    """An index from CAPS_FIRST to CAPS_LAST (0 = none, else 1 + the glyph's
    number) and the glyphs, h bytes each, bit 7 leftmost."""
    lines.append("x3art_%s_ix:" % name)
    ix = [order.index(chr(c)) + 1 if chr(c) in order else 0
          for c in range(CAPS_FIRST, CAPS_LAST + 1)]
    for i in range(0, len(ix), 24):
        lines.append("    db " + ", ".join(str(v) for v in ix[i:i + 24]))
    lines.append("x3art_%s:" % name)
    for ch in order:
        g = glyph(ch)
        lines.append("    db " + ", ".join("0x%02X" % rowbits(r, len(r)) for r in g)
                     + "  ; %r" % ch)


def emit(bm):
    L = ["; GENERATED by tools/os88x3art.py - do not edit (SPEC.md 121.8)",
         "; the record: dw width; db height; db stride; rows x stride bytes",
         ""]
    for name, b in bm.items():
        w, h, stride, data = pack(b)
        L.append("x3art_%s:" % name)
        L.append("    dw %d" % w)
        L.append("    db %d, %d" % (h, stride))
        for i in range(0, len(data), 16):
            L.append("    db " + ", ".join("0x%02X" % v for v in data[i:i + 16]))
    for name in DIGIT_SETS:
        L.append("x3art_%s_tab:" % name)
        L.append("    dw " + ", ".join("x3art_%s_%d" % (name, i)
                                     for i in range(len(DIGIT_ORDER))))
    for name, (w, h, t, style, gap) in DIGIT_SETS.items():
        N = name.upper()
        L.append("X3ART_%s_W equ %d" % (N, w))
        L.append("X3ART_%s_H equ %d" % (N, h))
        L.append("X3ART_%s_CW equ %d" % (N, len(bm[name + '_col'][0])))
        L.append("X3ART_%s_GAP equ %d" % (N, gap))
        if style == 'thin':
            L.append("X3ART_%s_SW equ %d" % (N, len(bm[name + '_sl'][0])))
    # the fonts
    L.append("X3ART_FONT_FIRST equ %d" % CAPS_FIRST)
    L.append("X3ART_FONT_LAST equ %d" % CAPS_LAST)
    order = ''.join(caps_order())
    emit_font(L, 'caps', order, caps_glyph, 7)
    emit_font(L, 'lcdf', order, lambda c: lcd_glyph(c, 10), 10)
    L.append("x3art_lcdm_ix equ x3art_lcdf_ix")
    L.append("x3art_lcdm:")
    for ch in order:
        g = lcd_glyph(ch, 8)
        L.append("    db " + ", ".join("0x%02X" % rowbits(r, 6) for r in g) + "  ; %r" % ch)
    emit_font(L, 'dd', DIALDIG_ORDER,
              lambda c: [[1 if v == '#' else 0 for v in r] for r in DIALDIG[c]], 5)
    # every size the layout arithmetic uses
    for tag in ('f', 'm'):
        for pic in ('logo%s' % tag, 'knob%s_p0' % tag, 'dial%s_k' % tag,
                    'grill%s' % tag):
            b = bm[pic]
            N = pic.upper().replace('_P0', '').replace('_K', '')
            L.append("X3ART_%s_W equ %d" % (N, len(b[0])))
            L.append("X3ART_%s_H equ %d" % (N, len(b)))
    for tag, rows in (('F', 56), ('M', 42)):
        cw, ch, kx, ky = KC[rows]
        L.append("X3ART_KC%s_W equ %d" % (tag, cw))
        L.append("X3ART_KC%s_H equ %d" % (tag, ch))
        L.append("X3ART_KC%s_KX equ %d" % (tag, kx))
        L.append("X3ART_KC%s_KY equ %d" % (tag, ky))
        ind, ring = knob_lines(rows)
        L.append("x3art_ki%s:" % tag.lower())
        for v in ind:
            L.append("    db %d, %d, %d, %d" % v)
        L.append("x3art_kr%s:" % tag.lower())
        for v in ring:
            L.append("    db %d, %d, %d, %d" % v)
    for tag, rows in (('F', 56), ('M', 40)):
        px, py, ends = needle_ends(rows)
        L.append("X3ART_N%s_PX equ %d" % (tag, px))
        L.append("X3ART_N%s_PY equ %d" % (tag, py))
        L.append("x3art_nd%s:" % tag.lower())
        for i in range(0, len(ends), 8):
            L.append("    db " + ", ".join("%d, %d" % e for e in ends[i:i + 8]))
        for mode in ('vu', 'buf'):
            L.append("x3art_ds%s_%s:" % (tag.lower(), mode))
            for t, x, y in dial_scale(mode, rows):
                L.append("    db %d, %d, '%s', 0" % (x, y, t))
            L.append("    db 0xFF")
    L.append("X3ART_NPOS equ %d" % NPOS)
    L.append("X3ART_KSTEPS equ %d" % KSTEPS)
    L.append("X3ART_SCREW_W equ %d" % len(SCREW[0]))
    L.append("X3ART_SCREW_H equ %d" % len(SCREW))
    L.append("X3ART_SCREWM_H equ %d" % len(SCREW_MID))
    return "\n".join(L) + "\n"


def write_png(path, b, scale_=4, pal=None):
    """A PNG of one picture: a bitmap ink black on white, or a pixmap through
    the sixteen colours."""
    w, h = len(b[0]) * scale_, len(b) * scale_
    raw = bytearray()
    for row in b:
        line = bytearray([0])
        for px in row:
            rgb = PAL[px] if pal else ((0, 0, 0) if px else (255, 255, 255))
            line += bytes(rgb) * scale_
        for _ in range(scale_):
            raw += line

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


# --- the previews: the colour pictures composed as the package composes them ----
def plot(pm, x, y, c):
    if 0 <= y < len(pm) and 0 <= x < len(pm[0]):
        pm[y][x] = c


def knob_preview(bm, tag, rows, step, colour=True):
    cw, ch, kx, ky = KC[rows]
    ind, ring = knob_lines(rows)
    if colour:
        pm = blank(cw, ch, LGRAY)
        p0, p1 = bm['knob%s_p0' % tag], bm['knob%s_p1' % tag]
        grey = [BLACK, DGRAY, LGRAY, WHITE]
        for y in range(rows):
            for x in range(KNOB_SZ):
                pm[ky + y][kx + x] = grey[p0[y][x] | p1[y][x] << 1]
        ink, paper = BLACK, WHITE
    else:
        pm = blank(cw, ch, WHITE)
        for y, r in enumerate(bm['knob%s_bw' % tag]):
            for x, v in enumerate(r):
                if v:
                    pm[ky + y][kx + x] = BLACK
        ink, paper = BLACK, WHITE
    for x1, y1, x2, y2 in ring:
        for x, y in line_px(x1, y1, x2, y2):
            plot(pm, x, y, ink)
    x1, y1, x2, y2 = ind[step]
    for dx, dy in KNOB_PEN:
        for x, y in line_px(x1 + dx, y1 + dy, x2 + dx, y2 + dy):
            plot(pm, x, y, ink)
    return pm


def dial_preview(bm, tag, rows, pos, mode, colour=True):
    k, r = bm['dial%s_k' % tag], bm['dial%s_r' % tag]
    pm = blank(DIAL_W, rows)
    for y in range(rows):
        for x in range(DIAL_W):
            if colour:
                c = YELLOW if (x + y) % 2 == 0 else WHITE
                if r[y][x]:
                    c = RED if mode == 'vu' else 2
            else:
                c = BLACK if bm['dial%s_bw' % tag][y][x] else WHITE
            if k[y][x]:
                c = BLACK
            pm[y][x] = c
    for t, x, y in dial_scale(mode, rows):
        for ch in t:
            for gy, gr in enumerate(DIALDIG[ch]):
                for gx, v in enumerate(gr):
                    if v == '#':
                        plot(pm, x + gx, y + gy, BLACK)
            x += 4
    px, py, ends = needle_ends(rows)
    ex, ey = ends[pos]
    for dx in (0, 1):
        for x, y in line_px(px + dx, py, ex + dx, ey):
            plot(pm, x, y, BLACK)
    return pm


def selfcheck():
    bad = 0
    bm = all_bitmaps()
    for name, (w, h, t, style, gap) in DIGIT_SETS.items():
        ds = [bm['%s_%d' % (name, i)] for i in range(10)]
        one = bm['%s_1' % name]
        lit = sum(1 for r in one if any(r))
        floor = h - 3 * t - 1 if style != 'thin' else h - 3
        if lit < floor:
            print("digit set %s: a '1' inks %d of %d rows - its vertical "
                  "segments have no length" % (name, lit, h))
            bad += 1
        if any((len(d), len(d[0])) != (h, w) for d in ds):
            print("digit set %s is not %dx%d" % (name, w, h))
            bad += 1
        if len(set(tuple(map(tuple, d)) for d in ds)) != 10:
            print("digit set %s has two digits alike" % name)
            bad += 1
        eight = bm['%s_8' % name]
        for i in range(10):
            d = bm['%s_%d' % (name, i)]
            if any(d[y][x] and not eight[y][x] for y in range(h) for x in range(w)):
                print("%s: digit %d has ink 8 lacks" % (name, i))
                bad += 1
    for ch, rows in CAPS.items():
        if len(rows) != 7 or any(len(r) != 5 for r in rows):
            print("caps glyph %r is not 5x7" % ch)
            bad += 1
    for ch in caps_order():
        for h in (10, 8):
            if len(lcd_glyph(ch, h)) != h:
                print("LCD glyph %r is not %d rows" % (ch, h))
                bad += 1
    for n in ICONS:
        if not any(any(r) for r in bm['h_' + n]):
            print("the CGA's %s icon is empty" % n)
            bad += 1
    # the masters' derivations: sizes, the groove gone, the planes' greys
    for tag, rows in (('f', 56), ('m', 42)):
        for p in ('p0', 'p1', 'bw'):
            b = bm['knob%s_%s' % (tag, p)]
            if (len(b), len(b[0])) != (rows, KNOB_SZ):
                print("knob%s_%s is not %dx%d" % (tag, p, KNOB_SZ, rows))
                bad += 1
    g = knob_master()
    x1, y1, x2, y2 = KNOB_GROOVE
    if sum(1 for y in range(y1, y2 + 1) for x in range(x1, x2 + 1)
           if g[y][x] == BLACK) > 2:
        print("the knob's groove is still in the picture")
        bad += 1
    for tag, rows in (('f', 56), ('m', 40)):
        for p in ('k', 'r', 'bw'):
            b = bm['dial%s_%s' % (tag, p)]
            if (len(b), len(b[0])) != (rows, DIAL_W):
                print("dial%s_%s is not %dx%d" % (tag, p, DIAL_W, rows))
                bad += 1
        if not any(any(r) for r in bm['dial%s_r' % tag]):
            print("dial%s has no red zone" % tag)
            bad += 1
        px, py, ends = needle_ends(rows)
        if any(not (1 <= x < DIAL_W - 1 and 1 <= y < rows - 1) for x, y in ends):
            print("a needle of dial%s leaves the face" % tag)
            bad += 1
        if ends[0][0] >= px or ends[-1][0] <= px:
            print("dial%s's needle does not swing both ways" % tag)
            bad += 1
    for rows in KC:
        cw, ch, kx, ky = KC[rows]
        if kx + KNOB_SZ > cw or ky + rows > ch:
            print("the knob does not fit its %d-row canvas" % rows)
            bad += 1
        ind, ring = knob_lines(rows)
        for v in ind + ring:
            if not (1 <= v[0] < cw - 1 and 1 <= v[2] < cw - 1 and
                    1 <= v[1] < ch - 1 and 1 <= v[3] < ch - 1):
                print("a knob line leaves the %d-row canvas: %r" % (rows, v))
                bad += 1
    for (rows, mode), marks in DIAL_SCALE.items():
        for t, x, y in marks:
            if any(c not in DIALDIG for c in t) or x + 4 * len(t) > DIAL_W \
                    or y + 5 > rows:
                print("dial scale mark %r does not fit" % t)
                bad += 1
    if emit(bm) != emit(all_bitmaps()):
        print("two runs differ")
        bad += 1
    print("os88x3art: selfcheck %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0


def main():
    if "--selfcheck" in sys.argv[1:]:
        sys.exit(selfcheck())
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--png", help="a directory: one preview PNG a picture, and "
                    "the knob and dials composed as the package composes them")
    a = ap.parse_args()
    bm = all_bitmaps()
    text = emit(bm)
    with open(a.out, "w") as f:
        f.write(text)
    if a.png:
        os.makedirs(a.png, exist_ok=True)
        for n, b in bm.items():
            write_png(os.path.join(a.png, n + ".png"), b)
        for tag, rows in (('f', 56), ('m', 42)):
            for step in (0, 5, 10):
                write_png(os.path.join(a.png, "knob%s_col_%d.png" % (tag, step)),
                          knob_preview(bm, tag, rows, step), pal=True)
                write_png(os.path.join(a.png, "knob%s_bwc_%d.png" % (tag, step)),
                          knob_preview(bm, tag, rows, step, False), pal=True)
        for tag, rows in (('f', 56), ('m', 40)):
            for mode in ('vu', 'buf'):
                for pos in (0, 12, 24):
                    write_png(os.path.join(a.png, "dial%s_%s_col_%d.png" % (tag, mode, pos)),
                              dial_preview(bm, tag, rows, pos, mode), pal=True)
                    write_png(os.path.join(a.png, "dial%s_%s_bwc_%d.png" % (tag, mode, pos)),
                              dial_preview(bm, tag, rows, pos, mode, False), pal=True)
        lcd = [lcd_glyph(c, 10) for c in "01  NEON DRIVE"]
        strip = blank(9 * len(lcd), 10)
        for i, g in enumerate(lcd):
            paste(strip, g, 9 * i, 0)
        write_png(os.path.join(a.png, "lcd_sample.png"), strip)
        lcdm = [lcd_glyph(c, 8) for c in "01  NEON DRIVE"]
        strip = blank(8 * len(lcdm), 8)
        for i, g in enumerate(lcdm):
            paste(strip, g, 8 * i, 0)
        write_png(os.path.join(a.png, "lcdm_sample.png"), strip)
    size = sum(4 + len(pack(b)[3]) for b in bm.values())
    print("os88x3art: %s - %d pictures, %d bytes" % (a.out, len(bm), size))


if __name__ == "__main__":
    main()
