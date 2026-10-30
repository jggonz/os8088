#!/usr/bin/env python3
"""kern_mega: a pool block IN THE HMA survives a DOS trip (docs/plans/
KERN-MEGA-MEMORY-PLAN.md MC4.1 (a)).

    python3 tests/megahma.py            the row
    python3 tests/megahma.py --keep     ...and leave build/bochs-megahma

Since MC4 the HMA - the pool's first 64KB, 1MB to 1MB+64K - is ordinary pool
that every top-down claim reaches last, and a DOS trip is the one thing that
still needs it: kern_dos runs there for the length of the program. So the trip
claims its park and arena above it (mx_alloc's floor, [mega_mxflr]) and, when
a block stands in the HMA, claims the park 64KB larger, copies the HMA into
that tail at the snapshot, sums it with the park and lists one copy back for
the park stub.

This boots build/mega.img under Bochs (16MB) with B: holding KBTRIP.COM
(tests/dostrap/kbtrip.asm, as megakbtrip), and through the debugger writes a
16KB PINNED mx_tab record at the pool's base - the HMA's first 16KB, owner
FEh with MX_FPIN, megashed's shape - and a pattern into it, one dword every
256 bytes. Then it starts KBTRIP, gives it its key, waits for the session to
come back, and asserts:

  1. `kern_mega: unparked` - the trip went and came back;
  2. every dword of the pattern reads back as written;
  3. the record still stands where it was written.

THE FAILING CASE (docs/WRITING-TESTS.md 1): make mega_dosclaimf leave
[mega_dhkb] at 0 and kern_dos's image overwrites the pattern - (2) fails on
every dword under its ~42KB. Verified when the row was written.

It reads build/ (wants=) and writes only under build/bochs-megahma/.
"""
import os
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs as ob  # noqa: E402

BUILD = os.path.join(ROOT, "build")
OUT = os.path.join(BUILD, "bochs-megahma")
MX_BLKSZ, MX_NBLK = 10, 96          # kernel/mega.inc
HMA = 0x100000
KB = 16                             # the block: the HMA's first 16KB
STEP = 256                          # a dword of the pattern every STEP bytes


def pat(i):
    return (0x5A000000 | (i * 0x10325)) & 0xFFFFFFFF


def w16(b, i):
    return b[i] | b[i + 1] << 8


def rows(box):
    X = box.read("mx_tab", MX_BLKSZ * MX_NBLK)
    return [(k, w16(X, k * MX_BLKSZ), w16(X, k * MX_BLKSZ + 2))
            for k in range(MX_NBLK)]


def main():
    keep = "--keep" in sys.argv
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    com = os.path.join(OUT, "KBTRIP.COM")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com,
                    os.path.join(HERE, "dostrap", "kbtrip.asm")], check=True)
    disk = os.path.join(OUT, "kbtrip.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", disk, "--size", "1440", com],
                   check=True, stdout=subprocess.DEVNULL)
    t0 = time.time()
    box = ob.Box(OUT, [], bimg=disk, btype="1_44")
    fails = []
    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            print("FAIL: kern_mega never reached protected mode")
            return 1
        box.up()
        box.capture()
        r = rows(box)
        used = [(k, o, kb) for k, o, kb in r if kb and o < KB]
        if used:
            print("FAIL: the HMA is not free at the desktop: %s" % used)
            return 1
        free = [k for k, o, kb in r if not kb]
        e = box.sym("mx_tab") + free[0] * MX_BLKSZ
        cmds = ["setpmem 0x%x 2 0" % e,                # MX_OFF: the base
                "setpmem 0x%x 2 0x08FE" % (e + 4),     # FEh, MX_FPIN
                "setpmem 0x%x 2 0" % (e + 6),
                "setpmem 0x%x 2 0" % (e + 8)]
        cmds += ["setpmem 0x%x 4 0x%x" % (HMA + i * STEP, pat(i))
                 for i in range(KB * 1024 // STEP)]
        cmds.append("setpmem 0x%x 2 %d" % (e + 2, KB))  # MX_KB: the claim
        box.dbg(cmds, timeout=60)
        box.dbl(591, 105, "B:'s Disk window")
        box.goto(165, 123)
        box.dblclick()
        if not box.wait_for("KBTRIP ready", 120):
            fails.append("KBTRIP never started")
        else:
            time.sleep(1.5)
            ob.xd("key", "k")
            if not box.wait_for("kern_mega: unparked", 120):
                fails.append("the session never came back")
        if not fails:
            box.ui_done("the desktop after the return")
            got = box.read(HMA, KB * 1024)
            bad = [i for i in range(KB * 1024 // STEP)
                   if struct.unpack_from("<I", got, i * STEP)[0] != pat(i)]
            if bad:
                fails.append("%d of %d pattern dwords changed across the "
                             "trip (first at 1MB+%Xh)"
                             % (len(bad), KB * 1024 // STEP, bad[0] * STEP))
            k, o, kb = rows(box)[free[0]]
            if (o, kb) != (0, KB):
                fails.append("the HMA's record moved or went: off %d kb %d"
                             % (o, kb))
        parks = [l for l in box.lines() if "kern_mega: parked" in l]
        print("megahma: %s" % (parks[-1] if parks else "no park line"))
    finally:
        box.stop()
    print("megahma: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if not fails and not keep:
        shutil.rmtree(OUT, ignore_errors=True)
    print("megahma: %s" % ("FAILED" if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
