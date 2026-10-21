#!/usr/bin/env python3
"""WireFleet's generated effects atlas -> reproducible 24x24 cell STAMPS.

SPEC.md 114.11. A stamp is drawn over whatever tile a board cell already
shows, as `(pixel AND a) OR o` per byte - the hit X's mechanism (114.10.2),
generalised - so one table pair per form serves every face: packed 4bpp
(VGA/EGA, and the planar path spreads it on the fly), 1bpp (Hercules and
windowed CGA) and 2bpp (the CGA 320x200 bracket).

The tables are not in WIREFLET.O88's image: they are its lazy part
WFLETFX.GFX (SPEC.md 20.12), assembled from the fx.inc written here. No
image generation service is used by the build; --check detects drift.
"""
import argparse
from collections import Counter
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'apps/wirefleet/art/fx-source.png'
OUTPUT = ROOT / 'apps/wirefleet/art/fx.inc'
CELL = 24
CLEAR = 255
EGA = ((0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
       (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
       (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
       (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255))
# The atlas is 4 x 3 (fx-source.png, FX-PROMPT.txt): row 0 missile right,
# missile up-right, launch flash, smoke; row 1 four explosion frames; row 2
# four splash frames. Stamp IDs are 1-based; 0 is "no stamp" in the cell.
NAMES = ('missile E', 'missile NE', 'missile SE', 'missile W', 'missile NW',
         'missile SW', 'launch flash', 'smoke', 'explosion 1', 'explosion 2',
         'explosion 3', 'explosion 4', 'splash 1', 'splash 2', 'splash 3',
         'splash 4')
COUNT = len(NAMES)


def keyed(rgb):
    r, g, b = rgb
    return r > g + 60 and b > g + 60          # the flat magenta matte


_near = {}


def nearest(rgb):
    hit = _near.get(rgb)
    if hit is None:
        hit = min(range(16), key=lambda i: sum((a - b) ** 2
                                               for a, b in zip(rgb, EGA[i])))
        _near[rgb] = hit
    return hit


def frame(atlas, row, col):
    w, h = atlas.size
    cell = atlas.crop((col * w // 4, row * h // 3,
                       (col + 1) * w // 4, (row + 1) * h // 3))
    mask = Image.new('L', cell.size)
    mask.putdata([0 if keyed(p) else 255 for p in cell.get_flattened_data()])
    return cell.crop(mask.getbbox())


def reduce(art, scale, box=22):
    """Area-sample `art` into a CELL x CELL indexed stamp: a pixel is opaque
    when at least 45% of its block is, and takes the block's MOST COMMON
    palette colour - a mode, not an average, so a fireball stays yellow, red
    and white instead of becoming a brown smear."""
    w = max(1, min(box, round(art.width * scale)))
    h = max(1, min(box, round(art.height * scale)))
    px = art.load()
    out = [CLEAR] * (CELL * CELL)
    ox, oy = (CELL - w) // 2, (CELL - h) // 2
    for y in range(h):
        y0, y1 = y * art.height // h, max(y * art.height // h + 1, (y + 1) * art.height // h)
        for x in range(w):
            x0, x1 = x * art.width // w, max(x * art.width // w + 1, (x + 1) * art.width // w)
            votes, total = Counter(), 0
            for sy in range(y0, y1):
                for sx in range(x0, x1):
                    total += 1
                    p = px[sx, sy]
                    if not keyed(p):
                        votes[nearest(p)] += 1
            if sum(votes.values()) * 100 >= 45 * total:
                out[(oy + y) * CELL + ox + x] = votes.most_common(1)[0][0]
    return out


def flip(stamp, horizontal, vertical):
    out = [CLEAR] * (CELL * CELL)
    for y in range(CELL):
        for x in range(CELL):
            sx = CELL - 1 - x if horizontal else x
            sy = CELL - 1 - y if vertical else y
            out[y * CELL + x] = stamp[sy * CELL + sx]
    return out


def stamps():
    atlas = Image.open(SOURCE).convert('RGB')
    row0 = [frame(atlas, 0, c) for c in range(4)]
    east = reduce(row0[0], 22 / row0[0].width)
    ne = reduce(row0[1], 22 / max(row0[1].size))
    flash = reduce(row0[2], 22 / max(row0[2].size))
    smoke = reduce(row0[3], 15 / max(row0[3].size), 15)
    result = [east, ne, flip(ne, False, True),          # E, NE, SE
              flip(east, True, False), flip(ne, True, False),
              flip(ne, True, True), flash, smoke]       # W, NW, SW
    for row in (1, 2):
        frames = [frame(atlas, row, c) for c in range(4)]
        # One scale for the row, so the frames keep their RELATIVE sizes: an
        # explosion that grows has to be seen growing.
        scale = 23 / max(max(f.size) for f in frames)
        result += [reduce(f, scale, 23) for f in frames]
    assert len(result) == COUNT
    return result


def bright(c):
    return c in (7, 9, 10, 11, 12, 13, 14, 15)


def packed(stamp):
    andt, ort = bytearray(), bytearray()
    for i in range(0, CELL * CELL, 2):
        a = o = 0
        for p in stamp[i:i + 2]:
            a, o = a << 4, o << 4
            if p == CLEAR:
                a |= 15
            else:
                o |= p
        andt.append(a)
        ort.append(o)
    return bytes(andt), bytes(ort)


def halo(stamp):
    """The clear pixels touching the effect, which the 1bpp and 2bpp forms
    paint BLACK: over a ship's grey stipple a white burst is otherwise white
    on white. The hit X's mono rule (tools/wirefleet_assets.py) the other
    way round - there the X is dark and its ring lit."""
    ring = set()
    for y in range(CELL):
        for x in range(CELL):
            if stamp[y * CELL + x] != CLEAR:
                continue
            if any(stamp[ny * CELL + nx] != CLEAR
                   for ny in range(max(0, y - 1), min(CELL, y + 2))
                   for nx in range(max(0, x - 1), min(CELL, x + 2))):
                ring.add((x, y))
    return ring


def mono(stamp):
    """Water is black and marks white on the 1bpp face (114.2). An effect
    pixel is lit when its colour is bright; dark grey is a fixed stipple so
    smoke still reads over black water; black stays black, the outline -
    and so does the halo around it."""
    ring = halo(stamp)
    andt, ort = bytearray(), bytearray()
    for y in range(CELL):
        for x0 in range(0, CELL, 8):
            a = o = 0
            for x in range(x0, x0 + 8):
                p = stamp[y * CELL + x]
                a, o = a << 1, o << 1
                if p == CLEAR and (x, y) not in ring:
                    a |= 1
                elif bright(p) or (p == 8 and (x + y) % 2 == 0):
                    o |= 1
            andt.append(a)
            ort.append(o)
    return bytes(andt), bytes(ort)


def cgaink(p, x, y):
    # Palette 1, bright (114.8): black, cyan, magenta, white. Fire is
    # magenta - the impact colour - water is cyan, light is white.
    if p in (1, 2, 3, 9, 10, 11):
        return 1
    if p in (4, 5, 6, 12, 13):
        return 2
    if p in (7, 14, 15):
        return 3
    if p == 8:
        return 3 if (x // 2 + y) % 2 else 0
    return 0


def cga(stamp):
    """Packed 2bpp, 12 pixels across: the bracket's cells are 12 wide, so a
    row is three bytes of four pixels, every other source column - exactly
    how tools/wirefleet_assets.py's cga() samples a tile. The halo is
    black here too."""
    ring = halo(stamp)
    andt, ort = bytearray(), bytearray()
    for y in range(CELL):
        for x0 in range(0, CELL, 8):
            a = o = 0
            for step in range(0, 8, 2):
                p = stamp[y * CELL + x0 + step]
                a, o = a << 2, o << 2
                if p == CLEAR and (x0 + step, y) not in ring:
                    a |= 3
                else:
                    o |= cgaink(p, x0 + step, y)
            andt.append(a)
            ort.append(o)
    return bytes(andt), bytes(ort)


def emit(label, payload):
    return label + ':\n' + ''.join(
        '    db ' + ','.join('0x%02x' % v for v in payload[i:i + 24]) + '\n'
        for i in range(0, len(payload), 24))


def build():
    art = stamps()
    sizes = {}
    body = ''
    offset = 0
    for name, form, size in (('color', packed, 288), ('mono', mono, 72),
                             ('cga', cga, 72)):
        sizes[name] = offset
        block = bytearray()
        for stamp in art:
            andt, ort = form(stamp)
            assert len(andt) == len(ort) == size
            block += andt + ort
        body += emit('wf_fx_' + name, block)
        offset += len(block)
    head = ('; Generated by tools/wirefleet_fx.py; do not edit. SPEC.md 114.11.\n'
            '; Stamp N (1-based) of a form is an AND table then an OR table,\n'
            '; each the form\'s tile size, at WF_FX_<form> + (N-1)*2*size.\n'
            'WF_FX_COUNT equ %d\n' % COUNT)
    for i, n in enumerate(NAMES):
        head += 'WF_FX_%s equ %d\n' % (n.upper().replace(' ', '_'), i + 1)
    head += ('WF_FX_COLOR equ %d\nWF_FX_MONO equ %d\nWF_FX_CGA equ %d\n'
             'WF_FX_BYTES equ %d\n' % (sizes['color'], sizes['mono'],
                                       sizes['cga'], offset))
    return head + '%ifdef WF_FX_DATA\n' + body + '%endif\n', art


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--check', action='store_true')
    args = p.parse_args()
    data, art = build()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != data:
            p.error('stale fx.inc; run python3 tools/wirefleet_fx.py')
    else:
        OUTPUT.write_text(data)
    # Preview (an inspection artifact, never a runtime dependency): each
    # stamp over water and over a ship grey, VGA colours, at 4x.
    preview = Image.new('RGB', (COUNT * 26, 52), (40, 40, 40))
    for i, stamp in enumerate(art):
        for row, ground in enumerate((1, 7)):
            rgb = Image.new('RGB', (CELL, CELL))
            rgb.putdata([EGA[ground if v == CLEAR else v] for v in stamp])
            preview.paste(rgb, (i * 26 + 1, row * 26 + 1))
    (ROOT / 'build').mkdir(exist_ok=True)
    preview.resize((COUNT * 104, 208), Image.Resampling.NEAREST).save(
        ROOT / 'build/wirefleet-fx.png')
    print('wirefleet_fx: %d stamps, %d bytes in three forms' % (COUNT, len(data)))


if __name__ == '__main__':
    main()
