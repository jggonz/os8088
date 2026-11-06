#!/usr/bin/env python3
"""TEST bank for PDREND's host harness (PAGEDESK-SPEC 11.2 + 20.3.4 extension).

NOT the product bank (tools/os88pdfont.py builds that from OFL fonts). This one
rasterises the host's system fonts so the harness has real glyph bitmaps; its
output is never committed.  Usage: pdr_mkbank.py out.bin [sizes...]"""
import struct, sys
from PIL import ImageFont, Image, ImageDraw

D = '/System/Library/Fonts/Supplemental/'
FACES = {
    (0, 0): 'Arial.ttf', (0, 1): 'Arial Bold.ttf', (0, 2): 'Arial Italic.ttf', (0, 3): 'Arial Bold Italic.ttf',
    (1, 0): 'Times New Roman.ttf', (1, 1): 'Times New Roman Bold.ttf', (1, 2): 'Times New Roman Italic.ttf', (1, 3): 'Times New Roman Bold Italic.ttf',
    (2, 0): 'Courier New.ttf', (2, 1): 'Courier New Bold.ttf', (2, 2): 'Courier New Italic.ttf', (2, 3): 'Courier New Bold Italic.ttf',
}
SIZES = [int(a) for a in sys.argv[2:]] or [6, 7, 8, 9, 10, 12, 14, 16, 20, 24]
out = sys.argv[1]

def cp(c):
    try: return bytes([c]).decode('cp1252')
    except Exception: return None

def strike(fam, sty, size):
    f = ImageFont.truetype(D + FACES[(fam, sty)], size)
    asc, desc = f.getmetrics()
    rows = asc + desc
    recs, bmp = [], bytearray()
    for code in range(32, 256):
        ch = cp(code)
        if ch is None or code == 32 or code == 160:
            recs.append((0, 0, 0)); continue
        pad = size
        im = Image.new('L', (size * 3, rows + 2), 0)
        ImageDraw.Draw(im).text((pad, asc), ch, font=f, fill=255, anchor='ls')
        px = im.load()
        cols = [x for x in range(im.width) if any(px[x, y] > 60 for y in range(rows))]
        if not cols:
            recs.append((0, 0, 0)); continue
        x0, x1 = cols[0], cols[-1]
        w = x1 - x0 + 1
        wb = (w + 7) // 8
        off = len(bmp)
        for y in range(rows):
            for b in range(wb):
                v = 0
                for k in range(8):
                    x = x0 + b * 8 + k
                    if x <= x1 and px[x, y] > 60: v |= 0x80 >> k
                bmp.append(v)
        recs.append((w, x0 - pad, off))
    data = bytearray()
    for (w, bear, off) in recs:
        data += struct.pack('<BbH', w, bear, off)
    return asc, desc, bytes(data) + bytes(bmp)

strikes = []
for fam in range(3):
    for sty in range(4):
        for size in SIZES:
            strikes.append((fam, sty, size) + strike(fam, sty, size))
n = len(strikes)
widths = bytearray()
for fam in range(3):
    for sty in range(4):
        f = ImageFont.truetype(D + FACES[(fam, sty)], 1000)
        for code in range(32, 256):
            ch = cp(code)
            w = int(round(f.getlength(ch))) if ch else 0
            if fam == 2: w = 600
            widths += struct.pack('<H', w)
names = [('space', 32), ('exclam', 33), ('quotedbl', 34), ('numbersign', 35), ('dollar', 36), ('percent', 37), ('ampersand', 38),
         ('quotesingle', 39), ('parenleft', 40), ('parenright', 41), ('asterisk', 42), ('plus', 43), ('comma', 44), ('hyphen', 45),
         ('period', 46), ('slash', 47), ('colon', 58), ('semicolon', 59), ('less', 60), ('equal', 61), ('greater', 62), ('question', 63),
         ('at', 64), ('bracketleft', 91), ('backslash', 92), ('bracketright', 93), ('asciicircum', 94), ('underscore', 95), ('grave', 96),
         ('braceleft', 123), ('bar', 124), ('braceright', 125), ('asciitilde', 126), ('bullet', 0x95), ('endash', 0x96), ('emdash', 0x97),
         ('quoteleft', 0x91), ('quoteright', 0x92), ('quotedblleft', 0x93), ('quotedblright', 0x94), ('ellipsis', 0x85), ('Euro', 0x80),
         ('eacute', 0xE9), ('Eacute', 0xC9), ('adieresis', 0xE4), ('odieresis', 0xF6), ('udieresis', 0xFC), ('germandbls', 0xDF)]
for i, w in enumerate(['zero', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine']): names.append((w, 48 + i))
for i in range(26): names.append((chr(65 + i), 65 + i)); names.append((chr(97 + i), 97 + i))
names.sort(key=lambda t: t[0].encode())
nb = bytearray()
for nm, c in names: nb += bytes([len(nm)]) + nm.encode() + bytes([c])
nb += b'\0'
hdr_len = 8 + 16 * n + 8
widths_off = hdr_len
names_off = widths_off + len(widths)
pos = names_off + len(nb)
recs = bytearray(); body = bytearray()
for (fam, sty, size, asc, desc, data) in strikes:
    recs += struct.pack('<BBBBBBBBII', fam, sty, size, asc, desc, 32, 255, 0, pos + len(body), len(data))
    body += data
bank = b'PDFB' + struct.pack('<HH', 1, n) + recs + b'PDFX' + struct.pack('<HH', widths_off, names_off) + widths + nb + body
open(out, 'wb').write(bank)
print('bank', out, len(bank), 'bytes,', n, 'strikes')
