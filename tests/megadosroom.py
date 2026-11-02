#!/usr/bin/env python3
"""kern_mega: a DOS trip with NO ROOM FOR A PARK takes the hibernate instead
(docs/plans/completed/KERN-MEGA-MEMORY-PLAN.md D5, MC4).

    python3 tests/megadosroom.py            the row
    python3 tests/megadosroom.py --keep     ...and leave build/bochs-megadosroom

A DOS program runs in real mode with the whole machine, and kern_mega keeps the
session for it in a PARK above 1MB (kernel/megados.inc). With the pool full
there is nowhere to put one, and the trip used to refuse (`No room above
1MB`). Since D5 it writes HIBERNAT.IMG instead - kern_big's route - gives the
program the machine with no park at all, and at the program's exit kern_dos
reboots (W6: its kd_resume does not read a kern_mega image) and the boot
resumes the image with no question (HBP_DOS) through the streaming resume
(MC4.1 (b)), which needs no room either.

This boots build/mega.img under Bochs at 2MB with B: holding only KBTRIP.COM
(tests/dostrap/kbtrip.asm, as megakbtrip) and C: the fixed disk the image is
written to, covers every free KB of the pool with PINNED mx_tab records
through the debugger (megahiber --full's shape), starts KBTRIP, gives it a key,
and asserts:

  1. no park was had - [mega_dwoke] still 0, the unpark never came home
     (a MEGA_E9 kernel's `kern_mega: parked` line);
  2. `KBTRIP key 256B` - the program ran and read its key;
  3. [hb_resumes] moved - the reboot came back into the session (a MEGA_E9
     kernel's `kern_mega: resumed`);
  4. every filler record stands where it was, and no FAULT or CLOSED (read
     out of the kernel's state, Box.faults).

THE FAILING CASE (docs/WRITING-TESTS.md 1): the tree before D5 refuses the
trip with the toast and KBTRIP never starts - (2) and (3) fail. Verified when
the row was written.

It reads build/ (wants=) and writes only under build/bochs-megadosroom/.
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
OUT = os.path.join(BUILD, "bochs-megadosroom")
MX_BLKSZ, MX_NBLK = 10, 96          # kernel/mega.inc


def pool(box):
    X, K = box.reads([("mx_tab", MX_BLKSZ * MX_NBLK), ("xm_kb", 2)])
    rows = [(k,) + struct.unpack_from("<HH", X, k * MX_BLKSZ)
            for k in range(MX_NBLK)]
    return rows, struct.unpack_from("<H", K, 0)[0]


def fill(box):
    """every free KB of the pool under a PINNED record (FEh, MX_FPIN)"""
    rows, xkb = pool(box)
    used = sorted((o, o + kb) for _, o, kb in rows if kb)
    gaps, at = [], 0
    for a, b in used:
        if a > at:
            gaps.append((at, a - at))
        at = max(at, b)
    if at < xkb:
        gaps.append((at, xkb - at))
    free = [k for k, _, kb in rows if not kb]
    base = box.sym("mx_tab")
    made, cmds = [], []
    for (o, kb), k in zip(gaps, free):
        e = base + k * MX_BLKSZ
        cmds += ["setpmem 0x%x 2 0x%x" % (e, o),
                 "setpmem 0x%x 2 0x08FE" % (e + 4),
                 "setpmem 0x%x 2 0" % (e + 6),
                 "setpmem 0x%x 2 0" % (e + 8),
                 "setpmem 0x%x 2 0x%x" % (e + 2, kb)]
        made.append((k, o, kb))
    box.dbg(cmds, timeout=60)
    return made, xkb


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
    fails, lines, parked = [], [], False
    box = ob.Box(OUT, [], bimg=disk, btype="1_44", megs=2)
    try:
        if not box.wait_pm(300):
            print("FAIL: kern_mega never reached protected mode")
            return 1
        box.up()
        box.capture()
        made, xkb = fill(box)
        print("megadosroom: the pool's %dK, %d free run(s) filled: %s"
              % (xkb, len(made), made))
        if not made:
            fails.append("nothing to fill: the pool had no free KB")
        box.dbl(591, 105, "B:'s Disk window")
        box.goto(165, 123)
        box.dblclick()
        if not box.wait_for("KBTRIP ready", 240):
            fails.append("KBTRIP never started (the trip refused?)")
        else:
            box.guest_sleep(1.5)        # GUEST seconds (Box.guest_sleep)
            ob.xd("key", "k")
            if not box.wait_for("KBTRIP key", 60):
                fails.append("KBTRIP never read its key")
            if not box.wait_resumed(400):
                fails.append("no `kern_mega: resumed` after the program's "
                             "exit")
            else:
                box.ui_done("the resumed desktop", secs=120)
                box.shot(os.path.join(OUT, "resumed.png"))
                rows, _ = pool(box)
                gone = [(k, o, kb) for k, o, kb in made
                        if (rows[k][1], rows[k][2]) != (o, kb)]
                if gone:
                    fails.append("filler records not back as they were: %s"
                                 % gone)
        lines = box.said()
        parked = box.trip_back()    # [mega_dwoke]: an unpark came home
    finally:
        box.stop()
    if parked:
        fails.append("the trip PARKED: the pool was not full")
    keys = [l for l in lines if l.startswith("KBTRIP key")]
    if keys and not keys[-1].startswith("KBTRIP key 256B"):
        fails.append("KBTRIP read the wrong key: %s" % keys[-1])
    for l in lines:
        if l.startswith("kern_mega FAULT") or l.startswith(
                "kern_mega CLOSED"):
            fails.append(l)
    print("megadosroom: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if keep or fails:
        print("megadosroom: the boot is in " + OUT)
    else:
        shutil.rmtree(OUT, ignore_errors=True)
    print("megadosroom: %s" % ("FAILED" if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
