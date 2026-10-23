#!/usr/bin/env python3
"""PIXELSTEIN 3D on kern_mega with NO #GP fixup during a PLAY: the timedemo,
then a game played in the window and fullscreen, under MEGATRAP=1's census
(docs/plans/KERN-MEGA-PLAN.md 14.50).

    python3 tests/pxsmega.py                 the row
    python3 tests/pxsmega.py --keep          ...and leave the E9 log and shots
    python3 tests/pxsmega.py --pkg F.o88     census another PXSTEIN.O88 (the
                                             one before a change: the counts
                                             are the package's, the kernel the
                                             same) - reports, does not gate

kern_mega COMPLETES a store through a code selector (mega_gpw) instead of
faulting, and each completion is an exception - a few hundred cycles on a 286.
PIXELSTEIN's compose kept its per-column memory in its own image and stored it
through CS, ~33,000 completed traps in ~20 s of 286 play (14.48.5), and its
column driver patched a near `ret` into the generated scalers through CS for
every sprite post. A launch census (tests/megatrap0.py) sees none of it: the
cost is in PLAYING, so this row plays. It boots `make megatrap`'s kernel (a
private tree) under Bochs - the one emulator here that enforces a 286's
segment rules (docs/BOCHS.md) - with B: holding PXSTEIN.O88, double-clicks
it, and then:

  T        the timedemo (97.13): the fullscreen bracket, Mode X on Bochs's
           VGA, a recorded walk through the first floor - textured walls,
           doors, guards as sprite posts, the weapon;
  Esc, Space  out of the bracket, a new game in the WINDOW (WIN4);
  Up, Left, Right, Ctrl  walk, turn and shoot in the window;
  F, Up, Left, Ctrl, Esc  the same in the fullscreen bracket, and back.

Every step must add NO trap line charged to the game and no kern_mega FAULT.

THE FAILING CASE (docs/WRITING-TESTS.md 1): put one of px_compose's `[es:`
stores back to `[cs:` and the walk takes a trap a changed column; copy
px_drv_tpl into part 1 unconditionally (px_gen_init) and every sprite post is
two. Both were measured red when this was written.
"""
import argparse
import collections
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs  # noqa: E402
import os88build  # noqa: E402

QUIET = 8                       # seconds of no E9 line: settled
LIMIT = 240


def held(key, secs):
    os88bochs.xd("keydown", key)
    time.sleep(secs)
    os88bochs.xd("keyup", key)
    time.sleep(0.3)


def tap(key, n=1, gap=0.6):
    for _ in range(n):
        os88bochs.xd("keydown", key)
        time.sleep(0.15)
        os88bochs.xd("keyup", key)
        time.sleep(gap)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--pkg", help="the PXSTEIN.O88 to census (default: the tree's)")
    a = ap.parse_args()
    t = os88build.tree("MEGATRAP=1", targets=(
        "mega.img", "megarun.o88", "mbr.bin", "pxstein.o88"))
    out = os.path.join(t.dir, "pxsmega")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    disk = os.path.join(out, "pxs.img")
    stage = os.path.join(out, "PXSTEIN.O88")
    shutil.copy(a.pkg or t.img("pxstein.o88"), stage)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", disk, "--size", "1440", stage],
                   check=True, stdout=subprocess.DEVNULL)
    t0 = time.time()
    box = os88bochs.Box(out, [], bimg=disk, btype="1_44", build=t.dir)
    fails, counts = [], collections.OrderedDict()
    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            print("FAIL: kern_mega never reached protected mode")
            return 1
        time.sleep(25)
        box.capture()
        os88bochs.dbl(591, 105)          # B:
        time.sleep(10)

        def step(name, act, settle=True):
            n0 = os.path.getsize(box.e9)
            act()
            if settle:
                end, last, lt = time.time() + LIMIT, n0, time.time()
                while time.time() < end:
                    time.sleep(1)
                    n = os.path.getsize(box.e9)
                    if n != last:
                        last, lt = n, time.time()
                    elif time.time() - lt >= QUIET:
                        break
            with open(box.e9, "rb") as f:
                f.seek(n0)
                new = f.read().decode("latin-1").splitlines()
            traps = [l for l in new if l.startswith("T ")]
            fault = [l for l in new if l.startswith("kern_mega FAULT")]
            sites = collections.Counter(" ".join(l.split()[2:3] + l.split()[5:6])
                                        for l in traps)
            os88bochs.shot(os.path.join(out, "%02d-%s.png"
                                        % (len(counts), name.split()[0])))
            counts[name] = len(traps)
            print("pxsmega: %-34s %6d traps%s %s" % (
                name, len(traps), " FAULT" if fault else "",
                ", ".join("%s x%d" % kv for kv in sites.most_common(4))))
            if fault:
                fails.append("%s: %s" % (name, fault[0]))
            if traps:
                fails.append("%s took %d #GP fixups, the first `%s` "
                             "(tools/megatraps.py %s)"
                             % (name, len(traps), traps[0], box.e9))

        step("launch (double-click)", lambda: os88bochs.dbl(165, 123))
        step("timedemo (T, fullscreen)", lambda: tap("t"))
        step("leave the bracket (Esc)", lambda: tap("Escape"))
        step("new game (Space, window)", lambda: tap("space"))
        step("walk (Up 4s)", lambda: held("Up", 4.0), settle=False)
        step("turn (Left 3s, Right 2s)",
             lambda: (held("Left", 3.0), held("Right", 2.0)), settle=False)
        step("shoot (Ctrl x3)", lambda: tap("Control_L", 3), settle=False)
        step("fullscreen (F)", lambda: tap("f"))
        step("walk fullscreen (Up 3s)", lambda: held("Up", 3.0), settle=False)
        step("turn fullscreen (Left 3s)", lambda: held("Left", 3.0), settle=False)
        step("shoot fullscreen (Ctrl x3)", lambda: tap("Control_L", 3),
             settle=False)
        step("back to the window (Esc)", lambda: tap("Escape"))
    finally:
        box.stop()
    print("pxsmega: %d steps, %d trap lines, %.0fs under Bochs"
          % (len(counts), sum(counts.values()), time.time() - t0))
    if a.pkg:
        return 0
    for f in fails:
        print("FAIL: " + f)
    if not fails and not a.keep:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
