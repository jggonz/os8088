#!/usr/bin/env python3
"""A VERSION 2 video (SPEC.md 98.1.10) played on kern_mega through the block
decoder's PROTECTED-MODE part, with no #GP fixup (SPEC.md 98.3.23.2).

    python3 tests/megavidv2.py                     the row: lin320 modex vga4
    python3 tests/megavidv2.py --lay lin320        one layout
    python3 tests/megavidv2.py --keep              ...and leave the shots
    python3 tests/megavidv2.py --part0             the FAILING CASE (below)

The Video Player says OS88_HIGH | OS88_HIGHMEM, so on kern_mega it runs above
1MB, its DS and CS LDT selectors, and it far-calls the block decoder - a PART
of VIDEO.O88 - through the part's CODE alias. Part 0 keeps its state through
CS, which a code selector never allows: every such store is a #GP the kernel
completes (mega_gpw), one per block of every frame. So under CPU_F_PM the
player fetches part 1, the same source assembled to write nothing through CS.

The clip is tests/vidv2.py's (160 x 96, every block kind of the layout on
its odd frames, version 1's records on the even ones) with a still TAIL of
empty frames after it. Under `make megatrap`'s kern_mega (a private tree;
every completed #GP is a line on E9) in Bochs at 16MB, with the clip and
VIDEO.O88 on a 1.44MB B:, for each layout:

  play     R (Repeat on: a COPYING play, its ring a pool claim above 1MB,
           98.3.18.17 - Repeat's seam is the ring's, so it is never read in
           place) and Space: the full screen. Space again a second in,
           while the blocks are coming: paused, "Paused" up, and three
           frame steps (Period) decode block records ROUND THE TEXT BOX -
           every byte through vp_bcb, the box's own into its save. Space:
           on, the box down, into the tail - where the picture stops
           changing, and is shot.
  again    Esc to the window, R (Repeat off), Left to the first key, the
           card out (its shot says `At the start`), and Play again: the
           file is HELD whole in XMS, so this play reads it IN PLACE
           through OSAPI_XMEM_RUN's selectors (98.3.18.16) - version 2's
           in-place refusal does not apply under protected mode. Its tail
           is shot, and the card's after it says `XMS in place (PM)`.

Each step must add NO trap line and no kern_mega FAULT, and the two tail
shots must be the same picture: the tail draws nothing, so what is on the
screen is every frame's decode - the three round the box included, which
went into the box's save and came back out when it was taken down.

THE FAILING CASE (docs/WRITING-TESTS.md 1): --part0 plays a VIDEO.O88 whose
vp_bget fetches part 0 under protected mode too (its `inc ax` made a `nop`).
Every play step then takes hundreds of traps - vb_walk's `dec word [cs:vb_n]`
a block, vb_set's patching - and the run FAILS on them. Its tail shots must
still be the same picture as each other: the fixups are correct, only slow.

It writes only under its private tree's megavidv2/ directory.
"""
import argparse
import collections
import os
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88bochs  # noqa: E402
import os88build  # noqa: E402
import os88vid as vid  # noqa: E402
import vidv2  # noqa: E402

TAIL = 600                      # still frames after the clip's 40: 40 s
QUIET = 8                       # seconds of no E9 line: settled
STILL = 90                      # seconds for a play to reach its tail
CROP = "640x380+0+0"            # the guest's screen, not Bochs's status bar
LAYS = {"lin320": vid.LAY_LIN320, "modex": vid.LAY_MODEX,
        "vga4": vid.LAY_LIN80}
BGET = bytes((0xF6, 0xC4, 0x10, 0xB0, 0x00, 0x74, 0x01, 0x40))


def tailclip(tmp, lay):
    """vidv2.clip's frames with TAIL empty ones after them, made here by
    wrapping the Writer the clip is written through - and a VGA4 one given
    its sixteen colours, which makes it a FULL SCREEN file (SPEC.md
    98.1.3.2.1): without, a 160 x 96 VGA4 clip plays in the window, whose
    scrub bar never stands still"""
    W = vid.Writer
    frames, write, init = W.frame, W.write, W.__init__
    state = {}

    def init16(self, g, rate, spf, afmt, ab, pf, *a, **k):
        if pf == vid.PF_VGA4 and not k.get("palette"):
            k["palette"] = bytes((i * 4 + 3) & 63 for i in range(48)) + \
                bytes(720)
        return init(self, g, rate, spf, afmt, ab, pf, *a, **k)

    def frame(self, ops, surf, *a, **k):
        state["surf"] = surf
        return frames(self, ops, surf, *a, **k)

    def wr(self, path, *a, **k):
        for _ in range(TAIL):
            frames(self, [], state["surf"])
        return write(self, path, *a, **k)
    W.frame, W.write, W.__init__ = frame, wr, init16
    try:
        return vidv2.clip(tmp, lay)
    finally:
        W.frame, W.write, W.__init__ = frames, write, init


def tap(box, key, n=1, gap=1.0):
    """KEY pressed N times, held 0.15 and then GAP seconds apart - GUEST
    seconds (Box.guest_sleep): where the pause lands in the clip is the
    scenario, and in host seconds it moved with how much CPU this Bochs got
    (four to a core paused before the first frame, both tails wrong)"""
    for _ in range(n):
        box.xd("keydown", key)
        box.guest_sleep(0.15)
        box.xd("keyup", key)
        if gap:
            box.guest_sleep(gap)


def differ(a, b):
    """ImageMagick's count of differing pixels on the guest's screen"""
    r = subprocess.run(["compare", "-metric", "AE", a + "[" + CROP + "]",
                        b + "[" + CROP + "]", "null:"],
                       capture_output=True, text=True)
    try:
        return int(float(r.stderr.split()[0]))
    except (ValueError, IndexError):
        return -1


def still(box, d, tag):
    """shots STEP s apart until two are the same picture: the tail. The last
    one's path, or None (it was 3 s apart; a play still running changes
    every frame, so 1.5 s is a stricter test of standing still, not a
    looser one)"""
    prev, end, i = None, time.time() + STILL, 0
    while time.time() < end:
        p = os.path.join(d, "%s-%02d.png" % (tag, i))
        box.shot(p)
        if prev and differ(prev, p) == 0:
            return p
        prev, i = p, i + 1
        box.guest_sleep(1.0)
    return None


def pkg(out, src, part0):
    """VIDEO.O88 staged: the tree's, or (--part0) one that fetches part 0
    under protected mode too"""
    stage = os.path.join(out, "VIDEO.O88")
    if not part0:
        shutil.copy(src, stage)
        return stage
    b = open(os.path.join(ROOT, "build", "megak", "apps", "video.bin"),
             "rb").read()           # (the kern_mega ARM, KERN-MEGA-PLAN 14.66)
    assert b.count(BGET) == 1, "vp_bget's CPU_F_PM test is not where it was"
    i = b.index(BGET) + len(BGET) - 1
    binf = os.path.join(out, "video0.bin")
    with open(binf, "wb") as f:
        f.write(b[:i] + b"\x90" + b[i + 1:])
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88pkg.py"),
                    binf, "-o", stage,
                    "--part", os.path.join(ROOT, "build", "vblk.bin"),
                    "--part", os.path.join(ROOT, "build", "vblkpm.bin")],
                   check=True, stdout=subprocess.DEVNULL)
    return stage


def prep(out, lay, video):
    """The leg's B: - made BEFORE any leg starts, since tailclip patches
    os88vid.Writer for the length of one clip and the legs run at once"""
    d = os.path.join(out, lay)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    blk = os.path.join(d, "BLK.V88")
    os.rename(tailclip(d, LAYS[lay]), blk)
    disk = os.path.join(d, "v2.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", disk, "--size", "1440", video, blk],
                   check=True, stdout=subprocess.DEVNULL)
    return d, disk


def leg(t, out, lay, video, display=None):
    d, disk = out
    box = os88bochs.Box(d, [], bimg=disk, btype="1_44", build=t.dir,
                        display=display, megs=16)
    fails, counts, tails = [], collections.OrderedDict(), []
    pos = [0]
    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            return ["%s: kern_mega never reached protected mode" % lay], []

        def step(what, act, settle=False, tail=None, out_of_fsx=False):
            # SETTLE was QUIET s of no E9 line (8 s on a clean run): it is
            # the UI task done now, and an Esc out of the fullscreen play
            # waits for [fsx_task] to close and the window after it - not
            # the 4 s that was slept. The keys INSIDE a play keep their
            # gaps: where the pause lands in the clip is the scenario.
            act()
            try:
                if out_of_fsx:
                    box.until(lambda: not box.in_fsx(), "the play to close",
                              120)
                if settle or out_of_fsx:
                    box.ui_done("the player after " + what, secs=120,
                                lock=False)
            except RuntimeError as e:   # a FAULT: reported from E9 below
                print("megavidv2: %-6s %s" % (lay, e))
            if tail:
                p = still(box, d, tail)
                if p:
                    tails.append(p)
                else:
                    fails.append("%s %s: the picture never stood still"
                                 % (lay, what))
            with open(box.e9, "rb") as f:
                f.seek(pos[0])
                t = f.read()
            t = t[:t.rfind(b"\n") + 1]     # whole lines: the windows TILE
            pos[0] += len(t)
            new = t.decode("latin-1").splitlines()
            traps = [ln for ln in new if ln.startswith("T ")]
            fault = [ln for ln in new if ln.startswith("kern_mega FAULT")]
            sites = collections.Counter(" ".join(ln.split()[2:3] +
                                                 ln.split()[5:6])
                                        for ln in traps)
            box.shot(os.path.join(d, "%02d-%s.png"
                                  % (len(counts), what.split()[0])))
            counts[what] = len(traps)
            print("megavidv2: %-6s %-30s %6d traps%s %s" % (
                lay, what, len(traps), " FAULT" if fault else "",
                ", ".join("%s x%d" % kv for kv in sites.most_common(3))))
            if fault:
                fails.append("%s %s: %s" % (lay, what, fault[0]))
            if traps:
                fails.append("%s %s took %d #GP fixups, the first `%s` "
                             "(tools/megatraps.py %s)"
                             % (lay, what, len(traps), traps[0], box.e9))

        box.up()                             # the desktop, off kernel state
        box.capture()
        box.dbl(591, 105, "B:'s Disk window")
        pos[0] = os.path.getsize(box.e9)
        step("open (BLK.V88)", lambda: box.dbl(170, 123), settle=True)
        step("repeat, play, paused in the blocks",
             lambda: (tap(box, "r", gap=1.0), tap(box, "space", gap=1.0),
                      tap(box, "space", gap=2.0)))
        step("three steps round the box",
             lambda: tap(box, "period", 3, gap=1.5))
        step("on, into the tail", lambda: tap(box, "space", gap=2.0),
             tail="tail1")
        step("back, the first key, the card",
             lambda: (tap(box, "Escape", gap=0.0),
                      box.until(lambda: not box.in_fsx(),
                                "the play to close", 120),
                      box.ui_done("the window", lock=False),
                      tap(box, "r", gap=0.3),
                      tap(box, "Left", 25, gap=0.2), tap(box, "i", gap=0.0)),
             settle=True)
        step("again (held: in place)", lambda: tap(box, "space", gap=2.0),
             tail="tail2")
        step("back to the card", lambda: tap(box, "Escape", gap=0.0),
             out_of_fsx=True)
    finally:
        box.stop()
    return fails, tails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--lay", action="append", choices=sorted(LAYS))
    ap.add_argument("--serial", action="store_true", help="the legs one "
                    "at a time, on the one display")
    ap.add_argument("--part0", action="store_true", help="the failing case: "
                    "part 0 fetched under protected mode too")
    a = ap.parse_args()
    t = os88build.tree("MEGATRAP=1", targets=("mega.img", "megarun.o88",
                                              "mbr.bin"))
    out = os.path.join(t.dir, "megavidv2" + ("-part0" if a.part0 else ""))
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    video = pkg(out, os.path.join(ROOT, "build", "megak", "apps",
                                  "video.o88"), a.part0)
    t0 = time.time()
    fails = []
    lays = a.lay or ("lin320", "modex", "vga4")
    # THE LEGS ARE THREE MACHINES AND RUN AT ONCE, one X display each (the
    # row's own, then os88bochs.take_display's): nothing passes between
    # them, and each counts only its own E9. --serial is one at a time.
    res = {}
    made = {lay: prep(out, lay, video) for lay in lays}

    def run(lay, disp):
        try:
            res[lay] = leg(t, made[lay], lay, video, disp)
        except Exception as e:      # (reported, never swallowed)
            res[lay] = (["%s: %r" % (lay, e)], [])
    if a.serial:
        for lay in lays:
            run(lay, None)
    else:
        ths = [threading.Thread(target=run, args=(lay, None if i == 0 else
                                                   os88bochs.take_display()))
               for i, lay in enumerate(lays)]
        for th in ths:
            th.start()
        for th in ths:
            th.join()
    for lay in lays:
        f, tails = res[lay]
        fails += f
        if len(tails) == 2:
            n = differ(*tails)
            print("megavidv2: %-6s the two tails differ in %d pixel(s)"
                  % (lay, n))
            if n != 0:
                fails.append("%s: the in-place play's tail is not the "
                             "copying play's (%d pixels)" % (lay, n))
    print("megavidv2: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if not fails and not a.keep:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
