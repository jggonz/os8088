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
    r = run("pdftoppm", "-r", "30", "-f", str(page), "-l", str(page), "-gray", f, "-")
    return r.stdout


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
print("pgcheck: %d failure(s)" % fails)
sys.exit(1 if fails else 0)
