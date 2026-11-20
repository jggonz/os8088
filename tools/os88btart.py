#!/usr/bin/env python3
"""Borat face art. `draft` is development-only; `build` reads committed text.

Sprites reuse Breadboard XT's packed nibble format and WIN16 font converter.
BA directory offsets are unsigned 16-bit (draw.inc bx_art_at loads a word).
The original eight-cell master is a 4-column x 2-row sheet, row-major;
only the seven documented expression states are used. No PNG/Pillow at build.
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
FONTS = ['dlg10', 'dlg08', 'sys16']
# CGA palette 1, intensity on: black, cyan, magenta, white.
CGA = [0, 11, 5, 15]


def sources(path=None):
    path = Path(path or ART / 'sprites.txt')
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
    expected = [(f'{name}{e}', w, h) for name, w, h in SIZES for e in range(7)]
    if [(n, w, h) for n, w, h, _ in out] != expected:
        raise ValueError('source must contain all 28 sprites in header order')
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
    print(f'os88btart: {len(out)} bytes, 28 sprites, 3 shared WIN16 fonts -> {path}')


def draft(master, path):
    from PIL import Image, ImageOps
    im = Image.open(master).convert('RGB')
    if im.width < 400 or im.height < 200:
        raise ValueError('master is smaller than a four-column expression sheet')
    cw, ch = im.width / 4, im.height / 2
    out = ['# Selected original image master; generated once by os88btart draft.',
           '# Palette kbgcrmnldBGCR.yw; . is transparent index13.']
    for name, w, h in SIZES:
        for e in range(7):
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
            out += [f'sprite {name}{e} {w} {h}', *rows, '']
    Path(path).write_text('\n'.join(out) + '\n')
    sources(path)
    print(f'os88btart: wrote exact-size sources {path}')


def preview(path):
    from PIL import Image
    ss = sources()
    im = Image.new('RGB', (7 * 100, 272), bb.EGA[1])
    yy = 0
    for name, w, h in SIZES:
        for e in range(7):
            _, _, _, rows = ss[SIZES.index((name, w, h)) * 7 + e]
            cell = Image.new('RGB', (w, h))
            cell.putdata([bb.EGA[k] for row in rows for k in row])
            im.paste(cell, (e * 100, yy))
        yy += h + 4
    im.save(path)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sp = p.add_subparsers(dest='cmd', required=True)
    q = sp.add_parser('build'); q.add_argument('-o', required=True)
    q = sp.add_parser('draft'); q.add_argument('master'); q.add_argument('-o', default=str(ART / 'sprites.txt'))
    q = sp.add_parser('preview'); q.add_argument('-o', required=True)
    sp.add_parser('check')
    a = p.parse_args()
    try:
        if a.cmd == 'build': build(a.o)
        elif a.cmd == 'draft': draft(a.master, a.o)
        elif a.cmd == 'preview': preview(a.o)
        else:
            ss = sources()
            for n, w, h, rows in ss:
                if n.startswith('mono') and any(k not in [0, 15] for r in rows for k in r): raise ValueError('mono ink')
                if n.startswith('cga') and any(k not in CGA for r in rows for k in r): raise ValueError('CGA ink')
            print('os88btart: source geometry and palette pass')
    except (ValueError, OSError, StopIteration) as e:
        sys.exit(f'os88btart: {e}')

if __name__ == '__main__': main()
