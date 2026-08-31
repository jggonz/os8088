#!/usr/bin/env python3
"""The PS/2 mouse reaches the pointer, and the keyboard survives it
(SPEC.md 9.9).

    python3 tests/ps2mouse.py

docs/TESTING.md has carried this as a RECIPE since SPEC.md 9.9 landed - a
list of addresses to read with `xp` after `make test MOUSEPORT=ps2` - and a
recipe is a thing somebody has to remember to run. It was not run for months,
and when the feature was finally tried on an 86Box 386SX it did not work at
all. Six rounds of that were spent on a bug (SPEC.md 9.9.1's IRQ1 mask) that
QEMU cannot reproduce; the rows below would not have caught it either. What
they catch is the OTHER half - every change to the handshake, the decode or
the contest that this container CAN see - so that the next person's field
round is spent on the field's bug and not on one that was findable here.

QEMU, and on CLAUDE.md's closed list by name: MartyPC is an 8088 and an XT's
keyboard is an 8255 PPI, so `[cpu_tier]` refuses the whole module there and
there is no "prefer MartyPC" to weigh. Nothing here is a time.

WHAT IT ASSERTS, in the order the handshake makes it true:

  mou_bases   0000 0000 - `-serial none`, so the serial probe rejected both
                          rows and the contest cannot even be entered. This
                          is what makes the rest a statement about the PS/2
                          half alone
  mou_p2st    9         - the handshake ran to the end. It is the STEP, so a
                          failure names where: 1 the command byte, 2 the
                          auxiliary-port question, 4/5/6 the device's reset
  mou_p2      1         - ...and did not lose a contest afterwards
  mou_p2id    0         - a plain 3-byte mouse

then one exact move, which is the row that catches the defect a PS/2 driver
actually has:

  mou_seen    1
  mou_port    4         - MOU_P2ROW, one past the last serial row
  mou_line    FF        - MOU_P2LINE
  mou_ptr     1         - so the keyboard mouse stands down (SPEC.md 9.6.6)
  mouse_x/y   EXACTLY the requested pixel

tools/mouse.py pins against the kernel's own edge clamp and walks back by
exact deltas, so landing on the pixel is a statement about the sign handling
and SPEC.md 9.9.3's Y inversion - "positive is up" - which nothing else here
would notice. A mouse with the Y sense inverted moves, draws, clicks and drags
perfectly and simply goes the wrong way.

And last, the one that is not about the mouse at all and is the reason the
handshake is written the way it is: THE KEYBOARD MUST SURVIVE. The 8042 has
one output buffer for two devices, and both `mou_p2_init`'s reads and the
ISR's are a chance to take a keystroke away from int 09h. Six keys must
advance the BIOS buffer's tail by exactly twelve bytes.
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
import os88sym                                              # noqa: E402

SOCK = os.path.join(ROOT, "build", "ps2.sock")
PIDFILE = os.path.join(ROOT, "build", "ps2.pid")

# The BIOS keyboard buffer's head and tail (0040:001A), which is where "did a
# keystroke survive the handshake" is answered. Two bytes a key.
KBHEAD = 0x41A

TARGET_X, TARGET_Y = 200, 150


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


# NAMED TARGETS, never a bare `make`: the default goal runs the fast tier and
# api-abi there re-assembles the kernel and compares it against
# build/kernel.bin, so a bare `make` inside a test fights its own harness.
TARGETS = ["build/os8088.img", "build/apps.img"]


def build():
    subprocess.run(["make"] + TARGETS, cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)


def launch():
    """`-serial none` and no msmouse chardev: the guest gets NO UARTs at all,
    so the only pointing device on the machine is the PS/2 mouse the `pc`
    machine has anyway (docs/TESTING.md)."""
    em = "qemu" + "-system-i386"     # never whole on a command line: kill_stale
    subprocess.run(
        em + " -drive file=build/os8088.img,format=raw,if=floppy -boot a"
        " -drive file=build/apps.img,format=raw,if=floppy,index=1"
        " -serial none"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
        % (SOCK, PIDFILE), cwd=ROOT, shell=True, check=True)


def byte(q, sym):
    return q.read(os88sym.linear(sym), 1)[0]


def word(q, sym):
    b = q.read(os88sym.linear(sym), 2)
    return b[0] | (b[1] << 8)


def main():
    kill_stale()
    build()
    fails = []
    try:
        launch()
        q = heapmap.Qmp(SOCK)
        for _ in range(200):
            try:
                q.hmp("info status")
                break
            except OSError:
                time.sleep(0.1)

        # Wait for the handshake to have run. mouse_init is inside kmain, so
        # [mou_p2st] settling is the signal - poll it rather than sleeping a
        # guessed interval, and give the floppy boot room.
        t0, st = time.time(), 0
        while time.time() - t0 < 90:
            st = byte(q, "mou_p2st")
            if st == 9:
                break
            time.sleep(0.25)

        bases = q.read(os88sym.linear("mou_bases"), 4)
        if any(bases):
            fails.append("mou_bases %s - a serial row probed PRESENT, so this "
                         "run is not the PS/2-only case it is meant to be"
                         % bases.hex())
        if st != 9:
            fails.append("mou_p2st %d, want 9: the handshake stopped at that "
                         "step (SPEC.md 9.9.1)" % st)
        for sym, want in (("mou_p2", 1), ("mou_p2id", 0)):
            got = byte(q, sym)
            if got != want:
                fails.append("%s %02X, want %02X" % (sym, got, want))

        # --- one exact move: the sign handling and the Y inversion ----------
        subprocess.run([sys.executable, "tools/mouse.py", SOCK, "to",
                        str(TARGET_X), str(TARGET_Y)], cwd=ROOT, check=True,
                       stdout=subprocess.DEVNULL, timeout=180)
        time.sleep(0.5)
        for sym, want in (("mou_seen", 1), ("mou_port", 4), ("mou_line", 0xFF),
                          ("mou_ptr", 1)):
            got = byte(q, sym)
            if got != want:
                fails.append("%s %02X, want %02X" % (sym, got, want))
        gx, gy = word(q, "mouse_x"), word(q, "mouse_y")
        if (gx, gy) != (TARGET_X, TARGET_Y):
            fails.append("pointer at %d,%d, want %d,%d - the deltas are exact, "
                         "so a miss is the SIGN handling or SPEC.md 9.9.3's Y "
                         "inversion, not a rounding" % (gx, gy, TARGET_X,
                                                        TARGET_Y))

        # --- ...and the keyboard, which shares the one output buffer --------
        before = q.read(KBHEAD, 4)
        tail0 = before[2] | (before[3] << 8)
        for k in "abcdef":
            q.hmp("sendkey " + k)
        time.sleep(1.0)
        after = q.read(KBHEAD, 4)
        tail1 = after[2] | (after[3] << 8)
        if (tail1 - tail0) & 0xFFFF != 12:
            fails.append("BIOS keyboard tail %04X -> %04X, want +12 for six "
                         "keys: a byte was taken from int 09h by the mouse "
                         "path (SPEC.md 9.9.1)" % (tail0, tail1))

        q.hmp("quit")
    finally:
        kill_stale()

    if fails:
        for f in fails:
            print("ps2mouse: FAIL " + f)
        return 1
    print("ps2mouse: ok - p2st 9, port 04, line FF, pointer exact on %d,%d, "
          "keyboard intact" % (TARGET_X, TARGET_Y))
    return 0


if __name__ == "__main__":
    sys.exit(main())
