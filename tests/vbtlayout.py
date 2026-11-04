#!/usr/bin/env python3
"""THE BAND'S LAYOUT (SPEC.md 125.5.1): every part of the band placed and
shown or hidden, and menu items placed one by one - the engine's band
against the builder's preview (tools/vbtband.py), PIXEL FOR PIXEL, on
MartyPC's XT with a CGA.

    make vbattle && python3 tests/vbtlayout.py [--arm custom|default]

The owner: "a way to individually place the menu items, and each menu
component (name, hp, mp, status message) and a way to optionally hide each
one (aka this battle doesn't have mp, or this other battle doesn't want
status messages shown)".

The rules battle (tests/vbtrulesdemo.py), its band plain, is played to its
first menu; the engine's composed band ([vb_cseg]) is read off the machine
and the preview drawn for the same moment - its turn, its cursor, the
message the engine shows, and THE ENGINE'S OWN GLYPHS (read out of
[vb_font]), so that nothing but the layout can differ:

  custom:  the enemy's name moved, the limit star and HP's most hidden,
           HP and MP moved, the message moved, the divider moved, and the
           top menu's second item placed of its own - and a second build
           with MP and the message HIDDEN;
  default: no layout at all - the band as it always was.

Broken on purpose (the engine's message drawn at its old fixed row) the
custom arm differs, at the message's rows.
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
import numpy as np          # noqa: E402
import os88build, os88geom as geom, os88marty, os88ui   # noqa: E402
import os88vbt as B         # noqa: E402
import vbtband              # noqa: E402
import vbtrulesdemo as D    # noqa: E402
from cycweb import pkg_syms     # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
MACHINE = "os8088_xt_hdd_sb"
GS_MENU = 1
fails = []

CUSTOM = dict(enemy_col=2, enemy_row=40, limit_show=0, hpmax_show=0,
              hp_col=28, mp_col=33, name_col=20, msg_col=4, msg_row=2,
              divider_col=17)
HIDDEN = dict(CUSTOM, mp_show=0, msg_show=0)


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def project(tmp, layout):
    p = D.make_project(os.path.join(tmp, "l.vbp"))
    if layout is not None:
        p.set(("band_layout",), dict(layout))
        looks = dict(p.d.get("menu_looks", {}))
        lk = dict(looks.get("ARI:top", {}))
        lk["places"] = [None, [24, 30]]     # its second item, of its own
        looks["ARI:top"] = lk
        p.set(("menu_looks",), looks)
    p.save()
    return p


def unpack(buf, rows):
    a = np.frombuffer(buf, np.uint8)[:80 * rows].reshape(rows, 80)
    px = np.zeros((rows, 320), np.uint8)
    for k in range(4):
        px[:, k::4] = (a >> (6 - 2 * k)) & 3
    return px


def run(tmp, layout, tag, syms):
    p = project(tmp, layout)
    data = B.build_project(p)
    vbt = os.path.join(tmp, "BATTLE.VBT")
    open(vbt, "wb").write(data)
    vhd = os.path.join(tmp, "c.vhd")
    subprocess.run(
        [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
         "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
         "--vbr", os88build.at("build/boothd.bin"),
         "--mbr", os88build.at("build/mbr.bin"),
         "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
         "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
         "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
         "--file", "VBATTLE.O88=" + os88build.at("build/vbattle.o88"),
         "--file", "BATTLE.VBT=" + vbt], check=True, capture_output=True)
    m = os88marty.launch(None, machine=MACHINE,
                         extra=["--mount", "hd:0:" + vhd], boot=False)
    try:
        m.run()
        ui = os88ui.UI(m)
        ui.up(limit=240)
        os88marty.no_saver(m)
        os88marty.ui_done(m)
        w = ui.path("C:/VBATTLE.O88")
        rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4

        def rb(nm, off=0):
            return m.read(base + syms[nm] + off, 1)[0]

        def rw(nm, off=0):
            return struct.unpack_from("<H", m.read(base + syms[nm] + off,
                                                   2))[0]

        def until(cond, what, guest=120.0, poll=0.05):
            os88marty.until(m, lambda mm: cond(), what, poll=poll,
                            limit=900.0, guest=guest)
        m.key("Enter", up=False)
        until(lambda: True, "a key", guest=0.06, poll=0.0)
        m.key("Enter", down=False)
        until(lambda: rb("vb_ready") == 1, "the play to start")
        until(lambda: rb("vb_state") == GS_MENU and rb("vb_dirty") == 0
              and rb("vb_tcomp") == 0, "the menu", guest=240.0)
        by = rb("vb_meta", 65)
        rows = 200 - by
        # the band is composed a piece at a time in the foreground (~1-2
        # guest s on the XT): read until two reads 0.3 guest s apart agree,
        # with nothing left to draw
        # the band is composed a piece at a time in the foreground, and only
        # after the menu's state is up (~1-2 guest s on the XT): the
        # preview is drawn for this moment, and the engine's buffer read
        # until it is the same, for 30 guest s at most - a layout drawn
        # wrong never is
        m.pause()
        fnt = m.read(base + syms["vb_font"], 95 * 8)
        msg = m.read(base + rw("vb_msg"), 40).split(b"\0")[0].decode("latin-1")
        turn, cur = rb("vb_turn"), rb("vb_cursor")
        vbtband._FONT = {32 + i: list(fnt[i * 8:i * 8 + 8]) for i in range(95)}
        ref = vbtband.render(p, turn, None, cur, message=msg, raw=True)
        for _ in range(100):
            eng = unpack(m.read(rw("vb_cseg") << 4, 80 * rows), rows)
            if np.array_equal(eng, ref):
                break
            m.run()
            until(lambda: True, "a moment", guest=0.3, poll=0.0)
            m.pause()
        m.run()
    finally:
        m.quit()
    if os.environ.get("VBTL_DUMP"):
        from PIL import Image
        pal = np.array([[0, 0, 0], [0, 170, 170], [170, 0, 170],
                        [255, 255, 255]], np.uint8)
        im = np.vstack([pal[eng], np.full((4, 320, 3), 80, np.uint8),
                        pal[ref]])
        Image.fromarray(im).resize((640, im.shape[0] * 2)).save(
            os.environ["VBTL_DUMP"] + "-%s.png" % tag.split()[0])
    diff = np.argwhere(eng != ref)
    lit = int((eng != 0).sum())
    where = "" if not len(diff) else " - first at row %d, column %d" % (
        diff[0][0], diff[0][1] // 8)
    ok(eng.shape == ref.shape and not len(diff) and lit > 500,
       "%s: the engine's band IS the preview's, pixel for pixel (%d lit, %d "
       "differ%s; turn %d, message %r)" % (tag, lit, len(diff), where, turn,
                                           msg))
    return eng


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("custom", "default"), default="custom")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        if a.arm == "default":
            run(tmp, None, "default", syms)
        else:
            e1 = run(tmp, CUSTOM, "custom", syms)
            e2 = run(tmp, HIDDEN, "MP and the message hidden", syms)
            ok((e1 != e2).any(), "...and hiding them changed the band")
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
