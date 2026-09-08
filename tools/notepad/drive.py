# =============================================================================
# drive.py - boot the npbench disk and get a note open, the same way twice
#
# The mouse is RELATIVE and the kernel clamps it at the screen edge, so the
# way to an absolute position is to slam into a corner first and then move by
# a known delta - tools/mouse.py's trick, needed here for its reason.
#
# One trap of MartyPC's own: its mouse scales deltas by 0.25 by default, so an
# unscaled move lands a quarter of the way and every click misses in a way
# that reads as a broken hit-test. os88marty.py's mouse command forces the
# scaler to 1.0, and going through it is why this works at all.
# =============================================================================
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
MARTY = os.path.join(ROOT, "tools", "os88marty.py")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M                                       # noqa: E402

# WHERE THE BENCH IS, written by start() and read by every command after it.
# The lab is a boot-once-poke-many workflow - `lab.py boot`, then `lab.py
# verify`, then pixcheck - so the machine has to outlive the command that
# started it, and the address is therefore a FACT ON DISK rather than a
# constant. It used to be a hard-coded 127.0.0.1:9001, which is also why
# start() used to kill every martypc_headless on the box before it began:
# with one port there was nowhere else to put a second machine, and a
# survivor holding it meant driving the wrong one. Neither is true now
# (docs/MARTYPC-DEBUG.md).
ADDRFILE = os.path.join(ROOT, "build", "notepad-lab.addr")


def addr():
    """The bench's address, or the old fixed port if nobody has started one."""
    try:
        with open(ADDRFILE) as f:
            return f.read().strip() or "127.0.0.1:9001"
    except OSError:
        return "127.0.0.1:9001"

# Where things sit on a 640x200 CGA desktop booted from build/npbench360.img.
DISK_A = (600, 42)          # the Disk A drive zone
README = (175, 100)         # README.TXT's row in a freshly opened Disk window


def kill_emulator():
    """End THIS LAB's bench, and nothing else.

    It used to `kill` every process whose name contained "marty", which is the
    behaviour the instance layer exists to remove: two labs, or a lab and a
    test run, took turns ending each other's machines and the loser saw a
    dead socket with nothing to explain it. Now it kills the one instance this
    lab started, by port, out of the registry.
    """
    a = addr()
    try:
        M.kill_one(a.rpartition(":")[2])
    except M.MartyError:
        pass                            # nothing of ours is running
    try:
        os.remove(ADDRFILE)
    except OSError:
        pass


def start(image="build/npbench360.img", machine="os8088_5150_cga"):
    """Cold boot to the desktop, and LEAVE IT RUNNING for the commands after.

    Blocking, and it now waits for the desktop rather than for a stopwatch:
    `launch` settles on the screen, which is the same wait the whole test
    tree uses and is both quicker and right on a loaded box (the 12 s + 45 s
    it replaces were guesses, and a busy host missed them).
    """
    kill_emulator()
    m = M.launch(os.path.join(ROOT, image), machine=machine,
                 label="notepad-lab", detach=True)
    with open(ADDRFILE, "w") as f:
        f.write(m.addr + "\n")
    m.close()                           # the connection, not the machine
    return m.addr


def shot(path, timeout=60):
    """A rendered screenshot. NOT check=True on purpose: rasterising a frame
    can outrun os88marty.py's default socket timeout on a busy host, and a
    convenience capture failing must not abort the run it was documenting."""
    r = subprocess.run(["python3", MARTY, addr(), "--timeout", str(timeout),
                        "shot", path, "--rendered"], check=False)
    return r.returncode == 0


def tap(m, key, frames=120):
    """One keystroke, and GUEST TIME TO DELIVER IT. Use this, not m.key().

    A bare `key` followed by `advance(frames=...)` does not reliably run the
    guest: measured from a machine left in MartyPC's latched BreakpointHit
    state, the keystroke sat in the emulator's keyboard queue and the advance
    returned having delivered nothing. `step(1)` first - the same thing
    Tracer.collect does, which is why the tracer never showed this.

    The failure mode is the nasty one: the key is QUEUED, not dropped, so it
    arrives during some later advance and the measurement that inherits it
    reports a keystroke nobody asked for. Three characters typed this way
    turned up all at once on the fourth.
    """
    m.key(key)
    m.step(1)
    m.advance(frames=frames)


def chord(m, mods, key, frames=120):
    """A modified keystroke - chord(m, ["ControlLeft"], "KeyZ").

    Each event needs guest time of its own: press, key, release with nothing
    running in between puts four scancodes into the controller in the same
    instant, and an XT delivers one per IRQ1.
    """
    for mod in mods:
        m.key(mod, down=True, up=False)
        m.step(1)
        m.advance(frames=2)
    tap(m, key, frames=frames)
    for mod in reversed(mods):
        m.key(mod, down=False, up=True)
        m.step(1)
        m.advance(frames=2)


def _pkt(m, dx=0, dy=0, l=False, frames=2):
    """One mouse packet AND the guest time to clock it in.

    Same trap as tap(): on a paused machine - which is where every `advance`
    leaves it - packets queue instead of arriving, and a later advance then
    delivers a burst. It also matters on a running one, because the serial
    mouse is 1200 baud and mou_isr decodes three bytes at a time: packets sent
    faster than the UART can carry them overrun its receive register, which is
    tools/mouse.py's pacing rule for QEMU and true here for the same reason.
    """
    m.mouse(dx, dy, l=l)
    m.step(1)
    m.advance(frames=frames)


def goto(m, x, y):
    for _ in range(9):                      # into the top-left corner clamp
        _pkt(m, -100, -100, frames=1)
    while x or y:
        sx = max(-100, min(100, x))
        sy = max(-100, min(100, y))
        _pkt(m, sx, sy)
        x -= sx
        y -= sy


def click(m, x, y, frames=120):
    goto(m, x, y)
    _pkt(m, l=True)
    _pkt(m, frames=frames)


def dclick(m, x, y, frames=200):
    """Two clicks inside SPEC.md 13's 9-tick (~0.5 s) window.

    The gap between them is guest time and has to be SMALL: the birth-tick
    comparison is what makes it a double-click rather than two clicks, so the
    two presses get 2 frames apart and only the tail waits.
    """
    goto(m, x, y)
    _pkt(m, l=True)
    _pkt(m)
    _pkt(m, l=True)
    _pkt(m, frames=frames)


def pkg_present(m, name=b"NOTEPAD", lo=0x1000, hi=0xA000):
    """Is a v3 package header carrying `name` anywhere in RAM yet?"""
    for base in range(lo, hi, 0x400):
        try:
            data = m.read(base * 16, min(0x400, hi - base) * 16)
        except Exception:
            continue
        for i in range(0, len(data) - 32, 16):
            if (data[i] == 0x4F and data[i + 1] == 0x38 and data[i + 2] == 3
                    and bytes(data[i + 16:i + 32]).split(b"\0")[0] == name):
                return True
    return False


def wait_until(m, pred, frames=60, tries=60):
    """Advance guest time until `pred` holds. Returns True if it did.

    A CONDITION, not a sleep. docs/plans/completed/NOTEPAD-NOTES.md 6.1 is a whole wrong
    answer caused by two runs waiting different lengths of time, and a wall
    sleep cannot be matched across builds that differ in speed - this can,
    because it stops when the thing has actually happened.
    """
    for _ in range(tries):
        if pred():
            return True
        m.advance(frames=frames)
    return pred()


def open_readme(m):
    """Disk A -> README.TXT, which SPEC.md 54's association opens in Note Pad.

    The npbench disk carries the bench build as APPS/NOTEPAD.O88 precisely so
    that a double-click gets there - see the Makefile's npbench target.
    """
    dclick(m, *DISK_A)
    wait_until(m, lambda: True, frames=120, tries=1)
    dclick(m, *README)
    if not wait_until(m, lambda: pkg_present(m)):
        raise SystemExit("notepad/drive: README.TXT never opened - check the "
                         "DISK_A/README coordinates against a screenshot")
