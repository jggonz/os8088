#!/usr/bin/env python3
"""pdw_nest.py OUT.PDF - a nested page tree for the page-operation tests
(PAGEDESK-SPEC 20.11): two levels of /Pages nodes whose /MediaBox, /Resources
and /Rotate are INHERITED (the only place they are written), an annotation
with a popup, and pages of different sizes - the shape a reordering writer
must materialise attributes for."""
import sys

objs = {}
def put(n, body): objs[n] = body
def stream(txt): return "<< /Length %d >>\nstream\n%s\nendstream" % (len(txt), txt)
def page_content(label, x, y):
    return "0.2 0.4 0.8 rg 20 20 100 60 re f\nBT /F1 96 Tf %d %d Td (%s) Tj ET\n" % (x, y, label)

put(1, "<< /Type /Catalog /Pages 2 0 R >>")
put(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 4 >>")
put(3, "<< /Type /Pages /Parent 2 0 R /Kids [5 0 R 6 0 R] /Count 2 /MediaBox [0 0 300 400] /Resources 9 0 R /Rotate 90 >>")
put(4, "<< /Type /Pages /Parent 2 0 R /Kids [7 0 R 8 0 R] /Count 2 /MediaBox [0 0 500 200] /Resources << /Font << /F1 10 0 R >> >> >>")
put(5, "<< /Type /Page /Parent 3 0 R /Contents 11 0 R >>")
put(6, "<< /Type /Page /Parent 3 0 R /Contents 12 0 R /Annots [13 0 R 14 0 R] >>")
put(7, "<< /Type /Page /Parent 4 0 R /Contents 15 0 R >>")
put(8, "<< /Type /Page /Parent 4 0 R /Contents 16 0 R /Rotate 180 >>")
put(9, "<< /Font << /F1 10 0 R >> >>")
put(10, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
put(11, stream(page_content("A", 60, 150)))
put(12, stream(page_content("B", 60, 150)))
put(13, "<< /Type /Annot /Subtype /Text /Rect [200 300 220 318] /Contents (note on B) /NM (orig-B-1) /P 6 0 R /Popup 14 0 R /F 4 /T (Alice) >>")
put(14, "<< /Type /Annot /Subtype /Popup /Rect [220 300 290 360] /Parent 13 0 R /Open false /F 28 >>")
put(15, stream(page_content("C", 160, 60)))
put(16, stream(page_content("D", 160, 60)))

out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
off = {}
for n in sorted(objs):
    off[n] = len(out)
    out += ("%d 0 obj\n%s\nendobj\n" % (n, objs[n])).encode("latin1")
x = len(out)
out += ("xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)).encode()
for n in sorted(objs):
    out += ("%010d 00000 n \n" % off[n]).encode()
out += ("trailer\n<< /Size %d /Root 1 0 R /ID [<00112233445566778899AABBCCDDEEFF><00112233445566778899AABBCCDDEEFF>] >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, x)).encode()
open(sys.argv[1], "wb").write(out)
