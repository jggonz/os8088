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
    page = ("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Contents 4 0 R "
            "/Resources << /ColorSpace << %s >> >> >>" % (W, H, res)).encode()
    objs = {
        1: (b"<< /Type /Catalog /Pages 2 0 R >>", None),
        2: (b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>", None),
        3: (page, None),
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
    return W, H, cols, cell, page


def write_cases(path, d, cs, page=None):
    with open(path, "w") as f:
        if page is not None:
            f.write("O 3 %s\n" % page.hex())
            f.write("O 2 %s\n" % b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>".hex())
        for n, (syn, st) in sorted(d.objs.items()):
            f.write("O %d %s\n" % (n, syn.encode().hex()))
            if st is not None:
                f.write("S %d %s\n" % (n, st.hex()))
        for i, (name, text, comps) in enumerate(cs):
            f.write("C %s %s %d %s\n" % (name, text.encode().hex(), len(comps),
                                         " ".join(repr(float(c)) for c in comps)))
            # the same space reached through the page's resources, the way the core asks (PDGXI_RES)
            f.write("O %d %s\n" % (3000 + i, ("<< /Type /Page /Parent 2 0 R /Resources << /ColorSpace << /C%d %s >> >> >>" % (i, text)).encode().hex()))
            f.write("R res_%s %s %d %s\n" % (name, ("C%d" % i).encode().hex(), len(comps),
                                            " ".join(repr(float(c)) for c in comps)))


def build(outdir):
    os.makedirs(outdir, exist_ok=True)
    d = Doc()
    cs = cases(d)
    pdf = os.path.join(outdir, "colour.pdf")
    geom = write_pdf(pdf, d, cs)
    write_cases(os.path.join(outdir, "colour.cases"), d, cs, geom[4])
    return pdf, cs, geom[:4]


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
    cc = ["cc", "-std=gnu99", "-O1", "-Wall", "-fno-strict-aliasing", "-Wno-parentheses", "-Wno-unused-function",
          "-DPD_HOST", "-I", PD, "-o", exe] + srcs
    subprocess.check_call(cc)
    got = {}
    p = subprocess.run([exe, os.path.join(work, "colour.cases")], capture_output=True, text=True)
    if p.returncode:
        print(p.stdout, p.stderr)
        return 1
    for line in p.stdout.splitlines():
        t = line.split()
        if len(t) == 4 and t[1] != "FAILED":
            got[t[0]] = tuple(int(v) for v in t[1:])
        elif len(t) == 2 and t[1] == "NONE":
            got[t[0]] = None
    ref = mutool_pixels(pdf, cs, geom, work)
    bad = 0
    for name, _, _ in cs:               # the resource route must give exactly the direct route's colour
        if got.get("res_" + name, "m") != got.get(name, "d"):
            print("FAIL res_%s: %s != %s" % (name, got.get("res_" + name), got.get(name)))
            bad += 1
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
    mod = os.path.join(build_dir, "PDXG.MOD")
    try:
        import unicorn  # noqa: F401
        have_uc = os.path.exists(mod)
    except ImportError:
        have_uc = False
    if have_uc:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import os88pdfx_gfx_t8 as t8
        tgt = {}
        for line in t8.run(mod, os.path.join(work, "colour.cases")):
            t = line.split()
            tgt[t[0]] = None if t[1] == "NONE" else tuple(int(v) for v in t[1:])
        nb = 0
        for name, _, _ in cs:
            a, b = tgt.get(name), got.get(name)
            if (a is None) != (b is None) or (a and max(abs(x - y) for x, y in zip(a, b)) > 1):
                print("FAIL 8086 %-16s target=%s host=%s" % (name, a, b)); nb += 1
        print("pdfx_gfx 8086 (Unicorn) vs host: %d cases, %d differ by more than 1" % (len(cs), nb))
        bad += nb
    else:
        print("pdfx_gfx 8086 (Unicorn): SKIP (no unicorn, or build/PDXG.MOD not built)")
    bad2 = check_shade(build_dir, work, exe, verbose)
    return 1 if (bad or bad2) else 0


# ---------------------------------------------------------------- shading ----
def bits(vals):
    """[(value, nbits)...] -> bytes, MSB first, padded at the end."""
    acc, n = 0, 0
    for v, w in vals:
        acc = (acc << w) | (v & ((1 << w) - 1)); n += w
    pad = (-n) % 8
    acc <<= pad
    return acc.to_bytes((n + pad) // 8, "big")


def vtx(flag, x, y, c, bf=8, bc=8, bk=8, with_flag=True):
    v = [(flag, bf)] if with_flag else []
    v += [(x, bc), (y, bc)] + [(k, bk) for k in c]
    return bits(v)


def shade_cases(d):
    """-> [(name, shading object number)]; each is drawn in a 64x48 cell."""
    out = []
    g = d.add("<< /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [1] /N 1 >>")
    out.append(("ax_gray", d.add("<< /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 64 0] /Function %d 0 R /Extend [true true] >>" % g)))
    a = d.add("<< /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 1 0] /N 1 >>")
    b = d.add("<< /FunctionType 2 /Domain [0 1] /C0 [0 1 0] /C1 [0 0 1] /N 1 >>")
    st = d.add("<< /FunctionType 3 /Domain [0 1] /Functions [%d 0 R %d 0 R] /Bounds [0.5] /Encode [0 1 0 1] >>" % (a, b))
    out.append(("ax_rgb_diag", d.add("<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 64 48] /Function %d 0 R /Extend [true true] >>" % st)))
    out.append(("ax_noext", d.add("<< /ShadingType 2 /ColorSpace /DeviceGray /Coords [16 0 48 0] /Function %d 0 R /Extend [false false] >>" % g)))
    out.append(("ax_ext0", d.add("<< /ShadingType 2 /ColorSpace /DeviceGray /Coords [16 0 48 0] /Function %d 0 R /Extend [true false] >>" % g)))
    e = d.add("<< /FunctionType 2 /Domain [0 1] /C0 [0.9] /C1 [0.1] /N 1.5 >>")
    out.append(("rad_gray", d.add("<< /ShadingType 3 /ColorSpace /DeviceGray /Coords [32 24 0 32 24 22] /Function %d 0 R /Extend [false true] >>" % e)))
    out.append(("rad_off", d.add("<< /ShadingType 3 /ColorSpace /DeviceRGB /Coords [20 24 4 44 24 14] /Function %d 0 R /Extend [true true] >>" % st)))
    out.append(("rad_same", d.add("<< /ShadingType 3 /ColorSpace /DeviceGray /Coords [32 24 6 32 24 6] /Function %d 0 R /Extend [true true] >>" % g)))
    ps = b"{ mul }"
    f1 = d.add("<< /FunctionType 4 /Domain [0 1 0 1] /Range [0 1] /Length %d >>" % len(ps), ps)
    out.append(("fn1", d.add("<< /ShadingType 1 /ColorSpace /DeviceGray /Domain [0 1 0 1] /Matrix [64 0 0 48 0 0] /Function %d 0 R >>" % f1)))
    dec = "/BitsPerCoordinate 8 /BitsPerComponent 8 /BitsPerFlag 8 /Decode [0 64 0 48 0 1 0 1 0 1]"
    data = (vtx(0, 0, 0, (255, 0, 0)) + vtx(0, 255, 0, (0, 255, 0)) + vtx(0, 128, 255, (0, 0, 255))
            + vtx(1, 255, 255, (255, 255, 255)))
    out.append(("mesh4", d.add("<< /ShadingType 4 /ColorSpace /DeviceRGB %s /Length %d >>" % (dec, len(data)), data)))
    dec5 = "/BitsPerCoordinate 8 /BitsPerComponent 8 /Decode [0 64 0 48 0 1 0 1 0 1] /VerticesPerRow 3"
    data = b"".join(vtx(0, x, y, c, with_flag=False) for x, y, c in (
        (0, 0, (255, 0, 0)), (128, 0, (0, 255, 0)), (255, 0, (0, 0, 255)),
        (0, 255, (0, 0, 255)), (128, 255, (255, 0, 0)), (255, 255, (0, 255, 0))))
    out.append(("mesh5", d.add("<< /ShadingType 5 /ColorSpace /DeviceRGB %s /Length %d >>" % (dec5, len(data)), data)))
    bnd = [(0, 0), (0, 85), (0, 170), (0, 255), (85, 255), (170, 255), (255, 255), (255, 170), (255, 85), (255, 0), (170, 0), (85, 0)]
    cols = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)]
    def patch(pts, cs, flag=0):
        return bits([(flag, 8)]) + b"".join(bits([(x, 8), (y, 8)]) for x, y in pts) + \
            b"".join(bits([(k, 8) for k in c]) for c in cs)
    data = patch(bnd, cols)
    out.append(("coons6", d.add("<< /ShadingType 6 /ColorSpace /DeviceRGB %s /Length %d >>" % (dec, len(data)), data)))
    bent = [(0, 0), (20, 85), (0, 170), (0, 255), (85, 235), (170, 255), (255, 255), (235, 170), (255, 85), (255, 0), (170, 20), (85, 0)]
    data = patch(bent, cols)
    out.append(("coons6b", d.add("<< /ShadingType 6 /ColorSpace /DeviceRGB %s /Length %d >>" % (dec, len(data)), data)))
    inner = [(85, 85), (85, 170), (170, 170), (170, 85)]
    data = patch(bent + inner, cols)
    out.append(("tensor7", d.add("<< /ShadingType 7 /ColorSpace /DeviceRGB %s /Length %d >>" % (dec, len(data)), data)))
    # two patches, the second sharing the first's right edge (flag 1)
    half = [(0, 0), (0, 42), (0, 85), (0, 127), (42, 127), (85, 127), (127, 127), (127, 85), (127, 42), (127, 0), (85, 0), (42, 0)]
    nxt = [(127, 127 + 0)]  # unused
    p1 = patch(half, cols)
    new8 = [(127, 170), (127, 212), (127, 255), (170, 255), (212, 255), (255, 255), (255, 212), (255, 170)]
    p2 = bits([(1, 8)]) + b"".join(bits([(x, 8), (y, 8)]) for x, y in new8) + \
        b"".join(bits([(k, 8) for k in c]) for c in ((255, 255, 255), (40, 40, 40)))
    data = p1 + p2
    out.append(("coons_flag", d.add("<< /ShadingType 6 /ColorSpace /DeviceRGB %s /Length %d >>" % (dec, len(data)), data)))
    return out


def write_shade(outdir):
    d = Doc()
    cs = shade_cases(d)
    cols, cw, ch = 4, 64, 48
    rows = (len(cs) + cols - 1) // cols
    W, H = cols * (cw + 4), rows * (ch + 4)
    sh = " ".join("/S%d %d 0 R" % (i, o) for i, (_, o) in enumerate(cs))
    cont = []
    geom = {}
    for i, (name, _) in enumerate(cs):
        x, y = (i % cols) * (cw + 4) + 2, H - (i // cols + 1) * (ch + 4) + 2
        geom[name] = (x, H - y - ch)           # device (left, top)
        cont.append("q %d %d %d %d re W n 1 0 0 1 %d %d cm /S%d sh Q" % (x, y, cw, ch, x, y, i))
    content = "\n".join(cont).encode()
    objs = {1: (b"<< /Type /Catalog /Pages 2 0 R >>", None), 2: (b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>", None),
            3: (("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Contents 4 0 R /Resources << /Shading << %s >> >> >>" % (W, H, sh)).encode(), None),
            4: (("<< /Length %d >>" % len(content)).encode(), content)}
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
    pdf = os.path.join(outdir, "shade.pdf")
    open(pdf, "wb").write(bytes(out))
    with open(os.path.join(outdir, "shade.cases"), "w") as f:
        for n, (syn, st) in sorted(d.objs.items()):
            f.write("O %d %s\n" % (n, syn.encode().hex()))
            if st is not None:
                f.write("S %d %s\n" % (n, st.hex()))
        for name, o in cs:
            f.write("H %s %d\n" % (name, o))
    return pdf, cs, geom, (cw, ch, W, H)


def parse_shade_out(text):
    res, cur = {}, None
    for line in text.splitlines():
        t = line.split()
        if not t:
            continue
        if t[0] == "SH":
            cur = {"name": t[1], "tris": []}
            res[t[1]] = cur
            if t[2] == "FAILED":
                cur["failed"] = True
                continue
            cur.update(type=int(t[2]), flags=int(t[3]), t0=float(t[4]), t1=float(t[5]),
                       coords=[float(v) for v in t[6:12]])
        elif t[0] == "R":
            cur["ramp"] = [int(v) for v in t[1:]]
        elif t[0] == "T":
            v = t[1:]
            cur["tris"].append([(float(v[4 * k]), float(v[4 * k + 1]), int(v[4 * k + 2])) for k in range(3)])
    return res


def ref_raster(sh, cw, ch):
    """The module's output painted as the core is specified to paint it (float, exact)."""
    img = [[255] * cw for _ in range(ch)]
    for j in range(ch):
        for i in range(cw):
            px, py = i + 0.5, ch - (j + 0.5)
            if sh["type"] in (2, 3):
                c = sh["coords"]
                ext0, ext1 = sh["flags"] & 1, sh["flags"] & 2
                if sh["type"] == 2:
                    dx, dy = c[2] - c[0], c[3] - c[1]
                    L2 = dx * dx + dy * dy
                    s = ((px - c[0]) * dx + (py - c[1]) * dy) / L2 if L2 else 0
                else:
                    x0, y0, r0, x1, y1, r1 = c
                    dx, dy, dr = x1 - x0, y1 - y0, r1 - r0
                    a = dx * dx + dy * dy - dr * dr
                    qx, qy = px - x0, py - y0
                    b = qx * dx + qy * dy + r0 * dr
                    cc = qx * qx + qy * qy - r0 * r0
                    cand = []
                    if abs(a) < 1e-9:
                        if abs(b) > 1e-9:
                            cand = [cc / (2 * b)]
                    else:
                        disc = b * b - a * cc
                        if disc >= 0:
                            r = disc ** 0.5
                            cand = [(b + r) / a, (b - r) / a]
                    s = None
                    for sc in sorted(cand, reverse=True):
                        if r0 + sc * dr < 0:
                            continue
                        if (sc < 0 and not ext0) or (sc > 1 and not ext1):
                            continue
                        s = sc
                        break
                    if s is None:
                        continue
                if s < 0:
                    if not ext0:
                        continue
                    s = 0
                if s > 1:
                    if not ext1:
                        continue
                    s = 1
                img[j][i] = sh["ramp"][int(round(s * 255))]
    for tri in sh["tris"]:
        (x0, y0, l0), (x1, y1, l1), (x2, y2, l2) = tri
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-9:
            continue
        for j in range(ch):
            for i in range(cw):
                px, py = i + 0.5, ch - (j + 0.5)
                w0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / den
                w1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / den
                w2 = 1 - w0 - w1
                if w0 >= -1e-6 and w1 >= -1e-6 and w2 >= -1e-6:
                    img[j][i] = int(round(w0 * l0 + w1 * l1 + w2 * l2))
    return img


def read_imgs(text, cw, ch):
    res, cur, rows = {}, None, []
    for line in text.splitlines():
        if line.startswith("IMG "):
            cur = line.split()[1]; res[cur] = []
        elif cur is not None and line.strip():
            res[cur].append([int(v) for v in line.split()])
    return res


def check_core(work, cs, geom, im, cw, ch, verbose):
    """the CORE's rasteriser (pdrxgr.c, compiled with the module in one unit) against mutool"""
    exe = os.path.join(work, "pdxgr_test")
    srcs = [os.path.join(PD, "hosttest", "pdxgr_test.c"), os.path.join(PD, "pdmath.c"), os.path.join(PD, "hosttest", "pdhost.c")]
    subprocess.check_call(["cc", "-std=gnu99", "-O1", "-w", "-fno-strict-aliasing", "-DPD_HOST", "-I", PD, "-o", exe] + srcs)
    p = subprocess.run([exe, os.path.join(work, "shade.cases")], capture_output=True, text=True)
    if p.returncode:
        print(p.stdout, p.stderr)
        return 1
    imgs = read_imgs(p.stdout, cw, ch)
    bad = 0
    for name, _ in cs:
        left, top = geom[name]
        a = imgs.get(name)
        if not a:
            print("FAIL %-12s the rasteriser painted nothing" % name); bad += 1; continue
        tot = dn = 0; sd = 0
        for j in range(ch):
            for i in range(cw):
                d = abs(im.getpixel((left + i, top + j)) - a[j][i])
                tot += 1; sd += d; dn += d > 36
        rate = 100.0 * dn / tot
        lim = CORE_RATE.get(name, 3.0)
        ok = rate <= lim and sd / tot <= 10
        bad += 0 if ok else 1
        if verbose or not ok:
            print("%-4s core %-12s mean|d|=%5.2f  >36: %5.2f%% (limit %.1f%%)" % ("ok" if ok else "FAIL", name, sd / tot, rate, lim))
    print("pdfx_gfx core raster: %d cases, %d outside tolerance" % (len(cs), bad))
    return 1 if bad else 0


CORE_RATE = {"coons6b": 4.5, "tensor7": 4.5}      # curved edges in 4x4 chords, as SHADE_RATE


def check_shade(build_dir, work, exe, verbose):
    from PIL import Image
    pdf, cs, geom, (cw, ch, W, H) = write_shade(work)
    p = subprocess.run([exe, os.path.join(work, "shade.cases")], capture_output=True, text=True)
    if p.returncode:
        print(p.stdout, p.stderr)
        return 1
    mods = parse_shade_out(p.stdout)
    png = os.path.join(work, "shade.png")
    subprocess.check_call(["mutool", "draw", "-q", "-N", "-M", "0", "-r", "72", "-c", "gray", "-o", png, pdf],
                          stderr=subprocess.DEVNULL)
    im = Image.open(png).convert("L")
    bad = 0
    for name, _ in cs:
        sh = mods.get(name)
        if not sh or sh.get("failed"):
            print("FAIL %-12s the module failed" % name)
            bad += 1
            continue
        mine = ref_raster(sh, cw, ch)
        left, top = geom[name]
        tot = diff_n = 0
        sumd = 0
        for j in range(ch):
            for i in range(cw):
                r = im.getpixel((left + i, top + j))
                d = abs(r - mine[j][i])
                tot += 1; sumd += d
                if d > 28:
                    diff_n += 1
        rate = 100.0 * diff_n / tot
        ok = rate <= SHADE_RATE.get(name, 3.0) and sumd / tot <= 8
        bad += 0 if ok else 1
        if verbose or not ok:
            print("%-4s %-12s type %d tris %-3d mean|d|=%5.2f  >28: %5.2f%% (limit %.1f%%)" %
                  ("ok" if ok else "FAIL", name, sh["type"], len(sh["tris"]), sumd / tot, rate, SHADE_RATE.get(name, 3.0)))
    print("pdfx_gfx shading: %d cases, %d outside tolerance" % (len(cs), bad))
    bad += check_core(work, cs, geom, im, cw, ch, verbose)
    return 1 if bad else 0


# differing-pixel limits (percent of a 64x48 cell); the module's fan-out is a 8x8 grid or 4x4 patch cells
SHADE_RATE = {"coons6b": 4.5, "tensor7": 4.5}   # a curved edge cut into 4x4 chords: the boundary pixels differ from MuPDF's finer subdivision


def tables():
    def arr_(name, vals, per=10):
        s = "static const pdw %s[%d] = {\n" % (name, len(vals))
        for i in range(0, len(vals), per):
            s += "    " + ", ".join(("%du" if v > 32767 else "%d") % v for v in vals[i:i + per]) + ",\n"
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
