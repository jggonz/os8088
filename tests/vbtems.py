#!/usr/bin/env python3
"""The Video Battle's bundle HELD IN EXPANDED MEMORY on an 8088
(VBATTLE-MEMORY-PLAN 7, SPEC.md 125.4.1): MartyPC's 5150 with a Hercules,
a Sound Blaster, an XT-IDE and the Lo-tech 2 MB EMS board
(os8088_5150_herc_hdd_sb_ems_gla) - the machine class the owner plays on.

    make && make emstest && python3 tests/vbtems.py

The system floppy is build/emstest.img with its SYSTEM.CFG wanting the card,
the hard disk and EMS.DRV; C: holds VBATTLE.O88 and the Hercules demo
(BATTLEH.VBT), its enemy at 1 HP. Below a 386 the engine takes EMS first:

  - the hold is EMS (`[vb_htype]` 2), one range, the whole bundle;
  - it fills while the title goes round, the disk reading straight INTO
    the frame's window;
  - from then on the engine reads THE DISK NOT ONCE (`[vb_rcalls]`, which
    only the disk path counts) through the fade, the openings, the menu,
    victory, its end loop and the play again - every read the hold's
    (`[vb_hcopies]`).
"""
import os
import shutil
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
MACHINE = "os8088_5150_herc_hdd_sb_ems_gla"
KEYS = {"return": "Enter", "esc": "Escape"}
WANT = (1 << 0) | (1 << 1) | (1 << 7)   # SOUND.DRV, HDD.DRV, EMS.DRV
GS_MENU = 1
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def main():
    os.chdir(ROOT)
    img0 = os88build.at("build/emstest.img")
    if not os.path.exists(img0):
        sys.exit("vbtems: no %s - run `make emstest`" % img0)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        vbp = os.path.join(tmp, "e.vbp")
        B.demo_project(vbp, bars=2)
        p = P.Project.load(vbp)
        p.set(("enemy", "hp"), 1)
        p.save()
        data = B.build_project(p, kind="herc")
        vbt = os.path.join(tmp, "BATTLEH.VBT")
        open(vbt, "wb").write(data)
        img = os.path.join(tmp, "sys.img")
        shutil.copy(img0, img)
        cfg = os.path.join(tmp, "SYSTEM.CFG")
        open(cfg, "wb").write(b"O88CFG\0\0" + (3).to_bytes(2, "little") +
                              b"DW" + bytes([1, 2]) +
                              WANT.to_bytes(2, "little") + b"\0\0")
        for verb in (["del", img, "SYSTEM.CFG"], ["add", img, cfg,
                                                 "SYSTEM.CFG"]):
            subprocess.run([sys.executable, "tools/os88fat.py"] + verb,
                           check=True, capture_output=True)
        vhd = os.path.join(tmp, "c.vhd")
        subprocess.run([sys.executable, "tools/os88hdd.py", "--template",
                        TEMPLATE, "--out", vhd, "--noboot",
                        "--file", "VBATTLE.O88=" +
                        os88build.at("build/vbattle.o88"),
                        "--file", "BATTLEH.VBT=" + vbt],
                       check=True, capture_output=True)
        m = os88marty.launch(img, machine=MACHINE,
                             extra=["--mount", "hd:0:" + vhd], boot=False)
        try:
            m.run()
            ui = os88ui.UI(m)
            ui.up(limit=300)
            os88marty.no_saver(m)
            os88marty.ui_done(m)
            w = ui.path("C:/VBATTLE.O88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4

            def rb(nm, off=0):
                return m.read(base + syms[nm] + off, 1)[0]

            def rw(nm, off=0):
                return struct.unpack_from("<H", m.read(base + syms[nm] + off,
                                                       2))[0]

            def until(cond, what, guest=120.0, poll=0.1):
                os88marty.until(m, lambda mm: cond(), what, poll=poll,
                                limit=1200.0, guest=guest)

            def key(k):
                m.key(KEYS[k], up=False)
                os88marty.until(m, lambda mm: True, "a key", poll=0.0,
                                limit=10.0, guest=0.06)
                m.key(KEYS[k], down=False)
                os88marty.until(m, lambda mm: True, "a key", poll=0.0,
                                limit=10.0, guest=0.25)
            key("return")
            until(lambda: rb("vb_ready") == 1, "the play to start")
            ok(rb("vb_hon") == 1 and rb("vb_htype") == 2,
               "below a 386 the hold is EXPANDED memory (htype %d)"
               % rb("vb_htype"))
            ok(rb("vb_hrn") == 1 and rw("vb_hkb4") * 4096 >= len(data),
               "...one range, the whole bundle (%d KB of %d)"
               % (rw("vb_hkb4") * 4, len(data) // 1024))
            until(lambda: rb("vb_tplay") == 1, "the title")
            until(lambda: rb("vb_hfull") == 1, "the hold to fill while "
                  "the title goes round", guest=300.0)
            rc0, c0 = rw("vb_rcalls"), rw("vb_hcopies")
            ok(True, "the hold filled while the title went round (%d disk "
               "reads so far)" % rc0)
            key("return")
            until(lambda: rb("vb_state") == GS_MENU, "the menu", guest=240.0)
            key("return")
            until(lambda: rb("vb_eplay") == 1, "victory's end loop",
                  guest=240.0)
            ok(rb("vb_over") == 1, "the battle to victory and its end loop")
            key("return")
            until(lambda: rb("vb_tplay") == 1 and rb("vb_over") == 0,
                  "the play again, at the title", guest=240.0)
            # THE BYTES: the title, played again out of the board, is the
            # host's own decode of the rendition on the glass
            bt = B.battle_of(p, None, "herc")
            B.REND = bt.rend
            want = []
            for f in bt.title["frames"]:
                sf = bt.rend.surface(B.full_of(f), bt.rend.full)
                g = bytearray(32768)
                g[5:] = bytes(sf[:32768 - 5])
                want.append(bytes(g))
            hits = 0
            for _ in range(30):
                m.pause()
                hits += m.read(0xB0000, 32768) in want
                m.run()
                until(lambda: True, "a moment", guest=0.07, poll=0.0)
            ok(hits >= 15, "the title out of the board is the host's decode "
               "on the glass (%d of 30 looks exact)" % hits)
            rc1, c1 = rw("vb_rcalls"), rw("vb_hcopies")
            ok(rc1 == rc0 and c1 > c0,
               "from the hold's fill on, NOT ONE disk read: %d reads, %d "
               "copies out of the board" % (rc1 - rc0, c1 - c0))
            key("esc")
            until(lambda: rb("vb_ready") == 0, "Esc to leave")
        finally:
            m.quit()
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
