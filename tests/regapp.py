#!/usr/bin/env python3
"""Every shipped package that hires a worker has DECLARED (SPEC.md 66.6.1/66.6.2).

    make && make build/regapp360.img && python3 tests/regapp.py

WHAT THIS IS AND IS NOT. `tests/regwork.py` proves the kernel end to end - a
region moves out from under its worker, the worker is re-entered and keeps
running - against `tests/pinme`, and it carries the kernel A/B that says the
row is load-bearing. Once a package's two declarations are in the kernel's
tables the path is that same path, so THIS row does not re-prove it. It proves
the half that is per-package and that fails SILENTLY:

    a declaration the owner fence refused is indistinguishable, from inside
    the package, from one that took (SPEC.md 66.5.6.2)

which is exactly how this project once shipped a table saying MOVABLE for a
cache that was pinned. Five packages declare, between them 165,765 bytes of
region that was pinned for the life of a session before this, and a typo, a
clobbered DX or a macro used before the window exists would leave any of them
silently back where it started.

SO IT READS THE KERNEL'S OWN TABLES, per package: `MC_RLOC` out of `mem_tab`
for the region, `inst_restart` out of the side table for the restart point, and
`I_TASK` out of the instance record to show the worker is real - because
without a worker the restart declaration is inert and the row would be
asserting nothing.

WHY IT DOES NOT ALSO FORCE A MOVE, written down because it was tried. The
arena needed is regmove.py's - a pinned wall above the subject and every free
run smaller than the ask - and with one app open on a 640KB machine the
filler's forcing ask is simply GRANTED out of a hole that was there all along:
measured twice, at 93KB and at 24KB, a compaction that never had to happen and
a region that correctly did not move. Building that arena per app is five
emulator rounds to re-prove one kernel path. `regwork` proves the path;
this proves the packages.

TANK ATTACK IS HERE AND IS NOT IN ANY MOVE ROW: its worker redraws every
frame, so `settle` can never return and a picture-compare after a move is
false of a machine working perfectly.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88fixture                                      # noqa: E402
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sym                                          # noqa: E402
import dispcp                                           # noqa: E402
import sheetmove                                        # noqa: E402

DISK = "build/regapp360.img"
u16, claims, uncovered = sheetmove.u16, sheetmove.claims, sheetmove.uncovered
pkg_seg, park = sheetmove.pkg_seg, sheetmove.park
I_RECSZ, I_SPTR = os88geom.I_RECSZ, os88geom.I_SPTR
INST_MAX = os88geom.INST_MAX

# file -> the window title its package opens, which is how a row finds it
APPS = {"word":    ("WORD.O88", "Word"),
        "tank":    ("TANK.O88", "Tank"),
        "ftpd":    ("FTPD.O88", "FTP"),
        "browser": ("BROWSER.O88", "Browser"),
        "audio":   ("AUDIO.O88", "Audio")}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    S = os88sym.linear
    os88fixture.need(DISK)

    with os88marty.launch("build/os8088-360.img", apps=DISK,
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        os88marty.no_saver(m)       # SPEC.md 79 DRAWS, so a settle can never
        mo = os88mouse.Mouse(marty=m)   # return while it is up

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)
        os88marty.settle(m)
        disk = dispcp.win_rect(m, S, dslot)[:2]

        def raise_disk():
            dw = [w for w in os88geom.windows(m, S) if w.i == dslot][0]
            pt = uncovered(m, S, dw)
            if pt is None:
                raise RuntimeError("the Disk window is wholly covered")
            mo.click(*pt)
            os88marty.settle(m)

        bad, checked = 0, 0
        for name in sorted(APPS):
            fname, title = APPS[name]
            before = set(w.i for w in os88geom.windows(m, S) if w.visible)
            raise_disk()
            dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=fname)
            time.sleep(10)
            # NO settle: Tank Attack redraws every frame for ever, and this
            # row reads guest MEMORY rather than the glass, so it does not
            # need the screen to hold still - which is what lets one row
            # cover an animating package at all.
            new = [w for w in os88geom.windows(m, S)
                   if w.visible and w.i not in before]
            if not new:
                print("FAIL %-8s never opened a window" % name)
                bad += 1
                continue
            w = new[0]
            seg = u16(m.read(os88geom.winptr(m, w.i, S) + os88geom.W_SEG, 2))

            reg = [c for c in claims(m, S) if c[0] == seg]
            rloc = reg[0][3] if reg else 0
            kb = (reg[0][1] // 64) if reg else 0
            slot, task = None, None
            t = m.read(S("inst_tab"), INST_MAX * I_RECSZ)
            for i in range(INST_MAX):
                if u16(t, i * I_RECSZ + I_SPTR) == seg:
                    slot, task = i, t[i * I_RECSZ + os88geom.I_TASK]
            rst = (u16(m.read(S("inst_restart") + slot * 2, 2))
                   if slot is not None else 0)
            worker = task is not None and task != 0xFF
            # THE REGION DECLARATION IS UNCONDITIONAL and the restart one is
            # not: a package that has not hired a worker yet is movable on
            # I_TASK = 0xFF alone (SPEC.md 66.6.1), and demanding the restart
            # point of it would fail a package that is behaving correctly.
            # Audio hires only when playback starts and ftpd only when the
            # card is up, so on this machine - MartyPC has no NIC - neither
            # ever does, and that is the case that used to declare NOTHING.
            ok = bool(rloc) and (bool(rst) or not worker)
            checked += 1
            bad += not ok
            print("  %-8s %s  region %04x %3dKB  MC_RLOC=%04x  "
                  "inst_restart=%04x  I_TASK=%s%s"
                  % (name, "ok  " if ok else "FAIL", seg, kb, rloc, rst,
                     task if task is not None else "?",
                     "" if worker else "  (no worker yet: movable already)"))
            if not ok:
                if not rloc:
                    print("           OS88_REGION_MOVABLE did not take - the "
                          "owner fence refused it and the package cannot tell")
                if worker and not rst:
                    print("           OS88_WORKER_RESTARTABLE did not take, "
                          "so the region is pinned for as long as the worker "
                          "lives")

        print("regapp: %d package(s) checked, %d problem(s)" % (checked, bad))
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
