#!/usr/bin/env python3
"""System Check's release checks on the two 1bpp adapters (SPEC.md 118.11) -
python3 tests/syscheckmono.py [--machine NAME ...]

tests/syscheck.py plants one of every breakage SPEC.md 118.1 names on a hard
disk and checks it on QEMU's VGA. This is the same planted disk on the
adapters the window's other layouts are for (SPEC.md 118.4): MartyPC's 5150
with CGA and with Hercules, each with an XT-IDE disk, the disk wrapped as a
VHD at that disk's geometry. Its AUTOSTRT link says `C: RELEASE REPORT`
(118.14.9), so the scan runs and saves SYSCHECK.TXT with nobody at the
machine - SYSTEM.CFG's breakage opens the Control Panel on top of it, and
nothing has to be clicked past. The report, read back off the VHD, must say
syscheck.py's exact totals and name every planted finding.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import syscheck as S                                        # noqa: E402
import fsckhd as H                                          # noqa: E402

MACHINES = ("os8088_5150_cga_hdd_gla", "os8088_5150_herc_hdd_gla")
GEOM = "615/4/26"                       # default_xtide.vhd's (os8088_machines)


def fixture(work):
    raw = os.path.join(work, "c.img")
    tmp = S.build_fixture(raw, arg="C: RELEASE REPORT", geometry=GEOM)
    shutil.rmtree(tmp, ignore_errors=True)
    vhd = os.path.join(work, "c.vhd")
    c, h, spt = GEOM.split("/")
    subprocess.check_call(["python3", "tools/os88hdd.py", "--cyls", c,
                           "--heads", h, "--spt", spt, "--out", vhd,
                           "--wrap", raw], cwd=ROOT,
                          stdout=subprocess.DEVNULL)
    return vhd


def one(machine, work, fails):
    import os88marty as M
    import os88ui
    vhd = fixture(work)
    m = M.launch(None, machine=machine, extra=["--mount", "hd:0:" + vhd],
                 boot=False)
    try:
        m.run()
        ui = os88ui.UI(m)
        ui.up(limit=300)
        M.no_saver(m)
        try:
            ui.wait_toast(says="SYSCHECK.TXT", limit=900)
        except Exception as ex:             # noqa: BLE001
            fails.append("%s: the report was never saved: %s" % (machine, ex))
    finally:
        m.quit()
    try:
        text = H.Disk(vhd).v.read("\\SYSCHECK.TXT").decode("latin1")
    except Exception:                       # noqa: BLE001
        fails.append("%s: no SYSCHECK.TXT on C:" % machine)
        return
    print(text)
    if "Result: 7 broken, 7 warnings" not in text:
        fails.append("%s: the report does not say 7 broken, 7 warnings"
                     % machine)
    lines = text.splitlines()
    for sev, path, word in S.EXPECT:
        if not [ln for ln in lines if ln.startswith(sev) and
                (" %s " % path) in ln and word in ln]:
            fails.append("%s: no %s line for %s (%s)"
                         % (machine, sev, path, word))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", action="append")
    a = ap.parse_args()
    fails = []
    for mach in a.machine or MACHINES:
        work = tempfile.mkdtemp(prefix="scmono-")
        try:
            one(mach, work, fails)
        finally:
            shutil.rmtree(work, ignore_errors=True)
    if fails:
        for f in fails:
            print("FAIL: " + f)
        return 1
    print("syscheckmono: every planted breakage found and reported on CGA "
          "and Hercules")
    return 0


if __name__ == "__main__":
    sys.exit(main())
