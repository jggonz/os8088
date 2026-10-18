#!/usr/bin/env python3
"""apps/os88pit.inc's pit_now never reads a tick early (SPEC.md 72.15.1).

    python3 tests/pitnow.py            # must pass: 0 bad reads
    python3 tests/pitnow.py --no-irr   # the IRR check taken out: must FAIL

pit_now is the 838 ns clock five packages and ETHER.DRV's profiler share. Its
tick re-read misses a reload that lands INSIDE its cli: IRQ0 is pending and
not yet taken when the tick is read again, so the pair passes with the old
tick and the new count, one tick early. PIXELSTEIN's Auto judged a 98.2 ms
frame as 43.3 that way, about one run in twelve of tests/pxsauto.py, which is
far too rare to gate on - and the cause is not PixelStein's.

So the clock is hammered on its own: tests/pitnow/pitnow.asm is a BOOT SECTOR
that sets channel 0 to the kernel's mode 2 and calls pit_now 60,000 times,
counting every read that went backwards or a whole tick forward. No kernel,
no package, nothing between the read and the hardware but the BIOS's own
int 08h - and on MartyPC's 5150 the IRR-less clock fails 40 of 60,000 reads
over 211 ticks, deterministically, so a single run is the whole answer.
"""
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M                                      # noqa: E402

READS = 60000
DONE = 0xD0E0


def build(tmp, no_irr):
    inc = os.path.join(tmp, "inc")
    os.makedirs(inc)
    src = open(os.path.join(ROOT, "apps", "os88pit.inc")).read()
    if no_irr:
        cut = [l for l in src.splitlines(True) if l.lstrip().startswith("inc dx")]
        assert len(cut) == 1, "pit_now's owed-tick `inc dx` was not found once"
        src = src.replace(cut[0], "")
    open(os.path.join(inc, "os88pit.inc"), "w").write(src)
    out = os.path.join(tmp, "pitnow.bin")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", inc + "/", "-o", out,
                    os.path.join(ROOT, "tests", "pitnow", "pitnow.asm")],
                   check=True)
    sect = open(out, "rb").read()
    assert len(sect) == 512, "the boot sector is %d bytes" % len(sect)
    img = os.path.join(tmp, "pitnow.img")
    open(img, "wb").write(sect + bytes(368640 - 512))
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--no-irr", action="store_true",
                    help="take the IRR check out: the row must then FAIL")
    a = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        img = build(tmp, a.no_irr)
        with M.launch(img, machine=a.machine, boot=False) as m:
            m.run()
            M.until(m, lambda mm: int.from_bytes(mm.read(0x604, 2), "little")
                    == DONE, "%d pit_now reads" % READS, poll=0.5, guest=60.0)
            r = m.read(0x600, 16)
    w = [int.from_bytes(r[i:i + 2], "little") for i in range(0, 16, 2)]
    print("pitnow: %d reads, %d bad%s" % (w[0], w[1], (
        " - the last %04x:%04x then %04x:%04x" % (w[5], w[4], w[7], w[6]))
        if w[1] else ""))
    ok = w[0] == READS and w[1] == 0
    print("pitnow: %s" % ("ok" if ok else "FAIL - a read reported a tick that "
                          "had not happened, or missed one that had"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
