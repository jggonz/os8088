#!/usr/bin/env python3
"""The generated part against tools/pxsgen.py, byte for byte (SPEC.md 97.3,
97.10).

    python3 tests/pxsscale.py [--machine os8088_5150_cga_gla] [--c160]

Part 2 (the scratch, 97.9) is what px_gen_build wrote on the window's first
TEXTURED frame for the window's backend (WIN1, the Hercules phase - the
sets are built when the rung in force first wants them, px_apply, and an
8086's window starts at Flat, so the row pins Textured before it looks)
and again on entering the bracket (the adapter's own phase: CGA4's two
`ror al, 1`, or none on Mode X, or C160's with --c160): the four bodies
copied from the image, the driver copied from its template, the draw
queue's header, then BOTH resolution sets of scalers and their col2tex
tables. This row reads the part back off MartyPC between frames and diffs
every byte of it against the model - the bodies and the driver against
the image's own bytes, the generated half against pxsgen.image() - and the
two directories in part 0 (px_sctab, px_c2t) against the model's. The
draw queue is transient and is not compared. A differing byte is a wrong
instruction in code the frame calls 64 times.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))     # LAST, so it wins (pxslib)
import os88marty                                                # noqa: E402
import os88parts                                                # noqa: E402
import os88build                                                # noqa: E402
import pxslib                                                    # noqa: E402
import pxsgen                                                    # noqa: E402

FAIL = []
PHASE_OF = {"win1": "win1", "cga4": "cga4", "herc": "herc", "modex": "modex",
            "cga16": "c160", "win4": "modex"}     # (WIN4: Mode X's class, 97.14)


def check(ok, what):
    print("   %-66s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def compare(g, label):
    g.m.pause()
    st = g.state()
    L = pxslib.layout()
    be = PHASE_OF[st["back"]]
    img, sctab, c2t = pxsgen.image(be, L["SCAL"])
    part = g.part_gen(L["SCAL"] + len(img))
    raw = open(pxslib.PKG or os88build.at("build/pxstein.o88"), "rb").read()
    image = os88parts.part_bytes(raw, 0)
    s = pxslib.syms()
    sc = g.bytes_("px_sctab", 484)
    ct = g.bytes_("px_c2t", 484)
    g.m.run()
    check(st["texok"] == 1, "%s: the parts the rung needs were carved" % label)
    bodies = bytearray(image[s["px_bodies"]:s["px_bodies"] + L["BALL"]])
    got_b = bytearray(part[:L["BALL"]])
    for q in range(4):                  # the four patched immediates a body
        for off in (13, 17, 38, 42):    # (PXB_VFRAC/VINT/HFRAC/HINT, 97.2.1)
            for k in (0, 1):
                bodies[q * 58 + off + k] = got_b[q * 58 + off + k] = 0
    # ...and NOT the sprite posts' patch: the driver puts the es: prefix back
    # before it returns (97.6's one patch site), so between frames every
    # texel load of every scaler still begins 0x26 - which the generated-half
    # compare below asserts byte for byte, a ret left standing being the
    # 0xC3 that would differ
    check(got_b == bodies, "%s: the four bodies are the image's %d bytes but the "
          "eight patched ones each" % (label, L["BALL"]))
    drv = image[s["px_drv_tpl"]:s["px_drv_end"]]
    check(part[L["DRV"]:L["DRV"] + len(drv)] == drv, "%s: the driver is its template (%d bytes)"
          % (label, len(drv)))
    qtex = int.from_bytes(part[L["QTEX"]:L["QTEX"] + 2], "little")
    check(qtex == g.handoff()["bt"], "%s: the driver's ES is part 3 (%04x)" % (label, qtex))
    qspr = int.from_bytes(part[L["QSPR"]:L["QSPR"] + 2], "little")
    check(qspr == g.handoff()["spr"], "%s: the sprite pass's ES is part 4 (%04x)" % (label, qspr))
    got = part[L["SCAL"]:]
    diff = [i for i in range(len(img)) if got[i] != img[i]]
    check(not diff, "%s: the generated half (%s) is the model's, all %d bytes (%s)"
          % (label, be, len(img), "0 differ" if not diff else
             "offset 0x%04X is %02X, model %02X; %d differ"
             % (L["SCAL"] + diff[0], got[diff[0]], img[diff[0]], len(diff))))
    for name, buf, want in (("px_sctab", sc, sctab), ("px_c2t", ct, c2t)):
        gotd = [int.from_bytes(buf[i * 2:i * 2 + 2], "little") for i in range(242)]
        wantd = want["full"] + want["low"]
        bad = [i for i in range(242) if gotd[i] != wantd[i]]
        check(not bad, "%s: %s is the model's directory, both sets (%s)"
              % (label, name, "all 242" if not bad else "entry %d is %04X, model %04X; %d differ"
                 % (bad[0], gotd[bad[0]], wantd[bad[0]], len(bad))))
    end = g.word("px_genend")
    check(end == L["SCAL"] + len(img), "%s: px_genend is one past the model (0x%04X vs 0x%04X)"
          % (label, end, L["SCAL"] + len(img)))
    return st["back"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/games360.img")
    ap.add_argument("--c160", action="store_true", help="the second Mode item on a genuine CGA")
    a = ap.parse_args()
    os.chdir(ROOT)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        g = pxslib.open_game(m)
        print("   PXSTEIN.O88: window %d, part 0 at %04x, handoff %s" % (g.win, g.seg, g.handoff()))
        check(g.byte("px_genback") == 0xFF,
              "nothing generated at launch: the window's rung is Flat (px_genback %02X)"
              % g.byte("px_genback"))
        g.pin(rung="tex", lowres=True)          # the first Textured frame
        g.scene("a")                            # builds the sets (px_apply)
        g.wait_frames(1)
        # A FRAME IN FLIGHT when the pokes landed (READY's own force, a
        # Flat Full window frame of ~140 ms) completes first and counts,
        # before the frame whose prologue applies the pick and generates
        # (wave 6's soak read the part as zeros exactly so, once): the
        # generation is waited for, then one more frame drawn after it
        os88marty.until(m, lambda mm: g.byte("px_genback") != 0xFF,
                        "the window's generation", poll=0.2, limit=120.0)
        g.force()
        g.wait_frames(1)
        compare(g, "window, Textured pinned")
        g.scene("b")                            # ...and a second frame
        g.wait_frames(1)                        # regenerates nothing
        compare(g, "window, after a second textured frame")
        if a.c160:
            m.pause()
            g.poke_byte("px_mode", 2)           # PXB_C160
            g.poke_byte("px_fsxm", 0)           # FSXM_TEXT80
            m.run()
        g.enter_fsx()
        g.force()
        g.wait_frames(1)
        back = compare(g, "bracket")
        check(back != "win1", "the bracket regenerated for its own phase (%s)" % back)
        g.leave_fsx()
        g.force()
        g.wait_frames(1)
        compare(g, "window again")
    if FAIL:
        print("pxsscale: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pxsscale: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
