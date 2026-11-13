#!/usr/bin/env python3
"""pdw_foreign.py OUT.PDF - the foreign file of Insert's tests (PAGEDESK-SPEC
20.16): four pages whose object numbers (1-16) and font names (/F1 /F2) COLLIDE
with samples/PAGES.PDF's, but whose fonts are different (Courier-Bold, Times).

  - a nested tree: pages 1-2 inherit /MediaBox, /Resources and /Rotate from
    their /Pages node, pages 3-4 carry their own
  - ONE font (obj 12, /F1) and ONE form XObject (obj 9, which draws with that
    font) shared by every page: inserting several pages must copy each once
  - a Text annotation with a Popup (/Parent <-> /Popup, /P -> the page: cycles)
    and a Link whose /Dest names page 3 and whose /A is a URI, on page 2
  - a content stream whose /Length is an indirect object
"""
import sys

objs = {}
def put(n, body): objs[n] = body
def stream(txt, length_ref=None):
    ln = length_ref if length_ref else str(len(txt))
    return "<< /Length %s >>\nstream\n%s\nendstream" % (ln, txt)
def content(k, w):
    return ("q 0.9 0.2 0.2 rg 30 30 %d 40 re f Q\n/Fm1 Do\n"
            "BT /F1 30 Tf 40 %d Td (FOREIGN %d) Tj ET\nBT /F2 14 Tf 40 60 Td (Times line %d) Tj ET\n" % (w // 2, w // 3, k, k))

put(1, "<< /Type /Catalog /Pages 2 0 R >>")
put(2, "<< /Type /Pages /Kids [3 0 R 8 0 R] /Count 4 >>")
put(3, "<< /Type /Pages /Parent 2 0 R /Kids [4 0 R 5 0 R] /Count 2 /MediaBox [0 0 400 300] /Resources 10 0 R /Rotate 90 >>")
put(4, "<< /Type /Page /Parent 3 0 R /Contents 14 0 R >>")
put(5, "<< /Type /Page /Parent 3 0 R /Contents 15 0 R /Annots [6 0 R 7 0 R 13 0 R] >>")
put(6, "<< /Type /Annot /Subtype /Text /Rect [300 200 320 218] /Contents (foreign note) /NM (F-1) /P 5 0 R /Popup 7 0 R /F 4 /T (Bob) >>")
put(7, "<< /Type /Annot /Subtype /Popup /Rect [320 200 390 260] /Parent 6 0 R /Open false /F 28 >>")
put(8, "<< /Type /Pages /Parent 2 0 R /Kids [11 0 R 16 0 R] /Count 2 >>")
FORM = "BT /F1 12 Tf 5 5 Td (shared form) Tj ET q 1 0 0 RG 2 2 96 96 re S Q"
put(9, "<< /Type /XObject /Subtype /Form /BBox [0 0 100 100] /Resources << /Font << /F1 12 0 R >> >> /Length %d >>\nstream\n%s\nendstream" % (len(FORM), FORM))
put(10, "<< /Font << /F1 12 0 R /F2 17 0 R >> /XObject << /Fm1 9 0 R >> >>")
put(11, "<< /Type /Page /Parent 8 0 R /MediaBox [0 0 500 220] /Resources 10 0 R /Contents 18 0 R >>")
put(12, "<< /Type /Font /Subtype /Type1 /BaseFont /Courier-Bold >>")
put(13, "<< /Type /Annot /Subtype /Link /Rect [40 40 200 70] /Border [0 0 1] /Dest [16 0 R /Fit] /A << /S /URI /URI (http://example.com/) >> >>")
put(14, stream(content(1, 400)))
put(15, stream(content(2, 400)))
put(16, "<< /Type /Page /Parent 8 0 R /MediaBox [0 0 300 300] /Rotate 180 /Resources 10 0 R /Contents 19 0 R >>")
put(17, "<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman >>")
c3 = content(3, 500)
put(18, "<< /Length 20 0 R >>\nstream\n%s\nendstream" % c3)
put(19, stream(content(4, 300)))
put(20, str(len(c3)))

out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
off = {}
for n in sorted(objs):
    off[n] = len(out)
    out += ("%d 0 obj\n%s\nendobj\n" % (n, objs[n])).encode("latin1")
x = len(out)
out += ("xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)).encode()
for n in sorted(objs):
    out += ("%010d 00000 n \n" % off[n]).encode()
out += ("trailer\n<< /Size %d /Root 1 0 R /ID [<AABBCCDDEEFF00112233445566778899><AABBCCDDEEFF00112233445566778899>] >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, x)).encode()
open(sys.argv[1], "wb").write(out)
