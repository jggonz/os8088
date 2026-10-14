#!/usr/bin/env python3
"""fpgnotch - SPEC.md 18.91.6: dsk_xfer asks once a RUN whether either bar is
live, and skips its per-sector notch loop when neither is.

    make && python3 tests/fpgnotch.py

The skip is worth having only while it never fires when a bar IS live, and
the failure it would cause is silent: a progress widget that goes up and
never moves. So the row launches a package off B: - one job with a scale,
the widget armed - and asserts the bar LIT (`fpg_lit`, the only routine that
paints the trough's progress, SPEC.md 12.8), and that fpg_step was called
for that job's sectors. Then it opens a Disk window on B: again, a mount
whose reads arrive with NO scale armed, and counts fpg_step there: with the
skip in place the unarmed sectors never reach it.

Break on purpose: make the test always skip (`ja .notch` as `jmp .notched`)
and the launch lights nothing; take the test out and the unarmed count is
every sector the mount read.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M                                       # noqa: E402
import os88ui                                               # noqa: E402


def main():
    bad = []
    with os88ui.boot("build/os8088-360.img", apps="build/apps360.img",
                     machine="os8088_5150_cga_gla") as ui:
        m = ui.m
        with M.bp_trace(m, "fpg_lit", "fpg_step", "fpg_begin") as tr:
            calc = ui.path("B:/APPS/CALC.O88")
        lit, step = tr.count("fpg_lit"), tr.count("fpg_step")
        print("   1. a launch off B: (a scale armed): fpg_begin %d, "
              "fpg_step %d, fpg_lit %d" % (tr.count("fpg_begin"), step, lit))
        if not tr.count("fpg_begin"):
            bad.append("the launch armed no widget - this tests nothing")
        elif not lit:
            bad.append("the widget was armed with a scale and its bar never "
                       "lit: the notch loop skipped a live bar")
        ui.close(calc)
        for w in [w for w in ui.windows() if w.visible and
                  ui.fs_of(w) is not None]:
            ui.close(w)
        with M.bp_trace(m, "fpg_step", "fpg_begin", "dsk_xfer.count") as tr:
            ui.open_drive("B")
        runs = tr.count("dsk_xfer.count")
        print("   2. B: opened again: %d runs read, fpg_begin %d, fpg_step %d"
              % (runs, tr.count("fpg_begin"), tr.count("fpg_step")))
        if not runs:
            bad.append("the second open read nothing off the disk - step 2 "
                       "tests nothing")
        elif not tr.count("fpg_begin") and tr.count("fpg_step"):
            bad.append("%d fpg_step calls with no scale ever armed: the "
                       "notch loop is not asking once a run"
                       % tr.count("fpg_step"))
    for b in bad:
        print("   FAIL: %s" % b)
    print("fpgnotch: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
