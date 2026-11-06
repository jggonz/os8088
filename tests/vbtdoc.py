#!/usr/bin/env python3
"""A .VBT DOUBLE-CLICKED (SPEC.md 125.2, 54.5, 54.6), on MartyPC.

    make vbattle && python3 tests/vbtdoc.py

The owner: "Can we have an option to load a .o88 in builds without an
embed, and associate the extension with it?" VBATTLE.O88 declares .VBT,
so a battle under ANY name opens it, and the play reads that document -
and, as the builder names a battle's renditions (BATTLE, BATTLEV,
BATTLEH), this machine's own beside it. C: holds VBATTLE.O88 and the demo
as MYFIGHT.VBT (CGA4) and MYFIGHTH.VBT (Hercules), and no BATTLE.VBT:

  cga:  on the XT's CGA, MYFIGHT.VBT opened from the Disk window opens
        Video Battle with it, and Enter plays it to the first menu -
        MYFIGHT.VBT read, a CGA4 bundle;
  herc: on the 5150's Hercules, the same double-click plays MYFIGHTH.VBT,
        the Hercules rendition beside it.

Broken on purpose (vb_docnames not called) the window opens with no
document, looks for BATTLE.VBT and says it is not found: both arms red.
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
import os88build, os88geom as geom, os88marty, os88ui   # noqa: E402
import os88vbt as B         # noqa: E402
import vbtproj as P         # noqa: E402
from cycweb import pkg_syms     # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
ARMS = {"cga": ("os8088_xt_hdd_sb", "MYFIGHT.VBT", 0),
        "herc": ("os8088_5150_herc_hdd_sb_gla", "MYFIGHTH.VBT", 2)}
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def run(arm, tmp, syms):
    machine, want, kind = ARMS[arm]
    vbp = os.path.join(tmp, "d.vbp")
    B.demo_project(vbp, bars=2)
    p = P.Project.load(vbp)
    files = []
    for k, nm in (("cga4", "MYFIGHT.VBT"), ("herc", "MYFIGHTH.VBT")):
        f = os.path.join(tmp, nm)
        open(f, "wb").write(B.build_project(p, kind=k))
        files += ["--file", "%s=%s" % (nm, f)]
    vhd = os.path.join(tmp, "c.vhd")
    subprocess.run(
        [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
         "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
         "--vbr", os88build.at("build/boothd.bin"),
         "--mbr", os88build.at("build/mbr.bin"),
         "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
         "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
         "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
         "--file", "VBATTLE.O88=" + os88build.at("build/vbattle.o88")] +
        files, check=True, capture_output=True)
    m = os88marty.launch(None, machine=machine,
                         extra=["--mount", "hd:0:" + vhd], boot=False)
    try:
        m.run()
        ui = os88ui.UI(m)
        ui.up(limit=300)
        os88marty.no_saver(m)
        os88marty.ui_done(m)
        w = ui.path("C:/MYFIGHT.VBT")       # (a document: its program opens)
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
            os88marty.until(m, lambda mm: True, "k", poll=0.0, limit=10.0,
                            guest=0.06)
            m.key(k, down=False)
        doc = m.read(base + syms["vb_docname"], 13).split(b"\0")[0]
        ok(rb("vb_doc") == 1 and doc == b"MYFIGHT.VBT",
           "%s: MYFIGHT.VBT double-clicked opens Video Battle with it (%r)"
           % (arm, doc))
        key("Enter")
        try:
            until(lambda: rb("vb_ready") == 1, "the play")
            until(lambda: rb("vb_tplay") == 1, "the title")
            key("Enter")
            until(lambda: rb("vb_state") == 1 and rb("vb_dirty") == 0,
                  "the menu", guest=240.0)
            played = True
        except os88marty.MartyError:
            played = False
        name = m.read(base + rw("vb_namep"), 13).split(b"\0")[0].decode()
        ok(played and name == want and rb("vb_kind") == kind,
           "%s: Enter plays %s (%s, adapter %d) to its first menu"
           % (arm, name, "played" if played else "NOT played",
              rb("vb_kind")))
        key("Escape")
    finally:
        m.quit()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("cga", "herc", "both"), default="both")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    for arm in (("cga", "herc") if a.arm == "both" else (a.arm,)):
        with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) \
                as tmp:
            run(arm, tmp, syms)
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
