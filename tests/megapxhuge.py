#!/usr/bin/env python3
"""megapxhuge - PiXEL's master as ONE OSAPI_MEM_HUGE claim on kern_mega's arm,
a window every 32KB, sized by that shape's own ask (SPEC.md 106.8.1, 50.3.5;
docs/plans/MEGA-HUGE-PLAN.md 3).

    make && make mega && python3 tests/megapxhuge.py [--megs M] [--h H]
         [--w W] [--pkg O88] [--find]

86Box's at286_vga_sb with $OS88_MEGA_MEGS of memory (4 unless --megs: the
4MB machine, ~2.9MB of pool free), build/mega.img in A: and a B: of its own:
`make mega`'s PIXEL.O88 (build/megak/apps/pixel.o88) and BIG.PCX, an 8-bit
--w x --h picture (2048 x 1400 by default: a 2,800 KB master at 1/1) made
here, every row different and every 32-pixel run one colour, so the RLE
file fits a floppy. B:/BIG.PCX is double-clicked (PCX is PiXEL's) and:

  1. the picture is decoded at 1/1 (PXR_SCL 0), the master KB past the
     1MB PiXEL could hold before (PX_MWN's 32 windows);
  2. the master's table is the claim's own RUN: [px_hrun] = 8, the shown
     table's window k = the claim + 8k;
  3. rows of the master read through those windows - the first, rows each
     side of 1MB and 2MB into it, the last - are the file's indices;
  4. no exception charged to PiXEL;
  5. the histogram's TAIL past the pixels (SPEC.md 106.20) holds the file's
     counts: at a count of paragraphs past FFFFh, which the shipped build's
     16-bit sum would put back inside the master (pxrec.inc's PX_TAILAT).

--find opens the picture once per height, halving between a height held at
1/1 and one not, and prints the LARGEST master held at 1/1 (to --w x 32
rows) - the measurement, not the row (--megs 1.375 is the 640K + 384K
machine).

THE FAILING CASE (docs/WRITING-TESTS.md 1): --pkg naming mega-release's arm
before the adoption (the master an OSAPI_MEM_CLAIM, conventional on kern_mega
past 64KB and capped at 1MB): the picture opens at 1/4 (SCL 2) and 1 fails.
And with PX_TAILAT's arm made the shipped sum (the first build of this): 1-4
pass and 5 fails. Both seen when the row was written.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
os.environ["OS88_MEGA"] = "1"
os.environ["OS88_MEGA_EMU"] = "86box"
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]

PXR = dict(SW=20, SH=22, MW=32, MH=34, SCL=36, MSEG=38, MKB=40, HAVE=43)
NAME = "BIG.PCX"


def pix(x, y):
    return ((x >> 5) * 5 + y * 3 + (y >> 6)) & 0xFF


def row(w, y):
    return bytes(pix(x, y) for x in range(w))


def make_pcx(path, w, h):
    """An 8-bit, one-plane PCX (version 5, RLE) with the 256-colour palette
    after its 0Ch: runs of at most 63, a byte of C0h or more always coded"""
    hdr = bytearray(128)
    hdr[0], hdr[1], hdr[2], hdr[3] = 10, 5, 1, 8
    struct.pack_into("<HHHHHH", hdr, 4, 0, 0, w - 1, h - 1, 72, 72)
    hdr[65] = 1
    bpl = w + (w & 1)
    struct.pack_into("<HH", hdr, 66, bpl, 1)
    out = bytearray(hdr)
    for y in range(h):
        r = row(w, y) + (b"\0" if w & 1 else b"")
        i = 0
        while i < len(r):
            c = r[i]
            n = 1
            while i + n < len(r) and r[i + n] == c and n < 63:
                n += 1
            if n > 1 or c >= 0xC0:
                out += bytes((0xC0 | n, c))
            else:
                out.append(c)
            i += n
    out.append(0x0C)
    for k in range(256):
        out += bytes((k, (k * 7) & 0xFF, 255 - k))
    open(path, "wb").write(out)
    return len(out)


def run(a, h, work):
    import os88marty as om
    import os88sym
    import os88ui
    os.environ["OS88_MEGA_MEGS"] = a.megs
    sym = os88sym._parse_map(os.path.join(os.path.dirname(a.pkg),
                                          "pixel.map"))[0]
    pcx = os.path.join(work, NAME)
    flen = make_pcx(pcx, a.w, h)
    disk = os.path.join(work, "b.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools",
                                                        "os88disk.py"),
                           "-o", disk, "--size", "1440", a.pkg, pcx,
                           "--folder", "SYSTEM/APPDATA"],
                          stdout=subprocess.DEVNULL)
    res = dict(h=h, held=False)
    with os88ui.boot("build/mega.img", apps=disk, machine="at286_vga_sb",
                     label="megapxhuge") as ui:
        m = ui.m
        f0 = len(m.faults())
        w = ui.path("B:/" + NAME)
        hrun = sym.get("px_hrun")

        def st():
            spans = [(sym["px_cur"], 52), (sym["px_busy"], 1),
                     (sym["px_job"], 1), (sym["px_pcon"], 1),
                     (sym["px_mwc"] + 6, 2 * 8)]
            if hrun:
                spans.append((hrun, 2))
            r = om.pkg_reads(m, w, spans)
            rec = dict((k, r[0][v] if k in ("SCL", "HAVE") else
                        struct.unpack_from("<H", r[0], v)[0])
                       for k, v in PXR.items())
            rec["idle"] = not (r[1][0] or r[2][0] or r[3][0])
            rec["win"] = struct.unpack("<8H", r[4])
            rec["hrun"] = struct.unpack("<H", r[5])[0] if hrun else None
            return rec
        s = st()
        for _ in range(900):            # the decode: 1,400 rows on a 286
            om.guest_sleep(m, 1)
            s = st()
            if s["HAVE"] and s["idle"]:
                break
        print("   megapxhuge: %dx%d (%d-byte PCX), %sMB: have %d scale 1/%d "
              "master %dx%d %d KB seg %04X hrun %s" % (
                  a.w, h, flen, a.megs, s["HAVE"], 1 << s["SCL"], s["MW"],
                  s["MH"], s["MKB"], s["MSEG"], s["hrun"]), flush=True)
        res.update(s, held=bool(s["HAVE"]) and s["SCL"] == 0)
        if a.find:
            return res
        res["checks"] = []

        def want(ok, what):
            res["checks"].append((ok, what))
            print("   %s  %s" % ("ok  " if ok else "FAIL", what), flush=True)
            return ok
        if not want(res["held"] and s["MKB"] > 1024,
                    "1: the %dx%d picture decoded at 1/1 (scale 1/%d, have "
                    "%d), its master %d KB - past PX_MWN's 1MB of before"
                    % (a.w, h, 1 << s["SCL"], s["HAVE"], s["MKB"])):
            return res
        want(s["hrun"] == 8 and all(s["win"][k] == s["MSEG"] + 8 * k
                                    for k in range(8)),
             "2: the table is the claim's run: px_hrun %s, windows %s"
             % (s["hrun"], " ".join("%04X" % v for v in s["win"])))
        mw = s["MW"]
        ys = sorted(set(y for y in (0, 1, (1 << 20) // mw - 1,
                                    (1 << 20) // mw + 1, (2 << 20) // mw,
                                    h - 2, h - 1) if 0 <= y < h))
        bad = []
        for y in ys:
            off = y * mw
            k, o = off >> 15, off & 0x7FFF
            got = m.readseg(s["MSEG"] + 8 * k, o, mw)
            if got != row(a.w, y):
                bad.append(y)
        want(not bad, "3: master rows %s, read through window (offset >> "
             "15) of the run, are the file's%s" % (
                 ",".join(map(str, ys)),
                 "" if not bad else " - rows %s differ" % bad))
        # THE HISTOGRAM'S TAIL past the pixels (106.20): at their
        # paragraphs rounded up to 8 - a count past FFFFh on a master over
        # 1MB, which a 16-bit sum put back inside the master
        tp = ((mw * s["MH"] + 15) // 16 + 7) & ~7
        t = tp * 16
        tail = m.readseg(s["MSEG"] + 8 * (t >> 15), t & 0x7FFF, 1024)
        got = struct.unpack("<256I", tail)
        exp = [0] * 256
        for y in range(h):
            r = row(a.w, y)
            for v in set(r):
                exp[v] += r.count(v)
        want(list(got) == exp, "5: the histogram's tail at byte %X of the "
             "claim (window %d) holds the file's counts%s" % (
                 t, t >> 15, "" if list(got) == exp else
                 " - it does not (first counts %s, the file's %s)"
                 % (got[:4], exp[:4])))
        new = [l for l in m.faults()[f0:] if "PiXEL" in l]
        want(not new, "4: no exception charged to PiXEL (%s)"
             % "; ".join(new[:3]))
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--w", type=int, default=2048)
    ap.add_argument("--h", type=int, default=1400)
    ap.add_argument("--megs", default=os.environ.get("OS88_MEGA_MEGS", "4"))
    ap.add_argument("--pkg", default="build/megak/apps/pixel.o88")
    ap.add_argument("--find", action="store_true",
                    help="the largest master held at 1/1, to --w x 32 rows")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    os.chdir(ROOT)
    a.pkg = os.path.abspath(a.pkg)
    for f in ("build/mega.img", a.pkg,
              os.path.join(os.path.dirname(a.pkg), "pixel.map")):
        if not os.path.exists(f):
            sys.exit("megapxhuge: %s is not built - `make && make mega`" % f)
    work = tempfile.mkdtemp(prefix="megapxhuge-")
    try:
        if a.find:
            lo, hi, h = 32, None, a.h
            while True:
                r = run(a, h, work)
                if r["held"]:
                    lo = h
                    h = h * 2 if hi is None else (lo + hi) // 2
                else:
                    hi = h
                    h = (lo + hi) // 2
                h = h // 32 * 32
                if hi is not None and hi - lo <= 32:
                    break
            print("megapxhuge: %sMB: the largest picture held at 1/1 is "
                  "%d x %d, a %d KB master (%d rows is not)" % (
                      a.megs, a.w, lo, (a.w * lo + 1023) // 1024 + 2, hi))
            return 0
        r = run(a, a.h, work)
        fails = [w for ok, w in r.get("checks", [(False, "no run")]) if not ok]
        print("megapxhuge: %s" % ("ok" if not fails else
                                  "FAILED: " + "; ".join(fails)))
        return 1 if fails else 0
    finally:
        if a.keep:
            print("megapxhuge: kept %s" % work)
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
