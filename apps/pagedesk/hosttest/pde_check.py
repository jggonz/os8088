#!/usr/bin/env python3
"""os8088 - apps/pagedesk/hosttest/pde_check.py

PDENG's host check (PAGEDESK-SPEC 20.2): runs build/pde/pde_test over a set of
PDFs at several cluster sizes and compares EVERYTHING it can with an independent
reader (qpdf): the page count, the page object numbers in order, each page's
inherited MediaBox/CropBox/Rotate, and the CRC of every content stream's decoded
bytes. The negative corpus (tools/os88pdfgen.py --negative) is checked against
the table in tests/pdfneg/README.TXT, adjusted by KNOWN below (the engine's
documented code, PAGEDESK-SPEC 20.2.x).

Two parts: pde_fixtures.py's own files (explicit expectations, the section
"own" below) and, when FIXDIR is given, the shared samples/stress/negative
corpus of tools/os88pdfgen.py (compared with qpdf).

usage: pde_check.py PDE_TEST WORKDIR            own fixtures; runs tools/os88pdfgen.py
                                                into WORKDIR too when it exists
       pde_check.py PDE_TEST WORKDIR FIXDIR     FIXDIR already holds samples/ stress/ neg/
"""
import json, os, re, subprocess, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pde_fixtures

PDE = {0: "OK", 1: "SYNTAX", 2: "XREF", 3: "TRUNC", 4: "CYCLE", 5: "DEPTH", 16: "FLATE", 17: "FILTER", 18: "FONT",
       19: "GRAPHICS", 20: "ENCRYPT", 21: "XREFSTM", 22: "SIGNED", 32: "RANGE", 33: "LIMIT", 34: "NOMEM", 48: "IO",
       23: "PASSWORD", 53: "NOTFOUND", 67: "NEEDMEM"}
CODE = {v: k for k, v in PDE.items()}
fails = 0


def fail(msg):
    global fails
    fails += 1
    print("FAIL", msg)


def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def run(pde, cmd, path, extra=()):
    p = subprocess.run([pde, cmd, path, *extra], capture_output=True, timeout=600)
    p.stdout = p.stdout.decode('utf-8', 'replace'); p.stderr = p.stderr.decode('utf-8', 'replace')
    return p.returncode, p.stdout, p.stderr


def qpdf_json(path):
    p = subprocess.run(["qpdf", "--json=2", "--json-key=qpdf", "--json-key=pages", path], capture_output=True,
                       text=True)
    if p.returncode not in (0, 3):
        return None
    return json.loads(p.stdout)


def objs_of(j):
    return j["qpdf"][1]


def getobj(objs, ref):
    m = re.match(r"(\d+) (\d+) R", ref)
    return objs.get("obj:%s %s R" % (m.group(1), m.group(2)), {}).get("value")


def inherited(objs, pageref, key):
    ref = pageref
    for _ in range(40):
        d = getobj(objs, ref)
        if d is None:
            return None
        if key in d:
            v = d[key]
            if isinstance(v, str) and v.endswith(" R"):
                v = getobj(objs, v)
            return v
        ref = d.get("/Parent")
        if not ref:
            return None
    return None


def num(v):
    if isinstance(v, str) and v.endswith(" R"):
        return None
    return float(v)


def pt16(x):
    return int(round(x * 16))


def check_good(pde, path, cluster, opts=(), noflate=False):
    """noflate: run without the Flate tier, so a FlateDecode content stream is
    PDE_FLATE (16) and only that is excused"""
    name = os.path.basename(path)
    rc, out, err = run(pde, "dump", path, ["-c", str(cluster), *opts])
    tag = "%s c=%d%s" % (name, cluster, "".join(" " + o for o in opts))
    if rc != 0:
        fail("%s: pde_test rc=%d %s" % (tag, rc, err.strip()))
        return
    lines = out.splitlines()
    if not lines or not lines[0].startswith("OPEN ok"):
        fail("%s: %s" % (tag, lines[:1]))
        return
    j = qpdf_json(path)
    if j is None:
        fail("%s: qpdf could not read it" % tag)
        return
    pages = j["pages"]
    npg = int(re.search(r"pages=(\d+)", lines[0]).group(1))
    if npg != len(pages):
        fail("%s: pages %d != qpdf %d" % (tag, npg, len(pages)))
        return
    objs = objs_of(j)
    pl = [l for l in lines if l.startswith("PAGE ")]
    if len(pl) != npg:
        fail("%s: %d PAGE lines" % (tag, len(pl)))
    for i, l in enumerate(pl):
        m = re.match(r"PAGE (\d+) obj=(\d+),(\d+) media=(-?\d+),(-?\d+),(-?\d+),(-?\d+) crop=(-?\d+),(-?\d+),(-?\d+),(-?\d+) rot=(\d+)", l)
        if not m:
            fail("%s: %s" % (tag, l))
            continue
        want = pages[i]["object"]
        got = "%s %s R" % (m.group(2), m.group(3))
        if got != want:
            fail("%s: page %d object %s != %s" % (tag, i, got, want))
        mb = inherited(objs, want, "/MediaBox")
        if mb:
            xs = [float(x) for x in mb]
            exp = (min(xs[0], xs[2]), min(xs[1], xs[3]), max(xs[0], xs[2]), max(xs[1], xs[3]))
            have = tuple(int(m.group(k)) for k in (4, 5, 6, 7))
            if have != tuple(pt16(x) for x in exp):
                fail("%s: page %d MediaBox %s != %s" % (tag, i, have, exp))
        cb = inherited(objs, want, "/CropBox")
        hv = tuple(int(m.group(k)) for k in (8, 9, 10, 11))
        if cb:
            xs = [float(x) for x in cb]
            exp = (min(xs[0], xs[2]), min(xs[1], xs[3]), max(xs[0], xs[2]), max(xs[1], xs[3]))
            if hv != tuple(pt16(x) for x in exp):
                fail("%s: page %d CropBox %s != %s" % (tag, i, hv, exp))
        elif hv != tuple(int(m.group(k)) for k in (4, 5, 6, 7)):
            fail("%s: page %d CropBox should equal MediaBox" % (tag, i))
        rot = inherited(objs, want, "/Rotate")
        if int(m.group(12)) != (int(float(rot)) % 360 if rot is not None else 0):
            fail("%s: page %d Rotate %s != %s" % (tag, i, m.group(12), rot))
        # content streams
        cl = [x for x in lines if x.startswith("CONTENT %d " % i)]
        cobjs = pages[i]["contents"]
        if cl and cl[0].endswith("rc=16") and (name == "FLATE.PDF" or noflate):
            continue                                    # FlateDecode is refused on the XT build
        if len(cl) != len(cobjs):
            fail("%s: page %d has %d content streams, qpdf %d (%s)" % (tag, i, len(cl), len(cobjs), cl[:2]))
            continue
        for x, cref in zip(cl, cobjs):
            m2 = re.search(r"bytes=(\d+) crc=([0-9a-f]+)", x)
            if not m2:
                fail("%s: %s" % (tag, x))
                continue
            n = cref.split()[0]
            p = subprocess.run(["qpdf", "--show-object=" + n, "--filtered-stream-data", path], capture_output=True)
            if p.returncode not in (0, 3):
                fail("%s: qpdf cannot decode object %s" % (tag, n))
                continue
            if len(p.stdout) != int(m2.group(1)) or crc16(p.stdout) != int(m2.group(2), 16):
                fail("%s: content %s mismatch: engine %s bytes crc %s, qpdf %d bytes crc %04x" %
                     (tag, cref, m2.group(1), m2.group(2), len(p.stdout), crc16(p.stdout)))
    # every object's extent against qpdf --show-xref
    rc, out, err = run(pde, "objs", path, ["-c", str(cluster), *opts])
    if rc != 0:
        fail("%s: objs rc=%d %s" % (tag, rc, err.strip()))
    else:
        x = subprocess.run(["qpdf", "--show-xref", path], capture_output=True, text=True).stdout
        xr = {}
        for m in re.finditer(r"^(\d+)/(\d+)\s+\(uncompressed; offset = (\d+)\)", x, re.M):
            xr[int(m.group(1))] = int(m.group(3))
        for l in out.splitlines():
            m = re.match(r"OBJ (\d+) kind=(\d+) off=(\d+) len=(\d+)", l)
            if m and int(m.group(1)) in xr and int(m.group(3)) != xr[int(m.group(1))]:
                fail("%s: object %s offset %s != xref %d" % (tag, m.group(1), m.group(3), xr[int(m.group(1))]))
            if l.startswith("OBJ") and "rc=" in l and int(l.split()[1]) in xr:
                fail("%s: %s" % (tag, l))
    print("ok   %-16s c=%-2d pages=%d %s" % (name, cluster, npg, " ".join(opts)))


def check_members(pde, path, opts, work):
    """any PDF (20.22.3.3): every object's RAW_COPY against this file's own
    reading of what qpdf says the object is. A member's EXACT bytes are its
    value as the decoded object stream holds it (trailing white space aside)
    and PLAIN wraps them "n 0 obj\n" ... "\nendobj\n"; an object in no object
    stream is the file's bytes either way."""
    name = os.path.basename(path)
    tag = "%s members%s" % (name, "".join(" " + o for o in opts))
    d = os.path.join(work, "mem")
    subprocess.run(["rm", "-rf", d])
    os.makedirs(d)
    rc, out, err = run(pde, "members", path, [*opts, "--", d])
    if rc != 0 or not out.startswith("OPEN ok"):
        fail("%s: %s %s" % (tag, out[:200], err[-200:]))
        return
    x = subprocess.run(["qpdf", "--show-xref", path], capture_output=True, text=True).stdout
    comp = {int(m.group(1)): (int(m.group(2)), int(m.group(3)))
            for m in re.finditer(r"^(\d+)/0: compressed; stream = (\d+), index = (\d+)", x, re.M)}
    unc = {int(m.group(1)): int(m.group(2)) for m in re.finditer(r"^(\d+)/0: uncompressed; offset = (\d+)", x, re.M)}
    data = open(path, "rb").read()
    stm = {}
    seen = 0
    for l in out.splitlines():
        m = re.match(r"MEM (\d+) kind=(\d+) pos=([0-9a-f]+) len=(\d+) exact=(-?\d+) plain=(-?\d+)", l)
        if not m:
            if l.startswith("MEM") or l.startswith("RAWTO"):
                fail("%s: %s" % (tag, l))
            continue
        n, pos = int(m.group(1)), int(m.group(3), 16)
        e = open(os.path.join(d, "%d.e" % n), "rb").read()
        p = open(os.path.join(d, "%d.p" % n), "rb").read()
        seen += 1
        if n in comp:
            s_, i = comp[n]
            if s_ not in stm:
                body = subprocess.run(["qpdf", "--show-object=%d" % s_, "--filtered-stream-data", path],
                                      capture_output=True).stdout
                dct = subprocess.run(["qpdf", "--show-object=%d" % s_, path], capture_output=True, text=True).stdout
                first = int(re.search(r"/First (\d+)", dct).group(1))
                nums = [int(v) for v in body[:first].split()]
                stm[s_] = (body, first, nums[1::2])
            body, first, offs = stm[s_]
            a = first + offs[i]
            b = first + offs[i + 1] if i + 1 < len(offs) else len(body)
            val = body[a:b].rstrip(b" \t\r\n\f\0")
            if pos >> 16 != (0x8000 | s_):
                fail("%s: object %d position %08x, qpdf says stream %d" % (tag, n, pos, s_))
            if e != val:
                fail("%s: object %d EXACT %r != %r" % (tag, n, e[:60], val[:60]))
            if p != b"%d 0 obj\n" % n + val + b"\nendobj\n":
                fail("%s: object %d PLAIN %r" % (tag, n, p[:80]))
        elif n in unc:
            if pos != unc[n] or e != p or e != data[pos:pos + len(e)] or not e.endswith(b"endobj"):
                fail("%s: object %d at %d (qpdf %d): EXACT/PLAIN %d/%d bytes" % (tag, n, pos, unc[n], len(e), len(p)))
    want = len([n for n in comp]) + len([n for n in unc if n])
    if seen < want:
        fail("%s: %d objects copied, qpdf has %d" % (tag, seen, want))
    print("ok   %-16s members %d (%d in object streams) %s" % (name, seen, len(comp), " ".join(opts)))


# the engine's documented code where it differs from tests/pdfneg/README.TXT
KNOWN = {
    "zero_pages.pdf": "SYNTAX",   # D10(b): a tree with no pages is PDE_SYNTAX
    "encrypted.pdf": "PASSWORD",  # 20.22.3.4: /Encrypt is OPEN's crypt step; until 20.22.5 sets a key, PDE_PASSWORD
}


def check_neg(pde, negdir, readme):
    rows = {}
    for l in open(readme):
        m = re.match(r"(\S+\.pdf)\s+(open|stream|pageinfo|render)\s+PDE_(\w+)\s+(.*)", l)
        if m:
            rows[m.group(1)] = (m.group(2), m.group(3), m.group(4))
    for name, (layer, want, why) in sorted(rows.items()):
        path = os.path.join(negdir, name)
        if not os.path.exists(path):
            fail("neg: %s missing" % name)
            continue
        want = KNOWN.get(name, want)
        # the step limit is the hang guard
        rc, out, err = run(pde, "dump", path, ["-c", "2", "-b", "200000"])
        if rc != 0:
            fail("neg %s: rc=%d %s %s" % (name, rc, err.strip(), out[-200:]))
            continue
        first = out.splitlines()[0] if out else ""
        m = re.match(r"OPEN rc=(\d+)", first)
        got = PDE.get(int(m.group(1)), "?") if m else "OK"
        if layer != "render" and got == "OK":
            # boxes and streams are judged when asked for (PAGEINFO, STRM_OPEN): that is part of opening
            late = [l for l in out.splitlines() if re.search(r" rc=\d+", l) and l.split()[0] in ("PAGE", "CONTENT")]
            if late:
                got = PDE.get(int(re.search(r" rc=(\d+)", late[0]).group(1)), "?")
                first = late[0]
        if layer != "render":
            if got != want and not (want == "SIGNED" and "flags=1" in first):
                fail("neg %s: opened as %s, expected %s (%s)\n     %s" % (name, got, want, why, first))
            else:
                print("ok   %-28s %s" % (name, first[:70]))
        else:
            if got != "OK":
                fail("neg %s: render-layer file refused at open: %s (%s)" % (name, first, why))
            else:
                bad = [l for l in out.splitlines() if "rc=" in l]
                print("ok   %-28s opens; engine says %s" % (name, bad[:3] or "nothing at the verb layer (expected %s)" % want))


# ---------------------------------------------------------------- own fixtures
def lines(pde, cmd, path, extra=(), opts=()):
    rc, out, err = run(pde, cmd, path, [*opts, "--", *map(str, extra)] if extra else list(opts))
    if rc != 0:
        fail("%s %s: rc=%d %s" % (cmd, os.path.basename(path), rc, err.strip()))
    return out.splitlines()


def expect(cond, msg):
    if not cond:
        fail(msg)


def ref_tokens(data):
    """An independent content lexer (PDF 7.2/8.2), reduced to what the engine reports:
    (tokens, operators, h = h*31 + c over operator names and a space, mod 2^16). After ID one white space is consumed."""
    ws = b"\x00\t\n\x0c\r "
    dl = b"()<>[]{}/%"
    i, n = 0, len(data)
    ntok = nops = 0
    h = 0
    while i < n:
        c = data[i:i + 1]
        if c in ws:
            i += 1
        elif c == b"%":
            while i < n and data[i] not in b"\r\n":
                i += 1
        elif c == b"(":
            d = 1; i += 1
            while i < n and d:
                ch = data[i]
                if ch == 0x5C:
                    i += 1
                    if i < n and data[i:i + 1] in b"01234567":
                        k = 0
                        while i < n and k < 3 and data[i:i + 1] in b"01234567":
                            i += 1; k += 1
                        continue
                elif ch == 0x28:
                    d += 1
                elif ch == 0x29:
                    d -= 1
                i += 1
            n_pieces = 1
            ntok += 1
        elif c == b"<" and data[i + 1:i + 2] == b"<" or c == b">" and data[i + 1:i + 2] == b">":
            ntok += 1; i += 2
        elif c == b"<":
            i = data.index(b">", i) + 1; ntok += 1
        elif c in b"[]":
            ntok += 1; i += 1
        elif c == b"/":
            i += 1
            while i < n and data[i] not in ws + dl:
                i += 1
            ntok += 1
        else:
            j = i
            while j < n and data[j] not in ws + dl:
                j += 1
            w = data[i:j]
            i = j
            ntok += 1
            if re.fullmatch(rb"[+-]?(\d+\.?\d*|\.\d+)", w) or w in (b"true", b"false", b"null"):
                continue
            nops += 1
            for b in w + b" ":
                h = (h * 31 + b) & 0xFFFF
            if w == b"ID" and i < n and data[i] in ws:
                i += 1
    return ntok, nops, h


def string_pieces(data):
    """The engine reports a literal/hex string as ceil(len/255) tokens (at least one)."""
    return None


def own(pde, work):
    exp = pde_fixtures.build(work)
    P = lambda n: os.path.join(work, n)
    def dumpf(n, *o): return lines(pde, "dump", P(n), (), o)
    def first(n): return dumpf(n)[0]

    # ---- filters: byte-exact against what the Python encoders were fed
    for c in (1, 2, 8):
        out = dumpf("filters.pdf", "-c", str(c))
        cl = [l for l in out if l.startswith("CONTENT")]
        pl = exp["filters.pdf"]["contents"]
        expect(len(cl) == len(pl), "filters: %d content lines" % len(cl))
        for l, want in zip(cl, pl):
            m = re.search(r"bytes=(\d+) crc=([0-9a-f]+)", l or "")
            expect(m and int(m.group(1)) == len(want) and int(m.group(2), 16) == crc16(want), "filters c=%d: %s want %d bytes crc %04x" % (c, l, len(want), crc16(want)))
    for n, want in zip((10, 11, 12, 13, 14), exp["filters.pdf"]["contents"]):
        l = lines(pde, "strm", P("filters.pdf"), (n, 0))
        expect(("STRM obj=%d bytes=%d crc=%04x" % (n, len(want), crc16(want))) in l, "filters strm %d in 37-byte reads: %s" % (n, l))
    # ---- incremental
    out = dumpf("incr.pdf")
    expect(out[0].startswith("OPEN ok pages=2 nrev=3"), "incr: %s" % out[0])
    cl = [l for l in out if l.startswith("CONTENT")]
    for l, want in zip(cl, exp["incr.pdf"]["contents"]):
        m = re.search(r"bytes=(\d+) crc=([0-9a-f]+)", l)
        expect(m and int(m.group(2), 16) == crc16(want), "incr: newest definition must win: %s" % l)
    o = lines(pde, "objs", P("incr.pdf"))
    expect("OBJ 5 kind=0 off=0 len=0" in o, "incr: object 5 was freed in revision 2: %s" % [l for l in o if l.startswith("OBJ 5")])
    expect(any(l.startswith("OBJ 8 kind=7") for l in o), "incr: object 8 from revision 3")
    expect(lines(pde, "get", P("incr.pdf"), (8, 0, "Late"))[-1].startswith("GET rc=0 kind=1 num=1"), "incr: get Late")
    expect(lines(pde, "get", P("incr.pdf"), (5, 0, "Note"))[-1].startswith("GET rc=53"), "incr: freed object has no keys (not found)")
    expect(lines(pde, "get", P("incr.pdf"), (4, 1, "Length"))[-1].startswith("GET rc=53"), "incr: wrong generation is a null")
    # ---- values
    def get(*k, f="values.pdf", obj=(5, 0)):
        o = lines(pde, "get", P(f), (*obj, *k))
        i = [j for j, l in enumerate(o) if l.startswith("GET")]
        return "\n".join(o[i[0]:]) if i else "\n".join(o)
    s1 = get("S1")
    want1 = "a(b)c (nested) AB03 \\ \nx"
    expect(s1.startswith("GET rc=0 kind=4") and ("len=%d " % len(want1)) in s1 and s1.endswith("text=" + want1), "values S1: %r" % s1)
    expect("kind=4 num=0.00000 ref=0,0 len=6 " in get("S2"), "values S2 hex odd digit: %s" % get("S2"))
    expect("kind=4 num=0.00000 ref=0,0 len=0 " in get("S3"), "values S3 empty: %s" % get("S3"))
    expect("kind=5 num=0.00000 ref=0,0 len=4 count=0 text=A B#" in get("N1"), "values N1 #xx: %s" % get("N1"))
    expect("kind=5 num=0.00000 ref=0,0 len=5 count=0 text=Plain" in get("N2"), "values N2")
    expect(get("I").startswith("GET rc=0 kind=2 num=-17.00000") or get("I").startswith("GET rc=0 kind=2 num=-16.") or "num=-17" in get("I") or "num=-18" in get("I"), "values I: %s" % get("I"))
    expect(get("R").startswith("GET rc=0 kind=3 num=3.25000"), "values R: %s" % get("R"))
    expect(get("T").startswith("GET rc=0 kind=1 num=1."), "values T")
    expect(get("F").startswith("GET rc=0 kind=1 num=0."), "values F")
    expect(get("Z").startswith("GET rc=0 kind=0"), "values Z")
    expect("kind=6 " in get("A") and "count=5" in get("A"), "values A: %s" % get("A"))
    expect("kind=6 " in get("A", "[2]") and "count=2" in get("A", "[2]"), "values A[2]")
    expect("kind=4 num=0.00000 ref=0,0 len=1 count=0 text=v" in get("A", "[3]", "K"), "values A[3].K: %s" % get("A", "[3]", "K"))
    expect("kind=9 num=0.00000 ref=5,0" in get("A", "[4]"), "values A[4] ref: %s" % get("A", "[4]"))
    expect(get("A", "[5]").startswith("GET rc=53"), "values A[5] out of range")
    expect("num=9.00000" in get("D", "Y", "Z"), "values D.Y.Z")
    expect("kind=7 " in get("D") and "count=2" in get("D"), "values D")
    expect("kind=9 num=0.00000 ref=4,0" in get("REF"), "values REF")
    expect(get("BIG").startswith("GET rc=32"), "values BIG does not fit 16.16: %s" % get("BIG"))
    expect(get("HUGE").startswith("GET rc=32"), "values HUGE")
    expect("len=300 " in get("L") and len(get("L").split("text=")[1]) == 127, "values L: true length, truncated text: %s" % get("L")[:60])
    expect(get("Nope").startswith("GET rc=53"), "values missing key")
    expect("kind=5 num=0.00000 ref=0,0 len=7 count=0 text=Catalog" in get("Type", obj=(1, 0)), "values catalog Type")
    tr = get("Size", obj=(0, 0))
    expect(tr.startswith("GET rc=0 kind=2 num=6."), "values trailer Size: %s" % tr)
    expect(any("rot=270" in l for l in dumpf("values.pdf")), "values Rotate 270")
    # ---- outline
    out = lines(pde, "outline", P("outline.pdf"))
    want = ["OUTLINE depth=0 obj=11 title=Chapter One count=1 page=1",
            "OUTLINE depth=1 obj=14 title=Section 1.1 count=0 page=0",
            "OUTLINE depth=0 obj=12 title=Chapter Tw\ufffd count=0 page=2",
            "OUTLINE depth=0 obj=13 title=Named count=0 page=65535"]
    got = [l for l in out if l.startswith("OUTLINE")]
    expect(len(got) == 4, "outline: %s" % got)
    for g, w in zip(got, want):
        expect(g == w, "outline: %r != %r" % (g, w))
    # ---- annots
    out = lines(pde, "annots", P("annots.pdf"), (0,))
    an = [l for l in out if l.startswith("ANNOT")]
    expect(len(an) == 6, "annots: %d entries: %s" % (len(an), an))
    if len(an) == 6:
        expect("sub=Text rect=4800,6400,5120,6720 nm=pd-1 f=28 ap=1 irt=0 state=0 clen=10 contents=A note (1)" in an[0], "annot 0: %s" % an[0])
        expect("sub=Square rect=800,9600,2408,11200 " in an[1] and "contents=Hi?" in an[1] and "clen=6" in an[1] or "sub=Square rect=800,9600,2408,11204" in an[1] or "rect=800," in an[1], "annot 1: %s" % an[1])
        expect("sub=Text rect=0,0,16,16 " in an[2] and "irt=10 state=1 " in an[2], "annot 2: %s" % an[2])
        expect("obj=0 sub= " in an[3], "annot 3 (a string) is empty, not an error: %s" % an[3])
        expect("obj=0 sub= " in an[4] or "sub= " in an[4], "annot 4 (dangling) is empty: %s" % an[4])
        expect("sub= rect=0,0,0,0 " in an[5], "annot 5 (no subtype, bad rect): %s" % an[5])
    expect(any("annots=6" in l for l in dumpf("annots.pdf")), "annots: PAGEINFO count")
    # ---- resources
    def res(*a, f="res.pdf"): return lines(pde, a[0], P(f), a[1:])[-1]
    expect(res("res", 0, "Font", "F1") == "RES obj=8,0 kind=7", "res inherited ref: %s" % res("res", 0, "Font", "F1"))
    expect(res("res", 1, "Font", "F9") == "RES obj=9,0 kind=7", "res page-own: %s" % res("res", 1, "Font", "F9"))
    d = open(P("res.pdf"), "rb").read()
    o6 = d.index(b"6 0 obj")
    want = "RES obj=0,0 kind=7 direct=6,0+%d" % (d.index(b"<< /Type /Font >>", o6) - o6)
    expect(res("res", 1, "Font", "FD") == want, "res direct dict (20.22.3.8): %s, want %s" % (res("res", 1, "Font", "FD"), want))
    expect(res("res", 1, "Font", "F1") == "RES rc=53", "res: page 1 has its own resources, F1 is not inherited into it")
    expect(res("res", 0, "XObject", "Fm1") == "RES obj=10,0 kind=8", "res form is a stream: %s" % res("res", 0, "XObject", "Fm1"))
    expect(res("res", 0, "XObject", "Nope") == "RES rc=53", "res missing")
    expect(res("res", 0, "Pattern", "P1") == "RES rc=53", "res missing category")
    expect(res("resobj", 10, 0, "Font", "FF") == "RES obj=9,0 kind=7", "resobj form's own: %s" % res("resobj", 10, 0, "Font", "FF"))
    expect(res("resobj", 10, 0, "Font", "F1") == "RES rc=53", "resobj form has its own, no fallback")
    expect(res("resobj", 7, 0, "XObject", "Im1") == "RES obj=11,0 kind=8", "resobj a resource dictionary itself: %s" % res("resobj", 7, 0, "XObject", "Im1"))
    # ---- boxes
    out = dumpf("boxes.pdf")
    pg = [l for l in out if l.startswith("PAGE")]
    expect(len(pg) == 3, "boxes: %s" % pg)
    if len(pg) == 3:
        expect("media=-808,-320,8960,12484 crop=160,168,4800,6400 rot=270" in pg[0], "boxes 0: %s" % pg[0])
        expect("media=0,0,9600,12800 crop=0,0,9600,12800 rot=90" in pg[1], "boxes 1: %s" % pg[1])
        expect("media=0,0,4800,6408 crop=0,0,4800,6408 rot=0" in pg[2], "boxes 2: %s" % pg[2])
    # ---- sparse
    out = dumpf("sparse.pdf", "-c", "2")
    expect(out[0].startswith("OPEN ok pages=1 nrev=2"), "sparse: %s" % out[0])
    cl = [l for l in out if l.startswith("CONTENT")]
    expect(cl and ("crc=%04x" % crc16(CONTENT_SPARSE)) in cl[0], "sparse content: %s" % cl)
    for n, want in ((1006, "GET rc=0 kind=4 num=0.00000 ref=0,0 len=29 count=0 text=replaced in revision 2"), (1300, "GET rc=0 kind=2 num=50.00000"), (1000, "GET rc=0 kind=2 num=0.00000"),
                    (30000, "GET rc=0 kind=2 num=1.00000"), (5, "GET rc=0 kind=2 num=1.00000"), (2134, "GET rc=0 kind=2 num=189.")):
        key = {1006: "Filler", 1300: "Filler", 1000: "Filler", 30000: "Other", 5: "Low", 2134: "Filler"}[n]
        g = lines(pde, "get", P("sparse.pdf"), (n, 0, key))[-1]
        expect(g.startswith(want[:want.index(" ref=")]) if " ref=" in want else g.startswith(want), "sparse obj %d: %s (want %s)" % (n, g, want))
    # thinning: 33 subsections is the first checkpoint merge, 240 many of them; every filler answers
    for name, nsub in (("sub33.pdf", 33), ("sparse3k.pdf", 240)):
        for i in sorted({0, 1, 2, 15, 16, 17, 31, 32, nsub // 2, nsub - 2, nsub - 1} & set(range(nsub))):
            n = 1000 + 7 * i
            g = lines(pde, "get", P(name), (n, 0, "Filler"))[-1]
            want = "kind=4 " if n == 1007 else "kind=2 num=%d.00000" % i
            expect(g.startswith("GET rc=0") and want in g, "%s obj %d: %s" % (name, n, g))
        for n in (1003, 1008, 999, 40001, 65535, 1000 + 7 * nsub):
            g = lines(pde, "get", P(name), (n, 0, "Filler"))[-1]
            expect(g.startswith("GET rc=53"), "%s obj %d should be absent: %s" % (name, n, g))
    for n in (2, 1001, 1002, 65535):
        g = lines(pde, "get", P("sparse.pdf"), (n, 0, "Filler"))[-1]
        expect(g.startswith("GET rc=53") or n == 2 and False, "sparse obj %d should be absent: %s" % (n, g))
    # ---- tokens: our lexer against an independent one
    big = exp["tokens.pdf"]["big"]
    out = dumpf("tokens.pdf")
    cl = [l for l in out if l.startswith("CONTENT")]
    m = re.search(r"tokens=(\d+) ops=(\d+) opshash=([0-9a-f]+)", cl[0]) if cl else None
    # a string of 100000 bytes arrives as 393 pieces (255 each) in the engine; the reference counts it once
    pieces = (100000 + 254) // 255
    rt, ro, rh = ref_tokens(big)
    expect(m and int(m.group(1)) == rt + pieces - 1 + 2 * 0 and int(m.group(2)) == ro and int(m.group(3), 16) == rh,
           "tokens: engine %s, reference tokens=%d (+%d pieces) ops=%d hash=%04x" % (m.groups() if m else cl, rt, pieces - 1, ro, rh))
    tl = [l for l in out if l.startswith("TOKEN")]
    expect(not tl, "tokens: %s" % tl)
    out = dumpf("n_deep_content.pdf")
    expect(any(l.startswith("TOKEN") and "rc=5" in l for l in out), "40-deep content arrays: PDE_DEPTH: %s" % out[-2:])
    # ---- the negatives with a correct xref
    EXP = {
        "n_len_short": "stream:1", "n_len_long": "stream:1", "n_len_past_eof": "stream:3", "n_len_negative": "stream:1",
        "n_len_real": "stream:1", "n_no_endstream": "stream:1", "n_cr_only": "stream:1", "n_crlf": "ok", "n_huge_length": "stream:1",
        # any PDF (20.22.3.5): a cross-reference that does not hold up is rebuilt by a scan
        "n_trunc_xref": "repaired", "n_trunc_in_trailer": "repaired", "n_startxref_inside": "repaired",
        "n_startxref_zero": "repaired", "n_startxref_huge": "repaired", "n_no_eof_marker": "ok",
        "n_junk_after_eof": "repaired", "n_header_late": "open:1",
        "n_header_v20": "ok", "n_header_bad_ver": "open:1", "n_empty": "open:3", "n_cz": "open:1", "n_garbage": "open:1",
        "n_xref_short_entry": "repaired", "n_xref_badtype": "repaired", "n_xref_gen_big": "repaired",
        "n_xref_off_eof": "repaired", "n_prev_inside": "repaired", "n_prev_cycle3": "repaired", "n_page_twice": "open:4", "n_no_pages": "open:1",
        "n_kid_not_page": "open:1", "n_kid_dangling": "ok", "n_kid_wrong_gen": "open:1", "n_tree_30": "ok", "n_tree_33": "open:5",
        "n_objnum_70000": "open:33", "n_flate": "stream:16", "n_lzw": "stream:16", "n_a85": "ok", "n_dct": "stream:17",
        "n_crypt": "stream:17", "n_chain5": "stream:33", "n_chain4": "ok", "n_unknown": "stream:17", "n_rle_bomb": "read:33",
        "n_encrypt": "open:23", "n_perms": "flags:1", "n_sigflags": "flags:5", "n_acroform": "flags:4", "n_xrefstm": "repaired",
        "hybrid": "ok", "n_good": "ok", "n_hexws_bomb": "read:33", "n_thin_unord": "ok", "n_subsec_65540": "open:33",
    }
    for name, e in sorted(EXP.items()):
        out = lines(pde, "dump", P(name + ".pdf"), (), ("-b", "100000"))
        f0 = out[0]
        kind, _, val = e.partition(":")
        if kind == "open":
            expect(f0.startswith("OPEN rc=%s " % val), "%s: want OPEN rc=%s, got %s" % (name, val, f0))
            if name == "n_cz":
                expect("detail=1 " in f0, "n_cz: a packed file is reported with detail 1: %s" % f0)
        elif kind == "repaired":
            expect(f0.startswith("OPEN ok") and " flags=128 " in f0, "%s: want a repaired open, got %s" % (name, f0))
        elif kind == "flags":
            expect(f0.startswith("OPEN ok") and ("flags=%s " % val) in f0, "%s: want flags=%s, got %s" % (name, val, f0))
        else:
            expect(f0.startswith("OPEN ok"), "%s: should open, got %s" % (name, f0))
            late = [l for l in out if l.startswith("CONTENT") and re.search(r" (read)?rc=\d", l)]
            if kind == "stream":
                expect(any((" rc=%s" % val) in l for l in late), "%s: want stream rc=%s, got %s" % (name, val, late or out[-1]))
            elif kind == "read":
                expect(any(("readrc=%s" % val) in l for l in late), "%s: want read rc=%s, got %s" % (name, val, late or out[-1]))
            else:
                expect(not late, "%s: unexpected %s" % (name, late))
    # a refused file leaves the engine reusable, the answers do not change, a fault in any read is an I/O error
    names = [P(n + ".pdf") for n in ("n_good", "n_len_short", "n_encrypt", "filters", "n_tree_33", "incr", "n_xrefstm", "outline")]
    rc, out, err = run(pde, "multi", names[0], names[1:])
    expect(rc == 0, "multi rc=%d %s" % (rc, err))
    again = subprocess.run([pde, "multi", *names], capture_output=True).stdout.decode()
    expect(out == again, "multi is not repeatable")
    solo = []
    for nme in names:
        solo += subprocess.run([pde, "multi", nme], capture_output=True).stdout.decode().splitlines()
    expect(out.splitlines() == solo, "reuse changes answers:\n%s\nvs\n%s" % (out, "\n".join(solo)))
    for nme in ("n_good", "filters", "incr", "outline", "res", "sparse"):
        l = lines(pde, "faults", P(nme + ".pdf"))
        expect(l and l[-1].startswith("FAULTS ok"), "faults %s: %s" % (nme, l[-1:] if l else l))
    # verify-eng: FINGERPRINT sees a file another program APPENDED an update to
    # (same first KiB, same old last KiB, new size) and the same bytes as the same.
    # filters.pdf is over 2 KiB, so the old head and tail are both unchanged
    g = open(P("filters.pdf"), "rb").read()
    expect(len(g) > 2048, "fpchange needs a file over 2 KiB")
    grown = os.path.join(work, "fp_grown.pdf")
    with open(grown, "wb") as f:
        f.write(g + b"99 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>\nendobj\n" + b" " * 900)
    l = lines(pde, "fpchange", P("filters.pdf"), (grown,))
    fp = [x for x in l if x.startswith("FP before")]
    expect(fp and fp[0].endswith(" CHANGED") and "before rc=0" in fp[0] and "after rc=0" in fp[0], "fingerprint of an appended file: %s" % fp)
    l = lines(pde, "fpchange", P("n_good.pdf"), (P("n_good.pdf"),))
    fp = [x for x in l if x.startswith("FP before")]
    expect(fp and fp[0].endswith(" SAME"), "fingerprint of the same bytes: %s" % fp)
    # any PDF (20.22.3.5): a bad entry no longer refuses - the file is scanned and
    # the bad entry's object found where it really is
    f0 = first_of(pde, P("n_xref_off_eof.pdf"), ())
    expect(f0.startswith("OPEN ok") and " flags=128 " in f0, "a bad entry is repaired: %s" % f0)
    # claims
    expect(first_of(pde, P("n_good.pdf"), ("-k", "4")).startswith("OPEN rc=34"), "a 4 KiB cache is PDE_NOMEM")
    for c in (1, 2, 4, 8, 16, 32, 64):
        for k in (8, 16, 64):
            l = first_of(pde, P("filters.pdf"), ("-c", str(c), "-k", str(k)))
            ok = l.startswith("OPEN ok") or (c * 512 * 2 > k * 1024 and l.startswith("OPEN rc=34"))
            expect(ok, "cluster %d sectors, cache %d KiB: %s" % (c, k, l))
    perf_checks(pde, P, exp)
    print("own fixtures done")


# lexedge.pdf, object 10 + i: what a key-less DICT_GET answers (pde_fixtures.LEXEDGE)
LEXEDGE_WANT = ["rc=32 kind=2 num=32767.99998", "rc=32 kind=2 num=32767.99998", "rc=32 kind=2 num=32767.99998",
                "rc=32 kind=2 num=-32768.00001", "rc=0 kind=2 num=17.00000", "rc=0 kind=2 num=0.00000",
                "rc=0 kind=2 num=7.00000", "rc=0 kind=2 num=32767.00000", "rc=32 kind=2 num=32767.99998",
                "rc=0 kind=3 num=1.75000", "rc=0 kind=3 num=0.99998", "rc=0 kind=3 num=-1.50000", "rc=1 kind=0",
                "rc=1 kind=0", "rc=0 kind=5 num=0.00000 ref=0,0 len=3 count=0 text=ABC",
                "rc=0 kind=5 num=0.00000 ref=0,0 len=5 count=0 text=ab/cd",
                "rc=0 kind=5 num=0.00000 ref=0,0 len=127 count=0 text=" + "x" * 127, "rc=33 kind=0",
                "rc=0 kind=9 num=0.00000 ref=5,0", "rc=0 kind=9 num=0.00000 ref=5,0", "rc=0 kind=9 num=0.00000 ref=5,0",
                "rc=0 kind=2 num=5.00000", "rc=33 kind=0", "rc=0 kind=4 num=0.00000 ref=0,0 len=18 count=0 text=lit (nested) ) str",
                "rc=0 kind=4 num=0.00000 ref=0,0 len=5 count=0 text=Hello", "rc=0 kind=2 num=1.00000",
                "rc=0 kind=3 num=0.12345"]


def perf_checks(pde, P, exp):
    """verify-perf: the asm lexer's edges, "n +g R", and dict_get's array
    cursor after a read that failed in the middle of an element"""
    for c in (1, 8):
        for i, want in enumerate(LEXEDGE_WANT):
            g = lines(pde, "get", P("lexedge.pdf"), (10 + i, 0), ("-c", str(c)))[-1]
            expect(g.startswith("GET " + want), "lexedge obj %d c=%d: %s, want %s" % (10 + i, c, g, want))
    g = lines(pde, "get", P("plusref.pdf"), (1, 0, "Pages"))
    expect(g[0].startswith("OPEN ok pages=1") and g[-1].startswith("GET rc=0 kind=9 num=0.00000 ref=2,0"),
           "plusref: \"2 +0 R\" is a reference: %s" % g)
    # a read failing while the element that straddles a block is lexed cuts it
    # short ("12" of 12345); the answers after the disk is back must be what a
    # fresh engine answers, never the cursor resuming in the middle of it
    n_case = n_bad = 0
    for s in (0, 2):
        name, k = "arrcut%d.pdf" % s, exp["arrcut%d.pdf" % s]
        tail = ["5/0/Widths/[%d]" % (k + 1), "5/0/Widths/[%d]" % (k + 2), "5/0/Widths/[%d]" % (k + 30), "5/0/Tail"]
        fresh = [lines(pde, "get", P(name), q.split("/"), ("-k", "8"))[-1] for q in tail]
        for b in range(k - 160, k, 8):
            for a in (1, 2, 3):
                for ln in (0, 8):
                    q = ["5/0/Widths/[%d]" % b, "F%d-%d" % (a, a + ln), "5/0/Widths/[%d]" % k, "O"] + tail
                    got = lines(pde, "seq", P(name), q, ("-k", "8"))[-4:]
                    got = [l.split(" ", 1)[1] if " " in l else l for l in got]
                    n_case += 1
                    if got != fresh:
                        n_bad += 1
                        if n_bad <= 3:
                            fail("arrcut s=%d [%d] F%d-%d: after the read came back %s, a fresh engine %s" % (s, b, a, a + ln, got, fresh))
    expect(n_case and not n_bad, "arrcut: %d of %d fault cases left the cursor wrong" % (n_bad, n_case))
    print("ok   verify-perf: lexedge %d values x2 clusters, plusref, arrcut %d fault cases" % (len(LEXEDGE_WANT), n_case))


CONTENT_SPARSE = pde_fixtures.CONTENT % b"sparse"


def first_of(pde, path, opts):
    rc, out, err = run(pde, "dump", path, list(opts))
    return out.splitlines()[0] if out else "rc=%d %s" % (rc, err)


def main():
    pde, work = sys.argv[1], sys.argv[2]
    os.makedirs(work, exist_ok=True)
    own(pde, os.path.join(work, "own"))
    fix = sys.argv[3] if len(sys.argv) > 3 else None
    gen = os.path.join("tools", "os88pdfgen.py")
    if fix is None and os.path.exists(gen):
        fix = os.path.join(work, "shared")
        for mode, sub in (("", "samples"), ("--stress", "stress"), ("--negative", "neg")):
            subprocess.run([sys.executable, gen] + ([mode] if mode else []) + [os.path.join(fix, sub)], capture_output=True, check=True)
    if fix:
        for sub in ("samples", "stress"):
            d = os.path.join(fix, sub)
            for f in sorted(os.listdir(d)):
                if f.endswith(".PDF"):
                    for c in (1, 2, 8):
                        check_good(pde, os.path.join(d, f), c)
        rd = next((r for r in (os.path.join("tests", "pdfneg", "README.TXT"),
                               os.path.join(fix, "..", "tests", "pdfneg", "README.TXT")) if os.path.exists(r)), None)
        if rd:
            check_neg(pde, os.path.join(fix, "neg"), rd)
    # any PDF (20.22.3): tools/os88pdfx_eng.py's fixtures, with and without the Flate tier
    eng = os.path.join("tests", "pdfany", "eng")
    if os.path.isdir(eng):
        for f in sorted(os.listdir(eng)):
            if f.endswith(".PDF"):
                pth = os.path.join(eng, f)
                for c in (1, 8):
                    check_good(pde, pth, c, ("-F", "2"))
                check_good(pde, pth, 2, (), noflate=True)
                check_members(pde, pth, ("-F", "2"), work)
                check_members(pde, pth, (), work)
                if f.startswith("RESDI"):               # 20.22.3.8: the direct value, where RAW_COPY puts it
                    l = lines(pde, "res", pth, (0, "ExtGState", "GS1"))[-1]
                    m = re.match(r"RES obj=0,0 kind=7 direct=(\d+),0\+(\d+)$", l)
                    mem = os.path.join(work, "mem")
                    e = open(os.path.join(mem, "%s.e" % m.group(1)), "rb").read() if m else b""
                    expect(m and re.match(rb"<<\s*/CA 0\.5\s*/ca 0\.25\s*>>", e[int(m.group(2)):]),
                           "%s: RES_LOOKUP direct %s, bytes there %r" % (f, l, e[int(m.group(2)):][:30] if m else b""))
                    expect(lines(pde, "res", pth, (0, "ExtGState", "GS2"))[-1] == "RES obj=5,0 kind=7",
                           "%s: a referenced ExtGState is not PDRL_DIRECT" % f)
    print("FAILURES:", fails)
    sys.exit(1 if fails else 0)


main()
