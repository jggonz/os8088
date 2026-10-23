#!/usr/bin/env python3
"""kern_mega's #GP census held at ZERO for a fixed set of OS88_HIGH packages
(docs/plans/KERN-MEGA-PLAN.md 10.3 item 50, 14.39).

    python3 tests/megatrap0.py            the row
    python3 tests/megatrap0.py --keep     ...and leave the boot's E9 log

kern_mega COMPLETES a write through a code selector, a far transfer through
a data segment and a segment-0 access instead of faulting on it (mega_gpw,
mega_gpfix, mega_null0) - each is right and each costs an exception, so a
package that does one in a loop is a SLOW program that looks like a working
one, and nothing on the glass says so. `make megatrap`'s knob (MEGATRAP=1,
-DMEGA_TRAPLOG) writes one E9 line per #GP before it is completed, and
tools/megatraps.py ranks them; the census sessions that took the counts down
used a throwaway logger and scratch scripts, so nothing held them there.

This boots that kernel under Bochs - the one emulator here that checks a
286's segment rules; QEMU lets code write through CS and would count nothing
(docs/BOCHS.md) - with PKGS on C:, starts each through MEGARUN as
tools/os88bochs.py's sweep does, lets them run LINGER seconds, and charges
every trap line to the image the kernel names (or, for an image with no
header, to the package MEGARUN started last). Any count for an image not in
ALLOW fails the row, and so does a package MEGARUN never reached or a kern_mega
FAULT. ALLOW is the census's known residue, each with its reason; it only
shrinks.

THE FAILING CASE (docs/WRITING-TESTS.md 1): put a `push cs / pop ds` and a
store into any package in PKGS - HELLO is the cheapest - and this goes red
naming it; MEGACSL (tests/megacsw) is a package that does exactly that on
purpose.

The kernel is a private tree (os88build.tree), so build/ is never written.
"""
import collections
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs  # noqa: E402
import os88build  # noqa: E402
import megatraps  # noqa: E402

# Started in this order (MEGARUN takes RUN/'s listing, which os88disk.py
# writes in the order given). Each says OS88_HIGH, so on kern_mega it runs
# above 1MB, and each opened its window with no trap at all when this row was
# written (2026-10-20, 14.39).
PKGS = ("hello", "calc", "mines", "notepad", "texpad", "timer", "fontview",
        "solitair", "piano", "bounce",
        "deskcal", "deskclok", "stiknote")
# A second boot after the first ten, because an eleventh window is refused
# (05) under MEGARUN on Bochs's 16MB - TASKMGR, CHART and ARTFUL were, and
# opened no window to count. BATCH is the packages a boot takes.
BATCH = 10
# The three Sticky Apps (DESKCAL, DESKCLOK, STIKNOTE) were left out of the
# first census (14.39): each far-called back into itself through its DATA
# segment from the Sticky App library (os88stik.inc's stik_cb, ~2 a second
# an app), and MEGATRAP's own logger then faulted on that frame, loading
# the free LDT entry after the library's lone code selector as if it were
# a pair's data half (mega_ldt2ds). Both are fixed (KERN-MEGA-PLAN 14.46):
# stik_cb asks OSAPI_MEM_CODESEL, and mega_ldt2ds checks the twin is one

# image name (the header's +16) -> why its traps are tolerated. The row is
# about the packages above; a driver or the kernel's own residue goes here
# with the census that found it.
ALLOW = {
    "(boot)": "mega_strcal's calibration (KERN-MEGA-PLAN 14.30): it MEASURES "
              "how this CPU restarts a faulted string write by taking the "
              "fault on purpose, eight lines at boot",
}

LINGER = 20


def main():
    keep = "--keep" in sys.argv
    t = os88build.tree("MEGATRAP=1", targets=(
        "mega.img", "megarun.o88", "mbr.bin") +
        tuple(p + ".o88" for p in PKGS))
    out = os.path.join(t.dir, "megatrap0")
    shutil.rmtree(out, ignore_errors=True)
    t0 = time.time()
    os88bochs.sweep([t.img(p + ".o88") for p in PKGS], batch=BATCH,
                    out=out, build=t.dir, linger=LINGER)
    logs = sorted(os.path.join(out, d, "e9.out") for d in os.listdir(out)
                  if d.startswith("boot"))
    text = ""
    for p in logs:
        with open(p, "rb") as f:
            text += f.read().decode("latin-1")
    lines = text.splitlines()
    started = [l[9:] for l in lines if l.startswith("MEGARUN: ")
               and l not in ("MEGARUN: ok", "MEGARUN: done")
               and not l.startswith("MEGARUN: no ")]
    sites, meta = megatraps.parse(logs)
    per = collections.Counter()
    for (cs, ip, k, name), n in sites.items():
        who = name if name != "-" else ",".join(sorted(
            meta[(cs, ip, k, name)]))
        per[who] += n

    fails = []
    want = [p.upper() + ".O88" for p in PKGS]
    missing = [p for p in want if p not in started]
    if missing:
        fails.append("MEGARUN never started %s" % " ".join(missing))
    for l in lines:
        if l.startswith("MEGARUN: no "):
            fails.append("a package was refused (%s): it ran nothing to "
                         "count" % l)
    if "MEGARUN: done" not in lines:
        fails.append("MEGARUN never said done")
    for l in lines:
        if l.startswith("kern_mega FAULT"):
            fails.append("a kern_mega FAULT: %s" % l)
    for who, n in sorted(per.items()):
        if who in ALLOW:
            print("megatrap0: %-14s %6d traps (allowed: %s)"
                  % (who, n, ALLOW[who]))
        else:
            fails.append("%s took %d #GP traps (kind and site: python3 "
                         "tools/megatraps.py %s)" % (who, n, " ".join(logs)))
    print("megatrap0: %d packages started, %d trap lines, %.0fs under Bochs"
          % (len(started), sum(per.values()), time.time() - t0))
    if not keep and not fails:
        for p in logs:
            os.remove(p)
    for f in fails:
        print("FAIL: " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
