#!/usr/bin/env python3
"""wastill - WireArena's STANDING VIEW draws the moving view's picture
(docs/plans/WIREARENA-ARENAS-PLAN.md 5, "What a standing view keeps").

The renderer keeps work across frames while the view stands: a floor line's
walk (wa_flines), a low box's corners (wl_vert: the vertices stamped last
frame) and a pickup's place on the screen (wa_pickups, wa_pkc). Each is a
chance to draw LAST frame's picture where this frame's differs - a corner
taken as made that was not, a pickup left at its rows after a nod. This row
holds every kept path to the picture a moved view draws, where nothing is
kept at all: the same seeded poses of the HEIGHTS arena (2: platforms, a
flight, pickups) and the TWIN (4: floor lines, lifts, pickups) rendered
through tools/waposes.py four ways, an emulator each -

  moved   the view's key forgotten before every render: everything made
  still   the view where it was for three frames: everything kept
  nod     the pitch alone moved since the frame before
  rise    the eye's height alone moved since the frame before (a jump, a
          lift's ride) - which also changes the corners a low box wants
  blink   (the twin, three poses before the path's health pack, one pitch
          and height) no pickup there on the frame the view moved, all of
          them back on the next: a pickup's kept place is keyed by a count
          of the view's changes, which a frame that draws none must count
          too - or the pack is drawn where the pose before saw it

  picture  rows 0..22 of text VRAM, every cell, the same as the moved
           view's in each of the others
  kept     the still view's frame, single-stepped: no pickup goes through
           wa_thing's transform (its cells are drawn - wa_plotf runs), and
           no low-box corner through wa_vert (wl_vert is asked for them) -
           the paths this row is about are the ones that ran
  faults   no divide fault in any render

    python3 tests/wastill.py
"""
import sys, os, json, subprocess, tempfile, time
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

W = 40
N, SEED = 12, 7                 # the seeded poses (waposes' own, per arena)
ARENAS = ((2, "heights"), (4, "twin"))
MODES = (("moved", []), ("still", ["--still"]), ("nod", ["--nod"]), ("rise", ["--rise"]))
LANES = 4                       # emulators side by side
AWAY = dict(x=1024, y=80, z=0, ang=0, anim=4, nfr=8, ph=0, flash=0, dead=0)
BLINK = [dict(px=x, py=832, ang=0, pitch=0, feet=0, jump=0, figs=[AWAY] * 3)
         for x in (600, 680, 760)]      # the health pack at (1024, 832) ahead
A_HEALTH = 0x0C                 # its colour on a colour card (wa_pkshape)


def main():
    d = tempfile.mkdtemp()
    jobs = []
    for arena, an in ARENAS:
        for mode, flag in MODES:
            out = os.path.join(d, "%s_%s.json" % (an, mode))
            cmd = [sys.executable, os.path.join(ROOT, "tools/waposes.py"), "record",
                   "--arena", str(arena), "--width", str(W), "--n", str(N),
                   "--seed", str(SEED), "--out", out] + flag
            if mode == "still":     # (and its frame once more, stepped)
                cmd += ["--profile", os.path.join(d, "%s_prof.json" % an)]
            jobs.append([an, mode, out, cmd, None, ""])
    bp = os.path.join(d, "blink.json")
    json.dump(BLINK, open(bp, "w"))
    for mode, flag in (("blink-moved", []), ("blink", ["--blink"])):
        out = os.path.join(d, "twin_%s.json" % mode)
        jobs.append(["twin", mode, out,
                     [sys.executable, os.path.join(ROOT, "tools/waposes.py"), "record",
                      "--arena", "4", "--width", str(W), "--poses", bp, "--out", out] + flag,
                     None, ""])
    todo, live = list(jobs), []
    while todo or live:
        while todo and len(live) < LANES:
            j = todo.pop(0)
            j[4] = subprocess.Popen(j[3], cwd=ROOT, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True)
            live.append(j)
        for j in list(live):
            if j[4].poll() is not None:
                j[5] = j[4].communicate()[0]
                live.remove(j)
        time.sleep(0.2)
    fails = 0
    for an, mode, out, cmd, q, log in jobs:
        if q.returncode or not os.path.exists(out):
            print(log[-2000:])
            print("wastill: FAIL - waposes could not record the %s's %s view" % (an, mode))
            return 1
        if " 0 DIVIDE FAULTS" not in log:
            print("  FAIL  a divide fault, the %s's %s view: %s"
                  % (an, mode, log.strip().splitlines()[-1]))
            fails += 1
    rec = {(an, mode): json.load(open(out))["frames"] for an, mode, out, *_ in jobs}
    for arena, an in ARENAS:
        base = rec[(an, "moved")]
        cells = sum(1 for f in base for k in range(0, 23 * W * 2, 2)
                    if bytes.fromhex(f["vram"])[k] != 0x20)
        for mode, _ in MODES[1:]:
            diff, first = 0, None
            for i, (fa, fb) in enumerate(zip(base, rec[(an, mode)])):
                va, vb = bytes.fromhex(fa["vram"]), bytes.fromhex(fb["vram"])
                for k in range(0, 23 * W * 2, 2):
                    if va[k:k + 2] != vb[k:k + 2]:
                        diff += 1
                        first = first or (i, k // 2 // W, k // 2 % W)
            if diff:
                print("  FAIL  the %s, %s: %d cell(s) are not the moved view's - the first "
                      "in pose %d, row %d column %d" % ((an, mode, diff) + first))
                fails += 1
            else:
                print("  ok    the %s, %s: the moved view's picture, %d poses, %d cells"
                      % (an, mode, len(base), cells))
        # THE KEPT PATHS RAN: the still frame's own calls, a pose
        prof = json.load(open(os.path.join(d, "%s_prof.json" % an)))
        cnt, n = prof["cnt"], float(prof["n"])
        c = lambda name: cnt.get(name, 0) / n
        bad = []
        if c("wa_pickups") < 1 or c("wa_plotf") <= 0:
            bad.append("no pickup drawn in the still frames (wa_pickups %.2f, wa_plotf %.2f a "
                       "pose): the poses ask nothing" % (c("wa_pickups"), c("wa_plotf")))
        if c("wa_thing") > 0:
            bad.append("wa_thing entered %.2f times a pose: a pickup made again, its place "
                       "not kept" % c("wa_thing"))
        if c("wl_vert") <= 0:
            bad.append("wl_vert never asked: no low box's corner in the still frames - the "
                       "poses ask nothing")
        if c("wa_vert") > 0:
            bad.append("wa_vert entered %.2f times a pose: a low box's corner made again, "
                       "not kept" % c("wa_vert"))
        print("  %-4s  the %s's still frame: wl_vert %.1f a pose and wa_vert %.1f, wa_thing "
              "%.1f with %.1f cells plotted%s"
              % ("FAIL" if bad else "ok", an, c("wl_vert"), c("wa_vert"), c("wa_thing"),
                 c("wa_plotf"), "".join("\n          " + b for b in bad)))
        fails += bool(bad)
    # THE BLINK: the pack is in each moved picture, and where it is there in
    # the blinked one
    a, b = rec[("twin", "blink-moved")], rec[("twin", "blink")]
    packs = [sum(1 for k in range(0, 20 * W * 2, 2)
                 if bytes.fromhex(f["vram"])[k + 1] == A_HEALTH
                 and bytes.fromhex(f["vram"])[k] != 0x20) for f in a]
    diff = sum(1 for fa, fb in zip(a, b) for k in range(0, 23 * W * 2, 2)
               if bytes.fromhex(fa["vram"])[k:k + 2] != bytes.fromhex(fb["vram"])[k:k + 2])
    bad = []
    if min(packs) < 1 or len(set(f["vram"] for f in a)) < len(a):
        bad.append("the health pack's cells a pose: %r - the poses ask nothing" % packs)
    if diff:
        bad.append("%d cell(s) are not the moved view's: a pickup drawn where another "
                   "view saw it" % diff)
    print("  %-4s  the twin, blink: no pickup on the frame the view moved, then the moved "
          "view's picture (the pack's cells a pose: %r)%s"
          % ("FAIL" if bad else "ok", packs, "".join("\n          " + x for x in bad)))
    fails += bool(bad)
    if fails:
        print("wastill: FAIL - %d check(s)" % fails)
        return 1
    print("wastill: ok - the heights arena and the twin, %d poses each: a still view, a nod "
          "and a rise draw the moved view's picture cell for cell; a still frame makes no "
          "pickup's place and no low box's corner again; a pickup back after a frame with "
          "none is drawn where it is; no divide fault" % N)
    return 0


if __name__ == "__main__":
    sys.exit(main())
