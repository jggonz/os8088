#!/usr/bin/env python3
"""WHAT A BATTLE HOLDS, on a machine that has been USED (SPEC.md 125.4.4,
docs/plans/VBATTLE-RAM-PLAN.md R1), on MartyPC's Hercules 5150, 640 KB.

    make vbattle && python3 tests/vbtram.py [--arm fresh|used]

The demo battle's Hercules rendition played to its first menu, and the heap
read off the machine (tools/heapmap.py's Map):

  fresh: straight out of a boot - the desktop's caches (the directory
         read-ahead, the icon store, a window's raise cache) GONE while the
         play holds the machine (the owner: "we can purge them for the extra
         ram"), nothing of the arena stranded, and both rings over 100 KB
         (they were 2 x 88 on the owner's battle, the caches a pocket under
         the block);
  used:  the owner's "they opened clear skies, then paint, then opened
         video battle ... then closed paint and clear skies, then went to
         play": the battle's region with holes under it. The play posts a
         compaction and starts on its wake ([vb_ncomp]), and its rings are
         within 16 KB of the fresh boot's.

Broken on purpose (vb_cpask answering "not worth it" always) the used arm's
play never waits on a wake and its rings come out smaller; the block claimed
from the bottom again, the fresh arm's caches stay, stranded under it.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap              # noqa: E402
import os88build, os88geom as geom, os88marty, os88ui   # noqa: E402
import os88vbt as B         # noqa: E402
from cycweb import pkg_syms     # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
MACHINE = "os8088_5150_herc_hdd_sb_gla"
CACHES = {0xFE02: "the directory read-ahead", 0xFB01: "the icon store",
          0xFB10: "a window's raise cache"}
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


class Q(object):
    def __init__(self, m):
        self.m = m

    def read(self, lin, n):
        return self.m.read(lin, n)


def run(arm, tmp, syms):
    vbp = os.path.join(tmp, "s.vbp")
    B.demo_project(vbp, bars=48)
    import vbtproj as P
    p = P.Project.load(vbp)
    p.set(("target", "renditions"), ["cga4", "herc"])
    p.save()
    files = []
    for k in ("cga4", "herc"):
        f = os.path.join(tmp, B.BUNDLE_NAMES[k])
        open(f, "wb").write(B.build_project(p, kind=k))
        files.append((B.BUNDLE_NAMES[k], f))
    vhd = os.path.join(tmp, "c.vhd")
    cmd = [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
           "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
           "--vbr", os88build.at("build/boothd.bin"),
           "--mbr", os88build.at("build/mbr.bin"),
           "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
           "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
           "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
           "--file", "SKIES.O88=" + os88build.at("build/skies.o88"),
           "--file", "PAINT.O88=" + os88build.at("build/paint.o88"),
           "--file", "VBATTLE.O88=" + os88build.at("build/vbattle.o88")]
    for nm, f in files:
        cmd += ["--file", "%s=%s" % (nm, f)]
    subprocess.run(cmd, check=True, capture_output=True)
    m = os88marty.launch(None, machine=MACHINE,
                         extra=["--mount", "hd:0:" + vhd], boot=False)
    try:
        m.run()
        ui = os88ui.UI(m)
        ui.up(limit=300)
        os88marty.no_saver(m)
        os88marty.ui_done(m)
        others = []
        if arm == "used":
            others = [ui.path("C:/SKIES.O88"), ui.path("C:/PAINT.O88")]
        w = ui.path("C:/VBATTLE.O88")
        rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4
        for o in others:            # the holes, under the battle's region
            ui.close(o)
        os88marty.ui_done(m)
        # (the battle's region may have moved since: read it again)
        rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4

        def rb(nm):
            return m.read(base + syms[nm], 1)[0]

        def rw(nm):
            return struct.unpack_from("<H", m.read(base + syms[nm], 2))[0]

        def until(cond, what, guest=120.0):
            os88marty.until(m, lambda mm: cond(), what, poll=0.05,
                            limit=900.0, guest=guest)

        def key(k):
            m.key(k, up=False)
            until(lambda: True, "k", guest=0.06)
            m.key(k, down=False)
            until(lambda: True, "k", guest=0.25)
        m.pause()
        before = heapmap.Map(Q(m), heapmap.symbols())
        m.run()
        key("Enter")
        # (a compaction moves the battle's region: its record says where)
        def reb():
            r = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
            return struct.unpack_from("<H", r, geom.W_SEG)[0] << 4
        until(lambda: m.read(reb() + syms["vb_ready"], 1)[0] == 1,
              "the play")
        base = reb()
        until(lambda: rb("vb_tplay") == 1, "the title")
        key("Enter")
        until(lambda: rb("vb_state") == 1 and rb("vb_dirty") == 0,
              "the menu", guest=240.0)
        m.pause()
        mp = heapmap.Map(Q(m), heapmap.symbols())
        got = dict(r0=rw("vb_rkb"), r1=rw("vb_rkb2"), comp=rw("vb_ncomp"))
        m.run()
    finally:
        m.quit()
    left = [CACHES[c.own] for c in mp.claims if c.own in CACHES]
    had = [CACHES[c.own] for c in before.claims if c.own in CACHES]
    free = sum(p for _, p in mp.runs()) / 64.0
    got.update(left=left, had=had, free=free)
    if os.environ.get("VBTRAM_MAP"):
        for c in mp.claims:
            print("       %05X %6.1f KB own %04X %s %s" % (
                c.seg << 4, c.kb, c.own, "HI" if c.hi else "lo",
                "pinned" if c.pinned else "movable"))
        print("       runs " + ", ".join("%.1f@%05X" % (p / 64.0, b << 4)
                                          for b, p in mp.runs()))
    return got


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("fresh", "used", "both"),
                    default="both")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    res = {}
    for arm in (("fresh", "used") if a.arm == "both" else (a.arm,)):
        with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) \
                as tmp:
            res[arm] = g = run(arm, tmp, syms)
        print("     %s: rings %d + %d KB, free %.1f KB, caches before %s, "
              "during %s, compactions %d" % (arm, g["r0"], g["r1"], g["free"],
                                             g["had"], g["left"], g["comp"]),
              flush=True)
    if "fresh" in res:
        g = res["fresh"]
        ok(not g["left"] and g["had"] and min(g["r0"], g["r1"]) > 100 and
           g["free"] < 40,
           "fresh: the caches (%s) gone for the play, nothing stranded "
           "(%.1f KB free), the rings %d + %d KB" % (
               ", ".join(g["had"]), g["free"], g["r0"], g["r1"]))
    if "used" in res:
        g = res["used"]
        ok(g["comp"] >= 1, "used: the play POSTED a compaction and started "
           "on its wake (%d)" % g["comp"])
        if "fresh" in res:
            f = res["fresh"]
            ok(g["r0"] + g["r1"] >= f["r0"] + f["r1"] - 16,
               "used: the rings %d + %d KB, within 16 KB of the fresh "
               "boot's %d + %d" % (g["r0"], g["r1"], f["r0"], f["r1"]))
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
