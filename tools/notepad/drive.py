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
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
MARTY = os.path.join(ROOT, "tools", "os88marty.py")
ADDR = "127.0.0.1:9001"

# Where things sit on a 640x200 CGA desktop booted from build/npbench360.img.
DISK_A = (600, 42)          # the Disk A drive zone
README = (175, 100)         # README.TXT's row in a freshly opened Disk window


def kill_emulator():
    """By PID, never by pattern: a `pkill -f martypc` from inside a shell
    tool call matches the calling shell too, and kills it."""
    out = subprocess.run(["ps", "-eo", "pid,comm"], capture_output=True,
                         text=True).stdout
    for line in out.splitlines():
        if "marty" in line.lower():
            subprocess.run(["kill", line.split()[0]], check=False)
    time.sleep(2)


def start(image="build/npbench360.img", machine="os8088_5150_cga"):
    """Cold boot to the desktop. Blocking; ~60 s of wall clock."""
    kill_emulator()
    run = os.path.join(ROOT, "build", "martypc", "run")
    base = os.path.basename(image)
    subprocess.run(["cp", os.path.join(ROOT, image),
                    os.path.join(run, "media", "floppies", base)], check=True)
    subprocess.Popen(
        ["./martypc_headless", "--machine-config-name", machine,
         "--mount", "fd:0:media/floppies/" + base],
        cwd=run, env=dict(os.environ, MARTYPC_DEBUG_ADDR=ADDR),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(12)
    subprocess.run(["python3", MARTY, ADDR, "run"], check=True,
                   stdout=subprocess.DEVNULL)
    time.sleep(45)


def shot(path, timeout=60):
    """A rendered screenshot. NOT check=True on purpose: rasterising a frame
    can outrun os88marty.py's default socket timeout on a busy host, and a
    convenience capture failing must not abort the run it was documenting."""
    r = subprocess.run(["python3", MARTY, ADDR, "--timeout", str(timeout),
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


def goto(m, x, y):
    for _ in range(9):                      # into the top-left corner clamp
        m.mouse(-100, -100)
    m.mouse_move(x, y, step=100)


def dclick(m, x, y):
    """Two clicks inside SPEC.md 13's 9-tick window, on one connection."""
    goto(m, x, y)
    time.sleep(0.3)
    m.mouse(0, 0, l=True)
    m.mouse(0, 0)
    m.mouse(0, 0, l=True)
    m.mouse(0, 0)


def open_readme(m):
    """Disk A -> README.TXT, which SPEC.md 54's association opens in Note Pad.

    The npbench disk carries the bench build as APPS/NOTEPAD.O88 precisely so
    that a double-click gets there - see the Makefile's npbench target.
    """
    dclick(m, *DISK_A)
    time.sleep(5)
    dclick(m, *README)
    time.sleep(25)
