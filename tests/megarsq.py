#!/usr/bin/env python3
"""kern_mega: what STREAMING a file costs, the Video Player's way
(docs/plans/KERN-MEGA-PLAN.md 14.122) - an INSTRUMENT, the A/B behind the
answer to *"won't 14.121's bounce cost the video player two copies?"*

    python3 tests/megarsq.py [--mb 4] [--build DIR] [--keep] [--floppy]

kern_mega under Bochs at 4MB off A:, C: beside it. tests/megarsq's MEGARSQ
streams SQ.DAT (MB megabytes, every dword its own offset) with
OSAPI_FILE_READ_SEQ in 32 KB calls into one 32 KB page-safe conventional
claim - the player's bounce for a ring above 1MB ([vp_dbnc]) - checks every
byte, and times three passes with the PIT. SQ.DAT is in C:'s RUN folder
(a BIOS volume through the reflected int 13h, the read-ahead store in
front of it), or with --floppy on a 1.44MB B: (1 MB, the floppy's size).

The row asserts the bytes and that all three passes completed; the TIMES are
the instrument's output (Bochs holds the guest to the host clock, so they
compare two kernels on one box, run the same way, and nothing else). --build
DIR boots another tree's mega.img (a tree built at another commit).

It also reads the kernel's read-ahead and FAT-window state afterwards: where
the store and the landing are, so the report says which path ran.

--box86 runs the same bench on 86Box's mr286 (vm/at286-port: MR BIOS, 16
MHz, ISA IDE), booted off a VHD carrying --build's kernel, MEGARSQ.O88 and
SQ.DAT - a disk with a transfer time, and deterministic run to run. 86Box
has no port E9, so the bench leaves its verdict and times in the BIOS data
area's intra-application bytes (40:F0) as well, and that is what is read.

KERN-MEGA-PLAN 14.122 is what it was built to answer: on a C: boot 14.121
copies ~20 KB a MB more (the read-ahead's fill tails, landing -> store),
and the mr286 streams 4 MB in 4.189/4.213/4.206 s against 4.184/4.204/
4.199 s before it - 0.17%.
"""
import argparse
import os
import shutil
import struct
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs  # noqa: E402
from os88fixture import need  # noqa: E402

PKG = "build/megarsq.o88"


QCNT = (("dsk_q_nf", 2), ("dsk_q_hit", 2), ("dsk_q_nb", 2),
        ("dsk_q_sb", 2), ("dsk_q_n1", 2), ("dsk_q_cb", 4),
        ("dsk_q_srv", 2), ("dsk_q_nc", 2))


def counters(box):
    """A kernel built with the read-ahead's counters (an instrument tree,
    never shipped): fills, store hits, run bounces through the landing and
    their sectors, single-sector bounces, bytes copied landing -> store.
    {} on any other kernel."""
    try:
        r = box.reads(list(QCNT))
    except Exception:
        return {}
    return {k[0][6:]: int.from_bytes(v, "little") for k, v in zip(QCNT, r)}


def ica_report(ica, mb):
    """The bench's results out of 40:F0 (the emulator with no port E9):
    -> ([us a pass], verdict)."""
    us = [int.from_bytes(ica[2 + 4 * i:6 + 4 * i], "little")
          for i in range(min(ica[1], 3))]
    for i, u in enumerate(us):
        print("megarsq:   pass %02d: %d KB, %.3f s, %.1f KB/s"
              % (i, mb * 1024, u / 1e6, mb * 1024 / (u / 1e6) if u else 0))
    return us, chr(ica[0]) if ica[0] else "?"


def box86(a):
    """The same bench on 86Box's 286, booted off a VHD carrying the kernel of
    --build's tree (or this one), MEGARSQ.O88 and SQ.DAT in C:'s root."""
    import subprocess
    os.environ["OS88_MEGA"] = "1"
    os.environ["OS88_MEGA_EMU"] = "86box"
    bld = a.build or os.path.join(ROOT, "build")
    os.environ["OS88_MEGA_BUILD"] = bld
    import os88marty  # noqa: E402
    import os88ui  # noqa: E402
    kb = os.path.join(bld, "megak")
    out = tempfile.mkdtemp(prefix="megarsq86-")
    sq = os.path.join(out, "SQ.DAT")
    n = a.mb * 1024 * 1024 // 4
    with open(sq, "wb") as fh:
        fh.write(struct.pack("<%dI" % n, *range(0, n * 4, 4)))
    vhd = os.path.join(out, "rsq.vhd")
    subprocess.run(
        [sys.executable, os.path.join(ROOT, "tools", "os88hdd.py"),
         "--template", os.path.join(
             ROOT, "build/martypc/run/media/hdds/default_xtide.vhd"),
         "--out", vhd, "--kernel", os.path.join(kb, "kernel.sys"),
         "--vbr", os.path.join(kb, "boothd.bin"),
         "--mbr", os.path.join(bld, "mbr.bin"),
         "--file", "CTRL.DRV=" + os.path.join(kb, "ctrl.drv"),
         "--file", "MEGARSQ.O88=" + os.path.join(ROOT, PKG),
         "--file", "SQ.DAT=" + sq], check=True, capture_output=True)
    fails = []
    m = os88marty.launch(None, boot=False, extra=["--mount", "hd:0:" + vhd])
    try:
        m.run()
        ui = os88ui.UI(m)
        ui.up(limit=240)
        os88marty.no_saver(m)
        m.write(0x4F0, bytes(16))
        try:
            ui.path("C:/MEGARSQ.O88")
        except Exception as e:      # (no window: the bench runs in its
            print("megarsq: (open: %s)" % e)    # entry proc and closes)
        os88marty.until(m, lambda mm: mm.read(0x4F0, 1)[0] in (0x52, 0x58),
                        "the bench's verdict in 40:F0", poll=0.5,
                        limit=900.0, guest=600.0)
        ica = m.read(0x4F0, 16)
    finally:
        m.close()
    us, v = ica_report(ica, a.mb)
    if v != "R" or len(us) != 3:
        fails.append("the bench's verdict %r, %d passes" % (v, len(us)))
    for f in fails:
        print("FAIL: " + f)
    if a.keep or fails:
        print("megarsq: the run is in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mb", type=int, default=4)
    ap.add_argument("--build")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--floppy", action="store_true")
    ap.add_argument("--box86", action="store_true",
                    help="86Box's mr286 (vm/at286-port, MR BIOS, 16 MHz, "
                    "ISA IDE) booting C: off a VHD instead of Bochs - a "
                    "disk with a transfer time; --build names the tree")
    ap.add_argument("--bootc", action="store_true",
                    help="boot off C:, so C: is the kernel's own BIOS "
                    "volume (the read-ahead store in front of it) rather "
                    "than a mount")
    a = ap.parse_args()
    if a.box86:
        return box86(a)
    bld = a.build or os.path.join(ROOT, "build")
    for p in (PKG,):
        need(p)
    out = tempfile.mkdtemp(prefix="megarsq-")
    t0 = time.time()
    os88bochs.MEGS = 4
    mb = 1 if a.floppy else a.mb
    sq = os.path.join(out, "SQ.DAT")
    n = mb * 1024 * 1024 // 4
    with open(sq, "wb") as fh:
        fh.write(struct.pack("<%dI" % n, *range(0, n * 4, 4)))
    kw = dict(build=bld, bootc=a.bootc)
    pkgs = [os.path.join(ROOT, PKG)]
    if a.floppy:
        img = os.path.join(out, "b.img")
        import subprocess
        subprocess.check_call([sys.executable, os.path.join(
            ROOT, "tools", "os88disk.py"), "-o", img, "--size", "1440",
            pkgs[0], sq], stdout=subprocess.DEVNULL)
        kw["bimg"] = img
        box = os88bochs.Box(out, [], **kw)
    else:
        box = os88bochs.Box(out, pkgs, put=("RUN:" + sq,), **kw)
    fails, lines, st, c0 = [], [], {}, {}
    try:
        if box.wait_for("kern_mega:", 240):
            box.up()
            if a.floppy:
                box.capture()
                box.dbl(591, 112, "B:'s Disk window")
                box.dbl(150, 107, "MEGARSQ")
            else:
                c0 = counters(box)
                box.megarun()
            end = time.time() + 600
            while time.time() < end:
                lines = box.lines()
                if any(l.startswith(("kern_mega FAULT", "MEGARSQ: done",
                                     "MEGARSQ: bad", "MEGARSQ: io",
                                     "MEGARSQ: no")) for l in lines):
                    break
                time.sleep(0.5)
            lines = box.lines()
            c1 = counters(box)
            if c0 and c1:
                d = {k: c1[k] - c0[k] for k in c1}
                print("megarsq: raw before %s after %s" % (c0, c1))
                print("megarsq: counted across the stream (an instrumented "
                      "kernel): %s" % d)
            r = box.reads([("dsk_rah_seg", 2), ("dsk_rah_bnc", 2),
                           ("dsk_rah_runs", 2), ("dsk_fatseg", 2)])
            for k, v in zip(("rah_seg", "rah_bnc", "rah_runs", "fatseg"), r):
                v = int.from_bytes(v, "little")
                st[k] = "%04X" % v
                if k != "rah_runs" and v & 4:
                    st[k] += "@%X" % box.sel_base(v)
            print("megarsq: kernel state after: %s" % st)
        else:
            fails.append("no kern_mega line in 240s")
    finally:
        box.stop()
    passes = []
    for l in lines:
        if l.startswith("kern_mega FAULT") or l.startswith(
                "kern_mega CLOSED"):
            fails.append(l)
        if l.startswith("MEGARSQ"):
            print("megarsq: " + l)
            p = l.split()
            if p[1] == "pass":
                kb, calls, us = (int(p[4], 16), int(p[6], 16),
                                 int(p[8], 16))
                passes.append((kb, calls, us))
                print("megarsq:   pass %s: %d KB in %d calls, %.3f s, "
                      "%.1f KB/s" % (p[2], kb, calls, us / 1e6,
                                     kb / (us / 1e6) if us else 0))
    if len(passes) != 3 or "MEGARSQ: done" not in lines:
        fails.append("three whole passes and `done`: %s" % [
            l for l in lines if l.startswith("MEGARSQ")])
    elif any(kb != mb * 1024 for kb, _, _ in passes):
        fails.append("a pass short of %d KB: %s" % (mb * 1024, passes))
    print("megarsq: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if a.keep or fails:
        print("megarsq: the boot is in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
