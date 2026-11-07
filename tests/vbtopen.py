#!/usr/bin/env python3
"""VIDEO BATTLE WITH NO BATTLE BESIDE IT, and File > Open (SPEC.md 126.2.4).

    make vbattle vbattlev.img && python3 tests/vbtopen.py

The owner, before the merge: "When there is no BATTLE.VBT beside the player,
lets change the opening messages to tell them to load a battle. And take out
the 'enter to play' when nothing is loaded. Then add a File->Open".

MartyPC's VGA XT with 1.44 MB drives; B: holds VBATTLE.O88 at its root and
the VGA rendition's BATTLEV.VBT in a folder, BATTLES - nothing beside the
player:

  1. the window opens with NO BATTLE ([vb_none]) and says so - its lines
     the "no battle" set (to open one, or put BATTLE.VBT beside it);
  2. Enter plays nothing: no "Loading...", no play;
  3. File > Open... -> BATTLES/BATTLEV.VBT: the window has its document, and
     Enter plays it - the title on the glass ([vb_tplay]);
  4. after the play, File > Open... again - the battle read before let go
     (vb_unload) - and Enter plays it again.
BROKEN ON PURPOSE (vb_onfile leaving [vb_none] set) step 3's play never
starts.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
import os88build    # noqa: E402
import os88geom as geom     # noqa: E402
import os88marty    # noqa: E402
import os88ui       # noqa: E402
from cycweb import pkg_syms     # noqa: E402

fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    try:
        vbt = os.path.join(tmp, "BATTLEV.VBT")
        subprocess.run(["mcopy", "-n", "-i", os88build.at("build/vbattlev.img"),
                        "::BATTLEV.VBT", vbt], check=True)
        b = os.path.join(tmp, "b.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", b, "--size",
                        "1440", os88build.at("build/vbattle.o88"),
                        "BATTLES:" + vbt], check=True, capture_output=True)
        with os88ui.boot(os88build.at("build/os8088.img"), apps=b,
                         machine="os8088_xt_vga_144") as ui:
            m = ui.m
            w = ui.path("B:/VBATTLE.O88")
            base = struct.unpack_from("<H", m.read(
                geom.winptr(m, w.i, ui._S) + geom.W_SEG, 2))[0] << 4

            def rb(nm):
                return m.read(base + syms[nm], 1)[0]

            def rw(nm):
                return struct.unpack_from("<H", m.read(base + syms[nm], 2))[0]

            def text(nm):
                return m.read(base + syms[nm], 40).split(b"\0")[0]
            if os.environ.get("VBTO_SHOT"):     # (a look: the window)
                ui.still()
                from PIL import Image
                fw, fh, fb = m.fbuf()
                Image.frombytes("RGB", (fw, fh), fb).save(
                    os.environ["VBTO_SHOT"])
            ok(rb("vb_none") == 1 and rw("vb_t4p") == syms["vb_s_n3"],
               "1. no battle beside the player: the window says so (%r / "
               "%r)" % (text("vb_s_n1"), text("vb_s_n3")))
            st = rw("vb_status")
            m.key("Enter")
            os88marty.until(m, lambda mm: True, "a moment", poll=0.0,
                            limit=60, guest=3.0)
            ok(rb("vb_ready") == 0 and rw("vb_status") == st,
               "2. Enter plays nothing (no play, no 'Loading...')")

            def play(n, spec):
                ui.menu_pick("File", "Open...")
                ui.chooser_open(spec)       # (the answer comes after the
                os88marty.until(m, lambda mm: rb("vb_doc") == 1 and  # close)
                                rb("vb_none") == 0, "the answer", poll=0.05,
                                limit=120, guest=10)
                ok(rb("vb_none") == 0 and rb("vb_doc") == 1 and
                   text("vb_docname") == b"BATTLEV.VBT",
                   "%d. File > Open... %s: the window's document (%r)"
                   % (n, spec, text("vb_docname")))
                m.key("Enter")
                os88marty.until(m, lambda mm: rb("vb_tplay") == 1,
                                "the title", poll=0.1, limit=600, guest=120)
                ok(True, "%d. ...and Enter plays it: the title on the glass"
                   % n)
                m.key("Escape")
                os88marty.until(m, lambda mm: rb("vb_ready") == 0,
                                "Esc to leave", poll=0.1, limit=300,
                                guest=60)
                ui.still()
            play(3, "BATTLES/BATTLEV.VBT")
            play(4, "BATTLEV.VBT")      # (the chooser opens where it was
            #                             left; the battle read before let go)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print("vbtopen: %d FAILED" % len(fails))
        return 1
    print("vbtopen: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
