#!/usr/bin/env python3
"""os88pdfx_rcore - the rcore fixtures of PageDesk's any-PDF round (PAGEDESK-SPEC 20.22.7).

    python3 tools/os88pdfx_rcore.py <out dir>

Writes, deterministically (classic xref, no compression unless a filter is the
point of the file), the PDFs hosttest/pdr_xchk.py draws through PDREND and
holds to MuPDF and to each other:

  dash.pdf     the dash walker that moved to PDXG (pdxdash.c): every pattern
               shape 20.12.4 names - runs, phase, odd counts, round caps and
               zero-length dashes, closed paths, curves flattened into many
               points (a dash longer than a piece of 12), q/Q
  ramp.pdf     page 1: an 8-bit DeviceGray ramp as a plain image; page 2: the
               SAME geometry as a /DCTDecode image (a real JPEG of the ramp when
               Pillow is there, else a stub); page 3: /JPXDecode; page 4: a DCT
               and a JPX image on one page, apart; page 5: the two side by
               side, sharing rows (one slot: the band's stash, 20.22.7.10);
               page 6: page 5 with both as plain images, which page 5 must
               draw as to the pixel; page 7: a JPX, a rectangle, a DCT over
               both and the JPX again, overlapping on the same bands - the
               paint order through the stash; page 8: page 7 plain; page 9:
               text, a DCT image, text - the glyph claim moved under the
               image's NEEDMOD (20.22.7.11) must draw it unchanged. The
               host's stand-in decoder (hosttest/pdr_xmod.c) delivers the ramp
               whatever the bytes, so pages 1 and 2 must draw identically
  depth.pdf    the ramp at 1, 2, 4, 16 bits DeviceGray, 8 and 16-bit RGB,
               8-bit CMYK, and a 4-bit image under /Decode [1 0]: smode 4
  depthg.pdf   the same images as the 8-bit DeviceGray PDREND must make of
               them: depth.pdf must draw to the pixel as this does
  mask.pdf     a /DCTDecode image with /ImageMask true (a CCITT-like stencil
               through the decoder's 1-bit rows), and an image with /SMask
  forms.pdf    Flate Forms inside a Flate page, as Quartz writes every page
               (QA R14): page 1 draws before and after a Form that nests a
               second Flate Form, with 300 tokens before each Do, so with ONE
               Flate slot each parent is parked and replayed (20.22.7.12) and
               must draw as with two; page 2 has an inline image before the
               Do, which cannot be replayed by tokens (the known limit)

The ramp is a function of the pixel: (x * 200) / (w - 1) + (y * 55) / (h - 1),
the same as hosttest/pdr_xmod.c's.
"""
import io
import os
import sys
import zlib


def ramp(x, y, w, h):
    v = (x * 200) // max(w - 1, 1) + (y * 55) // max(h - 1, 1)
    return min(v, 255)


def ramp_bytes(w, h):
    return bytes(ramp(x, y, w, h) for y in range(h) for x in range(w))


class Pdf:
    def __init__(self):
        self.objs = [None, None]           # 1 catalog, 2 pages
        self.pages = []

    def add(self, body):
        self.objs.append(body)
        return len(self.objs)

    def stream(self, dict_txt, data):
        if isinstance(data, str):
            data = data.encode('latin-1')
        return self.add(('<< %s /Length %d >>\nstream\n' % (dict_txt, len(data))).encode() + data + b'\nendstream')

    def page(self, content, xobjs=None, media='[0 0 400 400]', fonts=None):
        c = self.stream('', content)
        res = ''
        if xobjs:
            res = '/XObject << %s >>' % ' '.join('/%s %d 0 R' % (k, v) for k, v in xobjs.items())
        if fonts:
            res += ' /Font << %s >>' % ' '.join('/%s %d 0 R' % (k, v) for k, v in fonts.items())
        p = self.add('<< /Type /Page /Parent 2 0 R /MediaBox %s /Contents %d 0 R /Resources << %s >> >>' % (media, c, res))
        self.pages.append(p)

    def write(self, path):
        self.objs[0] = '<< /Type /Catalog /Pages 2 0 R >>'
        self.objs[1] = '<< /Type /Pages /Kids [%s] /Count %d >>' % (' '.join('%d 0 R' % p for p in self.pages), len(self.pages))
        out = io.BytesIO()
        out.write(b'%PDF-1.5\n%\xe2\xe3\xcf\xd3\n')
        offs = []
        for i, b in enumerate(self.objs):
            offs.append(out.tell())
            if isinstance(b, str):
                b = b.encode('latin-1')
            out.write(b'%d 0 obj\n' % (i + 1) + b + b'\nendobj\n')
        x = out.tell()
        out.write(b'xref\n0 %d\n0000000000 65535 f \n' % (len(self.objs) + 1))
        for o in offs:
            out.write(b'%010d 00000 n \n' % o)
        out.write(b'trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (len(self.objs) + 1, x))
        open(path, 'wb').write(out.getvalue())


def dash_pdf(path):
    d = Pdf()
    c = []
    c.append('2 w [10 10] 0 d 20 380 m 380 380 l S')
    c.append('[10 10] 5 d 20 368 m 380 368 l S')
    c.append('[4] 0 d 20 356 m 380 356 l S')
    c.append('[12 4 2 4] 3 d 20 344 m 380 344 l S')
    c.append('[6 3 2] 0 d 20 332 m 380 332 l S')                          # odd count: swaps
    c.append('1 J 3 w [0 8] 0 d 20 318 m 380 318 l S 0 J')                # dots
    c.append('1 w [10 10] 1002 d 20 306 m 380 306 l S')                   # a huge phase
    c.append('[8 5] 0 d 30 200 100 90 re S')                               # closed
    c.append('[30 4] 2 d 160 200 m 160 300 250 300 250 200 c 300 150 340 250 360 210 c S')
    c.append('[200 6] 0 d 20 120 m 60 180 100 60 140 180 c 180 60 220 180 260 60 c 300 180 340 60 380 120 c S')
    c.append('q [3 3] 0 d 0.5 w 20 100 m 380 100 l S Q 20 92 m 380 92 l S')
    c.append('4 w [1 0.5] 0 d 20 80 m 380 80 l S')                       # too fine: solid
    c.append('2 w [16 8] 0 d 1 0 0 1 0 0 cm 2 0 0 1 -100 0 cm 70 60 m 240 30 l S')
    c.append('1 j 6 w [20 10] 7 d 40 20 m 120 50 l 200 20 l 280 50 l 360 20 l S')
    d.page('\n'.join(c))
    d.write(path)


def image_xobj(d, w, h, data, cs='/DeviceGray', bpc=8, extra=''):
    return d.stream('/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace %s /BitsPerComponent %d %s'
                    % (w, h, cs, bpc, extra), data)


def jpeg_of(w, h):
    try:
        from PIL import Image
    except Exception:
        return b'\xff\xd8 not a JPEG: the stand-in decoder never reads it \xff\xd9'
    im = Image.frombytes('L', (w, h), ramp_bytes(w, h))
    b = io.BytesIO()
    im.save(b, 'JPEG', quality=95)
    return b.getvalue()


def ramp_pdf(path):
    d = Pdf()
    w, h = 64, 48
    raw = image_xobj(d, w, h, ramp_bytes(w, h))
    dct = image_xobj(d, w, h, jpeg_of(w, h), extra='/Filter /DCTDecode')
    jpx = image_xobj(d, w, h, b'\x00\x00\x00\x0cjP  stub', extra='/Filter /JPXDecode')
    place = 'q 200 0 0 150 100 125 cm /Im Do Q'
    d.page(place, {'Im': raw})
    d.page(place, {'Im': dct})
    d.page(place, {'Im': jpx})
    d.page('q 150 0 0 100 20 280 cm /A Do Q q 150 0 0 100 220 20 cm /B Do Q', {'A': dct, 'B': jpx})
    side = 'q 150 0 0 100 20 280 cm /A Do Q q 120 0 0 80 230 300 cm /B Do Q'
    d.page(side, {'A': dct, 'B': jpx})
    d.page(side, {'A': raw, 'B': raw})
    over = ('q 160 0 0 120 40 200 cm /B Do Q 0.3 g 120 230 80 60 re f q 160 0 0 120 140 160 cm /A Do Q '
            'q 100 0 0 75 60 150 cm /B Do Q 0 g 250 250 30 30 re f')
    d.page(over, {'A': dct, 'B': jpx})
    d.page(over, {'A': raw, 'B': raw})
    helv = d.add('<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>')
    text = ('BT /F1 40 Tf 30 330 Td (Glyphs, then) Tj ET q 200 0 0 150 100 125 cm /Im Do Q '
            'BT /F1 12 Tf 30 60 Td (and text after the image: the cache rebuilt) Tj ET')
    d.page(text, {'Im': dct}, fonts={'F1': helv})
    d.write(path)


def pack(vals, bpc):
    """samples (0..2^bpc-1) packed MSB first, one row"""
    if bpc == 8:
        return bytes(vals)
    if bpc == 16:
        return b''.join(bytes((v >> 8, v & 255)) for v in vals)
    out, acc, n = bytearray(), 0, 0
    for v in vals:
        acc = (acc << bpc) | v
        n += bpc
        while n >= 8:
            out.append((acc >> (n - 8)) & 255)
            n -= 8
    if n:
        out.append((acc << (8 - n)) & 255)
    return bytes(out)


def depth_pdf(path, grey=False):
    """grey=True writes depthg.pdf: every image as the 8-bit DeviceGray one
    PDREND's smode 4 must make of it (PAGEDESK-SPEC 20.22.7.5) - a sample of
    n bits scaled by 255 / (2^n - 1), 16 bits by its high byte, RGB as
    (77 R + 150 G + 29 B) >> 8, CMYK as 255 - min(255, (77 C + 150 M + 29 Y +
    256 K) >> 8), /Decode [1 0] as 255 - v - so the two files must draw to
    the pixel the same"""
    d = Pdf()
    w, h = 40, 24
    xo, c = {}, []
    k = 0

    def put(name, xobj):
        nonlocal k
        xo[name] = xobj
        c.append('q 160 0 0 70 %d %d cm /%s Do Q' % (20 + (k % 2) * 200, 310 - (k // 2) * 85, name))
        k += 1

    def g8(vals):
        return image_xobj(d, w, h, bytes(vals))
    for bpc in (1, 2, 4, 16):
        top = (1 << bpc) - 1
        s = [[ramp(x, y, w, h) * top // 255 for x in range(w)] for y in range(h)]
        if grey:
            put('G%d' % bpc, g8(v * 255 // top if bpc < 16 else v >> 8 for r in s for v in r))
        else:
            put('G%d' % bpc, image_xobj(d, w, h, b''.join(pack(r, bpc) for r in s), bpc=bpc))
    rgb = [(ramp(x, y, w, h), 255 - ramp(x, y, w, h), x * 6) for y in range(h) for x in range(w)]
    if grey:
        put('RGB8', g8((77 * r + 150 * g + 29 * b) >> 8 for r, g, b in rgb))
        put('RGB16', g8((77 * r + 150 * g + 29 * b) >> 8 for r, g, b in rgb))
    else:
        put('RGB8', image_xobj(d, w, h, b''.join(bytes(t) for t in rgb), '/DeviceRGB'))
        put('RGB16', image_xobj(d, w, h, b''.join(bytes((v, 0)) for t in rgb for v in t), '/DeviceRGB', 16))
    cmyk = [(ramp(x, y, w, h) // 2, x * 3, 0, (y * 5) % 128) for y in range(h) for x in range(w)]
    if grey:
        put('CMYK', g8(255 - min(255, (77 * c_ + 150 * m + 29 * y_ + 256 * k_) >> 8) for c_, m, y_, k_ in cmyk))
    else:
        put('CMYK', image_xobj(d, w, h, b''.join(bytes(t) for t in cmyk), '/DeviceCMYK'))
    inv = [[ramp(x, y, w, h) * 15 // 255 for x in range(w)] for y in range(h)]
    if grey:
        put('INV4', g8(255 - v * 17 for r in inv for v in r))
    else:
        put('INV4', image_xobj(d, w, h, b''.join(pack(r, 4) for r in inv), bpc=4, extra='/Decode [1 0]'))
    d.page('\n'.join(c), xo)
    d.write(path)


def mask_pdf(path):
    d = Pdf()
    w, h = 64, 48
    st = d.stream('/Type /XObject /Subtype /Image /Width %d /Height %d /ImageMask true /BitsPerComponent 1 /Filter /DCTDecode'
                  % (w, h), b'stub')
    sm = image_xobj(d, w, h, ramp_bytes(w, h))
    im = image_xobj(d, w, h, ramp_bytes(w, h), extra='/SMask %d 0 R' % sm)
    d.page('0 0 1 rg q 200 0 0 150 100 225 cm /M Do Q q 200 0 0 150 100 25 cm /S Do Q', {'M': st, 'S': im})
    d.write(path)


def forms_pdf(path):
    d = Pdf()
    def flate(dict_txt, text):
        return d.stream(dict_txt + ' /Filter /FlateDecode', zlib.compress(text.encode('latin-1')))
    walk = ' '.join('%d %d l' % (30 + (i * 7) % 340, 30 + (i * 13) % 340) for i in range(100))
    inner = flate('/Type /XObject /Subtype /Form /BBox [0 0 400 400]',
                  '0.6 g 150 150 100 100 re f 1 w 0 G 30 30 m ' + walk + ' S')
    outer = flate('/Type /XObject /Subtype /Form /BBox [0 0 400 400] /Resources << /XObject << /In %d 0 R >> >>' % inner,
                  '0.2 g 40 250 120 60 re f 2 w 0 G 380 30 m ' + walk + ' S /In Do 0 g 250 40 100 40 re f')
    content = '0.8 g 10 10 380 30 re f 0 G 1 w 20 380 m ' + walk + ' S /Fm1 Do 0.4 g 300 300 60 60 re f'
    for page, pre in ((1, ''), (2, 'q 20 0 0 20 10 360 cm BI /W 4 /H 4 /BPC 8 /CS /G ID \x00\x40\x80\xc0'
                                       '\x40\x80\xc0\xff\x80\xc0\xff\x00\xc0\xff\x00\x40 EI Q ')):
        body = (pre + content).encode('latin-1').decode('unicode_escape').encode('latin-1')
        c = d.stream('/Filter /FlateDecode', zlib.compress(body))
        p = d.add('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 400] /Contents %d 0 R '
                  '/Resources << /XObject << /Fm1 %d 0 R >> >> >>' % (c, outer))
        d.pages.append(p)
    # page 3: a number past 16.16 (QA R16) - the token is kept, saturated, and
    # the Incomplete note must NAME it (it was the one unnamed bit)
    c = d.stream('', b'0.5 g 50 50 100 100 re f 40000 w 0 g 200 200 50 50 re f')
    d.pages.append(d.add('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 400] /Contents %d 0 R >>' % c))
    d.write(path)


def hair_pdf(path):
    """QA R11: one-pixel strokes. A fan of 1 w lines, one every 7 degrees
    from 30 to 160 pt out of (200, 200), and an axis-aligned square: every
    row of a steep line and every column of a shallow one must hold a pixel,
    and the square's sides must stay one pixel thick."""
    import math
    d = Pdf()
    c = ['1 w']
    for k in range(0, 360, 7):
        a = math.radians(k + 0.37)
        c.append('%.3f %.3f m %.3f %.3f l S' % (200 + 30 * math.cos(a), 200 + 30 * math.sin(a),
                                                200 + 160 * math.cos(a), 200 + 160 * math.sin(a)))
    c.append('5 375 m 25 375 l 25 395 l 5 395 l h S')
    d.page('\n'.join(c))
    d.write(path)


def small_pdf(path):
    """QA R15: hundreds of shapes a few pixels tall - knobs, dots, a shading's
    levels inside a small clip - which PDREND keeps as RECT runs rather than
    edge lists. Page 1 is circles (nonzero), rings (even-odd, a hole) and
    clipped stripes at three sizes; the A/B (PDR_NOSMALL) must agree to the
    pixel and need more passes in a small list. Page 2 is 96 shadings in
    clips of 3 to 9 points."""
    d = Pdf()
    c = []
    k = 0.5523
    def circle(x, y, r):
        return ('%.2f %.2f m %.2f %.2f %.2f %.2f %.2f %.2f c %.2f %.2f %.2f %.2f %.2f %.2f c '
                '%.2f %.2f %.2f %.2f %.2f %.2f c %.2f %.2f %.2f %.2f %.2f %.2f c h ') % (
            x + r, y, x + r, y + k * r, x + k * r, y + r, x, y + r, x - k * r, y + r, x - r, y + k * r, x - r, y,
            x - r, y - k * r, x - k * r, y - r, x, y - r, x + k * r, y - r, x + r, y - k * r, x + r, y)
    for i in range(16):
        for j in range(12):
            x, y = 12 + i * 23.5, 12 + j * 31.0
            g = ((i * 7 + j * 3) % 10) / 10.0
            r = (0.6, 0.9, 1.3)[(i + j) % 3]
            if (i + j) % 4 == 0:
                c.append('%.1f g %s %s f*' % (g, circle(x, y, r * 1.6), circle(x, y, r * 0.7)))
            elif (i + j) % 4 == 1:
                c.append('q %.1f %.1f 3 3 re W n' % (x - 1.5, y - 1.5))
                for b in range(6):
                    c.append('%.2f g %.1f %.1f %.1f 4 re f' % (b / 6.0, x - 2 + b * 0.7, y - 2, 0.7))
                c.append('Q')
            else:
                c.append('%.1f g %s f' % (g, circle(x, y, r)))
    d.page('\n'.join(c))
    # page 2: 96 knobs, each an axial /Sh in its own clip a few pixels across
    # (SYNTH.PDF's buttons): a pass must not ask PDXG for a knob whose clip
    # misses its rows, and a knob is cut into no more bands than it has
    # pixels (the harness's PDR_XSHADE stands in for PDXG)
    fn = d.add('<< /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [1] /N 1 >>')
    sh = d.add('<< /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 0 1] /Function %d 0 R /Extend [true true] >>' % fn)
    c = []
    for i in range(12):
        for j in range(8):
            x, y, sz = 20 + i * 30.0, 20 + j * 45.0, (3, 5, 9)[(i + j) % 3]
            c.append('q %.1f %.1f %d %d re W n %d 0 0 %d %.1f %.1f cm /S0 sh Q' % (x, y, sz, sz, sz, sz, x, y))
    cc = d.stream('', '\n'.join(c))
    d.pages.append(d.add('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 400] /Contents %d 0 R '
                         '/Resources << /Shading << /S0 %d 0 R >> >> >>' % (cc, sh)))
    d.write(path)


def park_pdf(path):
    """The occupant park (20.22.7.12): a Flate page whose shadings make PDXG
    read a Flate stream of its own (a sampled function, object 3, which the
    harness's PDR_XSHADE_OBJ=3 stand-in opens). With ONE Flate slot the page's
    stream holds it, PDXG's open is refused, and the frame is parked, asked
    again and replayed; rectangles after each `sh` prove it resumes at the
    operator after. Page 2 is the same inside a Flate Form. Page 3 is a Flate
    image with a Flate /SMask, which PDX8 reads while the core reads the image."""
    d = Pdf()
    samples = bytes(range(0, 256, 4))
    fn = d.stream('/FunctionType 0 /Domain [0 1] /Range [0 1] /Size [64] /BitsPerSample 8 /Filter /FlateDecode',
                  zlib.compress(samples))
    assert fn == 3
    sh = d.add('<< /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 0 1] /Function %d 0 R >>' % fn)
    c = []
    for i in range(6):
        for j in range(5):
            x, y = 30 + i * 60.0, 40 + j * 70.0
            c.append('q %.1f %.1f 30 30 re W n 30 0 0 30 %.1f %.1f cm /S0 sh Q 0 g %.1f %.1f 30 4 re f' % (x, y, x, y, x, y + 34))
    body = '\n'.join(c)
    res = '/Shading << /S0 %d 0 R >>' % sh
    cc = d.stream('/Filter /FlateDecode', zlib.compress(body.encode('latin-1')))
    d.pages.append(d.add('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 400] /Contents %d 0 R /Resources << %s >> >>' % (cc, res)))
    fm = d.stream('/Type /XObject /Subtype /Form /BBox [0 0 400 400] /Resources << %s >> /Filter /FlateDecode' % res,
                  zlib.compress(body.encode('latin-1')))
    c2 = d.stream('/Filter /FlateDecode', zlib.compress(b'0.5 g 5 5 20 20 re f /Fm0 Do 0 g 370 370 20 20 re f'))
    d.pages.append(d.add('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 400] /Contents %d 0 R '
                         '/Resources << /XObject << /Fm0 %d 0 R >> >> >>' % (c2, fm)))
    # page 3: a Flate image with a Flate /SMask over black stripes - PDX8 reads the
    # mask while the core reads the image: two Flate streams at band time
    w = h = 64
    img = bytes(((x * 4) & 0xFF) | 0x80 for y in range(h) for x in range(w))
    msk = bytes(min(255, (y * 4)) for y in range(h) for x in range(w))
    sm = d.stream('/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceGray /BitsPerComponent 8 '
                  '/Filter /FlateDecode' % (w, h), zlib.compress(msk))
    im = d.stream('/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceGray /BitsPerComponent 8 '
                  '/SMask %d 0 R /Filter /FlateDecode' % (w, h, sm), zlib.compress(img))
    c3 = d.stream('/Filter /FlateDecode', zlib.compress(
        b' '.join(b'0 g 50 %d 300 10 re f' % (60 + k * 20) for k in range(15)) + b' q 300 0 0 300 50 50 cm /Im0 Do Q'))
    d.pages.append(d.add('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 400] /Contents %d 0 R '
                         '/Resources << /XObject << /Im0 %d 0 R >> >> >>' % (c3, im)))
    d.write(path)


def noap_pdf(path):
    """Annotations with NO appearance stream (QA R6b, PAGEDESK-SPEC 20.12.2): a
    viewer draws them from the dictionary, as MuPDF does. Page 1: two Text notes
    (one yellow by /C, one with no /C), a Square with /C, /IC and /BS /W 3, a
    Circle with /Border [0 0 2], a Line with /L, a Highlight over a black bar
    (it goes first: the bar must stay black on top), an Underline and a
    StrikeOut, and three that must NOT draw: a Hidden Square (/F 2), a Square
    with neither /C nor /IC and a Popup. Page 2: 80 Text notes in a column
    grid, BIG_OS's shape."""
    d = Pdf()
    ann = []
    def a(body):
        ann.append(d.add('<< /Type /Annot %s >>' % body))
    a('/Subtype /Text /Rect [40 330 60 350] /C [1 1 0] /Contents (yellow)')
    a('/Subtype /Text /Rect [80 330 100 350] /Contents (no colour)')
    a('/Subtype /Square /Rect [40 200 140 280] /C [1 0 0] /IC [0 0 1] /BS << /W 3 >>')
    a('/Subtype /Circle /Rect [180 200 300 280] /C [0 0.6 0] /Border [0 0 2]')
    a('/Subtype /Line /Rect [40 100 360 180] /L [50 110 350 170] /C [0 0 0] /BS << /W 2 >>')
    a('/Subtype /Highlight /Rect [190 320 330 350] /QuadPoints [195 345 330 345 195 325 330 325] /C [1 1 0]')
    a('/Subtype /Underline /Rect [40 40 160 70] /QuadPoints [40 70 160 70 40 50 160 50] /C [1 0 0]')
    a('/Subtype /StrikeOut /Rect [200 40 360 70] /QuadPoints [200 70 360 70 200 50 360 50] /C [0 0 1]')
    a('/Subtype /Square /Rect [320 200 380 280] /C [0 0 0] /F 2')
    a('/Subtype /Square /Rect [320 290 380 310]')
    a('/Subtype /Popup /Rect [300 300 390 390]')
    c = d.stream('', '0 g 200 330 120 10 re f 0.5 g 45 55 110 10 re f 205 55 150 10 re f')
    d.pages.append(d.add('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 400] /Contents %d 0 R /Annots [%s] >>'
                         % (c, ' '.join('%d 0 R' % o for o in ann))))
    ann2 = []
    for i in range(80):
        x, y = 20 + (i // 16) * 75, 360 - (i % 16) * 22
        ann2.append(d.add('<< /Type /Annot /Subtype /Text /Rect [%d %d %d %d] /C [1 1 0] /NM (n%d) >>' % (x, y, x + 20, y + 20, i)))
    c2 = d.stream('', '')
    d.pages.append(d.add('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 400] /Contents %d 0 R /Annots [%s] >>'
                         % (c2, ' '.join('%d 0 R' % o for o in ann2))))
    d.write(path)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    dash_pdf(os.path.join(out, 'dash.pdf'))
    ramp_pdf(os.path.join(out, 'ramp.pdf'))
    depth_pdf(os.path.join(out, 'depth.pdf'))
    depth_pdf(os.path.join(out, 'depthg.pdf'), grey=True)
    mask_pdf(os.path.join(out, 'mask.pdf'))
    forms_pdf(os.path.join(out, 'forms.pdf'))
    hair_pdf(os.path.join(out, 'hair.pdf'))
    small_pdf(os.path.join(out, 'small.pdf'))
    park_pdf(os.path.join(out, 'park.pdf'))
    noap_pdf(os.path.join(out, 'noap.pdf'))
    print('os88pdfx_rcore: 10 fixtures in %s' % out)


if __name__ == '__main__':
    main()
