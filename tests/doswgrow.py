#!/usr/bin/env python3
"""doswgrow - a DOS window's AUTOMATIC limit grows to its program (SPEC.md
96.54.2.1), and the Setup page says so (96.32.2.4).

A run in the window is given DVW_DEFKB, 128 KB, so a second DOS window has
something left to run in (96.54.2). WordPerfect 4.1's load module is 134,512
bytes, and with an EMPTY Memory limit - Automatic - it used to be refused
"Too big for a window's 128K". Now the header decides: dvw_regrow reads it,
raises the cap to what the loader's two fit tests need, rounded up to
DVW_STEPKB, and dos_run sizes again.

tests/dostrap/dwbig.asm is the fixture: a 136 KB image behind a 512-byte
header, MINALLOC 16 KB more - it fails both tests at 128 KB.

  1  the Setup page with `Run in a window` ticked and the limit empty reads
     `128KB+` (the window's figure, and the `+` that it grows); unticked it
     is the full screen's figure, no `+`, and larger - and the cell the `+`
     was drawn in is blank ON THE GLASS (the first build left it standing:
     an opaque run one cell shorter does not erase the cell after it)
  2  Run: the program RUNS in the window (on its worker, its line in the
     band), the cap raised past 128 to a DVW_STEPKB multiple, and the block
     the program reports holds its image and its MINALLOC
  3  a limit of the user's own - 128 typed into the field, which the figure
     then shows without the `+` - is a decision: the same run is REFUSED
     DER_FIT, in the sentence it always had

**Break it on purpose** (docs/WRITING-TESTS.md 1): with the `call
HENT(dvw_regrow)` in dos_run's `.freeerr` taken out, leg 2 fails - the run
is refused DER_FIT and never reaches the worker. With dos_mem_row's `sbb dl,
dl` changed to `xor dl, dl`, leg 1 fails on the missing `+`.
"""

import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SYS = os.path.join(ROOT, "build", "os8088-360.img")
PROBE = os.path.join(ROOT, "tests", "dostrap", "dwbig.asm")
MACH = "os8088_5150_cga_gla"
DEFKB = 128
STEPKB = 16
IMGPARA = 136 * 64
MODPARA = 32 + IMGPARA                   # e_cp pages, in paragraphs
MINAL = 0x400
DER_FIT = 7
LABEL = "Est DOS Ram (Current Machine):"
SHOTS = os.environ.get("OS88_DOSWGROW_SHOTS")


def fail(msg):
    print("doswgrow: FAIL: %s" % msg)
    sys.exit(1)


def until(m, cond, what, limit):
    try:
        os88marty.until(m, lambda _m: cond(), what, poll=0.2, limit=limit)
        return True
    except os88marty.MartyError:
        return False


def shot(m, name):
    if SHOTS:
        os88marty.write_png_rgb(os.path.join(SHOTS, name), *m.fbuf())


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    tmp = tempfile.mkdtemp(prefix="doswgrow")
    try:
        exe = os.path.join(tmp, "DWBIG.EXE")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", exe, PROBE],
                       check=True)
        disk = os.path.join(tmp, "dwgrow360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", exe], check=True,
                       stdout=subprocess.DEVNULL)
        run(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswgrow: ok")


def run(disk):
    with os88ui.boot(SYS, apps=disk, machine=MACH) as ui:
        m = ui.m
        dm = dosmap.package()
        mo = os88mouse.Mouse(marty=m)
        # The double-click runs it once on the full screen, where it fits;
        # a key ends it and the DOS window stays, the program in its path box.
        if not ui.path("B:/DWBIG.EXE"):
            fail("could not launch DWBIG.EXE")
        os88marty.pace(m, 3)
        m.type_text("q")
        os88marty.pace(m, 2)
        os88marty.settle(m)
        pseg = dosmap.instance(m)
        base = dosmap.lin(m, pseg)

        def w(name):
            return int.from_bytes(dosmap.cellread(m, dm, base, name, 2),
                                  "little")

        def b(name):
            return dosmap.cellread(m, dm, base, name, 1)[0]

        def marn():
            return m.read(base + dm["dos_marn"], 9).split(b"\0")[0] \
                .decode("latin-1")

        def rows():
            scr = m.read(base + dm["con_scr"], 4000)
            return ["".join(chr(scr[r * 160 + i]) if 32 <= scr[r * 160 + i]
                            < 127 else " " for i in range(0, 160, 2)).rstrip()
                    for r in range(25)]

        if b("dos_err"):
            fail("the full-screen run was refused ([dos_err] %d) - the "
                 "fixture should fit the bracket" % b("dos_err"))

        # --- 1: the figure ---------------------------------------------------
        mo.click(*dosmap.centre(m, pseg, dm, "dos_erect"))
        os88marty.settle(m)
        full = marn()
        mo.click(*dosmap.centre(m, pseg, dm, "dos_mwin"))
        os88marty.settle(m)
        win = marn()
        shot(m, "doswgrow_setup.png")
        # ...and unticked again the `+` is gone from the GLASS too: the row is
        # one opaque run, and a run a cell shorter would leave it standing
        mo.click(*dosmap.centre(m, pseg, dm, "dos_mwin"))
        os88marty.settle(m)
        again = marn()
        x = w("dos_mx") + len(LABEL + again.rstrip("+ ")) * 8
        y = w("dos_my") + dm["DOS_MARNY"]
        px = os88marty.crop_rgb(m, x, y, 8, 8)
        ink = sum(1 for i in range(0, len(px), 3) if px[i] < 128)
        if again != full or ink:
            fail("unticked, the figure reads %r (was %r) and the cell after "
                 "its KB has %d dark pixels - the window's `+` is still on "
                 "the glass" % (again, full, ink))
        mo.click(*dosmap.centre(m, pseg, dm, "dos_mwin"))
        os88marty.settle(m)
        print("doswgrow: 1 Setup reads %r on the full screen, %r in a window"
              % (full, win))
        if win.strip() != "%dKB+" % DEFKB:
            fail("with `Run in a window` ticked and no limit the figure "
                 "should be %dKB+ - the window's default and the + that an "
                 "Automatic limit grows (SPEC.md 96.54.2.1); it is %r"
                 % (DEFKB, win))
        if full.strip().endswith("+") or \
                int(full.strip().rstrip("KB")) <= DEFKB:
            fail("the full screen's figure %r should be the arena, more than "
                 "the window's %d and with no +" % (full, DEFKB))

        # --- 2: Run, and it runs ---------------------------------------------
        ui.menu_pick("Program", "Run")
        if not until(m, lambda: b("dvw_run") == 1 or b("dos_err"),
                     "the run", 60):
            fail("Run with the box ticked reached neither the worker nor a "
                 "refusal")
        if b("dvw_run") != 1:
            fail("the run was REFUSED, [dos_err] %d (DER_FIT is %d): an "
                 "Automatic limit should have grown to the program (SPEC.md "
                 "96.54.2.1) - console says %r"
                 % (b("dos_err"), DER_FIT, rows()[-4:]))
        if not until(m, lambda: any("DWBIG READY" in r for r in rows()),
                     "its line", 30):
            fail("the program is on its worker but never printed: %r"
                 % rows()[-4:])
        shot(m, "doswgrow_run.png")
        cap, akb = w("dvw_cap"), w("dos_akb")
        line = [r for r in rows() if "DWBIG READY" in r][-1]
        para = int(line.split("READY")[1].split()[0])
        print("doswgrow: 2 it runs in the window: cap %dK, arena %dK, its "
              "block %d paragraphs (%r)" % (cap, akb, para, line.strip()))
        if cap <= DEFKB or cap % STEPKB:
            fail("the cap is %dK - it should have grown past %d in steps of "
                 "%d" % (cap, DEFKB, STEPKB))
        if akb != cap:
            fail("the arena is %dK against a cap of %dK on a machine with "
                 "room for both" % (akb, cap))
        if para < IMGPARA + MINAL:
            fail("the program's block is %d paragraphs - under its image "
                 "and MINALLOC, %d" % (para, IMGPARA + MINAL))
        # what the loader needed, in paragraphs of ARENA: the larger fit test
        # (the file, or image + MINALLOC) and the PSP, plus what the arena
        # spends before the block - read off the run, cap*64 less the block
        need = max(MODPARA, IMGPARA + MINAL) + 16 + (cap * 64 - para)
        if not (cap - STEPKB) * 64 < need <= cap * 64:
            fail("the cap is %dK for a program needing %d paragraphs of "
                 "arena - it should be that rounded UP to a %dK step, and "
                 "not a step more" % (cap, need, STEPKB))
        m.type_text("q")
        if not until(m, lambda: b("dvw_run") == 0, "its end", 20):
            fail("`q` did not end the windowed program")
        os88marty.settle(m)

        # --- 3: a limit of the user's own is a decision ------------------------
        # TYPED, not poked: Run commits the field (dos_mem_take), so a poked
        # [dos_memkb] is parsed back to the empty field's 0 before it is read
        mo.click(*dosmap.centre(m, pseg, dm, "dos_erect"))
        os88marty.settle(m)
        mo.click(*dosmap.centre(m, pseg, dm, "dos_mln"))
        os88marty.settle(m)
        m.type_text(str(DEFKB))
        os88marty.settle(m)
        if w("dos_memkb") != DEFKB or marn().strip() != "%dKB" % DEFKB:
            fail("typing %d into the limit left [dos_memkb] %d and the "
                 "figure %r - a typed limit is the window's figure, no +"
                 % (DEFKB, w("dos_memkb"), marn()))
        shot(m, "doswgrow_typed.png")
        ui.menu_pick("Program", "Run")
        if not until(m, lambda: b("dos_err") == DER_FIT or b("dvw_run"),
                     "the refusal", 60):
            fail("with a limit of %dK typed the run neither refused nor ran"
                 % DEFKB)
        if b("dvw_run"):
            fail("with a limit of %dK typed the program RAN - the user's own "
                 "number must stand (SPEC.md 96.54.2.1)" % DEFKB)
        os88marty.settle(m)
        said = [r for r in rows() if "DWBIG.EXE:" in r]
        if not said:
            fail("the refusal reached no console line: %r"
                 % [r for r in rows() if r.strip()])
        print("doswgrow: 3 a typed %dK limit refuses it, as before: %r"
              % (DEFKB, said[-1].strip()))


if __name__ == "__main__":
    main()
