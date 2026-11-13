#!/usr/bin/env python3
"""pdx_check - PDXF's rasteriser against MuPDF (PAGEDESK-SPEC 20.22.8).

    python3 apps/pagedesk/hosttest/pdx_check.py [--thr N] [--build] [fixture ...]

For each fixture (tests/pdfany/fonts/<name>.json, made by tools/os88pdfx_fonts.py)
and each size, renders the page with `mutool draw` (AA, thresholded at 50% =
the 1-bit picture an ideal rasteriser would give, and -A 0 = MuPDF's own 1-bit)
and composes the same glyphs from `pdx_host -bin` at the same pen positions,
then counts differing pixels. Reports per size; exits 1 if a size's rate over
the AA reference is above --limit (the threshold the spec defends).
"""
import argparse, json, os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
FIX = os.path.join(ROOT, "tests", "pdfany", "fonts")
HOST = os.path.join(ROOT, "build", "pdx_host")

def build(thr=None):
    os.makedirs(os.path.dirname(HOST), exist_ok=True)
    cmd = ["cc", "-std=gnu99", "-O1", "-w", "-DPD_HOST", "-I", os.path.join(ROOT, "apps/pagedesk")]
    if thr: cmd.append("-DPDXF_THR=%d" % thr)
    cmd += ["-o", HOST, os.path.join(ROOT, "apps/pagedesk/hosttest/pdx_host.c"), os.path.join(ROOT, "apps/pagedesk/pdmath.c")]
    subprocess.check_call(cmd, stderr=subprocess.DEVNULL)

def read_pgm(path):
    d = open(path, "rb").read()
    parts = d.split(None, 4)
    assert parts[0] == b"P5"
    w, h = int(parts[1]), int(parts[2])
    return w, h, d[len(d) - w * h:]

def mutool(pdf, page, aa):
    with tempfile.TemporaryDirectory() as t:
        o = os.path.join(t, "o.pgm")
        cmd = ["mutool", "draw", "-r", "72", "-c", "gray", "-o", o]
        if aa is not None: cmd += ["-A", str(aa)]
        subprocess.check_call(cmd + [pdf, str(page)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return read_pgm(o)

def glyphs(program, size, asc, rows, gids):
    out = subprocess.check_output([HOST, "-bin", program, str(size), str(asc), str(rows)] + [str(g) for g in gids])
    res = {}; pos = 0
    for g in gids:
        nl = out.index(b"\n", pos)
        gid, w, lb, adv, nb = [int(x) for x in out[pos:nl].split()]
        pos = nl + 1
        data = b""
        if nb and nb != 0xFFFF:
            data = out[pos:pos + nb]; pos += nb + 1
        res[g] = (w, lb, adv, nb, data)
    return res

def compose(j, size, gl, asc, rows):
    W, H = j["page"]; px, py = j["pitch"]; mg = j["margin"]
    img = bytearray(W * H)       # 1 = ink
    per = (W - mg) // px
    miss = 0
    for i, g in enumerate(j["gids"]):
        w, lb, adv, nb, data = gl[g]
        if nb == 0xFFFF: miss += 1; continue
        if not nb: continue
        x0 = mg + (i % per) * px + lb; base = size + 6 + (i // per) * py; top = base - asc
        wb = (w + 7) // 8
        for r in range(rows):
            for c in range(w):
                if data[r * wb + c // 8] >> (7 - c % 8) & 1:
                    x, y = x0 + c, top + r
                    if 0 <= x < W and 0 <= y < H: img[y * W + x] = 1
    return img, miss

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fixtures", nargs="*")
    ap.add_argument("--thr", type=int, default=None, help="rebuild the host with PDXF_THR=N")
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--limit", type=float, default=None)
    a = ap.parse_args()
    if a.build or a.thr or not os.path.exists(HOST): build(a.thr)
    names = a.fixtures or sorted(f[:-5] for f in os.listdir(FIX) if f.endswith(".json"))
    worst = 0.0; tot_ref = tot_diff = 0
    for n in names:
        j = json.load(open(os.path.join(FIX, n + ".json")))
        if j.get("type3") or not j.get("program"): continue            # a Type 3 font has no program: pdx_page.py draws it
        pdf = os.path.join(FIX, j["pdf"]); prog = os.path.join(FIX, j["program"])
        for pg, size in enumerate(j["sizes"], 1):
            asc = size + 1; rows = asc + (size + 3) // 4 + 1
            gl = glyphs(prog, size, asc, rows, sorted(set(j["gids"])))
            mine, miss = compose(j, size, gl, asc, rows)
            W, H, aa = mutool(pdf, pg, None)
            _, _, a0 = mutool(pdf, pg, 0)
            ref = bytearray(1 if v < 128 else 0 for v in aa)
            ref0 = bytearray(1 if v < 128 else 0 for v in a0)
            ink = sum(ref); d = sum(1 for x, y in zip(mine, ref) if x != y); d0 = sum(1 for x, y in zip(mine, ref0) if x != y)
            rate0 = 100.0 * d0 / max(1, sum(ref0))
            # the strokes (QA R24): LOST = a pixel MuPDF covers at least a quarter with no ink of ours within one
            # pixel; STRAY = ink of ours with MuPDF covering under 1/8 everywhere within one pixel. A 1-bit
            # picture differs from AA@50% at every edge, which is no defect; these two are the defects
            def near(img, i, test):
                y, x = divmod(i, W)
                return any(test(img[yy * W + xx]) for yy in range(max(0, y - 1), min(H, y + 2)) for xx in range(max(0, x - 1), min(W, x + 2)))
            lost = sum(1 for i in range(W * H) if aa[i] < 192 and not near(mine, i, lambda v: v))
            stray = sum(1 for i in range(W * H) if mine[i] and not near(aa, i, lambda v: v < 224))
            rate = 100.0 * (lost + stray) / max(1, ink)
            print("%-14s %2dpx  ink %5d  lost %4d stray %4d (%5.1f%% of ink)  diff %5d vs AA@50%%, %5d (%5.1f%%) vs -A 0   refused %d" % (n, size, ink, lost, stray, rate, d, d0, rate0, miss))
            tot_ref += ink; tot_diff += lost + stray; worst = max(worst, rate)
    print("total: %d lost or stray of %d ink pixels (%.1f%%); worst size %.1f%%" % (tot_diff, tot_ref, 100.0 * tot_diff / max(1, tot_ref), worst))
    if a.limit is not None and worst > a.limit: return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
