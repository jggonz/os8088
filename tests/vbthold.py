#!/usr/bin/env python3
"""The Video Battle's bundle HELD IN XMS (VBATTLE-MEMORY-PLAN 4 and 5,
SPEC.md 125.4.1), on QEMU.

    make && python3 tests/vbthold.py [--arm whole|part|nox]

WHY QEMU: docs/TESTING.md's closed list, entry 1 - XMS is a 286-and-up
store and every MartyPC machine is an 8088. Nothing here is timed: every
wait is on the guest's own state.

THE INSTRUMENT IS A DISK SWAP, as tests/vidxms.py's is. The demo battle
(its enemy at 1 HP) on B:, the engine on B:, QEMU's own 128 MB:

  whole: the play starts and the title goes round while the reader fills
         the hold with the WHOLE bundle (one range, `[vb_hfull]`); then B:
         is changed to a BLANK floppy. Enter: the fade, the openings, the
         menu, an Attack, victory and its end loop, Enter and the title
         again - every read after the swap the hold's (`[vb_hcopies]`
         counts them), none the disk's, which has nothing to find.
  part:  the pool held to less than the bundle (a hold order of the
         builder's): the ranges come from the bundle's own HOLD ORDER, the
         hold fills to its last, and the play runs with the disk serving
         the rest - copies AND disk reads both counted.
  nox:   `-m 1`, no pool: no hold, and the play the disk's as before.
"""
import argparse
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
import os88build, os88geom as geom, os88qemu, os88ui    # noqa: E402
import os88vbt as B         # noqa: E402
import vbtproj as P         # noqa: E402
import xmcheck              # noqa: E402
from cycweb import pkg_syms     # noqa: E402
from vidxms import Q        # noqa: E402
from pxswin import qpoke       # noqa: E402

GS_MENU = 1
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("whole", "part", "nox"), default="whole")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    pkg = os88build.at("build/vbattle.o88")
    sysimg0 = os88build.at("build/os8088.img")
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        vbp = os.path.join(tmp, "h.vbp")
        B.demo_project(vbp, bars=2)
        p = P.Project.load(vbp)
        p.set(("enemy", "hp"), 1)
        p.save()
        data = B.build_project(p)
        vbt = os.path.join(tmp, "BATTLE.VBT")
        open(vbt, "wb").write(data)
        bd = B.Bundle(data)
        disk = os.path.join(tmp, "vbt.img")
        blank = os.path.join(tmp, "blank.img")
        sysimg = os.path.join(tmp, "sys.img")
        shutil.copy(sysimg0, sysimg)
        for out, files in ((disk, [pkg, vbt]), (blank, [])):
            subprocess.run([sys.executable, "tools/os88disk.py", "-o", out,
                            "--size", "1440"] + files, check=True,
                           capture_output=True)
        q = Q(tmp, sysimg, disk, 1 if a.arm == "nox" else 128)
        xmcheck.wait_desktop(q.sock, "vbthold")
        S = xmcheck.sym
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
        mo = os88qemu.Mouse(q, S, q.hmp, after=0)

        def wait(cond, what, secs=60):
            if not os88qemu.acted(q, cond, secs=secs, what=what, poll=0.05):
                raise SystemExit("vbthold: timed out waiting for %s" % what)
        x, y = geom.drive_pt(q, "B", S)
        mo.dblclick(x, y)
        box = {}

        def diskwin():
            for w in ui.windows():
                if w.visible and ui.fs_of(w) == (1, 0, 1):
                    box["w"] = w
                    return True
            return False
        wait(diskwin, "drive B's window", 30)
        os88qemu.ui_done(q, S, cap=1.0)
        w = box["w"]
        i, _ = ui.entry("VBATTLE.O88", w)
        before = set(o.i for o in ui.windows())
        mo.dblclick(*ui.row_xy(w, i - ui.scroll(w)))

        def engine():
            for o in ui.windows():
                if o.i not in before and o.visible:
                    box["p"] = o
                    return True
            return False
        wait(engine, "the battle's window", 30)
        os88qemu.ui_done(q, S, cap=1.0)
        rec = q.read(S("wm_wins") + box["p"].i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = u16(rec, geom.W_SEG) << 4

        def rb(n, off=0):
            return q.read(base + syms[n] + off, 1)[0]

        def rw(n, off=0):
            return u16(q.read(base + syms[n] + off, 2))
        if a.arm == "part":     # the pool held under the bundle: the
            cap = len(data) // 4096 * 6 // 10   # gate's cap, in 4 KBs
            qpoke(q, [(base + syms["vb_hcap"], struct.pack("<H", cap))])
        q.hmp("sendkey ret")
        wait(lambda: rb("vb_ready") == 1, "the play to start", 60)
        hon = rb("vb_hon")
        if os.environ.get("VBTH_DBG"):
            print("   hon %d hrn %d hkb4 %d hpool %d hcap %d hold %s tab %s" % (
                hon, rb("vb_hrn"), rw("vb_hkb4"), rw("vb_hpool"), rw("vb_hcap"),
                bd.hold[:4], q.read(base + syms["vb_htab"], 24).hex()))
        if a.arm == "nox":
            ok(hon == 0, "nox: no pool, no hold")
        else:
            ok(hon == 1 and rb("vb_hrn") >= 1,
               "%s: the hold is taken (%d range(s), %d KB)"
               % (a.arm, rb("vb_hrn"), rw("vb_hkb4") * 4))
            if a.arm == "whole":
                ok(rb("vb_hrn") == 1 and rw("vb_hkb4") * 4096 >= len(data),
                   "whole: ONE range, the whole bundle (%d KB of %d)"
                   % (rw("vb_hkb4") * 4, len(data) // 1024))
            wait(lambda: rb("vb_tplay") == 1, "the title", 60)
            wait(lambda: rb("vb_hfull") == 1, "the hold to fill, while the "
                 "title goes round", 240)
            ok(True, "%s: the hold filled while the title went round" % a.arm)
            # the hold is the file, byte for byte
            tok = struct.unpack_from("<I", q.read(base + syms["vb_htok"], 4))[0]
            if a.arm == "whole":
                got = q.read(tok, len(data))
                ok(got == data, "whole: the hold is the bundle, byte for byte")
            c0 = rw("vb_hcopies")
            if a.arm == "whole":
                q.hmp("change floppy1 %s raw" % blank)
                print("   B: is blank now")
            else:
                ok(rb("vb_hrn") >= 2 and rw("vb_hkb4") * 4096 < len(data),
                   "part: the pool under the bundle - the HOLD ORDER's %d "
                   "ranges, %d KB of %d" % (rb("vb_hrn"), rw("vb_hkb4") * 4,
                                            len(data) // 1024))
                first = struct.unpack_from("<HH", q.read(base + syms["vb_htab"], 4))
                ok(first == tuple(bd.hold[0]),
                   "part: ...its first range the order's first")
        q.hmp("sendkey ret")                 # the fade, then the openings
        wait(lambda: rb("vb_state") == GS_MENU, "the menu", 240)
        q.hmp("sendkey ret")                 # Attack, at the 1-HP enemy
        wait(lambda: rb("vb_eplay") == 1, "victory's end loop", 240)
        ok(rb("vb_over") == 1 and rb("vb_quit") == 0,
           "%s: the battle played to victory and its end loop" % a.arm)
        q.hmp("sendkey ret")                 # play again: the title
        wait(lambda: rb("vb_tplay") == 1 and rb("vb_over") == 0,
             "the play again, at the title", 240)
        ok(rb("vb_quit") == 0, "%s: ...and again, from the title" % a.arm)
        if a.arm != "nox":
            n = rw("vb_hcopies") - c0
            ok(n > 10, "%s: reads the hold answered after it filled: %d"
               % (a.arm, n))
        q.hmp("sendkey esc")
        wait(lambda: rb("vb_ready") == 0, "Esc to leave", 60)
        q.close()
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
