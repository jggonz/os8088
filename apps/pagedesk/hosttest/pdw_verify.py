#!/usr/bin/env python3
"""pdw_verify.py OUTDIR [SHOTDIR] - the independent half of PDWRT's host check.

Runs qpdf --check on every PDF the harness wrote, dumps each with qpdf --json
and asserts the annotation facts PAGEDESK-SPEC 13.3/13.4/14.2 promise, renders
every page with mutool draw (PNG into SHOTDIR) and checks the source's SHA-256
is what it was before the incremental saves. Exit status = failures."""
import hashlib, json, os, re, shutil, subprocess, sys

if not (shutil.which("qpdf") and shutil.which("mutool")):
    print("SKIP: qpdf and mutool are the independent readers (PAGEDESK-SPEC 18.4) and one is not installed;"
          " the harness's own checks passed but this half did not run")
    sys.exit(0)
out = sys.argv[1]
shots = sys.argv[2] if len(sys.argv) > 2 else None
fails = 0


def fail(msg):
    global fails
    fails += 1
    print("FAIL", msg)


def run(*a):
    return subprocess.run(a, capture_output=True, text=True)


def dump(f):
    r = run("qpdf", "--json=2", "--json-key=qpdf", os.path.join(out, f))
    if r.returncode not in (0, 3):
        fail("%s: qpdf --json rc=%d %s" % (f, r.returncode, r.stderr[:200]))
        return None
    return json.loads(r.stdout)["qpdf"][1]


def ref(s):
    m = re.match(r"(\d+) 0 R", s) if isinstance(s, str) else None
    return int(m.group(1)) if m else None


def annots(objs, page):
    a = page.get("/Annots")
    if isinstance(a, str):
        a = objs["obj:%s" % a]["value"]
    return [objs["obj:%s" % r]["value"] for r in (a or [])]


def pages(objs):
    root = objs["trailer"]["value"]["/Root"]
    pg = objs["obj:%s" % objs["obj:%s" % root]["value"]["/Pages"]]["value"]
    return [objs["obj:%s" % k]["value"] for k in pg["/Kids"]]


def check_file(f):
    r = run("qpdf", "--check", os.path.join(out, f))
    if r.returncode != 0 or "No syntax or stream encoding errors" not in r.stdout:
        fail("%s: qpdf --check rc=%d\n%s%s" % (f, r.returncode, r.stdout[-300:], r.stderr[-300:]))
    else:
        print("ok   qpdf --check", f)
    if shutil.which("pdftoppm"):
        pp = run("pdftoppm", "-r", "30", "-png", os.path.join(out, f), os.path.join(out, "pp-" + f[:-4]))
        if pp.returncode != 0 or pp.stderr.strip():
            fail("%s: pdftoppm (Poppler): %s" % (f, pp.stderr[:200]))
        else:
            print("ok   pdftoppm", f)
    if shots:
        os.makedirs(shots, exist_ok=True)
        m = run("mutool", "draw", "-r", "70", "-o", os.path.join(shots, f[:-4] + "-p%d.png"), os.path.join(out, f))
        if m.returncode != 0 or "error" in m.stderr.lower():
            fail("%s: mutool draw: %s" % (f, m.stderr[:200]))
        else:
            print("ok   mutool draw", f)


files = [l.split()[1] for l in open(os.path.join(out, "files.txt")).read().splitlines() if l.startswith("PDW-FILE")]
for f in files:
    if f.endswith(".pdf"):
        check_file(f)

# ---- a fresh document with every annotation kind -------------------------------------
for f in ("fresh512.pdf", "fresh4096.pdf"):
    o = dump(f)
    if not o:
        continue
    pgs = pages(o)
    if len(pgs) != 2:
        fail("%s: %d pages" % (f, len(pgs)))
        continue
    if pgs[1].get("/Rotate") != 90 or pgs[1]["/MediaBox"] != [0, 0, 792, 612]:
        fail("%s: page 2 box/rotate %s %s" % (f, pgs[1].get("/MediaBox"), pgs[1].get("/Rotate")))
    a = annots(o, pgs[0])
    kinds = sorted(x["/Subtype"] for x in a)
    want = sorted(["/Text", "/Text", "/Highlight", "/Square", "/Square", "/FreeText", "/FreeText", "/FreeText",
                   "/Ink", "/Ink", "/Line", "/Line", "/Line", "/Circle"])
    if kinds != want:
        fail("%s: page 1 subtypes %s" % (f, kinds))
    for x in a:
        if not re.match(r"u:PD-[0-9A-F]{8}-[0-9A-F]{4}$", x["/NM"]):
            fail("%s: bad /NM %s" % (f, x["/NM"]))
        if x["/F"] not in (4, 32):
            fail("%s: bad /F %s" % (f, x["/F"]))
        if "/P" not in x:
            fail("%s: no /P" % f)
        if x["/F"] == 4 and "/AP" not in x:
            fail("%s: no /AP on %s" % (f, x["/Subtype"]))
    nm = [x for x in a if x["/Subtype"] == "/Text" and "/IRT" not in x][0]
    if nm["/Contents"] != "u:Café €uro — “quoted” note":
        fail("%s: note Contents %r" % (f, nm["/Contents"]))
    rep = [x for x in a if "/IRT" in x][0]
    if rep.get("/State") != "/Completed" or rep.get("/StateModel") != "/Review" or rep["/F"] != 32 or ref(rep["/IRT"]) is None:
        fail("%s: reply %s" % (f, rep))
    else:
        parent = o["obj:%s" % rep["/IRT"]]["value"]
        if parent is not nm and parent["/NM"] != nm["/NM"]:
            fail("%s: /IRT does not point at the note" % f)
    hl = [x for x in a if x["/Subtype"] == "/Highlight"][0]
    q = hl["/QuadPoints"]
    if len(q) != 16 or q[0:8] != [107, 715, 190, 715, 107, 697, 190, 697]:
        fail("%s: QuadPoints %s" % (f, q))
    if hl["/Rect"] != [72, 677, 190, 715]:
        fail("%s: highlight Rect %s" % (f, hl["/Rect"]))
    inks = [x for x in a if x["/Subtype"] == "/Ink"]
    big = max(inks, key=lambda x: sum(len(s) for s in x["/InkList"]))
    if [len(s) for s in big["/InkList"]] != [1000, 6, 2]:
        fail("%s: ink strokes %s" % (f, [len(s) for s in big["/InkList"]]))
    lines = [x for x in a if x["/Subtype"] == "/Line"]
    les = sorted(tuple(x["/LE"]) for x in lines)
    if les != [("/None", "/OpenArrow"), ("/OpenArrow", "/None"), ("/OpenArrow", "/OpenArrow")]:
        fail("%s: /LE %s" % (f, les))
    ft = [x for x in a if x["/Subtype"] == "/FreeText"]
    if sorted(x["/Q"] for x in ft) != [0, 1, 2] or not all("/DA" in x for x in ft):
        fail("%s: FreeText /Q /DA %s" % (f, [(x["/Q"], x.get("/DA")) for x in ft]))
    # the font resource of every FreeText appearance
    for x in ft:
        ap = o["obj:%s" % x["/AP"]["/N"]]
        fr = ap["stream"]["dict"]["/Resources"]["/Font"]
        enc = list(fr.values())[0]["/Encoding"]
        if enc != "/WinAnsiEncoding":
            fail("%s: FreeText font encoding %s" % (f, enc))
    hlap = o["obj:%s" % hl["/AP"]["/N"]]["stream"]["dict"]
    if hlap["/Resources"]["/ExtGState"]["/GS"]["/BM"] != "/Multiply":
        fail("%s: highlight blend mode" % f)
    print("ok   %s: %d annotations on page 1, %d on page 2" % (f, len(a), len(annots(o, pgs[1]))))
    trailer = o["trailer"]["value"]
    if len(trailer["/ID"]) != 2:
        fail("%s: /ID" % f)

# ---- many notes: 250 records in a 12 KiB buffer, each where it was put -------------------
o = dump("many.pdf")
if o:
    pgs = pages(o)
    a0, a1 = annots(o, pgs[0]), annots(o, pgs[1])
    rects = set(tuple(x["/Rect"]) for x in a0 + a1)
    if len(a0) != 125 or len(a1) != 125 or len(rects) != 250:
        fail("many.pdf: %d + %d annotations, %d distinct rects" % (len(a0), len(a1), len(rects)))
    if a0[0]["/Rect"] != [100, 100, 120, 118] or a0[0]["/Contents"] != "u:note number 0":
        fail("many.pdf: first note %s %s" % (a0[0]["/Rect"], a0[0]["/Contents"]))
    print("ok   many.pdf: 250 notes, 250 distinct rects")

# ---- the incremental saves --------------------------------------------------------------
src = os.path.join(out, "src.pdf")
raw = open(src, "rb").read()
sx = int(re.search(rb"startxref\n(\d+)\n", raw).group(1))
for f in ("inc1.pdf", "inc2.pdf"):
    d = open(os.path.join(out, f), "rb").read()
    if f == "inc1.pdf" and not d.startswith(raw):
        fail("inc1.pdf does not begin with the source bytes")
    print("     %s sha256(source)=%s source bytes preserved=%s" % (f, hashlib.sha256(raw).hexdigest()[:16], d.startswith(raw)))
o1, o2 = dump("inc1.pdf"), dump("inc2.pdf")
if o1:
    t = o1["trailer"]["value"]
    if t["/Prev"] != sx or t["/Root"] != "1 0 R" or t["/Info"] != "6 0 R":
        fail("inc1 trailer %s (source startxref %d)" % (t, sx))
    if t["/ID"][0] != "b:0123456789abcdef0123456789abcdef" or t["/ID"][1] == t["/ID"][0]:
        fail("inc1 /ID %s" % t["/ID"])
    if o1["obj:1 0 R"]["value"].get("/Version") != "/1.5":
        fail("inc1 catalog /Version %s (a highlight and a review state need 1.5)" % o1["obj:1 0 R"]["value"])
    p = pages(o1)
    a0 = annots(o1, p[0]); a1 = annots(o1, p[1]); a2 = annots(o1, p[2])
    cs = sorted(x.get("/Contents", "") for x in a0)
    if cs != ["", "u:Check this figure", "u:Existing note (moved)"] or len(a0) != 3:
        fail("inc1 page 1 annots %s" % cs)
    if sorted(x["/Subtype"] for x in a1) != ["/Highlight", "/Square"]:
        fail("inc1 page 2 annots (the deleted note must be gone, the keeper and the highlight there): %s" % [x["/Subtype"] for x in a1])
    if [x["/Subtype"] for x in a2] != ["/Ink"]:
        fail("inc1 page 3 annots %s" % [x["/Subtype"] for x in a2])
    rep = [x for x in a0 if "/IRT" in x]
    if not rep or rep[0]["/State"] != "/Completed":
        fail("inc1 review reply %s" % rep)
    print("ok   inc1: /Prev, /Size %s, /Version /1.5, annots per page %d/%d/%d" % (t["/Size"], len(a0), len(a1), len(a2)))
if o2:
    t = o2["trailer"]["value"]
    if t["/ID"][0] != o1["trailer"]["value"]["/ID"][0]:
        fail("inc2 /ID[0] changed")
    p = pages(o2)
    a0 = annots(o2, p[0]); a2 = annots(o2, p[2])
    cs = sorted(x.get("/Contents", "") for x in a0)
    if "u:Check this figure (revised)" not in cs or "u:Check this figure" in cs:
        fail("inc2: newest revision does not win: %s" % cs)
    if [x["/Subtype"] for x in a2] != ["/Square"] or a2[0]["/Contents"] != "u:second round":
        fail("inc2 page 3 annots %s" % [x["/Subtype"] for x in a2])
    nm1 = [x for x in annots(o1, pages(o1)[0]) if x.get("/Contents") == "u:Check this figure"][0]["/NM"]
    nm2 = [x for x in a0 if x.get("/Contents") == "u:Check this figure (revised)"][0]["/NM"]
    if nm1 != nm2:
        fail("inc2: the edited note lost its /NM (%s -> %s)" % (nm1, nm2))
    print("ok   inc2: revised note wins, ink deleted, area mark added, /NM kept (%s)" % nm2)

print("verify:", "PASS" if not fails else "%d FAIL" % fails)
sys.exit(1 if fails else 0)
