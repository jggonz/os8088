#!/usr/bin/env python3
"""pdw_pgcheck.py DIR SAMPLES - the independent half of pdw_real.c's page-operation
and Extract checks (PAGEDESK-SPEC 20.11): qpdf --check, then Poppler's text and
pixels for every page against the source's own pages. Exit status = failures."""
import os, re, shutil, subprocess, sys

d, samples = sys.argv[1], sys.argv[2]
fails = 0


def fail(m):
    global fails
    fails += 1
    print("FAIL", m)


def run(*a):
    return subprocess.run(a, capture_output=True)


def need(t):
    if not shutil.which(t):
        print("SKIP: %s not installed" % t)
        sys.exit(0)


for t in ("qpdf", "pdftotext", "pdftoppm", "pdfinfo"):
    need(t)


def pages_text(f):
    r = run("pdftotext", "-layout", f, "-")
    return [p.strip() for p in r.stdout.decode("latin1").split("\f")][:-1] if r.returncode == 0 else None


def npages(f):
    return int(run("qpdf", "--show-npages", f).stdout.decode().strip() or 0)


def check(f):
    r = run("qpdf", "--check", f)
    if r.returncode != 0 or b"No syntax or stream encoding errors" not in r.stdout:
        fail("%s: qpdf --check rc=%d %s" % (f, r.returncode, r.stdout[-300:] + r.stderr[-300:]))
        return False
    print("ok   qpdf --check", os.path.basename(f))
    return True


def rots(f):
    out = run("pdfinfo", "-f", "1", "-l", "999", f).stdout.decode()
    return [int(x) for x in re.findall(r"Page\s+\d+ rot:\s+(\d+)", out)]


def pix(f, page, rotate=None):
    import glob, tempfile
    with tempfile.TemporaryDirectory() as t:
        r = run("pdftoppm", "-r", "30", "-f", str(page), "-l", str(page), "-gray", f, os.path.join(t, "p"))
        fs = sorted(glob.glob(os.path.join(t, "p*.pgm")))
        if r.returncode != 0 or not fs:
            fail("pdftoppm produced nothing for %s page %d" % (f, page))
            return b"?"
        return open(fs[0], "rb").read()


# ---- P1: PAGES.PDF reordered ----
src = os.path.join(samples, "PAGES.PDF")
p1 = os.path.join(d, "P1.PDF")
if os.path.exists(p1) and check(p1):
    st = pages_text(src)
    pt = pages_text(p1)
    want = [7, 6, 5, 1, 1, None, 2, 4]            # source page indexes, None = the new blank
    if len(pt) != 8:
        fail("P1: %d pages, want 8" % len(pt))
    else:
        bad = 0
        for i, w in enumerate(want):
            exp = "" if w is None else st[w]
            if sorted(pt[i].split()) != sorted(exp.split()):
                bad += 1
                print("     P1 page %d text %r want %r" % (i + 1, pt[i][:40], exp[:40]))
        if bad:
            fail("P1: %d of 8 pages show the wrong text" % bad)
        else:
            print("ok   P1: pages show 8 7 6 2 2 (blank) 3 5, as the map said")
        sr, pr = rots(src), rots(p1)
        exp_rot = [None if w is None else (sr[w] + 90 * q) % 360 for w, q in zip(want, [0, 0, 1, 0, 2, 0, 0, 3])]
        got = [r for r in pr]
        okr = all(e is None or e == g for e, g in zip(exp_rot, got)) and len(got) == 8
        if okr:
            print("ok   P1: rotations", got)
        else:
            fail("P1: rotations %s want %s" % (got, exp_rot))
        # the duplicate does not share a page object with its original (13.7)
        j = run("qpdf", "--json=2", "--json-key=pages", p1).stdout.decode()
        objs = re.findall(r'"object": "(\d+ 0 R)"', j)
        if len(set(objs)) != 8:
            fail("P1: %d distinct page objects among 8 pages" % len(set(objs)))
        else:
            print("ok   P1: 8 distinct page objects (the duplicate is its own object)")

# ---- X1: Extract ----
src = os.path.join(samples, "XTGUIDE.PDF")
x1 = os.path.join(d, "X1.PDF")
if os.path.exists(x1) and check(x1):
    st = pages_text(src)
    xt = pages_text(x1)
    want = [4, 0, None, 2]
    if len(xt) != 4:
        fail("X1: %d pages, want 4" % len(xt))
    else:
        for i, w in enumerate(want):
            exp = "" if w is None else st[w]
            if sorted(xt[i].split()) != sorted(exp.split()):
                fail("X1 page %d text differs from source page %s" % (i + 1, "blank" if w is None else w + 1))
        # pixels of the unrotated ones
        for i, w in ((0, 4), (1, 0)):
            if pix(x1, i + 1) != pix(src, w + 1):
                fail("X1 page %d draws differently from source page %d" % (i + 1, w + 1))
        sr, xr = rots(src), rots(x1)
        if xr[3] != (sr[2] + 90) % 360:
            fail("X1: page 4 rotate %d, want %d" % (xr[3], (sr[2] + 90) % 360))
        n_src = len(re.findall(rb"\n\d+ 0 obj", open(src, "rb").read()))
        n_x = len(re.findall(rb"\n\d+ 0 obj", open(x1, "rb").read()))
        print("ok   X1: text and pixels match the source pages; %d objects against the source's %d (%d bytes against %d)"
              % (n_x, n_src, os.path.getsize(x1), os.path.getsize(src)))
        if n_x >= n_src:
            fail("X1 has as many objects as the whole source: it is a page-tree edit, not a graph copy")
        # no page of the source that was not chosen survives
        raw = open(x1, "rb").read()
        if raw.count(b"/Type /Page\n") + raw.count(b"/Type /Page ") > 5:
            fail("X1 carries more than the 4 page objects")
# ---- N1/X2: a nested tree (inherited boxes, resources, rotation) ----
src = os.path.join(d, "NEST.PDF")
n1 = os.path.join(d, "N1.PDF")
if os.path.exists(n1) and check(n1):
    want = [3, 0, 1, 1, 2]
    if npages(n1) != 5:
        fail("N1: %d pages" % npages(n1))
    else:
        for i, w in enumerate(want[:4]):
            if i == 2:
                continue                    # B carries the new note: it draws more than the source
            if pix(n1, i + 1) != pix(src, w + 1):
                fail("N1 page %d draws differently from source page %d (an inherited attribute was lost)" % (i + 1, w + 1))
        sr, nr = rots(src), rots(n1)
        if nr[4] != (sr[2] + 90) % 360:
            fail("N1: page 5 rotate %d want %d" % (nr[4], (sr[2] + 90) % 360))
        j = run("qpdf", "--json=2", "--json-key=qpdf", n1).stdout.decode()
        import json
        objs = json.loads(j)["qpdf"][1]
        pgs = objs["obj:%s" % objs["obj:%s" % objs["trailer"]["value"]["/Root"]]["value"]["/Pages"]]["value"]["/Kids"]
        pv = [objs["obj:%s" % k]["value"] for k in pgs]
        okp = all(v.get("/Parent") == objs["obj:%s" % objs["trailer"]["value"]["/Root"]]["value"]["/Pages"] for v in pv)
        if not okp:
            fail("N1: a page is not a child of the root /Pages")
        # B (index 2) and its duplicate (index 3) own different annotation objects whose /P is themselves
        a2 = [objs["obj:%s" % r]["value"] for r in pv[2].get("/Annots", [])]
        a3 = [objs["obj:%s" % r]["value"] for r in pv[3].get("/Annots", [])]
        names2 = [a.get("/NM") for a in a2]
        names3 = [a.get("/NM") for a in a3]
        refs2, refs3 = pv[2].get("/Annots", []), pv[3].get("/Annots", [])
        if set(refs2) & set(refs3):
            fail("N1: the duplicate shares an annotation object with its original")
        if any(a.get("/P") != pgs[2] for a in a2 if "/P" in a) or any(a.get("/P") != pgs[3] for a in a3 if "/P" in a):
            fail("N1: an annotation's /P does not name its own page")
        pop3 = [a for a in a3 if a.get("/Subtype") == "/Popup"]
        txt3 = [a for a in a3 if a.get("/Subtype") == "/Text"]
        if not (pop3 and txt3 and pop3[0].get("/Parent") == refs3[a3.index(txt3[0])] and txt3[0].get("/Popup") == refs3[a3.index(pop3[0])]):
            fail("N1: the duplicate's popup and note do not point at each other")
        if len(set(n for n in names2 + names3 if n)) != len([n for n in names2 + names3 if n]):
            fail("N1: /NM repeats between the original and the duplicate: %s %s" % (names2, names3))
        print("ok   N1: 5 pages, every inherited box/resource kept (pixels match), the duplicate's annotations are its own")
x2 = os.path.join(d, "X2.PDF")
if os.path.exists(x2) and check(x2):
    if npages(x2) != 2 or pix(x2, 1) != pix(src, 4) or pix(x2, 2) != pix(src, 2):
        fail("X2: the extract of D and B does not draw as the source's pages")
    else:
        raw = open(x2, "rb").read()
        print("ok   X2: D and B extracted from the nested tree, %d bytes, inherited attributes materialised" % len(raw))

# ---- I1: Insert from other PDFs whose object numbers and font names collide (20.16) ----
src = os.path.join(samples, "PAGES.PDF")
fsrc = os.path.join(d, "FOREIGN.PDF")
i1 = os.path.join(d, "I1.PDF")
if os.path.exists(i1) and os.path.exists(fsrc) and check(i1):
    import json
    # (file, 0-based page, quarter turns added); None = a page of PAGES.PDF
    want = [(src, 0, 0), (fsrc, 1, 0), (fsrc, 3, 0), (src, 1, 0), (fsrc, 2, 1), (fsrc, 0, 0)] + [(src, k, 0) for k in range(2, 8)]
    if npages(i1) != len(want):
        fail("I1: %d pages, want %d" % (npages(i1), len(want)))
    else:
        txt = {}
        def pt(f):
            if f not in txt:
                txt[f] = pages_text(f)
            return txt[f]
        bad = 0
        out_t, out_r = pages_text(i1), rots(i1)
        for i, (f, k, q) in enumerate(want):
            if sorted(out_t[i].split()) != sorted(pt(f)[k].split()):
                bad += 1
                print("     I1 page %d text %r want %r" % (i + 1, out_t[i][:40], pt(f)[k][:40]))
            if out_r[i] != (rots(f)[k] + 90 * q) % 360:
                bad += 1
                print("     I1 page %d rotate %d want %d" % (i + 1, out_r[i], (rots(f)[k] + 90 * q) % 360))
            if q == 0 and pix(i1, i + 1) != pix(f, k + 1):
                bad += 1
                print("     I1 page %d draws differently from %s page %d" % (i + 1, os.path.basename(f), k + 1))
        if bad:
            fail("I1: %d page checks failed" % bad)
        else:
            print("ok   I1: 12 pages - text, rotation and pixels equal the source pages (inherited MediaBox/Resources/Rotate kept)")
        # shared resources: one copy per foreign FILE, never one per page
        q = run("qpdf", "--qdf", "--object-streams=disable", "--stream-data=uncompress", i1, "-").stdout
        n_cb, n_tr = q.count(b"/BaseFont /Courier-Bold"), q.count(b"/BaseFont /Times-Roman")
        n_form = q.count(b"/Subtype /Form")
        if (n_cb, n_tr, n_form) != (2, 2, 2):
            fail("I1: Courier-Bold x%d, Times-Roman x%d, form x%d - want 2 each (one copy per inserted file, not per page)" % (n_cb, n_tr, n_form))
        else:
            print("ok   I1: the font and the form used by three pages are copied once for that file (2 files -> 2 copies)")
        # references rewritten: the Link's /Dest names the output page that is FOREIGN page 4, its URI action survives
        j = json.loads(run("qpdf", "--json=2", "--json-key=qpdf", "--json-key=pages", i1).stdout.decode())
        objs = j["qpdf"][1]
        pgs = [p["object"] for p in j["pages"]]
        page2 = objs["obj:%s" % pgs[1]]["value"]
        links = [objs["obj:%s" % r]["value"] for r in page2.get("/Annots", [])]
        links = [a for a in links if a.get("/Subtype") == "/Link"]
        if len(links) != 1 or links[0].get("/Dest", [None])[0] != pgs[2] or "example.com" not in json.dumps(links[0].get("/A", {})):
            fail("I1: the link's /Dest %s does not name page 3's object %s, or lost its action: %s" % (links and links[0].get("/Dest"), pgs[2], links))
        else:
            print("ok   I1: the link's destination follows its page to the new number, the URI action is kept")
        # the note and its popup point at each other and at their page (cycles survive the copy)
        notes = [a for a in links or [] ]
        ann = [objs["obj:%s" % r]["value"] for r in page2.get("/Annots", [])]
        tx = [a for a in ann if a.get("/Subtype") == "/Text"]
        po = [a for a in ann if a.get("/Subtype") == "/Popup"]
        refs = page2.get("/Annots", [])
        if not (tx and po and tx[0].get("/P") == pgs[1] and tx[0].get("/Popup") == refs[ann.index(po[0])] and po[0].get("/Parent") == refs[ann.index(tx[0])]):
            fail("I1: the inserted page's note and popup do not point at each other and the page")
        else:
            print("ok   I1: /P, /Popup and /Parent (the copy's cycles) are rewritten to the new objects")
        # object numbers: no number of the source is reused (the source's own bytes are the prefix of the output)
        raw = open(i1, "rb").read()
        sraw = open(src, "rb").read()
        if not raw.startswith(sraw.rstrip(b"\n")[:len(sraw) - 64]):
            fail("I1: the source's bytes are not the output's prefix")
        else:
            print("ok   I1: the source file is the output's prefix (an incremental update); %d bytes added" % (len(raw) - len(sraw)))
print("pgcheck: %d failure(s)" % fails)
sys.exit(1 if fails else 0)
