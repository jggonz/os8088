#!/usr/bin/env python3
"""PIXEL'S DECODERS AGAINST THE REFERENCE, BYTE FOR BYTE (SPEC.md 106.13).

    python3 tests/pxdecode.py [--machine os8088_xt_vga] [-k NAME]

Every fixture tools/pixcorpus.py makes - each depth, type, orientation and
packing SPEC.md 106.10 reads, and the hostile half - goes onto one scratch
floppy beside PIXEL.O88. PiXEL is opened on the first by its association and
then made to open each of the rest through File > Revert with the name
poked into the shown picture's record: Revert is a real user path, and the
poke is the only thing a test adds.

  a GOOD fixture   the master PiXEL decoded - every byte of it - and its
                   palette equal tools/pixelsim.py's emit() of the same file
                   at the scale PiXEL chose; the record names the file
  a HOSTILE one    refused with SPEC.md 106.10's number for it (pixcorpus's
                   verdict, which --check holds pixelsim to), the picture
                   that was showing still showing, and PiXEL holding exactly
                   the claims it held before the refusal
  the SCALE        two fixtures again with [px_mcap] set, so memory "allows"
                   only 1/2: the box filter and, for an 8-bit source, the
                   palette's colours through the cube - against pixelsim at
                   the same scale

A decoder that writes one byte wrong, reads one past a row, frees a claim it
did not make or forgets one it did fails here, and none of those show on a
screenshot.
"""
import os, sys, argparse, functools
print = functools.partial(print, flush=True)
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tests"); sys.path.insert(0, "tools")
import os88marty as M
import os88ui, os88geom, heapmap, os88build
import pixelsim as P
import pixcorpus as C
from pxsyms import pkg_syms, u16

FAIL = []


def check(name, ok, detail=""):
    print("   %-64s %s%s" % (name, "ok" if ok else "FAIL",
                             "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_xt_vga")
ap.add_argument("-k", default=None, help="only fixtures whose name has this")
a = ap.parse_args()

syms, image = pkg_syms()
o88 = open(os88build.at("build/pixel.o88"), "rb").read()
if o88[:syms["op_table"]] != image[:syms["op_table"]]:
    sys.exit("build/pixel.o88 is not this tree's pixel.asm - run "
             "`make build/pixel.o88`")
corpus = C.corpus()
if a.k:
    corpus = [c for c in corpus if a.k.upper() in c[0]]
tmp = "build/pxcorpus"
os.makedirs(tmp, exist_ok=True)
for name, data, _ in corpus:
    open(os.path.join(tmp, name), "wb").write(data)
first = "C1.PCX"                    # PiXEL's own association opens it
open(os.path.join(tmp, first), "wb").write(
    [d for n, d, v in C.corpus() if n == first][0])
# FOLDERS OF FIFTY: the kernel lists at most 64 entries a directory, and the
# corpus is more. PA holds C1.PCX and the first fifty, PB the next, and so
# on; the run moves PiXEL from one to the next by poking the record's
# folder, which is exactly what File > Revert reads
DISK = "build/pxdecode.img"
names = sorted(set(n for n, _, _ in corpus) - {first})
folder = {n: "P" + chr(ord("A") + i // 50) for i, n in enumerate(names)}
folder[first] = "PA"
FOLDERS = sorted(set(folder.values()))
files = ["PA:" + os.path.join(tmp, first)] + \
    ["%s:%s" % (folder[n], os.path.join(tmp, n)) for n in names]
M.scratch_disk(DISK, "build/pixel.o88", *files)


def dir_cluster(img, name):
    """A root folder's first cluster, read off the FAT12 image itself."""
    d = open(img, "rb").read()
    bps, res, nfat, nroot = (u16(d, 11), u16(d, 14), d[16], u16(d, 17))
    spf = u16(d, 22)
    root = (res + nfat * spf) * bps
    for i in range(nroot):
        e = d[root + 32 * i:root + 32 * i + 32]
        if e[:11] == name.ljust(11).encode() and e[11] & 0x10:
            return u16(e, 26)
    raise SystemExit("no folder %s on %s" % (name, img))

print("== PiXEL: %d fixtures against tools/pixelsim.py (SPEC.md 106.13) =="
      % len(corpus))
with os88ui.boot("build/os8088-360.img", apps=DISK, machine=a.machine) as ui:
    m = ui.m
    S = ui._S

    def inst():
        for i in range(12):
            r = m.read(S("inst_tab") + i * os88geom.I_RECSZ, os88geom.I_RECSZ)
            c = u16(r, os88geom.I_SPTR)
            if c and m.read(c * 16, 32) == image[:32]:
                return c
        return None
    ui.path("B:/PA/" + first)
    M.until(m, lambda _: inst(), "PiXEL's instance", poll=0.3, limit=90)
    seg = inst()
    base = seg * 16
    B = lambda n: m.read(base + syms[n], 1)[0]
    W = lambda n: u16(m.read(base + syms[n], 2))

    def rec():
        b = m.read(base + syms["px_cur"], 48)
        return dict(name=b[:13].split(b"\0")[0].decode(), mw=u16(b, 32),
                    mh=u16(b, 34), scl=b[36], pmode=b[37], mseg=u16(b, 38),
                    have=b[43], fmt=b[24])

    OP_T_ROWS, OP_FETCHED = 10, 32      # apps/os88parts.inc
    PXF_PNG, PXF_GIF = 2, 3
    DECPART = {1: PXF_GIF, 2: PXF_PNG}  # pixel.asm's PXPART_* -> PXF_*

    def parts_here():
        """{part: its segment} for the decoder parts fetched now."""
        out = {}
        for k in DECPART:
            r = m.read(base + syms["op_table"] + OP_T_ROWS + 8 * k, 8)
            if r[1] & OP_FETCHED:
                out[k] = u16(r, 6)
        return out

    def claims():
        """PiXEL's claims but its region - and but a DECODER PART's, which
        an open of another kind may drop and fetch (SPEC.md 106.18): what
        PiXEL holds of those is parts_ok()'s question."""
        hm = heapmap.Map(m, {n: S(n) for n in ("mem_base", "mem_top",
                                              "spl_live", "mem_tab")})
        here = parts_here().values()
        return sorted((c.seg, c.para) for c in hm.claims
                      if c.own == seg and c.seg != seg
                      and not any(c.seg <= p < c.seg + c.para for p in here))

    def parts_ok():
        """At most one decoder part, and only the shown picture's kind."""
        here = parts_here()
        r = rec()
        return len(here) <= 1 and all(
            r["have"] and DECPART[k] == r["fmt"] for k in here), here

    def idle():
        return B("px_busy") == 0 and B("px_job") == 0

    M.until(m, lambda _: W("px_ndone") and idle() and rec()["have"],
            "the first picture", poll=0.3, limit=300)
    M.ui_done(m, "the first picture's paint")
    print("   PiXEL at %04X, %s open" % (seg, rec()["name"]))

    def revert(name, cap=0):
        """Open `name` through File > Revert; answer the record after."""
        m.write(base + syms["px_cur"], name.encode().ljust(13, b"\0"))
        m.write(base + syms["px_mcap"], bytes((cap & 255, cap >> 8)))
        m.write(base + syms["px_lastref"], b"\0")
        n0 = W("px_ndone")
        m.ctrl("KeyR")
        M.until(m, lambda _: W("px_ndone") != n0 and idle(),
                "the open of " + name, poll=0.3, limit=600)
        M.ui_done(m, name)
        return rec()

    def compare(name, data, scale):
        p = P.decode(data, name.rsplit(".", 1)[1])
        sm, mw, mh, mode, pal = P.emit(p, scale)
        r = rec()
        got = m.read(r["mseg"] * 16, r["mw"] * r["mh"])
        gpal = m.read(base + syms["px_pal"], 768)
        spal = b"".join(bytes(c) for c in pal) + b"\0" * (768 - 3 * len(pal))
        ok = (r["mw"], r["mh"], r["pmode"]) == (mw, mh, mode)
        check("%s: %dx%d at 1/%d, mode %d" % (name, mw, mh, 1 << scale, mode),
              ok, "guest %dx%d mode %d" % (r["mw"], r["mh"], r["pmode"]))
        if ok:
            bad = [i for i in range(len(sm)) if sm[i] != got[i]]
            check("%s: the master, byte for byte" % name, not bad,
                  "%d differ, first at %s: pixelsim %d, guest %d" %
                  (len(bad), bad[:1], sm[bad[0]], got[bad[0]]) if bad else "")
            check("%s: the palette" % name, gpal == spal, "")

    order = [first] + [n for n in names if folder[n] == "PA"] + \
        [n for n in names if folder[n] == "PB"]
    bydata = {n: (d, v) for n, d, v in corpus}
    bydata.setdefault(first, ([d for n, d, v in C.corpus() if n == first][0], 0))
    here = "PA"
    for name in order:
        data, want = bydata[name]
        if name == first:
            compare(name, data, 0)
            continue
        if folder[name] != here:    # into PB: the record's folder, poked
            here = folder[name]
            cl = dir_cluster(DISK, here)
            m.write(base + syms["px_cur"] + 14, bytes((cl & 255, cl >> 8)))
        before = claims()
        was = rec()
        shown = (was["mseg"], was["mw"], was["mh"], was["pmode"], was["have"])
        r = revert(name)
        if not want:
            check("%s: opened" % name, r["name"] == name and r["have"] == 1,
                  "record %r, refusal %d" % (r, B("px_lastref")))
            if r["name"] == name and r["have"]:
                compare(name, data, r["scl"])
        else:
            check("%s: refused, %d (%s)" % (name, want, P.PXD_WORDS[want]),
                  B("px_lastref") == want,
                  "got %d (%s)" % (B("px_lastref"),
                                   P.PXD_WORDS[B("px_lastref")]
                                   if B("px_lastref") < len(P.PXD_WORDS)
                                   else "?"))
            check("%s: the previous picture still shown" % name,
                  (r["mseg"], r["mw"], r["mh"], r["pmode"], r["have"]) == shown
                  and r["have"] == 1, "record %r, was %r" % (r, shown))
            check("%s: claims as they were" % name, claims() == before,
                  "%s -> %s" % (before, claims()))
            pok, here = parts_ok()
            check("%s: no decoder part but the shown picture's" % name, pok,
                  "parts %r, shown format %d" % (here, rec()["fmt"]))

    # --- the scale: memory "allows" 1/2 ---------------------------------------
    for name in ("BIG24.BMP", "BIG8.BMP", "GBIG.GIF", "GBIGI.GIF",
                 "PBIG.PNG", "PBIGI.PNG", "PBIG3I.PNG"):
        if name not in bydata:
            continue
        cl = dir_cluster(DISK, folder[name])
        m.write(base + syms["px_cur"] + 14, bytes((cl & 255, cl >> 8)))
        data = bydata[name][0]
        r = revert(name, cap=9)     # 3 KB at 1/1 + the 8 KB reserve > 9
        check("%s: memory took it to 1/2" % name, r["scl"] == 1,
              "scale 1/%d, record %r, refusal %d, largest %d total %d" %
              (1 << r["scl"], r, B("px_lastref"), W("px_avl"), W("px_avt")))
        if r["scl"] == 1:
            compare(name, data, 1)
    m.write(base + syms["px_mcap"], b"\0\0")

print("pxdecode: %s" % ("ok" if not FAIL else "%d FAILED" % len(FAIL)))
sys.exit(1 if FAIL else 0)
