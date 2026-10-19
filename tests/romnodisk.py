#!/usr/bin/env python3
"""romnodisk - with the ROM in, the system disk can be OUT of the drive.

docs/plans/ROM-PLAN.md wave 4, SPEC.md 2.10.5. kern_small reads its Standard
File dialog (FDLG.DRV), Cut/Copy/Paste (FILECP.DRV), the Control Panel
(CTRL.DRV) and the Task Manager (SYSTEM/TASKMGR.O88) off the SYSTEM disk when
they are first asked for. On a one-drive 128KB machine that is the disk the
user has just taken out to put a data floppy in - so a Save As onto that
floppy refuses. The ROM carries all four, and mod_need and ui_sys_open ask it
before they ask the drive.

Two boots of `make small`'s disks on the 128KB floor machine, A/B:

  A. WITH the ROM `make rom-small` cuts (Task Manager included). The desktop
     comes up off the system disk; then A: is swapped for a scratch floppy
     with no SYSTEM folder and no module on it, and the guest is given the
     motor-off time it needs to notice (SPEC.md 18.9.1). Then:
       - a Save As chooser opens (muptest's button Two) and FDLG.DRV's row is
         held while it is up - the image came out of F4000-FDFFF;
       - the Control Panel opens, and CTRL.DRV's row is held;
       - Apple > Task Manager opens a "Task Manager" window.
  B. the same session WITHOUT the ROM - the negative control
     (docs/WRITING-TESTS.md 1). The same three gestures, the same swapped
     floppy, and all three REFUSE: no chooser, no panel, no Task Manager.
     Without B, A's three windows would say nothing about where the bytes
     came from.

    make small smallapps bench && python3 tests/romnodisk.py
"""

import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")
import os88build   # noqa: E402
os88build.use_build("build/smallk")
import os88marty   # noqa: E402
import os88sym     # noqa: E402
import os88ui      # noqa: E402
import dispcp      # noqa: E402
from os88fixture import need  # noqa: E402

MACHINE = "os8088_5150_cga_128k"
SYS = os.path.join(ROOT, "build", "small360.img")
MUP = os.path.join(ROOT, "build", "muptest.img")
TM = os.path.join(ROOT, "build", "smallapp", "taskmgr.o88")
EQ = os88sym.equates(("KERN_SMALL",))   # MOD_CTRL, MODR_SIZE: mod.inc's
SWAP_GUEST = 3.0        # SPEC.md 18.9.1's 2.03 s motor-off, with margin


def held(ui, row):
    return int.from_bytes(ui.m.read(ui._S("mod_tab") + row
                                    * EQ["MODR_SIZE"], 2), "little")


def session(rom, data):
    """Boot, swap `data` into A:, try the three; what opened."""
    out = {}
    with os88ui.boot(SYS, apps=MUP, machine=MACHINE, rom=rom,
                     label="romnodisk") as ui:
        m = ui.m
        out["cs"] = int.from_bytes(m.read(ui._S("api_coldseg"), 2), "little")
        ui.path("B:/MUPTEST.O88")
        ui.settle()
        mup = ui.window("MupTest")
        cx, cy = mup.content[:2]
        two = (cx + 100 + 31, cy + 40 + 9)     # tests/fdlgdrop.py's button

        m.mount(0, data)                        # the system disk OUT
        m.advance(frames=int(SWAP_GUEST * 60))   # GUEST time: ~60 Hz on CGA
        m.run()                                 # advance leaves it paused

        # --- the Standard File dialog ------------------------------------
        ui.mo.click(*two, settle=0)
        try:
            w = ui.chooser()
            out["fdlg"] = held(ui, EQ["MOD_FDLG"]) != 0
            ui.chooser_cancel()
        except os88ui.UIError:
            out["fdlg"] = False
        ui.settle()

        # --- the Control Panel -------------------------------------------
        try:
            dispcp.open_panel(m, ui.mo, ui._S, os88marty.settle, page=None)
            out["ctrl"] = (dispcp._cp_win(m, ui._S) is not None
                           and held(ui, EQ["MOD_CTRL"]) != 0)
        except RuntimeError:
            out["ctrl"] = False
        if out["ctrl"]:
            ui.close(ui.window("Control Panel"))
        ui.settle()

        # --- the Task Manager --------------------------------------------
        try:
            ui.menu_pick("Apple", "Task Manager")
            ui.wait_window("Task Manager")
            out["tm"] = True
        except os88ui.UIError:
            out["tm"] = False
    return out


def main():
    need("build/muptest.img", "build/small360.img",   # RELATIVE: the
         "build/smallapp/taskmgr.o88")                # runner matches wants=
    fails = []

    def want(cond, what):
        print("   %s  %s" % ("ok  " if cond else "FAIL", what))
        if not cond:
            fails.append(what)

    with tempfile.TemporaryDirectory(prefix="romnodisk-") as tmp:
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                        "os88rom.py"), "kernel", "--build",
                        os.path.join(ROOT, "build", "smallk"), "--small",
                        "--pkg", TM, "--out", tmp], check=True)
        rom = os.path.join(tmp, "osrom-small.bin")
        # the data floppy: a disk with no SYSTEM folder and no module on it.
        # muptest.img is one, and a COPY because the guest may write it
        data = os.path.join(tmp, "data.img")

        print("A: the ROM, the system disk out")
        shutil.copyfile(MUP, data)
        a = session(rom, data)
        want(a["cs"] == 0xF400, "[api_coldseg] = %04X, adopted" % a["cs"])
        want(a["fdlg"], "a Save As chooser opened, FDLG.DRV out of the ROM")
        want(a["ctrl"], "the Control Panel opened, CTRL.DRV out of the ROM")
        want(a["tm"], "the Task Manager opened, TASKMGR.O88 out of the ROM")

        print("B: no ROM, the system disk out (the control)")
        shutil.copyfile(MUP, data)
        b = session(False, data)    # False: $OS88_ROM must not reach it
        want(not b["fdlg"], "no chooser - FDLG.DRV is on the disk that left")
        want(not b["ctrl"], "no Control Panel - nor is CTRL.DRV")
        want(not b["tm"], "no Task Manager - nor is SYSTEM/TASKMGR.O88")

    if fails:
        sys.exit("romnodisk: %d failed" % len(fails))
    print("romnodisk: ok")


if __name__ == "__main__":
    main()
