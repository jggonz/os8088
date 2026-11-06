#!/usr/bin/env python3
"""VBATTLED.O88's PROFILE (SPEC.md 126.8): a play timed on the machine,
written as VBPROFn.DAT beside the engine, read by tools/vbtprof.py.

    make vbattledisk && python3 tests/vbtdiag.py

MartyPC's VGA XT with 1.44 MB drives (os8088_xt_vga_144), the system disk in A: and a copy of
build/vbattlev.img in B: (VBATTLED.O88 and the demo's BATTLEV.VBT):
VBATTLED.O88 opened, played, the title watched a few guest seconds, Esc,
and B: read back on the host:

  1. B:VBPROF1.DAT is there: 'VBPR' version 1, VGA4, the bundle's name;
  2. its frames are the play's: as many as the hook drew while it ran
     (vb_prn at the end), each with a decode time, the title's clip;
  3. tools/vbtprof.py reads it and prints the play.

BROKEN ON PURPOSE (vb_prmark's call out of the hook) 2 goes red: a profile
of no frames.
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
import os88fat      # noqa: E402
import os88geom as geom    # noqa: E402
import os88marty    # noqa: E402
import os88ui       # noqa: E402
import vbtprof      # noqa: E402
from cycweb import pkg_syms     # noqa: E402

fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",),
                       defines=("VB_DIAG",))
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    try:
        b = os.path.join(tmp, "b.img")
        shutil.copy(os88build.at("build/vbattlev.img"), b)
        with os88ui.boot(os88build.at("build/os8088.img"), apps=b,
                         machine="os8088_xt_vga_144") as ui:
            m = ui.m
            w = ui.path("B:/VBATTLED.O88")
            base = struct.unpack_from("<H", m.read(
                geom.winptr(m, w.i, ui._S) + geom.W_SEG, 2))[0] << 4

            def rb(nm, k=1):
                return m.read(base + syms[nm], k)

            def rw(nm):
                return struct.unpack_from("<H", rb(nm, 2))[0]
            m.key("Enter")
            os88marty.until(m, lambda mm: rb("vb_tplay")[0] == 1, "the title",
                            poll=0.1, limit=600, guest=120)
            os88marty.until(m, lambda mm: rw("vb_prn") >= 40,
                            "40 frames of it", poll=0.1, limit=300,
                            guest=60)
            m.pause()
            drawn = rw("vb_prn")
            claimed = rw("vb_prseg")
            m.run()
            m.key("Escape")
            os88marty.until(m, lambda mm: rw("vb_prseg") == 0,
                            "the profile written", poll=0.1, limit=300,
                            guest=60)
            os88marty.ui_done(m)
            m.pause()
            back = os.path.join(tmp, "back.img")
            m.flush(1, back)
            m.run()
        fat = os88fat.Fat12(back)
        try:
            prof = bytes(fat.read("VBPROF1.DAT"))
        except Exception as e:
            prof = b""
            print("     (no VBPROF1.DAT: %s)" % e)
        p = os.path.join(tmp, "VBPROF1.DAT")
        open(p, "wb").write(prof)
        good = prof[:4] == b"VBPR"
        h, fr, rd = vbtprof.read_prof(p) if good else ({}, [], [])
        ok(good and h["ver"] == 1 and h["kind"] == "VGA4" and
           h["name"] == "BATTLEV.VBT" and claimed,
           "1. B:VBPROF1.DAT: %r, version %s, %s, %r" % (
               prof[:4], h.get("ver"), h.get("kind"), h.get("name")))
        clipf = [f for f in fr if f["mode"] == vbtprof.CLIP_MODE]
        ok(good and len(fr) >= drawn >= 40 and clipf and
           all(f["t"] > 0 for f in clipf if not f["stall"]),
           "2. its %d frames are the play's (%d drawn when Esc was pressed), "
           "the title's %d each with a decode time (mean %.1f ms)" % (
               len(fr), drawn, len(clipf), sum(f["t"] for f in clipf) /
               max(1, len(clipf))))
        r = subprocess.run([sys.executable, "tools/vbtprof.py", p],
                           capture_output=True, text=True)
        print("\n".join("     | " + x for x in r.stdout.splitlines()[:12]))
        ok(r.returncode == 0 and "frames drawn" in r.stdout,
           "3. tools/vbtprof.py reads it")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print("vbtdiag: %d FAILED" % len(fails))
        return 1
    print("vbtdiag: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
