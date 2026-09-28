#!/usr/bin/env python3
"""What a video frame costs on a cycle-exact 5150 - docs/plans/VIDEO-PLAN.md
wave 0 (a) and (d).

    make vidbench && python3 tests/vidbench.py --samples DIR
                                     [--machine os8088_5150_herc_gla] [--ibm]

AN INSTRUMENT, and its one assertion is the PICTURE. tests/vidbench/ runs in
the guest: it applies each frame to a black RAM canvas three ways (XDC's own
program and vd_native) and compares each
canvas's checksum with the one tools/os88vid.py computed on the host; then,
inside an fsx bracket in the mode the player would use on this adapter, it
times each frame three ways with benchlib's PIT method, which MartyPC counts to
the cycle. This builds the data file and a scratch floppy, boots, opens the
bench, presses R, waits on the GUEST's clock and reads the arrays back.

It FAILS if any frame's picture differs on any path, or if any row did not
produce a number. The timings are reported and never gated (tests/tank.py's
rule) - they go to docs/reports/ with the date and the machine.

THE SAMPLES ARE NOT IN THE TREE. They are XDC streams the owner supplied
(VIDEO-PLAN section 13) and are copyrighted, so `--samples DIR` (or
$OS88_XDC_SAMPLES) names a directory holding BADAPPLE.XDV, THUNDERC.XDV,
TRONDISC.XDV and BBBB_BW.XDV; the row SKIPS without it.

THE MACHINE: a GLaBIOS twin by default, because nothing
measured here goes through the ROM - it is the CPU and the card's memory.
`--ibm` runs the period ROM anyway, for a figure a reviewer can set beside a
field run.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88flush, os88geom as geom  # noqa: E402
from cycweb import pkg_syms                         # noqa: E402

HZ = 4772727.0                  # the 5150's 8088, PERFORMANCE.md Part 2
MAXF = 32                       # VB_MAXF
NKIND = 3                       # VB_NKIND
NRES = 8 + MAXF * NKIND         # VB_NRES
STREAMS = ("BADAPPLE.XDV", "THUNDERC.XDV", "TRONDISC.XDV", "BBBB_BW.XDV")
RAW = ("movsb 8000 to screen", "movsw 8000 to screen",
       "movsb 8000 to RAM", "movsw 8000 to RAM")
KINDS = ("XDC scr", "nat scr", "nat ram")
MODES = {3: "CGA640", 4: "HERC", 7: "VGA12"}
VKIND = {0: "VGA", 1: "HERC", 2: "CGA", 3: "EGA"}


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def skip(why):
    print("vidbench: SKIP - %s" % why)
    sys.exit(0)


def build_dat(samples, out, frames=None):
    paths = [os.path.join(samples, s) for s in STREAMS]
    missing = [p for p in paths if not os.path.exists(p)]
    if missing:
        skip("no %s" % ", ".join(missing))
    # the picks and the one-construct frames, or exactly the frames asked for
    sel = ["--synth"] if not frames else \
        [x for f in frames for x in ("--frame", f)]
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools/os88vid.py"),
                        "benchdat", "--limit", str(190 * 1024)] + sel +
                       [out] + paths, capture_output=True, text=True)
    if r.returncode:
        sys.exit("vidbench: os88vid benchdat failed:\n" + r.stdout + r.stderr)
    return r.stdout


def dat_dir(path):
    d = open(path, "rb").read()
    n = u16(d, 4)
    out = []
    for i in range(n):
        e = 8 + 32 * i
        lab = d[e:e + 12].split(b"\0")[0].decode()
        spans, ents, wb, cyc16 = struct.unpack_from("<HHHH", d, e + 22)
        out.append(dict(label=lab, spans=spans, ents=ents, wb=wb,
                        model=cyc16 * 16))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", default=os.environ.get("OS88_XDC_SAMPLES"))
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--ibm", action="store_true",
                    help="run the period IBM ROM, not its GLaBIOS twin")
    ap.add_argument("--label", default="")
    ap.add_argument("--frame", action="append", metavar="FILE:INDEX",
                    help="time exactly these frames (os88vid benchdat --frame)")
    ap.add_argument("--dat", help="run this VIDBENCH.DAT as it is - `make "
                    "vidfield`'s, to check the field disk's own file")
    a = ap.parse_args()
    os.chdir(ROOT)
    if not a.samples and not a.dat:
        skip("no --samples DIR / $OS88_XDC_SAMPLES (the XDC streams are the "
             "owner's and are not in the tree)")

    syms, image = pkg_syms("tests/vidbench/vidbench.asm", ("apps/", "tests/"))
    try:
        built = open(os88build.at("build/vidbench.bin"), "rb").read()
    except OSError:
        sys.exit("vidbench: no build/vidbench.bin - run `make vidbench`")
    if built != image:
        sys.exit("vidbench: build/vidbench.bin is behind the tree - run "
                 "`make vidbench`")

    with tempfile.TemporaryDirectory() as tmp:
        dat = os.path.join(tmp, "VIDBENCH.DAT")
        if a.dat:
            with open(a.dat, "rb") as src, open(dat, "wb") as dst:
                dst.write(src.read())
        else:
            print(build_dat(a.samples, dat, a.frame), end="")
        frames = dat_dir(dat)
        disk = os.path.join(tmp, "vidbench.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", os88build.at("build/vidbench.o88"),
                        dat], check=True, capture_output=True)
        # --ibm: the same machine on the period ROM. Nothing measured here
        # goes through the ROM (it is the CPU and the card's memory), so the
        # twin is the default and the ROM is for setting a figure beside a
        # field run
        machine, why = a.machine, None
        if a.ibm:
            machine = a.machine.replace("_gla", "")
            why = ("--ibm: a figure to set beside a field run on the "
                   "owner's 5150")
        with os88ui.boot(os88build.at(a.image), apps=disk, machine=machine,
                         why_ibm=why) as ui:
            m = ui.m
            w = ui.path("B:/VIDBENCH.O88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            seg = u16(rec, geom.W_SEG)
            base = seg << 4

            def rw(name):
                return u16(m.read(base + syms[name], 2))

            m.type_text("r")
            # THE WAIT IS ON THE GUEST'S CLOCK: ~16 frames x 5 rows x 4
            # iterations of up to 21 ms, plus the checks - seconds of 8088.
            os88marty.until(m, lambda mm: rw("vb_done") != 0,
                            "the bench run to finish", poll=0.5, limit=900.0,
                            guest=300.0)
            if rw("vb_done") == 0xFFFF:
                sys.exit("vidbench: the guest found no usable VIDBENCH.DAT")
            nf = rw("vb_nf")
            res = m.read(base + syms["vb_res"], NRES * 4)
            flg = m.read(base + syms["vb_resf"], NRES)
            chk = m.read(base + syms["vb_chk"], MAXF)
            fsxm = m.read(base + syms["vb_fsxm"], 1)[0]
            vkind = m.read(base + syms["vb_vkind"], 1)[0]
            inmode = m.read(base + syms["vb_inmode"], 1)[0]
            full = m.read(base + syms["bl_full"], 1)[0]
            try:                    # the report the bench SAVED (benchlib's
                txt = os88flush.Flush(marty=m).volume(1).read(  # bl_save):
                    "VIDBENCH.TXT").decode("latin-1")         # B:, where it
            except Exception as e:                            # was launched
                txt = None
                txterr = str(e)

    def us(i):
        return int.from_bytes(res[i * 4:i * 4 + 4], "little") / 100.0

    bad = []
    print()
    print("   machine %s%s: adapter %s, bracket mode %s%s" % (
        machine, " (IBM ROM)" if a.ibm else "", VKIND.get(vkind, vkind),
        MODES.get(fsxm, fsxm), "" if inmode else "  ** MODE REFUSED **"))
    if a.label:
        print("   arm: %s" % a.label)
    print()
    print("   %-24s %10s %9s %9s" % ("raw row", "us/iter", "cycles", "cyc/B"))
    for i, lab in enumerate(RAW):
        v = us(i)
        if flg[i] in (0, ord("-")):
            bad.append("raw row %r produced no number" % lab)
        c = v * HZ / 1e6
        print("   %-24s %10.1f %9.0f %9.2f %s" % (lab, v, c, c / 8000,
                                               chr(flg[i]) if flg[i] else "?"))
    print()
    print("   %-12s %5s %5s %5s | %s | %s" % (
        "frame", "spans", "ents", "bytes",
        " ".join("%9s" % k for k in KINDS), "nat/XDC  card     pic"))
    for f in range(nf):
        fr = frames[f]
        row = []
        for k in range(NKIND):
            i = 8 + f * NKIND + k
            if flg[i] in (0, ord("-")):
                bad.append("%s %s produced no number" % (fr["label"], KINDS[k]))
            row.append(us(i) * HZ / 1e6)
        ok = chk[f] == 3
        if not ok:
            bad.append("%s: the picture differs (paths %s)" % (
                fr["label"], ", ".join(n for b, n in ((1, "XDC"),
                                                      (2, "native"))
                                       if not chk[f] & b)))
        print("   %-12s %5d %5d %5d | %s | %7.2fx %7.0f  %s" % (
            fr["label"], fr["spans"], fr["ents"], fr["wb"],
            " ".join("%9.0f" % c for c in row), row[1] / row[0],
            row[1] - row[2], "OK" if ok else "BAD"))
    print("\n   (cycles per frame; the XDC model column in the data file is "
          "XDC's own estimate, for comparison)")
    for f in range(nf):
        fr = frames[f]
        print("   %-12s XDC's own model %6d  measured XDC scr %6.0f" % (
            fr["label"], fr["model"], us(8 + f * NKIND) * HZ / 1e6))
    if full:
        bad.append("the report TRUNCATED (bl_full): the arena is too small")
    if txt is None:
        bad.append("no VIDBENCH.TXT on B: - the save did not happen (%s)"
                   % txterr)
    elif "per mille" not in txt or "TRUNCATED" in txt:
        bad.append("VIDBENCH.TXT is not the whole report")
    else:
        print("\n   VIDBENCH.TXT saved: %d lines" % len(txt.splitlines()))
    if bad:
        print()
        for b in bad:
            print("   FAIL: %s" % b)
        return 1
    print("\n   ok: every frame's picture matches on both paths, and "
          "every row produced a number")
    return 0


if __name__ == "__main__":
    sys.exit(main())
