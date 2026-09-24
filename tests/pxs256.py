#!/usr/bin/env python3
"""PIXELSTEIN 3D on a 256 KB 5150: the game OPENS, on the Flat rung with the
sprites as boxes and no weapon in the view (SPEC.md 97.9; review, wave 6).

    python3 tests/pxs256.py [--machine os8088_5150_cga_gla_256k] [--shots DIR]

97.9 promised that a 256 KB machine plays Flat with boxes, and nothing had
ever launched the game on one. The first launch opened NOTHING: the parts
loader's all-or-none carve (SPEC.md 20.12.4) took all 140 KB of a 147 KB
heap, optional parts included, and the level stream's fetch after it
refused the launch with a "Not enough memory" toast. The loader now holds a
reserve across op_load for the two claims a launch makes after the carve
(the level stream and the 16 KB shadow), and fetches the masters only when
they and the shadow fit. This row is the gate on that:

  (a) PXSTEIN.O88 opens a window on the 256 KB machine and draws a frame;
  (b) ...on the FLAT rung (px_rung 1) - parts 1 and 2 were given up, so
      Textured cannot be had - and with the sprite set refused (px_sprok 0,
      the sprites as boxes, 97.6);
  (c) a dog poked 1.5 tiles ahead is a candidate (drawn as a box), and no
      weapon is drawn (px_wdrawn 0xFFFF: the gun is a frame of the refused
      sprite set, px_weapon_check - review, wave 6 r2);
  (d) F takes the CGA bracket and a frame is drawn there, still on Flat.

MartyPC's os8088_5150_cga_gla_256k (GLaBIOS, CGA - tools/martypc/configs):
the two 86Box XTs of SPEC.md 97.15 are 256 KB machines, and this is the
machine an emulator here can assert on in their place.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))     # LAST, so it wins (pxslib)
import os88marty                                                # noqa: E402
import pxslib                                                   # noqa: E402

PXR_FLAT = 1
FAIL = []


def check(ok, what):
    print("   %-72s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def shot(m, a, name):
    if not a.shots:
        return
    m.pause()
    w, h, px = m.fbuf(0)
    m.run()
    os88marty.write_png_rgb(os.path.join(a.shots, "pxs256-%s.png" % name), w, h, px)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla_256k")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/games360.img")
    ap.add_argument("--shots", help="write the window's and the bracket's screendumps here")
    a = ap.parse_args()
    os.chdir(ROOT)
    if a.shots:
        os.makedirs(a.shots, exist_ok=True)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        g = pxslib.open_game(m)             # (a): sys.exit()s if no window
        g.sim(False)
        g.god(True)
        g.scene("a")
        g.wait_frames(1, limit=240.0)
        check(True, "(a) the game opened a window on the 256 KB machine and drew a frame")
        rung, sprok = g.byte("px_rung"), g.byte("px_sprok")
        check(rung == PXR_FLAT and sprok == 0,
              "(b) ...on the Flat rung with the sprites as boxes (px_rung %d, px_sprok %d)"
              % (rung, sprok))
        px, py, _ = pxslib.scene_at("a")
        m.pause()
        g._mark()
        g.actor_poke(3, x=px + 384, y=py - 100, kind=1, state=1, hp=1, ang=2048, dir=2, flags=0)
        g.force_all_poke()
        m.run()
        g.wait_frames(1, limit=240.0)
        cand = g.candidates()
        check(any(c[2] == 3 for c in cand), "(c) a dog 1.5 tiles ahead is a candidate (%s)" % cand)
        wd = g.word("px_wdrawn")
        check(wd == 0xFFFF, "(c) ...and NO WEAPON in the view: the gun is a sprite-set frame "
              "(px_wdrawn %04x)" % wd)
        shot(m, a, "win")
        g.enter_fsx(limit=240.0)
        os88marty.until(m, lambda mm: g.byte("px_inbr") == 1, "the bracket", poll=0.5,
                        limit=400.0)
        g.scene("a")
        g.wait_frames(1, limit=400.0)
        check(g.byte("px_rung") == PXR_FLAT,
              "(d) F took the CGA bracket and drew a frame, still on Flat (px_back %d, "
              "px_rung %d)" % (g.byte("px_back"), g.byte("px_rung")))
        shot(m, a, "fsx")
        g.leave_fsx()
    if FAIL:
        print("pxs256: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pxs256: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
