#!/usr/bin/env python3
"""PDREND hostile-input checks (PAGEDESK-SPEC 20.3.6): content streams built to
break an interpreter - numbers at and past every limit, runaway nesting,
singular and enormous matrices, a million-point path, text at size 0 and 1e9,
a Form that includes itself, images whose data is shorter than their header
says, binary junk - rendered by a binary built with the address and undefined-
behaviour sanitizers.  Every render must finish (no crash, no hang, no sanitizer
report, no write past a claim - the harness checks canaries behind each one) and
must say something about what it could not do (an Incomplete bit) where it
skipped content.

    pdr_fuzz.py <sanitized pdr_test> <scratch dir> <resource dir> [-n RANDOM_CASES] [--seed S]
"""
import os, random, subprocess, sys, shutil, argparse
ap = argparse.ArgumentParser(); ap.add_argument("exe"); ap.add_argument("scratch"); ap.add_argument("resdir")
ap.add_argument("-n", type=int, default=120); ap.add_argument("--seed", type=int, default=11)
a = ap.parse_args()
os.makedirs(a.scratch, exist_ok=True)
fd = os.path.join(a.scratch, "fd")
os.makedirs(fd, exist_ok=True)
data = open(os.path.join(a.resdir, "PDRES.BIN"), "rb").read()
open(os.path.join(fd, "PDRES.BIN"), "wb").write(data)
open(os.path.join(fd, "PAGEDESK.O88"), "wb").write(b"\x55" * 1536 + data)
shutil.copy(os.path.join(a.resdir, "PDFONTX.RES"), fd)

def pdf(content, extra_objs=(), res="", extra_page="", font1=None):
    objs = []
    def add(b): objs.append(b); return len(objs)
    add(None); add(None)
    f1 = add(font1 or "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
    c = add(b"<< /Length %d >>\nstream\n" % len(content) + content + b"\nendstream")
    ex = []
    for o in extra_objs: ex.append(add(o(len(objs) + 1) if callable(o) else o))
    p = add("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] /Contents %d 0 R /Resources << /Font << /F1 %d 0 R /F2 99 0 R >> %s >> %s >>" % (c, f1, res, extra_page))
    objs[0] = "<< /Type /Catalog /Pages 2 0 R >>"; objs[1] = "<< /Type /Pages /Count 1 /Kids [%d 0 R] >>" % p
    out = bytearray(b"%PDF-1.4\n"); offs = []
    for i, o in enumerate(objs):
        offs.append(len(out)); out += b"%d 0 obj\n" % (i + 1) + (o if isinstance(o, bytes) else o.encode("latin-1")) + b"\nendobj\n"
    x = len(out); out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
    for o in offs: out += b"%010d 00000 n \n" % o
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, x)
    return bytes(out)

def stream(d, body): return b"<< %s /Length %d >>\nstream\n" % (d.encode(), len(body)) + body + b"\nendstream"

CASES = {}
def case(name, expect_inc=None):
    def deco(f): CASES[name] = (f, expect_inc); return f
    return deco

@case("huge numbers")
def _(): return pdf(b"1e30 0 0 1e30 0 0 cm 99999999999 -99999999999 m 1e9 1e9 l 0 0 l f 32767 32767 32767 32767 re f -32768 -32768 65536 65536 re S 0.00000001 w 0 0 m 100 100 l S")
@case("nesting without Q", 0x80)
def _(): return pdf(b"q " * 200 + b"0 0 100 100 re f " + b"Q " * 300 + b"10 10 50 50 re f")
@case("Q underflow")
def _(): return pdf(b"Q Q Q Q 0 0 50 50 re f")
@case("singular and flipped matrices")
def _(): return pdf(b"0 0 0 0 0 0 cm 0 0 100 100 re f q 1 0 0 0 0 0 cm 0 0 100 100 re S Q q -1 0 0 -1 400 300 cm 0 0 50 50 re f Q q 1e-5 0 0 1e-5 0 0 cm 0 0 100000 100000 re f Q")
@case("million-point path")
def _(): return pdf(b"0 0 m " + b"".join(b"%d %d l " % (i % 400, (i * 7) % 300) for i in range(20000)) + b"f")
@case("wild curves")
def _(): return pdf(b"".join(b"0 0 m %d %d %d %d %d %d c S " % tuple(random.Random(i).randint(-30000, 30000) for _ in range(6)) for i in range(60)))
@case("dashes")
def _(): return pdf(b"[0 0] 0 d 0 0 m 400 300 l S [1e-9] 0 d 0 0 m 400 300 l S [0.001 0.001] 0 d 0 0 m 400 300 l S [3 2 1 5 7 9 11] -5 d 10 10 m 390 290 l S 1e6 w [100000] 0 d 0 0 m 100 100 l S")
@case("stroke styles")
def _(): return pdf(b"50 w 1 J 1 j 10 10 m 100 200 l 20 20 l 300 10 l S 2 J 0 j 0.001 M 30 w 10 250 m 200 280 l 10 250 l S 20 w 100 100 m 100 100 l S 20 w 1 J 100 100 m 100 100 l S")
@case("clips")
def _(): return pdf(b"".join(b"q %d %d m %d %d l %d %d l h W n 0 0 400 300 re f " % tuple(random.Random(i).randint(-500, 800) for _ in range(6)) for i in range(40)) + b"Q " * 40 + b"q 1 1 m 399 1 l 200 299 l h W* n 0 g 0 0 400 300 re f Q")
@case("text sizes")
def _(): return pdf(b"BT /F1 0 Tf 10 10 Td (zero) Tj /F1 -12 Tf (negative) Tj /F1 1e9 Tf (huge) Tj /F1 1e-9 Tf (tiny) Tj /F1 12 Tf 0 Tz (Tz0) Tj -100 Tz (neg) Tj 1e9 Tz (huge) Tj 100 Tz 1e9 Tc (Tc) Tj 0 Tc 1e9 Tw (a b c) Tj 0 Tw 1e30 Ts (rise) Tj 0 Ts ET")
@case("TJ abuse")
def _(): return pdf(b"BT /F1 12 Tf 10 100 Td [(a) 1e30 (b) -1e30 (c) 99999999 (d)] TJ [" + b"(x) -50 " * 400 + b"] TJ [] TJ [1 2 3] TJ [(unterminated TJ ET")
@case("strings")
def _(): return pdf(b"BT /F1 12 Tf 10 100 Td (" + bytes(range(1, 256)).replace(b"(", b"\\(").replace(b")", b"\\)").replace(b"\\", b"\\\\") + b") Tj <" + b"41" * 3000 + b"> Tj (" + b"A" * 4000 + b") Tj ET")
@case("missing and bad fonts")
def _(): return pdf(b"BT /F2 12 Tf 10 100 Td (missing font F2) Tj /Nope 12 Tf (no such) Tj Tf (no operands) Tj ET")
@case("text state without BT")
def _(): return pdf(b"/F1 12 Tf 10 10 Td (no BT) Tj ET ET BT BT (nested) Tj T* T* TD ' \" Tm Td 1 2 3 4 5 6 Tm (x) Tj")
@case("operand stack abuse")
def _(): return pdf(b"1 " * 1000 + b"re f " + b"/N " * 300 + b"Do " + b"[ " * 500 + b"] " * 500 + b"<< " * 300 + b">> " * 300 + b"0 0 10 10 re f")
@case("garbage")
def _(): return pdf(bytes(random.Random(5).randrange(256) for _ in range(5000)))
@case("unknown operators")
def _(): return pdf(b"foo bar BAZ 1 2 3 qux 0 0 10 10 re f ??? sh /P1 scn /Cs1 cs 1 0 0 sc /GS gs BI /W 4 /H 4 /BPC 8 /CS /G ID abcdefghijklmnop EI 0 0 5 5 re f")
@case("form that includes itself")
def _():
    return pdf(b"/Fm Do 0 0 20 20 re f", extra_objs=(lambda n: stream("/Type /XObject /Subtype /Form /BBox [0 0 400 300] /Resources << /XObject << /Fm %d 0 R >> >>" % n, b"/Fm Do 1 0 0 rg 10 10 30 30 re f"),),
               res="/XObject << /Fm 5 0 R >>")
@case("mutually recursive forms")
def _():
    return pdf(b"/A Do", extra_objs=(stream("/Type /XObject /Subtype /Form /BBox [0 0 400 300] /Resources << /XObject << /B 6 0 R >> >>", b"/B Do"),
                                     stream("/Type /XObject /Subtype /Form /BBox [0 0 400 300] /Resources << /XObject << /A 5 0 R >> >>", b"/A Do 0 0 9 9 re f")),
               res="/XObject << /A 5 0 R >>")
@case("images with lying headers")
def _():
    imgs = (stream("/Type /XObject /Subtype /Image /Width 1000 /Height 1000 /BitsPerComponent 8 /ColorSpace /DeviceGray", b"\x80" * 100),
            stream("/Type /XObject /Subtype /Image /Width 0 /Height 10 /BitsPerComponent 8 /ColorSpace /DeviceGray", b"\x80" * 100),
            stream("/Type /XObject /Subtype /Image /Width 65535 /Height 65535 /BitsPerComponent 1 /ColorSpace /DeviceGray", b"\x80" * 100),
            stream("/Type /XObject /Subtype /Image /Width 4000 /Height 4 /BitsPerComponent 8 /ColorSpace /DeviceRGB", b"\x80" * 100),
            stream("/Type /XObject /Subtype /Image /Width 8 /Height 8 /BitsPerComponent 3 /ColorSpace /DeviceGray", b"\x80" * 100),
            stream("/Type /XObject /Subtype /Image /Width 8 /Height 8 /BitsPerComponent 8 /ColorSpace /Indexed", b"\x80" * 100),
            stream("/Type /XObject /Subtype /Image /Width 8 /Height 8 /BitsPerComponent 8 /ColorSpace /DeviceGray /Filter /DCTDecode", b"\x80" * 100))
    return pdf(b"".join(b"q 100 0 0 100 %d 100 cm /I%d Do Q " % (10 + 40 * i, i) for i in range(7)), extra_objs=imgs,
               res="/XObject << " + " ".join("/I%d %d 0 R" % (i, 5 + i) for i in range(7)) + " >>")
@case("images at every angle and extreme scale")
def _():
    g = stream("/Type /XObject /Subtype /Image /Width 16 /Height 16 /BitsPerComponent 8 /ColorSpace /DeviceGray", bytes(range(256)))
    return pdf(b"".join(b"q %s cm /I Do Q " % m.encode() for m in ("1e6 0 0 1e6 -5e5 -5e5", "0.001 0 0 0.001 10 10", "0 100 -100 0 200 100", "100 50 -50 100 100 100", "-100 0 0 100 300 100", "100 0 0 -100 100 250", "100 0 0 100 -50 -50", "100 0 0 100 350 250")), extra_objs=(g,), res="/XObject << /I 5 0 R >>")
@case("huge pages")
def _(): return pdf(b"0 0 m 5000 5000 l S BT /F1 12 Tf 4000 4000 Td (far) Tj ET 1000000 1000000 5 5 re f")

# ---- verify-rend's additions: the paths its fixes are on -------------------------------------
@case("inline images: counted, mask, filtered, CMYK, unterminated", 0x04)
def _():
    return pdf(b"BI /W 4 /H 4 /BPC 8 /CS /G ID " + b"EI Q q " * 3 + b"rest" + b" EI 0 0 9 9 re f "        # data containing 'EI': counted, not scanned
               b"BI /IM true /W 9 /H 3 /D [1 0] ID " + bytes(6) + b" EI 10 10 5 5 re f "
               b"BI /W 2 /H 2 /BPC 8 /CS /CMYK ID " + bytes(16) + b" EI "
               b"BI /W 3 /H 3 /BPC 8 /CS /RGB /F /AHx ID 00ff00ff00ff00ff00ff00ff00ff00ff00ff00> EI "
               b"BI /W 3 /Foo 7 /H 2 /BPC 4 /CS /G ID " + bytes(4) + b" EI "
               b"BI /W 100 /H 100000 /BPC 8 /CS /G ID " + bytes(3000) + b" EI 20 20 5 5 re f BI /W 1 /H")
@case("graphics state overflow keeps q/Q balanced", 0x80)
def _(): return pdf(b"q 1 0 0 rg " + b"q " * 40 + b"0 0 1 rg " + b"Q " * 40 + b"0 0 50 50 re f Q 0 0 0 rg 60 60 20 20 re f")
@case("token flood without an operator")
def _(): return pdf(b"1 " * 200000 + b"0 0 10 10 re f " + b"[ " * 30 + b"(a) " * 20000 + b"] " * 30 + b"<< " + b"/K 1 " * 20000 + b">> 5 5 5 5 re f")
@case("TJ of numbers only")
def _(): return pdf(b"BT /F1 12 Tf 10 100 Td [-5000] TJ (after) Tj [1e9] TJ [-1e9 (x)] TJ ET")
@case("image rows that wrap 16 bits")
def _():
    imgs = (stream("/Type /XObject /Subtype /Image /Width 2731 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB", b"\x80" * 40000),
            stream("/Type /XObject /Subtype /Image /Width 21846 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB", b"\x80" * 100),
            stream("/Type /XObject /Subtype /Image /Width 8 /Height 40000 /BitsPerComponent 8 /ColorSpace /DeviceGray", b"\x80" * 1000))
    return pdf(b"".join(b"q 300 0 0 80 10 %d cm /I%d Do Q " % (10 + 90 * i, i) for i in range(3)), extra_objs=imgs,
               res="/XObject << " + " ".join("/I%d %d 0 R" % (i, 5 + i) for i in range(3)) + " >>")
@case("large text at every scale and quarter turn")
def _():
    return pdf(b"".join(b"BT /F1 %d Tf %s %d %d Tm (Big WMgy) Tj ET " % (sz, m, x, y) for sz, m, x, y in (
        (300, b"1 0 0 1", 0, 50), (90, b"0 1 -1 0", 300, 20), (90, b"0 -1 1 0", 100, 280), (90, b"-1 0 0 -1", 390, 280),
        (500, b"0 1 -1 0", 390, 0), (64, b"1 0 0 1", -50, 150), (40, b"1 0 0.3 1", 50, 200))))
@case("control codes with widths")
def _():
    return pdf(b"BT /F1 12 Tf 10 100 Td (\001\002\013\014\037A) Tj ET",
               font1="<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /FirstChar 0 /LastChar 65 /Widths [%s] >>" % " ".join(["500"] * 66))
@case("extreme miters and long strokes")
def _(): return pdf(b"20 M 200 w 0 J 0 j 0 0 m 400 1 l 0 2 l S 1 0 0 1 0 0 cm 30 w -100000 -100000 m 100000 100000 l -100000 100000 l S 1000 w 200 150 m 200.01 150 l 200 150.01 l s")

RAND_OPS = ["q", "Q", "cm", "w", "J", "j", "M", "d", "m", "l", "c", "v", "y", "h", "re", "S", "s", "f", "F", "f*", "B", "B*", "b", "b*", "n", "W", "W*",
            "g", "G", "rg", "RG", "k", "K", "sc", "scn", "SC", "cs", "BT", "ET", "Tc", "Tw", "Tz", "TL", "Tf", "Tr", "Ts", "Td", "TD", "Tm", "T*", "Tj", "TJ", "'", '"', "Do", "gs", "foo"]
def rand_content(R):
    out = []
    for _ in range(R.randint(20, 400)):
        for _ in range(R.randint(0, 7)):
            k = R.random()
            if k < .5: out.append("%g" % R.choice([0, 1, -1, 0.5, 12, 100, 399, 1e5, -1e5, 32767, 1e9, R.uniform(-500, 800)]))
            elif k < .6: out.append("/F1" if R.random() < .7 else "/Nope")
            elif k < .75: out.append("(%s)" % "".join(R.choice("abc XYZ019()") .replace("(", "").replace(")", "") for _ in range(R.randint(0, 30))))
            elif k < .8: out.append("[" + " ".join("%g" % R.uniform(-3000, 3000) for _ in range(R.randint(0, 6))) + "]")
            else: out.append(R.choice(["true", "false", "null"]))
        out.append(R.choice(RAND_OPS))
    return " ".join(out).encode()

fails = 0
def run(name, content_pdf, expect_inc=None, cl="1"):
    global fails
    p = os.path.join(a.scratch, "f.pdf"); open(p, "wb").write(content_pdf)
    for bpp, asp, W, H, zoom in ((4, "1/1", 640, 480, 100), (1, "5/12", 640, 200, 50), (1, "29/45", 720, 348, 300)):
        out = os.path.join(a.scratch, "o.ppm")
        env = dict(os.environ); env["PDR_CLUSTER"] = cl; env["PDR_GLYPHKB"] = "24"
        env["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=0"
        try:
            r = subprocess.run([a.exe, "render", p, "0", str(zoom), "0", str(bpp), asp, str(W), str(H), "8", "8", out, fd, "29"], capture_output=True, text=True, errors="replace", timeout=60, env=env)
        except subprocess.TimeoutExpired:
            print("  FAIL %s (%dbpp z%d): HANG (>60 s)" % (name, bpp, zoom)); fails += 1; return
        bad = r.returncode not in (0, 3) or "ERROR: AddressSanitizer" in r.stderr or "runtime error" in r.stderr or "CANARY" in r.stderr
        if bad:
            print("  FAIL %s (%dbpp z%d): rc=%d %s" % (name, bpp, zoom, r.returncode, r.stderr.strip().split("\n")[0][:200])); fails += 1; return
        if expect_inc is not None and bpp == 4:
            inc = 0
            for l in r.stdout.split("\n"):
                if l.startswith("status"): inc = int(l.split("incomplete=")[1].split()[0], 16)
            if not (inc & expect_inc): print("  FAIL %s: Incomplete bits 0x%x lack 0x%x" % (name, inc, expect_inc)); fails += 1; return
    print("  ok   %s" % name)

for name, (f, exp) in CASES.items(): run(name, f(), exp)
R = random.Random(a.seed)
nrand = 0
for i in range(a.n):
    run("random #%d" % i, pdf(rand_content(R))) if False else None
    p = os.path.join(a.scratch, "r.pdf"); content = rand_content(R)
    before = fails; open(p, "wb").write(pdf(content))
    out = os.path.join(a.scratch, "o.ppm"); env = dict(os.environ); env["PDR_GLYPHKB"] = "24"; env["ASAN_OPTIONS"] = "detect_leaks=0"
    bpp, asp, W, H, z = R.choice([(4, "1/1", 640, 480, 100), (1, "5/12", 640, 200, 50), (1, "29/45", 720, 348, 150)])
    try:
        r = subprocess.run([a.exe, "render", p, "0", str(z), "0", str(bpp), asp, str(W), str(H), "8", "8", out, fd, str(R.choice([7, 9, 11, 29]))], capture_output=True, text=True, errors="replace", timeout=60, env=env)
        if r.returncode not in (0, 3) or "AddressSanitizer" in r.stderr or "runtime error" in r.stderr or "CANARY" in r.stderr:
            fails += 1; keep = os.path.join(a.scratch, "crash_%d.pdf" % i); shutil.copy(p, keep)
            print("  FAIL random #%d rc=%d %s (kept %s)" % (i, r.returncode, r.stderr.strip().split("\n")[0][:160], keep))
    except subprocess.TimeoutExpired:
        fails += 1; keep = os.path.join(a.scratch, "hang_%d.pdf" % i); shutil.copy(p, keep); print("  FAIL random #%d HANG (kept %s)" % (i, keep))
print("pdr_fuzz: %d hostile cases + %d random streams, %d FAILED" % (len(CASES), a.n, fails))
sys.exit(1 if fails else 0)
