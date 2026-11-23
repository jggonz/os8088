#!/usr/bin/env python3
"""Borat face art. `draft` is development-only; `build` reads committed text.

Sprites reuse Breadboard XT's packed nibble format and WIN16 font converter.
BA directory offsets are unsigned 16-bit (draw.inc bx_art_at loads a word).
The original eight-cell master is a 4-column x 2-row sheet, row-major;
seven expression states stay in art part0; the eighth blink goes in sound part1.
No PNG/Pillow at build.
"""
import argparse
from pathlib import Path
import struct
import re
import sys
import os88bbart as bb

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'apps/boratype/art'
SIZES = [('big', 96, 120), ('small', 40, 48), ('mono', 40, 48), ('cga', 32, 40)]
FONTS = [str(ART / 'label'), 'dlg08', str(ART / 'keytype')]
# CGA palette 1, intensity on: black, cyan, magenta, white.
CGA = [0, 11, 5, 15]


def sources(path=None, blink=False):
    path = Path(path or ART / ('blink.txt' if blink else 'sprites.txt'))
    out = []
    lines = iter(path.read_text().splitlines())
    for line in lines:
        if not line or line.startswith('#'):
            continue
        tag, name, sw, sh = line.split()
        w, h = int(sw), int(sh)
        if tag != 'sprite' or not 0 < w < 256 or not 0 < h < 256:
            raise ValueError('bad sprite header')
        rows = [next(lines) for _ in range(h)]
        if any(len(r) != w or set(r) - set(bb.CHARS) for r in rows):
            raise ValueError(f'{name}: bad dimensions/palette')
        if any('.' in r for r in rows): raise ValueError(f'{name}: opaque portrait has transparency')
        inks = set(''.join(rows))
        if name.startswith('mono') and inks - {bb.CHARS[0], bb.CHARS[15]}: raise ValueError('mono ink')
        if name.startswith('cga') and inks - {bb.CHARS[k] for k in CGA}: raise ValueError('CGA ink')
        out.append((name, w, h, [[bb.CHARS.index(c) for c in r] for r in rows]))
    expected = [(f'{name}{e}', w, h) for name, w, h in SIZES for e in ([7] if blink else range(7))]
    if [(n, w, h) for n, w, h, _ in out] != expected:
        raise ValueError('source must contain all expected sprites in header order')
    return out


def build(path):
    ids = dict((n, int(v)) for n, v in re.findall(r'^#define (BT_\w+) (\d+)$', (ROOT / 'apps/boratype/art.h').read_text(), re.M))
    expected = dict(BT_ART_BIG=0, BT_ART_SMALL=7, BT_ART_MONO=14, BT_ART_CGA=21, BT_F_TEXT=28, BT_F_SMALL=29, BT_F_TITLE=30, BT_ART_COUNT=31)
    if any(ids.get(n) != v for n, v in expected.items()): raise ValueError('art.h directory differs from converter')
    sprites = sources()
    blobs = [bb.pack_sprite(w, h, rows) for _, w, h, rows in sprites]
    blobs += [bb.pack_font(f) for f in FONTS]
    out = bytearray(b'BA' + struct.pack('<H', len(blobs)) + bytes(2 * len(blobs)))
    for i, blob in enumerate(blobs):
        if len(out) & 1:
            out.append(0)
        struct.pack_into('<H', out, 4 + 2 * i, len(out))
        out += blob
    if len(out) > 65535:
        raise ValueError(f'art part {len(out)} exceeds unsigned word offsets')
    # Validate every packed record against the authoritative source length.
    for i, blob in enumerate(blobs):
        off, = struct.unpack_from('<H', out, 4 + 2 * i)
        if bytes(out[off:off + len(blob)]) != blob:
            raise ValueError('directory validation failed')
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(out)
    print(f'os88btart: {len(out)} bytes, 28 sprites, 3 bitmap fonts -> {path}')


def draft(master, path, blink_path):
    from PIL import Image, ImageOps
    im = Image.open(master).convert('RGB')
    if im.width < 400 or im.height < 200:
        raise ValueError('master is smaller than a four-column expression sheet')
    cw, ch = im.width / 4, im.height / 2
    out = ['# Selected original image master; generated once by os88btart draft.',
           '# Palette kbgcrmnldBGCR.yw; . is transparent index13.']
    blink_out = ['# Eighth generated cell: attentive idle blink. Same palette and converter as sprites.txt.']
    for name, w, h in SIZES:
        for e in range(8):
            cell = im.crop(tuple(round(v) for v in ((e % 4) * cw, (e // 4) * ch, (e % 4 + 1) * cw, (e // 4 + 1) * ch)))
            # A little border is retained: same anchor and framing in every state.
            cell = ImageOps.fit(cell, (w, h), Image.Resampling.LANCZOS)
            background = [r < 35 and g < 35 and b > r * 1.8 and b > g * 1.8 for r, g, b in [cell.getpixel((x, y)) for y in range(h) for x in range(w)]]
            if name in ('big', 'small'):
                pal = Image.new('P', (1, 1))
                pal.putpalette([v for i in range(256) for v in bb.EGA[i if i < 16 and i != 13 else 1]])
                cell = cell.quantize(palette=pal, dither=Image.Dither.FLOYDSTEINBERG).convert('RGB')
            rows = []
            palette = [0, 15] if name == 'mono' else CGA if name == 'cga' else [i for i in range(16) if i != 13]
            for y in range(h):
                row = ''
                for x in range(w):
                    rgb = cell.getpixel((x, y))
                    if name == 'mono':
                        # Explicit ordered threshold, distinct from core colour dithering.
                        lum = (rgb[0] * 3 + rgb[1] * 6 + rgb[2]) // 10
                        threshold = [32, 160, 224, 96][(y % 2) * 2 + x % 2]
                        k = 15 if lum > threshold else 0
                    else:
                        k = min(palette, key=lambda i: sum((rgb[j] - bb.EGA[i][j]) ** 2 for j in range(3)))
                    if name in ('big', 'small') and background[y * w + x]: k = 1
                    row += bb.CHARS[k]
                rows.append(row)
            (blink_out if e == 7 else out).extend([f'sprite {name}{e} {w} {h}', *rows, ''])
    Path(path).write_text('\n'.join(out) + '\n')
    Path(blink_path).write_text('\n'.join(blink_out) + '\n')
    sources(path); sources(blink_path, blink=True)
    print(f'os88btart: wrote exact-size sources {path} and {blink_path}')



def title_record():
    """A 432x24 1-bit logo as 27 adjacent 16-pixel shared-font glyphs."""
    rows = (ART / 'masthead.mask').read_text().splitlines()
    if len(rows) != 24 or any(len(row) != 432 or set(row) - {'.', '#'} for row in rows):
        raise ValueError('masthead must be a 432x24 binary mask')
    count = 27
    data = bytearray([24, 24, 65, count] + [16] * count + [0] * (2 * count))
    for i in range(count):
        struct.pack_into('<H', data, 4 + count + 2 * i, len(data))
        data.extend([0, 24])
        for row in rows:
            bits = row[16*i:16*i+16].replace('.', '0').replace('#', '1')
            data.extend(int(bits, 2).to_bytes(2, 'big'))
    return data


def build_sound(path, music, sfx):
    # Standard-library-only shared score compiler; no audio or image renderer.
    import os88gkmus as mus
    ids = dict((n, int(v)) for n, v in re.findall(r'^#define (BT_\w+) (\d+)$', (ROOT / 'apps/boratype/art.h').read_text(), re.M))
    expected = dict(BT_ART_BLINK_BIG=258, BT_ART_BLINK_SMALL=259, BT_ART_BLINK_MONO=260, BT_ART_BLINK_CGA=261, BT_F_LARGE=262)
    if any(ids.get(n) != v for n, v in expected.items()): raise ValueError('blink IDs differ from sound directory')
    try:
        records = {0: mus.compile_music(str(music)), 1: mus.compile_sfx(str(sfx))}
    except mus.GkmError as e:
        raise ValueError(str(e)) from e
    for i, (_, w, h, rows) in enumerate(sources(blink=True), 2):
        records[i] = bb.pack_sprite(w, h, rows)
    # The same committed font source syntax and packer as every other face.
    # An absolute source stem is accepted by read_font's path join.
    records[6] = bb.pack_font(str(ART / 'large'))
    records[7] = title_record()
    badges = (ART / 'badges.mask').read_text().splitlines()
    if len(badges) != 120 or any(len(row) != 24 or set(row) - set(bb.CHARS) for row in badges):
        raise ValueError('badges must be five 24x24 palette sprites')
    for i in range(5):
        pixels = [[bb.CHARS.index(c) for c in row] for row in badges[i*24:(i+1)*24]]
        records[8+i] = bb.pack_sprite(24, 24, pixels)
    # One small opaque meter update occupies a byte-aligned 16x10 rectangle.
    # Invisible pixels expose the purple footer through the shared sprite key.
    for phase, bars in enumerate(((0,0,0), (2,4,7), (5,7,4), (7,4,2), (3,2,5))):
        pixels = [[bb.KEY] * 16 for _ in range(10)]
        for y, row in enumerate(("..####","..####","..#..#","..#..#","..#..#","###.##","###.##","##....")):
            for x, pixel in enumerate(row):
                if pixel == '#':
                    pixels[y+1][x] = 14 if phase else 7
        for bar, height in enumerate(bars):
            for y in range(10-height, 10):
                x = 8+3*bar
                pixels[y][x:x+2] = [10,10]
        records[13+phase] = bb.pack_sprite(16,10,pixels)
    data = mus.art_part(records)
    if len(data) > 16384:
        raise ValueError(f'sound/blink/font bank {len(data)} exceeds16KB budget')
    for i, record in records.items():
        off, = struct.unpack_from('<H', data, 4 + 2 * i)
        if off & 1 or data[off:off + len(record)] != record:
            raise ValueError('sound/blink directory validation failed')
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(data)
    print(f'os88btart: {len(data)} bytes, music, effects, blink, display type, masthead and badges -> {path}')


def preview(path):
    from PIL import Image
    ss = sources(); blink = sources(blink=True)
    im = Image.new('RGB', (8 * 100, 272), bb.EGA[1])
    yy = 0
    for name, w, h in SIZES:
        for e in range(8):
            _, _, _, rows = blink[SIZES.index((name, w, h))] if e == 7 else ss[SIZES.index((name, w, h)) * 7 + e]
            cell = Image.new('RGB', (w, h))
            cell.putdata([bb.EGA[k] for row in rows for k in row])
            im.paste(cell, (e * 100, yy))
        yy += h + 4
    im.save(path)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sp = p.add_subparsers(dest='cmd', required=True)
    q = sp.add_parser('build'); q.add_argument('-o', required=True)
    q = sp.add_parser('draft'); q.add_argument('master'); q.add_argument('-o', default=str(ART / 'sprites.txt')); q.add_argument('--blink-output', default=str(ART / 'blink.txt'))
    q = sp.add_parser('build-sound'); q.add_argument('-o', required=True); q.add_argument('--music', default=str(ART / 'music.txt')); q.add_argument('--sfx', default=str(ART / 'sfx.txt'))
    q = sp.add_parser('preview'); q.add_argument('-o', required=True)
    sp.add_parser('check')
    a = p.parse_args()
    try:
        if a.cmd == 'build': build(a.o)
        elif a.cmd == 'draft': draft(a.master, a.o, a.blink_output)
        elif a.cmd == 'build-sound': build_sound(a.o, a.music, a.sfx)
        elif a.cmd == 'preview': preview(a.o)
        else:
            ss = sources() + sources(blink=True)
            for n, w, h, rows in ss:
                if n.startswith('mono') and any(k not in [0, 15] for r in rows for k in r): raise ValueError('mono ink')
                if n.startswith('cga') and any(k not in CGA for r in rows for k in r): raise ValueError('CGA ink')
            print('os88btart: source geometry and palette pass')
    except (ValueError, OSError, StopIteration) as e:
        sys.exit(f'os88btart: {e}')

if __name__ == '__main__': main()
