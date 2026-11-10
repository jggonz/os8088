#!/usr/bin/env python3
"""pdx_chain - PDXF's whole chain through the REAL PDENG against each fixture's sidecar
(PAGEDESK-SPEC 20.22.8): the font program read from its (Flate) stream, the encoding rules
(code -> glyph index), the CIDToGIDMap window, the ToUnicode table, an embedded Encoding CMap
and /W. Prints one line per fixture and exits 1 on any mismatch."""
import json, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
FIX = os.path.join(ROOT, "tests", "pdfany", "fonts")
BIN = os.path.join(ROOT, "build", "pdx_real")

def build():
    os.makedirs(os.path.dirname(BIN), exist_ok=True)
    pd = os.path.join(ROOT, "apps", "pagedesk")
    subprocess.check_call(["cc", "-std=gnu99", "-O1", "-w", "-DPD_HOST", "-I", pd, "-I", os.path.join(pd, "hosttest"), "-I", ROOT, "-o", BIN,
                           os.path.join(pd, "hosttest", "pdx_real.c"), os.path.join(pd, "pdmath.c"), os.path.join(pd, "pdeng.c")])

def main():
    if "--build" in sys.argv or not os.path.exists(BIN): build()
    bad = 0
    names = [a for a in sys.argv[1:] if not a.startswith("-")] or sorted(f[:-5] for f in os.listdir(FIX) if f.endswith(".json"))
    for n in names:
        j = json.load(open(os.path.join(FIX, n + ".json")))
        out = subprocess.run([BIN, os.path.join(FIX, j["pdf"]), str(j["nbytes"])] + [str(c) for c in j["codes"]], capture_output=True, text=True)
        lines = out.stdout.split("\n")
        gid = {}; uni = {}; wid = {}; cmp_ = {}; head = lines[0] if lines else ""
        for l in lines:
            p = l.split()
            if len(p) == 3 and p[0] == "GID": gid[int(p[1])] = int(p[2])
            elif len(p) == 3 and p[0] == "UNI": uni[int(p[1])] = int(p[2])
            elif len(p) == 3 and p[0] == "WID": wid[int(p[1])] = int(p[2])
            elif len(p) == 3 and p[0] == "CIDMAP": cmp_[int(p[1])] = int(p[2])
        errs = []
        if out.returncode: errs.append("harness rc %d: %s" % (out.returncode, (out.stdout + out.stderr)[:200]))
        isc = "cmap" in n
        for c, g in zip(j["codes"], j["gids"]):
            if gid.get(c) != g: errs.append("gid(%d)=%s want %d" % (c, gid.get(c), g))
        for c, u in j.get("unicode", {}).items():
            if uni.get(int(c)) != u: errs.append("uni(%s)=%s want %d" % (c, uni.get(int(c)), u))
        for c, w in j.get("widths", {}).items():
            if wid.get(int(c)) != w: errs.append("w(%s)=%s want %d" % (c, wid.get(int(c)), w))
        for c, g in j.get("cidmap", {}).items():
            if cmp_.get(int(c)) != g: errs.append("cmap(%s)=%s want %d" % (c, cmp_.get(int(c)), g))
        # the strike block at 12 px against a direct raster of the same gids
        px = 12; asc = px + 1; rows = px + 1 + (px + 3) // 4 + 1
        env = dict(os.environ, PDX_PX=str(px))
        blk = subprocess.run([BIN, os.path.join(FIX, j["pdf"]), str(j["nbytes"])] + [str(c) for c in j["codes"]], capture_output=True, text=True, env=env).stdout
        hostbin = os.path.join(ROOT, "build", "pdx_host")
        if os.path.exists(hostbin):
            gl = {}
            out2 = subprocess.check_output([hostbin, "-bin", os.path.join(FIX, j["program"]), str(px), str(asc), str(rows)] + [str(g) for g in sorted(set(j["gids"]))])
            pos = 0
            for g in sorted(set(j["gids"])):
                nl = out2.index(b"\n", pos); gid_, w, lb, adv, nb = [int(x) for x in out2[pos:nl].split()]; pos = nl + 1
                data = b""
                if nb and nb != 0xFFFF: data = out2[pos:pos + nb]; pos += nb + 1
                gl[g] = (w, lb, nb, data)
            nblk = 0; seen = set()
            for l in blk.split("\n"):
                p = l.split()
                if len(p) < 3 or p[0] != "BLK": continue
                code = int(p[1])
                if p[2] == "none": errs.append("block: no slot for %d" % code); continue
                g = dict(zip(j["codes"], j["gids"]))[code]
                w, lb, nb, data = gl[g]
                tw = int(p[5]); tlb = int(p[7]); bits = bytes.fromhex(p[16]) if len(p) > 16 else b""
                if tw != w or (w and tlb != lb) or (w and bits != data): errs.append("block glyph %d differs from the direct raster" % code)
                if int(p[3]) in seen: errs.append("block slot %s twice" % p[3])
                seen.add(int(p[3])); nblk += 1
            if nblk != len(j["codes"]): errs.append("block: %d of %d slots" % (nblk, len(j["codes"])))
        print("%-14s %s  %s" % (n, "ok " if not errs else "BAD", head if not errs else "; ".join(errs[:4])))
        bad += bool(errs)
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
