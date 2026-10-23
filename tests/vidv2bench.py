#!/usr/bin/env python3
"""WHAT A BLOCK COSTS on a 4.77 MHz 8088 - SPEC.md 98.3.23, and the
prices tools/os88vid.py's blk_cost hands the encoder.

    make && python3 tests/vidv2bench.py [--layout lin320|modex|vga4]
                                        [--machine NAME]

AN INSTRUMENT, and its one assertion is that it measured. A version 2 clip
is made here whose block frames each hold ONE kind, at two densities (40
and 280 blocks), placed at random as tests/vidv2.py places them; it plays
full screen on MartyPC's XT VGA with an exec breakpoint where the player
calls the part and one where the part has returned (vp_bpl for a planar
lead, vp_blin1 for LIN320's three lists), and each stop reads the frame it
belongs to. A breakpoint stops the guest and counts no cycle, so every span
is exact: the decode of that frame's block list and nothing else.

Per kind the two densities give a LINE: its slope is the cycles a block
costs, its intercept the call and the list's own. Those are printed beside
what blk_cost charges on the 8088's table today (os88vid's CYC_*), and the
ratio is what re-prices it. The machine is an 8088, so the 286's figures
are not here: the encoder carries a block's price to a profile through the
profile's own table terms, which is the assumption this measures against.
"""
import argparse
import os
import random
import subprocess
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88vid as vid, os88geom as geom  # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402
import vidv2                                                   # noqa: E402

DENS = (40, 280)


def kinds_of(lay):
    if lay in vid.ONEBIT_LAYOUTS:
        return ("tile",)
    if lay == vid.LAY_LIN320:
        return ("fill8", "two8", "eight8")
    if lay == vid.LAY_MODEX:
        return (0x20, 0x21, 0x22)
    return (vid.BLK_PFILL, vid.BLK_TWO)


def model_kind(lay, k):
    if lay == vid.LAY_LIN320 or lay in vid.ONEBIT_LAYOUTS:
        return k
    if lay == vid.LAY_MODEX:
        return vid.MODEX_LEADS[k]
    return vid.VGA4_LEADS[k]


def clip(tmp, lay):
    """frame 2i+1: kind i mod n at density i // n mod 2; even frames empty"""
    rnd = random.Random(77 + lay)
    pf = vid.PF_VGA4 if lay == vid.LAY_LIN80 else vid.PF_VGA8
    W, H = vidv2.W, vidv2.H
    g = vid.Geom(lay, W // vid.PIX_PER_BYTE[lay], H,
                 bitplanes=lay == vid.LAY_LIN80)
    g.v2 = True
    wr = vid.Writer(g, 1500, 100, vid.AUD_NONE, 0, pf, title="vidv2bench",
                    keysecs=60.0,
                    palette=vidv2.palette() if pf == vid.PF_VGA8 else None)
    wr.v2 = True
    surf = g.surface()
    ks = kinds_of(lay)
    plan = {}
    nf = 2 * len(ks) * len(DENS) * 2 + 1
    for f in range(nf):
        if not f % 2:
            wr.frame([] if g.planes == 1 else [], surf)
            continue
        i = f // 2
        k, n = ks[i % len(ks)], DENS[(i // len(ks)) % len(DENS)]
        vidv2.NBLK = n
        bl = [(a, p, kk) for a, p, kk in vidv2.blocks(rnd, g, lay)]
        bl = [(a, p, k) for a, p, kk in bl]     # one kind: payloads remade
        out = []
        for a, p, kk in bl:
            mk = model_kind(lay, k)
            ln = {"fill8": 1, "two8": 4, "eight8": 10, "mfill": 1,
                  "mtwo": 4, "meight": 10, "pfill": 2, "two": 5}[mk]
            pay = bytes(rnd.randrange(256) for _ in range(ln))
            if mk == "pfill":
                pay = bytes([pay[0], rnd.randrange(17)])
            out.append((a, pay, k))
        vidv2.put_blocks(surf, g, out, lay)
        ops = out if lay == vid.LAY_LIN320 else [(k, [(a, p, False)
                                                       for a, p, kk in out])]
        wr.frame(ops, surf)
        plan[f] = (k, n)
    path = os.path.join(tmp, "V2BENCH.V88")
    wr.write(path, 0)
    vid.verify_v88(path)
    return path, plan, nf


def clip1(tmp, lay):
    """ONE BIT: the whole codebook at frames 0 and 1 (the record after key
    0), timed in neither; then frame 2i+3 tiles at density i mod 2"""
    rnd = random.Random(77 + lay)
    W, H = vidv2.W, vidv2.H
    g = vid.Geom(lay, W // 8, H)
    g.v2 = True
    wr = vid.Writer(g, 1500, 100, vid.AUD_NONE, 0, vid.PF_MONO1,
                    title="vidv2bench", keysecs=60.0)
    wr.v2 = True
    surf = g.surface()
    cb = bytes(rnd.randrange(256) for _ in range(2048))
    tops = [g.base[y] + x for y in range(0, g.h - 3, 4)
            for x in range(0, g.wb - 1, 2)]
    plan = {}
    nf = 2 * 2 * len(DENS1) + 3
    for f in range(nf):
        if f < 2:
            wr.frame([(0, cb, "cbook")], surf)
            continue
        if not f % 2:
            wr.frame([], surf)
            continue
        n = DENS1[(f // 2 - 1) % len(DENS1)]
        ops = []
        for a in rnd.sample(tops, n):
            i = rnd.randrange(256)
            for r, ra in enumerate(vid.blk_rows(g, a)):
                surf[ra:ra + 2] = cb[i * 8 + 2 * r:i * 8 + 2 * r + 2]
            ops.append((a, bytes([i]), "tile"))
        wr.frame(ops, surf)
        plan[f] = ("tile", n)
    path = os.path.join(tmp, "V2BENCH.V88")
    wr.write(path, 0)
    vid.verify_v88(path)
    return path, plan, nf


DENS1 = (40, 200)                       # (one bit's canvas has 240 places)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--layout", choices=("lin320", "modex", "vga4", "cga",
                                         "herc"), default="lin320")
    a = ap.parse_args()
    lay = {"lin320": vid.LAY_LIN320, "modex": vid.LAY_MODEX,
           "vga4": vid.LAY_LIN80, "cga": vid.LAY_CGA,
           "herc": vid.LAY_HERC}[a.layout]
    one = lay in vid.ONEBIT_LAYOUTS
    global DENS
    if one:
        DENS = DENS1
        if a.machine == "os8088_xt_vga":
            a.machine = "os8088_5150_cga_gla" if lay == vid.LAY_CGA else \
                "os8088_5150_herc_gla"
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    if one:                             # (.one's far call is 5 bytes)
        ent, ret = syms["vp_blin1"], syms["vp_blin1.one"] + 5
    elif lay == vid.LAY_LIN320:
        ent, ret = syms["vp_blin1"], syms["vp_blin1.r"]
    else:
        ent, ret = syms["vp_bpl"], syms["vp_bpl.go"] + 5
    spans = {}
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88, plan, nf = clip1(tmp, lay) if one else clip(tmp, lay)
        disk = os.path.join(tmp, "vidv2b.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=a.machine) as ui:
            m = ui.m
            w = ui.path("B:/V2BENCH.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4

            def rw(n):
                return struct.unpack_from("<H", m.read(base + syms[n], 2))[0]

            os88marty.until(m, lambda mm: m.read(base + syms["vp_loaded"],
                                                 1)[0] == 1, "the header",
                            poll=0.3, limit=600.0, guest=30.0)
            m.write(base + syms["vp_nowin"], b"\1")
            m.write(base + syms["vp_played"], b"\0")
            with os88marty.bp_trace(m, base + ent, base + ret, cap=100000,
                                    on_hit=lambda mm, r: struct.unpack_from(
                                        "<H", mm.read(base + syms["vp_done"],
                                                      2))[0]) as tr:
                m.type_text("p")
                tr.until(lambda: m.read(base + syms["vp_played"], 1)[0] == 1,
                         "the play to end", 900)
            open_ = None
            for h in tr.hits:
                if h["addr"] == (base + ent) & 0xFFFFF:
                    open_ = h
                elif open_ is not None:
                    spans.setdefault(open_["hit"], []).append(
                        h["cycles"] - open_["cycles"])
                    open_ = None
    rows = {}
    for f, (k, n) in plan.items():
        c = spans.get(f)
        if not c:
            continue
        rows.setdefault(k, []).append((n, sum(c)))
    bad = []
    t8088 = vid.cyc_table() if hasattr(vid, "cyc_table") else None
    print("   kind      blocks  cycles    | per block (slope)  per list "
          "(intercept)  blk_cost on the 8088's table  ratio")
    for k in kinds_of(lay):
        pts = rows.get(k, [])
        if len({n for n, c in pts}) < 2:
            bad.append("kind %r: not measured at both densities" % (k,))
            continue
        lo = [c for n, c in pts if n == DENS[0]]
        hi = [c for n, c in pts if n == DENS[1]]
        clo, chi = sum(lo) / len(lo), sum(hi) / len(hi)
        slope = (chi - clo) / (DENS[1] - DENS[0])
        icpt = clo - slope * DENS[0]
        mk = model_kind(lay, k)
        model = vid.blk_cost(mk)
        print("   %-8s  %d/%d  %7.0f/%-7.0f| %8.1f          %8.0f            "
              "%8.1f                  %.2f" % (mk, DENS[0], DENS[1], clo, chi,
                                               slope, icpt, model,
                                               slope / model))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
