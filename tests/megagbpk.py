#!/usr/bin/env python3
"""RealDMG's ARM on a PACKED 1MB cartridge, in place above 1MB (REALDMG-SPEC
10.3.1, MEGA-HUGE-PLAN F4, docs/plans/KERN-MEGA-PLAN.md 14.171).

    python3 tests/megagbpk.py [--keep] [--pkg REALDMG.O88] [--secs S]

A packed cartridge never took the in-place arm: its bank reads are
OSAPI_FILE_READ_SEQ, which is RAW (SPEC.md 20.14.3), so rw_czprobe sent it
to ONE claim - and past 256KB (RW_ROMMAX_KB) that is refused, "packed and
past one claim" (KERN-MEGA-PLAN 14.147). With the packed read past 64KB
(F4) the arm reads the whole file into its 16KB-window claim with ONE
OSAPI_FILE_READ, which expands it in place: the claim is the cartridge byte
for byte, bank n at window n - 1's 4000h (rw_xread).

This boots kern_mega under Bochs (16MB) with B: holding the arm's
REALDMG.O88 and tools/gbours.py's GBBANK.GB - 1MB, MBC5, 64 banks, an LFSR
switching among all of them and checking each switch by a read at 4000h
and by code run at 4010h (tools/gbprof/gbbank.asm) - stored as an LZ4 'CZ'
file, double-clicks it, and asserts:

  1. the game RUNS: its WRAM says A5h at C00Fh and its switch count at C000h
     moves past 500;
  2. NOT ONE switch read the wrong bank (C002h = 0);
  3. the banks are IN PLACE: [rw_xsel] is a claim's first window, bank 1's
     word in the bank table is it, based at or above 1MB, and bank 2's 16KB
     on;
  4. no kern_mega FAULT.

THE FAILING CASE (docs/WRITING-TESTS.md 1): mega-release d433871c's arm
(`--pkg` its build/megak/apps/realdmg.o88) refuses the cartridge - packed
and past one claim - and the game never runs (no A5h). Verified when the row
was written.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
import os88bochs  # noqa: E402
import os88lz  # noqa: E402
from rdgmega import label_off, lin  # noqa: E402

S_BTAB1 = 0x6800 + 2             # RDG_BTAB's bank 1 (rdgmega's)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--pkg", default=os.path.join(
        ROOT, "build", "megak", "apps", "realdmg.o88"))
    ap.add_argument("--lst", default=os.path.join(
        ROOT, "build", "megak", "apps", "realdmg.lst"))
    ap.add_argument("--secs", type=float, default=90)
    a = ap.parse_args()
    rom = subprocess.check_output(
        [sys.executable, os.path.join(ROOT, "tools", "gbours.py"), "gbbank"],
        text=True, stderr=subprocess.DEVNULL).strip().splitlines()[-1]
    out = tempfile.mkdtemp(prefix="megagbpk-")
    data = open(rom, "rb").read()
    z, packed = os88lz.cz_wrap(data)
    if not packed:
        print("FAIL megagbpk: GBBANK.GB did not pack")
        return 1
    gb = os.path.join(out, "GBBANK.GB")
    with open(gb, "wb") as f:
        f.write(z)
    print("megagbpk: GBBANK.GB %d bytes, packed to %d" % (len(data), len(z)))
    shutil.copy(a.pkg, os.path.join(out, "REALDMG.O88"))
    viewoff = label_off(a.lst, "rdg_viewseg")
    xseloff = label_off(a.lst, "rw_xsel")
    disk = os.path.join(out, "gbpk.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", disk, "--size", "1440", "--folder", "SYSTEM/APPDATA",
                    "REALDMG:" + os.path.join(out, "REALDMG.O88"),
                    "REALDMG:" + gb], check=True, stdout=subprocess.DEVNULL)
    box = os88bochs.Box(out, [], bimg=disk, btype="1_44")
    fails, w = [], (0, 0, 0, 0)
    xsel = bank1 = b1lin = b2lin = 0
    try:
        if not box.wait_pm(300):
            fails.append("kern_mega never reached protected mode")
            raise StopIteration
        box.up()
        box.capture()
        box.dbl(591, 105, "B:'s Disk window")
        box.dbl(150, 123, "the REALDMG folder")
        box.dbl(160, 139, "the game to start", lock=False)   # GBBANK.GB
        end = time.time() + a.secs
        view = base = None
        while time.time() < end:
            box.guest_sleep(2, poll=0.5)
            if any(l.startswith("kern_mega FAULT") for l in box.lines()):
                break
            if view is None:
                base = box.pkg_base("RealDMG")
                if base is None:
                    continue
                view = int.from_bytes(box.read(base + viewoff, 2), "little")
                if not view:
                    view = None
                    continue
            vb = lin(box, view)
            m = box.read(vb + 0xC000, 16)
            w = (m[0] | m[1] << 8, m[2], m[3], m[15])
            if w[3] == 0xA5 and w[0] > 500:
                break
        if view is not None:
            vb = lin(box, view)
            bank1 = int.from_bytes(box.read(vb + S_BTAB1, 2), "little")
            bank2 = int.from_bytes(box.read(vb + S_BTAB1 + 2, 2), "little")
            xsel = int.from_bytes(box.read(base + xseloff, 2), "little")
            b1lin = lin(box, bank1) if bank1 else 0
            b2lin = lin(box, bank2) if bank2 else 0
        box.shot(os.path.join(out, "end.png"))
    except StopIteration:
        pass
    finally:
        box.stop()
    for l in box.lines():
        if l.startswith("kern_mega FAULT") or l.startswith("kern_mega CLOSED"):
            fails.append(l)
    n, wrong, first, mark = w
    print("megagbpk: switches %d, wrong %d (the first %d), C00F %02X; "
          "[rw_xsel] %04X, bank 1 %04X at %X, bank 2 at %X"
          % (n, wrong, first, mark, xsel, bank1, b1lin, b2lin))
    if mark != 0xA5 or n <= 500:
        fails.append("the packed cartridge never ran (C00F %02X, %d "
                     "switches) - refused, 'packed and past one claim'"
                     % (mark, n))
    if wrong:
        fails.append("%d switches read the wrong bank (the first, bank %d)"
                     % (wrong, first))
    if not xsel or bank1 != xsel or b1lin < 0x100000 or \
            b2lin - b1lin != 0x4000:
        fails.append("the banks are not IN PLACE above 1MB: [rw_xsel] %04X, "
                     "bank 1 %04X at %X, bank 2 at %X" % (xsel, bank1, b1lin,
                                                          b2lin))
    for f in fails:
        print("FAIL megagbpk: " + f)
    if a.keep or fails:
        print("megagbpk: the boot is in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    print("megagbpk: %s" % ("FAIL" if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
