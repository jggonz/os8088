#!/usr/bin/env python3
"""THE SMALLEST BATTLE THAT BUILDS, and plays to its menu and to victory -
the owner's "a minimal set to see the menu, and to build so I can start
iterating" (SPEC.md 125), on MartyPC's XT with a CGA.

    make vbattle && python3 tests/vbtminimal.py

A project made through the model as the window makes it, from one ffmpeg
test pattern and nothing else: no title, no openings, NO MUSIC, one
character, one action, one enemy move.

  host: step by step, what is missing is said BY NAME - never "None",
        which a KeyError on an unchosen field printed (the enemy's
        victory clip was the owner's); the band draws before the battle is
        complete (it never plays the endings); a battle with no music is
        not an error; a sound nothing uses, whose file is gone, neither
        stops the build nor is read; and a sound something uses refuses to
        be removed, naming what;
  glass: the bundle plays - the idle loop and the band, the menu, one
        Attack at the 1-HP enemy, victory.
"""
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
import vbtana as A          # noqa: E402
import vbtband              # noqa: E402
import vbtproj as P         # noqa: E402
from cycweb import pkg_syms     # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
MACHINE = "os8088_xt_hdd_sb"
KEYS = {"return": "Enter", "esc": "Escape"}
GS_MENU = 1
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def errors(p):
    return [x for x in p.check() if x.level == "ERROR"]


def project(tmp):
    vid = os.path.join(tmp, "fight.mp4")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                    "testsrc2=size=320x240:rate=15", "-t", "4", vid],
                   check=True)
    p = P.Project()
    p.save(os.path.join(tmp, "min.vbp"))
    p.add_source("fight", path=vid)
    n = len(A.screens(p, "fight"))
    p.add_character("ARI")
    p.add_action("attack", "Attack", effects=[{"verb": "damage",
                                               "amount": 50}])
    p.menu_add(0, action="attack")
    p.d["enemy"]["stances"][0]["moves"].append(
        {"action": None, "weight": 1, "rule": "alternate"})
    msgs = []
    try:
        vbtband.render(p, 0)
    except Exception as e:      # noqa: BLE001
        msgs.append(str(e))
    p.d["enemy"]["stances"][0]["moves"][0]["action"] = "attack"
    band = vbtband.render(p, 0)
    ok(msgs and "move 1: no action chosen" in msgs[0]
       and band.size == (320, 58),
       "the band: an enemy move with no action is said so (%r); given one, "
       "it draws before the battle is complete" % msgs[:1])
    left = [repr(x) for x in errors(p)]
    ok(left and not any("None" in x for x in left),
       "what a battle still needs, by name: %s" % left)
    p.add_clip("hit", "fight", (n // 2, n - 1), anchors=(0,), exit=0)
    p.add_clip("win", "fight", (0, n // 2), anchors=(0,), exit=0)
    p.set_idle("fight", (0, n))
    p.set(("actions", "attack", "pools"), {"hit": ["hit"]})
    p.set(("enemy", "victory"), "win")
    p.set(("enemy", "defeat"), "win")
    p.set(("enemy", "hp"), 1)
    # a sound nothing uses, its file gone; one the menu uses
    p.add_sound("old", path=os.path.join(tmp, "gone.wav"))
    beep = os.path.join(tmp, "beep.wav")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                    "sine=f=880:d=0.1", beep], check=True)
    p.add_sound("beep", path=beep)
    p.set(("ui_sounds", "move"), "beep")
    try:
        p.remove_sound("beep")
        refused = None
    except ValueError as e:
        refused = str(e)
    ok(refused and "the menu's move sound" in refused,
       "a sound in use refuses to be removed: %r" % refused)
    # THE OWNER'S TWO (2026-11-04): a clip of the idle loop's own video,
    # which nothing plays, and hit clips set PER TARGET only
    p.add_clip("idleclip", "fight", (0, n - 1), anchors=())
    p.add_character("BRAM")
    p.menu_add(1, action="attack")
    p.add_action("bite", "Bite", target="character",
                 effects=[{"verb": "damage", "amount": 5}])
    p.set(("actions", "bite", "by_target"), {"ARI": {"hit": ["hit"]},
                                             "BRAM": {"hit": ["hit"]}})
    p.d["enemy"]["stances"][0]["moves"].append(
        {"action": "bite", "weight": 1, "rule": "alternate"})
    about = [x for x in p.check() if x.where in ("clips.idleclip",
                                                  "actions.bite")]
    ok([x.level for x in about] == ["INFO"]
       and "idle loop's own video" in about[0].text,
       "a clip of the idle loop's video nothing plays: one note, no error "
       "(%s)" % about)
    p.set(("actions", "bite", "by_target"), {"ARI": {"hit": ["hit"]}})
    miss = [x.text for x in p.check() if x.where == "actions.bite"]
    ok(len(miss) == 1 and "at BRAM" in miss[0],
       "hit clips per target: only the character without one is said "
       "(%s)" % miss)
    p.set(("actions", "bite", "by_target"), {"ARI": {"hit": ["hit"]},
                                             "BRAM": {"hit": ["hit"]}})
    # EACH CHARACTER'S OWN CLIP of the shared Attack (the owner,
    # 2026-11-05): no general hit clip, ARI's and BRAM's own - aimed at the
    # enemy, an action's per-character clips are the DOER's
    p.add_clip("hit2", "fight", (0, n // 2), anchors=(0,), exit=0)
    p.set(("actions", "attack", "pools"), {})
    p.set(("actions", "attack", "by_target"), {"ARI": {"hit": ["hit"]},
                                               "BRAM": {"hit": ["hit2"]}})
    p.set(("enemy", "hp"), 120)
    import vbtrules as V
    idx = {c: i for i, c in enumerate(p.d["clips"])}
    R = V.Rules(V.compile_rules(p, idx))
    S = V.Sim(R)
    got = [S.resolve(list(p.d["actions"]).index("attack"), w)["clip"]
           for w in (0, 1)]
    ok(got == [idx["hit"], idx["hit2"]],
       "the reference: ARI's Attack plays ARI's clip, BRAM's BRAM's "
       "(clips %s)" % got)
    p.save()
    errs = errors(p)
    ok(not errs and p.d["music"] is None,
       "NO MUSIC and a leftover sound whose file is gone: no error (%s)"
       % errs)
    data = B.build_project(p)
    names_ = [c["name"].strip().lower() for c in B.Bundle(data).clips]
    ok("hit" in names_ and not any(x.startswith("idleclip") for x in names_),
       "...and the unused clip is left out of the bundle (%s)" % names_)
    p.remove_sound("old")
    ok("old" not in p.d["sounds"], "...and the leftover removed")
    return p, data


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        p, data = project(tmp)
        ok(len(data) > 0, "the smallest battle builds: %d KB" % (len(data) // 1024))
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
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4

            def rb(nm, off=0):
                return m.read(base + syms[nm] + off, 1)[0]

            def until(cond, what, guest=120.0, poll=0.05):
                os88marty.until(m, lambda mm: cond(), what, poll=poll,
                                limit=900.0, guest=guest)

            def key(k):
                m.key(KEYS[k], up=False)
                until(lambda: True, "a key", guest=0.06, poll=0.0)
                m.key(KEYS[k], down=False)
                until(lambda: True, "a key", guest=0.25, poll=0.0)
            key("return")
            until(lambda: rb("vb_ready") == 1, "the play to start")
            until(lambda: rb("vb_state") == GS_MENU and rb("vb_dirty") == 0,
                  "the menu", guest=120.0)
            band = m.read(0xB8000, 16384)
            lit = sum(1 for y in range(142, 200) for x in band[
                (y & 1) * 8192 + (y >> 1) * 80:(y & 1) * 8192 + (y >> 1) * 80
                + 80] if x)
            ok(lit > 200, "glass: the menu, the band up (%d bytes lit in its "
               "rows)" % lit)
            # EACH ONE'S OWN ATTACK, on the glass: at every menu, Attack -
            # and the clip that plays is the one whose turn it was
            names = [c["name"].strip().lower() for c in B.Bundle(data).clips]
            own = {0: names.index("hit"), 1: names.index("hit2")}
            plays = []
            for _ in range(8):
                if rb("vb_over"):
                    break
                until(lambda: rb("vb_state") == GS_MENU or rb("vb_over"),
                      "a menu", guest=120.0)
                if rb("vb_over"):
                    break
                turn = rb("vb_turn")
                key("return")           # Attack
                until(lambda: rb("vb_mode") == 1 and rb("vb_cc") in
                      own.values(), "an Attack's clip", guest=30.0)
                plays.append((turn, rb("vb_cc")))
                until(lambda: rb("vb_mode") != 1 or rb("vb_over"),
                      "the clip over", guest=60.0)
            ok(rb("vb_over") == 1 and {t for t, _ in plays} == {0, 1} and
               all(cc == own[t] for t, cc in plays),
               "glass: each character's Attack plays its OWN clip, to "
               "victory (turn, clip: %s; ARI's %d, BRAM's %d)"
               % (plays, own[0], own[1]))
            key("esc")
        finally:
            m.quit()
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
