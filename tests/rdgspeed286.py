#!/usr/bin/env python3
"""RealDMG's game speed on 86Box's 286, Super Mario Land and Kirby, in its
window and full screen.

    python3 tests/rdgspeed286.py [--vm 286] [--mem 2048] [--secs 20] [--only sml|kirby]

docs/plans/REALDMG-PLAN.md wave 4's measurement. For each game and each of
the two writers, tests/rdgcore/rdgwin.py boots vm/<VM> (the ami286 at 12.5 MHz
by default) with --mem KB of RAM, opens the game from a B: disk, starts it, holds Right and reads
the game's frames against 20 emulated seconds - and the frames drawn a
second off the package's own F1 line, which is what a field run reads off
the glass on the owner's machines.

The 2MB is not the VM's 1MB on purpose: there Kirby (256K) fitted as one
claim only once the recompiler had given its room back, so the row timed an
interpreted Kirby no field machine runs, and by under 1KB - the FM chip's
code was enough to tip it into "no room for ROM". With 2MB it goes to the
bank cache with the recompiler on, which is the owner's 4MB Victory's way.

It ASSERTS that the game RUNS on both writers - frames advance, at least a
tenth of real time - and REPORTS the speed; a speed is a measurement and
not a gate, because 86Box is not cycle-accurate (docs/86BOX.md).

The games are the owner's: $OS88_GB_ROMS names a directory holding SML.GB
and KIRBY.GB. The runner SKIPS the row without them (os88build.have_gbroms)
and without the automation-enabled 86Box (`python3 tools/os88box.py build`).
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMES = {
    # name: (file, the keys that start it from power-on)
    "sml": ("SML.GB", "w3000,return"),
    "kirby": ("KIRBY.GB", "w15000,return,w5000,return"),
}
LINE = re.compile(r"= ([\d.]+) fps, ([\d.]+)% of real time; overlay: (?:\d+% (\d+) fps drawn|none)")


def one(rom, press, fs, vm, secs, mem):
    cmd = [sys.executable, os.path.join(ROOT, "tests", "rdgcore", "rdgwin.py"), rom,
           "--vm", vm, "--mem", str(mem), "--press", press, "--hold", "0x14D",
           "--secs", str(secs)]
    if fs:
        cmd.append("--fs")
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=1800).stdout
    m = LINE.search(out)
    if not m:
        return None, out.strip().splitlines()[-1:] or ["no output"]
    return (float(m.group(1)), float(m.group(2)), int(m.group(3) or 0)), None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--vm", default="286")
    ap.add_argument("--mem", type=int, default=2048)
    ap.add_argument("--secs", type=float, default=20)
    ap.add_argument("--only", choices=sorted(GAMES))
    a = ap.parse_args()
    d = os.environ.get("OS88_GB_ROMS")
    if not d:
        print("rdgspeed286: SKIP - $OS88_GB_ROMS is not set")
        return 0
    fails = []
    for name, (f, press) in sorted(GAMES.items()):
        if a.only and name != a.only:
            continue
        for fs in (False, True):
            where = "full screen" if fs else "window"
            r, err = one(os.path.join(d, f), press, fs, a.vm, a.secs, a.mem)
            if r is None:
                fails.append("%s, %s: %s" % (name, where, err[0]))
                continue
            fps, pct, drawn = r
            print("rdgspeed286 %-5s %-11s %5.1f%% of real time (%5.2f game fps), %d fps drawn"
                  % (name, where, pct, fps, drawn))
            if pct < 10:
                fails.append("%s, %s: %.1f%% - the game is not running" % (name, where, pct))
    for f in fails:
        print("FAIL rdgspeed286 " + f)
    print("rdgspeed286: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
