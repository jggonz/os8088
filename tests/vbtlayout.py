#!/usr/bin/env python3
"""THE BAND'S LAYOUT (SPEC.md 126.5.1): every part of the band placed and
shown or hidden, and menu items placed one by one - the engine's band
against the builder's preview (tools/vbtband.py), PIXEL FOR PIXEL, on
MartyPC's XT with a CGA.

    make vbattle && python3 tests/vbtlayout.py [--arm custom|default]

The owner: "a way to individually place the menu items, and each menu
component (name, hp, mp, status message) and a way to optionally hide each
one (aka this battle doesn't have mp, or this other battle doesn't want
status messages shown)".

The rules battle (tests/vbtrulesdemo.py), its band plain, is played; at
each look the band's rows are read OFF THE GLASS and the preview drawn for
the same moment - the engine's turn, menu, cursor, message, HP, MP, gauges,
stance and items, and THE ENGINE'S OWN GLYPHS (read out of [vb_font]), so
that nothing but the layout can differ. The looks: the first menu (a whole
band built); the next turn's menu after the enemy has acted; then BRAM's
cursor moved, his Help sub-menu (three items, placed of their own) opened,
and closed again - all drawn IN PART, the band rebuilt only when its
ground changes (126.5: "any way to draw the menus faster?"):

  custom:  the enemy's name moved, the limit star and HP's most hidden,
           HP and MP moved, the message moved (clear of the menu), the divider moved, and the
           top menu's second item placed of its own, the Potion's count
           shown beside it in BRAM's Help ("x3") - and a second build
           with MP and the message HIDDEN;
  default: no layout at all - the band as it always was.

Broken on purpose (the engine's message drawn at its old fixed row) the
custom arm differs, at the message's rows; vb_menu_erase returning at once,
every look after the first differs (the old menu's lines left up). Before
the openings were encoded in the canvas once the band is up (126.3) the
band's rules were eaten by the last opening's records, and the glass
differed at both arms' rule rows.
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
              hp_col=28, mp_col=33, name_col=20, msg_col=4, msg_row=50,
              divider_col=17, item_count_show=1)
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
        m.pause()
        fnt = m.read(base + syms["vb_font"], 95 * 8)
        vbtband._FONT = {32 + i: list(fnt[i * 8:i * 8 + 8]) for i in range(95)}
        import vbtrules as V
        rules = V.Rules(V.compile_rules(
            p, {c: i for i, c in enumerate(p.d["clips"])},
            B.images_of(p)[1], preview=True))
        m.run()

        def key(k):
            m.key(k, up=False)
            until(lambda: True, "a key", guest=0.06, poll=0.0)
            m.key(k, down=False)
            until(lambda: True, "a key", guest=0.25, poll=0.0)

        def glass():
            v = m.read(0xB8000, 16384)
            return unpack(b"".join(
                v[(y & 1) * 8192 + (y >> 1) * 80:(y & 1) * 8192 + (y >> 1)
                  * 80 + 80] for y in range(by, 200)), rows)

        def compare(what):
            # the preview for THIS moment - the engine's own HP, MP, gauges,
            # stance, items, message, turn, menu and cursor - and the glass
            # read until it is the same, for 30 guest s at most
            until(lambda: rb("vb_state") == GS_MENU and rb("vb_dirty") == 0
                  and rb("vb_tcomp") == 0, what, guest=240.0)
            m.pause()
            st = V.Sim(rules)
            n = len(rules.chars)
            st.hp = [rw("vb_hp", 2 * i) for i in range(n)]
            st.mp = [rw("vb_mp", 2 * i) for i in range(n)]
            st.lim = [rb("vb_lim", i) for i in range(n)]
            st.stance = rb("vb_stance")
            st.items = [rw("vb_items", 2 * i) for i in range(len(st.items))]
            msg = m.read(base + rw("vb_msg"), 40).split(b"\0")[0].decode(
                "latin-1")
            turn, cur, mi = rb("vb_turn"), rb("vb_cursor"), rb("vb_menu")
            ch = rules.chars[turn]
            menu = (None if mi == ch["top"] else "side" if mi == ch["side"]
                    else rules.menus[mi]["label"])
            ref = vbtband.render(p, turn, menu, cur, message=msg, state=st,
                                 raw=True)
            for _ in range(100):
                eng = glass()
                if np.array_equal(eng, ref):
                    break
                m.run()
                until(lambda: True, "a moment", guest=0.3, poll=0.0)
                m.pause()
            m.run()
            seen.append((what, eng, ref, turn, menu, cur, msg))
            return turn

        seen = []
        compare("the first menu")
        # THE MENUS AFTER THE FIRST, drawn in part (126.5): the ground kept,
        # the old menu's uncovered lines cleared, only what changed drawn
        for _ in range(6):
            key("Enter")                    # the top menu's first item
            until(lambda: rb("vb_state") != GS_MENU, "the action", guest=30.0)
            if compare("a later menu") == 1:
                break
        if layout is not None and rb("vb_turn") == 1:
            top = rb("vb_menu")         # (each key TAKEN before the look)
            key("ArrowDown")                # BRAM: Fire -> Help
            until(lambda: rb("vb_cursor") == 1, "the cursor moved")
            compare("BRAM's cursor moved")
            key("Enter")
            until(lambda: rb("vb_menu") != top, "the sub-menu open")
            compare("BRAM's Help sub-menu open")
            key("Escape")
            until(lambda: rb("vb_menu") == top, "the sub-menu closed")
            compare("...and closed again")
    finally:
        m.quit()
    eng = seen[0][1]
    for what, e, ref, turn, menu, cur, msg in seen:
        diff = np.argwhere(e != ref)
        lit = int((e != 0).sum())
        where = "" if not len(diff) else " - first at row %d, column %d" % (
            diff[0][0], diff[0][1] // 8)
        if len(diff):
            where += ", rows %d..%d" % (diff[:, 0].min(), diff[:, 0].max())
        ok(e.shape == ref.shape and not len(diff) and lit > 500,
           "%s, %s: the glass IS the preview, pixel for pixel (%d lit, %d "
           "differ%s; turn %d, menu %s, cursor %d, message %r)"
           % (tag, what, lit, len(diff), where, turn, menu, cur, msg))
        if os.environ.get("VBTL_DUMP") and len(diff):
            from PIL import Image
            pal = np.array([[0, 0, 0], [0, 170, 170], [170, 0, 170],
                            [255, 255, 255]], np.uint8)
            im = np.vstack([pal[e], np.full((4, 320, 3), 80, np.uint8),
                            pal[ref]])
            Image.fromarray(im).resize((640, im.shape[0] * 2),
                                       Image.NEAREST).save(
                os.environ["VBTL_DUMP"] + "-%s-%d.png"
                % (tag.split()[0], seen.index((what, e, ref, turn, menu,
                                               cur, msg))))
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
