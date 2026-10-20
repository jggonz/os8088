#!/usr/bin/env python3
"""WireFleet's generated ship source -> reproducible opaque 24x24 tiles.

The runtime embeds VGA 4bpp and explicit 1bpp forms. Each ship is split into
board cells in both orientations; water and shot marks use the same tiles.
No image generation service is used by the build. --check detects drift.
"""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'apps/wirefleet/art/ships-source.png'
OUTPUT = ROOT / 'apps/wirefleet/art/tiles.inc'
LENGTHS = (5, 4, 3, 3, 2)
CELL = 24
EGA = ((0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
       (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
       (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
       (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255))


def water():
    tile = Image.new('L', (CELL, CELL), 1)
    d = ImageDraw.Draw(tile)
    d.line((0, 0, 23, 0), fill=11)
    d.line((0, 0, 0, 23), fill=11)
    d.point(((6, 13), (7, 12), (8, 13), (9, 13), (10, 12)), fill=3)
    return tile


def bands(source):
    """The five ships' row ranges, top to bottom. The atlas is NOT five equal
    strips: the carrier overhangs the second fifth, and cutting at fifths
    gave every later ship the previous one's keel as a grey line."""
    full = [sum(1 for r, g, b in source.crop((0, y, source.width, y + 1)).getdata()
                if not (r > g + 60 and b > g + 60)) for y in range(source.height)]
    runs, start = [], None
    for y, n in enumerate(full + [0]):
        if n and start is None:
            start = y
        elif not n and start is not None:
            runs.append((sum(full[start:y]), start, y))
            start = None
    picked = sorted(sorted(runs, reverse=True)[:5], key=lambda r: r[1])
    if len(picked) != 5:
        raise ValueError('expected five ships, found %d' % len(picked))
    return [(a, b) for _, a, b in picked]


def ships():
    source = Image.open(SOURCE).convert('RGB')
    result = []
    for i, (length, (top, bottom)) in enumerate(zip(LENGTHS, bands(source))):
        strip = source.crop((0, top, source.width, bottom))
        # This source has a flat magenta matte, including JPEG-like edge
        # variation. Key by chroma, never a coordinate-specific silhouette.
        mask = Image.new('L', strip.size)
        mask.putdata([255 if not (r > g + 60 and b > g + 60) else 0
                      for r, g, b in strip.getdata()])
        bounds = mask.getbbox()
        if bounds is None:
            raise ValueError('empty ship row %d' % i)
        # The HULL's rows, not the mast's: a mast and its yard are a few
        # pixels wide, and squashed into 18 rows they became a grey line
        # along the whole ship. A row belongs to the hull when it is at
        # least 40% as wide as the widest row.
        widths = [sum(1 for x in range(strip.width) if mask.getpixel((x, y)))
                  for y in range(strip.height)]
        rows = [y for y, w in enumerate(widths) if w >= 0.4 * max(widths)]
        bounds = (bounds[0], rows[0], bounds[2], rows[-1] + 1)
        size = (length * CELL - 6, 18)
        # Area-averaged, not LANCZOS: ringing on the black outline made
        # bright fringes at this reduction.
        strip = strip.crop(bounds).resize(size, Image.Resampling.BOX)
        mask = mask.crop(bounds).resize(size, Image.Resampling.BOX)
        mask = mask.point(lambda v: 255 if v >= 128 else 0)
        indexed = Image.new('L', strip.size)
        palette = (0, 8, 7, 15)
        indexed.putdata([palette[min(3, (r * 3 + g * 6 + b) // 640)]
                         for r, g, b in strip.getdata()])
        hull = Image.new('L', (length * CELL, CELL), 1)
        hull.paste(indexed, (3, 3), mask)
        result.append(hull)
    return result


def tiles():
    result = [water()]
    for kind in ('miss', 'hit', 'sunk'):
        tile = water()
        d = ImageDraw.Draw(tile)
        if kind == 'miss':
            d.ellipse((9, 9, 14, 14), fill=15)
        elif kind == 'hit':
            d.line((7, 7, 17, 17), fill=12, width=3)
            d.line((17, 7, 7, 17), fill=12, width=3)
        else:
            for offset in (9, 14):
                d.line((offset, 6, offset, 18), fill=15, width=2)
                d.line((6, offset, 18, offset), fill=15, width=2)
        result.append(tile)
    hulls = ships()
    for vertical in (False, True):
        for hull, length in zip(hulls, LENGTHS):
            if vertical:
                hull = hull.transpose(Image.Transpose.ROTATE_270)
            for segment in range(length):
                box = ((0, segment * CELL, CELL, (segment + 1) * CELL)
                       if vertical else
                       (segment * CELL, 0, (segment + 1) * CELL, CELL))
                tile = hull.crop(box)
                # Cell borders belong to the opaque tile, not a second pass.
                d = ImageDraw.Draw(tile)
                d.line((0, 0, 23, 0), fill=11)
                d.line((0, 0, 0, 23), fill=11)
                result.append(tile)
    for original in result[4:].copy():
        hit = original.copy()
        d = ImageDraw.Draw(hit)
        d.line((7, 7, 17, 17), fill=12, width=3)
        d.line((17, 7, 7, 17), fill=12, width=3)
        result.append(hit)
    return result


def packed(tile, mono=False):
    values = list(tile.getdata())
    if not mono:
        return bytes(values[i] * 16 + values[i + 1]
                     for i in range(0, len(values), 2))
    out = bytearray()
    for y in range(CELL):
        for x in range(0, CELL, 8):
            byte = 0
            for bit in range(8):
                c = values[y * CELL + x + bit]
                # Water black; grid and marks white; gray ship faces use
                # fixed stipple, preserving a dark silhouette at the outline.
                px = x + bit
                halo = any(values[ny * CELL + nx] == 12
                           for ny in range(max(0,y-1),min(CELL,y+2))
                           for nx in range(max(0,px-1),min(CELL,px+2)))
                lit = c != 12 and (halo or c in (7, 11, 15)
                                    or (c == 8 and (px + y) % 2 == 0))
                byte = byte * 2 + lit
            out.append(byte)
    return bytes(out)


def emit(label, payload):
    return label + ':\n' + ''.join(
        '    db ' + ','.join('0x%02x' % v for v in payload[i:i + 24]) + '\n'
        for i in range(0, len(payload), 24))


def cga(tile):
    values = tile.tobytes()
    out = bytearray()
    for y in range(CELL):
        for x in range(0, CELL, 8):
            byte = 0
            for step in range(0, 8, 2):
                c = values[y * CELL + x + step]
                ink = (1 if c in (3, 11) else 2 if c == 12 else
                       3 if c in (7, 14, 15) or (c == 8 and (x//2+step//2+y) % 2) else 0)
                byte = byte * 4 + ink
            out.append(byte)
    return bytes(out)


STORED = 38                     # water, three marks, 34 ship segments
HIT0 = STORED                   # tiles 38..71: a ship segment, hit


def overlay(art, form):
    """One AND table and one OR table, in `form`'s layout, such that EVERY
    hit tile is (its ship segment & AND) | OR. Built from the X itself and
    then CHECKED against all 34, so a converter change that breaks the
    identity fails here rather than drawing a different picture."""
    water = art[0]
    xmark = art[2]                  # water with the hit X on it
    base, mark = form(water), form(xmark)
    andt, ort = bytearray(len(base)), bytearray(len(base))
    for i, (b, m) in enumerate(zip(base, mark)):
        # Pixels the X changed are forced to the mark's bits; every other
        # pixel keeps the segment's.
        changed = b ^ m
        if form is packed:
            # a nibble is a pixel: mask whole nibbles the X touches
            mask = (0xf0 if changed & 0xf0 else 0) | (0x0f if changed & 0x0f else 0)
        elif form is cga:
            mask = 0
            for shift in (6, 4, 2, 0):
                if changed & (3 << shift):
                    mask |= 3 << shift
        else:
            mask = changed
        andt[i] = 255 & ~mask
        ort[i] = m & mask
    if form is mono:
        # packed()'s mono rule: an X pixel is dark and a pixel beside one is
        # lit, WHATEVER is under it - so the stamp is the X and its 1-pixel
        # halo, not what changed on water (an X pixel is dark there too).
        v = list(xmark.tobytes())
        x = {(px, py) for py in range(CELL) for px in range(CELL)
             if v[py * CELL + px] == 12}
        halo = {(px + dx, py + dy) for px, py in x for dx in (-1, 0, 1)
                for dy in (-1, 0, 1)} & {(a, b) for a in range(CELL) for b in range(CELL)}
        andt, ort = bytearray(len(base)), bytearray(len(base))
        for py in range(CELL):
            for px in range(CELL):
                i, bit = py * 3 + px // 8, 0x80 >> (px % 8)
                if (px, py) in halo:
                    andt[i] |= 0
                    if (px, py) not in x:
                        ort[i] |= bit
                else:
                    andt[i] |= bit
    return bytes(andt), bytes(ort)


def mono(tile):
    return packed(tile, True)


def build():
    art = tiles()
    assert len(art) == 72
    out = ('; Generated by tools/wirefleet_assets.py; do not edit.\n'
           'WF_TILE_COUNT equ 72\nWF_TILE_STORED equ %d\nWF_TILE_HIT0 equ %d\n'
           'WF_TILE_HITBASE equ %d\n'
           'WF_TILE_COLOR equ 288\nWF_TILE_MONO equ 72\n' % (STORED, HIT0, HIT0 - 4))
    for name, form in (('color', packed), ('mono', mono), ('cga', cga)):
        andt, ort = overlay(art, form)
        for k in range(HIT0, 72):
            seg = form(art[k - (HIT0 - 4)])
            want = form(art[k])
            got = bytes((a & n) | o for a, n, o in zip(seg, andt, ort))
            if got != want:
                raise ValueError('%s: hit tile %d is not its segment stamped' % (name, k))
        out += emit('wf_tiles_' + name, b''.join(form(t) for t in art[:STORED]))
        out += emit('wf_hitand_' + name, andt) + emit('wf_hitor_' + name, ort)
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--check', action='store_true')
    args = p.parse_args()
    data = build()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != data:
            p.error('stale tiles.inc; run python3 tools/wirefleet_assets.py')
    else:
        OUTPUT.write_text(data)
    # Preview is an inspection artifact, never a runtime dependency.
    preview = Image.new('RGB', (24 * 18, 96))
    for i, tile in enumerate(tiles()):
        rgb = Image.new('RGB', tile.size)
        rgb.putdata([EGA[v] for v in tile.getdata()])
        preview.paste(rgb, ((i % 18) * 24, (i // 18) * 24))
    (ROOT / 'build').mkdir(exist_ok=True)
    preview.resize((864, 192), Image.Resampling.NEAREST).save(
        ROOT / 'build/wirefleet-tiles.png')
    print('wirefleet_assets: 72 tiles, %d stored, hits stamped' % STORED)


if __name__ == '__main__':
    main()
