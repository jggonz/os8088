#!/usr/bin/env python3
"""PageDesk any-PDF, gfx: fixture generator and host check (PAGEDESK-SPEC 20.22.9).

    python3 tools/os88pdfx_gfx.py --tables             rewrite apps/pagedesk/pdxgmt.c
    python3 tools/os88pdfx_gfx.py --write DIR          colour.pdf + colour.cases into DIR
    python3 tools/os88pdfx_gfx.py --check [--build D]  build the host harness, run every case,
                                                       and hold PDXG against `mutool draw`

Deterministic and standard-library only (the check needs mutool, PIL and cc).
A case is one rectangle filled in one colour space; the harness (hosttest/
pdxg_test.c) prints the colour PDXG computes, the independent reader is
`mutool draw -c rgb` at 72 dpi sampled at the rectangle's centre.
"""
import argparse
import math
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PD = os.path.join(ROOT, "apps", "pagedesk")
FIX = os.path.join(ROOT, "tests", "pdfany", "gfx")


# ----------------------------------------------------------------------------
class Doc:
    def __init__(self):
        self.objs = {}        # num -> (syntax bytes, stream bytes | None)
        self.next = 10

    def add(self, syntax, stream=None):
        n = self.next
        self.next += 1
        self.objs[n] = (syntax, stream)
        return n


def fnum(v):
    s = ("%.5f" % v).rstrip("0").rstrip(".")
    return s if s not in ("", "-0") else "0"


def arr(vals):
    return "[" + " ".join(fnum(v) for v in vals) + "]"


def hexs(b):
    return "<" + b.hex() + ">"


def cases(d):
    """-> list of (name, cs syntax, [comps]). Objects are added to d."""
    out = []

    def add(name, cs, comps):
        out.append((name, cs, comps))

    # --- Indexed -----------------------------------------------------------
    pal = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0])
    for i in range(4):
        add("idx_rgb_%d" % i, "[/Indexed /DeviceRGB 3 %s]" % hexs(pal), [i])
    cm = bytes([0, 0, 0, 0, 255, 0, 0, 0, 0, 255, 0, 0, 0, 0, 255, 0, 128, 128, 0, 64])
    for i in range(5):
        add("idx_cmyk_%d" % i, "[/Indexed /DeviceCMYK 4 %s]" % hexs(cm), [i])
    n = d.add("<< /Length 6 >>", bytes([10, 200, 90, 250, 250, 20]))
    for i in range(2):
        add("idx_stream_%d" % i, "[/Indexed /DeviceRGB 1 %d 0 R]" % n, [i])
    # --- ICCBased (the profile is junk: both readers fall back to /N) -------
    junk = bytes(range(64))
    for nn, comps in ((1, [0.4]), (3, [0.2, 0.5, 0.8]), (4, [0.3, 0.1, 0.6, 0.2])):
        alt = {1: "/DeviceGray", 3: "/DeviceRGB", 4: "/DeviceCMYK"}[nn]
        o = d.add("<< /N %d /Alternate %s /Length %d >>" % (nn, alt, len(junk)), junk)
        add("icc_%d" % nn, "[/ICCBased %d 0 R]" % o, comps)
    # --- calibrated --------------------------------------------------------
    add("calgray_a", "[/CalGray << /WhitePoint [0.9505 1 1.089] /Gamma 2.2 >>]", [0.5])
    add("calgray_b", "[/CalGray << /WhitePoint [0.9505 1 1.089] /Gamma 1 >>]", [0.5])
    cr = ("[/CalRGB << /WhitePoint [0.9505 1 1.089] /Gamma [2.2 2.2 2.2] "
          "/Matrix [0.4124 0.2126 0.0193 0.3576 0.7152 0.1192 0.1805 0.0722 0.9505] >>]")
    add("calrgb_a", cr, [0.2, 0.5, 0.8])
    add("calrgb_b", cr, [0.9, 0.1, 0.1])
    lab = "[/Lab << /WhitePoint [0.9642 1 0.8249] /Range [-128 127 -128 127] >>]"
    for k, c in enumerate(([50, 40, -30], [80, -50, 50], [20, 0, 0], [95, 5, -5])):
        add("lab_%d" % k, lab, c)
    # --- Separation / DeviceN with every function type -----------------------
    f2 = d.add("<< /FunctionType 2 /Domain [0 1] /C0 [0 0 0 0] /C1 [0 1 1 0] /N 1 >>")
    for t in (0.0, 0.5, 1.0):
        add("sep_f2_%s" % fnum(t), "[/Separation /Spot /DeviceCMYK %d 0 R]" % f2, [t])
    f2b = d.add("<< /FunctionType 2 /Domain [0 1] /C0 [1 1 1] /C1 [0.9 0.2 0.1] /N 2.2 >>")
    add("sep_f2n", "[/Separation /Spot2 /DeviceRGB %d 0 R]" % f2b, [0.5])
    ps = b"{ dup 1 exch sub exch 0.5 mul 0.8 exch }"
    f4 = d.add("<< /FunctionType 4 /Domain [0 1] /Range [0 1 0 1 0 1] /Length %d >>" % len(ps), ps)
    for t in (0.2, 0.7):
        add("sep_f4_%s" % fnum(t), "[/Separation /Spot3 /DeviceRGB %d 0 R]" % f4, [t])
    ps = b"{ dup 0.5 gt { pop 1 0 0 } { pop 0 0 1 } ifelse }"
    f4i = d.add("<< /FunctionType 4 /Domain [0 1] /Range [0 1 0 1 0 1] /Length %d >>" % len(ps), ps)
    for t in (0.2, 0.8):
        add("sep_f4if_%s" % fnum(t), "[/Separation /Spot4 /DeviceRGB %d 0 R]" % f4i, [t])
    ps = b"{ 360 mul sin 1 add 2 div dup dup }"
    f4s = d.add("<< /FunctionType 4 /Domain [0 1] /Range [0 1 0 1 0 1] /Length %d >>" % len(ps), ps)
    for t in (0.1, 0.25, 0.4):
        add("sep_f4sin_%s" % fnum(t), "[/Separation /Spot5 /DeviceRGB %d 0 R]" % f4s, [t])
    ps = b"{ sqrt dup dup 0.5 exp }"
    f4e = d.add("<< /FunctionType 4 /Domain [0 1] /Range [0 1 0 1 0 1] /Length %d >>" % len(ps), ps)
    add("sep_f4sqrt", "[/Separation /Spot6 /DeviceRGB %d 0 R]" % f4e, [0.36])
    samples = bytes([255, 255, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255])
    f0 = d.add("<< /FunctionType 0 /Domain [0 1] /Range [0 1 0 1 0 1] /Size [4] "
               "/BitsPerSample 8 /Length %d >>" % len(samples), samples)
    for t in (0.0, 0.2, 0.5, 0.9, 1.0):
        add("sep_f0_%s" % fnum(t), "[/Separation /Spot7 /DeviceRGB %d 0 R]" % f0, [t])
    s16 = bytes([0, 0, 0, 0, 0, 0, 255, 255, 128, 0, 0, 0])
    f016 = d.add("<< /FunctionType 0 /Domain [0 1] /Range [0 1 0 1 0 1] /Size [2] "
                 "/BitsPerSample 16 /Length %d >>" % len(s16), s16)
    add("sep_f016", "[/Separation /Spot8 /DeviceRGB %d 0 R]" % f016, [0.5])
    s2 = bytes([0, 0, 0, 255, 255, 0, 0, 128, 255, 255, 255, 255])
    f02 = d.add("<< /FunctionType 0 /Domain [0 1 0 1] /Range [0 1 0 1 0 1] /Size [2 2] "
                "/BitsPerSample 8 /Length %d >>" % len(s2), s2)
    add("devn_f0_2d", "[/DeviceN [/A /B] /DeviceRGB %d 0 R]" % f02, [0.25, 0.75])
    fa = d.add("<< /FunctionType 2 /Domain [0 1] /C0 [1 1 1] /C1 [1 0 0] /N 1 >>")
    fb = d.add("<< /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >>")
    f3 = d.add("<< /FunctionType 3 /Domain [0 1] /Functions [%d 0 R %d 0 R] /Bounds [0.5] "
               "/Encode [0 1 0 1] >>" % (fa, fb))
    for t in (0.1, 0.4, 0.6, 0.95):
        add("sep_f3_%s" % fnum(t), "[/Separation /Spot9 /DeviceRGB %d 0 R]" % f3, [t])
    ps = b"{ 2 copy mul }"
    f4d = d.add("<< /FunctionType 4 /Domain [0 1 0 1] /Range [0 1 0 1 0 1] /Length %d >>" % len(ps), ps)
    add("devn_f4", "[/DeviceN [/A /B] /DeviceRGB %d 0 R]" % f4d, [0.3, 0.6])
    add("sep_none", "[/Separation /None /DeviceRGB %d 0 R]" % f4, [0.5])
    # an Indexed space over a Separation, through its alternate
    add("idx_sep", "[/Indexed [/Separation /S2 /DeviceRGB %d 0 R] 2 %s]" % (f4, hexs(bytes([0, 128, 255]))), [1])
    return out


def write_pdf(path, d, cs):
    cols = 12
    cell = 10
    rows = (len(cs) + cols - 1) // cols
    W, H = cols * cell, rows * cell
    res = " ".join("/C%d %s" % (i, c[1]) for i, c in enumerate(cs))
    cont = []
    for i, (name, _, comps) in enumerate(cs):
        x, y = (i % cols) * cell, H - (i // cols + 1) * cell
        cont.append("/C%d cs %s sc %d %d %d %d re f" % (i, " ".join(fnum(v) for v in comps), x, y, cell, cell))
    content = ("\n".join(cont)).encode()
    objs = {
        1: (b"<< /Type /Catalog /Pages 2 0 R >>", None),
        2: (b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>", None),
        3: (("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Contents 4 0 R "
             "/Resources << /ColorSpace << %s >> >> >>" % (W, H, res)).encode(), None),
        4: (("<< /Length %d >>" % len(content)).encode(), content),
    }
    for n, (syn, st) in d.objs.items():
        objs[n] = (syn.encode(), st)
    out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    offs = {}
    for n in sorted(objs):
        offs[n] = len(out)
        syn, st = objs[n]
        out += ("%d 0 obj\n" % n).encode() + syn
        if st is not None:
            out += b"\nstream\n" + st + b"\nendstream"
        out += b"\nendobj\n"
    top = max(objs) + 1
    x = len(out)
    out += ("xref\n0 %d\n0000000000 65535 f \n" % top).encode()
    for n in range(1, top):
        out += (("%010d 00000 n \n" % offs[n]) if n in offs else "0000000000 65535 f \n").encode()
    out += ("trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (top, x)).encode()
    open(path, "wb").write(bytes(out))
    return W, H, cols, cell


def write_cases(path, d, cs):
    with open(path, "w") as f:
        for n, (syn, st) in sorted(d.objs.items()):
            f.write("O %d %s\n" % (n, syn.encode().hex()))
            if st is not None:
                f.write("S %d %s\n" % (n, st.hex()))
        for name, text, comps in cs:
            f.write("C %s %s %d %s\n" % (name, text.encode().hex(), len(comps),
                                         " ".join(repr(float(c)) for c in comps)))


def build(outdir):
    os.makedirs(outdir, exist_ok=True)
    d = Doc()
    cs = cases(d)
    pdf = os.path.join(outdir, "colour.pdf")
    geom = write_pdf(pdf, d, cs)
    write_cases(os.path.join(outdir, "colour.cases"), d, cs)
    return pdf, cs, geom


def mutool_pixels(pdf, cs, geom, workdir):
    """Two readings of the same page. `-N` (no colour management) is the reference
    for everything but the calibrated spaces, which it treats as device spaces;
    the default run (lcms) honours CalGray/CalRGB/Lab but also moves DEVICE colours
    through a gamut map, so it is the reference for those three classes only.
    `-M 0` keeps a Separation's tint transform in play (the default spot
    simulation paints the colorant's full-tint ink at t of the way instead)."""
    from PIL import Image
    W, H, cols, cell = geom
    res = {}
    for tag, extra in (("n", ["-N", "-M", "0"]), ("c", ["-M", "0"])):
        png = os.path.join(workdir, "colour_%s.png" % tag)
        subprocess.check_call(["mutool", "draw", "-q"] + extra + ["-r", "72", "-c", "rgb", "-o", png, pdf],
                              stderr=subprocess.DEVNULL)
        im = Image.open(png).convert("RGB")
        for i, (name, _, _) in enumerate(cs):
            x = (i % cols) * cell + cell // 2
            y = (i // cols) * cell + cell // 2
            cms = name.startswith(("calgray", "calrgb", "lab_"))
            if cms == (tag == "c"):
                res[name] = im.getpixel((x, y))
    return res


# per-class thresholds on the largest channel difference (0..255) -- see the report
TOL = [("idx_cmyk", 40), ("icc_4", 40), ("idx_", 3), ("icc_", 3), ("sep_", 6), ("devn_", 6), ("calgray", 14), ("calrgb", 14), ("lab_", 14)]


def tol_of(name):
    for p, t in TOL:
        if name.startswith(p):
            return t
    return 4


def check(build_dir, verbose):
    work = os.path.join(build_dir, "pdfany-gfx")
    os.makedirs(work, exist_ok=True)
    pdf, cs, geom = build(work)
    exe = os.path.join(work, "pdxg_test")
    srcs = [os.path.join(PD, "hosttest", "pdxg_test.c"), os.path.join(PD, "pdmath.c"),
            os.path.join(PD, "hosttest", "pdhost.c")]
    cc = ["cc", "-std=gnu99", "-O1", "-Wall", "-Wno-parentheses", "-Wno-unused-function",
          "-DPD_HOST", "-I", PD, "-o", exe] + srcs
    subprocess.check_call(cc)
    got = {}
    p = subprocess.run([exe, os.path.join(work, "colour.cases")], capture_output=True, text=True)
    if p.returncode:
        print(p.stdout, p.stderr)
        return 1
    for line in p.stdout.splitlines():
        t = line.split()
        if len(t) == 4:
            got[t[0]] = tuple(int(v) for v in t[1:])
        elif len(t) == 2 and t[1] == "NONE":
            got[t[0]] = None
    ref = mutool_pixels(pdf, cs, geom, work)
    bad = 0
    worst = {}
    for name, _, _ in cs:
        g, r = got.get(name, "missing"), ref[name]
        if g is None:
            ok = name == "sep_none"            # PDXG paints nothing for /None (mutool -M 0 paints the tint)
            diff = 0 if ok else 255
        elif g == "missing":
            ok, diff = False, 255
        else:
            diff = max(abs(a - b) for a, b in zip(g, r))
            ok = diff <= tol_of(name)
        cls = name.rsplit("_", 1)[0] if name[-1].isdigit() else name
        worst[cls] = max(worst.get(cls, 0), diff)
        if not ok:
            bad += 1
        if verbose or not ok:
            print("%-4s %-16s pdxg=%-18s mutool=%-18s diff=%3d tol=%d" %
                  ("ok" if ok else "FAIL", name, g, r, diff, tol_of(name)))
    print("pdfx_gfx colour: %d cases, %d outside tolerance" % (len(cs), bad))
    return 1 if bad else 0


def tables():
    def arr_(name, vals, per=10):
        s = "static const pdw %s[%d] = {\n" % (name, len(vals))
        for i in range(0, len(vals), per):
            s += "    " + ", ".join("%d" % v for v in vals[i:i + per]) + ",\n"
        return s + "};\n"

    def enc(x):
        return 12.92 * x if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055
    lg = [min(65535, round(math.log2(1 + i / 64) * 65536)) for i in range(65)]
    ex = [min(65535, round((2 ** (i / 64) - 1) * 65536)) for i in range(65)]
    sn = [min(65535, round(math.sin(math.radians(i)) * 65536)) for i in range(91)]
    at = [round(math.degrees(math.atan(i / 64)) * 256) for i in range(65)]
    se = [min(65535, round(enc(i / 64) * 65535)) for i in range(65)]
    src = ("/* ============================================================================\n"
           " * os8088 - apps/pagedesk/pdxgmt.c   PDXG's math tables (20.22.9)\n"
           " * Generated by tools/os88pdfx_gfx.py --tables; the data of pdxgm.c.\n"
           " * ==========================================================================*/\n")
    src += arr_("gm_log2", lg) + arr_("gm_exp2", ex) + arr_("gm_sin", sn) + arr_("gm_atan", at) + arr_("gm_senc", se)
    open(os.path.join(PD, "pdxgmt.c"), "w").write(src)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--write", metavar="DIR")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--build", default=os.path.join(ROOT, "build"))
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    if a.tables:
        tables()
    if a.write:
        build(a.write)
    if a.check:
        sys.exit(check(a.build, a.v))


if __name__ == "__main__":
    main()
