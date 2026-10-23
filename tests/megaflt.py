#!/usr/bin/env python3
"""kern_mega: a fault in a PACKAGE closes that package, and the machine
carries on (docs/plans/KERN-MEGA-PLAN.md 14.42).

    python3 tests/megaflt.py            the row
    python3 tests/megaflt.py --keep     ...and leave the boot's directory

Boots kern_mega under Bochs - the emulator here that checks a 286's segment
rules, so a read past a segment's limit is a #GP as it is on the iron
(docs/BOCHS.md) - with tests/megaflt's seven builds on C:, started one every
five seconds by MEGARUN as tools/os88bochs.py's sweep does:

    FLTA  #UD in a timer callback (UI task, the caller's gfx lock held)
    FLTB  a WORKER holding the gfx lock reads past its segment (OS88_HIGH)
    FLTC  #DE inside an OSAPI_FSX_RUN bracket with a foreign mode set
    FLTD  #GP with a 32KB claim held, a timer armed and a TONE on (OS88_HIGH,
          OS88_HIGHMEM)
    FLTE  #GP in the ENTRY PROC: the launch is refused
    FLTF  #UD in a wake handler that took the gfx lock itself
    FLTZ  the control, which never faults

Each faulting build must be named by a `kern_mega CLOSED` line with its
vector; no `kern_mega FAULT` (today's fatal screen) may appear; every build's
entry must see the SAME free memory as the one of its kind before it (each
closed one gave back its region, its claims and its pool block - the high
builds read a KB more conventional and a KB less pool, so they are compared
with each other); FLTE must be refused; and FLTZ, started last, must say it
is alive and then answer a KEY and a CLICK sent from the host, which is the
machine's UI task, keyboard and pointer all working after six faults.

THE FAILING CASE (docs/WRITING-TESTS.md 1): put a `ret` at the top of
mega_fkill (kernel/mega.inc) and every fault is fatal again - the row goes
red on FLTA's `kern_mega FAULT` and nothing after it starts.
"""
import os
import re
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs  # noqa: E402
from os88fixture import need  # noqa: E402

CASES = "abcdefz"
PKGS = ["build/flt%s.o88" % c for c in CASES]
# the vector each case must be closed for
WANT = {"A": "06", "B": "0D", "C": "00", "D": "0D", "E": "0D", "F": "06"}
HIGH = ("B", "D")


def main():
    keep = "--keep" in sys.argv
    for p in PKGS + ["build/megarun.o88", "build/mega.img",
                     "build/mbr.bin"]:
        need(p)
    out = tempfile.mkdtemp(prefix="megaflt-")
    t0 = time.time()
    box = os88bochs.Box(out, [os.path.join(ROOT, p) for p in PKGS])
    fails = []
    lines = []
    try:
        if not box.wait_for("kern_mega:", 240):
            fails.append("no kern_mega line in 240s")
            raise StopIteration
        time.sleep(15)                  # the desktop's first frame
        box.capture()
        os88bochs.dbl(591, 160)         # C:
        time.sleep(10)
        os88bochs.dbl(150, 123)         # RUN
        time.sleep(8)
        os88bochs.dbl(160, 139)         # 0RUN.O88
        end = time.time() + 300
        while time.time() < end:
            lines = box.lines()
            if any(l.startswith("kern_mega FAULT") for l in lines):
                break
            if "MEGARUN: done" in lines and "FLTZ: alive" in lines:
                break
            time.sleep(1)
        os88bochs.shot(os.path.join(out, "after.png"))
        if "FLTZ: alive" in lines:
            os88bochs.xd("key", "a")    # FLTZ is the front window
            time.sleep(3)
            os88bochs.goto(140, 165)    # ...and its content
            os88bochs.xd("click", 1)
            time.sleep(3)
            os88bochs.shot(os.path.join(out, "input.png"))
        lines = box.lines()
    except StopIteration:
        pass
    finally:
        box.stop()

    for l in lines:
        if l.startswith("kern_mega FAULT"):
            i = lines.index(l)
            fails.append("FATAL: %s | %s" % (l, " ".join(lines[i + 1:i + 3])))
    closed = {}
    for l in lines:
        m = re.match(r"kern_mega CLOSED FLT(\w) (\w\w) at (\S+) err (\w+)", l)
        if m:
            closed.setdefault(m.group(1), m.group(2))
            print("megaflt: closed FLT%s, vector %s at %s err %s"
                  % m.groups())
    for c, v in WANT.items():
        if c not in closed:
            fails.append("FLT%s was not closed" % c)
        elif closed[c] != v:
            fails.append("FLT%s closed for vector %s, not %s"
                         % (c, closed[c], v))
    if "Z" in closed:
        fails.append("the control FLTZ was closed")
    avail = {}
    for l in lines:
        m = re.match(r"FLT(\w): avail (\w+) (\w+) caps (\w+)", l)
        if m:
            avail[m.group(1)] = m.groups()[1:]
    for c in CASES.upper():
        if c not in avail:
            fails.append("FLT%s never reached its entry proc" % c)
    for kind in (tuple(c for c in CASES.upper() if c not in HIGH), HIGH):
        seen = [(c, avail[c]) for c in kind if c in avail]
        for c, a in seen[1:]:
            if a != seen[0][1]:
                fails.append("FLT%s's entry saw avail/total/pool %s where "
                             "FLT%s's saw %s: what a closed one held did "
                             "not all come back"
                             % (c, "/".join(a), seen[0][0],
                                "/".join(seen[0][1])))
    if not any(l.startswith("MEGARUN: no ") for l in lines):
        fails.append("FLTE's launch was not refused")
    for what in ("FLTZ: alive", "FLTZ: key", "FLTZ: click"):
        if not any(l.startswith(what) for l in lines):
            fails.append("no `%s` line: the machine after the faults did "
                         "not run, or did not take the host's input" % what)
    print("megaflt: %d closed, %.0fs under Bochs" % (len(closed),
                                                   time.time() - t0))
    if keep or fails:
        print("megaflt: the boot is in %s" % out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    for f in fails:
        print("FAIL: " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
