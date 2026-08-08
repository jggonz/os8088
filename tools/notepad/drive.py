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


def shot(path):
    subprocess.run(["python3", MARTY, ADDR, "shot", path, "--rendered"],
                   check=True)


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
