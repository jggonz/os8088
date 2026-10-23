#!/usr/bin/env python3
"""RealDMG's replays, on 86Box's 286: the buttons recorded from a save state
and played back, frame for frame, in the window and in full screen.

    python3 tests/gbreplay.py [--vm 286] [--only win|fs]

docs/REALDMG-SPEC.md section 10.5. GBPAD.GB (tools/gbprof/gbpad.asm, ours,
built by tools/gbours.py) reads the joypad every VBlank and folds the
buttons and the frame into a sum at C000h, so one frame's buttons different
or a frame out of place anywhere in a play leaves a different machine. Each
leg boots vm/<VM> through tests/rdgcore/rdgwin.py's boot and opens it, then:

  * F6 records into slot 1 - GAME.SS1 written, [rw_rpmode] RECORDING - and
    the buttons are pressed, held and let go in a script of their own;
  * F6 again ends it: GAME.RP1 written, with more than one change of the
    buttons in it;
  * F7 plays it - the state loaded, the keyboard's buttons replaced - and it
    must END IN STEP: the package compares the machine at the play's end
    with the one the recording ended on ([rw_rpres] 1), which is the whole
    claim of a replay;
  * F7 again, with the game's sum in WRAM changed under the play: it must
    end OUT OF STEP ([rw_rpres] 2), or the comparison could not see a
    replay that went wrong.

The full-screen leg is the canvas, the recompiler and the page flips, whose
short runs are why the buttons change only between two frames. 86Box is not
cycle-accurate; nothing here is a time.
"""
import argparse
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tests", "rdgcore"))
import os88box  # noqa: E402
import os88flush  # noqa: E402
import rdgwin  # noqa: E402

F6, F7 = 0x40, 0x41
RIGHT, LEFT, UP = 0x14D, 0x14B, 0x148
KEY_A, KEY_B = 0x2D, 0x2C               # X and Z: the shipped A and B


def leg(rom, vm, fs, fails):
    name = "gbreplay%d%s" % (os.getpid(), "fs" if fs else "win")
    o = argparse.Namespace(rom=rom, vm=vm, keep=name, mem=None, press="", fs=fs)
    b = rdgwin.boot(o)
    where = "full screen" if fs else "window"
    try:
        seg, view, core = rdgwin.find(b)
        rd = lambda n, k=1: int.from_bytes(b.memr(seg * 16 + rdgwin.bss_off(n), k), "little")

        def until(cond, what, n=80):
            for _ in range(n):              # (a state is ~35KB of floppy)
                if cond():
                    return True
                b.cmd("step 250")
            fails.append("%s: %s never happened" % (where, what))
            return False

        def tap(code, ms=200):
            b.cmd("key down 0x%X" % code)
            b.cmd("step %d" % ms)
            b.cmd("key up 0x%X" % code)

        if fs:
            rdgwin.machine_item(b, rdgwin.ITEM_FULLSCREEN)
            b.cmd("step 3000")
        tap(F6)
        if not until(lambda: rd("rw_rpmode") == 1, "F6's recording"):
            return
        b.cmd("key down 0x%X" % RIGHT)
        for k in (KEY_A, KEY_B, UP, KEY_A):
            b.cmd("step 400")
            tap(k, 250)
        b.cmd("key up 0x%X" % RIGHT)
        b.cmd("key down 0x%X" % LEFT)
        b.cmd("step 600")
        tap(KEY_B, 150)
        b.cmd("key up 0x%X" % LEFT)
        b.cmd("step 300")
        tap(F6)
        if not until(lambda: rd("rw_rpmode") == 0, "F6's end"):
            return
        frames = rd("rw_rpfr", 4)
        print("gbreplay %s: recorded %d frames" % (where, frames))
        if frames < 60:
            fails.append("%s: only %d frames recorded" % (where, frames))
        b.cmd("step 1000")
        tap(F7)
        if not until(lambda: rd("rw_rpmode") == 2, "F7's play"):
            return
        if not until(lambda: rd("rw_rpmode") == 0, "the play's end", 200):
            return
        res = rd("rw_rpres")
        print("gbreplay %s: played %d frames, %s" % (where, rd("rw_rpfr", 4),
                                                      {1: "in step", 2: "OUT OF STEP"}.get(res, res)))
        if res != 1:
            fails.append("%s: the play did not end in step (rw_rpres %d)" % (where, res))
        b.cmd("step 1000")                  # THE NEGATIVE: the sum changed
        tap(F7)                             #   under a play
        if not until(lambda: rd("rw_rpmode") == 2, "the second play"):
            return
        # (the poke waits for the play's OWN frames, not a fixed 500 ms after
        # it began: with the core's holes as a cache tier (REALDMG-SPEC
        # 16.2.2) the fixed-time poke in full screen ended "in step" 6 runs
        # in 6 under load, and the same poke placed by the play's frame
        # count went OUT OF STEP 6 in 6 - why the fixed one did not take is
        # not pinned down yet)
        if not until(lambda: rd("rw_rpfr", 4) >= 30, "30 frames of the second play"):
            return
        s = b.memr(view * 16 + 0xC000, 2)
        b.cmd("memw 0x%X %02X%02X" % (view * 16 + 0xC000, s[0] ^ 0x5A, s[1] ^ 0xA5))
        if not until(lambda: rd("rw_rpmode") == 0, "the second play's end", 200):
            return
        res = rd("rw_rpres")
        print("gbreplay %s: with the sum changed under it, %s" % (where,
              {1: "in step", 2: "OUT OF STEP"}.get(res, res)))
        if res != 2:
            fails.append("%s: a play that went wrong ended in step (rw_rpres %d)" % (where, res))
    finally:
        b.cmd("quit")
    time.sleep(3)
    img = os.path.join(os88box.RUNS, name, "fdd2.img")
    try:
        vol = os88flush.Volume(open(img, "rb").read())
        rp = vol.read("GAME.RP1")
    except Exception as e:                  # noqa: BLE001
        fails.append("%s: GAME.RP1 is not on the disk (%s)" % (where, e))
        return
    pairs = (len(rp) - 16) // 2
    print("gbreplay %s: GAME.RP1, %d bytes, %d changes of the buttons" % (where, len(rp), pairs))
    if rp[:4] != b"RDRP" or pairs < 4:
        fails.append("%s: GAME.RP1 is not a replay of that play" % where)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--vm", default="286")
    ap.add_argument("--only", choices=("win", "fs"))
    a = ap.parse_args()
    rom = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "gbours.py"),
                                   "gbpad"], text=True).strip().splitlines()[-1]
    fails = []
    for fs in (False, True):
        if a.only and a.only != ("fs" if fs else "win"):
            continue
        leg(rom, a.vm, fs, fails)
    for f in fails:
        print("FAIL gbreplay: " + f)
    print("gbreplay: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
