#!/usr/bin/env python3
# =============================================================================
# os8088 - tools/arcart.py - ARCADE's cabinet art, generated (ARCADE-SPEC 11.3)
#
#     python3 tools/arcart.py --selfcheck
#     python3 tools/arcart.py --out build/arcade/arcart.inc
#     python3 tools/arcart.py --png DIR          (previews, zoomed)
#
# One generic cabinet for every game (the owner's rule): the marquee's
# letters, lit as rows of bulbs, and a 5x7 small-caps face for the tags and
# the card. Every picture is ASCII in this file - no image is read and no
# service asked - so `make` needs python3 alone and the output is the same
# every run. No game's logo, lettering or character is drawn here: titles
# are text (docs/plans/ARCADE-RELEASE-PLAN.md section 4).
# =============================================================================
import argparse
import os
import sys

# the marquee's letters, 5 x 7, each '#' one BULB
LETTERS = {
    'A': [' ### ', '#   #', '#   #', '#####', '#   #', '#   #', '#   #'],
    'R': ['#### ', '#   #', '#   #', '#### ', '# #  ', '#  # ', '#   #'],
    'C': [' ####', '#    ', '#    ', '#    ', '#    ', '#    ', ' ####'],
    'D': ['#### ', '#   #', '#   #', '#   #', '#   #', '#   #', '#### '],
    'E': ['#####', '#    ', '#    ', '#### ', '#    ', '#    ', '#####'],
}
WORD = 'ARCADE'

# the small caps: 5 x 7, bits 7..3 of each row byte
CAPS = {
    ' ': ['     '] * 7,
    '0': [' ### ', '#   #', '#  ##', '# # #', '##  #', '#   #', ' ### '],
    '1': ['  #  ', ' ##  ', '  #  ', '  #  ', '  #  ', '  #  ', ' ### '],
    '2': [' ### ', '#   #', '    #', '   # ', '  #  ', ' #   ', '#####'],
    '3': ['#####', '   # ', '  #  ', '   # ', '    #', '#   #', ' ### '],
    '4': ['   # ', '  ## ', ' # # ', '#  # ', '#####', '   # ', '   # '],
    '5': ['#####', '#    ', '#### ', '    #', '    #', '#   #', ' ### '],
    '6': ['  ## ', ' #   ', '#    ', '#### ', '#   #', '#   #', ' ### '],
    '7': ['#####', '    #', '   # ', '  #  ', ' #   ', ' #   ', ' #   '],
    '8': [' ### ', '#   #', '#   #', ' ### ', '#   #', '#   #', ' ### '],
    '9': [' ### ', '#   #', '#   #', ' ####', '    #', '   # ', ' ##  '],
    'A': [' ### ', '#   #', '#   #', '#####', '#   #', '#   #', '#   #'],
    'B': ['#### ', '#   #', '#   #', '#### ', '#   #', '#   #', '#### '],
    'C': [' ### ', '#   #', '#    ', '#    ', '#    ', '#   #', ' ### '],
    'D': ['#### ', '#   #', '#   #', '#   #', '#   #', '#   #', '#### '],
    'E': ['#####', '#    ', '#    ', '#### ', '#    ', '#    ', '#####'],
    'F': ['#####', '#    ', '#    ', '#### ', '#    ', '#    ', '#    '],
    'G': [' ### ', '#   #', '#    ', '# ###', '#   #', '#   #', ' ####'],
    'H': ['#   #', '#   #', '#   #', '#####', '#   #', '#   #', '#   #'],
    'I': [' ### ', '  #  ', '  #  ', '  #  ', '  #  ', '  #  ', ' ### '],
    'J': ['  ###', '   # ', '   # ', '   # ', '   # ', '#  # ', ' ##  '],
    'K': ['#   #', '#  # ', '# #  ', '##   ', '# #  ', '#  # ', '#   #'],
    'L': ['#    ', '#    ', '#    ', '#    ', '#    ', '#    ', '#####'],
    'M': ['#   #', '## ##', '# # #', '# # #', '#   #', '#   #', '#   #'],
    'N': ['#   #', '#   #', '##  #', '# # #', '#  ##', '#   #', '#   #'],
    'O': [' ### ', '#   #', '#   #', '#   #', '#   #', '#   #', ' ### '],
    'P': ['#### ', '#   #', '#   #', '#### ', '#    ', '#    ', '#    '],
    'Q': [' ### ', '#   #', '#   #', '#   #', '# # #', '#  # ', ' ## #'],
    'R': ['#### ', '#   #', '#   #', '#### ', '# #  ', '#  # ', '#   #'],
    'S': [' ####', '#    ', '#    ', ' ### ', '    #', '    #', '#### '],
    'T': ['#####', '  #  ', '  #  ', '  #  ', '  #  ', '  #  ', '  #  '],
    'U': ['#   #', '#   #', '#   #', '#   #', '#   #', '#   #', ' ### '],
    'V': ['#   #', '#   #', '#   #', '#   #', '#   #', ' # # ', '  #  '],
    'W': ['#   #', '#   #', '#   #', '# # #', '# # #', '# # #', ' # # '],
    'X': ['#   #', '#   #', ' # # ', '  #  ', ' # # ', '#   #', '#   #'],
    'Y': ['#   #', '#   #', ' # # ', '  #  ', '  #  ', '  #  ', '  #  '],
    'Z': ['#####', '    #', '   # ', '  #  ', ' #   ', '#    ', '#####'],
    '-': ['     ', '     ', '     ', ' ### ', '     ', '     ', '     '],
    '.': ['     ', '     ', '     ', '     ', '     ', ' ##  ', ' ##  '],
    '/': ['    #', '    #', '   # ', '  #  ', ' #   ', '#    ', '#    '],
    ':': ['     ', ' ##  ', ' ##  ', '     ', ' ##  ', ' ##  ', '     '],
    '*': ['     ', '# # #', ' ### ', '#####', ' ### ', '# # #', '     '],
    "'": ['  #  ', '  #  ', ' #   ', '     ', '     ', '     ', '     '],
    '!': ['  #  ', '  #  ', '  #  ', '  #  ', '  #  ', '     ', '  #  '],
    '?': [' ### ', '#   #', '    #', '   # ', '  #  ', '     ', '  #  '],
    '&': [' ##  ', '#  # ', '# #  ', ' #   ', '# # #', '#  # ', ' ## #'],
    '+': ['     ', '  #  ', '  #  ', '#####', '  #  ', '  #  ', '     '],
    '(': ['   # ', '  #  ', ' #   ', ' #   ', ' #   ', '  #  ', '   # '],
    ')': [' #   ', '  #  ', '   # ', '   # ', '   # ', '  #  ', ' #   '],
    ',': ['     ', '     ', '     ', '     ', ' ##  ', '  #  ', ' #   '],
}
CAPS_FIRST, CAPS_LAST = 32, 90      # ' ' .. 'Z': lower case is folded up

# the marquee: each bulb a BS x BS block on a BP-pixel pitch
BIG = (4, 3)                        # FULL: 4-pixel pitch, 3-pixel bulbs
GAP = 1                             # bulbs between letters


def marquee(pitch, bulb, rows_pitch=None, rows_bulb=None):
    """The word as a bitmap: a list of rows of 0/1."""
    rp = rows_pitch or pitch
    rb = rows_bulb or bulb
    cols = []
    for k, ch in enumerate(WORD):
        if k:
            cols += [' ' * 7] * GAP
        g = LETTERS[ch]
        for c in range(5):
            cols.append(''.join(g[r][c] for r in range(7)))
    w = len(cols) * pitch - (pitch - bulb)
    h = 7 * rp - (rp - rb)
    img = [[0] * w for _ in range(h)]
    for c, col in enumerate(cols):
        for r in range(7):
            if col[r] == '#':
                for y in range(rb):
                    for x in range(bulb):
                        img[r * rp + y][c * pitch + x] = 1
    return img


def pack(img):
    w = len(img[0])
    stride = (w + 7) // 8
    out = []
    for row in img:
        b = bytearray(stride)
        for x, v in enumerate(row):
            if v:
                b[x >> 3] |= 0x80 >> (x & 7)
        out.append(bytes(b))
    return w, stride, out


def caps_glyph(ch):
    rows = CAPS[ch]
    return bytes(sum((0x80 >> i) for i, c in enumerate(r) if c == '#') for r in rows)


def selfcheck():
    bad = []
    for ch, rows in list(CAPS.items()) + list(LETTERS.items()):
        if len(rows) != 7 or any(len(r) != 5 for r in rows):
            bad.append('%r: not 5 x 7' % ch)
    for ch in WORD:
        if ch not in LETTERS:
            bad.append('%r: no marquee letter' % ch)
    seen = {}
    for ch in CAPS:
        g = caps_glyph(ch)
        if g in seen and ch != ' ':
            bad.append('%r draws as %r' % (ch, seen[g]))
        seen[g] = ch
        if ch != ' ' and not any(g):
            bad.append('%r is blank' % ch)
    for c in range(ord('A'), ord('Z') + 1):
        if chr(c) not in CAPS:
            bad.append('%r: no small cap' % chr(c))
    big = marquee(*BIG)
    if len(big[0]) % 8 and len(big[0]) > 160:
        bad.append('marquee wider than its field')
    for b in bad:
        print('arcart: ' + b)
    if not bad:
        print('arcart: %d small caps, marquee %dx%d and %dx%d - ok'
              % (len(CAPS), len(big[0]), len(big), len(marquee(2, 2, 1, 1)[0]),
                 len(marquee(2, 2, 1, 1))))
    return not bad


def emit(out):
    L = ['; GENERATED by tools/arcart.py - ARCADE\'s cabinet art (ARCADE-SPEC 11.3).',
         '; Every picture: dw width, db height, db stride, then the rows (bit 7',
         '; leftmost). Do not edit: change the script.']

    def pic(name, img):
        w, stride, rows = pack(img)
        L.append('%s:' % name)
        L.append('    dw %d' % w)
        L.append('    db %d, %d' % (len(rows), stride))
        for r in rows:
            L.append('    db ' + ', '.join('0x%02X' % b for b in r))
        return w, len(rows), stride
    w, h, s = pic('arcart_logo', marquee(*BIG))
    L.append('ARCART_LOGO_W equ %d' % w)
    L.append('ARCART_LOGO_H equ %d' % h)
    w, h, s = pic('arcart_slogo', marquee(2, 2, 1, 1))
    L.append('ARCART_SLOGO_W equ %d' % w)
    L.append('ARCART_SLOGO_H equ %d' % h)
    # the small caps: an index (ASCII 32..90, 0xFF = none) and 7 bytes each
    order = [ch for ch in CAPS]
    ix = []
    for c in range(CAPS_FIRST, CAPS_LAST + 1):
        ix.append(order.index(chr(c)) if chr(c) in CAPS else 0xFF)
    L.append('ARCART_CAPS_FIRST equ %d' % CAPS_FIRST)
    L.append('ARCART_CAPS_LAST equ %d' % CAPS_LAST)
    L.append('arcart_caps_ix:')
    for k in range(0, len(ix), 16):
        L.append('    db ' + ', '.join('%d' % v for v in ix[k:k + 16]))
    L.append('arcart_caps:')
    for ch in order:
        L.append('    db ' + ', '.join('0x%02X' % b for b in caps_glyph(ch))
                 + '   ; %r' % ch)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, 'w').write('\n'.join(L) + '\n')


def png(d):
    from PIL import Image
    os.makedirs(d, exist_ok=True)
    for name, img in (('logo', marquee(*BIG)), ('slogo', marquee(2, 2, 1, 1))):
        h, w = len(img), len(img[0])
        im = Image.new('L', (w, h))
        im.putdata([255 if v else 0 for row in img for v in row])
        im.resize((w * 4, h * 4), Image.NEAREST).save(os.path.join(d, name + '.png'))
    chars = list(CAPS)
    im = Image.new('L', (len(chars) * 6, 7))
    for k, ch in enumerate(chars):
        g = caps_glyph(ch)
        for y in range(7):
            for x in range(5):
                if g[y] & (0x80 >> x):
                    im.putpixel((k * 6 + x, y), 255)
    im.resize((im.width * 4, 28), Image.NEAREST).save(os.path.join(d, 'caps.png'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--selfcheck', action='store_true')
    ap.add_argument('--out')
    ap.add_argument('--png')
    a = ap.parse_args()
    if a.selfcheck and not selfcheck():
        return 1
    if a.out:
        emit(a.out)
    if a.png:
        png(a.png)
    return 0


if __name__ == '__main__':
    sys.exit(main())
