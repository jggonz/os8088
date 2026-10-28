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
import atexit
import os
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import dispcp                                                # noqa: E402
import heapmap                                               # noqa: E402
import os88build                                             # noqa: E402
import os88qemu                                              # noqa: E402
import os88sym                                               # noqa: E402

IMG = os.path.join(ROOT, os88build.at("build/dosxmsq.img"))
DISKB = (610, 110)                  # tests/xmcheck.py's, and for its reason
ROW = (180, 128)
TEXTBASE = 0xB8000                  # the fsx bracket's 80x25, on a VGA guest


def fail(msg):
    print("dosxmsq: FAIL: %s" % msg)
    sys.exit(1)


def screen(q):
    """The 80x25 text screen, as 25 stripped lines."""
    raw = q.read(TEXTBASE, 80 * 25 * 2)
    if len(raw) < 80 * 25 * 2:
        return []
    out = []
    for r in range(25):
        row = raw[r * 160:(r + 1) * 160:2]
        out.append("".join(chr(c) if 32 <= c < 127 else " " for c in row)
                   .rstrip())
    return out


def boot():
    """`make test TESTAPPS=build/dosxmsq.img`'s machine WITHOUT make
    (os88qemu.launch_test: its own socket, copies of both floppies)."""
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    atexit.register(shutil.rmtree, tmp, True)   # (after the kill: LIFO)
    sock, _ = os88qemu.launch_test(tmp, apps=IMG)
    return heapmap.Qmp(sock)


def main():
    if not os.path.exists(IMG):
        fail("%s is missing - `make doscom` builds the gate disks" % IMG)
    q = boot()
    S = os88sym.linear
    hmp = lambda *c: [q.hmp(x) for x in c]                  # noqa: E731

    # EVERY WAIT ON THE GUEST'S STATE AND CLOCK (SOAK-SPEED 14), where it
    # was a host sleep: 22 s to a desktop, 7 s for a window, a 1 s poll
    def zone():
        try:
            return dispcp.drive_ordinal(q, S, "B") is not None
        except Exception:                                   # noqa: BLE001
            return False
    if not os88qemu.acted(q, zone, secs=90, what="drive B's zone"):
        fail("drive B: never got a desktop zone - no desktop")
    os88qemu.ui_done(q, S, cap=1.0)

    print("dosxmsq: opening Disk B and launching DOSXMSQ.COM")
    os88qemu.mouse_dblclick(q, S, hmp, *DISKB)      # closed-loop (15)
    if not os88qemu.acted(q, lambda: bool(dispcp.win_list(q, S, check=False)),
                          secs=20, what="the Disk B window"):
        fail("double-clicking B:'s zone opened no window")
    os88qemu.ui_done(q, S, cap=7.0)                 # ...its rows listed
    os88qemu.mouse_dblclick(q, S, hmp, *ROW)

    rows = []

    def ready():
        rows[:] = screen(q)
        return any("READY" in r for r in rows)
    if not os88qemu.acted(q, ready, secs=180.0, what="READY"):
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
