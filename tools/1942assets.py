#!/usr/bin/env python3
"""Build private 1942 graphics from a user-supplied NROM-256 cartridge.

No ROM data is embedded in this source. Output stays under build/.
"""
import argparse
import json
from pathlib import Path


def convert(rom, palette):
    if len(rom) < 16 or rom[:4] != b'NES\x1a':
        raise ValueError('expected an iNES/NES 2.0 cartridge')
    if rom[4:6] != bytes((2, 1)) or rom[6] & 0xfe or rom[7] & 0xf0:
        raise ValueError('1942 requires mapper 0, 32KB PRG, 8KB CHR, no trainer')
    if (rom[7] & 12) not in (0, 8):
        raise ValueError('unsupported cartridge header')
    if (rom[7] & 12) == 8 and (rom[8] or rom[9]):
        raise ValueError('unsupported NES 2.0 mapper or extended ROM size')
    if len(rom) != 40976:
        raise ValueError('expected exactly 40976 cartridge bytes')
    rgb = palette['rgb']
    if len(rgb) != 16 or any(len(c) != 3 or any(type(v) is not int or not 0 <= v <= 255 for v in c) for c in rgb):
        raise ValueError('rgb must contain sixteen RGB byte triples')
    if rgb[0] != [0,0,0]:
        raise ValueError('RGB entry 0 must be black for the VGA borders')
    profiles = palette['cga']
    if len(profiles) != 3:
        raise ValueError('exactly three CGA profiles are required')
    for p in profiles:
        if type(p['register']) is not int or not 0 <= p['register'] < 64:
            raise ValueError('CGA register must be 0..63')
        if len(p['map']) != 16 or any(type(v) is not int or not 0 <= v < 4 for v in p['map']):
            raise ValueError('CGA map must contain sixteen 2-bit indices')
    chrrom = rom[32784:]
    def tile(t):
        return [[((chrrom[t*16+y] >> (7-x)) & 1) | (((chrrom[t*16+y+8] >> (7-x)) & 1) << 1) for x in range(8)] for y in range(8)]
    out = ['; Generated from the local cartridge; do not commit.']
    def emit(name, data):
        out.append(name + ':')
        for i in range(0, len(data), 16):
            out.append('    db ' + ','.join(str(n) for n in data[i:i+16]))
    # ASCII-indexed 8-row font, using the cartridge's actual tile strokes.
    font = []
    for c in range(32, 91):
        t = c-48 if 48 <= c <= 57 else c-65+10 if 65 <= c <= 90 else None
        rows = tile(t) if t is not None else [[0]*8 for _ in range(8)]
        if c == 45: rows[3] = [0,1,1,1,1,1,1,0]
        if c == 58: rows[2][3] = rows[5][3] = 1
        font.extend(sum((1 << (7-x)) for x,v in enumerate(row) if v) for row in rows)
    emit('n_font', font)
    # Native P-38 silhouette. Terrain and lettering use cartridge pixels.
    aircraft = [
        '       44       ', '   9   44   9   ', '  999  44  999  ',
        '  949 4444 949  ', '  949 4444 949  ', '9999994444999999',
        '9999944444499999', '  999 4444 999  ', '  999 4444 999  ',
        '  999  44  999  ', '  999  44  999  ', '  999  44  999  ',
        '  999  44  999  ', '  999999999999  ', '   99  44  99   ', '       44       ']
    def sprite(name, rows):
        w, h = len(rows[0]), len(rows)
        assert all(len(r) == w for r in rows)
        emit(name, [w,h] + [v for row in rows for v in row])
    plane = [[int(c,16) if c != ' ' else 0 for c in row] for row in aircraft]
    sprite('n_playerart', plane)
    sprite('n_rollart', [[0 if x < 5 or x > 10 else plane[y][x] or 8 for x in range(16)] for y in range(16)])
    enemy = [[{9:10,4:6}.get(v,v) for v in row] for row in plane[::-1]]
    sprite('n_enemyart', enemy)
    sprite('n_eliteart', [[{10:5,6:4}.get(v,v) for v in row] for row in enemy])
    sprite('n_bossart', [[v for v in row for _ in range(2)] for row in enemy for _ in range(2)])
    sprite('n_pickart', [[6 if x in (0,7) or y in (0,7) else 4 if x in (2,3) or (y in (2,4) and x < 6) else 5 for x in range(8)] for y in range(8)])
    # Four original explosion tiles in their native two-by-two arrangement.
    tiles = [tile(t) for t in (0xd0,0xd1,0xd2,0xd3)]
    sprite('n_blastart', [[(0,4,6,15)[tiles[y//8*2+x//8][y%8][x%8]] for x in range(16)] for y in range(16)])
    land = tile(0x1e5)
    sprite('n_islandart', [[(10,13,11,12)[land[y%8][x%8]] if (x-15)**2*2+(y-15)**2 < 380 else 0 for x in range(32)] for y in range(32)])
    emit('n_dac', [v*63//255 for c in rgb for v in c])
    emit('n_cgaregs', [p['register'] for p in profiles])
    emit('n_cgamaps', [v for p in profiles for v in p['map']])
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('rom', type=Path)
    ap.add_argument('--palette', type=Path, default=Path('apps/1942/palette.json'))
    ap.add_argument('-o', type=Path, required=True)
    a = ap.parse_args()
    try:
        result = convert(a.rom.read_bytes(), json.loads(a.palette.read_text()))
    except (OSError, ValueError, KeyError, TypeError) as e:
        ap.error(str(e))
    a.o.parent.mkdir(parents=True, exist_ok=True)
    if not a.o.exists() or a.o.read_text() != result:
        a.o.write_text(result)

if __name__ == '__main__':
    main()
