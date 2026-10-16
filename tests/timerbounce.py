#!/usr/bin/env python3
"""TIMER.O88 and BOUNCE.O88 - the two kinds of SPEC.md 14 as packages (14.8).

    make && python3 tests/timerbounce.py [--machine os8088_5150_cga_gla]

SPEC.md 111 and 112. Both are opened BY NAME out of the apps disk's APPS/
folder, the way a user opens them now that the Builtins menu is gone, and
everything asserted is read out of the package's OWN bss through its map -
the window's segment names the instance, and NASM's map names the bytes - so
nothing here is a guess about a listing row or a launch's timing.

  Timer
    A  the window opens and the seconds ADVANCE: tm_hms[TM_SEC] moves over
       ~2.5 guest seconds, and tm_run reads 1 (opening the Timer starts it)
    B  the digits are ON THE GLASS: the 64x8 digit line is not blank white
    C  Stop, pressed through the real gesture (press and release on the
       button's own rect, read out of tm_rects): tm_run reads 0 and the
       seconds hold still for ~1.5 guest seconds
    D  Start: tm_run reads 1 again
  Bounce
    E  the window opens and its WORKER holds a task slot: one more live task
       than before the launch (the kind's task did, SPEC.md 20.6's reference)
    F  the square MOVES: bn_pos changes over ~1 guest second

MartyPC, a 5150 with a CGA by default - the machine this OS is for. The Timer
and the Bounce animate, so os88marty.settle cannot be the wait anywhere
below; frames are counted instead (heapscrl's note).
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from os88pkg import PKG_FMT                                  # noqa: E402
sys.path.insert(0, HERE)
import os88build                                             # noqa: E402
import os88pkg                                               # noqa: E402
import os88marty                                             # noqa: E402
import os88mouse                                             # noqa: E402
import os88sym                                               # noqa: E402
import os88geom as geom                                      # noqa: E402
import dispcp                                                # noqa: E402

S = os88sym.linear
E = os88sym.equates()
T_SIZE, MAX_TASKS = E["T_SIZE"], E["MAX_TASKS"]

PKG_FOLDER = "APPS"
TIMER, BOUNCE = ("timer", b"Timer", "TIMER.O88"), ("bounce", b"Bounce", "BOUNCE.O88")

CARD_HZ = 60.0                      # the CGA's frame rate, for tick()
TM_SEC = 2                          # apps/timer/timer.asm: the seconds byte

FAILED = []


def say(*a):
    print(*a)
    sys.stdout.flush()


def check(cond, what, why="", got=None, want=None):
    if cond:
        say("  ok   %s" % what)
        return True
    m = what
    if got is not None or want is not None:
        m += "\n        want: %s\n        got:  %s" % (want, got)
    if why:
        m += "\n        why:  " + why
    FAILED.append(m)
    say("  FAIL %s" % what)
    return False


def report():
    if FAILED:
        say("timerbounce: %d FAILED" % len(FAILED))
        for m in FAILED:
            say("  FAIL: %s" % m)
        sys.exit(1)
    say("timerbounce: pass")
    sys.exit(0)


def tick(m, frames):
    """Let the guest run for `frames` and leave it running."""
    m.advance(frames=frames)
    m.run()


def seconds(m, s=1.0):
    tick(m, int(s * CARD_HZ))


# --- a package's own state, out of its map -----------------------------------
class Pkg:
    """One package: its symbols (NASM's map of the shipped source) and, once
    it is open, its window slot, segment and bss."""
    _maps = {}

    def __init__(self, app, name, file):
        self.app, self.name, self.file = app, name, file
        self.slot = self.seg = self.bss = None

    def sym(self, name):
        mp = Pkg._maps.get(self.app)
        if mp is None:
            src = os.path.join(ROOT, "apps", self.app, self.app + ".asm")
            tmp = tempfile.mktemp(suffix=".asm")
            mf = tempfile.mktemp(suffix=".map")
            open(tmp, "w").write(open(src).read() + "\n[map all %s]\n" % mf)
            r = subprocess.run(["nasm", "-f", "bin", "-w+error",
                                "-I", os.path.join(ROOT, "apps") + os.sep,
                                "-o", tmp + ".bin", tmp],
                               capture_output=True, text=True)
            if r.returncode:
                sys.exit("timerbounce: could not map %s:\n%s"
                         % (self.app, r.stderr[:400]))
            mp = {}
            for line in open(mf):
                p = line.split()
                if len(p) == 3:
                    try:
                        mp[p[2]] = int(p[0], 16)
                    except ValueError:
                        pass
            for f in (tmp, mf, tmp + ".bin"):
                if os.path.exists(f):
                    os.remove(f)
            if "os88_image_end" not in mp:
                sys.exit("timerbounce: %s's map has no os88_image_end" % self.app)
            Pkg._maps[self.app] = mp
        if name not in mp:
            sys.exit("timerbounce: %s has no symbol %s" % (self.app, name))
        return mp[name]

    def img_size(self):
        raw = open(os88build.at("build/%s.o88" % self.app), "rb").read()
        d = os88pkg.image_unwrap(raw)
        n = int.from_bytes(d[8:10], "little")
        if n != len(d):
            sys.exit("timerbounce: %s.o88 says image=%d and unwraps to %d"
                     % (self.app, n, len(d)))
        return n

    def find(self, m):
        """The visible window whose package segment carries our name."""
        t = m.read(S("wm_wins"), geom.MAX_WIN * geom.WIN_SIZE)
        for i in range(geom.MAX_WIN):
            b = i * geom.WIN_SIZE
            if int.from_bytes(t[b + geom.W_FLAGS:b + geom.W_FLAGS + 2],
                              "little") & 3 != 3:
                continue
            seg = int.from_bytes(t[b + geom.W_SEG:b + geom.W_SEG + 2], "little")
            if not seg:
                continue
            hdr = m.read(seg << 4, 32)
            if hdr[:3] == b"O8" + bytes([PKG_FMT]) and \
                    hdr[16:32].split(b"\0")[0] == self.name:
                self.slot, self.seg = i, seg
                self.bss = (seg << 4) + self.img_size()
                return True
        return False

    def at(self, name, off=0):
        return self.bss + self.sym(name) - self.sym("os88_image_end") + off

    def byte(self, name, off=0, m=None):
        return m.read(self.at(name, off), 1)[0]

    def word(self, name, off=0, m=None):
        return int.from_bytes(m.read(self.at(name, off), 2), "little")

    def win(self, m):
        return dispcp.win_rect(m, S, self.slot)


def live_tasks(m):
    t = m.read(S("sch_tasks"), T_SIZE * MAX_TASKS)
    return sum(1 for i in range(MAX_TASKS) if t[i * T_SIZE])


def white_only(m, x1, y1, x2, y2):
    """Is every pixel of the inclusive rect the content's white?"""
    w, h, rows = m.vram()
    for y in range(y1, y2 + 1):
        row = rows[y]
        for x in range(x1, x2 + 1):
            if not row[x]:
                return False
    return True


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    tm, bn = Pkg(*TIMER), Pkg(*BOUNCE)

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        mo = os88mouse.Mouse(marty=m)

        def wait(mm, card=None, frames=90):
            tick(mm, frames)

        # --- B:, then APPS/ --------------------------------------------------
        dispcp.open_drive(m, mo, S, wait, "B")
        tick(m, 60)
        dispcp.open_named(m, mo, S, wait, 0, 0, PKG_FOLDER, expect="nav")
        tick(m, 40)

        # --- the Timer -------------------------------------------------------
        say("timerbounce: %s - opening %s" % (a.machine, tm.file))
        dispcp.open_named(m, mo, S, wait, 0, 0, tm.file, expect="window")
        tick(m, 30)
        if not check(tm.find(m), "a window whose package is 'Timer' is up"):
            return report()
        wx, wy, ww, wh = tm.win(m)
        say("  Timer: window %d at (%d,%d) %dx%d, segment %04x, bss %05x"
            % (tm.slot, wx, wy, ww, wh, tm.seg, tm.bss))

        s0 = tm.byte("tm_hms", TM_SEC, m=m)
        seconds(m, 2.5)
        s1 = tm.byte("tm_hms", TM_SEC, m=m)
        moved = (s1 - s0) % 60
        check(1 <= moved <= 4, "A: the seconds advanced over 2.5 guest seconds",
              "tm_tick re-arms OSAPI_WM_TIMER and tm_keep accumulates; a "
              "handler installed and never re-armed fires once and stops",
              got="%d -> %d" % (s0, s1), want="+2 or +3")
        check(tm.byte("tm_run", m=m) == 1, "A: tm_run = 1 - opening it starts it")

        # B: the digit line is lettered. Content x +48, y +5, 64x8 (SPEC.md 111.1)
        dx, dy = wx + 1 + 48, wy + geom.TITLE_H + 5
        m.pause()
        try:
            blank = white_only(m, dx, dy, dx + 63, dy + 7)
        finally:
            m.run()
        check(not blank, "B: the HH:MM:SS line is on the glass",
              "tm_digits letters the cells that changed through "
              "OSAPI_FONT_RUN; an all-white line means nothing drew")

        # C: Stop, on its own rect (index 1 of tm_rects, 4 words each)
        bx1, by1, bx2, by2 = (tm.word("tm_rects", 8 + 2 * i, m=m) for i in range(4))
        say("  Stop's rect (%d,%d)-(%d,%d)" % (bx1, by1, bx2, by2))
        mo.click((bx1 + bx2) // 2, (by1 + by2) // 2)
        tick(m, 30)
        check(tm.byte("tm_run", m=m) == 0, "C: Stop pressed and released: tm_run = 0",
              "os88ui_btnup fires on the RELEASE over the same rect (SPEC.md "
              "13.8, 20.5.1.3) and tm_act clears tm_run")
        s2 = tm.byte("tm_hms", TM_SEC, m=m)
        seconds(m, 1.5)
        s3 = tm.byte("tm_hms", TM_SEC, m=m)
        check(s2 == s3, "C: ...and the seconds hold still while stopped",
              "tm_keep samples the tick and discards the delta while tm_run "
              "is 0 - that is what Stop means (SPEC.md 14)",
              got="%d -> %d" % (s2, s3), want="unchanged")

        # D: Start (index 0)
        bx1, by1, bx2, by2 = (tm.word("tm_rects", 2 * i, m=m) for i in range(4))
        mo.click((bx1 + bx2) // 2, (by1 + by2) // 2)
        tick(m, 30)
        check(tm.byte("tm_run", m=m) == 1, "D: Start: tm_run = 1 again")

        # --- the Bounce ------------------------------------------------------
        before = live_tasks(m)
        say("timerbounce: opening %s (%d live tasks)" % (bn.file, before))
        dispcp.open_named(m, mo, S, wait, 0, 0, bn.file, expect="window")
        tick(m, 40)                     # the worker is spawned at the first paint
        if not check(bn.find(m), "a window whose package is 'Bounce' is up"):
            return report()
        wx, wy, ww, wh = bn.win(m)
        say("  Bounce: window %d at (%d,%d) %dx%d, segment %04x"
            % (bn.slot, wx, wy, ww, wh, bn.seg))
        after = live_tasks(m)
        check(after == before + 1 and bn.byte("bn_spawned", m=m) == 1,
              "E: the Bounce's worker holds one task slot",
              "bn_spawn_ck spawns from the first paint with the lock held "
              "(SPEC.md 112.2); an entry proc cannot",
              got="%d -> %d live tasks, bn_spawned %d"
                  % (before, after, bn.byte("bn_spawned", m=m)),
              want="%d -> %d, 1" % (before, before + 1))
        p0 = (bn.word("bn_pos", m=m), bn.word("bn_pos", 2, m=m))
        seconds(m, 1.0)
        p1 = (bn.word("bn_pos", m=m), bn.word("bn_pos", 2, m=m))
        check(p0 != p1, "F: the square moved over a guest second",
              "the worker steps every 2 ticks whether or not it can draw "
              "(SPEC.md 11.3, 14)", got="%r -> %r" % (p0, p1))
        check(0 <= p1[0] <= 140 and 0 <= p1[1] <= 103,
              "F: ...and stayed inside the content",
              got=p1, want="x in 0..140, y in 0..103")

    return report()


main(sys.argv[1:])
