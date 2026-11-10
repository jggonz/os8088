#!/usr/bin/env python3
"""pdx_page - PDREND + PDXF + the real PDENG draw each font fixture's pages, held to MuPDF
(PAGEDESK-SPEC 20.22.8). The whole interpretation-time path: PDREND's text operators find
the font's strike block, call PDXF (here in-process: a shim pdrxc.c answers x_have with 1 and
x_call with pdxf_verb), PDXF reads the program from the font dictionary, and the band
walker draws the synthetic strike.

    python3 apps/pagedesk/hosttest/pdx_page.py [--build] [--shots DIR] [fixture ...]

Per page it counts the pixels that differ between PDREND's 1:1 VGA render (thresholded)
and mutool's AA render at 50%, as a share of mutool's ink. Exits 1 above --limit.
"""
import argparse, json, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
PD = os.path.join(ROOT, "apps", "pagedesk")
FIX = os.path.join(ROOT, "tests", "pdfany", "fonts")
OUT = os.path.join(ROOT, "build", "pdxhost")
EXE = os.path.join(OUT, "pdr_test_xf")

SHIM = r'''/* the host shim of pdrxc.c (hosttest/pdx_page.py): PDXF is always resident and in-process */
#define XCK_CS 1
#define XCK_FN 2
#define XCK_SHADE 3
#define XCK_PAT 4
#define XCK_LUT 5
#define XCK_FONT 6
#define XCK_CMAP 7
#define XCK_GS 8
extern pdw pdxf_verb(pdw verb, pdw bx, pdw cx, pdw dx);
extern pdw pdx_cf;
static pdw x_res = 3, x_door_off, x_door_seg, x_pend, x_cf, x_dx, g_xcseg;
static pdw g_xb;
static pdw g_xinit;
static int x_have(pdw id) { return id == 3; }
static void x_need(pdw id) { x_pend |= (pdw)(1u << id); }
static pdw x_call(pdw verb, pdw bx, pdw cx, pdw dx)
{
    pdw r;
    if (!g_xinit) {
        static struct pdxb_init in;
        g_xinit = 1;
        pd_fcopy_in(g_xb, 0, &in, 0);          /* nothing: keep the caller's block */
        pdr_zero(&in, sizeof in);
        in.xb_seg = g_xb; in.slot_seg = 0xB000; in.slot_kb = 48;
        { static pdb keep[400]; pd_fcopy_in(g_xb, 0, keep, sizeof keep); pd_fcopy_out(&in, g_xb, 0, sizeof in);
          pdxf_verb(PDV_X_INIT, g_xb, 0, 0); pd_fcopy_out(keep, g_xb, 0, sizeof keep); }
    }
    r = pdxf_verb(verb, bx, cx, dx); x_cf = pdx_cf; x_dx = 0;
    return r;
}
static pdw xc_find(pdw kind, pdw k0, pdw k1) { (void)kind; (void)k0; (void)k1; return 0; }
static pdw xc_alloc(pdw kind, pdw k0, pdw k1, pdw bytes) { (void)kind; (void)k0; (void)k1; (void)bytes; return 0; }
'''

def build():
    os.makedirs(OUT, exist_ok=True)
    src = os.path.join(OUT, "src"); shutil.rmtree(src, ignore_errors=True); os.makedirs(src)
    for f in os.listdir(PD):
        if f.endswith((".c", ".h", ".inc")) and not f.startswith("pdx"):
            shutil.copy(os.path.join(PD, f), src)
    shutil.copy(os.path.join(PD, "pdabi.h"), src)
    open(os.path.join(src, "pdrxc.c"), "w").write(SHIM)
    hosttest = os.path.join(PD, "hosttest")
    cmd = ["cc", "-std=gnu99", "-O1", "-w", "-DPD_HOST", "-DPDR_REAL_ENG", "-I", src, "-I", PD, "-I", hosttest,
           "-o", EXE,
           os.path.join(hosttest, "pdr_test.c"), os.path.join(hosttest, "pdr_host.c"), os.path.join(hosttest, "pdhost.c"),
           os.path.join(PD, "pdmath.c"), os.path.join(src, "pdrend.c"), os.path.join(hosttest, "pdx_hosteng.c"), os.path.join(PD, "pdeng.c"),
           os.path.join(PD, "pdxf.c")]
    subprocess.check_call(cmd)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fixtures", nargs="*"); ap.add_argument("--build", action="store_true"); ap.add_argument("--shots")
    ap.add_argument("--limit", type=float, default=None)
    a = ap.parse_args()
    from PIL import Image
    import numpy as np
    if a.build or not os.path.exists(EXE): build()
    bank = os.path.join(OUT, "bank"); os.makedirs(bank, exist_ok=True)
    res = open(os.path.join(ROOT, "build", "PDRES.BIN"), "rb").read()
    open(os.path.join(bank, "PDRES.BIN"), "wb").write(res)
    open(os.path.join(bank, "PAGEDESK.O88"), "wb").write(b"\x55" * 1536 + res)
    shutil.copy(os.path.join(ROOT, "build", "PDFONTX.RES"), bank)
    names = a.fixtures or sorted(f[:-5] for f in os.listdir(FIX) if f.endswith(".json"))
    worst = 0.0; tot = totd = 0; bad = 0
    for n in names:
        j = json.load(open(os.path.join(FIX, n + ".json")))
        pdf = os.path.join(FIX, j["pdf"])
        for pg, size in enumerate(j["sizes"], 1):
            W, H = j["page"]
            with tempfile.TemporaryDirectory() as t:
                ref = os.path.join(t, "r.png"); out = os.path.join(t, "o.ppm")
                subprocess.check_call(["mutool", "draw", "-q", "-r", "72", "-c", "gray", "-o", ref, pdf, str(pg)], stdout=subprocess.DEVNULL)
                r = subprocess.run([EXE, "render", pdf, str(pg - 1), "100", "0", "4", "1/1", str(W), str(H), "0", "0", out, bank, "29"],
                                   capture_output=True, text=True, env=dict(os.environ, PDR_GLYPHKB="40"))
                if r.returncode:
                    print("%-14s %2dpx  RENDER FAILED rc=%d %s" % (n, size, r.returncode, r.stderr.strip()[:120])); bad += 1; continue
                st = dict(kv.split("=", 1) for l in r.stdout.split("\n") if l.startswith("status") for kv in l.split()[1:] if "=" in kv)
                mine = np.asarray(Image.open(out).convert("L"), dtype=float)[:H, :W] < 128
                refa = np.asarray(Image.open(ref).convert("L"), dtype=float)[:H, :W] < 128
                if a.shots:
                    os.makedirs(a.shots, exist_ok=True)
                    both = np.concatenate([np.asarray(Image.open(out).convert("L"))[:H, :W], np.asarray(Image.open(ref).convert("L"))[:H, :W]], axis=1)
                    Image.fromarray(both).save(os.path.join(a.shots, "%s-%d.png" % (n, size)))
            ink = int(refa.sum()); d = int((mine != refa).sum()); rate = 100.0 * d / max(1, ink)
            print("%-14s %2dpx  ink %5d  diff %5d (%5.1f%%)  inc=%s first=%s" % (n, size, ink, d, rate, st.get("incomplete", "?"), st.get("first", "").strip("'")[:24]))
            tot += ink; totd += d; worst = max(worst, rate)
    print("total: %d differing of %d ink pixels (%.1f%%); worst page %.1f%%" % (totd, tot, 100.0 * totd / max(1, tot), worst))
    return 1 if bad or (a.limit is not None and worst > a.limit) else 0

if __name__ == "__main__":
    sys.exit(main())
