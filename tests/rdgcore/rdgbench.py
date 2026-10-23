#!/usr/bin/env python3
"""RealDMG's core cost per M-cycle, fitted across gbclock's busy variants.

    python3 tests/rdgcore/rdgbench.py --marty [MACHINE] [--frames N]
    python3 tests/rdgcore/rdgbench.py --box [VM] [--frames N]
    python3 tests/rdgcore/rdgbench.py --box [VM] --rom GAME.GB [--frames N]

The instrument behind docs/reports/REALDMG-CORE-COST-2026-10-19.md. gbclock
(tools/gbclock.py) keeps the Game Boy CPU busy for a set share of each frame
and HALTs for the rest; CLOCK0, CLOCK25 and CLOCK50 are 0.7%, 25.7% and
50.7% busy (gbprof), so each step is 25.0% of a frame's 17,556 M-cycles. The
bare harness (tests/rdgcore/rdghost.asm, MODE 1) runs N frames as fast as the
machine can and prints the BIOS ticks they took; the slope across the three
is the cost of a busy M-cycle, and the intercept the fixed cost of a frame.

--marty runs MartyPC (cycle-accurate 8088; default os8088_xt_vga, 4.77 MHz,
360KB images). --box runs 86Box headless through tools/os88box.py (default
vm/286, the ami286 at 12.5 MHz, 1.44MB images) - per-instruction timing, not
cycle-accurate (docs/86BOX.md). The clock rate is the machine's own, passed
with --mhz when it is not the default.

--rom times a real game instead (MODE 3: gbprof's platformer input script,
the clock restarted at frame 800 so the frames timed are play), and prints
its frames against real time - the CPU's whole share, I/O and events
included, with no picture drawn.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests", "rdgcore"))
import rdgimg  # noqa: E402

BUSY = {0: 0.007, 25: 0.257, 50: 0.507}         # gbprof's measured shares
FRAME = 17556
TICK_HZ = 1193182 / 65536


def done_line(text):
    m = re.search(r"RDGHOST DONE frames (\d+) ticks (\d+)", text)
    return (int(m.group(1)), int(m.group(2))) if m else None


def run_marty(img, machine):
    import os88marty
    m = os88marty.launch(img, machine=machine, boot=False)
    try:
        m.run()
        txt = ""
        for _ in range(600):
            os88marty.guest_sleep(m, 5)
            txt = "\n".join(r.rstrip() for r in (m.screen() or []))
            if "END" in txt or "BADOP" in txt:
                break
        return txt
    finally:
        m.close()


def run_box(img, vm, name):
    import os88box
    b = os88box.launch(vm, name=name, hold=True, fresh=True, a=img)

    def text():
        try:
            r = b.cmd("text")
        except SystemExit:
            return ""
        return "\n".join(bytes.fromhex(x).decode("cp437").rstrip() for x in r.get("chars", []))
    try:
        t = ""
        for _ in range(600):
            b.cmd("step 2000")
            t = text()
            if "EXIT FOR BOOT" in t:                # an AT's first boot stops in setup
                b.tap("return")
            if "END" in t or "BADOP" in t:
                break
        return t
    finally:
        try:
            b.cmd("quit")
        except BaseException:
            pass


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--marty", nargs="?", const="os8088_xt_vga", metavar="MACHINE")
    g.add_argument("--box", nargs="?", const="286", metavar="VM")
    ap.add_argument("--frames", type=int)
    ap.add_argument("--mhz", type=float)
    ap.add_argument("--rom", help="time this game in play instead of the gbclock fit")
    ap.add_argument("--static", action="store_true",
                    help="with --rom: the recompiler's Static (REALDMG-SPEC 16.8): a first pass "
                         "records the profile, the second - the one timed - runs the game again "
                         "from its start, preloaded and fixed")
    ap.add_argument("--nojit", action="store_true",
                    help="the interpreter alone, without the recompiler (the A/B)")
    ap.add_argument("--seg2", action="store_true",
                    help="with --rom: a second 64KB cache segment (REALDMG-SPEC 16.2.1)")
    ap.add_argument("--nocore", action="store_true",
                    help="with --rom: no core tier, the cache's code in its own segments "
                         "only (REALDMG-SPEC 16.2.2): the A/B")
    ap.add_argument("--render", action="store_true",
                    help="with --rom: compose every frame with rdg_render as well")
    ap.add_argument("--renderd", action="store_true",
                    help="with --rom: ...with rdg_renderd, by damage, as the package does")
    ap.add_argument("--vga", action="store_true",
                    help="with --rom: ...and rdg_renderd's changed lines copied onto VGA mode "
                         "0Dh, as the package's full screen does (REALDMG-SPEC 15)")
    ap.add_argument("--ilace", type=int, default=0, choices=(0, 1, 2, 3),
                    help="with --vga: Interlace Off, On, Scrolling or Doubled (rdg_ilace)")
    a = ap.parse_args()
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools/gbclock.py")],
                          stdout=subprocess.DEVNULL)
    frames = a.frames or (300 if a.marty else 600)
    mhz = a.mhz or (4.772727 if a.marty else 12.5)
    geom = 360 if a.marty else 1440
    out = os.path.join(ROOT, "build", "realdmg")
    core, host = rdgimg.build_core(False, out), rdgimg.build_host(out)
    if a.rom:
        frames = a.frames or 2000
        img = os.path.join(out, "benchrom%d_%d.img" % (geom, os.getpid()))   # (one a run: two at once)
        rdgimg.make_image(a.rom, img, geom, "renderv" if a.vga else
                         "renderd" if a.renderd else
                         "render" if a.render else "play", frames,
                         core=core, host=host,
                         jit="static" if a.static else not a.nojit, seg2=a.seg2, ilace=a.ilace,
                         nocore=a.nocore)
        txt = run_marty(img, a.marty) if a.marty else run_box(img, a.box,
                                                              "rdgbrom%d" % os.getpid())
        os.remove(img)
        d = done_line(txt)
        if not d:
            sys.exit("rdgbench: %s did not finish:\n%s" % (a.rom, txt))
        played = frames - 800
        ms = d[1] / TICK_HZ / played * 1e3
        lines = txt.strip().splitlines()        # the DONE line, which wraps on
        done = [i for i, ln in enumerate(lines) if "RDGHOST DONE" in ln]   # the
        print(txt.strip() if os.environ.get("RT_DEBUG") else   # harness's 80
              " ".join(lines[done[0]:done[0] + 2]) if done else lines[-1])  # columns
        print("%s: %d frames in play, %d ticks: %.2f ms a frame = %.1f%% of real "
              "time's 16.74 ms (%s)"
              % (os.path.basename(a.rom), played, d[1], ms, ms / 16.7427 * 100,
                 "the CPU and rdg_renderd every frame, its lines copied onto VGA" if a.vga
                 else "the CPU and rdg_renderd every frame, no screen written" if a.renderd
                 else "the CPU and rdg_render every frame, no screen written" if a.render
                 else "CPU only, no picture"))
        return
    ticks = {}
    for b in sorted(BUSY):
        img = os.path.join(out, "bench%d_%d.img" % (geom, b))
        rdgimg.make_image(os.path.join(ROOT, "build/gbclock/CLOCK%d.GB" % b), img, geom,
                         "bench", frames, core=core, host=host,
                         jit="static" if a.static else not a.nojit)
        txt = run_marty(img, a.marty) if a.marty else run_box(img, a.box, "rdgb%d" % b)
        d = done_line(txt)
        if not d:
            sys.exit("rdgbench: CLOCK%d did not finish:\n%s" % (b, txt))
        ticks[b] = d[1]
        print("CLOCK%-2d %5.1f%% busy  %d frames  %d ticks" % (b, BUSY[b] * 100, frames, d[1]))
    step = FRAME * (BUSY[50] - BUSY[0]) * frames          # M-cycles across the fit
    secs = (ticks[50] - ticks[0]) / TICK_HZ
    per = secs * mhz * 1e6 / step
    fixed = ticks[0] / TICK_HZ / frames - BUSY[0] * FRAME * per / (mhz * 1e6)
    q = 1 / TICK_HZ * mhz * 1e6 / step                   # one tick of error, in clocks
    print("cost per busy M-cycle: %.2f clocks (+- %.2f, one tick)" % (per, q * 1.4))
    print("fixed cost per frame:  %.2f ms = %.0f clocks" % (fixed * 1e3, fixed * mhz * 1e6))


if __name__ == "__main__":
    main()
