#!/usr/bin/env python3
"""The filmstrip's thumbnails and their cache, PIXEL.THC (SPEC.md 106.21).

    python3 tests/pxthumb.py [--machine os8088_xt_vga_720b]

A folder of seven pictures in B:/PICS - a JPEG of the gallery, a JPEG, two
GIFs, two BMPs and a PCX of tools/pixcorpus.py's - and SYSTEM/APPDATA, the
folder the cache lives in. PiXEL is opened on LAKE.JPG by its ASSOCIATION,
from a subfolder of B: (SPEC.md 106.6 / 54.10: the strip shows the
document's own folder), and then:

  COLD   the cards on show come: the open picture's from its master with no
         decode, every other one by exactly one hidden decode; each
         thumbnail in the store is the host's own - tools/pixelsim.py's
         master at the scale the rule picks, sampled and taken to the cube
         as 106.21 says - byte for byte; and PIXEL.THC is written: read back
         off the floppy ON THE HOST (tools/os88flush.py), its header names
         every one and each entry is the store's thumbnail and its key
  WARM   PiXEL closed and opened again on the same document: the strip
         fills from the cache with NO decode, and every thumbnail is the
         cold one
  SLIDES File > Slideshow: three slides go by, the button latched; a key
         stops it, and the picture shown is the last slide
  BROWSE twenty Prev and Next (Space, Backspace) and then still: PiXEL's
         claims are as they were, the largest free run of the heap too

The picture shown is put back after every hidden decode: the record, the
claims and the largest run are compared at each still point.
"""
import argparse
import functools
import os
import sys

print = functools.partial(print, flush=True)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path.insert(0, "tests")
sys.path.insert(0, "tools")
import os88marty as M           # noqa: E402
import os88ui                   # noqa: E402
import os88flush                # noqa: E402
import heapmap                  # noqa: E402
import os88build                # noqa: E402
import pixelsim as P            # noqa: E402
import pixcorpus as C           # noqa: E402
from pxsyms import pkg_syms, instance, u16      # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_xt_vga_720b")
a = ap.parse_args()
syms, image = pkg_syms()
o88 = open(os88build.at("build/pixel.o88"), "rb").read()
if o88[:syms["op_table"]] != image[:syms["op_table"]]:
    sys.exit("build/pixel.o88 is not this tree's pixel.asm - run make")

TW, TH, TSLOTS, TDATA = 72, 54, 6, 72 * 54
SLSZ, SL0 = TDATA + 32, 0x2000
GREY = [0, 49, 98, 153, 202, 251]
FAIL = []


def check(what, ok, detail=""):
    print("%s %s%s" % ("PASS" if ok else "FAIL", what,
                       "" if ok or not detail else "  -- " + detail))
    if not ok:
        FAIL.append(what)


# --- the host's thumbnail (SPEC.md 106.21) --------------------------------------
def tfit(w, h):
    if 3 * w >= 4 * h:
        tw = min(TW, w)
        th = max(1, min(TH, (h * tw + w // 2) // w))
    else:
        th = min(TH, h)
        tw = max(1, min(TW, (w * th + h // 2) // h))
    return tw, th


def cube(c):
    q6 = lambda v: (5 * v + 127) // 255     # noqa: E731
    q7 = lambda v: (6 * v + 127) // 255     # noqa: E731
    r, g, b = c
    if r == g == b:
        return GREY[q6(r)]
    return (q6(r) * 7 + q7(g)) * 6 + q6(b)


def thumb(master, mw, mh, pal):
    pal = list(pal) + [(0, 0, 0)] * (256 - len(pal))
    tw, th = tfit(mw, mh)
    whole, rem = divmod(mw, tw)
    frac = (rem << 16) // tw
    out = bytearray()
    for y in range(th):
        si, f = (y * mh // th) * mw, 0
        for _ in range(tw):
            out.append(cube(pal[master[si]]))
            f += frac
            si += whole + (f >> 16)
            f &= 0xFFFF
    return tw, th, bytes(out)


def tscale(sw, sh):
    """px_tcap's scale: the coarsest at or below 1/8 covering the box."""
    nw, nh = min(TW, sw), min(TH, sh)
    for s in (3, 2, 1):
        if sw >> s >= nw and sh >> s >= nh:
            return s
    return 0


def host_thumb(data, name, scale):
    p = P.decode(data, name.rsplit(".", 1)[1], scale)
    sm, mw, mh, _, pal = P.emit(p, scale)
    return thumb(sm, mw, mh, pal)


# --- the disk ------------------------------------------------------------------
tmp = "build/pxthumb"
os.makedirs(tmp, exist_ok=True)
cor = {n: d for n, d, v in C.corpus()}
PICS = {}
for n in ("BIG24.BMP", "GBIG.GIF", "C8.PCX", "BIG8.BMP", "G8.GIF"):
    PICS[n] = cor[n]
PICS["LAKE.JPG"] = open("apps/pixel/samples/LAKE.JPG", "rb").read()
PICS["JBIG.JPG"] = open("tests/pixel/JBIG.JPG", "rb").read()
files = []
for n, d in sorted(PICS.items()):
    open(os.path.join(tmp, n), "wb").write(d)
    files.append("PICS:" + os.path.join(tmp, n))
DISK = M.scratch_disk("build/pxthumb.img", "build/pixel.o88",
                      "build/PIXEL.GFX", *files, "--folder", "SYSTEM/APPDATA",
                      size=720)
NAMES = sorted(PICS)

print("== PiXEL: the folder's thumbnails and PIXEL.THC, %s (SPEC.md 106.21) =="
      % a.machine)
with os88ui.boot("build/os8088-360.img", apps=DISK, machine=a.machine) as ui:
    m = ui.m
    S = ui._S
    fl = os88flush.Flush(marty=m)
    hsym = {n: S(n) for n in ("mem_base", "mem_top", "spl_live", "mem_tab")}

    def launch():
        ui.path("B:/PICS/LAKE.JPG")
        M.until(m, lambda _: instance(m, S, image), "PiXEL's instance",
                poll=0.3, limit=120)
        return instance(m, S, image) * 16

    base = launch()
    B = lambda n: m.read(base + syms[n], 1)[0]                 # noqa: E731
    W = lambda n: u16(m.read(base + syms[n], 2))              # noqa: E731

    def still():
        """Nothing in flight: no open, no hidden decode, the timer slow."""
        return (W("px_ndone") and B("px_busy") == 0 and B("px_job") == 0
                and B("px_hmode") == 0 and B("px_tq") == 0)

    def wait_still(what, limit=900):
        M.until(m, lambda _: still(), what, poll=0.5, limit=limit)
        M.ui_done(m, what)

    def window():
        fcs, fcn, nn = W("px_fcs"), W("px_fcn"), W("px_nnames")
        return list(range(fcs, min(fcs + fcn, nn)))

    def names():
        nn = W("px_nnames")
        raw = m.read(base + syms["px_names"], nn * 20)
        return [raw[i * 20:i * 20 + 13].split(b"\0")[0].decode()
                for i in range(nn)]

    def store():
        """{name: (tw, th, bytes, flags)} of the store's valid slots."""
        seg = W("px_tseg")
        out = {}
        if not seg:
            return out
        for i in range(TSLOTS):
            s = m.read(seg * 16 + SL0 + i * SLSZ, SLSZ)
            k = s[TDATA:]
            if k[23] & 1:
                nm = k[:12].split(b"\0")[0].decode()
                out[nm] = (k[21], k[22], s[:k[21] * k[22]], k[23])
        return out

    def parts_here():
        """The segments of the parts fetched now: a DECODER part is held
        while the picture shown is of its kind and dropped by an open of
        another (SPEC.md 106.18) - pxdecode's exclusion, and for its reason"""
        out = []
        for k in range(1, 6):
            r = m.read(base + syms["op_table"] + 10 + 8 * k, 8)
            if r[1] & 32:
                out.append(u16(r, 6))
        return out

    def claims():
        hm = heapmap.Map(m, hsym)
        seg = base >> 4
        here = parts_here()
        mine = sorted(c.para for c in hm.claims
                      if c.own == seg and c.seg != seg
                      and not any(c.seg <= p < c.seg + c.para for p in here))
        # the largest free run with the decoder parts taken as free, as the
        # claims are taken without them: a part is fetched into whatever
        # hole the picture it came after left, so where it lands is that
        # picture's size and not a leak (SPEC.md 106.24: a 1,216-paragraph
        # JPEG part landed 224 paragraphs higher after the browse, behind
        # the other neighbour's master, with every claim as it was)
        hm.claims = [c for c in hm.claims
                     if not any(c.seg <= p < c.seg + c.para for p in here)]
        return mine, max(p for _, p in hm.runs())

    def record():
        b = m.read(base + syms["px_cur"], 48)
        return (b[:13].split(b"\0")[0].decode(), u16(b, 32), u16(b, 34), b[36],
                u16(b, 38), b[43])

    # ---------------------------------------------------------------- COLD --
    wait_still("the first open and its thumbnails")
    nm = names()
    win = window()
    shown = record()
    check("the strip is the document's folder: %d pictures" % len(nm),
          nm == NAMES, "%r" % nm)
    check("LAKE.JPG open, n of N says %d of %d" % (W("px_fidx"), len(nm)),
          shown[0] == "LAKE.JPG" and shown[5] == 1)
    st = store()
    onshow = [nm[i] for i in win]
    check("every card on show has its thumbnail (%d)" % len(onshow),
          all(n in st for n in onshow), "store %r, on show %r"
          % (sorted(st), onshow))
    check("the open picture's with no decode, every other by ONE: "
          "%d decodes for %d cards" % (W("px_thdec"), len(onshow)),
          W("px_thdec") == len(onshow) - 1 and W("px_thmade") == len(onshow),
          "made %d" % W("px_thmade"))
    for n in onshow:
        if n not in st:
            continue
        ext = n.rsplit(".", 1)[1]
        if n == shown[0]:               # the open one: from ITS master
            sc = shown[3]
        else:                           # the others: px_tcap's scale
            hdr = P.decode(PICS[n], ext, 3 if ext == "JPG" else None)
            sc = tscale(hdr.w, hdr.h)
        tw, th, ref = host_thumb(PICS[n], n, sc)
        g = st[n]
        check("%s: %dx%d from 1/%d, the host's own byte for byte"
              % (n, tw, th, 1 << sc), (g[0], g[1], g[2]) == (tw, th, ref),
              "guest %dx%d, %d of %d bytes differ" % (
                  g[0], g[1], sum(1 for x, y in zip(g[2], ref) if x != y),
                  len(ref)))
    check("the cache written once: %d" % W("px_thwrote"),
          W("px_thwrote") == 1)
    vol = fl.volume(1)
    thc = vol.read("SYSTEM/APPDATA/PIXEL.THC")
    count = thc[5] if thc and len(thc) >= 4096 else -1
    check("PIXEL.THC on the floppy: %d bytes, header PXTC v1, %d entries"
          % (len(thc or b""), count),
          thc and thc[:4] == b"PXTC" and thc[4] == 1
          and count == len(onshow))
    seen = set()
    for c in range(max(count, 0)):
        k = thc[32 + 32 * c:64 + 32 * c]
        ent = thc[4096 * (c + 1):4096 * (c + 2)]
        n = k[:12].split(b"\0")[0].decode()
        seen.add(n)
        ok = ent[TDATA:TDATA + 21] == k[:21] and n in st and \
            ent[:k[21] * k[22]] == st[n][2]
        check("PIXEL.THC entry %d: %s, its own key the header's and its "
              "bytes the store's" % (c, n), ok)
    check("...and every card on show is in it", seen == set(onshow),
          "%r" % sorted(seen))
    cold = {n: st[n][:3] for n in onshow if n in st}
    c0 = claims()

    # ---------------------------------------------------------------- WARM --
    w = ui.window("PiXEL")
    ui.close(w)
    M.ui_done(m, "PiXEL closed")
    base = launch()
    wait_still("the second visit's strip")
    st = store()
    check("WARM: no decode (%d), %d thumbnails read from the cache"
          % (W("px_thdec"), W("px_thread")),
          W("px_thdec") == 0 and W("px_thread") == len(onshow)
          and W("px_thmade") == 0)
    check("WARM: every card on show is the cold one, byte for byte",
          all(n in st and st[n][:3] == cold[n] for n in onshow),
          "%r" % sorted(st))
    c1 = claims()
    check("WARM: PiXEL's claims as they were before the close",
          c1[0] == c0[0], "%r -> %r" % (c0[0], c1[0]))

    # -------------------------------------------------------------- SLIDES --
    n0 = W("px_ndone")
    ui.menu_pick("File", "Slideshow")
    M.until(m, lambda _: B("px_slon") == 1, "the slideshow", poll=0.3,
            limit=30)
    flags = u16(m.read(base + syms["px_bflags"] + 2 * 9, 2))
    check("SLIDES: the button latched", flags & 32 != 0, "flags %04X" % flags)
    M.until(m, lambda _: W("px_ndone") >= n0 + 3, "three slides", poll=0.5,
            limit=300)
    # the picture SHOWN: px_prev's while the next one decodes hidden
    rec = "px_prev" if B("px_hmode") else "px_cur"
    was = m.read(base + syms[rec], 13).split(b"\0")[0].decode()
    m.key("Escape")
    M.until(m, lambda _: B("px_slon") == 0, "the slideshow to stop",
            poll=0.3, limit=60)
    wait_still("still, after the slideshow")
    r2 = record()
    check("SLIDES: three slides, then Esc stopped it on %s, still shown"
          % r2[0], r2[0] != "LAKE.JPG" and r2[5] == 1 and r2[0] == was,
          "%s then %r" % (was, r2))
    c2 = claims()
    check("SLIDES: no claim of a decode left behind (%d, as before)"
          % len(c2[0]), len(c2[0]) == len(c1[0]),
          "%r -> %r" % (c1[0], c2[0]))

    # -------------------------------------------------------------- BROWSE --
    while record()[0] != "LAKE.JPG":
        n0 = W("px_ndone")
        m.key("Space")
        M.until(m, lambda _: W("px_ndone") != n0 and B("px_busy") == 0,
                "Next", poll=0.3, limit=300)
    wait_still("still, on LAKE.JPG again")
    base0 = claims()
    for k in ["Space"] * 10 + ["Backspace"] * 10:
        n0 = W("px_ndone")
        m.key(k)
        M.until(m, lambda _: W("px_ndone") != n0 and B("px_busy") == 0,
                k, poll=0.3, limit=300)
    wait_still("still, after twenty")
    after = claims()
    check("BROWSE: back on LAKE.JPG after ten Next and ten Prev",
          record()[0] == "LAKE.JPG", "%r" % (record(),))
    check("BROWSE: PiXEL's claims as they were (%d)" % len(after[0]),
          after[0] == base0[0], "%r -> %r" % (base0[0], after[0]))
    check("BROWSE: the largest free run as it was (%d paragraphs)"
          % after[1], after[1] >= base0[1], "%d -> %d"
          % (base0[1], after[1]))

print("%s pxthumb" % ("FAIL" if FAIL else "PASS"))
sys.exit(1 if FAIL else 0)
