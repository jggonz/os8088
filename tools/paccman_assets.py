#!/usr/bin/env python3
"""Turn pacman.c's tables into apps/paccman/pmc_rom.c (SPEC.md 91).

    python3 tools/paccman_assets.py --ref ../pacman.c
    python3 tools/paccman_assets.py --ref ../pacman.c --check

The reference is Andre Weissflog's pacman.c (https://github.com/floooh/pacman.c,
MIT, 2020) at the PINNED commit below; the tile, sprite, hardware-colour and
palette tables it embeds are Pac-Man arcade ROM data (Namco) and the two sound
register dumps were captured from an arcade emulator. Shipping them follows the
user-decided precedent of apps/c64/rom (docs/C64-SPEC.md); the provenance is
stated in the generated file's header, in apps/paccman/README.md, in SPEC.md 91
and on the About card.

NOTHING FROM THE REFERENCE IS VENDORED (CONTRIBUTING.md 6). What is committed
is apps/paccman/pmc_rom.c - this tool's OUTPUT, with the pin in its header -
and the build's truth is that committed file: --check runs only when a checkout
is present, and every ordinary build needs neither this tool nor the reference.

The decoders below are pacman.c's own, transcribed:
  tiles/sprites  gfx_decode_tile_8x4 at pacman.c 2800-2845 (2bpp, and the ROM
                 data is COUNTER-ROTATED because the arcade display is)
  colours        gfx_decode_color_palette at 2865-2891 (a 256-entry palette
                 indirecting into a 32-entry hardware palette; intensities
                 0x97 + 0x47 + 0x21; pixel 0 of every block is transparent)
  sound          the register format comment at pacman.c 3949-3958: volume in
                 bits 31-28, waveform 27-25, frequency 19-0. Frequency counts
                 at 96 kHz / 2^20, so Hz = f * 96000 >> 20.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

PIN = '0f5ec5a384c1988d9889046d92e615219e1cf3b4'
OUT = Path('apps/paccman/pmc_rom.c')

# --- the 16 EGA/VGA indices this OS draws in, as RGB (SPEC.md 39) ------------
EGA = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
       (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
       (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
       (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255)]

# --- nibble -> monochrome CLASS, authored by eye on VIDEO=cga and VIDEO=herc
# 0 black, 1 50% dither, 2 solid white (SPEC.md 39.4: grey rounds to black on
# a 1bpp adapter, so a three-way class table is the whole of the mono look).
# Walls (blue), dots and Pac-Man (yellow) are white; the ghost bodies are
# dither so four of them over a white wall stay four ghosts; black is black.
MONO = {
    0: 0,    # black          - the field
    1: 2,    # blue           - the maze walls
    2: 1,    # green
    3: 1,    # cyan
    4: 1,    # red            - Blinky
    5: 1,    # magenta
    6: 1,    # brown
    7: 2,    # light grey
    8: 1,    # dark grey
    9: 1,    # light blue     - Inky
    10: 1,   # light green
    11: 1,   # light cyan
    12: 1,   # light red
    13: 1,   # light magenta  - Pinky
    14: 2,   # yellow         - Pac-Man, dots, Clyde
    15: 2,   # white          - text, the frightened ghosts' faces
}

# --- the maze, pacman.c 1377-1432 (rows 3..33 of the 28x36 field) ------------
# Carried here verbatim from the reference so that the port's playfield is the
# reference's playfield and not a redrawing of it.
MAZE_ROWS = 31
MAZE_COLS = 28
MAZE_CHARMAP = {
    ' ': 0x40, '0': 0xD1, '1': 0xD0, '2': 0xD5, '3': 0xD4, '4': 0xFB,
    '5': 0xFA, '6': 0xD7, '7': 0xD9, '8': 0xD6, '9': 0xD8, 'U': 0xDB,
    'L': 0xD3, 'R': 0xD2, 'B': 0xDC, 'b': 0xDF, 'e': 0xE7, 'f': 0xE6,
    'g': 0xEB, 'h': 0xEA, 'l': 0xE8, 'r': 0xE9, 'u': 0xE5, 'w': 0xF5,
    'x': 0xF2, 'y': 0xF3, 'z': 0xF4, 'm': 0xED, 'n': 0xEC, 'o': 0xEF,
    'p': 0xEE, 'j': 0xDD, 'i': 0xD2, 'k': 0xDB, 'q': 0xD3, 's': 0xF1,
    't': 0xF0, '-': 0xCF, 'P': 0x14,
}
TILE_DOT = 0x10


def carray(decl, values, per_row=16, kind='unsigned char'):
    """Emit one C array, deterministically. `decl` is the WHOLE declaration -
    the type is in it, because a byte table and a word table print their
    entries differently and `kind` is only what chooses the format."""
    out = ['%s = {' % decl]
    fmt = '0x%02X' if kind.endswith('char') else '%d'
    for i in range(0, len(values), per_row):
        row = ', '.join(fmt % v for v in values[i:i + per_row])
        out.append('    %s,' % row)
    out.append('};')
    return '\n'.join(out)


def read_ref(ref):
    src = (ref / 'pacman.c') if (ref / 'pacman.c').is_file() else ref
    if not src.is_file():
        sys.exit('paccman_assets: no pacman.c at %s' % ref)
    return src.read_text(), src.parent


def head(ref_dir, unpinned):
    """Refuse a checkout that is not the pin (LESSONS.md 1: the reference is
    a moving target and the tables are not)."""
    try:
        got = subprocess.run(['git', '-C', str(ref_dir), 'rev-parse', 'HEAD'],
                             capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        got = None
    if got != PIN and not unpinned:
        sys.exit('paccman_assets: the reference at %s is %s, not the pinned\n'
                 '                %s. --unpinned overrides, and then the\n'
                 '                generated header no longer means what it says.'
                 % (ref_dir, got or '(not a git checkout)', PIN))
    return got


def table(src, decl):
    """The initialiser of `decl`, as a list of ints. Handles the two shapes
    pacman.c uses: `static const uint8_t rom_tiles[4096] = {` and the uint32
    dumps.  The forward declarations (no `= {`) are skipped by construction."""
    m = re.search(re.escape(decl) + r'\s*=\s*\{', src)
    if not m:
        sys.exit('paccman_assets: %s not found in the reference' % decl)
    depth, i = 1, m.end()
    while depth:
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
        i += 1
    body = re.sub(r'/\*.*?\*/', ' ', src[m.end():i - 1], flags=re.S)
    body = re.sub(r'//[^\n]*', ' ', body)
    vals = []
    for tok in body.split(','):
        tok = tok.strip()
        if not tok:
            continue
        if not re.fullmatch(r'0[xX][0-9A-Fa-f]+|\d+', tok):
            sys.exit('paccman_assets: %s: not a literal: %r' % (decl, tok))
        vals.append(int(tok, 0))
    return vals


def decode_8x4(dst, dst_w, tex_x, tex_y, base, stride, offset, code):
    """pacman.c 2800-2818, gfx_decode_tile_8x4 - verbatim."""
    for tx in range(8):
        ti = code * stride + offset + (7 - tx)
        b = base[ti]
        for ty in range(4):
            p_hi = (b >> (7 - ty)) & 1
            p_lo = (b >> (3 - ty)) & 1
            dst[(tex_y + ty) * dst_w + tex_x + tx] = (p_hi << 1) | p_lo


def decode_tiles(rom_tiles):
    """256 tiles, each 8x8 2bpp -> 16 bytes, 2 per row, leftmost pixel in the
    high bits (which is the order pmcband.inc's XLAT pair table wants)."""
    out = []
    for code in range(256):
        px = [0] * 64
        decode_8x4(px, 8, 0, 0, rom_tiles, 16, 8, code)
        decode_8x4(px, 8, 0, 4, rom_tiles, 16, 0, code)
        for row in range(8):
            r = px[row * 8:row * 8 + 8]
            out.append((r[0] << 6) | (r[1] << 4) | (r[2] << 2) | r[3])
            out.append((r[4] << 6) | (r[5] << 4) | (r[6] << 2) | r[7])
    return out


def decode_sprites(rom_sprites):
    """64 sprites, each 16x16 2bpp -> 64 bytes, 4 per row, same bit order."""
    out = []
    for code in range(64):
        px = [0] * 256
        for (x, y, off) in ((0, 0, 40), (8, 0, 8), (0, 4, 48), (8, 4, 16),
                            (0, 8, 56), (8, 8, 24), (0, 12, 32), (8, 12, 0)):
            decode_8x4(px, 16, x, y, rom_sprites, 64, off, code)
        for row in range(16):
            r = px[row * 16:row * 16 + 16]
            for g in range(0, 16, 4):
                out.append((r[g] << 6) | (r[g + 1] << 4)
                           | (r[g + 2] << 2) | r[g + 3])
    return out


def nearest_ega(rgb):
    r, g, b = rgb
    return min(range(16), key=lambda i: (EGA[i][0] - r) ** 2
               + (EGA[i][1] - g) ** 2 + (EGA[i][2] - b) ** 2)


def decode_palette(hwcolors, palette):
    """pacman.c 2865-2891: the 256-entry palette indirects into the 32-entry
    hardware palette. Pixel 0 of every 4-entry block is TRANSPARENT there; here
    the field's ground is black, so a tile's index 0 is black and a sprite's is
    skipped by the composer. Only the first 32 blocks are reachable - the
    colour codes in the game go to 0x1F."""
    hw = []
    for rgb in hwcolors:
        r = ((rgb >> 0) & 1) * 0x21 + ((rgb >> 1) & 1) * 0x47 + ((rgb >> 2) & 1) * 0x97
        g = ((rgb >> 3) & 1) * 0x21 + ((rgb >> 4) & 1) * 0x47 + ((rgb >> 5) & 1) * 0x97
        b = ((rgb >> 6) & 1) * 0x47 + ((rgb >> 7) & 1) * 0x97
        hw.append(nearest_ega((r, g, b)))
    pal = []
    for i in range(32 * 4):
        pal.append(0 if (i & 3) == 0 else hw[palette[i] & 0xF])
    return pal


def planar_table():
    """packed byte (two 4bpp pixels, high nibble leftmost) -> the four planes'
    two-bit fragments, nibble-spaced: bits 1:0 plane 0, 5:4 plane 1, 9:8 plane
    2, 13:12 plane 3, each holding (left pixel's bit << 1) | right pixel's bit.
    Nibble spacing is what lets _pmc_pack_pl accumulate four pixels of all four
    planes in ONE word shift (SPEC.md 91)."""
    out = []
    for b in range(256):
        hi, lo = b >> 4, b & 0x0F
        w = 0
        for p in range(4):
            w |= ((((hi >> p) & 1) << 1) | ((lo >> p) & 1)) << (4 * p)
        out.append(w)
    return out


def zero_mask_table():
    """source byte (four 2-bit tile pixels) -> 0b11 in every field that is
    ZERO, 0 in every field that is not.

    THIS IS THE CGA ROW MERGE (SPEC.md 91). A short display samples every
    other source row, and the arcade font keeps every horizontal middle
    stroke on the row that is dropped, so `B E F G H S 3 6 9` lose theirs and
    `HIGH SCORE` reads as a row of bars. Merging the two rows cannot be an OR:
    a tile pixel is a 2-bit colour INDEX, so `1 | 2` is 3 - an ink the tile
    does not have. The merge is per pixel, `take the odd row's pixel only
    where the even row's is 0`, and this table is what makes it two extra
    instructions and one XLAT a source byte inside _pmc_tile:

        merged = even | (odd & pmc_zmask[even])

    Computed rather than read out of the reference - it is a property of the
    2bpp format, the same way pmc_planar is a property of the packed one."""
    out = []
    for b in range(256):
        m = 0
        for f in range(4):
            if ((b >> (2 * f)) & 3) == 0:
                m |= 3 << (2 * f)
        out.append(m)
    return out


def mono_pair_table(mono):
    """[parity][packed byte] -> two 1bpp bits (bit 1 = the left pixel). A
    dithered class lights when (x + y) is even, so the table is per row
    parity and the composer picks one of the two per row."""
    out = []
    for parity in range(2):
        for b in range(256):
            v = 0
            for k, nib in enumerate((b >> 4, b & 0x0F)):
                cls = mono[nib]
                lit = 2 if cls == 2 else (1 if (cls == 1 and ((k + parity) & 1) == 0) else 0)
                if lit:
                    v |= (2 >> k)
            out.append(v)
    return out


def maze(src):
    """The 31x28 ASCII map at pacman.c 1381-1414, pre-decoded to tile codes so
    that neither the string nor the runtime char table ships."""
    m = re.search(r'static const char\* tiles =(.*?);', src, re.S)
    if not m:
        sys.exit('paccman_assets: the playfield map was not found')
    rows = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    if len(rows) != MAZE_ROWS:
        sys.exit('paccman_assets: the map is %d rows, expected %d'
                 % (len(rows), MAZE_ROWS))
    out = []
    for r in rows:
        if len(r) != MAZE_COLS:
            sys.exit('paccman_assets: a map row is %d wide, expected %d'
                     % (len(r), MAZE_COLS))
        for c in r:
            out.append(MAZE_CHARMAP.get(c, TILE_DOT))
    return out


def levels(src):
    """The 21-row level table at pacman.c 600-623, as three parallel arrays -
    a struct table would be a struct copy the moment anything returned one
    (SPEC.md 73.5.1)."""
    m = re.search(r'levelspec_table\[MAX_LEVELSPEC\]\s*=\s*\{(.*?)\n\};', src, re.S)
    if not m:
        sys.exit('paccman_assets: levelspec_table not found')
    fruit_names = ['FRUIT_NONE', 'FRUIT_CHERRIES', 'FRUIT_STRAWBERRY',
                   'FRUIT_PEACH', 'FRUIT_APPLE', 'FRUIT_GRAPES',
                   'FRUIT_GALAXIAN', 'FRUIT_BELL', 'FRUIT_KEY']
    fruit, bonus, fright = [], [], []
    for row in re.finditer(r'\{\s*(FRUIT_\w+)\s*,\s*(\d+)\s*,\s*([0-9*\s]+?)\s*,?\s*\}',
                           m.group(1)):
        fruit.append(fruit_names.index(row.group(1)))
        bonus.append(int(row.group(2)))
        fright.append(eval(row.group(3), {'__builtins__': {}}))   # `6*60`
    if len(fruit) != 21:
        sys.exit('paccman_assets: the level table is %d rows, expected 21' % len(fruit))
    return fruit, bonus, fright


def snd_dump(vals, nvoices):
    """A register dump is `nvoices` interleaved 32-bit registers per tick:
    volume in bits 31-28, frequency in bits 19-0 (pacman.c 3949-3958). The
    frequency counter runs at 96 kHz / 2^20, so Hz = f * 96000 >> 20. Both
    columns are decoded on the HOST; nothing 32-bit reaches the 8086."""
    ticks = len(vals) // nvoices
    hz = [[0] * ticks for _ in range(nvoices)]
    vol = [[0] * ticks for _ in range(nvoices)]
    for t in range(ticks):
        for v in range(nvoices):
            reg = vals[t * nvoices + v]
            f = reg & 0xFFFFF
            hz[v][t] = (f * 96000) >> 20
            vol[v][t] = (reg >> 28) & 0x0F
    return hz, vol, ticks


HEADER = '''/* ============================================================================
 * os8088 - apps/paccman/pmc_rom.c   *** GENERATED - DO NOT EDIT ***
 *
 * Written by tools/paccman_assets.py from Andre Weissflog's pacman.c
 * (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre Weissflog, at
 * commit %s.
 *
 * PROVENANCE. The code this file was decoded BY is the author's under MIT.
 * The tile, sprite, hardware-colour and palette tables are Pac-Man arcade ROM
 * data (Namco), and the two sound register dumps were captured from an arcade
 * emulator - the reference embeds all of them the same way. Shipping them here
 * follows the user-decided precedent of apps/c64/rom (docs/C64-SPEC.md), a
 * stated departure from CONTRIBUTING.md 6 for these tables alone. SPEC.md 91
 * and apps/paccman/README.md carry the full statement; the About card carries
 * the credits.
 *
 * To reproduce it, from a checkout of the reference at that commit:
 *     python3 tools/paccman_assets.py --ref <path-to-pacman.c>
 * and `--check` diffs this file against what the tool would write now. That
 * check runs only when a checkout is present: THIS FILE IS THE BUILD'S TRUTH,
 * and an ordinary build needs neither the tool nor the reference.
 *
 * Everything here is #included into apps/paccman/paccman.c, which is one
 * translation unit (SPEC.md 73.1).
 * ==========================================================================*/

'''


def generate(src, ref_head):
    rom_tiles = table(src, 'static const uint8_t rom_tiles[4096]')
    rom_sprites = table(src, 'static const uint8_t rom_sprites[4096]')
    hwcolors = table(src, 'static const uint8_t rom_hwcolors[32]')
    palette = table(src, 'static const uint8_t rom_palette[256]')
    prelude = table(src, 'static const uint32_t snd_dump_prelude[490]')
    dead = table(src, 'static const uint32_t snd_dump_dead[90]')

    pal = decode_palette(hwcolors, palette)
    mono = [MONO[i] for i in range(16)]
    fruit, bonus, fright = levels(src)
    p_hz, p_vol, p_ticks = snd_dump(prelude, 2)
    d_hz, d_vol, d_ticks = snd_dump(dead, 1)

    parts = [HEADER % PIN]
    parts.append('#define PMC_ROM_PIN "%s"\n' % PIN)
    parts.append('#define PMC_SND_PRELUDE_TICKS %d' % p_ticks)
    parts.append('#define PMC_SND_DEAD_TICKS    %d\n' % d_ticks)

    parts.append('/* 256 tiles, 8x8 at 2 bits a pixel: two bytes a row, the\n'
                 " * leftmost pixel in the high bits (pacman.c's gfx_decode_tile). */")
    parts.append(carray('static const unsigned char pmc_tiles[256 * 16]',
                        decode_tiles(rom_tiles)))

    parts.append('\n/* 64 sprites, 16x16 at 2 bits a pixel: four bytes a row.\n'
                 ' * Kept whole so the sprite numbering is the reference\'s. */')
    parts.append(carray('static const unsigned char pmc_sprites[64 * 64]',
                        decode_sprites(rom_sprites)))

    parts.append('\n/* colour block * 4 + pixel -> the nearest of this OS\'s 16\n'
                 ' * indices. Pixel 0 of a block is transparent on the arcade\n'
                 ' * board; here a tile draws it black and a sprite skips it. */')
    parts.append(carray('static const unsigned char pmc_pal[32 * 4]', pal))

    parts.append('\n/* colour block * 16 + a source NIBBLE (two 2-bit pixels)\n'
                 ' * -> the packed 4bpp byte for that pixel pair. The colour\n'
                 " * indirection, the nibble split and the pack are one of\n"
                 " * pmcband.inc's XLATs each, because the table is indexed by\n"
                 ' * the very nibble the tile ROM already holds. */')
    parts.append(carray('static const unsigned char pmc_pairs[32 * 16]',
                        [(pal[c * 4 + (n >> 2)] << 4) | pal[c * 4 + (n & 3)]
                         for c in range(32) for n in range(16)]))

    parts.append('\n/* nibble -> monochrome class: 0 black, 1 50%% dither, 2 white. */')
    parts.append(carray('static const unsigned char pmc_mono[16]', mono))

    parts.append('\n/* [row parity][packed byte] -> two 1bpp bits, bit 1 the\n'
                 ' * left pixel: pmc_mono resolved against the checkerboard. */')
    parts.append(carray('static const unsigned char pmc_mono2[2 * 256]',
                        mono_pair_table(mono)))

    parts.append('\n/* packed byte -> the four planes\' 2-bit fragments, nibble\n'
                 ' * spaced (plane 0 in bits 1:0, plane 3 in bits 13:12). */')
    parts.append(carray('static const unsigned int pmc_planar[256]', planar_table(),
                        per_row=8, kind='unsigned int'))

    parts.append('\n/* source byte -> 0b11 in every 2-bit pixel field that is\n'
                 ' * ZERO: the CGA row merge inside _pmc_tile (SPEC.md 91),\n'
                 ' * merged = even | (odd & pmc_zmask[even]). */')
    parts.append(carray('static const unsigned char pmc_zmask[256]',
                        zero_mask_table(), per_row=16))

    parts.append('\n/* the 31x28 playfield, rows 3..33, pre-decoded to tile\n'
                 ' * codes: neither the map string nor the char table ships. */')
    parts.append(carray('static const unsigned char pmc_maze[%d * %d]'
                        % (MAZE_ROWS, MAZE_COLS), maze(src), per_row=MAZE_COLS))

    parts.append('\n/* the 21-row level table, as three parallel arrays. */')
    parts.append(carray('static const unsigned char pmc_lvl_fruit[21]', fruit, per_row=21))
    parts.append(carray('static const unsigned int pmc_lvl_bonus[21]', bonus,
                        per_row=21, kind='unsigned int'))
    parts.append(carray('static const unsigned int pmc_lvl_fright[21]', fright,
                        per_row=21, kind='unsigned int'))

    parts.append('\n/* fruit -> { tile quad, sprite, colour } and the four\n'
                 ' * bonus-score tiles (pacman.c 564-587). */')
    parts.append(carray('static const unsigned char pmc_fruit_tc[9 * 3]',
                        [v for row in table_rows(src, 'fruit_tiles_colors') for v in row],
                        per_row=3))
    parts.append(carray('static const unsigned char pmc_fruit_score_tiles[9 * 4]',
                        [v for row in table_rows(src, 'fruit_score_tiles') for v in row],
                        per_row=4))

    parts.append('\n/* the prelude dump: voice 0 is the bass, voice 1 the\n'
                 ' * MELODY - every 32-bit register reduced on the host to a\n'
                 ' * frequency in Hz and a 4-bit volume (pacman.c 3949-3958). */')
    for v in range(2):
        parts.append(carray('static const unsigned int pmc_snd_prelude_hz%d[%d]' % (v, p_ticks),
                            p_hz[v], per_row=12, kind='unsigned int'))
    for v in range(2):
        parts.append(carray('static const unsigned char pmc_snd_prelude_vol%d[%d]' % (v, p_ticks),
                            p_vol[v], per_row=24))

    parts.append('\n/* the death dump, voice 2. */')
    parts.append(carray('static const unsigned int pmc_snd_dead_hz[%d]' % d_ticks,
                        d_hz[0], per_row=12, kind='unsigned int'))
    parts.append(carray('static const unsigned char pmc_snd_dead_vol[%d]' % d_ticks,
                        d_vol[0], per_row=24))

    return '\n'.join(parts) + '\n'


def table_rows(src, name):
    """A two-dimensional uint8_t table, as a list of rows. The entries are
    enum NAMES in the reference, so they are resolved against its own enum."""
    m = re.search(re.escape(name) + r'\[[^\]]*\]\[(\d+)\]\s*=\s*\{(.*?)\n\};', src, re.S)
    if not m:
        sys.exit('paccman_assets: %s not found' % name)
    width = int(m.group(1))
    body = re.sub(r'//[^\n]*', ' ', m.group(2))
    enums = ref_enums(src)
    rows = []
    for row in re.finditer(r'\{([^{}]*)\}', body):
        vals = []
        for tok in row.group(1).split(','):
            tok = tok.strip()
            if not tok:
                continue
            if re.fullmatch(r'0[xX][0-9A-Fa-f]+|\d+', tok):
                vals.append(int(tok, 0))
            elif tok in enums:
                vals.append(enums[tok])
            else:
                sys.exit('paccman_assets: %s: cannot resolve %r' % (name, tok))
        if len(vals) != width:
            sys.exit('paccman_assets: %s: a row is %d wide, expected %d'
                     % (name, len(vals), width))
        rows.append(vals)
    return rows


_ENUMS = {}


def ref_enums(src):
    """The reference's tile/sprite/colour enum, so a table written in names
    resolves to the same numbers the reference gives them."""
    if _ENUMS:
        return _ENUMS
    m = re.search(r'enum \{(.*?)\n\};', src, re.S)
    body = re.sub(r'//[^\n]*', ' ', m.group(1))
    body = re.sub(r'/\*.*?\*/', ' ', body, flags=re.S)
    for item in re.finditer(r'(\w+)\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)', body):
        _ENUMS[item.group(1)] = int(item.group(2), 0)
    return _ENUMS


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--ref', help='a checkout of github.com/floooh/pacman.c '
                                  'at %s (or $PACMANC_SRC)' % PIN[:7])
    ap.add_argument('--check', action='store_true',
                    help='diff the committed file against what would be written')
    ap.add_argument('--unpinned', action='store_true',
                    help='accept a checkout that is not the pin')
    ap.add_argument('-o', '--out', default=str(OUT))
    args = ap.parse_args()

    import os
    ref = args.ref or os.environ.get('PACMANC_SRC')
    if not ref:
        sys.exit('paccman_assets: no reference. Pass --ref or set PACMANC_SRC to a\n'
                 '                checkout of github.com/floooh/pacman.c at %s.\n'
                 '                The committed %s is the build\'s truth and\n'
                 '                needs neither.' % (PIN, OUT))
    src, ref_dir = read_ref(Path(ref))
    ref_head = head(ref_dir, args.unpinned)
    text = generate(src, ref_head)

    out = Path(args.out)
    if args.check:
        if not out.is_file():
            sys.exit('paccman_assets: %s does not exist' % out)
        have = out.read_text()
        if have != text:
            # Name the first differing line rather than dumping two files.
            a, b = have.splitlines(), text.splitlines()
            for i in range(max(len(a), len(b))):
                x = a[i] if i < len(a) else '(end of file)'
                y = b[i] if i < len(b) else '(end of file)'
                if x != y:
                    sys.exit('paccman_assets: %s does not reproduce at line %d\n'
                             '  committed: %s\n  regenerated: %s'
                             % (out, i + 1, x[:90], y[:90]))
        print('paccman_assets: %s reproduces byte for byte from %s' % (out, PIN[:7]))
        return
    out.write_text(text)
    print('paccman_assets: wrote %s (%d bytes) from %s'
          % (out, len(text), (ref_head or PIN)[:7]))


if __name__ == '__main__':
    main()
