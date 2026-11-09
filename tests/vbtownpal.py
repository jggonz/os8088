#!/usr/bin/env python3
"""A clip's OWN PALETTE on VGA (SPEC.md 126.3.3): every colour its own,
where it starts and ends on black over the whole screen.

    python3 tests/vbtownpal.py

The demo battle (its scenes for the idle loop and the actions) given VIDEO
for its title - ffmpeg's colour bars, left by the fade - and for its
opening - a warm gradient, over the whole screen, that fades to black - on
VGA4 at 16 colours:

  1. the autodetection: the title (left by the fade) and the full-screen
     opening (from the title's fade to its own black) have their own; an
     action's clip has not ("played from the menu");
  2. the build: those two flagged CF_PAL, each first record loading all
     15 colours but black; the opening's seam onto the idle loop puts the
     shared back (os88vbt.verify holds every such load);
  3. their colours are THEIR OWN: the title's palette nearer its footage
     than the shared (its error in CIELAB against the shared's);
  4. the clip's choice: "shared" on the title takes it back to the shared
     (no flag, no whole load); "own" on an action gives it one;
  5. the opening with the menu (not full) is never automatic;
  6. the Build tab's palette viewer shows a clip with its own its OWN
     colours (the load its first record carries), not the shared.
BROKEN ON PURPOSE (the seam's load left out) the build raises.
"""
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(ROOT, "tools")]
import os88vbt as B         # noqa: E402
import vbtana as A          # noqa: E402
import vbtproj as P         # noqa: E402

fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def video(path, src):
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                    "-i", src, "-t", "3", "-pix_fmt", "yuv420p", path],
                   check=True)


def project(tmp, name, bars, warm, cool):
    vbp = os.path.join(tmp, name + ".vbp")
    B.demo_project(vbp, bars=2)
    p = P.Project.load(vbp)
    p.set(("target", "renditions"), ["vga4"])
    p.set(("target", "vga_colours"), 16)
    p.add_source("bars", path=bars)
    p.add_source("warm", path=warm)
    t = p.d["clips"]["title"]
    t["source"], t["in"], t["out"] = "bars", 0, 40
    p.d["title"]["loop"] = [0, 40]
    p.add_clip("intro", "warm", (0, 44))   # A FULL-SCREEN OPENING first
    p.add_opening("intro")                  # (the last, the demo's, joins
    p.move_opening(len(p.d["openings"]) - 1, -len(p.d["openings"]) + 1)
    p.d["openings"][0]["full"] = True       # the idle loop with the menu)
    act = next(iter(p.d["actions"].values()))["pools"]
    act = next(c for pool in act.values() for c in pool)
    a = p.d["clips"][act]                   # AN ACTION'S CLIP on video too
    p.add_source("cool", path=cool)
    a["source"], a["in"], a["out"] = "cool", 0, min(30, a["out"] - a["in"])
    p.save()
    return p


def first_loads(bd, c):
    r = bd.stream(c)[0][B.REC_HDR:]
    return r[0] & 0x7F if r and r[0] & B.DAC_SUB else 0


def main():
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        bars = os.path.join(tmp, "bars.mp4")
        warm = os.path.join(tmp, "warm.mp4")
        video(bars, "smptebars=size=320x240:rate=15")
        video(warm, "gradients=size=320x240:rate=15:c0=orange:c1=darkred:"
              "seed=3,fade=t=out:st=2:d=0.9")
        cool = os.path.join(tmp, "cool.mp4")
        video(cool, "gradients=size=320x240:rate=15:c0=navy:c1=teal:seed=5")
        p = project(tmp, "a", bars, warm, cool)
        op = "intro"
        act = next(c for pool in next(iter(p.d["actions"].values()))[
            "pools"].values() for c in pool)
        auto = A.own_palettes(p)
        ok(auto.get("title", (0,))[0] and auto.get(op, (0,))[0] and
           not auto.get(act, (False,))[0],
           "1. automatic: the title (%s) and the opening (%s) their own; "
           "the action %s not (%s)" % (auto.get("title", (0, "-"))[1],
                                       auto.get(op, (0, "-"))[1], act,
                                       auto.get(act, (0, "-"))[1]))
        data = B.build_project(p, kind="vga4")
        bd = B.Bundle(data)
        tc = bd.clips[bd.title]
        oc = bd.clips[bd.openings[0]]
        ok(tc["flags"] & B.CF_PAL and oc["flags"] & B.CF_PAL and
           first_loads(bd, tc) == 15 and first_loads(bd, oc) == 15,
           "2. built: the title and the opening flagged, each first record "
           "loading 15 colours (%d, %d); verify held the seam's" % (
               first_loads(bd, tc), first_loads(bd, oc)))
        plan = A.vga_plan(p)
        w, _, fh = A.rend_size(p, "vga4")
        fr = A.rgb_frames(p, "bars", w, fh, "vga4")[:40]
        px = fr.reshape(-1, 3)[::37].astype(float)

        import os88cgacomp as cc

        def err(pal):
            lab = cc.srgb2lab(np.array(px, np.float64))
            pl = cc.srgb2lab(np.array(pal, np.float64))
            return float(np.sqrt(((lab[:, None] - pl[None]) ** 2).sum(2)
                                 .min(1)).mean())
        e_own, e_sh = err(plan["iso"]["title"]), err(plan["shared"])
        ok(e_own < 0.8 * e_sh, "3. the title's own palette nearer its bars "
           "than the shared: %.1f against %.1f (CIELAB)" % (e_own, e_sh))
        out = os.path.join(tmp, "BATTLEV.VBT")
        open(out, "wb").write(data)
        import os88vbtgui as G
        v = G.vga_colours(out, clip=bd.openings[0])
        want = [tuple(c) for c in plan["iso"]["intro"]]
        got = [tuple(x["rgb"]) for x in v["cols"]]
        near = all(max(abs(a - b) for a, b in zip(g, w_)) <= 4
                   for g, w_ in zip(sorted(got), sorted(want)))
        ok(v.get("whole") and near and all(x["own"] for x in v["cols"][1:]),
           "6. the palette viewer shows intro's OWN 16, as its first record "
           "loads them (not the shared)")
        p.set(("clips", "title", "palette"), "shared")
        p.set(("clips", act, "palette"), "own")
        d2 = B.build_project(p, kind="vga4")
        bd2 = B.Bundle(d2)
        ac = [c for c in bd2.clips if c["name"].strip() == act[:8].upper()]
        ok(not bd2.clips[bd2.title]["flags"] & B.CF_PAL and ac and
           ac[0]["flags"] & B.CF_PAL,
           "4. chosen: 'shared' on the title takes its own away, 'own' "
           "on %s gives it one" % act)
        p.set(("clips", "title", "palette"), None)
        p.set(("clips", act, "palette"), None)
        p.d["openings"][0]["full"] = False
        on, why = A.own_auto(p, op)
        ok(not on and "band" in why,
           "5. an opening with the menu is never automatic (%s)" % why)
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
