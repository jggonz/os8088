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
--text checks Find and Copy where the font module cannot be had (QA R17): each page's text
with PDXF, then the text with PDXF refused and every slice the ladder answered sent again
stale (as the primary does: it must finish, not ladder forever), then the text right after
drawing the page with PDXF refused, which the strike blocks carry and must equal the first.
--docs renders every fixture's page 1 one file after another in one process (QA R24 on VGA: a strike
block of the last file's font matched the next file's object numbers); each must equal its render alone.
--small runs each fixture with one Flate slot and a 32 KiB workspace (QA R13's QZTEXT scatter): grown
to its want it must draw as at 48 KiB; refused, it draws the same or notes the font, never a
truncated program silently.
--grow checks the glyph claim's want (QA R24): a page with five embedded fonts needs five strike blocks;
from a 40 KiB claim (four) it must grow to 48 and draw as a 48 KiB claim does, and a refused growth must
finish with "glyph block full" noted, never ladder forever.
--nopr draws each fixture page with every font program unusable (QA R13: MUHTML, LICJA, LICZH were
BLANK when their CID programs did not fit): PDXF still reads the font's tables, so the page must draw its
text in the substitute face - ink on every page whose text map is not empty - with "font substituted"
noted, and its text map must equal the one with the programs.
--seq renders every page of each fixture and then page 1 again in ONE process (PDR_SEQ): the
glyph claim and its strike blocks carry over from page to page as they do on the machine
(QA R24), so no page may say "glyph block full" and the second page 1 must equal the first.

Per page it counts LOST pixels (mutool's AA render covers them at least a quarter, no ink of
ours within one pixel) and STRAY ones (ours, mutool under 1/8 everywhere within one pixel), as
pdx_check.py does, as a share of mutool's ink at 50%; the plain difference from AA@50% is printed
beside it. Exits 1 above --limit. (The Type 3 fixture's small pages show LOST at their filled
shapes' anti-aliased edges: PDREND's path filler, not a glyph rasteriser.)
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
    ap.add_argument("--seq", action="store_true"); ap.add_argument("--text", action="store_true")
    ap.add_argument("--grow", action="store_true")
    ap.add_argument("--small", action="store_true")
    ap.add_argument("--docs", action="store_true")
    ap.add_argument("--nopr", action="store_true")
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
    if a.docs:
        # QA R24's VGA "glyph block full": every fixture's page 1, one file after another in ONE process,
        # as a session opens them. Object numbers repeat across files, so a strike block left by the last
        # file matched the next one's (font, size) - its glyphs, or its full keys. Each render must equal
        # the same page rendered alone
        # Twice: with a 40 KiB claim from the start (the INIT clears the header), and as the machine runs it -
        # a 24 KiB claim GROWN to 40 for each file's first block, into memory the last file's grown claim
        # left, header and all (the primary's heap hands the freed claim back; QA R24 round 2)
        W, H = 600, 800
        bad = 0
        for how, genv in (("40 KiB", {"PDR_GLYPHKB": "40"}), ("grown", {"PDR_GLYPHKB": "24", "PDR_GLYPHGROW": "1", "PDR_GLYREUSE": "1"})):
            with tempfile.TemporaryDirectory() as t:
                seq = " ".join("%s:0" % os.path.join(FIX, json.load(open(os.path.join(FIX, n + ".json")))["pdf"]) for n in names)
                ob = os.path.join(t, "s")
                r = subprocess.run([exe, "render", "x", "0", "100", "0", "4", "1/1", str(W), str(H), "0", "0", ob, bank, "29"],
                                   capture_output=True, text=True, env=dict(os.environ, PDR_SEQ=seq, **genv))
                incs = [int(l.split("incomplete=")[1].split()[0], 16) for l in r.stdout.split("\n") if "incomplete=" in l]
                for k, n in enumerate(names):
                    one = os.path.join(t, "one.ppm")
                    subprocess.run([exe, "render", os.path.join(FIX, json.load(open(os.path.join(FIX, n + ".json")))["pdf"]), "0", "100", "0", "4", "1/1",
                                    str(W), str(H), "0", "0", one, bank, "29"], capture_output=True, env=dict(os.environ, **genv))
                    same = os.path.exists(ob + ".%d.ppm" % k) and open(one, "rb").read() == open(ob + ".%d.ppm" % k, "rb").read()
                    full = k < len(incs) and incs[k] & 0x0080
                    ok = same and not full
                    print("%-14s %-6s after %d other files: %s%s  %s" % (n, how, k, "as alone" if same else "DIFFERS",
                          ", glyph block full" if full else "", "ok" if ok else "FAIL"))
                    bad += not ok
        return 1 if bad else 0
    if a.nopr:
        bad = 0
        for n in names:
            j = json.load(open(os.path.join(FIX, n + ".json")))
            pdf = os.path.join(FIX, j["pdf"]); W, H = j["page"]
            for pg, size in enumerate(j["sizes"]):
                env = dict(os.environ, PDR_GLYPHKB="40")
                ref = subprocess.run([exe, "text", pdf, str(pg), bank], capture_output=True, env=env)
                txt = subprocess.run([exe, "text", pdf, str(pg), bank], capture_output=True, env=dict(env, PDX_NOPROG="1"))
                with tempfile.TemporaryDirectory() as t:
                    ob = os.path.join(t, "o.ppm")
                    r = subprocess.run([exe, "render", pdf, str(pg), "100", "0", "4", "1/1", str(W), str(H), "0", "0", ob, bank, "29"],
                                       capture_output=True, text=True, env=dict(env, PDX_NOPROG="1"))
                    ink = int((np.asarray(Image.open(ob).convert("L"))[:H, :W] < 128).sum()) if os.path.exists(ob) else -1
                inc = [int(l.split("incomplete=")[1].split()[0], 16) for l in r.stdout.split("\n") if "incomplete=" in l]
                words = sum(c > 32 for c in ref.stdout)
                ok = (r.returncode == 0 and inc and (inc[0] & 0x0401 or n == "type3") and txt.returncode == 0 and txt.stdout == ref.stdout
                      and (ink > 0 or not words))
                print("%-14s p%d  no programs: ink %5d for %4d chars of text, inc=0x%04x, text map %s  %s" % (
                    n, pg + 1, ink, words, inc[0] if inc else 0, "same" if txt.stdout == ref.stdout else "DIFFERS", "ok" if ok else "FAIL"))
                bad += not ok
        return 1 if bad else 0
    if a.small:
        # QA R13 residue (the QZTEXT scatter): ONE Flate slot and a 32 KiB workspace, whose 22,016-byte
        # program area is smaller than t1-hex's program. The in-place inflate runs out of room; that must
        # read as "too large" (PDXF asks for its want, else the font is substituted and noted), never as a
        # truncated program drawn with no note
        bad = 0
        for n in names:
            j = json.load(open(os.path.join(FIX, n + ".json")))
            W, H = j["page"]
            with tempfile.TemporaryDirectory() as t:
                zp = os.path.join(t, "z.pdf")
                subprocess.check_call(["mutool", "clean", "-z", os.path.join(FIX, j["pdf"]), zp], stdout=subprocess.DEVNULL)
                def run(extra, name):
                    ob = os.path.join(t, name + ".ppm")
                    r = subprocess.run([exe, "render", zp, "0", "100", "0", "4", "1/1", str(W), str(H), "0", "0", ob, bank, "29"],
                                       capture_output=True, text=True, env=dict(os.environ, PDR_GLYPHKB="40", PDRH_FLKB="43", **extra))
                    inc = [int(l.split("incomplete=")[1].split()[0], 16) for l in r.stdout.split("\n") if "incomplete=" in l]
                    return r.returncode, inc[0] if inc else None, open(ob, "rb").read() if os.path.exists(ob) else None
                A = run({}, "a"); B = run({"PDX_SLOTKB": "32", "PDX_SLOTGROW": "1"}, "b"); C = run({"PDX_SLOTKB": "32"}, "c")
            noted = C[1] is not None and C[1] & 0x0401
            ok = A[0] == 0 and B[2] == A[2] and C[0] == 0 and (C[2] == A[2] or noted)
            print("%-14s 32 KiB grown: %s   32 KiB refused: %s  %s" % (n, "same" if B[2] == A[2] else "DIFFERS",
                  "same" if C[2] == A[2] else "substituted, noted 0x%04x" % C[1] if noted else "DIFFERS, NOT NOTED", "ok" if ok else "FAIL"))
            bad += not ok
        return 1 if bad else 0
    if a.grow:
        # QA R24: five embedded fonts on ONE page (qpdf overlays four fixtures' page 1 on tt-simple's) need
        # five strike blocks; the glyph claim's min (40 KiB) holds four, its want (48) six
        with tempfile.TemporaryDirectory() as t:
            five = os.path.join(t, "five.pdf")
            cmd = ["qpdf", os.path.join(FIX, "tt-simple.pdf"), "--pages", os.path.join(FIX, "tt-simple.pdf"), "1", "--"]
            for o in ("t1-simple", "cff-simple", "tt-cid", "cff-cid"):
                cmd += ["--overlay", os.path.join(FIX, o + ".pdf"), "--from=1", "--to=1", "--"]
            subprocess.check_call(cmd + [five])
            W, H = json.load(open(os.path.join(FIX, "tt-simple.json")))["page"]
            def run(extra, name):
                ob = os.path.join(t, name + ".ppm")
                r = subprocess.run([exe, "render", five, "0", "100", "0", "4", "1/1", str(W), str(H), "0", "0", ob, bank, "29"],
                                   capture_output=True, text=True, env=dict(os.environ, **extra))
                inc = [l.split("incomplete=")[1].split()[0] for l in r.stdout.split("\n") if "incomplete=" in l]
                return r.returncode, int(inc[0], 16) if inc else None, open(ob, "rb").read() if os.path.exists(ob) else None
            rg = run({"PDR_GLYPHKB": "40", "PDR_GLYPHGROW": "1"}, "grow")
            rr = run({"PDR_GLYPHKB": "40", "PDR_GLYPHGROW": "1", "PDR_GLYPHMAX": "40"}, "refused")
            r48 = run({"PDR_GLYPHKB": "48"}, "at48")
            r40 = run({"PDR_GLYPHKB": "40"}, "at40")
        ok = (rg[0] == 0 and rg[1] is not None and not rg[1] & 0x0080 and rg[2] == r48[2]
              and rr[0] == 0 and rr[1] is not None and rr[1] & 0x0080 and r40[1] is not None and r40[1] & 0x0080)
        print("five fonts: grown 40->48 inc=%s %s the 48 KiB render; refused at 40 inc=%s (block full %s); plain 40 inc=%s  %s" % (
            hex(rg[1] or 0), "equals" if rg[2] == r48[2] else "DIFFERS from", hex(rr[1] or 0), "noted" if (rr[1] or 0) & 0x80 else "NOT NOTED",
            hex(r40[1] or 0), "ok" if ok else "FAIL"))
        return 0 if ok else 1
    if a.text:
        bad = 0
        for n in names:
            j = json.load(open(os.path.join(FIX, n + ".json")))
            pdf = os.path.join(FIX, j["pdf"]); W, H = j["page"]
            # every page in one process, PDXF refused, as a Find job runs: ONE ladder answer for the job,
            # not one per page (PDU_LADMAX is 16 a job: "exceeds one of PageDesk's limits")
            al = subprocess.run([exe, "text", pdf, "all", bank], capture_output=True,
                                env=dict(os.environ, PDR_GLYPHKB="40", PDR_NOSLOT="3", PDR_TMSTALE="1"))
            loads = [int(l.split(b"=")[1]) for l in al.stderr.split(b"\n") if l.startswith(b"textmap: loads=")]
            okj = al.returncode == 0 and loads and loads[0] <= 1
            print("%-14s all pages, PDXF refused: %s answers  %s" % (n, loads[0] if loads else "?", "ok" if okj else "FAIL"))
            bad += not okj
            for pg, size in enumerate(j["sizes"]):
                env = dict(os.environ, PDR_GLYPHKB="40")
                ref = subprocess.run([exe, "text", pdf, str(pg), bank], capture_output=True, env=env)
                st = subprocess.run([exe, "text", pdf, str(pg), bank], capture_output=True, env=dict(env, PDR_NOSLOT="3", PDR_TMSTALE="1"))
                with tempfile.TemporaryDirectory() as t:
                    tf = os.path.join(t, "t.txt")
                    r = subprocess.run([exe, "render", pdf, str(pg), "100", "0", "4", "1/1", str(W), str(H), "0", "0", os.path.join(t, "o.ppm"), bank, "29"],
                                       capture_output=True, env=dict(env, PDR_TEXTAFTER=tf))
                    after = open(tf, "rb").read() if r.returncode == 0 and os.path.exists(tf) else None
                unread = (b"UNREAD" in ref.stderr, b"UNREAD" in st.stderr, b"UNREAD" in r.stderr)
                # cff-cid-cmap has no /ToUnicode: no text (MuPDF: U+FFFD). UNREAD (pdxb_textmap.done's PDTM_UNREAD) is said
                # exactly when PDXF was refused and the view had drawn nothing: Find then names the page to draw
                ok = ref.returncode == 0 and st.returncode == 0 and after == ref.stdout and unread == (False, True, False)
                print("%-14s p%d %2dpx  text %d chars  refused+stale rc=%d  after a draw %s  %s" % (
                    n, pg + 1, size, len(ref.stdout.strip()), st.returncode,
                    "same" if after == ref.stdout else "rc=%d" % r.returncode if after is None else "DIFFERS", "ok" if ok else "FAIL unread=%s" % (unread,)))
                if not ok and after is not None and after != ref.stdout: print("   ref  %r\n   got  %r" % (ref.stdout[:80], after[:80]))
                bad += not ok
        return 1 if bad else 0
    if a.seq:
        bad = 0
        for n in names:
            j = json.load(open(os.path.join(FIX, n + ".json")))
            pdf = os.path.join(FIX, j["pdf"]); W, H = j["page"]; np_ = len(j["sizes"])
            seq = " ".join("%s:%d" % (pdf, k) for k in list(range(np_)) + [0])
            with tempfile.TemporaryDirectory() as t:
                ob = os.path.join(t, "s")
                r = subprocess.run([exe, "render", "x", "0", "100", "0", "4", "1/1", str(W), str(H), "0", "0", ob, bank, "29"],
                                   capture_output=True, text=True, env=dict(os.environ, PDR_GLYPHKB="40", PDR_SEQ=seq))
                incs = [l.split("incomplete=")[1].split()[0] for l in r.stdout.split("\n") if "incomplete=" in l]
                same = os.path.exists(ob + ".0.ppm") and open(ob + ".0.ppm", "rb").read() == open(ob + ".%d.ppm" % np_, "rb").read()
            ok = r.returncode == 0 and len(incs) == np_ + 1 and all(int(x, 16) & 0x0080 == 0 for x in incs) and same
            print("%-14s seq %d renders  inc=%s  page 1 again %s  %s" % (n, len(incs), ",".join(incs), "identical" if same else "DIFFERS", "ok" if ok else "FAIL"))
            bad += not ok
        return 1 if bad else 0
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
                refg = np.asarray(Image.open(ref).convert("L"), dtype=float)[:H, :W]
                refa = refg < 128
                if a.shots:
                    os.makedirs(a.shots, exist_ok=True)
                    both = np.concatenate([np.asarray(Image.open(out).convert("L"))[:H, :W], np.asarray(Image.open(ref).convert("L"))[:H, :W]], axis=1)
                    Image.fromarray(both).save(os.path.join(a.shots, "%s-%d.png" % (n, size)))
            # LOST / STRAY as pdx_check.py counts them (the dropout control's measure, QA R24)
            def dil(m):
                p = np.pad(m, 1); return np.max([p[1 + dy:1 + dy + m.shape[0], 1 + dx:1 + dx + m.shape[1]] for dy in (-1, 0, 1) for dx in (-1, 0, 1)], axis=0)
            lost = int(((refg < 192) & ~dil(mine)).sum()); stray = int((mine & ~dil(refg < 224)).sum())
            ink = int(refa.sum()); d = int((mine != refa).sum()); rate = 100.0 * (lost + stray) / max(1, ink)
            print("%-14s %2dpx  ink %5d  lost %4d stray %4d (%5.1f%%)  diff %5d vs AA@50%%  inc=%s first=%s" % (n, size, ink, lost, stray, rate, d, st.get("incomplete", "?"), st.get("first", "").strip("'")[:24]))
            d = lost + stray
            tot += ink; totd += d; worst = max(worst, rate)
    print("total: %d lost or stray of %d ink pixels (%.1f%%); worst page %.1f%%" % (totd, tot, 100.0 * totd / max(1, tot), worst))
    return 1 if bad or (a.limit is not None and worst > a.limit) else 0

if __name__ == "__main__":
    sys.exit(main())
