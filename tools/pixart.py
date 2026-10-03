#!/usr/bin/env python3
"""PiXEL's colour button faces (SPEC.md 106.16): draw them, pack them.

    python3 tools/pixart.py -o build/PIXEL.GFX [--png preview.png]
    python3 tools/pixart.py --check-asm apps/pixel/pixel.asm
    python3 tools/pixart.py --selfcheck

The toolbar's ten buttons and Stop, each up / pressed / greyed, and the tool
column's six, each up / pressed (a latched tool is drawn pressed), as PLANAR
4bpp pictures in the standard sixteen colours: what OSAPI_GFX_BLITP puts on a
VGA or an EGA with one `rep movsb` a plane a row (SPEC.md 5.4.3). A button is
then ONE drawing call - where the code-drawn bevel it falls back to is ten -
and the caption is INSIDE the button, under its picture, so a greyed button's
caption greys with it (the wave-1 review's NIT 2: the mono face's caption row
is one run and cannot).

Nothing is hand-painted twice. The pictures are apps/pixel/pxicons.inc's own
16x16 art, read out of the asm, so the colour face and the mono face cannot
drift apart: the ink is the icon's, and the INSIDE of a shape - every paper
pixel the outside cannot reach - takes the icon's accent colour (the folder
yellow, a lens cyan, the stop sign's octagon red). The captions are SET IN
THE SYSTEM'S OWN HELVETICA (faces/helv.t88, SPEC.md 6.4) letter by letter on
the ink with a one-pixel gap, as tools/os88midart.py sets MIDIRack's, because
the face's even advances read "Sl ideshow" at this size. The output is
byte-for-byte deterministic.

THE GRID IS THE CARD'S: OSAPI_GFX_BLITP takes whole bytes at an x on a
multiple of 8, so every face is a multiple of 8 wide, and the button is the
face less two columns of chrome at its right - so faces laid edge to edge are
buttons with a 2-pixel gap, and a run of them is one blit each with no ground
between. A tool face is the tool column's whole width, its rule included.

The file is the toolbar's faces in PX_B_* order, three states each (up,
pressed, greyed), then Stop's three, then the tools' faces, two states each;
each face four planes of its rows of (width / 8) bytes, plane 0 first, bit 7
leftmost. apps/pixel/pixel.asm states the numbers (PXA_*) and the widths
(PXA_TABLES), and --check-asm compares them with these.
"""
import argparse
import os
import re
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import os88face                                                # noqa: E402

ICONS_INC = os.path.join(ROOT, 'apps', 'pixel', 'pxicons.inc')
FACE = os.path.join(ROOT, 'faces', 'helv.t88')

BLACK, BLUE, GREEN, CYAN, RED, MAGENTA, BROWN, LGRAY = range(8)
DGRAY, LBLUE, LGREEN, LCYAN, LRED, LMAGENTA, YELLOW, WHITE = range(8, 16)
PALETTE = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
           (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
           (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
           (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255)]
CHROME = LGRAY                      # the window's ground (pixel.asm px_pal4)
RULE = DGRAY                        # ...and its rules

TB_H = 30                           # a toolbar face's rows
GAP = 2                             # paper right of every button
# (caption, picture, face width): PX_B_OPEN .. PX_B_SHOW, then Stop
TOOLBAR = [('Open', 'open', 40), ('Save', 'save', 40),
           ('Prev', 'prev', 40), ('Next', 'next', 40),
           ('Zoom In', 'zoomin', 56), ('Zoom Out', 'zoomout', 64),
           ('Fit', 'fit', 40), ('Actual', '1:1', 40),
           ('Rotate', 'rotate', 48), ('Slideshow', 'show', 64),
           ('Stop', 'stop', 40)]
TB_STATES = ('up', 'down', 'dis')
TL_W, TL_H = 32, 24                 # a tool face: the column's width
TL_BX0, TL_BX1 = 4, 27              # ...the button's columns in it
TL_BY0, TL_BY1 = 1, 22              # ...and its rows
TOOLS = ['hand', 'magnify', 'marquee', 'crop', 'eyedrop', 'rotate']
TL_STATES = ('up', 'down')

# (ink, inside) per picture: what colours the art
STYLE = {'open': (BLACK, YELLOW), 'save': (BLACK, LBLUE),
         'prev': (BLUE, None), 'next': (BLUE, None),
         'zoomin': (BLACK, LCYAN), 'zoomout': (BLACK, LCYAN),
         'fit': (BLACK, LCYAN), 'rotate': (BLUE, None),
         'show': (GREEN, LGREEN), 'stop': (RED, WHITE),
         'hand': (BLACK, WHITE), 'magnify': (BLACK, LCYAN),
         'marquee': (BLACK, None), 'crop': (BLACK, None),
         'eyedrop': (BLACK, LCYAN)}


# "1:1" has no picture in pxicons.inc (the mono face's button is the text):
# drawn here, bold, at an icon's height
ONE_ART = ['..##.......##..',
           '.###.......###.',
           '####.......###.',
           '..##...##...##.',
           '..##...##...##.',
           '..##........##.',
           '..##........##.',
           '..##...##...##.',
           '..##...##...##.',
           '..##........##.',
           '######....######']
ONE = {(x, y) for y, r in enumerate(ONE_ART) for x, c in enumerate(r)
       if c == '#'}
ONE_W, ONE_H = max(len(r) for r in ONE_ART), len(ONE_ART)


# --- the art, out of the asm ---------------------------------------------------
def load_icons():
    """{name: [16 row words]} from pxicons.inc's DATA rows."""
    src = open(ICONS_INC).read()
    out = {}
    for m in re.finditer(r'^pxi_(\w+):\s*\n\s*db 1, 16\s*\n\s*times 16 dw '
                         r'0xFFFF\s*\n((?:\s*dw 0x[0-9A-Fa-f]{4}.*\n){16})',
                         src, re.M):
        out[m.group(1)] = [int(w, 16) for w in
                           re.findall(r'dw 0x([0-9A-Fa-f]{4})', m.group(2))]
    return out


def icon_shape(rows):
    """(ink set, inside set) of a 16x16 picture: the inside is every paper
    pixel the cell's edge cannot reach through paper."""
    ink = {(x, y) for y, w in enumerate(rows) for x in range(16)
           if w & (0x8000 >> x)}
    seen, todo = set(), [(x, y) for x in range(16) for y in (0, 15)] + \
        [(x, y) for y in range(16) for x in (0, 15)]
    while todo:
        p = todo.pop()
        if p in seen or p in ink or not (0 <= p[0] < 16 and 0 <= p[1] < 16):
            continue
        seen.add(p)
        x, y = p
        todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    inside = {(x, y) for x in range(16) for y in range(16)} - ink - seen
    return ink, inside


# --- the captions --------------------------------------------------------------
def load_face():
    head, styles, _ = os88face.parse(FACE)
    return head, styles['regular']


def label_pixels(text, head, glyphs):
    """[(x, y)] lit in a 12-row cell, x from 0, and the width - set by the
    ink with a one-pixel gap (os88midart.py's reason)."""
    pen, out = 0, []
    stride = head['stride'] * 8
    for ch in text:
        adv, _em, rows = glyphs[ord(ch)]
        cols = [x for x in range(stride)
                if any(r & (1 << (stride - 1 - x)) for r in rows)]
        if not cols:
            pen += adv
            continue
        x0, x1 = min(cols), max(cols)
        for y, bits in enumerate(rows):
            for x in range(x0, x1 + 1):
                if bits & (1 << (stride - 1 - x)):
                    out.append((pen + x - x0, y))
        pen += x1 - x0 + 2
    return out, pen - 1


def trim(pts):
    """The points moved so their ink starts at (0, 0), and (w, h)."""
    if not pts:
        return [], 0, 0
    x0 = min(x for x, _ in pts)
    y0 = min(y for _, y in pts)
    return ([(x - x0, y - y0) for x, y in pts],
            max(x for x, _ in pts) - x0 + 1, max(y for _, y in pts) - y0 + 1)


# --- a button ------------------------------------------------------------------
def bevel(img, x0, y0, x1, y1, state, face=LGRAY):
    """A black frame with its corners left as ground, the face light grey
    (or FACE), and a light top-left and a grey bottom-right - or, pressed,
    one grey line in at the top and the left. Answers the contents' shift."""
    for y in range(y0 + 1, y1):
        for x in range(x0 + 1, x1):
            img[y][x] = face
    for x in range(x0 + 1, x1):
        img[y0][x] = img[y1][x] = BLACK
    for y in range(y0 + 1, y1):
        img[y][x0] = img[y][x1] = BLACK
    if state == 'down':
        for x in range(x0 + 1, x1):
            img[y0 + 1][x] = DGRAY
        for y in range(y0 + 1, y1):
            img[y][x0 + 1] = DGRAY
        return 1
    for x in range(x0 + 1, x1 - 1):
        img[y0 + 1][x] = WHITE
    for y in range(y0 + 1, y1 - 1):
        img[y][x0 + 1] = WHITE
    for x in range(x0 + 1, x1):
        img[y1 - 1][x] = DGRAY
    for y in range(y0 + 1, y1):
        img[y][x1 - 1] = DGRAY
    return 0


def put_picture(img, pic, ox, oy, state, icons, head, glyphs):
    """The picture's ink trimmed and placed at (ox, oy); answers (w, h)."""
    if pic == '1:1':
        ink, inside = set(ONE), set()
        w, h = ONE_W, ONE_H
        colour, fill = BLUE, None
    else:
        ink, inside = icon_shape(icons[pic])
        both, w, h = trim(list(ink | inside))
        x0 = min(x for x, _ in ink | inside)
        y0 = min(y for _, y in ink | inside)
        ink = {(x - x0, y - y0) for x, y in ink}
        inside = {(x - x0, y - y0) for x, y in inside}
        colour, fill = STYLE[pic]
    if state == 'dis':                      # embossed: white a pixel down
        for x, y in ink:                    # and right, the ink grey on top,
            img[oy + y + 1][ox + x + 1] = WHITE     # the inside left as face
        for x, y in ink:
            img[oy + y][ox + x] = DGRAY
        return w, h
    for x, y in inside:
        if fill is not None:
            img[oy + y][ox + x] = fill
    for x, y in ink:
        img[oy + y][ox + x] = colour
    return w, h


def pic_size(pic, icons, head, glyphs):
    if pic == '1:1':
        return ONE_W, ONE_H
    ink, inside = icon_shape(icons[pic])
    _, w, h = trim(list(ink | inside))
    return w, h


def tb_face(i, state, icons, head, glyphs):
    cap, pic, fw = TOOLBAR[i]
    img = [[CHROME] * fw for _ in range(TB_H)]
    bx1 = fw - 1 - GAP
    sh = bevel(img, 0, 0, bx1, TB_H - 1, state)
    pw, ph = pic_size(pic, icons, head, glyphs)
    lit, lw, lh = trim(label_pixels(cap, head, glyphs)[0])
    # the picture's rows and the caption's, as one block in the face's
    # inside (rows 2 .. TB_H - 3), one row of air between them
    top = 2 + (TB_H - 4 - (ph + 2 + 9)) // 2
    put_picture(img, pic, (bx1 + 1 - pw) // 2 + sh, top + sh, state, icons,
                head, glyphs)
    lx = (bx1 + 1 - lw) // 2 + sh
    ly = top + ph + 2 + sh
    ink = DGRAY if state == 'dis' else BLACK
    if state == 'dis':
        for x, y in lit:
            img[ly + y + 1][lx + x + 1] = WHITE
    for x, y in lit:
        img[ly + y][lx + x] = ink
    return img


def tl_face(i, state, icons, head, glyphs):
    pic = TOOLS[i]
    img = [[CHROME] * TL_W for _ in range(TL_H)]
    for y in range(TL_H):
        img[y][TL_W - 1] = RULE
    # a tool is pressed because it is LATCHED (the tool in use), so its
    # pressed face is white inside as well as sunk: it has to read from
    # across the window, not only under the pointer
    sh = bevel(img, TL_BX0, TL_BY0, TL_BX1, TL_BY1, state,
               WHITE if state == 'down' else LGRAY)
    pw, ph = pic_size(pic, icons, head, glyphs)
    put_picture(img, pic, TL_BX0 + (TL_BX1 - TL_BX0 + 1 - pw) // 2 + sh,
                TL_BY0 + (TL_BY1 - TL_BY0 + 1 - ph) // 2 + sh, state, icons,
                head, glyphs)
    return img


def planar(img):
    out = bytearray()
    w = len(img[0])
    for p in range(4):
        for row in img:
            for b in range(w // 8):
                v = 0
                for i in range(8):
                    v = (v << 1) | ((row[b * 8 + i] >> p) & 1)
                out.append(v)
    return bytes(out)


def build():
    icons = load_icons()
    head, glyphs = load_face()
    imgs = [tb_face(i, s, icons, head, glyphs)
            for i in range(len(TOOLBAR)) for s in TB_STATES]
    imgs += [tl_face(i, s, icons, head, glyphs)
             for i in range(len(TOOLS)) for s in TL_STATES]
    return imgs, b''.join(planar(i) for i in imgs)


# --- the preview ----------------------------------------------------------------
def png(path, imgs, zoom=3):
    cols = 3
    cw = max(len(i[0]) for i in imgs) + 4
    ch = max(len(i) for i in imgs) + 4
    rows = (len(imgs) + cols - 1) // cols
    w, h = cols * cw * zoom, rows * ch * zoom
    canvas = [[(255, 255, 255)] * w for _ in range(h)]
    k = 0
    for i, img in enumerate(imgs):
        if i == len(TOOLBAR) * 3:           # the tools start a row of their own
            k = (k + cols - 1) // cols * cols
        ox, oy = (k % cols) * cw * zoom, (k // cols) * ch * zoom
        k += 1
        for y, row in enumerate(img):
            for x, c in enumerate(row):
                for dy in range(zoom):
                    for dx in range(zoom):
                        if oy + y * zoom + dy < h:
                            canvas[oy + y * zoom + dy][ox + x * zoom + dx] = \
                                PALETTE[c]
    raw = b''.join(b'\0' + bytes(v for px in row for v in px)
                   for row in canvas)

    def chunk(t, d):
        return (struct.pack('>I', len(d)) + t + d +
                struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        f.write(chunk(b'IEND', b''))


# --- the package's numbers ------------------------------------------------------
def want_numbers():
    return {'PXA_TBH': TB_H, 'PXA_TBN': len(TOOLBAR),
            'PXA_TBS': len(TB_STATES), 'PXA_GAP': GAP, 'PXA_TLW': TL_W,
            'PXA_TLH': TL_H, 'PXA_TLN': len(TOOLS), 'PXA_TLS': len(TL_STATES),
            'PXA_TLX0': TL_BX0, 'PXA_TLX1': TL_BX1, 'PXA_TLY0': TL_BY0,
            'PXA_TLY1': TL_BY1, 'PXA_STOP': len(TOOLBAR) - 1}


def check_asm(path):
    src = open(path).read()
    bad = []
    for k, v in want_numbers().items():
        m = re.search(r'^%s\s+equ\s+(\d+)' % k, src, re.M)
        if not m or int(m.group(1)) != v:
            bad.append('%s is %s, the art is %d' % (k, m and m.group(1), v))
    m = re.search(r'^%if PXA_SIZE != (\d+)', src, re.M)
    size = len(build()[1])
    if not m or int(m.group(1)) != size:
        bad.append('PXA_SIZE is asserted %s, the art is %d'
                   % (m and m.group(1), size))
    m = re.search(r'^\s+PXA_TABLES\s+([0-9, ]+)', src, re.M)
    ws = [w for _, _, w in TOOLBAR]
    if not m or [int(x) for x in m.group(1).split(',')] != ws:
        bad.append('PXA_TABLES is %s, the art is %s' % (m and m.group(1), ws))
    return bad


def selfcheck():
    imgs, data = build()
    errs = []
    icons = load_icons()
    for _, pic, _ in TOOLBAR:
        if pic != '1:1' and pic not in icons:
            errs.append('no picture pxi_%s in pxicons.inc' % pic)
    for t in TOOLS:
        if t not in icons:
            errs.append('no picture pxi_%s in pxicons.inc' % t)
    head, glyphs = load_face()
    for cap, _, fw in TOOLBAR:
        _, w = label_pixels(cap, head, glyphs)
        if w > fw - GAP - 6:
            errs.append('%s is %d px: no air in a %d px button'
                        % (cap, w, fw - GAP))
        if fw % 8:
            errs.append('%s: a face %d wide is not whole bytes' % (cap, fw))
    for i in range(len(TOOLBAR) * len(TB_STATES)):
        img = imgs[i]
        for row in img:                     # the gap is chrome, always
            if any(c != CHROME for c in row[len(row) - GAP:]):
                errs.append('toolbar face %d draws into its gap' % i)
                break
    n = len(TOOLBAR) * len(TB_STATES)
    for i, img in enumerate(imgs[n:]):      # the tool faces end in the rule
        if any(row[TL_W - 1] != RULE for row in img):
            errs.append('tool face %d loses the column\'s rule' % i)
    if build()[1] != data:
        errs.append('two builds differ')
    up = [r[:] for r in imgs[0]]            # a NEGATIVE control: a pixel in
    up[5][len(up[5]) - 1] = BLACK           # the gap must be caught
    if all(c == CHROME for c in up[5][len(up[5]) - GAP:]):
        errs.append('the gap check cannot fail')
    for e in errs:
        print('pixart: ' + e)
    print('pixart: selfcheck %s' % ('FAILED' if errs else 'ok'))
    return 1 if errs else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('-o', '--out')
    ap.add_argument('--png')
    ap.add_argument('--check-asm')
    ap.add_argument('--selfcheck', action='store_true')
    a = ap.parse_args()
    if a.selfcheck:
        return selfcheck()
    if a.check_asm:
        bad = check_asm(a.check_asm)
        for b in bad:
            print('pixart: %s: %s' % (a.check_asm, b))
        if bad:
            return 1
    imgs, data = build()
    if a.out:
        with open(a.out, 'wb') as f:
            f.write(data)
        print('pixart: %d faces, %d bytes -> %s' % (len(imgs), len(data),
                                                   a.out))
    if a.png:
        png(a.png, imgs)
    return 0


if __name__ == '__main__':
    sys.exit(main())
