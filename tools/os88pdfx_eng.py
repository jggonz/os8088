#!/usr/bin/env python3
"""os8088 - tools/os88pdfx_eng.py

The any-PDF reader's fixtures (PAGEDESK-SPEC 20.22.3, PAGEDESK-ANY-PLAN AP-01,
AP-02, AP-03): files with cross-reference STREAMS and OBJECT STREAMS, hybrid
files, damaged files the scan must rebuild, and content in LZWDecode and
ASCII85Decode. Every file is checked here, as it is written, against qpdf: qpdf
must read it, and a stream this tool encoded must decode under qpdf to the
bytes it was made from - so a fixture is right before the engine ever sees it.

    python3 tools/os88pdfx_eng.py OUTDIR [--samples DIR]

DIR holds tools/os88pdfgen.py's samples (made into OUTDIR/src when not given).
OUTDIR gets the .PDF files and README.TXT, the provenance: the command, the qpdf
version and what each file is. apps/pagedesk/hosttest/pde_check.py holds the
engine to qpdf over every one: pages, page objects, boxes, every content
stream's decoded bytes, and every object-stream member's bytes, EXACT and PLAIN
(20.22.3.3), against this file's own reading of the decoded object stream.

Deterministic: qpdf is run with --deterministic-id and nothing here reads the
clock, so the same tree and the same qpdf make the same bytes.
"""
import base64, os, re, subprocess, sys, zlib

QPDF = "qpdf"
NOTES = []


def qpdf(*a, check=True):
    p = subprocess.run([QPDF, *a], capture_output=True)
    if check and p.returncode not in (0, 3):
        sys.exit("qpdf %s: %s" % (" ".join(a), p.stderr.decode()))
    return p


def qpdf_version():
    return qpdf("--version").stdout.decode().splitlines()[0]


# ---------------------------------------------------------------- encoders
def lzw_encode(data, early=1):
    """PDF LZWDecode (7.4.4): 9-12 bit codes MSB first, 256 clear, 257 EOD.
    The width a code is written at is the width the DECODER reads it at: it
    has made one entry fewer than the encoder by then."""
    out = []                                    # (code, width)
    d = {bytes([i]): i for i in range(256)}
    nxt = 258

    def width():
        n = nxt - 1 + early
        w = 9
        while w < 12 and n >= (1 << w):
            w += 1
        return w

    out.append((256, 9))
    w = b""
    for c in data:
        wc = w + bytes([c])
        if wc in d:
            w = wc
            continue
        out.append((d[w], width()))
        if nxt < 4096 - early - 1:
            d[wc] = nxt
            nxt += 1
        else:                                   # the table is full: start again
            out.append((256, width()))
            d = {bytes([i]): i for i in range(256)}
            nxt = 258
        w = bytes([c])
    if w:
        out.append((d[w], width()))
        nxt += 1                                # the decoder adds an entry for it
    out.append((257, width()))
    acc = nb = 0
    b = bytearray()
    for code, wd in out:
        acc = (acc << wd) | code
        nb += wd
        while nb >= 8:
            nb -= 8
            b.append((acc >> nb) & 0xFF)
    if nb:
        b.append((acc << (8 - nb)) & 0xFF)
    return bytes(b)


def a85_encode(data, wrap=64):
    return base64.a85encode(data, wrapcol=wrap) + b"~>"


# ---------------------------------------------------------------- a small writer
class Doc:
    def __init__(self, version=b"1.4"):
        self.v = version
        self.objs = {}

    def put(self, n, body):
        self.objs[n] = body

    def stream(self, n, extra, data):
        self.objs[n] = b"<< /Length %d %s>>\nstream\n" % (len(data), extra) + data + b"\nendstream"

    def finish(self, root, extra=b""):
        out = bytearray(b"%PDF-" + self.v + b"\n%\xe2\xe3\xcf\xd3\n")
        off = {}
        for n in sorted(self.objs):
            off[n] = len(out)
            out += b"%d 0 obj\n" % n + self.objs[n] + b"\nendobj\n"
        size = max(self.objs) + 1
        xr = len(out)
        out += b"xref\n0 %d\n0000000000 65535 f \n" % size
        for n in range(1, size):
            out += (b"%010d 00000 n \n" % off[n]) if n in off else b"0000000000 65535 f \n"
        out += (b"trailer\n<< /Size %d /Root %d 0 R /ID [<00112233445566778899AABBCCDDEEFF> "
                b"<00112233445566778899AABBCCDDEEFF>] %s>>\nstartxref\n%d\n%%%%EOF\n" % (size, root, extra, xr))
        return bytes(out)


def page_doc(contents, version=b"1.4"):
    """One page a content stream: contents = [(filter_extra, raw_bytes)]."""
    d = Doc(version)
    d.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    kids = []
    n = 4
    for extra, data in contents:
        kids.append(n)
        d.put(n, b"<< /Type /Page /Parent 2 0 R /Contents %d 0 R >>" % (n + 1))
        d.stream(n + 1, extra, data)
        n += 2
    d.put(2, b"<< /Type /Pages /Kids [%s] /Count %d /MediaBox [0 0 612 792] "
             b"/Resources << /Font << /F1 3 0 R >> >> >>" % (b" ".join(b"%d 0 R" % k for k in kids), len(kids)))
    d.put(3, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    return d.finish(1)


def text_content(seed, lines):
    """A content stream long enough to grow LZW's table through every width."""
    out = bytearray(b"BT /F1 9 Tf 36 760 Td 11 TL\n")
    x = seed
    for i in range(lines):
        words = []
        for _ in range(8):
            x = (x * 1103515245 + 12345) & 0x7FFFFFFF
            words.append(b"w%d" % (x % 9973))
        out += b"(%d %s) '\n" % (i, b" ".join(words))
    out += b"ET\n"
    return bytes(out)


# ---------------------------------------------------------------- checks
def decoded(path, num):
    p = qpdf("--show-object=%d" % num, "--filtered-stream-data", path)
    return p.stdout


def must_decode(path, num, want, what):
    got = decoded(path, num)
    if got != want:
        sys.exit("%s: qpdf decodes object %d to %d bytes, wanted %d" % (what, num, len(got), len(want)))


def must_read(path):
    p = qpdf("--check", path, check=False)
    if p.returncode not in (0, 3):
        sys.exit("%s: qpdf --check failed: %s" % (path, p.stdout.decode()[-400:]))


def xref_of(path):
    """qpdf's reading: {num: ('u', offset) | ('c', stream, index)}"""
    x = qpdf("--show-xref", path).stdout.decode()
    r = {}
    for m in re.finditer(r"^(\d+)/(\d+): uncompressed; offset = (\d+)", x, re.M):
        r[int(m.group(1))] = ("u", int(m.group(3)))
    for m in re.finditer(r"^(\d+)/(\d+): compressed; stream = (\d+), index = (\d+)", x, re.M):
        r[int(m.group(1))] = ("c", int(m.group(3)), int(m.group(4)))
    return r


def save(out, name, data, what):
    path = os.path.join(out, name)
    with open(path, "wb") as f:
        f.write(data)
    must_read(path)
    NOTES.append((name, what))
    return path


# ---------------------------------------------------------------- the variants
def objstm(src, dst, compress, keep=False):
    a = ["--object-streams=generate", "--deterministic-id", "--newline-before-endstream"]
    if keep:
        a.append("--stream-data=preserve")      # the streams' own filters, as they are
    elif not compress:
        a.append("--compress-streams=n")
    qpdf(*a, src, dst)


def hybrid(osfile):
    """A hybrid-reference file (7.5.8.4) from an object-stream file: the xref
    stream stays where it is and is named by /XRefStm, and a classic table is
    appended that lists the direct objects only - a member is in no subsection,
    so a reader that ignores /XRefStm does not find it. (Members listed 'f'
    are the other shape; qpdf 12 lets the 'f' row win and cannot read it, so
    there is nothing to hold the engine to.)"""
    data = open(osfile, "rb").read()
    xr = xref_of(osfile)
    sx = int(re.search(rb"startxref\s+(\d+)", data[-64:]).group(1))
    trailer = re.search(rb"<<\s*/Type\s*/XRef.*?>>\s*stream", data[sx:], re.S).group(0)
    root = re.search(rb"/Root\s+(\d+ \d+ R)", trailer).group(1)
    size = int(re.search(rb"/Size\s+(\d+)", trailer).group(1))
    ident = re.search(rb"/ID\s*\[[^\]]*\]", trailer).group(0)
    body = data[:data.rindex(b"startxref")]
    direct = [n for n in range(1, size) if xr.get(n, ("x",))[0] == "u"]
    t = bytearray(b"xref\n0 1\n0000000000 65535 f \n")
    i = 0
    while i < len(direct):                      # one subsection a run of direct objects
        j = i
        while j + 1 < len(direct) and direct[j + 1] == direct[j] + 1:
            j += 1
        t += b"%d %d\n" % (direct[i], j - i + 1)
        for n in direct[i:j + 1]:
            t += b"%010d 00000 n \n" % xr[n][1]
        i = j + 1
    xo = len(body)
    return bytes(body + t + b"trailer\n<< /Size %d /Root %s %s /XRefStm %d >>\nstartxref\n%d\n%%%%EOF\n"
                 % (size, root, ident, sx, xo))


def xrefstm_update(classic):
    """An incremental update whose cross-reference is a STREAM, on top of a
    classic file: a page gains /Rotate 90 and an object is added, so the
    fold's newest-wins rule decides what the reader sees."""
    data = bytearray(classic)
    prev = int(re.search(rb"startxref\s+(\d+)", classic[-64:]).group(1))
    size = int(re.search(rb"/Size (\d+)", classic).group(1))
    m = re.search(rb"(\d+) 0 obj\s*(<<\s*/Type\s*/Page\b.*?)\s*endobj", classic, re.S)
    pg = int(m.group(1))
    body = m.group(2).rstrip()
    if b"/Rotate" in body:
        sys.exit("xrefstm_update: the page already rotates")
    root = re.search(rb"/Root (\d+) 0 R", classic).group(1)
    o_pg = len(data)
    data += b"%d 0 obj\n" % pg + body[:-2] + b" /Rotate 90 >>\nendobj\n"
    new = size
    o_new = len(data)
    data += b"%d 0 obj\n<< /Producer (os88pdfx_eng update) >>\nendobj\n" % new
    xs = new + 1
    o_xs = len(data)
    rows = [(pg, 1, o_pg, 0), (new, 1, o_new, 0), (xs, 1, o_xs, 0)]
    rows.sort()
    idx = b" ".join(b"%d 1" % r[0] for r in rows)
    raw = b"".join(bytes([r[1]]) + r[2].to_bytes(4, "big") + r[3].to_bytes(2, "big") for r in rows)
    data += (b"%d 0 obj\n<< /Type /XRef /Size %d /Root %s 0 R /Prev %d /W [1 4 2] /Index [%s] /Length %d "
             b"/ID [<00112233445566778899AABBCCDDEEFF> <00112233445566778899AABBCCDDEEFF>] >>\nstream\n"
             % (xs, xs + 1, root, prev, idx, len(raw))) + raw + b"\nendstream\nendobj\n"
    data += b"startxref\n%d\n%%%%EOF\n" % o_xs
    return bytes(data)


def damage_startxref(data):
    i = data.rindex(b"startxref")
    return data[:i] + b"startxref\n%d\n%%%%EOF\n" % (len(data) * 3)


def damage_root_row(data):
    """The row of the object stream holding the /Root, in an uncompressed
    /W [1 2 1] xref stream, points 3 bytes past "n 0 obj": entering the
    catalog finds no object there, and the scan rebuilds the cross-reference."""
    sx = int(re.search(rb"startxref\s+(\d+)", data[-64:]).group(1))
    m = re.compile(rb"<< /Type /XRef [^\n]*?/W \[ 1 2 1 \][^\n]*?/Root (\d+) 0 R[^\n]*>>\nstream\n").search(data, sx)
    if not m:
        sys.exit("damage_root_row: no uncompressed /W [1 2 1] xref stream")
    root = int(m.group(1))
    if data[m.end() + 4 * root] != 2:
        sys.exit("damage_root_row: the root is not in an object stream")
    own = int.from_bytes(data[m.end() + 4 * root + 1:m.end() + 4 * root + 3], "big")
    row = m.end() + 4 * own
    off = int.from_bytes(data[row + 1:row + 3], "big") + 3
    return data[:row + 1] + off.to_bytes(2, "big") + data[row + 3:]


def big_objstm():
    """Members whose decoded stream is longer than the window's 32,512-byte
    ring (20.22.3.3), read out of order, so the window decodes again from the
    start: 160 annotations of ~300 bytes on two pages."""
    d = Doc(b"1.5")
    d.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    d.put(2, b"<< /Type /Pages /Kids [4 0 R 5 0 R] /Count 2 /MediaBox [0 0 612 792] >>")
    d.put(3, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    a1 = list(range(10, 90))
    a2 = list(range(90, 170))
    a1.reverse()                                # the first page reads them backwards
    d.put(4, b"<< /Type /Page /Parent 2 0 R /Contents 6 0 R /Annots [%s] >>" % b" ".join(b"%d 0 R" % n for n in a1))
    d.put(5, b"<< /Type /Page /Parent 2 0 R /Contents 7 0 R /Annots [%s] >>" % b" ".join(b"%d 0 R" % n for n in a2))
    d.stream(6, b"", b"BT /F1 12 Tf 72 700 Td (big object streams) Tj ET")
    d.stream(7, b"", b"0 0 m 100 100 l S")
    for n in a1 + a2:
        y = 20 + (n * 7) % 700
        d.put(n, b"<< /Type /Annot /Subtype /Text /Rect [%d %d %d %d] /Contents (%s) /NM (eng-%d) >>"
              % (20 + n % 400, y, 40 + n % 400, y + 20, b"note %d " % n * 48, n))
    return d.finish(1)


def main():
    out = sys.argv[1]
    sdir = None
    if "--samples" in sys.argv:
        sdir = sys.argv[sys.argv.index("--samples") + 1]
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith(".PDF") or f == "README.TXT":
            os.remove(os.path.join(out, f))
    tmp = os.path.join(out, ".src")
    os.makedirs(tmp, exist_ok=True)
    if sdir is None:
        sdir = os.path.join(tmp, "samples")
        subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "os88pdfgen.py"), sdir],
                       check=True, capture_output=True)

    # AP-01: the samples as object-stream files, plain and compressed
    for s in ("HELLO", "PAGES", "MARKUP", "LATIN", "XTGUIDE"):
        src = os.path.join(sdir, s + ".PDF")
        for z, what in ((0, "uncompressed object and xref streams"),
                        (1, "Flate object streams and a /Predictor 12 xref stream")):
            name = "%s%s.PDF" % (s[:5], "_OZ" if z else "_OS")
            objstm(src, os.path.join(out, name), z)
            must_read(os.path.join(out, name))
            NOTES.append((name, "%s.PDF with %s (qpdf --object-streams=generate)" % (s, what)))
    # hybrid, and an xref-stream update on top of a classic file
    save(out, "HELLO_HY.PDF", hybrid(os.path.join(out, "HELLO_OS.PDF")),
         "HELLO_OS.PDF made hybrid: a classic table of the direct objects, /XRefStm naming the stream")
    save(out, "PAGES_HY.PDF", hybrid(os.path.join(out, "PAGES_OZ.PDF")),
         "PAGES_OZ.PDF made hybrid (compressed object streams)")
    save(out, "MARKU_UP.PDF", xrefstm_update(open(os.path.join(sdir, "MARKUP.PDF"), "rb").read()),
         "MARKUP.PDF plus an incremental update with a cross-reference STREAM (/Prev to the table): page 1 rotates")
    # big object streams: the window's ring and read-behind
    big = save(out, ".BIG.PDF", big_objstm(), "")
    NOTES.pop()
    for z, name in ((0, "BIG_OS.PDF"), (1, "BIG_OZ.PDF")):
        objstm(big, os.path.join(out, name), z)
        must_read(os.path.join(out, name))
        NOTES.append((name, "160 annotations of ~300 bytes in object streams longer than the 32,512-byte ring, "
                      "read backwards (%s)" % ("compressed" if z else "uncompressed")))
    os.remove(big)
    # AP-02: damage the scan must rebuild
    save(out, "PAGES_RS.PDF", damage_startxref(open(os.path.join(out, "PAGES_OS.PDF"), "rb").read()),
         "PAGES_OS.PDF with startxref past the end of the file: rebuilt by the scan, members included")
    save(out, "XTGUI_RZ.PDF", damage_startxref(open(os.path.join(out, "XTGUI_OZ.PDF"), "rb").read()),
         "XTGUI_OZ.PDF with startxref past the end of the file (compressed object streams)")
    save(out, "MARKU_RW.PDF", damage_root_row(open(os.path.join(out, "MARKU_OS.PDF"), "rb").read()),
         "MARKU_OS.PDF with the xref-stream row of the /Root's object stream 3 bytes off: the scan")
    # AP-03: LZW and ASCII85 content
    t1 = text_content(1, 700)                   # ~45 KB: the LZW table resets more than once
    t2 = text_content(2, 60)
    lz1 = lzw_encode(t1)
    lz0 = lzw_encode(t2, early=0)
    a85 = a85_encode(t2)
    a85z = a85_encode(b"\0" * 64 + t2[:301])    # 'z' groups and a short last group
    chain = a85_encode(lzw_encode(t2))
    p = save(out, "LZW.PDF", page_doc([(b"/Filter /LZWDecode ", lz1),
                                       (b"/Filter /LZWDecode /DecodeParms << /EarlyChange 0 >> ", lz0)]),
             "LZWDecode content: a 45 KB stream through every code width and table reset, and EarlyChange 0")
    must_decode(p, 5, t1, "LZW.PDF")
    must_decode(p, 7, t2, "LZW.PDF")
    p = save(out, "A85.PDF", page_doc([(b"/Filter /ASCII85Decode ", a85), (b"/Filter /A85 ", a85z),
                                       (b"/Filter [/ASCII85Decode /LZWDecode] ", chain)]),
             "ASCII85Decode content (wrapped lines, 'z', a short last group, the /A85 abbreviation) and an "
             "[ASCII85 LZW] chain")
    must_decode(p, 5, t2, "A85.PDF")
    must_decode(p, 7, b"\0" * 64 + t2[:301], "A85.PDF")
    must_decode(p, 9, t2, "A85.PDF")
    objstm(p, os.path.join(out, "A85_OS.PDF"), 0, keep=True)
    NOTES.append(("A85_OS.PDF", "A85.PDF in object streams, its filters kept"))
    # RES_LOOKUP's PDRL_DIRECT (20.22.3.8): an ExtGState written inside the page
    d = Doc(b"1.5")
    d.put(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    d.put(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 200] >>")
    d.put(3, b"<< /Type /Page /Parent 2 0 R /Contents 4 0 R /Resources << /ExtGState << /GS1 << /CA 0.5 /ca 0.25 >> "
             b"/GS2 5 0 R >> >> >>")
    d.stream(4, b"", b"/GS1 gs 0 0 100 100 re f /GS2 gs")
    d.put(5, b"<< /Type /ExtGState /LW 3 >>")
    p = save(out, "RESDIR.PDF", d.finish(1), "a page whose /ExtGState /GS1 is written inside it (RES_LOOKUP PDRL_DIRECT)")
    objstm(p, os.path.join(out, "RESDI_OS.PDF"), 0)
    NOTES.append(("RESDI_OS.PDF", "RESDIR.PDF in object streams: the direct value inside a member"))
    subprocess.run(["rm", "-rf", tmp])
    with open(os.path.join(out, "README.TXT"), "w") as f:
        f.write("tests/pdfany/eng: the any-PDF reader's fixtures (PAGEDESK-SPEC 20.22.3)\n\n"
                "Made by: python3 tools/os88pdfx_eng.py tests/pdfany/eng\n"
                "with %s, from tools/os88pdfgen.py's samples (this tree's own, generated).\n"
                "Every file is the tool's own or qpdf's rewrite of one; nothing third-party.\n"
                "The tool checks each with qpdf as it writes it, and pde_check.py holds the engine\n"
                "to qpdf over all of them.\n\n" % qpdf_version())
        for name, what in NOTES:
            f.write("%-14s %s\n" % (name, what))


if __name__ == "__main__":
    main()
