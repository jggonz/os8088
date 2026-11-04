#!/usr/bin/env python3
"""The Video Battle ENGINE against its REFERENCE (SPEC.md 125.6): the rules
battle (tests/vbtrulesdemo.py) played on MartyPC's XT (os8088_xt_hdd_sb,
the bundle on its XT-IDE C:) and in tools/vbtrules.Sim in LOCKSTEP.

    make vbattle && python3 tests/vbtengine.py

At every character's turn the engine's whole state is read out of the
package - every HP, MP and limit gauge, the enemy's HP and stance, and the
generator's own state - and must be the reference's at the same moment;
then the reference chooses (the plan's action, or the first it can pay for
and aim), and the keys that take it are worked out from the bundle's own
menus (a sub-menu's Enter, the side menu's Left, an ally's Enter) and
pressed. The battle covers every rule the engine plays (vbtrules.py checks
the battle does), so one number the engine gets wrong - a draw out of
order, a cap missed, a 16-bit lap - fails here at the turn it happened, by
name. The waits are on the guest's state, never on a clock.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build, os88geom as geom, os88marty, os88ui   # noqa: E402
import vbtrules as V    # noqa: E402
import vbtrulesdemo as D    # noqa: E402
from cycweb import pkg_syms     # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
MACHINE = "os8088_xt_hdd_sb"
KEYS = {"down": "ArrowDown", "left": "ArrowLeft", "return": "Enter"}


def u16(b, o=0):
    return struct.unpack_from("<H", b, o)[0]


def keys_for(R, c, ai):
    """The keys that take action AI from character C's top menu"""
    ch = R.chars[c]
    top = R.menus[ch["top"]]
    for i, (kind, idx) in enumerate(top["items"]):
        if kind == 0 and idx == ai:
            return ["down"] * i + ["return"]
        if kind == 1:
            for j, (_, i2) in enumerate(R.menus[idx]["items"]):
                if i2 == ai:
                    return ["down"] * i + ["return"] + ["down"] * j + ["return"]
    if ch["side"] != 0xFF:
        for j, (_, i2) in enumerate(R.menus[ch["side"]]["items"]):
            if i2 == ai:
                return ["left"] + ["down"] * j + ["return"]
    raise SystemExit("action %d is on none of character %d's menus" % (ai, c))


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    with tempfile.TemporaryDirectory() as tmp:
        proj = D.make_project(os.path.join(tmp, "rules.vbp"))
        data, R = D.bundle(proj)
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
        sim = V.Sim(R)
        gen = D.battle(sim, R)
        want = next(gen)
        per = {c: 0 for c in range(R.nchar)}
        bad, turns, n = [], 0, R.nchar
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
            base = u16(rec, geom.W_SEG) << 4

            def rb(nm, off=0):
                return m.read(base + syms[nm] + off, 1)[0]

            def rw(nm, off=0):
                return u16(m.read(base + syms[nm] + off, 2))

            def until(cond, what, guest=120.0):
                os88marty.until(m, lambda mm: cond(), what, poll=0.05,
                                limit=900.0, guest=guest)

            def key(k):
                m.key(KEYS[k], up=False)
                os88marty.until(m, lambda mm: True, "a key", poll=0.0,
                                limit=10.0, guest=0.06)
                m.key(KEYS[k], down=False)
                os88marty.until(m, lambda mm: True, "a key", poll=0.0,
                                limit=10.0, guest=0.25)
            key("return")
            until(lambda: rb("vb_ready") == 1, "the play to start")
            while want is not None:
                # a character's menu, freshly up: the engine waits on a key
                until(lambda: rb("vb_state") in (1, 3), "a menu or the end",
                      guest=180.0)
                if rb("vb_state") == 3:
                    bad.append("the engine's battle ended at turn %d, the "
                               "reference's goes on" % turns)
                    break
                e = dict(turn=rb("vb_turn"), ehp=rw("vb_ehp"),
                         hp=[rw("vb_hp", 2 * i) for i in range(n)],
                         mp=[rw("vb_mp", 2 * i) for i in range(n)],
                         lim=[rb("vb_lim", i) for i in range(n)],
                         stance=rb("vb_stance"), rng=rw("vb_rng"))
                c, r = want
                if e != r:
                    bad.append("turn %d: the engine %s, the reference %s"
                               % (turns, e, r))
                    print("DIFF turn %d\n  engine    %s\n  reference %s"
                          % (turns, e, r), flush=True)
                    break
                ai = D.choose(sim, R, c, per[c])
                per[c] += 1
                ks = keys_for(R, c, ai)
                if R.acts[ai]["target"] == V.TARGETS["ally"]:
                    ks.append("return")
                print("ok   turn %2d %s: %s  (%s)" % (turns, R.chars[c]["name"],
                                                     R.act_ids[ai], " ".join(ks)),
                      flush=True)
                turns += 1
                for k in ks:
                    key(k)
                until(lambda: rb("vb_state") != 1 or rb("vb_turn") != c or
                      rw("vb_rng") != r["rng"], "the action to be taken")
                try:
                    want = gen.send(ai)
                except StopIteration:
                    want = None
            if want is None:
                until(lambda: rb("vb_state") == 3, "the engine's battle to end",
                      guest=180.0)
                eo = ("", "victory", "defeat", "escape")[rb("vb_over")]
                if eo != sim.over:
                    bad.append("the engine's battle ended in %s, the "
                               "reference's in %s" % (eo, sim.over))
        finally:
            m.quit()
    print("%d turns compared; the reference: %s" % (turns, sim.over))
    if bad:
        for b in bad:
            print("FAIL", b)
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
