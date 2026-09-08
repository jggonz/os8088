#!/usr/bin/env python3
"""`vid_setmode` leaves the VGA framebuffer BLACK, whatever the ROM did
(SPEC.md 39.23).

    python3 tests/vgadirty.py

Two of `vid_setmode`'s three arms clear the framebuffer themselves - Hercules
behind its own blank, CGA by zeroing B800 before the mode set - and two places
in the kernel state as fact that "vid_setmode clears the framebuffer"
(`spl_reset`, `vid_switch`). The VGA arm trusted `int 10h AX=0012h`, and on
86Box that trust was misplaced: the loading screen came up drawn over a full
field of banded dashes, the BIOS's own title text speckled by it because
teletype into a graphics mode is transparent. The bands are the MODE 3
CHARACTER GENERATOR - it lives in plane 2 and is bitmap the instant the card is
in 12h - so a ROM that clears only plane 0, or clears B8000, leaves exactly
that.

**No emulator in this tree can show it**, which is the whole difficulty and is
what the CGA arm's own comment says about its half of the same bug. So the
kernel is asked to BE that ROM: `make VGADIRTY=1` fills A0000 with 0xDB in the
one window a machine cannot - after the ROM's mode set and before ours -
which is `DIRTYRAM=1`'s shape one device along and exists for its reason.

WHAT IT ASSERTS. With the framebuffer dirtied on purpose, the loading screen
comes up on BLACK: every pixel outside the dialog's own rectangle is 0. The
pattern is a stripe rather than a solid so that a clear reaching only some of
the four planes fails differently from one reaching none - a partial clear
leaves a colour, not a shade of the same black.

It also checks the dialog is THERE, because a screen that is black because
nothing drew on it would pass the first assertion perfectly.

QEMU, and not on CLAUDE.md's list by exception: it is the only VGA machine
this container can boot. Nothing here is a time.
"""
import os
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                              # noqa: E402
import os88build                                            # noqa: E402
import os88sym                                              # noqa: E402
import shot                                                 # noqa: E402
import os88qemu                                              # noqa: E402

PIDFILE = os.path.join(ROOT, "build", "qemu.pid")
SOCK = os.path.join(ROOT, "build", "qmp.sock")
PPM = os.path.join(ROOT, "build", "vgadirty.ppm")

# What the loading screen legitimately draws on, from splash.inc. Everything
# outside these two rectangles is background and nothing ever touches it.
#
#   the dialog  - spl_rechrome's outer frame, 152..487 x 208..343, with the
#                 bar's trough and the BIOS title (row 15 of the 80x25 grid)
#                 inside it
#   the spinner - spl_spin's own erase box, SPL_TOP*80 + 33 for 15 bytes and
#                 41 rows: x 264..383, y 118..158
#
# Both are given a couple of pixels of margin, because the point of this gate
# is a framebuffer full of 0xDB and not the chrome's exact extent.
BOXES = ((150, 206, 490, 346),
         (260, 114, 388, 162))


def kill_stale():
    """By PIDFILE, never by pkill -f (CLAUDE.md): `-f` matches the killing
    shell's own command line, so a script that names the emulator kills
    itself - exit 144, nothing dead, every command after it skipped."""
    if os.path.exists(PIDFILE):
        try:
            os.kill(int(open(PIDFILE).read().strip()), signal.SIGTERM)
            time.sleep(1)
        except Exception:
            pass
    for f in (SOCK, PIDFILE):
        if os.path.exists(f):
            os.unlink(f)


# NAMED TARGETS, never a bare `make`: the default goal runs the fast tier, and
# `api-abi` there re-assembles the kernel plain and compares it against
# kernel.bin - which is exactly what a knob kernel is not (CLAUDE.md's third
# trap). So a bare `make VGADIRTY=1` fails on its own instrumentation.
# os88build.tree() names its targets for the same reason.
TARGETS = ("os8088.img", "apps.img")

# THE KNOB KERNEL LIVES IN A TREE OF ITS OWN (tools/os88build.py), so this row
# no longer writes build/ and no longer has to put it back. It used to be
# `make VGADIRTY=1 build/os8088.img build/apps.img` followed by a `make` in a
# `finally` - and the `finally` could only promise the restore, not deliver
# it: a killed Python runs none.
KNOB = os88build.tree("VGADIRTY=1", targets=TARGETS).apply()


def launch():
    em = "qemu" + "-system-i386"     # never whole on a command line: kill_stale
    subprocess.run(
        em + " -S -machine pc,vmport=off"   # the msserial mouse drives this
        " -drive file=%s,format=raw,if=floppy -boot a"                # (SPEC.md
        " -drive file=%s,format=raw,if=floppy,index=1"                # 9.11)
        " -chardev msmouse,id=m0 -serial chardev:m0"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
        % (KNOB.img("os8088.img"), KNOB.img("apps.img"), SOCK, PIDFILE),
        cwd=ROOT, shell=True, check=True)
    # ...and it is DAEMONISED, so it outlives this script unless
    # somebody kills it - and the somebody is us (os88qemu).
    os88qemu.own(PIDFILE, SOCK)


def screen(q):
    q.hmp('screendump "%s"' % PPM)
    w, h, pix = shot.read_ppm(PPM)
    os.unlink(PPM)
    return w, h, pix


def main():
    kill_stale()
    try:
        launch()
        q = heapmap.Qmp(SOCK)
        for _ in range(200):
            try:
                q.hmp("info status")
                break
            except OSError:
                time.sleep(0.1)
        # The kernel under test is a KNOB kernel, so the symbol reader has to
        # be told which one (tools/os88sym.py's byte-identity check is right
        # to refuse otherwise - a map of a different kernel is a WRONG answer,
        # not a missing one).
        os88sym.default_defines("VGA_DIRTY")
        live = os88sym.linear("spl_live")
        q.hmp("cont")

        # Catch the splash mid-load: [spl_live] is 1 from its first tick until
        # spl_finish, and the frame is the same background throughout.
        shot_at, t0 = None, time.time()
        while time.time() - t0 < 60:
            if q.read(live, 1)[0] == 1:
                time.sleep(0.4)             # ...one bar frame in, so the
                shot_at = screen(q)         # chrome is certainly drawn
                break
            time.sleep(0.05)
        if shot_at is None:
            raise SystemExit("vgadirty: the loading screen never came up - "
                             "this machine did not boot, so nothing below "
                             "would mean what it says")
        q.hmp("quit")
    finally:
        kill_stale()

    w, h, pix = shot_at
    dirty, inbox = [], 0
    for y in range(h):
        for x in range(w):
            p = pix[(y * w + x) * 3:(y * w + x) * 3 + 3]
            if any(x0 <= x < x1 and y0 <= y < y1 for x0, y0, x1, y1 in BOXES):
                inbox += 1 if any(p) else 0
                continue
            if any(p):
                dirty.append((x, y, p.hex()))

    fail = []
    print("  %dx%d screen, %d lit pixels inside the chrome, %d outside it"
          % (w, h, inbox, len(dirty)))
    if dirty:
        print("  first six outside: %s" % (dirty[:6],))
        fail.append("%d pixels survive outside the chrome on a framebuffer "
                    "VGADIRTY=1 filled with 0xDB. vid_setmode's VGA arm is "
                    "not clearing it (SPEC.md 39.23) - and if some of them "
                    "are coloured rather than white, the clear reached some "
                    "planes and not all four, which is the Map Mask"
                    % len(dirty))
    if inbox < 500:
        fail.append("only %d lit pixels inside the chrome: the screen is "
                    "black because NOTHING DREW, which passes the assertion "
                    "above for the wrong reason" % inbox)

    for f in fail:
        print("FAIL: %s" % f)
    print("vgadirty: %s" % ("FAILED" if fail else
                            "the loading screen comes up on black over a "
                            "framebuffer the ROM did not clear"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
