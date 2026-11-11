#!/usr/bin/env python3
"""os8088 - apps/pagedesk/hosttest/pde_fl_check.py

PDENG's FlateDecode and predictors (PAGEDESK-SPEC 20.13) against two
independent decoders: Python's zlib (and a predictor written here from the
specification, not from pdeflate.c) and qpdf. Every case is a small PDF that
this script writes, opened through hosttest/pde_fl_test.c with the flate
claim, and the bytes the engine delivers are compared one for one.

  usage: pde_fl_check.py PDE_FL_TEST BUILDDIR [--fuzz N]

Also checks pdeflate.c's constants against apps/pixel/pxpng.asm's (20.13.2).
"""
import os, random, re, subprocess, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SYNTAX, TRUNC, FLATE, FILTER, LIMIT = 1, 3, 16, 17, 33
FAILS = []
NCASE = 0


def fail(msg):
    FAILS.append(msg)
    print("FAIL", msg)


def build_pdf(streams):
    """streams: list of (dict_extra bytes, data bytes); object 10 + i. Returns bytes."""
    objs = [b"<< /Type /Catalog /Pages 2 0 R >>",
            b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R >>"]
    objs.append(b"<< /Length 0 >>\nstream\n\nendstream")
    out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offs = {}
    def put(n, body):
        offs[n] = len(out)
        out.extend(b"%d 0 obj\n" % n + body + b"\nendobj\n")
    for i, o in enumerate(objs):
        put(i + 1, o)
    for i, (extra, data) in enumerate(streams):
        if extra is None:                       # a plain object
            put(10 + i, data)
            continue
        put(10 + i, b"<< /Length %d %s >>\nstream\n" % (len(data), extra) + data + b"\nendstream")
    top = 10 + len(streams)
    xref = len(out)
    out.extend(b"xref\n0 %d\n" % top)
    out.extend(b"0000000000 65535 f \n")
    for n in range(1, top):
        out.extend(b"%010d 00000 n \n" % offs[n] if n in offs else b"0000000000 65535 f \n")
    out.extend(b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (top, xref))
    return bytes(out)


class Engine:
    def __init__(self, exe, work):
        self.exe, self.work = exe, work
        os.makedirs(work, exist_ok=True)

    def run(self, pdf, obj=10, chunk=37, w=86, cluster=1, cache=16):
        global NCASE
        NCASE += 1
        pf = os.path.join(self.work, "case.pdf")
        of = os.path.join(self.work, "case.out")
        open(pf, "wb").write(pdf)
        p = subprocess.run([self.exe, "strm", pf, str(obj), of, "-r", str(chunk), "-w", str(w),
                            "-c", str(cluster), "-k", str(cache)], capture_output=True, text=True, timeout=300)
        if p.returncode not in (0,):
            return ("CRASH rc=%d %s" % (p.returncode, p.stderr[-300:]), b"")
        m = re.match(r"STRM rc=(\d+)(?: read=(\d+) bytes=(\d+))?", p.stdout)
        if not m:
            return ("BAD %r" % p.stdout, b"")
        data = open(of, "rb").read() if os.path.exists(of) else b""
        if m.group(2) is None:
            return (int(m.group(1)), b"")
        return (int(m.group(2)), data)

    def qpdf(self, pdf, obj=10):
        pf = os.path.join(self.work, "q.pdf")
        open(pf, "wb").write(pdf)
        p = subprocess.run(["qpdf", "--show-object=%d" % obj, "--filtered-stream-data", pf], capture_output=True)
        return p.stdout if p.returncode in (0, 3) else None


# ---- the specification's predictors, written here ----------------------------------------------

def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def png_encode(raw, rowbytes, bpp, tags):
    out = bytearray()
    prev = bytes(rowbytes)
    for r in range(len(raw) // rowbytes):
        row = raw[r * rowbytes:(r + 1) * rowbytes]
        t = tags[r % len(tags)]
        enc = bytearray()
        for i in range(rowbytes):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            x = row[i]
            if t == 0: v = x
            elif t == 1: v = x - a
            elif t == 2: v = x - b
            elif t == 3: v = x - ((a + b) >> 1)
            else: v = x - paeth(a, b, c)
            enc.append(v & 255)
        out.append(t)
        out.extend(enc)
        prev = row
    return bytes(out)


def tiff_encode(raw, rowbytes, colors, bpc):
    out = bytearray()
    step = colors * (bpc // 8)
    for r in range(len(raw) // rowbytes):
        row = raw[r * rowbytes:(r + 1) * rowbytes]
        if bpc == 8:
            enc = bytes((row[i] - (row[i - step] if i >= step else 0)) & 255 for i in range(rowbytes))
        else:
            enc = bytearray(rowbytes)
            for i in range(0, rowbytes, 2):
                x = (row[i] << 8) | row[i + 1]
                a = ((row[i - step] << 8) | row[i - step + 1]) if i >= step else 0
                v = (x - a) & 0xFFFF
                enc[i], enc[i + 1] = v >> 8, v & 255
        out.extend(enc)
    return bytes(out)


def zl(data, level=6, strategy=zlib.Z_DEFAULT_STRATEGY, wbits=15):
    c = zlib.compressobj(level, zlib.DEFLATED, wbits, 9, strategy)
    return c.compress(data) + c.flush()


def corpus(rng):
    text = (b"BT /F1 12 Tf 72 700 Td (Hello, PageDesk) Tj ET\n" * 40 +
            b"q 1 0 0 1 10 10 cm 0 0 100 100 re f Q\n" * 20)
    rnd = bytes(rng.randrange(256) for _ in range(5000))
    far = bytes(rng.randrange(256) for _ in range(32768))
    low = bytes(rng.randrange(4) for _ in range(20000))
    c = [("empty", b""), ("one", b"x"), ("text", text), ("random", rnd), ("zeros", bytes(100000)),
         ("pattern3", b"abc" * 30000), ("far32768", far + far[:2000]), ("far-mixed", far[:30000] + rnd + far[:30000]),
         ("lowent", low), ("allbytes", bytes(range(256)) * 50)]
    return c


def compare(name, got, want, rc, wantrc=0):
    if rc != wantrc:
        fail("%s: rc %r != %r" % (name, rc, wantrc))
        return False
    if got != want:
        n = next((i for i in range(min(len(got), len(want))) if got[i] != want[i]), min(len(got), len(want)))
        fail("%s: output differs at %d (got %d bytes, want %d)" % (name, n, len(got), len(want)))
        return False
    return True


def check_basic(E, rng):
    chunks = (1, 37, 128, 1000, 4096)
    for name, data in corpus(rng):
        variants = [("L6", zl(data, 6)), ("L1", zl(data, 1)), ("L9", zl(data, 9)), ("L0", zl(data, 0)),
                    ("fixed", zl(data, 6, zlib.Z_FIXED)), ("huff", zl(data, 6, zlib.Z_HUFFMAN_ONLY)),
                    ("rle", zl(data, 6, zlib.Z_RLE)), ("w9", zl(data, 6, wbits=9))]
        c = zlib.compressobj(6)
        part = b""
        for i in range(0, len(data), 997):
            part += c.compress(data[i:i + 997]) + c.flush(zlib.Z_FULL_FLUSH)
        variants.append(("flushed", part + c.flush()))
        for vname, z in variants:
            pdf = build_pdf([(b"/Filter /FlateDecode", z)])
            for ch in (chunks if len(data) < 5000 or vname == "L6" else (37, 4096)):
                rc, got = E.run(pdf, chunk=ch)
                if not compare("%s/%s/r%d" % (name, vname, ch), got, data, rc):
                    break
            if len(data) < 40000 and vname in ("L6", "fixed", "L0"):
                q = E.qpdf(pdf)
                if q is not None and q != data:
                    fail("%s/%s: qpdf disagrees with zlib" % (name, vname))
    # a 3 MiB expansion: cut at 1 MiB with PDE_LIMIT (PD-28), never more
    pdf = build_pdf([(b"/Filter /FlateDecode", zl(bytes(3 << 20), 9))])
    rc, got = E.run(pdf, chunk=4096)
    if rc != LIMIT or not (1 << 20) - 8192 <= len(got) <= (1 << 20):
        fail("bomb: rc=%r bytes=%d" % (rc, len(got)))
    # clusters and a bigger block cache
    data = corpus(rng)[2][1]
    pdf = build_pdf([(b"/Filter /FlateDecode", zl(data))])
    for cl, k in ((4, 16), (1, 64), (8, 64)):
        rc, got = E.run(pdf, cluster=cl, cache=k)
        compare("cluster%d/cache%d" % (cl, k), got, data, rc)


def check_chain(E, rng):
    data = corpus(rng)[2][1]
    z = zl(data)
    hexed = z.hex().upper().encode() + b">"
    rl = bytearray()
    for i in range(0, len(z), 100):
        piece = z[i:i + 100]
        rl.append(len(piece) - 1)
        rl.extend(piece)
    rl.append(128)
    for name, extra, body in (
            ("hex+flate", b"/Filter [/ASCIIHexDecode /FlateDecode]", hexed),
            ("rl+flate", b"/Filter [/RunLengthDecode /FlateDecode]", bytes(rl)),
            ("abbrev", b"/Filter [/AHx /Fl]", hexed),
            ("ref filter", b"/Filter 11 0 R", z)):
        pdf = build_pdf([(extra, body)] + ([(None, b"/FlateDecode")] if name == "ref filter" else []))
        rc, got = E.run(pdf)
        compare(name, got, data, rc)
    # two Flate stages are refused; Flate with no claim is the old PDE_FLATE; others stay PDE_FILTER
    rc, _ = E.run(build_pdf([(b"/Filter [/FlateDecode /FlateDecode]", z)]))
    if rc != FILTER:
        fail("two Flate stages: rc=%r" % rc)
    rc, _ = E.run(build_pdf([(b"/Filter /FlateDecode", z)]), w=0)
    if rc != FLATE:
        fail("no claim: rc=%r" % rc)
    rc, _ = E.run(build_pdf([(b"/Filter /FlateDecode", z)]), w=42)
    if rc != FLATE:
        fail("a claim under one slot: rc=%r" % rc)
    rc, got = E.run(build_pdf([(b"/Filter /FlateDecode", z)]), w=43)
    compare("one slot", got, data, rc)
    check_lzw(E, data, z)


def check_lzw(E, data, z):
    """LZWDecode (20.22.3.6), the slot's other stage: held to qpdf and to tools/os88pdfx_eng.py's encoder."""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from os88pdfx_eng import lzw_encode
    long = (data * (1 + 60000 // max(1, len(data))))[:60000]     # every width and a table reset
    for name, extra, raw, body in (
            ("lzw", b"/Filter /LZWDecode", data, lzw_encode(data)),
            ("lzw long", b"/Filter /LZW", long, lzw_encode(long)),
            ("lzw early0", b"/Filter /LZWDecode /DecodeParms << /EarlyChange 0 >>", long, lzw_encode(long, early=0)),
            ("hex+lzw", b"/Filter [/AHx /LZW]", data, lzw_encode(data).hex().encode() + b">")):
        pdf = build_pdf([(extra, body)])
        for chunk in (1, 37, 4096):
            rc, got = E.run(pdf, chunk=chunk)
            if not compare("%s/%d" % (name, chunk), got, raw, rc):
                break
        q = E.qpdf(pdf)
        if q is not None and q != raw:
            fail("%s: qpdf disagrees with the encoder" % name)
    # one slot stage a chain: LZW with Flate is refused, as two Flates are
    rc, _ = E.run(build_pdf([(b"/Filter [/LZWDecode /FlateDecode]", lzw_encode(z))]))
    if rc != FILTER:
        fail("lzw+flate: rc=%r" % rc)
    # Flate's bytes read as LZW codes are not LZW: a code past the table is PDE_SYNTAX, after what
    # qpdf also decodes before it gives up (a byte a read: a read the failure ends answers the failure)
    pdf = build_pdf([(b"/Filter /LZWDecode", z)])
    rc, got = E.run(pdf, chunk=1)
    q = E.qpdf(pdf)
    if rc != SYNTAX or (q is not None and got != q):
        fail("lzw on Flate bytes: rc=%r, %d bytes (qpdf %r)" % (rc, len(got), None if q is None else len(q)))


def check_predictors(E, rng):
    def case(name, cols, colors, bpc, pred, rows, tags=(0, 1, 2, 3, 4), want=0, parms=None, noq=False):
        rowbytes = (cols * colors * bpc + 7) // 8
        raw = bytes(rng.randrange(256) if rng.random() < .6 else rng.randrange(8) for _ in range(rowbytes * rows))
        if want:
            enc = png_encode(raw, rowbytes, 1, (0,)) if rowbytes < 4000 else raw
        elif pred == 2:
            enc = tiff_encode(raw, rowbytes, colors, bpc)
        else:
            enc = png_encode(raw, rowbytes, max(1, colors * bpc // 8), tags)
        z = zl(enc)
        dp = parms if parms is not None else b"/DecodeParms << /Predictor %d /Colors %d /BitsPerComponent %d /Columns %d >>" % (pred, colors, bpc, cols)
        pdf = build_pdf([(b"/Filter /FlateDecode " + dp, z)] +
                        ([(None, b"<< /Predictor 15 /Columns 20 >>")] if b"11 0 R" in dp else []))
        if want:
            rc, got = E.run(pdf, chunk=64)
            if rc != want:
                fail("%s: rc %r != %r" % (name, rc, want))
            return
        for ch in (1, 37, 128, 1000):
            rc, got = E.run(pdf, chunk=ch)
            if not compare("%s/r%d" % (name, ch), got, raw, rc, want):
                return
        q = None if noq else E.qpdf(pdf)
        if q is not None and q != raw:
            fail("%s: qpdf disagrees with the reference predictor" % name)
    case("png gray8", 100, 1, 8, 15, 20)
    case("png gray8 none", 100, 1, 8, 10, 6, tags=(0,))
    for t in range(5):
        case("png gray8 tag%d" % t, 50, 1, 8, 12 + (t % 4), 8, tags=(t,))
    case("png rgb8", 333, 3, 8, 15, 12)
    case("png rgba8", 41, 4, 8, 15, 9)
    case("png rgb16", 25, 3, 16, 15, 7)
    case("png gray1", 100, 1, 1, 15, 9)
    case("png gray2", 33, 1, 2, 15, 9)
    case("png gray4", 33, 1, 4, 15, 9)
    case("png wide row", 4096, 1, 8, 15, 3)
    case("png row 3000", 1000, 3, 8, 15, 3)
    case("png col1", 1, 1, 8, 15, 30)
    case("tiff gray8", 100, 1, 8, 2, 8)
    case("tiff rgb8", 77, 3, 8, 2, 8)
    case("tiff gray16", 100, 1, 16, 2, 8)
    case("tiff rgb16", 33, 3, 16, 2, 5)
    case("tiff wide", 4096, 1, 8, 2, 3)
    case("parm defaults", 1, 1, 8, 15, 5, parms=b"/DecodeParms << /Predictor 15 >>")
    case("parm array", 20, 1, 8, 15, 5, parms=b"/DecodeParms [ << /Predictor 15 /Columns 20 >> ]")
    case("parm indirect", 20, 1, 8, 15, 5, parms=b"/DecodeParms 11 0 R")
    # refusals
    case("row too big", 4097, 1, 8, 15, 1, want=LIMIT)
    case("rgb16 row too big", 683, 3, 16, 15, 1, want=LIMIT)
    case("colors 5", 10, 5, 8, 15, 1, want=FILTER)
    case("predictor 7", 10, 1, 8, 7, 1, want=FILTER, parms=b"/DecodeParms << /Predictor 7 /Columns 10 >>")
    case("tiff gray4", 10, 1, 4, 2, 1, want=FILTER)
    case("bpc 3", 10, 1, 3, 15, 1, want=FILTER, parms=b"/DecodeParms << /Predictor 15 /Columns 10 /BitsPerComponent 3 >>")
    # a row tag over 4 is corrupt data
    enc = bytes([9]) + bytes(10)
    rc, got = E.run(build_pdf([(b"/Filter /FlateDecode /DecodeParms << /Predictor 15 /Columns 10 >>", zl(enc))]))
    if rc != SYNTAX:
        fail("bad row tag: rc=%r" % rc)
    # the engine without a predictor row gives a partial last row as far as it goes
    enc = png_encode(bytes(range(10)) * 3, 10, 1, (1,))[:-4]
    rc, got = E.run(build_pdf([(b"/Filter /FlateDecode /DecodeParms << /Predictor 15 /Columns 10 >>", zl(enc))]))
    if rc != 0 or len(got) != 26:
        fail("partial row: rc=%r bytes=%d" % (rc, len(got)))


def partial_zlib(z):
    """What an independent inflater yields before it stops, and how: 'done', 'trunc' or 'error'.
    The zlib header is judged here (as RFC 1950 says) and the deflate data by zlib's raw inflater;
    the Adler-32 trailer is not read by either side (20.13)."""
    if len(z) < 2:
        return b"", "trunc", ""
    cmf, flg = z[0], z[1]
    if (cmf & 15) != 8 or (cmf >> 4) > 7 or (flg & 0x20) or (cmf * 256 + flg) % 31:
        return b"", "error", "header"
    d = zlib.decompressobj(-15)
    out = bytearray()
    body = z[2:]
    for i in range(len(body)):
        try:
            out.extend(d.decompress(body[i:i + 1]))
        except zlib.error as e:
            return bytes(out), "error", str(e)
        if d.eof:
            return bytes(out), "done", ""
    return bytes(out), "trunc", ""


def check_malformed(E, rng, nfuzz):
    data = corpus(rng)[2][1][:1500]
    good = [zl(data, 6), zl(data, 6, zlib.Z_FIXED), zl(data, 0), zl(b"abc" * 100, 9)]
    for zi, z in enumerate(good):
        # every truncation: the output is the independent decoder's partial output, the rc is TRUNC
        step = 1 if len(z) < 400 else 7
        for cut in range(0, len(z), step):
            part = z[:cut]
            want, how, _ = partial_zlib(part)
            rc, got = E.run(build_pdf([(b"/Filter /FlateDecode", part)]), chunk=1)
            if how == "trunc":
                ok = rc in (TRUNC, SYNTAX) and got == want[:len(got)] and len(got) == len(want)
                if cut < 2 and rc == 0: ok = (got == b"")
            else:
                ok = rc == 0 and got == want
            if not ok:
                fail("truncated %d@%d: rc=%r got %d want %d (%s)" % (zi, cut, rc, len(got), len(want), how))
    # corruptions: when zlib (raw, header checked by hand) sees a structural error, so must the engine,
    # and the bytes before it must agree
    for zi, z in enumerate(good):
        for _ in range(nfuzz):
            b = bytearray(z)
            for _ in range(rng.choice((1, 1, 2, 5))):
                b[rng.randrange(len(b))] ^= 1 << rng.randrange(8)
            b = bytes(b)
            want, how, why = partial_zlib(b)
            rc, got = E.run(build_pdf([(b"/Filter /FlateDecode", b)]), chunk=1)
            if isinstance(rc, str):
                fail("corrupt %d: %s" % (zi, rc)); continue
            if how == "error":
                if rc not in (SYNTAX, TRUNC) or got != want[:len(got)] or len(got) != len(want):
                    fail("corrupt %d: zlib says %r (%d good bytes); engine rc=%r bytes=%d" % (zi, why, len(want), rc, len(got)))
            elif how == "done":
                if rc != 0 or got != want:
                    fail("corrupt-but-valid %d: rc=%r bytes=%d want %d" % (zi, rc, len(got), len(want)))
    # named malformations
    def raw(bits):
        return bytes([0x78, 0x9C]) + bits
    cases = {
        "block type 3": raw(bytes([0x07])),
        "stored nlen": raw(bytes([0x01, 0x05, 0x00, 0x00, 0x00, 1, 2, 3, 4, 5])),
        "bad header cm": bytes([0x79, 0x9C, 0x03, 0x00]),
        "bad header fcheck": bytes([0x78, 0x9D, 0x03, 0x00]),
        "fdict": bytes([0x78, 0xBB, 0, 0, 0, 0, 0x03, 0x00]),
        "dist too far": raw(bytes([0x4B, 0x4C, 0x4A, 0x06, 0x01])),
    }
    for name, z in cases.items():
        rc, got = E.run(build_pdf([(b"/Filter /FlateDecode", z)]))
        if rc not in (SYNTAX, TRUNC):
            fail("%s: rc=%r" % (name, rc))
    # a distance beyond the history: fixed block, literal 'a', match len 3 dist 5
    # (built by hand: BFINAL=1 BTYPE=01, 'a', length 3, distance code 4 +1 extra bit)
    class W:
        def __init__(s): s.v = 0; s.n = 0
        def put(s, val, n): s.v |= val << s.n; s.n += n
        def code(s, val, n):
            r = 0
            for i in range(n): r |= ((val >> i) & 1) << (n - 1 - i)
            s.put(r, n)
        def bytes(s):
            return s.v.to_bytes((s.n + 7) // 8, "little")
    w = W(); w.put(1, 1); w.put(1, 2); w.code(0x30 + ord("a"), 8); w.code(1, 7); w.code(4, 5); w.put(0, 1); w.code(0, 7)
    z = bytes([0x78, 0x9C]) + w.bytes()
    rc, got = E.run(build_pdf([(b"/Filter /FlateDecode", z)]), chunk=1)
    if rc != SYNTAX or got != b"a":
        fail("distance too far: rc=%r got=%r" % (rc, got))
    # a distance exactly at the start is fine, with dist == bytes so far
    w = W(); w.put(1, 1); w.put(1, 2); w.code(0x30 + ord("a"), 8); w.code(0x30 + ord("b"), 8); w.code(1, 7); w.code(1, 5); w.code(0, 7)
    # length code 257 (3) with distance code 1 (dist 2)
    z = bytes([0x78, 0x9C]) + w.bytes()
    rc, got = E.run(build_pdf([(b"/Filter /FlateDecode", z)]))
    if rc != 0 or got != b"ababa":
        fail("dist == history: rc=%r got=%r" % (rc, got))
    check_blockflood(E, W)


def check_blockflood(E, W):
    """Blocks that decode to little or nothing are still work (20.13.4, verify-flate2): more than
    FL_MAXEMPTY (64) block headers with no output byte between them is PDE_LIMIT, and a header
    that builds tables is charged FL_TABCOST (1024) against the 4 MiB work bound. Each stream is
    first checked to be VALID deflate by Python's inflater, so the refusal is the bound's."""
    def empties(n):                              # n empty stored blocks, then 'a' in a final fixed block
        w = W()
        for _ in range(n):
            w.put(0, 1); w.put(0, 2); w.n = (w.n + 7) & ~7; w.put(0, 16); w.put(0xFFFF, 16)
        w.put(1, 1); w.put(1, 2); w.code(0x30 + ord("a"), 8); w.code(0, 7)
        return bytes([0x78, 0x9C]) + w.bytes()
    def dyn1(w, final):                          # a dynamic block whose only output is 'a'
        w.put(final, 1); w.put(2, 2); w.put(0, 5); w.put(0, 5); w.put(14, 4)    # HLIT 257, HDIST 1, HCLEN 18
        cl = {0: 2, 1: 2, 17: 2, 18: 2}          # code-length code: 0, 1, 17, 18 at 2 bits (complete)
        for s in (16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1):
            w.put(cl.get(s, 0), 3)
        c = {0: 0, 1: 1, 17: 2, 18: 3}           # canonical: by symbol
        w.code(c[18], 2); w.put(97 - 11, 7)      # literals 0..96: zero
        w.code(c[1], 2)                          # 'a': 1
        w.code(c[18], 2); w.put(138 - 11, 7)     # 98..235: zero
        w.code(c[18], 2); w.put(20 - 11, 7)      # 236..255: zero
        w.code(c[1], 2)                          # 256: 1
        w.code(c[0], 2)                          # the one distance code: 0
        w.code(0, 1); w.code(1, 1)               # 'a', end of block
    def dyns(n):
        w = W()
        for k in range(n):
            dyn1(w, int(k == n - 1))
        return bytes([0x78, 0x9C]) + w.bytes()
    for name, z, wantrc, wantn in (("63 empty blocks and a fixed one", empties(63), 0, 1), ("65 empty blocks", empties(65), LIMIT, 0),
                                   ("3,000 one-byte dynamic blocks", dyns(3000), 0, 3000),
                                   ("5,000 one-byte dynamic blocks", dyns(5000), LIMIT, None)):
        out, how, why = partial_zlib(z)
        if how != "done" or out != b"a" * (len(out)):
            fail("%s: not valid deflate (%s %s)" % (name, how, why)); continue
        rc, got = E.run(build_pdf([(b"/Filter /FlateDecode", z)]), chunk=512)
        if rc != wantrc or (wantn is not None and len(got) != wantn) or got != b"a" * len(got):
            fail("%s: rc=%r bytes=%d (want rc=%r bytes=%r)" % (name, rc, len(got), wantrc, wantn))


def check_nest(E, rng):
    a = corpus(rng)[2][1] * 3
    b = (corpus(rng)[7][1])[:20000]
    pdf = build_pdf([(b"/Filter /FlateDecode", zl(a)), (b"/Filter /FlateDecode", zl(b)), (b"/Filter /FlateDecode", zl(a[:900]))])
    pf = os.path.join(E.work, "nest.pdf")
    open(pf, "wb").write(pdf)
    for ch in (1, 37, 500):
        oa, ob = os.path.join(E.work, "na.out"), os.path.join(E.work, "nb.out")
        p = subprocess.run([E.exe, "nest", pf, "10", "11", oa, ob, "-r", str(ch)], capture_output=True, text=True)
        if "rc=0 read=0" not in p.stdout or open(oa, "rb").read() != a or open(ob, "rb").read() != b:
            fail("nest r%d: %s" % (ch, p.stdout.strip() or p.stderr[-200:]))
    p = subprocess.run([E.exe, "stack", pf, "10", "11", "12"], capture_output=True, text=True)
    want = "OPEN 10 rc=0\nOPEN 11 rc=0\nOPEN 12 rc=33\n"
    if p.stdout != want:
        fail("a third stream: %r" % p.stdout)
    p = subprocess.run([E.exe, "stack", pf, "10", "11", "12", "-w", "43"], capture_output=True, text=True)
    if p.stdout != "OPEN 10 rc=0\nOPEN 11 rc=33\nOPEN 12 rc=33\n":
        fail("one slot, a second stream: %r" % p.stdout)


def check_samples(E):
    path = os.path.join(ROOT, "apps", "pagedesk", "samples", "FLATE.PDF")
    pdf = open(path, "rb").read()
    q = subprocess.run(["qpdf", "--show-object=5", "--filtered-stream-data", path], capture_output=True).stdout
    rc, got = E.run(pdf, obj=5)
    compare("FLATE.PDF object 5", got, q, rc)
    rc, _ = E.run(pdf, obj=5, w=0)
    if rc != FLATE:
        fail("FLATE.PDF without the claim: rc=%r" % rc)


def check_tables():
    """pdeflate.c's constants are pxpng.asm's (20.13.2)."""
    asm = open(os.path.join(ROOT, "apps", "pixel", "pxpng.asm")).read()
    src = open(os.path.join(ROOT, "apps", "pagedesk", "pdeflate.c")).read()
    def asm_tab(label):
        m = re.search(r"^%s:\s*(?:dw|db)(.*?)(?=^\w+:|^;)" % label, asm, re.S | re.M)
        body = re.sub(r";[^\n]*", "", m.group(1))
        body = re.sub(r"\b(?:dw|db)\b", ",", body)
        return [int(x) for x in re.findall(r"\d+", body)]
    def c_tab(name):
        m = re.search(r"fl_%s\[\d+\] = \{(.*?)\};" % name, src, re.S)
        return [int(x) for x in re.findall(r"\d+", m.group(1))]
    for a, c in (("pz_lbase", "lbase"), ("pz_lext", "lext"), ("pz_dbase", "dbase"), ("pz_dext", "dext"), ("pz_clord", "clord")):
        if asm_tab(a) != c_tab(c):
            fail("pdeflate.c fl_%s differs from pxpng.asm %s" % (c, a))


def write_corpus(d):
    """PDFs for pde_fl_fuzz: one Flate stream each, as object 10."""
    rng = random.Random(7)
    os.makedirs(d, exist_ok=True)
    text = corpus(rng)[2][1]
    cols, rows = 40, 12
    raw = bytes(rng.randrange(256) for _ in range(cols * 3 * rows))
    files = {
        "plain": b"/Filter /FlateDecode",
        "fixed": b"/Filter /FlateDecode",
        "stored": b"/Filter /FlateDecode",
        "png": b"/Filter /FlateDecode /DecodeParms << /Predictor 15 /Colors 3 /BitsPerComponent 8 /Columns 40 >>",
        "tiff": b"/Filter /FlateDecode /DecodeParms << /Predictor 2 /Colors 3 /BitsPerComponent 16 /Columns 20 >>",
        "hex": b"/Filter [/AHx /Fl]",
    }
    data = {"plain": zl(text), "fixed": zl(text, 6, zlib.Z_FIXED), "stored": zl(text[:3000], 0),
            "png": zl(png_encode(raw, cols * 3, 3, (0, 1, 2, 3, 4))),
            "tiff": zl(tiff_encode(raw[:cols * 3 * 6], 120, 3, 16)),
            "hex": zl(text[:2000]).hex().upper().encode() + b">"}
    for k in files:
        open(os.path.join(d, k + ".pdf"), "wb").write(build_pdf([(files[k], data[k])]))


def check_asm_layout():
    """pdeflate.inc's FL_* offsets and table addresses are struct flst's and pdeflate.h's, on the TARGET's
    16-bit layout (every pdw two bytes, pdb one, no padding)."""
    h = open(os.path.join(ROOT, "apps", "pagedesk", "pdeflate.h")).read()
    inc = open(os.path.join(ROOT, "apps", "pagedesk", "pdeflate.inc")).read()
    consts = {k: int(v.rstrip("u"), 0) for k, v in re.findall(r"#define\s+(\w+)\s+(0x[0-9A-Fa-f]+u?|\d+u?)\b", h)}
    body = re.search(r"struct flst \{(.*?)\n\};", h, re.S).group(1)
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    off, offs = 0, {}
    for decl in body.split(";"):
        decl = decl.strip()
        if not decl:
            continue
        kind, names = decl.split(None, 1)
        for n in names.split(","):
            n = n.strip()
            m = re.match(r"(\w+)(?:\[(\w+)(?:\s*\+\s*(\w+))?\])?$", n)
            cnt = 1
            if m.group(2):
                cnt = consts.get(m.group(2), None) if not m.group(2).isdigit() else int(m.group(2))
                if m.group(3):
                    cnt += int(m.group(3))
            offs[m.group(1)] = off
            off += cnt * (2 if kind == "pdw" else 1)
    want = {"FL_SEG": offs["seg"], "FL_STATE": offs["state"], "FL_FINAL": offs["final"], "FL_BB": offs["bb"], "FL_BN": offs["bn"],
            "FL_MLEFT": offs["mleft"], "FL_MDIST": offs["mdist"], "FL_WPOS": offs["wpos"], "FL_WHAVE": offs["whave"],
            "FL_ON": offs["on"], "FL_II": offs["ii"], "FL_IN": offs["in"], "FL_OB": offs["ob"], "FL_IB": offs["ib"],
            "FL_OBSZ": consts["PDE_FLOB"], "FL_TABLUT": consts["PDE_FLTAB"] + consts["FT_LUT"],
            "FL_TABDLUT": consts["PDE_FLTAB"] + consts["FT_DLUT"], "FL_S_BLK": consts["FS_BLK"], "FL_S_END": consts["FS_END"]}
    for k, v in re.findall(r"^(FL_\w+)\s+equ\s+(\d+)", inc, re.M):
        if k in want and int(v) != want[k]:
            fail("pdeflate.inc %s = %s, pdeflate.h says %d" % (k, v, want[k]))
        want.pop(k, None)
    for k in want:
        fail("pdeflate.inc does not define %s" % k)
    # pdeflate.inc reaches no further than ib: the fields after it (LZW's, 20.22.3.6) start where ib
    # ends, so its offsets did not move; and the parse accounted for every byte up to the end
    names = list(offs)
    after = names[names.index("ib") + 1:]
    ib_end = offs["ib"] + consts["PDE_FLIB"]
    if (offs[after[0]] if after else off) != ib_end:
        fail("struct flst parse: ib does not end where the next field starts")
    if after and offs[after[-1]] + 2 != off:
        fail("struct flst parse: size mismatch after ib (%s)" % ", ".join(after))
    if PDE_FLSAVE_END(consts, off) > consts["PDE_FLSLOT_KB"] * 1024:
        fail("the saved decoder state does not fit its slot")


def PDE_FLSAVE_END(consts, size):
    return consts["PDE_FLSAVE"] + size


def main():
    if sys.argv[1] == "--corpus":
        write_corpus(sys.argv[2])
        return
    exe, build = sys.argv[1], sys.argv[2]
    nfuzz = 40
    if "--fuzz" in sys.argv:
        nfuzz = int(sys.argv[sys.argv.index("--fuzz") + 1])
    rng = random.Random(20261105)
    E = Engine(exe, os.path.join(build, "pdflate-work"))
    for name, fn in (("tables", lambda: check_tables()), ("asmlayout", check_asm_layout), ("samples", lambda: check_samples(E)),
                     ("basic", lambda: check_basic(E, rng)), ("chain", lambda: check_chain(E, rng)),
                     ("predictors", lambda: check_predictors(E, rng)), ("nest", lambda: check_nest(E, rng)),
                     ("malformed", lambda: check_malformed(E, rng, nfuzz))):
        before = len(FAILS)
        fn()
        print("%-11s %s (%d engine runs so far)" % (name, "ok" if len(FAILS) == before else "FAILED", NCASE))
    print("FAILURES: %d" % len(FAILS))
    sys.exit(1 if FAILS else 0)


if __name__ == "__main__":
    main()
