#!/usr/bin/env python3
"""Rebuild the native Gorillas art and palette tables (SPEC.md 99.1, 99.3).

Reference: reference/gorillas/gorilla.bas, SetScreen/DrawGorilla/DoSun.
The BASIC's EGA palette values are six-bit rgbrgb colors, not VGA indices.
No imaging libraries or runtime external assets are needed.

WHAT IS GENERATED AND WHAT IS DERIVED AT RUN TIME. `grart.inc` carries the
sources only: the three 16-entry palettes, the DAC, the 16x20 gorilla and its
left-arm pose, the marquee's changed-row runs and the bit-packed sun.
Everything the renderers index - the ink-pair tables of every surface, the
right-arm pose, both intro sprite forms and the five marquee masks - is built
from them by the package (`gr_inktables`, `gr_mirrorpose`, `gr_frontprepare`;
`gr_gorillapose` draws the mirror directly). The functions
below that compute those derived forms are the REFERENCE the gates compare
the guest's own copies against (tests/gorillascity.py, tests/gorillasmenu.py),
so this file stays the one statement of what every byte must be.
"""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Game ink -> EGA palette value. The original's 0,1,2,3,5,6,7 are literal
# SetScreen values. 8/14/15 are its default dark gray/yellow/white colors.
EGA = (1, 46, 44, 54, 0, 7, 4, 3, 56, 63, 38, 20, 56, 0, 62, 63)
DESKTOP = (1, 6, 12, 14, 0, 7, 4, 3, 8, 15, 14, 6, 8, 0, 14, 15)
MONO = (0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1)
CGA = (0, 3, 2, 3, 0, 2, 2, 1, 0, 3, 3, 2, 2, 0, 3, 3)
APE = (
    '0000011111100000',
    '000011AAAA110000',
    '0001111111110000',
    '0001100000110000',
    '0001110101110000',
    '0000111111100000',
    '0000011111000000',
    '0011111111111100',
    '0111A111111A1110',
    '1111A110011A1111',
    '1111A100001A1111',
    '111B11111111B111',
    '111B11111111B111',
    '111B11111111B111',
    '011B11111111B110',
    '0000111BB1110000',
    '0001111001111000',
    '00111B0000B11100',
    '01111B0000B11110',
    '0111110000111110',
)


def ega_rgb6(value):
    return tuple(42 * ((value >> bit) & 1) + 21 * ((value >> (bit + 3)) & 1)
                 for bit in (2, 1, 0))


def packed(rows):
    """16-pixel rows of hex inks -> packed 4bpp bytes, high nibble first."""
    return bytes(int(''.join(row[i:i+2]), 16) for row in rows for i in range(0, 16, 2))


def poses():
    """The raised-arm pair (left, right) as rows: one arm lifted, the face,
    chest and opposite arm kept; the right pose is the left one mirrored."""
    left = [list(row) for row in APE]
    for y in range(7, 15):
        for x in range(3):
            left[y][x] = '0'
    for y in range(2, 10):
        for x in range(3):
            left[y][x] = 'A' if x == 1 else '1'
    left[8][3] = left[9][3] = '1'
    left = [''.join(row) for row in left]
    return left, [row[::-1] for row in left]


def pair_tables():
    """Every ink-pair table the renderers have ever indexed, by its old name.

    gr_inktables builds the live surface's subset of these into gr_inkpairs;
    tests/gorillascity.py compares that area against these lists."""
    t = {'gr_winmap': [(DESKTOP[i >> 4] << 4) | DESKTOP[i & 15] for i in range(256)]}
    pairs = [0xCE if i == 1 else 17 * DESKTOP[i] for i in range(16)]
    t['gr_winx2'] = [v for i in range(256) for v in (pairs[i >> 4], pairs[i & 15])]
    t['gr_planebits'] = [(((i >> (plane + 4)) & 1) << 1) | ((i >> plane) & 1)
                         for plane in range(4) for i in range(256)]
    double = (0, 3, 12, 15)
    t['gr_planebitsx2'] = [double[v] for v in t['gr_planebits']]
    for name, scale in (('gr_winplanes', 1), ('gr_winplanesx2', 2)):
        values = []
        for plane in range(4):
            for pair in range(256):
                bits = 0
                for ink in (pair >> 4, pair & 15):
                    for pixel in ((12, 14) if scale == 2 and ink == 1 else
                                  (DESKTOP[ink],) * scale):
                        bits = (bits << 1) | ((pixel >> plane) & 1)
                values.append(bits)
        t[name] = values
    for name, mapping, shift, factor in (('gr_monopairs', MONO, 1, 1),
                                        ('gr_monopairsx2', MONO, 2, 3),
                                        ('gr_cgapairs', CGA, 2, 1)):
        t[name] = [(mapping[i >> 4]*factor << shift) | mapping[i & 15]*factor
                   for i in range(256)]
    return t


def inkpairs(depth, vga, sx):
    """The bytes gr_inktables leaves in gr_inkpairs for one surface."""
    t = pair_tables()
    if vga:
        return bytes(t['gr_planebits' if sx == 1 else 'gr_planebitsx2'])
    if depth == 4:
        return bytes(t['gr_winplanes'] + t['gr_winmap'] if sx == 1 else
                     t['gr_winplanesx2'] + t['gr_winx2'])
    if depth == 2:
        return bytes(t['gr_cgapairs'])
    return bytes(t['gr_monopairs' if sx == 1 else 'gr_monopairsx2'] + t['gr_monopairs'])


def front_sprite(mode, scale, sy=1):
    """An intro sprite form of both poses, as the old generator emitted it
    (`gr_front_<mode><scale>`) - planar forms with each row repeated sy times,
    which is how gr_frontprepare lays them out in gr_apecache."""
    out = []
    for rows in poses():
        pixels = [[int(p, 16) for p in row] for row in rows]
        if mode in ('vga', 'planar'):
            if mode == 'planar':
                pixels = [[v for p in row for v in
                           ((12, 14) if scale == 2 and p == 1 else
                            (DESKTOP[p],)*scale)] for row in pixels]
            for plane in range(4):
                for row in pixels:
                    bits = ''.join(str((p >> plane) & 1) *
                                   (scale if mode == 'vga' else 1) for p in row)
                    out += [int(bits[i:i+8], 2) for i in range(0, len(bits), 8)] * sy
        else:
            for row in pixels:
                bits = ''
                for p in row:
                    if mode == 'mono':
                        bits += str(int(p != 0))*scale
                    elif mode == 'cga':
                        bits += format(CGA[p], '02b')
                    elif scale == 2 and p == 1:
                        bits += '11001110'  # desktop orange stipple
                    else:
                        bits += format(DESKTOP[p], '04b')*scale
                out += [int(bits[i:i+8], 2) for i in range(0, len(bits), 8)]
    return bytes(out)


def light_phases():
    """Five marching-light phases, each a set of lit (x, y) pixels."""
    phases = []
    for phase in range(5):
        lit = set()
        def spark(x, y):
            lit.update((x+dx, y+dy) for dx, dy in ((1, 0), (0, 1),
                                                (1, 1), (2, 1), (1, 2)))
        for x in range(phase*4, 253, 20):
            spark(x, 0)
            spark(252-x, 125)
        for y in range(phase*4+5, 123, 20):
            spark(0, y)
            spark(253, 125-y)
        phases.append(lit)
    return phases


def light_masks():
    """The 1bpp masks gr_frontprepare draws, 448 bytes a phase: top/bottom
    (256x3), then left/right (8x128). Sides include corners, so all four
    rectangles agree where they overlap."""
    out = []
    for lit in light_phases():
        for x, y, w, h in ((0, 0, 256, 3), (0, 125, 256, 3),
                            (0, 0, 8, 128), (248, 0, 8, 128)):
            for yy in range(y, y+h):
                bits = ''.join(str(int((xx, yy) in lit)) for xx in range(x, x+w))
                out += [int(bits[i:i+8], 2) for i in range(0, len(bits), 8)]
    return bytes(out)


def light_runs():
    """Per phase and side strip, the rows (first, count) whose lights changed
    from the previous phase; neighbouring crosses coalesce across gaps <= 2."""
    phases = light_phases()
    runs = []
    for phase, lit in enumerate(phases):
        changed = lit | phases[(phase-1) % 5]
        for x in (0, 248):
            rows = [y for y in range(3, 125)
                    if any((xx, y) in changed for xx in range(x, x+8))]
            side = []
            while rows:
                first = last = rows.pop(0)
                while rows and rows[0] <= last+2:
                    last = rows.pop(0)
                side.append((first, last-first+1))
            runs.append(side)
    return runs


def sun():
    """Circle, rays and smile in the BASIC's proportions, as 21 rows of 25."""
    art = [[0] * 25 for _ in range(21)]
    for ex, ey in ((12, 0), (0, 10), (9, 7), (6, 9), (11, 4)):
        for signx in (-1, 1):
            for signy in (-1, 1):
                steps = max(ex, ey)
                for n in range(steps + 1):
                    x = 12 + signx * round(ex * n / steps)
                    y = 10 + signy * round(ey * n / steps)
                    art[y][x] = 1
    for y in range(21):
        for x in range(25):
            if (x-12)**2 + (y-10)**2 <= 49:
                art[y][x] = 1
    for x, y in ((9,8),(15,8),(8,12),(9,13),(10,14),(11,14),
                 (12,14),(13,14),(14,14),(15,13),(16,12)):
        art[y][x] = 0
    return art


def generate():
    result = ['; Generated by tools/gorillas_art.py; see reference/README.md.',
              '; Sources only: the pair tables, poses, intro sprites and marquee',
              '; masks are derived from these at run time (SPEC.md 99.3, 99.4).']
    def emit(values, kind='db', per=16):
        for i in range(0, len(values), per):
            result.append('    ' + kind + ' ' + ','.join(str(v) for v in values[i:i+per]))
    for name, palette in (('gr_deskpal', DESKTOP), ('gr_monopal', MONO),
                          ('gr_cgapal', CGA)):
        result.append(name + ':')
        emit(palette)
    result.append('; Exact QBasic EGA colors in the VGA DAC\ngr_dac:')
    emit([c for value in EGA for c in ega_rgb6(value)], per=12)
    result.append('; 16x20 packed 4bpp gorilla. 0=transparent, 1/A/B=body/light/shadow.\ngr_ape:')
    assert len(APE) == 20 and all(len(row) == 16 for row in APE)
    emit(list(packed(APE)), per=8)
    # The right pose is this one mirrored, drawn and converted so (SPEC.md 99.5).
    result.append('; The left arm raised; gr_gorillapose and gr_mirrorpose mirror it.\ngr_apeleft:')
    emit(list(packed(poses()[0])), per=8)
    # Each row's 25 pixels as two words, the leftmost pixel in bit 15. (The
    # word table goes before the byte ones, so it stays word aligned.)
    result.append('; 25x21 sun mask, ray tips and facial cutouts: two words a row, MSB first.')
    result.append('gr_sunart:')
    words = []
    for row in sun():
        bits = int(''.join(map(str, row)) + '0'*7, 2)
        words += [bits >> 16, bits & 65535]
    emit(words, 'dw', 8)
    # Changed side-strip rows, phase by phase: left list, then right list,
    # (first row, count) pairs each ended by a zero byte.
    runs = light_runs()
    body, starts = [], []
    for phase in range(5):
        starts.append(len(body))
        for side in runs[phase*2:phase*2+2]:
            assert side and all(3 <= f < 125 and 0 < n < 125 for f, n in side)
            body += [v for run in side for v in run] + [0]
    assert len(body) < 256
    result.append('; Marquee side-strip repaints: per phase, left then right (first, count)')
    result.append('; row runs, each side ended by a zero byte.\ngr_lightidx:')
    emit(starts)
    result.append('gr_lightruns:')
    emit(body)
    return '\n'.join(result) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    output = ROOT / 'apps/gorillas/grart.inc'
    generated = generate()
    if args.check:
        if output.read_text() != generated:
            raise SystemExit('grart.inc is stale: run python3 tools/gorillas_art.py')
        print('gorillas_art: committed artwork and palette tables match')
    else:
        output.write_text(generated)
        print('gorillas_art: wrote', output)


if __name__ == '__main__':
    main()
