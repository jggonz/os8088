#!/usr/bin/env python3
"""LINKS and AUTOSTART, end to end on MartyPC (SPEC.md 26.10).

    make autostarttest && python3 tests/autostart.py [--machine NAME]

build/autostart360.img is the shipped 360KB system disk plus \\AUTOSTRT
holding TASKMGR.LNK (SYSTEM\\TASKMGR.O88 on the link's own volume),
BADVER.LNK (ours, in a version 2 this kernel does not read, naming the
Calculator) and NOTES.TXT (no link at all, which Note Pad would open), and
CALC.LNK in the root - a link to B:\\APPS\\CALC.O88. tools/os88lnk.py
wrote the links. B: is build/astapps360.img, the Calculator and room: leg D
writes to it, and the shipped apps disk is a cluster from full.

  A. AUTOSTART. The boot opens every link in \\AUTOSTRT: the Task Manager's
     window is up once the boot is, with nobody touching anything - and it
     is the ONLY window: BADVER.LNK is refused on its version and NOTES.TXT
     is not a link, and neither stops what comes after it.
  B. A LINK OPENS WHAT IT NAMES. A:\\CALC.LNK, double-clicked in a Disk
     window, starts the Calculator off B: - an ABSOLUTE volume, and a
     document whose extension no program claims, which is the path every
     document open takes (assoc_run_x).
  C. EITHER SHIFT HELD THROUGH THE BOOT SKIPS IT: `Autostart skipped` on the
     bar, and no Task Manager after ten more guest seconds.
  D. AN ALT-DROP MAKES A LINK. B:\\APPS\\CALC.O88 dragged onto its own '..'
     with Alt held: nothing moves, B:\\CALC.LNK appears, and opening it
     starts the Calculator - CTRL.DRV's writer and the kernel's reader
     agreeing about the 50 bytes.

Broken on purpose and watched go red: as_boot's folder test `jz .out`
made `jmp .out` (no autostart at all) takes A and C red; its Shift test
`jnz .skip` made `jz .skip` takes A and C red; fm_dgdrop's `jz .noalt` made
`jmp .noalt` takes D red alone (the drag MOVES the package).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty                                          # noqa: E402
import os88ui                                             # noqa: E402

IMG = os.path.join(ROOT, "build", "autostart360.img")
APPS = os.path.join(ROOT, "build", "astapps360.img")   # B:, with room
FAILS = []


def check(what, ok, more=""):
    print("  %s  %s %s" % ("ok  " if ok else "FAIL", what, more))
    if not ok:
        FAILS.append(what)


def waited(fn):
    """fn()'s answer, or None when its wait ran out - a leg that fails is a
    FAIL line and the next leg still runs, rather than one traceback that
    hides what the other three would have said."""
    try:
        return fn()
    except os88ui.UIError as e:
        print("        (%s)" % str(e).split(".")[0])
        return None


def boot(machine, shift=False):
    """A machine on the gate disk, run to the end of the boot - which on a
    disk that autostarts a WINDOW is not a still desktop, so `os88ui.boot`'s
    picture settle is the wrong gate: the kernel's own end-of-boot word is
    the right one (`up`)."""
    cm = os88marty.launch(IMG, apps=APPS,
                          machine=os88marty.machine(machine, None), boot=False)
    m = cm.__enter__()
    ui = os88ui.UI(m)
    m.run()
    if shift:
        os88marty.pace(m, 3.0)          # past POST, inside the loading screen
        m.key("ShiftLeft", down=True, up=False)
    ui.up()
    os88marty.no_saver(m)
    return cm, ui


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    for f in (IMG, APPS):
        if not os.path.exists(f):
            sys.exit("autostart: %s is not built - make autostarttest" % f)

    print("A. autostart")
    cm, ui = boot(a.machine)
    try:
        w = waited(lambda: ui.wait_window("Task Manager", limit=60.0))
        check("the Task Manager was started by \\AUTOSTRT\\TASKMGR.LNK",
              w is not None)
        txt, on = ui.toast()
        check("...and nothing said it was skipped",
              "skipped" not in txt.lower(), "(toast %r)" % txt)
        check("...and BADVER.LNK (version 2) and NOTES.TXT started nothing",
              ui.titles() == ["Task Manager"], "(%r)" % ui.titles())
        if w is not None:
            ui.close(w)

        print("B. a link opens what it names")
        ui.open_drive("A")
        got = ui.open("CALC.LNK")
        check("A:\\CALC.LNK started the Calculator off B:",
              "Calculator" in ui.titles(), "(%r)" % got)
    finally:
        cm.__exit__(None, None, None)

    print("C. Shift held through the boot")
    cm, ui = boot(a.machine, shift=True)
    try:
        t = waited(lambda: ui.wait_toast(says="Autostart skipped",
                                         limit=60.0))
        check("the bar says so", t is not None, "(%r)" % (ui.toast(),))
        os88marty.pace(ui.m, 10.0)
        check("...and nothing was started", "Task Manager" not in ui.titles(),
              "(%r)" % ui.titles())
        ui.m.key("ShiftLeft", down=False, up=True)

        print("D. an Alt-drop makes a link")
        w = ui.path("B:/APPS")
        ui.settle()
        rows = ui.listing(w)
        up = [i for i, (_n, ty) in enumerate(rows) if ty == 3][0]
        src = [i for i, (nm, _t) in enumerate(rows) if nm == "CALC.O88"][0]
        sx, sy = ui.row_xy(w, src)
        ux, uy = ui.row_xy(w, up)
        ui.m.key("AltLeft", down=True, up=False)
        os88marty.pace(ui.m, 0.05)
        try:
            ui.mo.drag(sx, sy, ux, uy, settle=0)
            os88marty.pace(ui.m, 1.5)   # ALT IS READ AT THE DROP, not the
        finally:                        # press - held until it is spent
            ui.m.key("AltLeft", down=False, up=True)
        ui.settle()
        names = [n for n, _t in ui.listing(w)]
        check("CALC.O88 is still in B:\\APPS", "CALC.O88" in names,
              "(%r)" % names)
        w = ui.path("B:/")
        ui.settle()
        names = [n for n, _t in ui.listing(w)]
        check("B:\\CALC.LNK was written", "CALC.LNK" in names, "(%r)" % names)
        if "CALC.LNK" in names:
            ui.open("CALC.LNK")
            check("...and it starts the Calculator",
                  "Calculator" in ui.titles(), "(%r)" % ui.titles())
    finally:
        cm.__exit__(None, None, None)

    if FAILS:
        sys.exit("autostart: %d FAILED: %s" % (len(FAILS), "; ".join(FAILS)))
    print("autostart: ok")


if __name__ == "__main__":
    main()
