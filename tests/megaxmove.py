#!/usr/bin/env python3
"""kern_mega: an OSAPI_XMEM_ALLOC block MOVES, and is read back through the
token it was given (SPEC.md 50.6.10, docs/plans/completed/KERN-MEGA-MEMORY-PLAN.md MC3).

    python3 tests/megaxmove.py            the row
    python3 tests/megaxmove.py --keep     ...and leave the boot's directory

Boots kern_mega under Bochs at 4MB with tests/megaxm's MEGAXM on C:'s RUN
folder (and the packed ZBIG.TXT its file leg reads, made here) and lets
MEGARUN start it. MEGAXM takes a 64KB filler and then its
64KB block, which lies UNDER the filler (every claim comes from the top),
writes sixteen 4KB patterns into the block, frees the filler and claims every
free KB the pool has - a claim that fits only once the pool is packed UP,
which lifts the block into the filler's hole. Then every chunk is read back
through the block's ORIGINAL token, in place through OSAPI_XMEM_SEL, through
the file layer and through a package's own int 13h:

  1. `MEGAXM: move OOOO PPPP` with PPPP above OOOO - the block moved up;
  2. `MEGAXM: ok` - every read through the token found the bytes;
  3. no kern_mega FAULT or CLOSED.

THE FAILING CASE (docs/WRITING-TESTS.md 1): pin token blocks again
(kernel/mega.inc's mx_canmove: answer ZF = 0 for an owner under 80h) and the
block stays where it was - red on (1), `MEGAXM: no 42` when the whole-pool
claim cannot be placed (verified when the row was written). Make mx_chk compare MX_OFF instead of the key and the
block is found by its old place - red on (2), `MEGAXM: no 03 0002` (seen
while the row was written, from a defect of that shape).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs  # noqa: E402
from os88fixture import need  # noqa: E402

PKGS = ["build/megaxm.o88"]


def main():
    keep = "--keep" in sys.argv
    for p in PKGS + ["build/megarun.o88", "build/mega.img",
                     "build/mbr.bin"]:
        need(p)
    out = tempfile.mkdtemp(prefix="megaxmove-")
    t0 = time.time()
    # MEGAXM's packed-file leg reads RUN\ZBIG.TXT: 50,000 bytes of 'A' + i
    # mod 26, lz4-wrapped as the build wraps a data file (os88lz.py --wrap)
    raw = os.path.join(out, "zbig.raw")
    with open(raw, "wb") as f:
        f.write(bytes(65 + i % 26 for i in range(50000)))
    zbig = os.path.join(out, "ZBIG.TXT")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88lz.py"),
                    "--wrap", zbig, "--fmt", "lz4", raw], check=True,
                   capture_output=True)
    # ...and its HUGE leg reads RUN\ZHUGE.TXT, 90,000 bytes of the same, into
    # an OSAPI_MEM_HUGE run below 1MB: past one selector's 64KB, so the file
    # doors decode it on the segment arm (KERN-MEGA-MEMORY-PLAN MC7.4)
    rawh = os.path.join(out, "zhuge.raw")
    with open(rawh, "wb") as f:
        f.write(bytes(65 + i % 26 for i in range(90000)))
    zhuge = os.path.join(out, "ZHUGE.TXT")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88lz.py"),
                    "--wrap", zhuge, "--fmt", "lz4", rawh], check=True,
                   capture_output=True)
    os88bochs.MEGS = 4
    box = os88bochs.Box(out, [os.path.join(ROOT, p) for p in PKGS],
                        put=["RUN:" + zbig, "RUN:" + zhuge])
    fails = []
    lines = []
    try:
        if box.wait_for("kern_mega:", 240):
            box.up()
            box.megarun()
            end = time.time() + 240
            while time.time() < end:
                lines = box.lines()
                if any(l.startswith("kern_mega FAULT") or
                       l.startswith("MEGAXM: ok") or
                       l.startswith("MEGAXM: no") or
                       l.startswith("MEGAXM: bad") for l in lines):
                    break
                time.sleep(0.2)
            time.sleep(1)
            lines = box.lines()
        else:
            fails.append("no kern_mega line in 240s")
    finally:
        box.stop()
    for l in lines:
        if l.startswith("kern_mega FAULT") or l.startswith(
                "kern_mega CLOSED"):
            fails.append(l)
        if l.startswith("MEGAXM"):
            print("megaxmove: " + l)
    mv = [m for m in (re.match(r"MEGAXM: move (\w+) (\w+)", l)
                      for l in lines) if m]
    if not mv:
        fails.append("MEGAXM never reported the move (is the machine in "
                     "protected mode, and did the filler claim succeed?)")
    else:
        a, b = int(mv[0].group(1), 16), int(mv[0].group(2), 16)
        if a == 0xFFFF or b == 0xFFFF:
            fails.append("MEGAXM's block was not in the walk")
        elif b <= a:
            fails.append("the block did not move up: KB %d before the "
                         "pack, %d after" % (a, b))
    if "MEGAXM: ok" not in lines:
        fails.append("MEGAXM's reads through the token failed: %s" %
                     " | ".join(l for l in lines
                                if l.startswith(("MEGAXM: no",
                                                 "MEGAXM: bad"))))
    print("megaxmove: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if keep or fails:
        print("megaxmove: the boot is in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
