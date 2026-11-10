#!/usr/bin/env python3
"""pdx_page - PDREND + PDXF + the real PDENG draw each font fixture's pages, held to MuPDF
(PAGEDESK-SPEC 20.22.8). The whole interpretation-time path: PDREND's text operators find
the font's strike block, call PDXF through the REAL slot code (pdrxc.c) and pdr_test.c's
ladder, which "loads" the font module on PDE_NEEDMOD (hosttest/pdx_xmod.c: the door is
pdxf_verb in-process, re-INITed with the glyph claim as the primary does), PDXF reads the
program from the font dictionary, and the band walker draws the synthetic strike.

    python3 apps/pagedesk/hosttest/pdx_page.py [--build] [--shots DIR] [--oneslot [--noz]] [fixture ...]

--oneslot is the 640 KB VGA machine of QA R13 (PAGEDESK-SPEC 20.22.8, "Streams without a Flate slot"): each fixture's
content streams Flate-compressed (`mutool clean -z`) and PDENG given ONE Flate slot, so the
font program cannot be opened beside the page's content stream and PDXF inflates it itself.
--noz builds PDXF without that (-DPDXF_NOZ): the A/B, which must fail.

Per page it counts the pixels that differ between PDREND's 1:1 VGA render (thresholded)
and mutool's AA render at 50%, as a share of mutool's ink. Exits 1 above --limit.
"""
import argparse, json, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
PD = os.path.join(ROOT, "apps", "pagedesk")
FIX = os.path.join(ROOT, "tests", "pdfany", "fonts")
OUT = os.path.join(ROOT, "build", "pdxhost")
EXE = os.path.join(OUT, "pdr_test_xf")


def build(noz=False):
    os.makedirs(OUT, exist_ok=True)
    src = os.path.join(OUT, "src"); shutil.rmtree(src, ignore_errors=True); os.makedirs(src)
    for f in os.listdir(PD):
        if f.endswith((".c", ".h", ".inc")) and not f.startswith("pdx"):
            shutil.copy(os.path.join(PD, f), src)
    shutil.copy(os.path.join(PD, "pdabi.h"), src)
    hosttest = os.path.join(PD, "hosttest")
    cmd = ["cc", "-std=gnu99", "-O1", "-w", "-DPD_HOST", "-DPDR_REAL_ENG", "-DPDR_TYPE3"] + (["-DPDXF_NOZ"] if noz else []) + [
           "-I", src, "-I", PD, "-I", hosttest, "-o", EXE + ("_noz" if noz else ""),
           os.path.join(hosttest, "pdr_test.c"), os.path.join(hosttest, "pdr_host.c"), os.path.join(hosttest, "pdhost.c"),
           os.path.join(PD, "pdmath.c"), os.path.join(src, "pdrend.c"), os.path.join(hosttest, "pdx_hosteng.c"), os.path.join(PD, "pdeng.c"),
           os.path.join(PD, "pdxf.c"), os.path.join(hosttest, "pdx_xmod.c")]
    subprocess.check_call(cmd)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fixtures", nargs="*"); ap.add_argument("--build", action="store_true"); ap.add_argument("--shots")
    ap.add_argument("--limit", type=float, default=None)
    ap.add_argument("--oneslot", action="store_true"); ap.add_argument("--noz", action="store_true")
    a = ap.parse_args()
    from PIL import Image
    import numpy as np
    exe = EXE + ("_noz" if a.noz else "")
    if a.build or not os.path.exists(exe): build(a.noz)
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
        env = dict(os.environ, PDR_GLYPHKB="40")
        if a.oneslot:
            zt = tempfile.mkdtemp(); zp = os.path.join(zt, "z.pdf")
            subprocess.check_call(["mutool", "clean", "-z", pdf, zp], stdout=subprocess.DEVNULL)
            pdf = zp; env["PDRH_FLKB"] = "43"
        for pg, size in enumerate(j["sizes"], 1):
            W, H = j["page"]
            with tempfile.TemporaryDirectory() as t:
                ref = os.path.join(t, "r.png"); out = os.path.join(t, "o.ppm")
                subprocess.check_call(["mutool", "draw", "-q", "-r", "72", "-c", "gray", "-o", ref, pdf, str(pg)], stdout=subprocess.DEVNULL)
                r = subprocess.run([exe, "render", pdf, str(pg - 1), "100", "0", "4", "1/1", str(W), str(H), "0", "0", out, bank, "29"],
                                   capture_output=True, text=True, env=env)
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
