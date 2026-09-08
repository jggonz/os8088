#!/usr/bin/env python3
"""The pattern view's scroll gate (SPEC.md 45.12.2).

    make trkscrl && python3 tests/trkscrl.py

`tui_scrl` moves the row area by a blit and relights the 2n+1 strips a jump of
n rows exposes. Two things have to be true of that and only one of them shows
in a screenshot.

1. IT DRAWS THE RIGHT PIXELS. After a scroll the row area must be
   BYTE-IDENTICAL to what a full repaint of the same view produces - the
   standard SPEC.md 22.11, 27.7.2 and tests/brtest.py hold their own blits to,
   and the only thing that separates "it scrolled" from "it scrolled
   correctly". A blit that leaves one stale strip is perfectly plausible in a
   screenshot and wrong in every frame after it.

2. IT REACHES PAST ONE ROW. This is the defect the gate exists for. A frame
   that arrives late sees the view two rows on, and while `tui_scrl` only knew
   n = 1 that frame took `tui_draw_pat` instead - 35 strips against three - so
   the repaint outlasted the frame it was drawn in, the next frame was later
   still, and the machine settled into re-lettering the whole grid at every
   row change. The assertion is therefore not about time at all: a jump of
   n <= TUI_SCRL_MAX must cost ONE scroll and ZERO full repaints. That is
   deterministic, it needs neither a sound card nor a slow machine, and it
   fails the moment the reach is taken away again.

WHY QEMU, and why NOT MartyPC: the graphics fullscreen is not what a tier-0
machine draws (SPEC.md 45.9.1 turns the grid into one banded line there), so
the surface under test needs a machine faster than an 8088 - docs/TESTING.md's
first legitimate QEMU case. Nothing here is a measurement, so the host's speed
does not enter into it.

THE JUMP KEYS ARE THE ONLY WAY IN. The bracket's loop reads one key and then
draws a frame, so no number of Down presses can put two rows between two
frames; the music can, and a moving view cannot be screenshotted twice. So the
bench build (`-DTRKDBG`, tests/trkscrl.inc) carries j/k/n/v/b/c, which move
the STOPPED view by +-2, +-3, +-4 in a single frame, and G, which repaints the
grid with the view held still.
"""
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88rate                                              # noqa: E402
import os88fixture                                       # noqa: E402
import os88qemu                                              # noqa: E402

# Where things are on a 640x480 desktop with build/trkscrl.img in drive B:
# the B: drive zone, and BEVERLY.MOD's row in the window it opens (it sorts
# first - the only other file is TRKSCRL.O88). These two are the ONLY thing
# here that is the VGA desktop's - the row area under test is read out of the
# guest's live layout record - so `make test VIDEO=cga` needs its own pair
# before this can run there, and tui_lay_cga's shorter regions are worth
# covering when somebody wants them.
DISKB = (600, 112)
MODROW = (175, 128)
SCREEN = [640, 480]                 # replaced by the first screendump: the
                                    # pin-and-walk below is against the
                                    # KERNEL's clamp, so it has to be the live
                                    # adapter's size (`make test VIDEO=cga`)
STEP, PACE = 60, 0.06               # tools/mouse.py's, for the same reasons

# key, rows it moves the stopped view by. tests/trkscrl.inc owns the mapping;
# Up and Down are here too so n = 1 is covered by the same comparison.
JUMPS = [("down", 1), ("up", -1), ("j", 2), ("u", -2),
         ("k", 3), ("b", -3), ("n", 4), ("c", -4)]


class Qmp:
    """One connection, because QEMU's socket takes exactly one at a time."""

    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(path)
        self.f = self.s.makefile("rwb")
        self.f.readline()
        self.cmd({"execute": "qmp_capabilities"})

    def cmd(self, obj):
        self.f.write(json.dumps(obj).encode() + b"\n")
        self.f.flush()
        while True:
            line = self.f.readline()
            if not line:
                raise SystemExit("trkscrl: QMP connection closed")
            msg = json.loads(line)
            if "return" in msg or "error" in msg:
                if "error" in msg:
                    raise SystemExit("trkscrl: QMP: %s" % msg["error"])
                return msg["return"]

    def hmp(self, line):
        return self.cmd({"execute": "human-monitor-command",
                         "arguments": {"command-line": line}})

    def read(self, addr, n):
        """Guest PHYSICAL memory. pmemsave, not `xp`: the package scan below
        is 384KB and `xp` renders it as text one word at a time."""
        p = tempfile.mktemp(suffix=".mem")
        self.cmd({"execute": "pmemsave",
                  "arguments": {"val": addr, "size": n, "filename": p}})
        try:
            with open(p, "rb") as fh:
                return fh.read()
        finally:
            if os.path.exists(p):
                os.remove(p)

    def screen(self):
        """(width, rows) of the guest's framebuffer, out of a PPM screendump."""
        p = tempfile.mktemp(suffix=".ppm")
        self.hmp("screendump " + p)
        time.sleep(0.4)
        try:
            d = open(p, "rb").read()
        finally:
            if os.path.exists(p):
                os.remove(p)
        magic, dims, _maxval, px = d.split(b"\n", 3)
        if magic != b"P6":
            raise SystemExit("trkscrl: screendump is not a P6 PPM")
        w, h = (int(v) for v in dims.split())
        SCREEN[0], SCREEN[1] = w, h
        return w, [px[y * w * 3:(y + 1) * w * 3] for y in range(h)]

    def key(self, name):
        self.hmp("sendkey " + name)

    def goto(self, x, y):
        """tools/mouse.py's pin-and-walk, done over THIS connection.

        QEMU's QMP socket takes exactly one client, so shelling out to
        mouse.py while this object holds the socket open deadlocks: the child
        blocks on connect and the parent waits on the child. The walk itself
        is mouse.py's - pin against the kernel's bottom-right clamp, then step
        back with exact deltas, at most STEP a packet because msmouse
        truncates a larger one, and paced because the backend is 1200 baud.
        """
        cmds = []
        for _ in range((max(SCREEN) + STEP - 1) // STEP):
            cmds.append((STEP, STEP))
        dx, dy = x - (SCREEN[0] - 1), y - (SCREEN[1] - 1)
        while dx or dy:
            cx = max(-STEP, min(STEP, dx))
            cy = max(-STEP, min(STEP, dy))
            cmds.append((cx, cy))
            dx -= cx
            dy -= cy
        for cx, cy in cmds:
            self.hmp("mouse_move %d %d" % (cx, cy))
            time.sleep(PACE)
        time.sleep(0.2)

    def click(self, x, y):
        self.goto(x, y)
        self.hmp("mouse_button 1")
        time.sleep(0.15)
        self.hmp("mouse_button 0")
        time.sleep(0.15)


SOCK = None


def say(*a):
    print(*a)
    sys.stdout.flush()


def boot():
    """`make test` with the bench disk in B: - the minesrc.py idiom."""
    if os.path.exists("build/qemu.pid"):
        try:
            os.kill(int(open("build/qemu.pid").read().strip()), 15)
            time.sleep(1.0)
        except (OSError, ValueError):
            pass
    for f in ("build/qmp.sock", "build/qemu.pid"):
        if os.path.exists(f):
            os.remove(f)
    # `make test` DAEMONISES the emulator, so it outlives this script
    # unless somebody kills it - and the somebody is us (os88qemu).
    os88qemu.own()
    # THROUGH os88fixture.make: `make test` is a LAUNCHER, and the only thing
    # it leaves under build/ is the buildnum stamp the parse rewrites - this
    # puts that back. Its prerequisites ($(TESTIMG) $(TESTAPPS)) are declared
    # as this row's `wants=`, so the runner has them current and nothing is
    # built here either.
    r = os88fixture.make("test", "TESTAPPS=build/trkscrl.img")
    if r.returncode:
        raise SystemExit("trkscrl: make test failed:\n" + r.stdout + r.stderr)


def main():
    global SOCK
    SOCK = sys.argv[1] if len(sys.argv) > 1 else "build/qmp.sock"
    os.chdir(ROOT)
    r = subprocess.run(["make", "trkscrl"], capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("trkscrl: make trkscrl failed:\n" + r.stdout + r.stderr)
    boot()

    q = Qmp(SOCK)
    time.sleep(6)
    q.screen()                      # ...which is also how SCREEN is learned
    say("trkscrl: a %dx%d desktop, then B: -> BEVERLY.MOD" % tuple(SCREEN))
    q.goto(*DISKB)
    q.hmp("mouse_button 1"); time.sleep(0.1); q.hmp("mouse_button 0")
    time.sleep(0.2)
    q.hmp("mouse_button 1"); time.sleep(0.1); q.hmp("mouse_button 0")
    time.sleep(6)
    # SELECT, then Enter. A double-click here is the flakiest thing in this
    # file - the window has just been drawn and the second press can land
    # while it still is - and Enter opens the selection just as well.
    q.click(*MODROW)
    time.sleep(1)
    q.key("ret")
    time.sleep(12)

    seg = None
    for _ in range(20):
        seg, _drv = os88rate.scan(q)
        if seg:
            break
        time.sleep(2)
    if not seg:
        raise SystemExit("trkscrl: the player never became resident.")
    P, _ = os88rate.symbols(("TRKDBG",))
    base = seg * 16
    say("trkscrl: TRKSCRL.O88 at %#06x" % seg)

    def peek(name, n=2):
        d = q.read(base + P["@" + name], n)
        return d[0] if n == 1 else int.from_bytes(d, "little")

    q.key("f")                          # into the graphics fullscreen
    time.sleep(4)
    if peek("trk_fs", 1) != 1:
        raise SystemExit("trkscrl: F did not enter the bracket.")
    if peek("mp_xt", 1) != 0:
        raise SystemExit("trkscrl: XT mode is on, so the surface under test is "
                         "SPEC.md 45.13's text screen and not this one.")
    for _ in range(6):                  # SPACE is a TOGGLE, and whether the
        if peek("mp_playing", 1) == 0:  # module started depends on whether the
            break                       # machine has a card at all
        q.key("spc")
        time.sleep(2)
    if peek("mp_playing", 1) != 0:
        raise SystemExit("trkscrl: could not stop playback; the view would "
                         "move between the two screenshots.")

    # The row area under test, as the GUEST's live layout has it (the bench
    # build publishes it - tests/trkscrl.inc says why). Everything above it -
    # the readouts, the scopes, the status line - is drawn by other paths on
    # their own schedule and is not what this gate is about.
    q.key("g")                       # ...which also publishes the bounds
    time.sleep(1.5)
    y0, y1 = peek("tds_y0"), peek("tds_y1")
    # QEMU line-doubles a 200-row adapter into a 400-row screendump, so the
    # guest's rows and the dump's are not the same rows. The surface height
    # the app itself is drawing against is the honest divisor.
    scale = max(1, SCREEN[1] // max(1, peek("tui_ch")))
    y0, y1 = y0 * scale, y1 * scale
    say("trkscrl: the row area is dump rows %d..%d (x%d)" % (y0, y1 - 1, scale))

    fails = []
    for key, rows in JUMPS:
        s0, p0 = peek("tds_scrl"), peek("tds_pat")
        v0 = peek("tui_vrow", 1)
        q.key(key)
        time.sleep(1.5)
        v1, s1, p1 = peek("tui_vrow", 1), peek("tds_scrl"), peek("tds_pat")
        w, a = q.screen()
        q.key("g")                      # the same view, drawn the other way
        time.sleep(1.5)
        _w, b = q.screen()
        bad = [y for y in range(y0, y1) if a[y] != b[y]]
        moved, scrolls, repaints = v1 - v0, s1 - s0, p1 - p0
        ok = (moved == rows and scrolls == 1 and repaints == 0 and not bad)
        say("  %-4s %+2d rows: view %2d->%2d  scrolls %d  repaints %d  %s"
            % (key, rows, v0, v1, scrolls, repaints,
               "ok" if ok else "FAIL"))
        if moved != rows:
            fails.append("%s moved %d rows, not %d" % (key, moved, rows))
        elif scrolls != 1 or repaints != 0:
            fails.append("%s took %d scroll(s) and %d full repaint(s), not 1 "
                         "and 0 - the reach is gone" % (key, scrolls, repaints))
        elif bad:
            fails.append("%s left %d screen row(s) different from a full "
                         "repaint, first %d" % (key, len(bad), bad[0]))

    if fails:
        say("\ntrkscrl: FAIL")
        for f in fails:
            say("  " + f)
        return 1
    say("\ntrkscrl: %d jumps, every one a single blit and pixel-identical to a "
        "repaint" % len(JUMPS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
