#!/usr/bin/env python3
"""MIDIRack's colour button faces (SPEC.md 105.9.5): draw them, pack them.

    python3 tools/os88midart.py -o build/mrart.bin [--png preview.png]
    python3 tools/os88midart.py --selfcheck

The transport's five buttons - Prev, Play, Pause, Stop, Next - each in three
states (up, pressed, greyed), as PLANAR 4bpp pictures in the standard 16
colours: what OSAPI_GFX_BLITP puts on a VGA or an EGA with one `rep movsb` a
plane a row (SPEC.md 5.4.3), so a face costs one call and a few milliseconds
on the 8088 where a blit of packed pixels would cost thirty. The package
carries them as a LAZY part of MIDIRACK.O88 (SPEC.md 20.12.4) and fetches them
only on a colour display - a Hercules or a CGA never reads a byte of this.

Everything is drawn HERE, in code, and nothing is hand-painted: an icon is a
shape (a triangle, a bar, a square) given a one-pixel black outline and a
bevel - its light colour on every pixel whose left or upper neighbour is
outside it - and a label is SET IN THE SYSTEM'S OWN HELVETICA (faces/helv.t88,
the face SPEC.md 6.4 ships for the type library), so a MIDIRack button is
lettered in the same face as every proportional line on the machine. The
output is byte-for-byte deterministic.

THE GRID IS THE CARD'S: a face is 48 pixels wide because OSAPI_GFX_BLITP
takes whole bytes at an x on a multiple of 8. The button is the left 44 of
them and the right 4 are white paper, so five faces side by side at a pitch
of 48 are five buttons with a 4-pixel gap - and a window's content origin is
already on the grid (SPEC.md 11.94).

The file is the fifteen faces in order, button-major (Prev up, Prev pressed,
Prev greyed, Play up, ...), each four planes of FACE_H rows of FACE_W / 8
bytes - plane 0 first, bit 7 leftmost, a set bit meaning that bit of the
pixel's colour index. apps/midirack/midirack.asm states the same four numbers
(MRA_W, MRA_H, MRA_NB, MRA_NS) and the build compares them (--check-asm).
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

FACE_W, FACE_H = 48, 30             # the picture: whole bytes for BLITP
BTN_W = 44                          # ...of which the button
STATES = ('up', 'down', 'dis')
BUTTONS = ('Prev', 'Play', 'Pause', 'Stop', 'Next')
FACE_BYTES = 4 * FACE_H * FACE_W // 8

BLACK, BLUE, GREEN, CYAN, RED, MAGENTA, BROWN, LGRAY = range(8)
DGRAY, LBLUE, LGREEN, LCYAN, LRED, LMAGENTA, YELLOW, WHITE = range(8, 16)
PALETTE = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
           (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
           (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
           (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255)]

ICON_W, ICON_H = 16, 12             # an icon's cell; its shape in rows 1..10
ICON_Y = 2                          # where the cell sits in the face
LABEL_Y = 15                        # the label's 12-row cell
SHAPE_H = 10                        # an icon shape's rows
FACE = os.path.join(ROOT, 'faces', 'helv.t88')


# --- the shapes ------------------------------------------------------------
ROWS = range(1, SHAPE_H + 1)
EDGE = SHAPE_H + 1


def tri_right(x0):
    """A triangle pointing right, its flat side at x0, two pixels a row."""
    return {(x, r) for r in ROWS
            for x in range(x0, x0 + 2 * min(r, EDGE - r) - 1)}


def tri_left(x1):
    """A narrow triangle pointing left, its flat side at x1 (inclusive)."""
    return {(x, r) for r in ROWS
            for x in range(x1 - min(r, EDGE - r) + 1, x1 + 1)}


def tri_rfan(x0):
    """...and pointing right (the skip arrows)."""
    return {(x, r) for r in ROWS for x in range(x0, x0 + min(r, EDGE - r))}


def rect(x0, x1, y0=1, y1=SHAPE_H):
    return {(x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)}


# (shape, dark colour, light colour)
ICONS = {
    'Prev': (rect(1, 2) | tri_left(7) | tri_left(12), BLUE, LBLUE),
    'Play': (tri_right(4), GREEN, LGREEN),
    'Pause': (rect(3, 6) | rect(9, 12), BROWN, YELLOW),
    'Stop': (rect(3, 12), RED, LRED),
    'Next': (tri_rfan(3) | tri_rfan(8) | rect(13, 14), BLUE, LBLUE),
}


def icon_pixels(name, state):
    """{(x, y): colour} in the 16x10 cell."""
    shape, dark, light = ICONS[name]
    px = {}
    if state == 'dis':                      # embossed: white a pixel down and
        for (x, y) in shape:                # right, the shape in grey on top
            px[(x + 1, y + 1)] = WHITE
        for p in shape:
            px[p] = DGRAY
        return px
    for (x, y) in shape:                    # the outline: every pixel beside
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):   # the shape
            q = (x + dx, y + dy)
            if q not in shape:
                px[q] = BLACK
    for (x, y) in shape:
        lit = (x - 1, y) not in shape or (x, y - 1) not in shape
        px[(x, y)] = light if lit else dark
    return px


# --- the label ---------------------------------------------------------------
def load_face():
    head, styles, _ = os88face.parse(FACE)
    return head, styles['regular']


GAP = 1                             # the air between two letters' ink


def label_pixels(text, head, glyphs):
    """[(x, y)] lit, x from 0, in a 12-row cell, and the width.

    SET BY THE INK, not by the face's advances. A .F88 advance is EVEN by rule
    (SPEC.md 6.4) so the type library's pre-shifted table stays half the size,
    and at 12 pixels that rule puts two pixels between some pairs and three
    between others - 'Pa use' rather than 'Pause'. These labels are set once,
    here, so each letter is placed GAP pixels after the last one's ink."""
    pen, out = 0, []
    stride = head['stride'] * 8
    for ch in text:
        adv, _em, rows = glyphs[ord(ch)]
        cols = [x for x in range(stride)
                if any(r & (1 << (stride - 1 - x)) for r in rows)]
        if not cols:                        # a space: its advance, as is
            pen += adv
            continue
        x0, x1 = min(cols), max(cols)
        for y, bits in enumerate(rows):
            for x in range(x0, x1 + 1):
                if bits & (1 << (stride - 1 - x)):
                    out.append((pen + x - x0, y))
        pen += x1 - x0 + 1 + GAP
    return out, pen - GAP


# --- a face ------------------------------------------------------------------
def face(name, state, head, glyphs):
    img = [[WHITE] * FACE_W for _ in range(FACE_H)]
    x1, y1 = BTN_W - 1, FACE_H - 1
    for y in range(1, y1):                  # the face
        for x in range(1, x1):
            img[y][x] = LGRAY
    for x in range(1, x1):                  # the frame, its corners left as
        img[0][x] = img[y1][x] = BLACK      # paper: a rounded button
    for y in range(1, y1):
        img[y][0] = img[y][x1] = BLACK
    if state == 'down':                     # pressed: a shadow in at the top
        for x in range(1, x1):              # and the left, and the contents
            img[1][x] = DGRAY               # one pixel down and right
        for y in range(1, y1):
            img[y][1] = DGRAY
        sh = 1
    else:                                   # up: light top-left, a two-pixel
        for x in range(1, x1 - 1):          # shadow bottom-right
            img[1][x] = WHITE
        for y in range(1, y1 - 1):
            img[y][1] = WHITE
        for x in range(1, x1):
            img[y1 - 1][x] = DGRAY
        for x in range(2, x1):
            img[y1 - 2][x] = DGRAY
        for y in range(1, y1):
            img[y][x1 - 1] = DGRAY
        for y in range(2, y1):
            img[y][x1 - 2] = DGRAY
        sh = 0
    ix = (BTN_W - ICON_W) // 2 + sh
    for (x, y), c in icon_pixels(name, state).items():
        img[ICON_Y + y + sh][ix + x] = c
    lit, w = label_pixels(name, head, glyphs)
    lx = (BTN_W - w) // 2 + sh
    ink = DGRAY if state == 'dis' else BLACK
    if state == 'dis':
        for x, y in lit:
            img[LABEL_Y + y + sh + 1][lx + x + 1] = WHITE
    for x, y in lit:
        img[LABEL_Y + y + sh][lx + x] = ink
    return img


def planar(img):
    out = bytearray()
    for p in range(4):
        for row in img:
            for b in range(FACE_W // 8):
                v = 0
                for i in range(8):
                    v = (v << 1) | ((row[b * 8 + i] >> p) & 1)
                out.append(v)
    return bytes(out)


def build():
    head, glyphs = load_face()
    imgs = [face(n, s, head, glyphs) for n in BUTTONS for s in STATES]
    return imgs, b''.join(planar(i) for i in imgs)


# --- the preview ------------------------------------------------------------
def png(path, imgs, zoom=4):
    cols = len(STATES)
    w = cols * (FACE_W + 4) * zoom
    h = len(BUTTONS) * (FACE_H + 4) * zoom
    canvas = [[(255, 255, 255)] * w for _ in range(h)]
    for i, img in enumerate(imgs):
        ox = (i % cols) * (FACE_W + 4) * zoom
        oy = (i // cols) * (FACE_H + 4) * zoom
        for y, row in enumerate(img):
            for x, c in enumerate(row):
                for dy in range(zoom):
                    for dx in range(zoom):
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


def check_asm(path):
    """The four numbers the package states, against ours."""
    src = open(path).read()
    want = {'MRA_W': FACE_W, 'MRA_H': FACE_H, 'MRA_NB': len(BUTTONS),
            'MRA_NS': len(STATES)}
    bad = []
    for k, v in want.items():
        m = re.search(r'^%s\s+equ\s+(\d+)' % k, src, re.M)
        if not m or int(m.group(1)) != v:
            bad.append('%s is %s, the art is %d' % (k, m and m.group(1), v))
    return bad


def selfcheck():
    imgs, data = build()
    errs = []
    if len(data) != FACE_BYTES * len(BUTTONS) * len(STATES):
        errs.append('wrong length %d' % len(data))
    head, glyphs = load_face()
    for n in BUTTONS:
        _, w = label_pixels(n, head, glyphs)
        if w > BTN_W - 6:
            errs.append('%s is %d px: no air in a %d px button' % (n, w,
                                                                   BTN_W))
    for i, img in enumerate(imgs):         # the gap is paper, always
        for row in img:
            if any(c != WHITE for c in row[BTN_W:]):
                errs.append('face %d draws into its gap' % i)
                break
    a, b = build()[1], data                 # deterministic
    if a != b:
        errs.append('two builds differ')
    up = imgs[0]                            # a NEGATIVE control: a face with
    up[5][FACE_W - 1] = BLACK               # a pixel in its gap must be caught
    if all(c == WHITE for c in up[5][BTN_W:]):
        errs.append('the gap check cannot fail')
    for e in errs:
        print('os88midart: ' + e)
    print('os88midart: selfcheck %s' % ('FAILED' if errs else 'ok'))
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
            print('os88midart: %s: %s' % (a.check_asm, b))
        if bad:
            return 1
    imgs, data = build()
    if a.out:
        with open(a.out, 'wb') as f:
            f.write(data)
        print('os88midart: %d faces, %d bytes -> %s'
              % (len(imgs), len(data), a.out))
    if a.png:
        png(a.png, imgs)
    return 0


if __name__ == '__main__':
    sys.exit(main())
