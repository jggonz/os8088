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
import atexit
import json
import os
import shutil
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
import os88sym                                               # noqa: E402

DISK = "build/trkscrl.img"

# Where things are on a 640x480 desktop with build/trkscrl.img in drive B:
# the B: drive zone, and BEVERLY.MOD's row in the window it opens (it sorts
# first - the only other file is TRKSCRL.O88). These two are the ONLY thing
# here that is the VGA desktop's - the row area under test is read out of the
# guest's live layout record - so `make test VIDEO=cga` needs its own pair
# before this can run there, and tui_lay_cga's shorter regions are worth
# covering when somebody wants them.
DISKB = (600, 112)
MODROW = (175, 128)
SCREEN = [640, 480]                 # replaced by the first screendump
                                    # (the line-doubling scale below)

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
        # QEMU writes the file on its side: a HOST wait, on the file being
        # WHOLE (its header's size) rather than on a 0.4s guess.
        d = b""
        try:
            for _ in range(250):
                if os.path.exists(p):
                    d = open(p, "rb").read()
                    f = d.split(b"\n", 3)
                    if len(f) == 4 and len(f[1].split()) == 2:
                        w, h = (int(v) for v in f[1].split())
                        if len(f[3]) >= w * h * 3:
                            break
                time.sleep(0.02)
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
        """ONE KEY, CONFIRMED - os88qemu.key's shape over THIS connection
        (QEMU's socket takes one client, and os88qemu.key opens its own):
        the press, then the release only once the ROM has STORED the press
        (the BIOS buffer's tail moved), or after 0.3 guest seconds - inside
        the 500 ms typematic delay - for a key it stores nothing for. It was
        HMP's `sendkey`, which queues the release 100 ms of HOST time after
        the press: a guest the host starved past that takes both bytes at
        one IRQ1 and the key is lost (docs/plans/SOAK-PARALLEL.md 17.5, the
        last unconfirmed HMP client, 17.9)."""
        ev = {"type": "key", "data": {"down": True, "key": {
            "type": "qcode", "data": name}}}
        t0 = self.read(0x41C, 2)
        self.cmd({"execute": "input-send-event",
                  "arguments": {"events": [ev]}})
        os88qemu.acted(self, lambda: self.read(0x41C, 2) != t0, secs=0.3,
                       what="the ROM to store %s's press" % name, poll=0.005)
        ev["data"]["down"] = False
        self.cmd({"execute": "input-send-event",
                  "arguments": {"events": [ev]}})


SOCK = None


def say(*a):
    print(*a)
    sys.stdout.flush()


def boot():
    """`make test TESTAPPS=build/trkscrl.img`'s machine WITHOUT make
    (os88qemu.launch_test): its own socket and copies of both floppies, so
    no `make` runs in the shared build/ and no other row's QEMU is killed
    through build/qemu.pid. Answers the QMP socket."""
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    atexit.register(shutil.rmtree, tmp, True)   # (after the kill: LIFO)
    sock, _ = os88qemu.launch_test(tmp, apps=DISK)
    return sock


def main():
    global SOCK
    os.chdir(ROOT)
    os88fixture.need(DISK)          # (was `make trkscrl` in the shared tree)
    SOCK = boot()

    q = Qmp(SOCK)
    # EVERY WAIT BELOW IS ON THE GUEST'S CLOCK (tests/os88qemu.py), and on the
    # thing the next line needs where there is one: drive B's zone, the Disk
    # window, the player resident, and the bench counters the keys move.
    import dispcp

    def zone():
        try:
            return dispcp.drive_ordinal(q, os88sym.linear, "B") is not None
        except Exception:                                   # noqa: BLE001
            return False
    if not os88qemu.acted(q, zone, secs=90, what="drive B's zone", poll=0.05):
        raise SystemExit("trkscrl: drive B: never got a desktop zone - the "
                         "guest did not reach a desktop")
    os88qemu.ui_done(q, os88sym.linear, cap=1.0)     # (was pace(1))
    q.screen()                      # ...which is also how SCREEN is learned
    say("trkscrl: a %dx%d desktop, then B: -> BEVERLY.MOD" % tuple(SCREEN))
    # closed-loop on mouse_x/mouse_btn, every edge confirmed (SOAK-SPEED
    # 15) - well inside the 9-tick double-click window
    os88qemu.mouse_dblclick(q, os88sym.linear, q.hmp, *DISKB)
    if os88qemu.acted(q, lambda: bool(dispcp.win_list(q, os88sym.linear,
                                                      check=False)),
                      secs=20, what="the Disk B window", poll=0.05):
        os88qemu.ui_done(q, os88sym.linear, cap=1.0)    # ...its rows painted
    # SELECT, then Enter. A double-click here is the flakiest thing in this
    # file - the window has just been drawn and the second press can land
    # while it still is - and Enter opens the selection just as well.
    os88qemu.mouse_click(q, os88sym.linear, q.hmp, *MODROW)
    os88qemu.ui_done(q, os88sym.linear, cap=1.0)
    q.key("ret")

    seg = None

    def resident():
        nonlocal seg
        seg, _drv = os88rate.scan(q, DISK)
        return bool(seg)
    # 12s of settling and ten 2s re-scans was the old budget: 52 guest s
    os88qemu.acted(q, resident, secs=52, what="TRKSCRL.O88 resident",
                   poll=0.3)
    if not seg:
        raise SystemExit("trkscrl: the player never became resident.")
    P, _ = os88rate.symbols(("TRKDBG",))
    base = seg * 16
    say("trkscrl: TRKSCRL.O88 at %#06x" % seg)

    def peek(name, n=2):
        d = q.read(base + P["@" + name], n)
        return d[0] if n == 1 else int.from_bytes(d, "little")

    # ...and the module read in behind it: [mp_loaded] is the player's own
    # answer, and the open's tail (trk_play, the completion repaint) is its
    # state going still - tests/trkrate.py's pair. This was a blind 4s
    os88qemu.acted(q, lambda: peek("mp_loaded", 1) == 1, secs=30,
                   what="[mp_loaded]", poll=0.05)
    os88qemu.quiesce(q, lambda: tuple(
        q.read(base + P["@" + n], 2) for n in
        ("mp_loaded", "mp_playing", "mp_mixrate", "mp_xt", "trk_fs")),
        secs=0.5, limit=4.0, what="Tracker's open to finish")

    q.key("f")                          # into the graphics fullscreen
    if os88qemu.acted(q, lambda: peek("trk_fs", 1) == 1, secs=10,
                      what="[trk_fs]", poll=0.05):
        os88qemu.pace(q, 1)             # ...and its first frame
    if peek("trk_fs", 1) != 1:
        raise SystemExit("trkscrl: F did not enter the bracket.")
    if peek("mp_xt", 1) != 0:
        raise SystemExit("trkscrl: XT mode is on, so the surface under test is "
                         "SPEC.md 45.13's text screen and not this one.")
    for _ in range(6):                  # SPACE is a TOGGLE, and whether the
        if peek("mp_playing", 1) == 0:  # module started depends on whether the
            break                       # machine has a card at all
        q.key("spc")
        os88qemu.acted(q, lambda: peek("mp_playing", 1) == 0, secs=2,
                       what="playback stopped", poll=0.05)
    if peek("mp_playing", 1) != 0:
        raise SystemExit("trkscrl: could not stop playback; the view would "
                         "move between the two screenshots.")

    # The row area under test, as the GUEST's live layout has it (the bench
    # build publishes it - tests/trkscrl.inc says why). Everything above it -
    # the readouts, the scopes, the status line - is drawn by other paths on
    # their own schedule and is not what this gate is about.
    def counters():
        return (peek("tds_keys"), peek("tui_vrow", 1), peek("tds_scrl"),
                peek("tds_pat"))

    def answered(key, before):
        """A key sent, and the bracket's own counters its answer: seen, then
        still for a second of the machine - which is what 1.5 host seconds
        were a guess at, and what a loaded box cannot shorten."""
        q.key(key)
        os88qemu.acted(q, lambda: peek("tds_keys") != before[0], secs=3,
                       what="%s at the bracket" % key, poll=0.05)
        os88qemu.quiesce(q, counters, secs=0.5, stable=2, limit=10,
                         what="the bench counters", poll=0.05)

    answered("g", counters())        # ...which also publishes the bounds
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
        k0 = peek("tds_keys")
        v0 = peek("tui_vrow", 1)
        answered(key, (k0, v0, s0, p0))
        v1, s1, p1 = peek("tui_vrow", 1), peek("tds_scrl"), peek("tds_pat")
        k1 = peek("tds_keys")
        w, a = q.screen()
        answered("g", counters())       # the same view, drawn the other way
        _w, b = q.screen()
        bad = [y for y in range(y0, y1) if a[y] != b[y]]
        moved, scrolls, repaints = v1 - v0, s1 - s0, p1 - p0
        # **PER KEY THE BRACKET ACTUALLY SAW, and that is not pedantry.**
        # `q.key("down")` is one `sendkey`, and on QEMU the guest gets TWO
        # int 16h entries for it - MEASURED with `tds_keys` (the counter in
        # trk_fsx_key, TRKDBG only): arrows 2, every ASCII key 1. It is not
        # this application's doing and not the bracket's: the WINDOWED route
        # doubles identically (`tds_wkeys` 2 through trk_onkey, which ui_task
        # dispatches), ui_task dispatches exactly one W_ONKEY per key it takes
        # from int 16h, and the kernel's ONLY write to the BIOS key buffer is
        # kbd_ovflow's rewind, which REMOVES an entry and only when it is
        # full. So the ROM enqueued twice, and under QEMU that ROM is SeaBIOS.
        #
        # This row's subject is SPEC.md 45.12.2 - a jump of n rows costs ONE
        # gfx_scroll and no full repaint - so the honest expectation against
        # two presses is two scrolls of one row each, which is what it reads.
        # Scaling by the OBSERVED count keeps that subject measurable instead
        # of reporting the emulator's keyboard as a scroll defect, which is
        # the misdiagnosis this row already
        # made about this exact row once.
        keys = k1 - k0
        ok = (keys >= 1 and moved == rows * keys and scrolls == keys
              and repaints == 0 and not bad)
        say("  %-4s %+2d rows: view %2d->%2d  scrolls %d  repaints %d  "
            "keys %d  %s"
            % (key, rows, v0, v1, scrolls, repaints, keys,
               "ok" if ok else "FAIL"))
        if keys != 1:
            say("       NOTE: one sendkey, %d key(s) at the app - the ROM's, "
                "not this program's (see the comment here)" % keys)
        if keys < 1:
            fails.append("%s never reached the program at all" % key)
        elif moved != rows * keys:
            fails.append("%s moved %d rows, not %d - %d key(s) of %+d"
                          % (key, moved, rows * keys, keys, rows))
        elif scrolls != keys or repaints != 0:
            fails.append("%s took %d scroll(s) and %d full repaint(s), not %d "
                         "and 0 - the reach is gone"
                         % (key, scrolls, repaints, keys))
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
