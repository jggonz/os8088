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


# Holds and gaps in GUEST seconds (Box.guest_sleep): a host second hands a
# Bochs short of CPU a fraction of one, and the census's windows would hold
# less of the game on a loaded box (docs/BOCHS.md, *The clock*)
def held(box, key, secs):
    os88bochs.xd("keydown", key)
    box.guest_sleep(secs)
    os88bochs.xd("keyup", key)
    box.guest_sleep(0.3)


def tap(box, key, n=1, gap=0.6):
    for _ in range(n):
        os88bochs.xd("keydown", key)
        box.guest_sleep(0.15)
        os88bochs.xd("keyup", key)
        box.guest_sleep(gap)


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
        if not box.wait_pm(300):
            print("FAIL: kern_mega never reached protected mode")
            return 1
        box.up()                         # the desktop, off kernel state
        box.capture()
        box.dbl(591, 105, "B:'s Disk window")
        pos = [os.path.getsize(box.e9)]

        def step(name, act, settle=True, run=0.0, fsx=None):
            # SETTLE was QUIET s of no E9 line - 8 s a step on a clean run,
            # whatever the game was doing. It is the UI task done with the
            # input now (the game's worker draws for ever, so the gfx lock is
            # not asked); RUN keeps a fixed window where the frames ARE the
            # census (the timedemo). The windows tile the whole run.
            # Into or out of the fullscreen bracket (FSX), ui_task is
            # frozen: the gate is [fsx_task] itself, then (out) the UI done.
            act()
            try:
                if fsx is not None:
                    box.until(lambda: box.in_fsx() == fsx, "the bracket to "
                              + ("open" if fsx else "close"), LIMIT)
                if settle and not fsx:
                    box.ui_done("the game to take " + name, secs=LIMIT,
                                lock=False)
            except RuntimeError as e:       # a FAULT: reported from E9
                print("pxsmega: %s" % e)
            box.guest_sleep(run)        # the window, in GUEST seconds
            with open(box.e9, "rb") as f:
                f.seek(pos[0])
                t = f.read()
            t = t[:t.rfind(b"\n") + 1]
            pos[0] += len(t)
            new = t.decode("latin-1").splitlines()
            traps = [l for l in new if l.startswith("T ")]
            fault = [l for l in new if l.startswith("kern_mega FAULT")]
            sites = collections.Counter(" ".join(l.split()[2:3] + l.split()[5:6])
                                        for l in traps)
            box.shot(os.path.join(out, "%02d-%s.png"
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

        step("launch (double-click)", lambda: box.dbl(165, 123, lock=False))
        step("timedemo (T, fullscreen)", lambda: tap(box, "t"), run=QUIET,
             fsx=True)
        step("leave the bracket (Esc)", lambda: tap(box, "Escape"), fsx=False)
        step("new game (Space, window)", lambda: tap(box, "space"))
        step("walk (Up 4s)", lambda: held(box, "Up", 4.0), settle=False)
        step("turn (Left 3s, Right 2s)",
             lambda: (held(box, "Left", 3.0), held(box, "Right", 2.0)), settle=False)
        step("shoot (Ctrl x3)", lambda: tap(box, "Control_L", 3), settle=False)
        step("fullscreen (F)", lambda: tap(box, "f"), fsx=True, run=1.0)
        step("walk fullscreen (Up 3s)", lambda: held(box, "Up", 3.0), settle=False)
        step("turn fullscreen (Left 3s)", lambda: held(box, "Left", 3.0), settle=False)
        step("shoot fullscreen (Ctrl x3)", lambda: tap(box, "Control_L", 3),
             settle=False)
        step("back to the window (Esc)", lambda: tap(box, "Escape"), run=2.0,
             fsx=False)
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
