#!/usr/bin/env python3
"""The windowed DOS box's PROMPT on kern_mega with no #GP fixup per character
(docs/plans/KERN-MEGA-PLAN.md 14.54).

    python3 tests/megadosp.py                the row
    python3 tests/megadosp.py --keep         ...and leave the E9 log and shots
    python3 tests/megadosp.py --pkg F.O88    census another kern_mega DOS.O88
                                             (the one before a change) -
                                             reports, does not gate

kern_mega COMPLETES a store through a code selector (mega_gpw) instead of
faulting, and each completion is an exception - ~880 clocks on a 286. The
box's console hook, dos_hk_tty, entered with `push cs / pop ds`, so every
byte the prompt printed - a DIR row, an ECHO, a TYPE - was one completed
#GP at con_markc's first store (and con_write's, for a CR): 20,002 for a
20,000-byte TYPE, and 4.0 s against 5.6 s on 86Box's mr286. megatrap0 counts
a LAUNCH and the DOS prompt's cost is in what it prints, so this row types.

It boots `make megatrap`'s kernel (a private tree) under Bochs - the one
emulator here that enforces a 286's segment rules (docs/BOCHS.md) - with B:
holding kern_mega's DOS.O88 (its tree's megak/kdos/) and an 800-byte text
file, double-clicks DOS.O88, and at the prompt runs:

  ECHO <60 x>     must add NO trap charged to DOS
  DIR             must add NO trap charged to DOS
  TYPE SMALL.TXT  at most TYPE_ALLOW: the TYPE buffer's free leaves ES
                  holding a freed selector twice (api_x.done's `pop es` and
                  the core's dsh_bufput's), each completed once a COMMAND -
                  the `.stale` fixup, 14.52.3; it does not grow with the file

and none of the three may raise a kern_mega FAULT.

THE FAILING CASE (docs/WRITING-TESTS.md 1): put dos_hk_tty's `push cs / pop
ds` back on DOS_MEGA and ECHO takes 62 traps, DIR 87 and TYPE 802 - measured
red when this was written (`--pkg` with the DOS.O88 from before 14.54).
"""
import argparse
import collections
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs  # noqa: E402
import os88build  # noqa: E402

QUIET = 8                       # seconds of no E9 line: settled
LIMIT = 240
TYPE_ALLOW = 2                  # the two .stale fixups a TYPE (see above)
SMALL = 800                     # bytes of SMALL.TXT: ten 78-character lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--pkg", help="the kern_mega DOS.O88 to census "
                    "(default: the tree's megak/kdos/DOS.O88)")
    a = ap.parse_args()
    t = os88build.tree("MEGATRAP=1", targets=(
        "mega.img", "megarun.o88", "mbr.bin"))
    out = os.path.join(t.dir, "megadosp")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    dos = os.path.join(out, "DOS.O88")
    shutil.copy(a.pkg or os.path.join(t.dir, "megak", "kdos", "DOS.O88"), dos)
    txt = os.path.join(out, "SMALL.TXT")
    with open(txt, "wb") as f:
        for i in range(SMALL // 80):
            f.write(("L%03d " % i + "abcdefghijklmnopqrstuvwxyz0123456789"
                     * 3)[:78].encode() + b"\r\n")
    disk = os.path.join(out, "dosp.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", disk, "--size", "1440", dos, txt],
                   check=True, stdout=subprocess.DEVNULL)
    t0 = time.time()
    box = os88bochs.Box(out, [], bimg=disk, btype="1_44", build=t.dir)
    fails, counts = [], collections.OrderedDict()
    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            print("FAIL: kern_mega never reached protected mode")
            return 1
        time.sleep(25)
        box.capture()
        os88bochs.dbl(591, 105)          # B:
        time.sleep(10)

        def step(name, act, allow=0):
            n0 = os.path.getsize(box.e9)
            act()
            end, last, lt = time.time() + LIMIT, n0, time.time()
            while time.time() < end:
                time.sleep(1)
                n = os.path.getsize(box.e9)
                if n != last:
                    last, lt = n, time.time()
                elif time.time() - lt >= QUIET:
                    break
            with open(box.e9, "rb") as f:
                f.seek(n0)
                new = f.read().decode("latin-1").splitlines()
            # every trap from the prompt on is the box's: the one image
            # running is DOS, and api_x.done's `pop es` (CS 0060) is the
            # kernel returning to it - so none is set aside by name
            traps = [l for l in new if l.startswith("T ")]
            fault = [l for l in new if l.startswith("kern_mega FAULT")]
            sites = collections.Counter(" ".join(l.split()[2:3] + l.split()[5:6])
                                        for l in traps)
            os88bochs.shot(os.path.join(out, "%02d-%s.png"
                                        % (len(counts), name.split()[0])))
            counts[name] = len(traps)
            print("megadosp: %-22s %6d traps%s %s" % (
                name, len(traps), " FAULT" if fault else "",
                ", ".join("%s x%d" % kv for kv in sites.most_common(4))))
            if fault:
                fails.append("%s: %s" % (name, fault[0]))
            if len(traps) > allow:
                fails.append("%s took %d #GP fixups (%d allowed), the first "
                             "`%s` (tools/megatraps.py %s)"
                             % (name, len(traps), allow, traps[0], box.e9))

        def cmd(text):
            def go():
                os88bochs.xd("type", "--delay", 120, text)
                time.sleep(1)
                os88bochs.xd("key", "Return")
            return go

        step("launch (double-click)", lambda: os88bochs.dbl(165, 123))
        time.sleep(10)                   # the prompt's first paint
        step("ECHO (60 characters)", cmd("ECHO " + "x" * 60))
        step("DIR", cmd("DIR"))
        step("TYPE SMALL.TXT (%d B)" % SMALL, cmd("TYPE SMALL.TXT"),
             allow=TYPE_ALLOW)
    finally:
        box.stop()
    print("megadosp: %d steps, %d trap lines, %.0fs under Bochs"
          % (len(counts), sum(counts.values()), time.time() - t0))
    if a.pkg:
        return 0
    for f in fails:
        print("FAIL: " + f)
    if not fails and not a.keep:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
