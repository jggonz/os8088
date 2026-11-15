#!/usr/bin/env python3
"""wafiglod - WireArena's SMALL FIGURE begins where the detail level says
(docs/plans/WIREARENA-ARENAS-PLAN.md 5, WIREARENA-HEIGHTS-PLAN.md 9.11).

A figure small on the screen is drawn as J: seven joints, five straight
limbs, no head (LK_FEW). How small is the detail level's (wa_flodv, CONFIG's:
wa_levtab's last column): under 24 half-rows a unit x 256 at Medium and below
- from 464 units ahead at the default 75 degrees - and under 16 at High, from
696; Very High draws every figure whole. The owner chose it on trial builds
(2026-11-15): at that distance there are too few characters to show more.

One figure before the eye in the open arena at three depths - 400, 560 and
760 units ahead - through tools/waposes.py at four levels, an emulator each,
the still frame single-stepped: the LIMBS walked a pose (wa_flgo's entries: 9
a whole figure, 5 a small one) say which was drawn.

  Low, Medium  9 + 5 + 5   whole at 400, small at 560 and at 760
  High         9 + 9 + 5   whole at 560 too
  Very High    9 + 9 + 9   whole everywhere

The level is set by its key in a running match, so a threshold that stayed
the last level's (CONFIG not synced) reads as the wrong sum too.

    python3 tests/wafiglod.py
"""
import sys, os, json, subprocess, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

AWAY = dict(x=300, y=40, z=0, ang=0, anim=4, nfr=8, ph=0, flash=0, dead=0)
DEPTHS = (400, 560, 760)
POSES = [dict(px=300, py=1024, ang=0, pitch=0, feet=0, jump=0,
              figs=[dict(AWAY, x=300 + d, y=1024, ang=300, ph=5), AWAY, AWAY])
         for d in DEPTHS]
WANT = ((2, "Low", 19), (3, "Medium", 19), (4, "High", 23), (5, "Very High", 27))


def main():
    d = tempfile.mkdtemp()
    pin = os.path.join(d, "poses.json")
    json.dump(POSES, open(pin, "w"))
    runs = []
    for lev, name, want in WANT:
        prof = os.path.join(d, "p%d.json" % lev)
        cmd = [sys.executable, os.path.join(ROOT, "tools/waposes.py"), "record",
               "--arena", "1", "--width", "40", "--level", str(lev), "--poses", pin,
               "--profile", prof, "--out", os.path.join(d, "o%d.json" % lev)]
        runs.append((lev, name, want, prof,
                     subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT, text=True)))
    fails = 0
    for lev, name, want, prof, q in runs:
        log = q.communicate()[0]
        if q.returncode or not os.path.exists(prof):
            print(log[-2000:])
            print("wafiglod: FAIL - waposes could not record level %d" % lev)
            return 1
        p = json.load(open(prof))
        limbs, figs = p["cnt"].get("wa_flgo", 0), p["cnt"].get("wa_figure", 0)
        bad = []
        if " 0 DIVIDE FAULTS" not in log:
            bad.append("a divide fault: %s" % log.strip().splitlines()[-1])
        if figs != len(DEPTHS):
            bad.append("%d figures drawn, not %d: the poses ask nothing" % (figs, len(DEPTHS)))
        if limbs != want:
            bad.append("%d limbs walked, not %d: a figure whole where it should be small, "
                       "or small where whole" % (limbs, want))
        print("  %-4s  %-9s %2d limbs over the three depths (%d + %d + %d wanted)%s"
              % ("FAIL" if bad else "ok", name, limbs,
                 9, 9 if want >= 23 else 5, 9 if want >= 27 else 5,
                 "".join("\n          " + b for b in bad)))
        fails += bool(bad)
    if fails:
        print("wafiglod: FAIL - %d level(s)" % fails)
        return 1
    print("wafiglod: ok - a figure 400, 560 and 760 units ahead: small from 464 at Low "
          "and Medium, from 696 at High, never at Very High")
    return 0


if __name__ == "__main__":
    sys.exit(main())
