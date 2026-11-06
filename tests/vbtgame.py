#!/usr/bin/env python3
"""A BATTLE EMBEDDED IN ITS OWN .O88 (SPEC.md 125.2), on MartyPC.

    make vbattle && python3 tests/vbtgame.py

The owner: "Can we have an option to embed a clip into the .o88? One
adapter per output." `os88vbt.py game` writes each built rendition into its
own copy of the engine - flags bit 2 and the packed image's end in the link
word, the bundle at the next 4 KB - and the engine reads it out of its own
file (vb_embfind). C: holds THAT FILE ALONE: no VBATTLE.O88, no .VBT.

  cga:  the demo's CGA4 game on the XT's CGA: it opens, its window titled
        with the battle's name, and Enter plays it to the first menu - read
        out of its own file, at the 4 KB past its image;
  herc: the Hercules game on the 5150's Hercules, the same;
  wrong: the CGA4 game on the Hercules machine says which adapter it is
        for, in the window, and plays nothing.

Broken on purpose (vb_embfind answering none) cga and herc say BATTLE.VBT
not found and play nothing.
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
ARMS = {"cga": ("os8088_xt_hdd_sb", "cga4", 0),
        "herc": ("os8088_5150_herc_hdd_sb_gla", "herc", 2),
        "wrong": ("os8088_5150_herc_hdd_sb_gla", "cga4", 0)}
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def run(arm, tmp, syms):
    machine, kind, vk = ARMS[arm]
    vbp = os.path.join(tmp, "g.vbp")
    B.demo_project(vbp, bars=int(os.environ.get("VBTG_BARS", "2")))
    p = P.Project.load(vbp)
    stem = B.game_stem(p) + B.GAME_SUFFIX[kind]
    game = os.path.join(tmp, stem + ".O88")
    open(game, "wb").write(B.embed_o88(
        open(os88build.at("build/vbattle.o88"), "rb").read(),
        B.build_project(p, kind=kind), p.d["name"]))
    vhd = os.path.join(tmp, "c.vhd")
    subprocess.run(
        [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
         "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
         "--vbr", os88build.at("build/boothd.bin"),
         "--mbr", os88build.at("build/mbr.bin"),
         "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
         "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
         "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
         "--file", "%s.O88=%s" % (stem, game)], check=True,
        capture_output=True)
    m = os88marty.launch(None, machine=machine,
                         extra=["--mount", "hd:0:" + vhd], boot=False)
    try:
        m.run()
        ui = os88ui.UI(m)
        ui.up(limit=300)
        os88marty.no_saver(m)
        os88marty.ui_done(m)
        w = ui.path("C:/%s.O88" % stem)
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
        title = m.read(base + 16, 16).split(b"\0")[0]
        key("Enter")
        played = False
        # (vb_err is "a read failed" WHILE the load runs - its default until
        # the load is done - so a refusal is the window's status, not that)
        try:
            until(lambda: rb("vb_ready") == 1 or (
                rw("vb_err") != 0 and rw("vb_status") == rw("vb_err")),
                "the play or a refusal")
            if rb("vb_ready") == 1:
                until(lambda: rb("vb_tplay") == 1, "the title")
                key("Enter")
                until(lambda: rb("vb_state") == 1 and rb("vb_dirty") == 0,
                      "the menu", guest=240.0)
                played = True
        except os88marty.MartyError:
            pass
        at = struct.unpack_from("<I", m.read(base + syms["vb_embat"], 4))[0]
        name = m.read(base + rw("vb_namep"), 13).split(b"\0")[0].decode()
        err = rw("vb_err")
        msg = m.read(base + err, 40).split(b"\0")[0].decode() if err else ""
        if arm == "wrong":
            ok(not played and "CGA" in msg,
               "wrong: the CGA4 game on a Hercules says so and plays nothing "
               "(%r)" % msg)
        else:
            ok(played and at and at % 4096 == 0 and name == stem + ".O88"
               and rb("vb_kind") == vk,
               "%s: %s.O88 alone plays to its first menu, read out of itself "
               "at %d (%s, adapter %d)" % (arm, stem, at, name,
                                           rb("vb_kind")))
            ok(title == p.d["name"].encode()[:15],
               "%s: ...its window titled %r" % (arm, title))
        key("Escape")
    finally:
        m.quit()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=sorted(ARMS) + ["all"], default="all")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    for arm in (("cga", "herc", "wrong") if a.arm == "all" else (a.arm,)):
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
