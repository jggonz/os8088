#!/usr/bin/env python3
"""The DOS XMS WORKING gate (SPEC.md 96.15, 96.15.3).

tests/dosxms.py asserts the REFUSAL on an 8088 with no store. This is its
twin and asserts the other half: on a machine that HAS extended memory, a DOS
program finds the driver, allocates a block, moves a pattern out, wipes the
conventional copy, moves it back, and gets every byte.

WHY QEMU: the machine must have a store above 1MB, and the target machine
never can - an 8088 has no A20 line and nothing above linear 0x0FFFFF
(SPEC.md 41.9 rule 1). That is entry 1 on docs/TESTING.md's short list, and
tests/xmcheck.py is here for exactly the same reason.

WHAT IT CATCHES that an "did it allocate" test cannot: the move block's
handle/offset pairs. A handle of 0 means CONVENTIONAL memory and its offset
is then a FAR POINTER rather than a linear one, so a layer that reads the
pairs the wrong way round allocates perfectly and brings back rubbish - which
is why the pattern is checked byte for byte and is not a block of zeros.

THE TEXT SCREEN IS READ OUT OF 0xB8000, because there is no os88ui for QEMU
and the guest is in the fsx bracket's 80x25 text mode - so the framebuffer IS
the output, char and attribute byte about.
"""
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88qemu                                              # noqa: E402
import os88fixture                                           # noqa: E402

SOCK = os.path.join(ROOT, "build", "qmp.sock")
PID = os.path.join(ROOT, "build", "qemu.pid")
IMG = os.path.join(ROOT, "build", "dosxmsq.img")
DISKB = (610, 110)                  # tests/xmcheck.py's, and for its reason
ROW = (180, 128)
TEXTBASE = 0xB8000                  # the fsx bracket's 80x25, on a VGA guest


def fail(msg):
    print("dosxmsq: FAIL: %s" % msg)
    sys.exit(1)


def qmp(*cmds):
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "qmp.py"),
                        SOCK, *cmds], capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        fail("qmp failed:\n" + (r.stderr or r.stdout)[-500:])
    return r.stdout


def read_bytes(addr, n):
    data = bytearray()
    for line in qmp("xp /%dxb 0x%X" % (n, addr)).splitlines():
        if ":" in line:
            for tok in line.split(":", 1)[1].split():
                if tok.startswith("0x"):
                    data.append(int(tok, 16) & 0xFF)
    return data


def screen():
    """The 80x25 text screen, as 25 stripped lines."""
    raw = read_bytes(TEXTBASE, 80 * 25 * 2)
    if len(raw) < 80 * 25 * 2:
        return []
    out = []
    for r in range(25):
        row = raw[r * 160:(r + 1) * 160:2]
        out.append("".join(chr(c) if 32 <= c < 127 else " " for c in row)
                   .rstrip())
    return out


def dblclick(x, y):
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mouse.py"),
                    SOCK, "to", str(x), str(y)], capture_output=True, cwd=ROOT)
    qmp("mouse_button 1", "sleep 0.08", "mouse_button 0",
        "sleep 0.08", "mouse_button 1", "sleep 0.08", "mouse_button 0")


def boot():
    if os.path.exists(PID):
        try:
            os.kill(int(open(PID).read().strip()), 15)
            time.sleep(1.0)
        except (OSError, ValueError):
            pass
    for f in (SOCK, PID):
        if os.path.exists(f):
            os.remove(f)
    os88qemu.own()
    r = os88fixture.make("test", "TESTAPPS=" + IMG)
    if r.returncode:
        fail("make test failed:\n" + r.stdout + r.stderr)
    for _ in range(150):
        if os.path.exists(SOCK):
            return
        time.sleep(0.2)
    fail("QMP never came up")


def main():
    if not os.path.exists(IMG):
        fail("%s is missing - `make doscom` builds the gate disks" % IMG)
    boot()
    time.sleep(22)                  # ...to a desktop

    print("dosxmsq: opening Disk B and launching DOSXMSQ.COM")
    dblclick(*DISKB)
    time.sleep(7)
    dblclick(*ROW)

    rows = []
    end = time.time() + 180.0
    while time.time() < end:
        rows = screen()
        if any("READY" in r for r in rows):
            break
        time.sleep(1.0)
    else:
        fail("the program never finished; the text screen was %r"
             % ([r for r in rows if r.strip()][:12],))

    print("dosxmsq: the bracket's text screen:")
    for r in rows[:14]:
        if r.strip():
            print("   | %s" % r)
    text = "\n".join(rows)

    for line in (l.strip() for l in rows):
        if line.startswith("FAILED"):
            fail("the program reported: %s" % line)
    if "XMSVER 0300" not in text:
        fail("the driver did not answer XMS 3.0 (SPEC.md 96.15.2)")
    if "XMS ok" not in text:
        fail("the out-and-back move did not complete")

    free = None
    for r in rows:
        if r.strip().startswith("XMSFREE "):
            try:
                free = int(r.split()[1])
            except (IndexError, ValueError):
                pass
    if not free:
        fail("the driver reported %r KB free on a machine that has a store - "
             "AH=08h answers OSAPI_XMEM_CAPS and a zero there is the refusal "
             "path, not this one" % (free,))
    print("dosxmsq: %d KB free, allocated, moved out, wiped, moved back, "
          "every byte" % free)
    print("dosxmsq: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
