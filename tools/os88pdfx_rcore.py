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
               side, sharing rows (one slot: 20.22.7.4's limit). The
               host's stand-in decoder (hosttest/pdr_xmod.c) delivers the ramp
               whatever the bytes, so pages 1 and 2 must draw identically
  depth.pdf    the ramp at 1, 2, 4, 16 bits DeviceGray, 8 and 16-bit RGB,
               8-bit CMYK, and a 4-bit image under /Decode [1 0]: smode 4
  depthg.pdf   the same images as the 8-bit DeviceGray PDREND must make of
               them: depth.pdf must draw to the pixel as this does
  mask.pdf     a /DCTDecode image with /ImageMask true (a CCITT-like stencil
               through the decoder's 1-bit rows), and an image with /SMask

The ramp is a function of the pixel: (x * 200) / (w - 1) + (y * 55) / (h - 1),
the same as hosttest/pdr_xmod.c's.
"""
import io
import os
import sys


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

    def page(self, content, xobjs=None, media='[0 0 400 400]'):
        c = self.stream('', content)
        res = ''
        if xobjs:
            res = '/XObject << %s >>' % ' '.join('/%s %d 0 R' % (k, v) for k, v in xobjs.items())
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
    d.page('q 150 0 0 100 20 280 cm /A Do Q q 120 0 0 80 230 300 cm /B Do Q', {'A': dct, 'B': jpx})
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


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    dash_pdf(os.path.join(out, 'dash.pdf'))
    ramp_pdf(os.path.join(out, 'ramp.pdf'))
    depth_pdf(os.path.join(out, 'depth.pdf'))
    depth_pdf(os.path.join(out, 'depthg.pdf'), grey=True)
    mask_pdf(os.path.join(out, 'mask.pdf'))
    print('os88pdfx_rcore: 5 fixtures in %s' % out)


if __name__ == '__main__':
    main()
