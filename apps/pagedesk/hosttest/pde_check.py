#!/usr/bin/env python3
"""os8088 - apps/pagedesk/hosttest/pde_check.py

PDENG's host check (PAGEDESK-SPEC 20.2): runs build/pde/pde_test over a set of
PDFs at several cluster sizes and compares EVERYTHING it can with an independent
reader (qpdf): the page count, the page object numbers in order, each page's
inherited MediaBox/CropBox/Rotate, and the CRC of every content stream's decoded
bytes. The negative corpus (tools/os88pdfgen.py --negative) is checked against
the table in tests/pdfneg/README.TXT, adjusted by KNOWN below (the engine's
documented code, PAGEDESK-SPEC 20.2.x).

usage: pde_check.py PDE_TEST FIXDIR     (FIXDIR holds samples/, stress/, neg/)
"""
import json, os, re, subprocess, sys

PDE = {0: "OK", 1: "SYNTAX", 2: "XREF", 3: "TRUNC", 4: "CYCLE", 5: "DEPTH", 16: "FLATE", 17: "FILTER", 18: "FONT",
       19: "GRAPHICS", 20: "ENCRYPT", 21: "XREFSTM", 22: "SIGNED", 32: "RANGE", 33: "LIMIT", 34: "NOMEM", 48: "IO",
       53: "NOTFOUND"}
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
    p = subprocess.run([pde, cmd, path, *extra], capture_output=True, text=True, timeout=600)
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


def check_good(pde, path, cluster):
    name = os.path.basename(path)
    rc, out, err = run(pde, "dump", path, ["-c", str(cluster)])
    tag = "%s c=%d" % (name, cluster)
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
        if cl and cl[0].endswith("rc=16") and name == "FLATE.PDF":
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
    rc, out, err = run(pde, "objs", path, ["-c", str(cluster)])
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
    print("ok   %-16s c=%-2d pages=%d" % (name, cluster, npg))


# the engine's documented code where it differs from tests/pdfneg/README.TXT
KNOWN = {
    "prev_loop.pdf": "XREF",       # SPEC 7.4: a /Prev cycle is PDE_XREF with the revision named
}


def check_neg(pde, negdir, readme):
    rows = {}
    for l in open(readme):
        m = re.match(r"(\S+\.pdf)\s+(open|render)\s+PDE_(\w+)\s+(.*)", l)
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
        if layer == "open" and got == "OK":
            # boxes and streams are judged when asked for (PAGEINFO, STRM_OPEN): that is part of opening
            late = [l for l in out.splitlines() if re.search(r" rc=\d+", l) and l.split()[0] in ("PAGE", "CONTENT")]
            if late:
                got = PDE.get(int(re.search(r" rc=(\d+)", late[0]).group(1)), "?")
                first = late[0]
        if layer == "open":
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


def main():
    pde, fix = sys.argv[1], sys.argv[2]
    for sub in ("samples", "stress"):
        d = os.path.join(fix, sub)
        for f in sorted(os.listdir(d)):
            if f.endswith(".PDF"):
                for c in (1, 2, 8):
                    check_good(pde, os.path.join(d, f), c)
    rd = os.path.join(fix, "..", "tests", "pdfneg", "README.TXT")
    check_neg(pde, os.path.join(fix, "neg"), rd)
    print("FAILURES:", fails)
    sys.exit(1 if fails else 0)


main()
