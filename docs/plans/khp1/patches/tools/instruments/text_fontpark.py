#!/usr/bin/env python3
"""fontpark - cycle-exact per-call cost of kernel/font.inc's routines, and the
pixels they leave, on a booted os8088 under MartyPC (os88linecost's park rig).

    python3 fontpark.py --tree . --image build/os8088-360.img \
        --syms syms.json --machine os8088_5150_cga_gla --out out.json

Every case draws in a band of its own, three times (the median is reported),
and the whole framebuffer is captured at the end, so two kernels are compared
on cycles per case AND on the final picture byte for byte.
"""
import argparse, hashlib, json, os, sys

KERNEL_SEG = 0x60
CBLACK, CWHITE, CLGRAY, CDGRAY = 0, 15, 7, 8

TXT10 = b"C-2 01 A0F"
TXT20 = b"The quick brown fox!"
PAD20 = b"Hello" + b" " * 15
TXT78 = (b"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
         b"eiusmod tempor ")[:78]
TITLE = b"Note Pad - README"
WIDE = (b"x" * 40)


def cases(wide):
    """(name, routine, string, x, y, regs-extra, pokes)."""
    out = []
    y = 8
    def add(name, routine, s, x, al=None, ah=None, color=None, dis=0,
            rows=8, at=None, clip=None):
        nonlocal y
        out.append(dict(name=name, routine=routine, s=s, x=x,
                        y=y if at is None else at, al=al,
                        ah=ah, color=color, dis=dis, clip=clip))
        if at is None:
            y += rows
    add("run10 aligned b/w", "font_run_x", TXT10, 64, CBLACK, CWHITE)
    add("run10 skew5 b/w", "font_run_x", TXT10, 69, CBLACK, CWHITE)
    add("run10 skew1 b/w", "font_run_x", TXT10, 65, CBLACK, CWHITE)
    add("run10 aligned w/b", "font_run_x", TXT10, 64, CWHITE, CBLACK)
    add("run20 text aligned", "font_run_x", TXT20, 64, CBLACK, CWHITE)
    add("run20 padded aligned", "font_run_x", PAD20, 64, CBLACK, CWHITE)
    add("run78 aligned", "font_run_x", TXT78, 0, CBLACK, CWHITE)
    add("run78 skew5", "font_run_x", TXT78[:77], 5, CBLACK, CWHITE)
    add("run10 disabled", "font_run_x", TXT10, 64, CDGRAY, CWHITE, dis=1)
    add("run10 blk/lgray", "font_run_x", TXT10, 64, CBLACK, CLGRAY)
    add("run10 dgray/lgray", "font_run_x", TXT10, 64, CDGRAY, CLGRAY)
    add("run1 aligned", "font_run_x", b"A", 64, CBLACK, CWHITE)
    add("run1 skew3", "font_run_x", b"A", 67, CBLACK, CWHITE)
    add("run10 offright", "font_run_x", TXT10, wide - 40, CBLACK, CWHITE)
    add("char W aligned", "font_char", b"W", 64, color=CBLACK)
    add("char W skew5", "font_char", b"W", 69, color=CBLACK)
    add("char W white", "font_char", b"W", 64, color=CWHITE)
    add("str10 aligned", "font_str_x", TXT10, 64, color=CBLACK)
    add("str10 skew5", "font_str_x", TXT10, 69, color=CBLACK)
    add("str17 title skew3", "font_str_x", TITLE, 203, color=CBLACK)
    add("str10 white skew2", "font_str_x", TXT10, 66, color=CWHITE)
    add("str10 disabled", "font_str_x", TXT10, 64, color=CDGRAY, dis=1)
    add("str10 offright", "font_str_x", TXT10, wide - 40, color=CBLACK)
    add("width40", "font_width_x", WIDE, 0, rows=0)
    X = 400
    for i, (nm, clip) in enumerate([
            ("whole", lambda x, y: [(0, 0, 719, 347)]),
            ("hcut", lambda x, y: [(0, 0, 719, y + 3), (0, y + 4, 719, 347)]),
            ("vcut", lambda x, y: [(0, 0, x + 37, 347)]),
            ("hole", lambda x, y: [(0, 0, 719, y + 2)])]):
        yy = 8 + i * 32
        add("str clip %s" % nm, "font_str_x", TXT10, X + 3, color=CBLACK,
            at=yy, clip=clip(X + 3, yy))
        add("str clip %s al" % nm, "font_str_x", TXT10, X, color=CWHITE,
            at=yy + 8, clip=clip(X, yy + 8))
        add("run clip %s" % nm, "font_run_x", TXT10, X, CBLACK, CWHITE,
            at=yy + 16, clip=clip(X, yy + 16))
        add("run clip %s skew" % nm, "font_run_x", TXT10, X + 5, CWHITE,
            CBLACK, at=yy + 24, clip=clip(X + 5, yy + 24))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", default=".")
    ap.add_argument("--image", required=True)
    ap.add_argument("--syms", required=True)
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--out", required=True)
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--only", default=None)
    ap.add_argument("--marks", default=None,
                    help="comma list of extra symbols to split one case by")
    a = ap.parse_args()
    tree = os.path.abspath(a.tree)
    sys.path.insert(0, os.path.join(tree, "tools"))
    os.chdir(tree)
    import os88marty
    S = json.load(open(a.syms))
    lin = lambda n: S[n][1]
    off = lambda n: S[n][0]
    stub = off("fsx_caps")
    sbuf = stub + 16
    res = {"machine": a.machine, "cases": []}
    with os88marty.launch(os.path.abspath(a.image), machine=a.machine) as m:
        os88marty.settle(m)
        m.pause()
        r = m.regs()
        ss, sp = r["ss"], r["sp"]
        vt = m.cmd(cmd="video")["type"]
        def rw(n):
            return int.from_bytes(m.read(lin(n), 2), "little")
        wide = rw("vid_cwm8") + 8
        res["video"], res["wide"] = vt, wide
        m.write(lin("wm_clip_n"), b"\0\0")
        def fb():
            if vt == "cga":
                return m.read(0xB8000, 0x4000)
            if vt in ("mda", "hercules"):
                return m.read(0xB0000, 0x8000)
            return b"".join(m.planes())
        def park(routine, regs):
            tgt = off(routine)
            rel = (tgt - (stub + 3)) & 0xFFFF
            m.write((KERNEL_SEG << 4) + stub,
                    bytes([0xE8, rel & 0xFF, rel >> 8, 0xEB, 0xFE]))
            m.cmd(cmd="park", cs=KERNEL_SEG, ip=stub)
            for reg, val in (("ds", KERNEL_SEG), ("es", KERNEL_SEG),
                             ("ss", ss), ("sp", sp)):
                m.setreg(reg, val)
            for reg, val in regs.items():
                m.setreg(reg, val & 0xFFFF)
            m.write(lin("cur_lazy"), b"\0")
        def measure(marks):
            c0 = m.status()["cycles"]
            pts = []
            for addr in marks + [(KERNEL_SEG << 4) + stub + 3]:
                m.bp_exec(addr)
                m.run()
                if m.wait_stop(120) is None:
                    raise SystemExit("never reached %x" % addr)
                st = m.status()
                pts.append(st["cycles"] - c0)
            return pts, m.regs()
        for c in cases(wide):
            if a.only and a.only not in c["name"]:
                continue
            m.write((KERNEL_SEG << 4) + sbuf, c["s"] + b"\0")
            regs = dict(cx=c["x"], dx=c["y"], si=sbuf)
            if c["al"] is not None:
                regs["ax"] = c["al"] | (c["ah"] << 8)
            else:
                regs["ax"] = c["s"][0]
            got = []
            outregs = None
            for _ in range(a.reps):
                if c["color"] is not None:
                    m.write(lin("gfx_color"), bytes([c["color"]]))
                m.write(lin("gfx_dis"), bytes([c["dis"]]))
                if c["dis"]:
                    m.write(lin("gfx_disink"), bytes([CBLACK]))
                if c.get("clip"):
                    tab = b"".join(v.to_bytes(2, "little") for r in c["clip"]
                                   for v in r)
                    m.write(lin("wm_clip_tab"), tab)
                    m.write(lin("wm_clip_n"), len(c["clip"]).to_bytes(2, "little"))
                park(c["routine"], regs)
                marks = []
                if a.marks and a.only:
                    marks = [lin(x) for x in a.marks.split(",")]
                pts, outregs = measure(marks)
                got.append(pts)
                m.write(lin("wm_clip_n"), b"\0\0")
            m.write(lin("gfx_dis"), b"\0")
            tot = sorted(p[-1] for p in got)[len(got) // 2]
            rec = dict(name=c["name"], cycles=tot, all=[p[-1] for p in got],
                       us=tot / 4.772727, marks=got[0][:-1],
                       ax=outregs["ax"] if c["routine"] == "font_width_x"
                       else None)
            res["cases"].append(rec)
            print("%-24s %8d cyc %9.1f us  %s" % (rec["name"], tot, rec["us"],
                                                  got[0][:-1] or ""),
                  flush=True)
        data = fb()
        res["fb_sha"] = hashlib.sha1(data).hexdigest()
        open(a.out + ".fb", "wb").write(data)
    json.dump(res, open(a.out, "w"), indent=1)
    print("fb", res["fb_sha"])


if __name__ == "__main__":
    main()
