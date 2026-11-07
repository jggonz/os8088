#!/usr/bin/env python3
"""Writes PDREND's own test PDF (hosttest/pdr_test.pdf) - uncompressed, classic
xref, every feature the renderer claims in one place.  Independent readers
(mutool, pdftoppm) must open it; the harness compares against them."""
import sys, math
objs = []
def add(body):
    objs.append(body); return len(objs)
def stream(d, data):
    if isinstance(data, str): data = data.encode('latin-1')
    return d + b'\nstream\n' + data + b'\nendstream' if isinstance(d, bytes) else None

def S(dict_txt, data):
    if isinstance(data, str): data = data.encode('latin-1')
    return ('<< %s /Length %d >>\nstream\n' % (dict_txt, len(data))).encode() + data + b'\nendstream'

pages = []
fonts = {}
def font(name, base, enc='/WinAnsiEncoding', extra=''):
    return add('<< /Type /Font /Subtype /Type1 /BaseFont /%s /Encoding %s %s >>' % (base, enc, extra))

# reserve 1 = catalog, 2 = pages
add(None); add(None)
F1 = font('F1', 'Helvetica'); F2 = font('F2', 'Times-Roman'); F3 = font('F3', 'Courier')
F4 = font('F4', 'Helvetica-Bold'); F5 = font('F5', 'Times-Italic'); F6 = font('F6', 'Courier-Bold')
F7 = add('<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding << /Type /Encoding /BaseEncoding /WinAnsiEncoding /Differences [ 65 /bullet /endash /emdash ] >> >>')
F8 = add('<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>')           # no /Encoding: StandardEncoding
F9 = add('<< /Type /Font /Subtype /TrueType /BaseFont /ABCDEF+Calibri /Encoding /WinAnsiEncoding >>')   # substituted
FONTRES = '/Font << /F1 %d 0 R /F2 %d 0 R /F3 %d 0 R /F4 %d 0 R /F5 %d 0 R /F6 %d 0 R /F7 %d 0 R /F8 %d 0 R /F9 %d 0 R >>' % (F1, F2, F3, F4, F5, F6, F7, F8, F9)

def page(content, extra='', res='', media='[0 0 400 300]'):
    c = add(S('', content))
    p = add('<< /Type /Page /Parent 2 0 R /MediaBox %s /Contents %d 0 R /Resources << %s %s >> %s >>' % (media, c, FONTRES, res, extra))
    pages.append(p); return p

# ---- 1. text ------------------------------------------------------------------
t = """BT /F1 14 Tf 20 270 Td (Helvetica 14: The quick brown fox) Tj ET
BT /F2 14 Tf 20 250 Td (Times 14: The quick brown fox) Tj ET
BT /F3 12 Tf 20 232 Td (Courier 12: The quick brown fox) Tj ET
BT /F4 10 Tf 20 214 Td [(Kerned) -300 (TJ) 200 (text) 500 (with) -120 (gaps)] TJ ET
BT /F5 12 Tf 20 198 Td 2 Tc (Tc=2 spacing) Tj 0 Tc 8 Tw ( Tw=8 word spacing here) Tj 0 Tw ET
BT /F1 12 Tf 20 182 Td 50 Tz (Tz 50 narrow) Tj 150 Tz ( Tz 150 wide) Tj 100 Tz ET
BT /F1 10 Tf 14 TL 20 164 Td (Line one with T-star) Tj T* (Line two via T-star) Tj (Line three via quote) ' 1 2 (Four via dquote) " ET
BT /F1 12 Tf 20 100 Td (Escapes: \\(paren\\) \\\\ back \\101\\102\\103 \\351 caf\\351) Tj ET
BT /F1 12 Tf 20 84 Td (Rise ) Tj 4 Ts (up) Tj -4 Ts (down) Tj 0 Ts ( base) Tj ET
BT /F7 12 Tf 20 68 Td (ABC Differences: A B C) Tj ET
BT /F8 12 Tf 20 52 Td (Standard 'quote' `grave`) Tj ET
BT /F9 12 Tf 20 36 Td (Substituted font Calibri) Tj ET
BT /F1 6 Tf 250 270 Td (Six point text at 6pt) Tj ET
BT /F1 8 Tf 250 256 Td (Eight point text at 8pt) Tj ET
BT /F1 24 Tf 250 228 Td (Big 24) Tj ET
BT /F2 36 Tf 250 190 Td (Big36) Tj ET
BT /F1 1 Tf 12 0 0 12 250 170 Tm (Tm scaled 12) Tj ET
BT /F1 12 Tf 0.866 0.5 -0.5 0.866 250 100 Tm (Rotated 30 degrees) Tj ET
BT /F1 12 Tf 1 0 0 1 250 60 Tm 0.6 g (Gray text) Tj 1 0 0 rg ( red) Tj 0 g 3 Tr ( hidden) Tj 0 Tr ( visible) Tj ET
"""
page(t)

# ---- 2. paths -------------------------------------------------------------------
p = """0.8 g 20 200 100 80 re f
0.4 g 60 220 100 60 re f
0 G 4 w 20 200 100 80 re S
1 0 0 RG 0.5 g 2 w 160 200 m 220 280 l 280 200 l h B
0 0 1 rg 300 200 80 80 re f 1 1 0 rg 320 220 40 40 re f
0 G 1 w 20 180 m 380 180 l S
0.5 w 20 175 m 380 175 l S
0 w 20 170 m 380 170 l S
8 w 20 150 m 80 150 l 80 120 l S
1 J 1 j 8 w 100 150 m 160 150 l 160 120 l S
2 J 2 j 8 w 180 150 m 240 150 l 240 120 l S
0 J 0 j 1 w [4 3] 0 d 20 110 m 380 110 l S [] 0 d
0 0 0 RG 1 w 20 60 m 20 90 100 90 100 60 c S
0.9 g 140 60 m 140 100 200 100 200 60 c h f
0 g 1 w 230 90 m 270 30 l 310 90 l 220 50 l 320 50 l h S
0 g 340 40 m 370 90 l 390 40 l 330 70 l 380 70 l h f*
q 20 10 60 40 re W n 0.2 g 0 0 400 100 re f 1 g 30 20 20 20 re f Q
q 100 10 m 140 50 l 180 10 l h W n 0 g 90 0 100 60 re f Q
"""
page(p)

# ---- 3. forms and images --------------------------------------------------------
chk = bytes([0b10101010 if (r // 2) % 2 == 0 else 0b01010101 for r in range(16) for _ in range(2)])
I1 = add(S('/Type /XObject /Subtype /Image /Width 16 /Height 16 /BitsPerComponent 1 /ColorSpace /DeviceGray', chk))
grad = bytes([(x * 4 + y * 2) & 255 for y in range(64) for x in range(64)])
I2 = add(S('/Type /XObject /Subtype /Image /Width 64 /Height 64 /BitsPerComponent 8 /ColorSpace /DeviceGray', grad))
rgb = bytes(v for y in range(32) for x in range(32) for v in (x * 8, y * 8, 128))
I3 = add(S('/Type /XObject /Subtype /Image /Width 32 /Height 32 /BitsPerComponent 8 /ColorSpace /DeviceRGB', rgb))
msk = bytes([0xF0 if (r // 2) % 2 else 0x0F for r in range(8)])
I4 = add(S('/Type /XObject /Subtype /Image /Width 8 /Height 8 /BitsPerComponent 1 /ImageMask true', msk))
inv = add(S('/Type /XObject /Subtype /Image /Width 16 /Height 16 /BitsPerComponent 1 /ColorSpace /DeviceGray /Decode [1 0]', chk))
FI = add(S('/Type /XObject /Subtype /Form /BBox [0 0 60 60] /Matrix [1 0 0 1 0 0] /Resources << >>',
           '0.7 g 0 0 60 60 re f 0 g 5 5 50 50 re S 1 0 0 rg 20 20 100 100 re f'))
FO = add(S('/Type /XObject /Subtype /Form /BBox [0 0 100 100] /Matrix [1 0 0 1 0 0] /Resources << /XObject << /In %d 0 R >> >>' % FI,
           '0.9 g 0 0 100 100 re f q 1 0 0 1 20 20 cm /In Do Q 0 G 0 0 100 100 re S'))
c = """q 80 0 0 80 20 200 cm /Im1 Do Q
q 80 0 0 80 110 200 cm /Im2 Do Q
q 80 0 0 80 200 200 cm /Im3 Do Q
0 0 0 rg q 60 0 0 60 290 210 cm /Im4 Do Q
q 60 0 0 60 20 130 cm /Im5 Do Q
q 1 0 0 1 100 100 cm /Fo Do Q
q 0.7 0.7 -0.7 0.7 280 60 cm /Fo Do Q
q 30 0 0 30 20 20 cm /Im2 Do Q
q -80 0 0 80 160 20 cm /Im1 Do Q
BT /F1 9 Tf 210 130 Td (forms nested, images 1/8 bit, stencil) Tj ET
"""
page(c, res='/XObject << /Im1 %d 0 R /Im2 %d 0 R /Im3 %d 0 R /Im4 %d 0 R /Im5 %d 0 R /Fo %d 0 R >>' % (I1, I2, I3, I4, inv, FO))

# ---- 4. cropped and rotated ------------------------------------------------------
cr = """0.9 g 100 100 200 200 re f 0 G 2 w 100 100 200 200 re S 0 g
BT /F1 18 Tf 120 250 Td (Crop origin 100,100) Tj ET
BT /F2 12 Tf 120 200 Td (Rotated and cropped page) Tj ET
1 0 0 rg 100 100 30 30 re f 0 0 1 rg 270 270 30 30 re f
0 g 100 100 m 300 300 l S
"""
page(cr, extra='/Rotate 90 /CropBox [100 100 300 300]', media='[0 0 400 400]')

# ---- 5. many small shapes (display-list overflow) --------------------------------
parts = []
for i in range(40):
    for j in range(60):
        g = ((i * 7 + j * 13) % 10) / 10.0
        parts.append('%.1f g %d %d 8 4 re f' % (g, 10 + i * 9, 10 + j * 4.5))
parts.append('0 g BT /F1 10 Tf 20 285 Td (Many small rectangles) Tj ET')
page('\n'.join(parts))

# ---- finish ----------------------------------------------------------------------
objs[0] = '<< /Type /Catalog /Pages 2 0 R >>'
objs[1] = '<< /Type /Pages /Count %d /Kids [%s] >>' % (len(pages), ' '.join('%d 0 R' % p for p in pages))
out = bytearray(b'%PDF-1.4\n%\xe2\xe3\xcf\xd3\n')
offs = []
for i, o in enumerate(objs):
    offs.append(len(out))
    out += ('%d 0 obj\n' % (i + 1)).encode() + (o if isinstance(o, bytes) else o.encode('latin-1')) + b'\nendobj\n'
x = len(out)
out += ('xref\n0 %d\n0000000000 65535 f \n' % (len(objs) + 1)).encode()
for o in offs: out += ('%010d 00000 n \n' % o).encode()
out += ('trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (len(objs) + 1, x)).encode()
open(sys.argv[1], 'wb').write(out)
print('wrote', sys.argv[1], len(pages), 'pages')
