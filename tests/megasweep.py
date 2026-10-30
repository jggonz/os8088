#!/usr/bin/env python3
"""megasweep - every shipped package OPENS under kern_mega
(docs/plans/completed/KERN-MEGA-MEMORY-PLAN.md MC7, "step 6's follow-ups").

    make && make mega && python3 tests/megasweep.py [--keep] [NAME ...]

Since MC7's step 5 every claim a package holds under kern_mega's protected
mode is a selector, and since step 6 the GDT's identity map stops at the
kernel's own segments - so a package that does arithmetic on a segment, or
loads a paragraph it computed, faults where it used to work by accident. The
sweep that found the last round of them (twelve packages closed for a #GP and
two that faulted the kernel) was a scratch script; this is it kept.

Boots kern_mega under Bochs at 16MB with every package the shipped disks
carry on C: (the kern_mega ARM where `make mega` builds one, and kern_mega's
own DOS.O88), starts each through MEGARUN (tools/os88bochs.py's sweep, the
batches in parallel) and lets each boot run LINGER seconds after its last
start. Fails on:

  - a `kern_mega CLOSED` line (a package's #GP, completed by closing it) or
    a `kern_mega FAULT` (the kernel's own);
  - a package MEGARUN could not start (`MEGARUN: no NN`) or never reached.

It asserts that a package OPENS and survives LINGER seconds of its own loop,
not that it works: a package whose arithmetic is wrong only when it loads a
document, a module or a song is a row about that package.

Broken on purpose (docs/WRITING-TESTS.md 1), both seen when the row was
written: the parts standard's `op_seg` with its `call op_sub` put back to
`add ax, [op_base]` closes SCRIBE, WORD, SKIES, PXSTEIN and SHEET at their
first load; `vga_blit_prow` loading `[rom_vgabw]` again faults the kernel at
SOLITAIR's first blit.
"""
import glob
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

BUILD = os.path.join(ROOT, "build")
BATCH = 9
LINGER = 10
# The shipped disks whose packages are swept: the 1.44MB apps disk and the
# five 360KB category disks, which between them carry every package `all`
# ships (the system disk's SYSTEM/ ones are on kern_mega's own disk too).
DISKS = ("apps.img", "office360.img", "network360.img", "games360.img",
         "emu360.img", "util360.img", "os8088.img")
# Not a package a user opens: the installer wants a blank C: of its own
SKIP = {"install", "rlapply"}


def shipped():
    """Every .O88 on the shipped disks, by name -> the file to sweep: read
    off the images' own directory entries (an 8.3 name, ext O88, a file's
    attribute byte), at every depth, so a folder a disk adds is swept too"""
    names = set()
    ent = re.compile(rb"([A-Z0-9_$~!#%&'-]{1,8}) {0,7}O88[\x00-\x27]")
    for d in DISKS:
        raw = open(os.path.join(BUILD, d), "rb").read()
        for i in range(0, len(raw) - 32, 32):
            e = raw[i:i + 32]
            if e[8:11] == b"O88" and e[0] not in (0, 0xE5) and \
                    not e[11] & 0x18 and ent.match(e[:12]):
                names.add(e[:8].decode().strip().lower())
    found = {}
    for n in sorted(names - SKIP):
        arm = os.path.join(BUILD, "megak", "apps", n + ".o88")
        if n == "dos":
            p = os.path.join(BUILD, "megak", "kdos", "DOS.O88")
        elif os.path.exists(arm):
            p = arm
        else:
            p = os.path.join(BUILD, n + ".o88")
        if os.path.exists(p):
            found[n] = p
    return found


def main():
    keep = "--keep" in sys.argv
    only = [a.lower() for a in sys.argv[1:] if not a.startswith("--")]
    for p in ("build/mega.img", "build/megak/kernel.bin", "build/apps.img"):
        need(p)
    pk = shipped()
    if only:
        pk = {n: p for n, p in pk.items() if n in only}
    out = tempfile.mkdtemp(prefix="megasweep-")
    t0 = time.time()
    print("megasweep: %d packages, %d a boot" % (len(pk), BATCH), flush=True)
    os88bochs.sweep(list(pk.values()), BATCH, out, linger=LINGER,
                    parallel=True)
    fails = []
    started = set()
    for log in sorted(glob.glob(os.path.join(out, "boot*", "e9.out"))):
        last = None
        for line in open(log, errors="replace").read().splitlines():
            if line.startswith("MEGARUN: ") and line not in (
                    "MEGARUN: ok", "MEGARUN: done"):
                if line.startswith("MEGARUN: no "):
                    fails.append("%s refused (%s)" % (last, line[12:]))
                else:
                    last = line[9:]
                    started.add(last.split(".")[0].lower())
            elif line.startswith("kern_mega CLOSED") or \
                    line.startswith("kern_mega FAULT"):
                fails.append("%s: %s" % (last, line))
    for n in pk:
        if n not in started:
            fails.append("%s: MEGARUN never started it" % n)
    print("megasweep: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL megasweep: " + f)
    if keep:
        print("megasweep: the boots are in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    print("megasweep: %s" % ("FAILED" if fails else
                             "PASS (%d packages)" % len(pk)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
