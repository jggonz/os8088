#!/usr/bin/env python3
"""win16nedos - a Windows program double-clicked never shows DOS
(SPEC.md 124.3.1, 21.5.4; docs/plans/DOS-PARTS-PLAN.md 4).

    make build/win16/TINY16.EXE build/win16/DOS.O88 build/DOSHELLO.EXE \\
        && python3 tests/win16nedos.py

On a 4.77MHz 5150 (CGA, MartyPC), off a 360KB B: carrying WIN16.O88, the
shipped DOS.O88, TINY16.EXE (tests/win16/tiny16.asm, an NE assembled here -
no Microsoft binary and no Watcom) and DOSHELLO.EXE (a plain MZ):

  A  TINY16.EXE double-clicked: the machine is STEPPED ONE FRAME AT A TIME
     from the click to WIN16's window, and on no frame is a window called
     DOS visible, and no package claim the size of DOS's carve (the host
     part's image + bss) is ever in mem_tab - the loader decided before
     op_load. No toast is up afterwards: the refusal the loader returns is a
     hand-on, and `Load failed` over it would be the failure. WIN16's window
     opens (on an 8088 it is WIN16's own refusal, `Needs a V20...`, which is
     still WIN16's window and is what this row is about).
  B  DOSHELLO.EXE double-clicked: DOS's window opens, as it always did - the
     loader read the header and handed the document on.
  C  TINY16.EXE TYPED at that window's prompt: WIN16's window opens - the
     host's own arm (`dos_w16_try`), which the loader never sees and which
     no other row drove. "WIN16's window" is the instance window titled with
     the program OR the program's MessageBox (W16TITLES): since WIN16 phase A
     the first is up only through the load, and a typed TINY16 that joins the
     instance leg A left running (SPEC.md 124.28.12) shows only the second.

BROKEN ON PURPOSE: the same probe on c714ec45 (the tree before the loader
arm) sees DOS's window from frame 257 and a 55,328-byte carve, so leg A goes
red; dropping $(DOSW16DEF) from the dosload rule does the same.
"""
import os
import struct
import subprocess
import sys
import tempfile
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88build                                              # noqa: E402
import os88geom as geom                                       # noqa: E402
import os88marty                                              # noqa: E402
import os88ui                                                 # noqa: E402

XT = "os8088_5150_cga_gla"
NE, MZ = "TINY16.EXE", "DOSHELLO.EXE"
# WIN16'S WINDOWS for TINY16: its instance window is titled with the program
# while it loads, and TINY16 has no window of its own - its MessageBox is
# captioned this (tests/win16/tiny16.asm). Since WIN16 phase A (SPEC.md
# 124.28) the instance window is up only through the load, so a poll can
# miss it and find the box; and a second TINY16 that JOINS the instance the
# first left running (124.28.12) shows only its box. Either is WIN16's.
T16BOX = "os8088 WIN16"
W16TITLES = (NE, T16BOX)
FRAMES = 1500                   # 25 guest seconds at 60Hz: the launch is ~6


def b(name):
    return os88build.at(os.path.join("build", name))


def carve():
    """DOS's host part as op_load claims it: image + bss, to the paragraph"""
    raw = open(b("dosp.bin"), "rb").read(12)
    img, bss = struct.unpack_from("<HH", raw, 8)
    return (img + bss + 15) & ~15


def claims(ui):
    raw = bytes(ui.m.read(ui.sym("mem_tab"), 32 * geom.MC_SIZE))
    out = []
    for i in range(32):
        seg, para, own = struct.unpack_from("<HHH", raw, i * geom.MC_SIZE)
        if para and own < 0xFF00:
            out.append(para * 16)
    return out


def dblclick(ui, dw, name):
    i, _ = ui.entry(name, dw)
    x, y = ui.row_xy(dw, i - ui.scroll(dw))
    ui.mo.dblclick(x, y, settle=0)


def main():
    for f in ("win16.o88", "win16/DOS.O88", "win16/" + NE, MZ, "dosp.bin",
              "os8088-360.img"):
        if not os.path.exists(b(f)):
            print("win16nedos: SKIP - no build/%s (make it first)" % f)
            return 77
    bad = []
    work = tempfile.mkdtemp(prefix="w16nd-", dir=os.path.join(ROOT, "build"))
    try:
        bimg = os.path.join(work, "b360.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", bimg,
                        "--size", "360", b("win16.o88"), b("win16/DOS.O88"),
                        b("win16/" + NE), b(MZ), "--folder", "SYSTEM/APPDATA",
                        "--folder", "SYSTEM/APPDATA/WIN16"],
                       check=True, capture_output=True)
        lo = carve()
        with os88ui.boot(b("os8088-360.img"), apps=bimg, machine=XT,
                         verbose=False) as ui:
            dw = ui.open_drive("B")
            os88marty.ui_done(ui.m, limit=30)
            before = set(w.i for w in ui.windows() if w.visible)
            # --- A ---------------------------------------------------------
            dblclick(ui, dw, NE)
            ui.m.pause()
            dosf, big, up = None, [], None
            for f in range(FRAMES):
                ui.m.advance(frames=1)
                for w in ui.windows():
                    if not w.visible or w.i in before:
                        continue
                    if w.title == "DOS" and dosf is None:
                        dosf = f
                    if w.title in W16TITLES and up is None:
                        up = f
                big += [c for c in claims(ui) if lo <= c < lo + 2048]
                if up is not None and f > up + 30:
                    break
            ui.m.go()
            print("A. %s: WIN16's window at frame %s; DOS's window %s; "
                  "carve-sized claims %s" % (NE, up, "never" if dosf is None
                                             else "at frame %d" % dosf,
                                             sorted(set(big)) or "none"))
            if up is None:
                bad.append("A: no window for %s in %d frames" % (NE, FRAMES))
            if dosf is not None:
                bad.append("A: DOS's window was on the glass at frame %d"
                           % dosf)
            if big:
                bad.append("A: a %d-byte claim - DOS's carve (%d) - was taken"
                           % (big[0], lo))
            os88marty.ui_done(ui.m, limit=30)
            text, on = ui.toast()
            if on:
                bad.append("A: a toast is up after the hand-on: %r" % text)
            # --- B ---------------------------------------------------------
            for w in ui.windows():              # WIN16's window is over B:'s
                if w.visible and w.title in W16TITLES:
                    ui.close(w)
            ui.raise_window(dw)
            before = set(w.i for w in ui.windows() if w.visible)
            dblclick(ui, dw, MZ)
            box = {}

            def dos():
                for w in ui.windows():
                    if w.visible and w.i not in before and w.title == "DOS":
                        box["w"] = w
                        return True
                return False
            try:
                ui._wait(dos, "DOS's window for %s" % MZ, 60)
                print("B. %s: DOS's window opened" % MZ)
            except os88ui.UIError as e:
                bad.append("B: %s" % e)
            # --- C ---------------------------------------------------------
            if "w" in box:
                os88marty.pace(ui.m, 8.0)     # DOSHELLO runs, and waits
                ui.m.key("Enter")             # for a key before it ends
                os88marty.pace(ui.m, 4.0)
                before = set(w.i for w in ui.windows() if w.visible)
                ui.m.type_text(NE + "\n")

                def w16():
                    return any(w.visible and w.i not in before and
                               w.title in W16TITLES for w in ui.windows())
                try:
                    ui._wait(w16, "WIN16's window for a typed %s" % NE, 60)
                    print("C. %s typed at the prompt: WIN16's window opened"
                          % NE)
                except os88ui.UIError as e:
                    bad.append("C: %s" % e)
                    if os.environ.get("W16ND_SHOT"):
                        w_, h_, d_ = ui.m.fbuf(0)
                        os88marty.write_png_rgb(os.environ["W16ND_SHOT"],
                                                w_, h_, d_)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16nedos: an NE double-clicked never showed DOS, a plain "
              ".EXE still opens it, and a typed NE still goes to WIN16")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
