#!/usr/bin/env python3
"""HOW DEEP DOES kern_dos's OWN STACK GO? (SPEC.md 96.43.1)

    make KDSTKDIAG=1 kdostest && python3 tools/kdstkwater.py

`KD_LOW_KB` buys `.lowbss`'s buffers AND the stack that grows down toward
them, and every kilobyte of it is a kilobyte off the DOS program - the one
quantity docs/plans/KERN-DOS-PLAN.md is a budget for.  Nothing said how much
of it the stack wanted, so it was 8 KB on nobody's measurement.

THE INSTRUMENT IS FOUR INSTRUCTIONS AND COSTS NOTHING WHEN OFF.  `kd_entry`
zeroes the buffers exactly as it ships - the machine under measurement has to
be the machine - and then, with DI already at `kd_e_lowbss` and SP still at
`KD_STACK`, fills the GAP with 0xAA.  That `rep stosb` stops one byte under
the first thing the CPU will push, so nothing live is written and every byte
of the region is a byte the stack has not reached yet.

Then this drives the handoff (tests/kdhand.py's path: pick the third arm,
confirm, run) and reads LOW_SEG back at the program's READY prompt - by which
point the mount, the .COM load and the program's own INT 21h calls are all
behind it.  The water mark is `KD_STACK` minus the top of the sentinel run.

TWO TRAPS, and both cost a wrong answer on the way in:

  - **`FAT_PARA` is `DSK_FAT_SECS * 32` and kdshim.inc says TWO sectors**, so
    LOW_SEG is 64 paragraphs over the image and not 32.  Computing it with one
    put the scan 512 bytes low, which reads as a stack that was never used.
  - **the sentinel is not the only thing in there**: a buffer nothing wrote is
    still 0xAA, so the answer is the TOP of the highest run and not the first
    non-sentinel byte going up.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))


def equ(path, name):
    m = re.search(r"^%s\s+equ\s+(\d+)" % name, open(path).read(), re.M)
    if not m:
        sys.exit("kdstkwater: %s does not define %s" % (path, name))
    return int(m.group(1))


def lowbss_len():
    """`.lowbss`'s own length, off nasm's map of the real root."""
    d = tempfile.mkdtemp(prefix="kdstk")
    a, mp = os.path.join(d, "m.asm"), os.path.join(d, "m.map")
    open(a, "w").write('[map all %s]\n%%include "kdos.asm"\n' % mp)
    r = subprocess.run(["nasm", "-f", "bin", "-w+error"]
                       + sum((["-I", os.path.join(ROOT, i) + os.sep] for i in
                             ("kernel", "kerndos", "apps", "apps/dos",
                              "drivers/net")), [])
                       + ["-o", os.path.join(d, "m.bin"), a],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("kdstkwater: kdos.asm would not assemble:\n" + r.stderr[:600])
    for ln in open(mp):
        m = re.match(r"^\s*[0-9A-F]+\s+[0-9A-F]+\s+[0-9A-F]+\s+([0-9A-F]{8})"
                     r"\s+nobits\s+\.lowbss\s*$", ln)
        if m:
            return int(m.group(1), 16)
    sys.exit("kdstkwater: no .lowbss in the map")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=None)
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    import os88marty                                          # noqa: E402
    import os88mouse                                          # noqa: E402
    import os88ui                                             # noqa: E402
    import dosmap                                             # noqa: E402
    import kdhand as K                                        # noqa: E402

    LAY = "kerndos/kdlayout.inc"
    stack = equ(LAY, "KD_LOW_KB") * 1024
    fats = equ("kerndos/kdshim.inc", "DSK_FAT_SECS")
    low_seg = 0x60 + equ(LAY, "KD_IMG_KB") * 64 + fats * 32
    buffers = lowbss_len()
    gap = stack - buffers
    print("kdstkwater: LOW_SEG %04X, .lowbss %d, KD_STACK %d, gap %d"
          % (low_seg, buffers, stack, gap))
    if gap <= 0:
        sys.exit("kdstkwater: the buffers already reach the stack top")

    with os88ui.boot(K.SYS, apps=K.COM, machine=a.machine or K.MACH) as ui:
        m = ui.m
        ui.path("B:/DOSHELLO.COM")
        K.wait_text(m, "READY", what="the windowed run")
        m.type_text("x")
        os88marty.settle(m)
        dm, pseg = dosmap.package(), dosmap.instance(m)
        mo, base = os88mouse.Mouse(marty=m), None
        base = pseg << 4
        mo.click(*dosmap.centre(m, pseg, dm, "dos_erect"))
        os88marty.settle(m)
        x1, y1, x2, _ = dosmap.rect(m, pseg, dm, "dos_mrad")
        pitch = K.rec(m, pseg, dm, K.RD_PITCH)
        mo.click((x1 + x2) // 2, y1 + K.WHOLE * pitch + pitch // 2)
        os88marty.settle(m)
        mo.click(*dosmap.centre(m, pseg, dm, "dos_trect"))
        os88marty.settle(m)
        mo.click(*dosmap.centre(m, pseg, dm, "dos_rrect"))
        os88marty.settle(m)
        if K.alert_up(m, base, dm):                 # no fixed disk: it ASKS
            mo.click(*K.alert_button(m, base, dm, 1))
        K.wait_text(m, "READY", secs=150, what="the run under kern_dos")

        low = m.read(low_seg * 16, stack)

    runs, st = [], None
    for i in range(buffers, stack):
        if low[i] == 0xAA and st is None:
            st = i
        elif low[i] != 0xAA and st is not None:
            runs.append((st, i))
            st = None
    if st is not None:
        runs.append((st, stack))
    if not runs:
        sys.exit("kdstkwater: NO sentinel anywhere in the gap - the image on "
                 "the disk was not built KDSTKDIAG=1, or LOW_SEG is wrong")
    top = max(b for _, b in runs)
    water = stack - top
    print("kdstkwater: sentinel survives to +%d of %d, so the stack's water "
          "mark is %d byte(s)" % (top, stack, water))
    print("kdstkwater: %d byte(s) of stack are spare; KD_LOW_KB could be %d"
          % (gap - water, -(-(buffers + water) // 1024)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
