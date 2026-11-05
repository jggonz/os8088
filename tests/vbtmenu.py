#!/usr/bin/env python3
"""THE MENU READS NOTHING IT CANNOT USE, AND NOTHING A KEY WAITS ON
(docs/plans/completed/VBATTLE-RAM-PLAN.md R3), on MartyPC's XT with a CGA and an SB.

    make vbattle && python3 tests/vbtmenu.py

The owner: "we never need to preload a disabled/unreachable action's files
... We can probably make the interface part more responsive by changing
when we delay to after a selection, so the user can use the interface
freely, and then the longer waits (when needed) are hidden in the delay
AFTER they make the selection."

The rules battle (tests/vbtrulesdemo.py) to ARI's first menu - Attack,
Bolt (8 MP), Limit (a full gauge, which the first turn has not):

  1. the cursor resting on LIMIT, which cannot be taken: its clip is not
     wanted ([vb_want] none) and nothing of it is read; resting on BOLT,
     which can: Bolt's clip is wanted;
  2. twelve moves of the cursor up and down the menu, each after a rest:
     a key is taken (the cursor moved) in under 0.3 guest s, every time.

Broken on purpose (vb_hover's cost check taken out) 1 fails: Limit's clip
is wanted and read.
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
import vbtrulesdemo as D    # noqa: E402
from cycweb import pkg_syms     # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
MACHINE = "os8088_xt_hdd_sb"
GS_MENU = 1
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        p = D.make_project(os.path.join(tmp, "m.vbp"))
        data = B.build_project(p)
        clips = [c["name"].strip().lower() for c in B.Bundle(data).clips]
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

            def rb(nm):
                return m.read(base + syms[nm], 1)[0]

            def rw(nm):
                return struct.unpack_from("<H", m.read(base + syms[nm], 2))[0]

            def cyc():
                return int(m.status().get("cycles", 0)) / os88marty.GUEST_HZ

            def until(cond, what, guest=120.0, poll=0.05):
                os88marty.until(m, lambda mm: cond(), what, poll=poll,
                                limit=900.0, guest=guest)

            def key(k):
                m.key(k, up=False)
                until(lambda: True, "k", guest=0.06, poll=0.0)
                m.key(k, down=False)
                until(lambda: True, "k", guest=0.25, poll=0.0)
            key("Enter")
            until(lambda: rb("vb_ready") == 1, "the play")
            until(lambda: rb("vb_state") == GS_MENU and rb("vb_dirty") == 0
                  and rb("vb_turn") == 0, "ARI's menu", guest=240.0)
            until(lambda: True, "rest", guest=2.0, poll=0.0)
            # 1. Limit (cursor 2), then Bolt (cursor 1)
            key("ArrowDown")
            key("ArrowDown")
            until(lambda: rb("vb_cursor") == 2, "the cursor on Limit")
            r0 = rw("vb_rcalls")
            until(lambda: True, "rest", guest=1.5, poll=0.0)
            lim_want, lim_reads = rb("vb_want"), (rw("vb_rcalls") - r0) & 0xFFFF
            key("ArrowUp")
            until(lambda: rb("vb_cursor") == 1, "the cursor on Bolt")
            until(lambda: True, "rest", guest=1.5, poll=0.0)
            bolt_want = rb("vb_want")
            want_bolt = clips.index("ari_bolt"[:8]) if "ari_bolt"[:8] in \
                clips else None
            ok(lim_want == 0xFF and bolt_want == want_bolt,
               "1. on LIMIT (cannot be taken) nothing wanted (%d, %d reads); "
               "on BOLT its clip wanted (%d, Bolt's %s)" % (
                   lim_want, lim_reads, bolt_want, want_bolt))
            # 2. a key's wait, twelve times
            waits = []
            for i in range(12):
                k = "ArrowDown" if (i // 2) % 2 == 0 else "ArrowUp"
                m.pause()
                c0, t0 = rb("vb_cursor"), cyc()
                m.key(k, up=False)
                m.run()
                moved = None
                while cyc() - t0 < 3.0:
                    until(lambda: True, "t", guest=0.01, poll=0.0)
                    if rb("vb_cursor") != c0:
                        moved = cyc() - t0
                        break
                m.key(k, down=False)
                if moved is not None:
                    waits.append(moved)
                until(lambda: True, "rest", guest=0.8, poll=0.0)
            ok(waits and max(waits) < 0.3,
               "2. a key taken in under 0.3 guest s every time (%d moves, "
               "the longest %.2f s)" % (len(waits), max(waits or [9])))
            key("Escape")
        finally:
            m.quit()
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
