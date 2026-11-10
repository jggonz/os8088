#!/usr/bin/env python3
"""os8088 - apps/pagedesk/hosttest/pde_fixtures.py

PDENG's OWN fixtures (PAGEDESK-SPEC 20.2): small hand-built PDFs for the cases
the shared samples (tools/os88pdfgen.py) do not isolate - ASCIIHex and
RunLength pipelines, a three-revision incremental update with a freed object
and a generation mismatch, escapes in strings and names, outlines and
annotations with known contents, a Form XObject with its own resources, a file
of 10,000 one-object xref subsections, and negatives whose xref is CORRECT so
the engine reaches the defect under test. Deterministic; stdlib only.
"""
import os, struct, sys


class P:
    """A PDF being written. Objects are added with put/stream; xref() writes a
    classic table for the objects added since the last one."""

    def __init__(self, ver=b"1.4"):
        self.b = bytearray(b"%PDF-" + ver + b"\n%\xe2\xe3\xcf\xd3\n")
        self.off = {}                       # n -> (offset, gen)
        self.free = []                      # [(n, gen_next)] to list as free in the next table
        self.last = {}                      # objects the NEXT xref() lists
        self.xrefs = []

    def put(self, n, body, gen=0):
        self.off[n] = (len(self.b), gen)
        self.last[n] = (len(self.b), gen, "n")
        self.b += b"%d %d obj\n" % (n, gen) + body + b"\nendobj\n"

    def stream(self, n, d, data, length=None, gen=0, eol=b"\n", endstream=b"\nendstream"):
        ln = len(data) if length is None else length
        self.put(n, b"<< " + d + (b" /Length " + (ln if isinstance(ln, bytes) else b"%d" % ln)) + b" >>\nstream" + eol
                 + data + endstream, gen)

    def drop(self, n, next_gen):
        self.last[n] = (0, next_gen, "f")

    def xref(self, root, extra=b"", size=None, prev=None, ids=True, per_object=False, order=None):
        pos = len(self.b)
        ents = dict(self.last)
        if not self.xrefs:                  # object 0 heads the first table
            ents[0] = (0, 65535, "f")
        nums = sorted(ents) if order is None else order
        out = bytearray(b"xref\n")
        if per_object:
            groups = [[n] for n in nums]
        else:
            groups, run = [], []
            for n in nums:
                if run and n != run[-1] + 1:
                    groups.append(run); run = []
                run.append(n)
            if run:
                groups.append(run)
        for g in groups:
            out += b"%d %d\n" % (g[0], len(g))
            for n in g:
                o, gen, t = ents[n]
                out += b"%010d %05d %s \n" % (o, gen, t.encode())
        sz = size if size is not None else max(max(ents), max(self.off, default=0)) + 1
        tr = b"trailer\n<< /Size %d /Root %d 0 R" % (sz, root)
        if prev is not None:
            tr += b" /Prev %d" % prev
        if ids:
            tr += b" /ID [<00112233445566778899AABBCCDDEEFF> <00112233445566778899AABBCCDDEEFF>]"
        tr += extra + b" >>\n"
        self.b += out + tr + b"startxref\n%d\n%%%%EOF\n" % pos
        self.xrefs.append(pos)
        self.last = {}
        return pos


def hexenc(b, width=16):
    h = b.hex().upper().encode()
    return b"\n".join(h[i:i + width] for i in range(0, len(h), width)) + b">"


def rle(b):
    """RunLengthDecode: literal runs and repeat runs."""
    out, i = bytearray(), 0
    while i < len(b):
        j = i
        while j + 1 < len(b) and b[j + 1] == b[i] and j - i < 127:
            j += 1
        if j > i:
            out += bytes([257 - (j - i + 1), b[i]]); i = j + 1
        else:
            k = i + 1
            while k < len(b) and k - i < 128 and not (k + 1 < len(b) and b[k] == b[k + 1]):
                k += 1
            out += bytes([k - i - 1]) + b[i:k]; i = k
    return bytes(out) + b"\x80"


FONT = b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
CONTENT = b"BT /F1 12 Tf 72 700 Td (PDENG %s) Tj ET\n"


def basic(name_extra=b""):
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>")
    p.stream(4, b"", CONTENT % b"basic")
    p.put(5, FONT)
    p.xref(1)
    return p


def build(d):
    os.makedirs(d, exist_ok=True)
    files, exp = {}, {}

    def save(name, p):
        files[name] = bytes(p.b if isinstance(p, P) else p)

    # ---- filters: hex, run length, pipelines, abbreviations, an indirect chain
    plain = [CONTENT % (b"stream %d" % i) + bytes(range(32, 120)) * (i + 1) + b"\n" for i in range(5)]
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 200] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R /Contents [10 0 R 11 0 R 12 0 R 13 0 R 14 0 R] >>")
    p.stream(10, b"/Filter /ASCIIHexDecode", hexenc(plain[0]))
    p.stream(11, b"/Filter /RunLengthDecode", rle(plain[1]))
    p.stream(12, b"/Filter [/ASCIIHexDecode /RunLengthDecode]", hexenc(rle(plain[2])))
    p.stream(13, b"/Filter /AHx /DecodeParms null", hexenc(plain[3], 7))
    p.stream(14, b"/Filter 15 0 R", rle(plain[4]))
    p.put(15, b"[/RunLengthDecode]")
    p.xref(1)
    save("filters.pdf", p)
    exp["filters.pdf"] = {"contents": plain}

    # ---- incremental: three revisions, a freed object, a generation mismatch
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R 6 0 R] /Count 2 /MediaBox [0 0 612 792] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>")
    p.stream(4, b"", CONTENT % b"revision 1")
    p.put(5, b"<< /Note (old) >>")
    p.put(6, b"<< /Type /Page /Parent 2 0 R /Contents 7 0 R >>")
    p.stream(7, b"", CONTENT % b"page two revision 1")
    x1 = p.xref(1)
    p.stream(4, b"", CONTENT % b"revision 2 is longer than the first one", )
    p.drop(5, 1)
    x2 = p.xref(1, prev=x1, size=8)
    p.stream(7, b"", CONTENT % b"page two revision 3")
    p.put(8, b"<< /Late true >>")
    p.xref(1, prev=x2, size=9)
    save("incr.pdf", p)
    exp["incr.pdf"] = {"contents": [CONTENT % b"revision 2 is longer than the first one", CONTENT % b"page two revision 3"],
                       "nrev": 3, "pages": 2}

    # ---- strings, names, numbers through DICT_GET
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R /Lang (en-GB) >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 612 792] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R /Contents 4 0 R /Rotate 270 >>")
    p.stream(4, b"", CONTENT % b"strings")
    p.put(5, b"<< /S1 (a\\(b\\)c (nested) \\101\\102\\0603 \\\\ \\n\\\nx) /S2 <48656C6C6F2> /S3 () /N1 /A#20B#23 /N2 /Plain"
             b" /I -17 /R 3.25 /R2 -.5 /R3 4. /T true /F false /Z null /A [1 2 [3 4] << /K (v) >> 5 0 R] /D << /X 1 /Y << /Z 9 >> >>"
             b" /REF 4 0 R /BIG 84478 /HUGE 99999999999 /L (" + b"x" * 300 + b") >>")
    p.xref(1)
    save("values.pdf", p)

    # ---- outline with nested items, a name destination and a UTF-16 title
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R /Outlines 10 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 /MediaBox [0 0 100 100] >>")
    for n in (3, 4, 5):
        p.put(n, b"<< /Type /Page /Parent 2 0 R >>")
    p.put(10, b"<< /Type /Outlines /First 11 0 R /Last 13 0 R /Count 4 >>")
    p.put(11, b"<< /Title (Chapter One) /Parent 10 0 R /Next 12 0 R /First 14 0 R /Last 14 0 R /Count 1 /Dest [4 0 R /Fit] >>")
    p.put(12, b"<< /Title <FEFF004300680061007000740065007200200054007700F6> /Parent 10 0 R /Prev 11 0 R /Next 13 0 R /A << /S /GoTo /D [5 0 R /XYZ 0 0 0] >> >>")
    p.put(13, b"<< /Title (Named) /Parent 10 0 R /Prev 12 0 R /Dest (chap3) >>")
    p.put(14, b"<< /Title (Section 1.1) /Parent 11 0 R /Dest [3 0 R /Fit] >>")
    p.xref(1)
    save("outline.pdf", p)

    # ---- annotations
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 612 792] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R /Contents 4 0 R /Annots [10 0 R 11 0 R 12 0 R (junk) 99 0 R 13 0 R] >>")
    p.stream(4, b"", CONTENT % b"annots")
    p.stream(20, b"/Type /XObject /Subtype /Form /BBox [0 0 10 10]", b"0 0 10 10 re f")
    p.put(10, b"<< /Type /Annot /Subtype /Text /Rect [300 400 320 420] /NM (pd-1) /F 28 /Contents (A note \\(1\\)) /AP << /N 20 0 R >> >>")
    p.put(11, b"<< /Type /Annot /Subtype /Square /Rect [150.5 700 50 600.25] /Contents <FEFF0048006900FF> >>")
    p.put(12, b"<< /Type /Annot /Subtype /Text /Rect [0 0 1 1] /IRT 10 0 R /StateModel (Review) /State (Completed) /Contents (done) >>")
    p.put(13, b"<< /Type /Annot /Rect [1 2 3] >>")
    p.xref(1)
    save("annots.pdf", p)

    # ---- resources: inherited, direct dicts, a Form with its own
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R 6 0 R] /Count 2 /MediaBox [0 0 612 792] /Resources 7 0 R >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>")
    p.stream(4, b"", CONTENT % b"res")
    p.put(6, b"<< /Type /Page /Parent 2 0 R /Contents 4 0 R /Resources << /Font << /F9 9 0 R /FD << /Type /Font >> >> >> >>")
    p.put(7, b"<< /Font << /F1 8 0 R >> /XObject << /Fm1 10 0 R /Im1 11 0 R >> >>")
    p.put(8, FONT)
    p.put(9, b"<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>")
    p.stream(10, b"/Type /XObject /Subtype /Form /BBox [0 0 5 5] /Resources << /Font << /FF 9 0 R >> >>", b"q Q")
    p.stream(11, b"/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray", b"\x80")
    p.xref(1)
    save("res.pdf", p)

    # ---- page boxes: nonzero origin, CropBox, odd Rotate, reversed corners, indirect numbers
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 /MediaBox [-50.5 -20 560 780.25] /Rotate -90 >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R /CropBox [10 10.5 300 400] >>")
    p.put(4, b"<< /Type /Page /Parent 2 0 R /MediaBox [600 800 0 0] /Rotate 450 >>")
    p.put(5, b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 6 0 R 7 0 R] /Rotate 0 >>")
    p.put(6, b"300")
    p.put(7, b"400.5")
    p.xref(1)
    save("boxes.pdf", p)

    # ---- 190 one-object subsections (PD_MAXSUBSEC is 256 for the whole chain) (ids <= 65535) and an unordered second revision
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 100 100] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R /Contents 60999 0 R >>")
    ids = [1000 + 6 * i for i in range(190)]
    for i, n in enumerate(ids):
        p.put(n, b"<< /Filler %d >>" % i)
    p.stream(60999, b"", CONTENT % b"sparse")
    x1 = p.xref(1, per_object=True)
    p.put(1006, b"<< /Filler (replaced in revision 2) >>")
    p.put(30000, b"<< /Other 1 >>")
    p.put(5, b"<< /Low 1 >>")
    p.xref(1, prev=x1, per_object=True, size=61000, order=[30000, 5, 1006])
    save("sparse.pdf", p)
    exp["sparse.pdf"] = {"contents": [CONTENT % b"sparse"]}

    # ---- small enough for the 640 KiB target harness: 33 and 240 one-object subsections
    for nsub, name in ((33, "sub33.pdf"), (240, "sparse3k.pdf")):
        p = P()
        p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
        p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 100 100] >>")
        p.put(3, b"<< /Type /Page /Parent 2 0 R /Contents 40000 0 R >>")
        for i in range(nsub):
            p.put(1000 + 7 * i, b"<< /Filler %d >>" % i)
        p.stream(40000, b"", CONTENT % b"thin")
        x1 = p.xref(1, per_object=True)
        p.put(1007, b"<< /Filler (revised) >>")
        p.xref(1, prev=x1, per_object=True, size=40001)
        save(name, p)

    # ---- the negatives whose xref is correct
    good = basic()
    save("n_good.pdf", good)

    def variant(name, mutate_len=None, eol=b"\n", endstream=b"\nendstream", length=None, data=None):
        p = P()
        p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
        p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 612 792] >>")
        p.put(3, b"<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>")
        p.stream(4, b"", data if data is not None else CONTENT % b"neg", length=length, eol=eol, endstream=endstream)
        p.xref(1)
        save(name, p)
    n = len(CONTENT % b"neg")
    variant("n_len_short.pdf", length=3)
    variant("n_len_long.pdf", length=n + 7)          # lands inside "endstream\nendobj"? no: before it -> keyword wrong
    variant("n_len_past_eof.pdf", length=5000000)
    variant("n_len_negative.pdf", length=b"-5")
    variant("n_len_real.pdf", length=b"12.5")
    variant("n_no_endstream.pdf", endstream=b"\nendstrXX")
    variant("n_cr_only.pdf", eol=b"\r")
    variant("n_crlf.pdf", eol=b"\r\n")
    variant("n_huge_length.pdf", length=b"99999999999")
    # xref damage on an otherwise good file
    g = bytes(good.b)
    x = g.rindex(b"\nxref\n") + 1
    save("n_trunc_xref.pdf", g[:x + 40])
    save("n_trunc_in_trailer.pdf", g[:g.index(b"trailer") + 12])
    sx = g.rindex(b"startxref\n") + 10
    save("n_startxref_inside.pdf", g[:sx] + b"%d" % (x + 3) + g[g.index(b"\n%%EOF"):])
    save("n_startxref_zero.pdf", g[:sx] + b"0" + g[g.index(b"\n%%EOF"):])
    save("n_startxref_huge.pdf", g[:sx] + b"99999999999" + g[g.index(b"\n%%EOF"):])
    save("n_no_eof_marker.pdf", g[:g.index(b"\n%%EOF")])
    save("n_junk_after_eof.pdf", g + b"\n" * 3000 + b"trailing junk\n")
    save("n_header_late.pdf", b"junk\n" + g)
    save("n_header_v20.pdf", g.replace(b"%PDF-1.4", b"%PDF-2.0", 1))
    save("n_header_bad_ver.pdf", g.replace(b"%PDF-1.4", b"%PDF-X.4", 1))
    save("n_empty.pdf", b"")
    save("n_cz.pdf", b"CZ" + g[2:])             # a CZ-packed file as READ_SEQ would deliver it
    save("n_garbage.pdf", bytes(range(256)) * 20)
    # xref entry damage
    ent = g.index(b"0000000000 65535 f")
    for tag, repl in (("n_xref_short_entry", g[:ent + 38] + g[ent + 39:]),
                      ("n_xref_badtype", g.replace(b" 00000 n \n", b" 00000 x \n", 1)),
                      ("n_xref_gen_big", g.replace(b" 00000 n \n", b" 99999 n \n", 1)),
                      ("n_xref_off_eof", g.replace(b"0000000015 00000 n", b"0000099999 00000 n", 1))):
        save(tag + ".pdf", repl)
    # /Prev damage
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 10 10] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R >>")
    x1 = p.xref(1)
    p.put(4, b"<< >>")
    p.xref(1, prev=x1 + 5)
    save("n_prev_inside.pdf", p)
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 10 10] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R >>")
    x1 = p.xref(1)
    p.put(4, b"<< >>")
    x2 = p.xref(1, prev=x1)
    p.put(5, b"<< >>")
    p.xref(1, prev=x2)
    b = bytearray(p.b)
    # point the FIRST table's trailer at the newest one: x1 -> x3 (a 3-cycle)
    save("n_prev_cycle3.pdf", bytes(b).replace(b"/ID [<00112233445566778899AABBCCDDEEFF> <00112233445566778899AABBCCDDEEFF>] >>\nstartxref\n%d" % x1,
                                              b"/ID [<00112233445566778899AABBCCDDEEFF> <00112233445566778899AABBCCDDEEFF>] /Prev %d >>\nstartxref\n%d" % (p.xrefs[2], x1), 1))
    # trees
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R 3 0 R] /Count 2 /MediaBox [0 0 10 10] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R >>")
    p.xref(1)
    save("n_page_twice.pdf", p)
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 0 >>")
    p.put(3, b"<< /Type /Pages /Kids [] /Count 0 /Parent 2 0 R >>")
    p.xref(1)
    save("n_no_pages.pdf", p)
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 1 /MediaBox [0 0 10 10] >>")
    p.put(3, b"<< /Type /Font >>")
    p.put(4, b"<< /Type /Page /Parent 2 0 R >>")
    p.xref(1)
    save("n_kid_not_page.pdf", p)
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R 90 0 R 4 0 R] /Count 2 /MediaBox [0 0 10 10] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R >>")
    p.put(4, b"<< /Type /Page /Parent 2 0 R >>")
    p.xref(1)
    save("n_kid_dangling.pdf", p)           # a reference to a missing object is a null: skipped
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 1 R] /Count 1 /MediaBox [0 0 10 10] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R >>")
    p.xref(1)
    save("n_kid_wrong_gen.pdf", p)
    # a page tree 31 deep (ok) and 33 deep (PDE_DEPTH)
    for depth, name in ((30, "n_tree_30.pdf"), (33, "n_tree_33.pdf")):
        p = P()
        p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
        for i in range(depth):
            p.put(2 + i, b"<< /Type /Pages /Kids [%d 0 R] /Count 1 /MediaBox [0 0 10 10] >>" % (3 + i))
        p.put(2 + depth, b"<< /Type /Page >>")
        p.xref(1)
        save(name, p)
    # object numbers over 16 bits
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 70000 0 R >>")
    p.put(70000, b"<< /Type /Pages /Kids [] >>")
    p.xref(1, per_object=True)
    save("n_objnum_70000.pdf", p)
    # filters
    for name, flt in (("n_flate.pdf", b"/FlateDecode"), ("n_lzw.pdf", b"/LZWDecode"), ("n_a85.pdf", b"/ASCII85Decode"),
                      ("n_dct.pdf", b"/DCTDecode"), ("n_crypt.pdf", b"/Crypt"), ("n_chain5.pdf", b"[/AHx /AHx /AHx /AHx /AHx]"),
                      ("n_chain4.pdf", b"[/AHx /AHx /AHx /AHx]"), ("n_unknown.pdf", b"/Bogus")):
        p = P()
        p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
        p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 10 10] >>")
        p.put(3, b"<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>")
        p.stream(4, b"/Filter " + flt, (hexenc(hexenc(hexenc(hexenc(b"AB")))) if flt == b"[/AHx /AHx /AHx /AHx]" else b"4142>") if b"AHx" in flt else b"data")
        p.xref(1)
        save(name, p)
    # a run length bomb: 0x81 0x00 -> 128 bytes each, 20000 pairs = 2.5 MB
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 10 10] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>")
    p.stream(4, b"/Filter /RunLengthDecode", b"\x81\x00" * 20000 + b"\x80")
    p.xref(1)
    save("n_rle_bomb.pdf", p)
    # verify-eng: white space a LOWER stage makes and ASCIIHex skips is work too
    # (PDE_WORK_HI): 0x81 0x20 -> 128 spaces, 40000 pairs = 5.1 MB of them
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 10 10] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>")
    p.stream(4, b"/Filter [/RL /AHx]", b"\x81\x20" * 40000 + b"\x80")
    p.xref(1)
    save("n_hexws_bomb.pdf", p)
    # verify-eng: a thinned revision (33+ subsections) with one out of order
    # AFTER the thinning is PDE_LIMIT, as one before it always was (20.2.3)
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 100 100] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R >>")
    for i in range(40):
        p.put(1000 + 7 * i, b"<< /Filler %d >>" % i)
    order = [0, 1, 2, 3] + [1000 + 7 * i for i in range(40) if i != 5] + [1035]
    p.xref(1, per_object=True, order=order)
    save("n_thin_unord.pdf", p)
    # verify-eng: a subsection reaching past object 65535 is PDE_LIMIT (D8)
    g2 = bytes(basic().b)
    t = g2.index(b"trailer")
    g2 = g2[:t] + b"65530 10\n" + b"0000000000 00000 f \n" * 10 + g2[t:]
    save("n_subsec_65540.pdf", g2)
    # a stream the content lexer must survive: a 100,000-byte string, huge numbers, nesting, an inline image
    big = (b"[" * 30 + b"]" * 30 + b" 99999999999 -99999999999 123456.789e3 1.5 (" + b"s" * 100000 + b") Tj\n"
           b"BI /W 2 /H 2 /BPC 8 /CS /G ID \x01\x02\x03\x04\nEI Q\n(\\(esc\\)) Tj <4142> Tj /N#41 gs % comment\n[(a)-5(b)] TJ")
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 10 10] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>")
    p.stream(4, b"", big)
    p.xref(1)
    save("tokens.pdf", p)
    exp["tokens.pdf"] = {"big": big}
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 10 10] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>")
    p.stream(4, b"", b"[" * 40 + b"]" * 40 + b" q Q")
    p.xref(1)
    save("n_deep_content.pdf", p)
    # security
    for name, extra, root in (("n_encrypt.pdf", b" /Encrypt 5 0 R", b"<< /Type /Catalog /Pages 2 0 R >>"),
                              ("n_perms.pdf", b"", b"<< /Type /Catalog /Pages 2 0 R /Perms << /DocMDP 5 0 R >> >>"),
                              ("n_sigflags.pdf", b"", b"<< /Type /Catalog /Pages 2 0 R /AcroForm << /SigFlags 3 /Fields [] >> >>"),
                              ("n_acroform.pdf", b"", b"<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [] >> >>")):
        p = P()
        p.put(1, root)
        p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 10 10] >>")
        p.put(3, b"<< /Type /Page /Parent 2 0 R >>")
        p.put(5, b"<< /Filter /Standard >>")
        p.xref(1, extra=extra)
        save(name, p)
    # an xref stream (not our profile) and a hybrid (read through its table)
    p = P(b"1.5")
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 10 10] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R >>")
    xs = len(p.b)
    p.put(4, b"<< /Type /XRef /Size 5 /W [1 2 1] /Root 1 0 R /Length 0 >>\nstream\n\nendstream".replace(b"\nendobj", b""))
    p.b += b"startxref\n%d\n%%%%EOF\n" % xs
    save("n_xrefstm.pdf", p)
    p = P(b"1.5")
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 10 10] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R >>")
    p.xref(1, extra=b" /XRefStm 99999")
    save("hybrid.pdf", p)

    # ---- verify-perf: what dict_get's array cursor and the lexer's asm must keep
    # arrcut<s>.pdf: /Widths element 644 (12345) straddles the 4,096-byte block
    # boundary, cut s digits in; 15 KiB of comment keeps the xref's blocks apart,
    # so with an 8 KiB cache a read of that block can be made to fail
    for s in (0, 2):
        p = P()
        p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
        p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 612 792] >>")
        p.put(3, b"<< /Type /Page /Parent 2 0 R >>")
        head = b"5 0 obj\n<< /FirstChar 32 /Widths ["
        at = len(p.b) + len(head)
        body = bytearray(head[len(b"5 0 obj\n"):])
        nums, cut = [], 4096 - 2 - s
        while at + 12 < cut:
            v = b"%d " % (10000 + len(nums)); nums.append(v); body += v; at += len(v)
        body += b" " * (cut - at)
        body += b"12345 " + b"".join(b"%d " % (20000 + j) for j in range(40)) + b"] /Tail 7 >>"
        p.put(5, bytes(body))
        p.b += (b"%" + b"p" * 78 + b"\n") * 200
        p.xref(1)
        save("arrcut%d.pdf" % s, p)
        exp["arrcut%d.pdf" % s] = len(nums)
    # plusref.pdf: "2 +0 R" is a reference (qpdf and MuPDF open it; perf's
    # lexer look-ahead took a digit only and refused the file)
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 +0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 200] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R >>")
    p.xref(1)
    save("plusref.pdf", p)
    # lexedge.pdf: one value an object, each straddling the lexer's 256-byte
    # refill (objects 10..), for the asm word scan and number converter
    p = P()
    p.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    p.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 200] >>")
    p.put(3, b"<< /Type /Page /Parent 2 0 R >>")
    for i, v in enumerate(LEXEDGE):
        n = 10 + i
        pad = 256 - len(b"%d 0 obj\n" % n) - min(1 + i % 3, max(len(v) - 1, 0))
        p.put(n, b" " * pad + v)
        p.b += (b"%" + b"q" * 70 + b"\n") * 8
    p.xref(1)
    save("lexedge.pdf", p)

    for name, data in files.items():
        with open(os.path.join(d, name), "wb") as f:
            f.write(data)
    return exp


# lexedge.pdf's values, object 10 onwards, and what DICT_GET answers for each
# (the engine before the asm lexer answered the same, verify-perf)
LEXEDGE = [b"2147483647", b"2147483648", b"4294967296", b"-2147483648", b"+17", b"-0", b"007",
           b"32767", b"32768", b"1.75", b"0.99999", b"-.5", b"1.2.3", b"--5", b"/A#42C", b"/ab#2Fcd",
           b"/" + b"x" * 127, b"/" + b"y" * 128, b"5 +0 R", b"5\n%c\n0 R", b"5 0 %c\nR", b"5 0 RG",
           b"70000 0 R", b"(lit (nested) \\) str)", b"<48656C6C6F>", b"0000000000000000000001", b".12345678901"]


if __name__ == "__main__":
    build(sys.argv[1] if len(sys.argv) > 1 else "pde-fixtures")
