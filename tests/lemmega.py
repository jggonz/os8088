#!/usr/bin/env python3
"""lemmega - LEMMINGS PLAYED under kern_mega's protected mode
(docs/plans/completed/KERN-MEGA-MEMORY-PLAN.md MC7, SPEC.md 116.6).

    make mega && make lemmingsdisk && python3 tests/lemmega.py [--keep]

Under kern_mega every claim LEMMINGS holds is an LDT selector, and the
package used to make segments by adding paragraphs to them: the style bank's
pieces at bank + off/16 (96 KB, past one selector's reach), the bank's second
part read at bank + 4032, the special picture's pieces at bank + n x 600, and
the T claim's four 32,000-byte planes at T + k x 2000 (125 KB). It opens -
tests/megasweep.py proves that much - and then faults or draws garbage the
moment a level loads. MC7 gave it fixed windows made once
(os88_seg_sub): three over the bank, four over T.

Boots kern_mega under Bochs at 16MB, LEMMINGS and the 1.44MB disk's band
files in C:\\RUN, starts it through MEGARUN, and:

  0. its region is above 1MB - LEMMINGS declares OS88_HIGH and OS88_HIGHMEM
     since this row first ran it so, which is what retired its two lines in
     tests/megahigh.txt;
  1. launches Fun 1 (Dirt, VGA mode 0Dh): the preview, then PLAY - the
     game's own iteration counter `_lem_iter` must MOVE, the level's T planes
     and bank windows must be DISTINCT selectors, and the T planes' bases
     must be T's + 32,000 x k;
  2. nukes it (F12 twice): every lemming explodes and takes terrain with it,
     which is the T-plane SYNC path (lemsprite.inc) - the iteration counter
     must keep moving through it;
  3. leaves and launches Fun 22, a SPECIAL level: its 76,800-byte picture is
     composed out of the bank through the windows, and Fire's bank (96,256
     bytes, the largest) after it - the counter must move again;
  4. no `kern_mega CLOSED` (a package's #GP, completed by closing it) or
     `kern_mega FAULT` line anywhere.

A screenshot of each stage lands in the boot's directory (--keep keeps it).

Broken on purpose (docs/WRITING-TESTS.md 1), both seen when the row was
written: lemload.c's ovl_far put back to `base + (hi << 12) + (lo >> 4)`
closes LEMMINGS at the launch (`kern_mega CLOSED LEMMINGS 0D`), and T's planes
made as T + 2000 x k in lem_wins in place of os88_seg_sub closes it there too,
at the first T-plane clear - 4 and the launch fail either way.
"""
import glob
import os
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
import os88bochs  # noqa: E402
from os88fixture import need  # noqa: E402
import lemplaymarty  # noqa: E402

FAILS = []
TPS = 18.2                      # BIOS ticks a guest second


def ticks(box):
    """The kernel's tick count: GUEST time, which a loaded host cannot
    shorten (docs/BOCHS.md, *The clock*)"""
    return int.from_bytes(box.read(box.sym("ticks"), 2), "little")


def gsleep(box, secs, done=None):
    """SECS of GUEST time (or until DONE()). A host sleep hands a guest
    under `sync=slowdown` whatever the host gave Bochs - on a loaded box a
    fraction of it"""
    t = ticks(box)
    box.until(lambda: (done is not None and done()) or
              (ticks(box) - t) & 0xFFFF >= secs * TPS,
              "%g guest seconds" % secs, 60 + secs * 10, poll=0.25)


def nuke(box, sym):
    """F12 TWICE, in the game's own terms (lemmings.c ovl_key): the second
    press must reach a pass that has seen F12 UP since the first, and within
    LEM_DBLKEY = 5 ticks of it. `key F12 F12 --delay 100` spaced the two
    presses in HOST time, and under `sync=slowdown` a host interval is no
    guest interval - Bochs runs ahead of the wall clock and sleeps, and an X
    event that arrives while it sleeps is taken with whatever came after
    it, so both presses reached ONE pass and the second was a typematic
    repeat to the game: no nuke, and the Esc that followed opened the
    postview instead of leaving it. So each press is queued on a STOPPED
    guest (Box.dblclick's shape) and the guest is stopped on WRITES to the
    game's own state - the first press's `lem_nuket`, then the `lem_f12up =
    0` beside it, then the pass's `lem_f12up = 1` - and the second press is
    queued there: one pass on, whatever the host does."""
    base = box.pkg_base("LEMMINGS")
    nuket, f12up = base + sym["_lem_nuket"], base + sym["_lem_f12up"]

    def press():
        if box.captured:
            box.inp.regrab = True
        box.inp.xd("key", "F12")

    os88bochs.session(box.out, ["watch w 0x%x" % nuket], pids=[box.p.pid],
                      inp=box.inp, at_prompt=press)
    box.inp.taken()
    box.at_prompt(["unwatch", "watch w 0x%x" % f12up], timeout=60)
    box.at_prompt([], timeout=60)           # the press's lem_f12up = 0
    os88bochs.session(box.out, ["unwatch"], 60, pids=[box.p.pid],
                      inp=box.inp, interrupt=False, at_prompt=press)
    box.inp.taken()                         # (...and = 1: the second press)


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def main():
    keep = "--keep" in sys.argv
    pkg = os.path.join(ROOT, "build", "lemmings.o88")
    for p in ("build/mega.img", "build/megak/kernel.map",
              "build/megarun.o88", "build/lemmings.o88",
              "build/lemmings.img"):
        need(p)                     # (the image's recipe writes the bands)
    sym = lemplaymarty.lemmap()
    put = ["RUN:" + f for f in
           sorted(glob.glob(os.path.join(ROOT, "build", "lemband", "1440",
                                         "*.LEM")))]
    out = tempfile.mkdtemp(prefix="lemmega-")
    disp = os.environ.get("OS88_BOCHS_DISPLAY") or os88bochs.take_display()
    box = os88bochs.Box(out, [pkg],
                        put=put, megs=16, display=disp)
    t0 = time.time()
    try:
        box.up()
        box.megarun()
        box.until(lambda: box.pkg_base("LEMMINGS"), "LEMMINGS started", 120)
        rbase = box.pkg_base("LEMMINGS")
        print("lemmega: LEMMINGS up after %.0fs, its region at %06X"
              % (time.time() - t0, rbase), flush=True)
        want(rbase >= 0x100000, "0. the region above 1MB (it declares "
             "OS88_HIGH, tests/megahigh.txt)")

        def rd(name, n=2):
            return int.from_bytes(box.pkg_read("LEMMINGS", sym[name], n),
                                  "little")

        def wr(name, v):
            a = box.pkg_base("LEMMINGS") + sym[name]
            box.dbg(["setpmem 0x%x 1 0x%x" % (a, v & 0xFF),
                     "setpmem 0x%x 1 0x%x" % (a + 1, v >> 8)])

        box.until(lambda: rd("_lem_screen") == 0 and rd("_lem_win") != 0,
                  "the launcher's list", 120)
        box.ui_done("the launcher's paint", secs=120)
        box.shot(os.path.join(out, "0-launcher.png"))

        def play(sel, what):
            """The launch, the preview (its picture drawn out of T and the
            mask, lemfont.inc), then PLAY: Return until the game's own
            iteration counter moves. The preview sets nothing a script can
            wait on, so the key is offered every 10 s, five times."""
            def moving():
                a = rd("_lem_iter")
                gsleep(box, 1)
                return rd("_lem_iter") != a

            wr("_lem_sel", sel)
            box.key("Return")                   # the launch: the preview
            box.until(box.in_fsx, what + ": the bracket", 120)
            for n in range(5):
                t = ticks(box)
                while (ticks(box) - t) & 0xFFFF < 10 * TPS and not moving():
                    if box.fault():
                        raise RuntimeError(box.fault())
                if moving():
                    break
                if n == 0:
                    box.shot(os.path.join(out, what + "-preview.png"))
                box.key("Return")               # PLAY
            if not moving():
                raise RuntimeError(what + ": the level never ran")
            return rd("_lem_iter")

        # --- 1. Fun 1 ----------------------------------------------------
        play(0, "fun1")
        gsleep(box, 20)
        it1 = rd("_lem_iter")
        box.shot(os.path.join(out, "fun1-play.png"))
        raw = box.pkg_read("LEMMINGS", sym["_lem_tpl"], 8)
        tpl = [int.from_bytes(raw[i:i + 2], "little") for i in (0, 2, 4, 6)]
        raw = box.pkg_read("LEMMINGS", sym["_lem_bwin"], 6)
        bwin = [int.from_bytes(raw[i:i + 2], "little") for i in (0, 2, 4)]
        bases = [box.sel_base(s) for s in tpl]
        bb = [box.sel_base(s) for s in bwin]
        print("lemmega: T planes %s based %s; bank windows %s based %s"
              % (["%04X" % s for s in tpl], ["%06X" % b for b in bases],
                 ["%04X" % s for s in bwin], ["%06X" % b for b in bb]))
        want(len(set(tpl)) == 4 and len(set(bwin)) == 3 and
             all(s & 4 for s in tpl + bwin),
             "1. the four T planes and the three bank windows are distinct "
             "LDT selectors")
        want(bases == [bases[0] + 32000 * k for k in range(4)] and
             bb == [bb[0] + 32768 * k for k in range(3)],
             "1. T's planes 32,000 bytes apart, the bank's windows 32 KB")
        want(it1 > 0, "1. Fun 1 plays: _lem_iter %d after 20s" % it1)

        # --- 2. the nuke ----------------------------------------------------
        nuke(box, sym)
        box.until(lambda: rd("_lem_nuked") == 1, "the nuke taken", 120)
        box.until(lambda: rd("_lem_finished") == 1,
                  "every lemming gone after the nuke", 240)
        it2 = rd("_lem_iter")
        box.shot(os.path.join(out, "fun1-nuked.png"))
        want(it2 > it1, "2. the nuke's explosions (the T sync path): "
             "_lem_iter %d -> %d, every lemming gone" % (it1, it2))

        # --- 3. Fun 22, a special level ---------------------------------
        # the verdict is in, so the loop leaves for the postview: Esc there
        # is the menu (the list), and an Esc the play loop took first is
        # the postview - so Esc until the bracket is left, in GUEST time
        for _ in range(4):
            if not box.in_fsx():
                break
            box.key("Escape")
            gsleep(box, 3, lambda: not box.in_fsx())
        box.until(lambda: not box.in_fsx(), "back on the desktop", 120)
        box.ui_done("the launcher again", secs=120)
        play(21, "fun22")
        gsleep(box, 20)
        it3 = rd("_lem_iter")
        box.shot(os.path.join(out, "fun22-play.png"))
        want(it3 > 0, "3. Fun 22 (the special picture) plays: _lem_iter %d"
             % it3)
    except Exception as e:      # noqa: BLE001 - the machine stopped
        want(False, "the run stopped: %s" % e)
    finally:
        bad = [l for l in box.lines() if "kern_mega FAULT" in l or
               "kern_mega CLOSED" in l]
        want(not bad, "4. no kern_mega FAULT or CLOSED%s"
             % "".join(" | " + l for l in bad[:3]))
        box.stop()
        print("lemmega: %.0fs under Bochs" % (time.time() - t0))
        if keep or FAILS:
            print("lemmega: the boot is in " + out)
        else:
            shutil.rmtree(out, ignore_errors=True)
    for f in FAILS:
        print("FAIL lemmega: " + f)
    print("lemmega: %s" % ("FAILED" if FAILS else "PASS"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
