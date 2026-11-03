#!/usr/bin/env python3
"""win16ovr - a global block's selector is its HANDLE DESCRIPTOR on kern_mega:
it follows the block and an overrun FAULTS (SPEC.md 124.31.3, 50.6.11
SEG_HND; docs/plans/WIN16-ANY-PLAN.md S24).

    make && make mega && make win16fixtures && python3 tests/win16ovr.py [--keep]

Under Bochs, because QEMU's TCG does not check a data segment's limit - the
same write runs to completion there, which is how the row's fixture was first
seen to "survive" (docs/BOCHS.md: QEMU runs what a real 286 faults on).
kern_mega boots off C: with WIN16, its DOS.O88 and OVRTEST.EXE in C:\\WIN16
and an \\AUTOSTRT link that opens the fixture. OVRTEST
(tests/win16/ovrtest.c, `make win16fixtures`) writes OVRTEST.TXT beside
itself, a line at a time, and the row reads it off C: (Bochs writes the image
in place):

  1. "ovrtest: 0 wrong": a 100-byte moveable block's selector reaches
     exactly GlobalSize; a GlobalReAlloc to 6000 bytes with a fixed block
     after it answers the SAME selector, with the new limit and the old
     bytes, and its last byte is writable (w16_phfix, w16s_gwin);
  2. "ovrtest: armed ...": it is about to write one byte past the block;
  3. kern_mega CLOSES WIN16 for the fault (`faults()`'s CLOSED line, vector
     0D) - and OVRTEST.TXT never gets "SURVIVED the overrun".

Broken on purpose (docs/WRITING-TESTS.md 1): the parent commit, whose
w16_pblk answered the lead's D1(a) stopgap - a SUB reaching to the claim's
END - must fail step 3, the write landing in the next block and the file
saying SURVIVED. Under QEMU, which checks no limit, the shipped arm says
SURVIVED too (seen 2026-11-03): that is why this row is Bochs's. The fixture
not built is a SKIP.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
os.environ["OS88_DEFINES"] = "KERN_MEGA"
import os88bochs  # noqa: E402

B = os.path.join(ROOT, "build")
FIX = os.path.join(B, "win16", "OVRTEST.EXE")
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)
    return cond


def mt_env():
    return dict(os.environ, MTOOLS_SKIP_CHECK="1")


def hd(out):
    """C: - mega.img's files (its kernel and ASSOC.DAT aside: os88disk.py
    writes both), WIN16 in C:\\WIN16, and the \\AUTOSTRT link"""
    tree = os.path.join(out, "tree")
    os.makedirs(tree)
    subprocess.run(["mcopy", "-s", "-n", "-i", os.path.join(B, "mega.img"),
                    "::*", tree], env=mt_env(), check=True,
                   capture_output=True)
    args = []
    for dp, _, fns in os.walk(tree):
        rel = os.path.relpath(dp, tree)
        for f in sorted(fns):
            if rel == "." and f.upper() in ("KERNEL.SYS", "ASSOC.DAT"):
                continue
            p = os.path.join(dp, f)
            args.append(p if rel == "." else
                        rel.replace(os.sep, "/").upper() + ":" + p)
    lnk = os.path.join(out, "OVRTEST.LNK")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88lnk.py"),
                    lnk, "WIN16\\OVRTEST.EXE", "--vol", "C",
                    "--kind", "document"], check=True, capture_output=True)
    args += ["WIN16:" + os.path.join(B, "megak", "apps", "win16.o88"),
             "WIN16:" + os.path.join(B, "megak", "kdos", "DOS.O88"),
             "WIN16:" + FIX, "AUTOSTRT:" + lnk]
    c = os.path.join(out, "c.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", c, "--hdd", "--mbr", os.path.join(B, "mbr.bin"),
                    "--boot", os.path.join(B, "megak", "boothd.bin"),
                    "--kernel", os.path.join(B, "megak", "kernel.sys"),
                    "--geometry", "%d/%d/%d" % os88bochs.GEOM,
                    "--folder", "SYSTEM/APPDATA/WIN16"] + args,
                   check=True, capture_output=True)
    return c


def result(c):
    """OVRTEST.TXT's lines off C:'s first partition, [] while there is none"""
    with open(c, "rb") as f:
        mbr = f.read(512)
    lba = int.from_bytes(mbr[0x1BE + 8:0x1BE + 12], "little")
    r = subprocess.run(["mtype", "-i", "%s@@%d" % (c, lba * 512),
                        "::/WIN16/OVRTEST.TXT"], env=mt_env(),
                       capture_output=True)
    if r.returncode:
        return []
    return [x for x in r.stdout.decode("latin-1").splitlines() if x]


def main():
    keep = "--keep" in sys.argv
    if not os.path.exists(FIX):
        print("win16ovr: SKIP - OVRTEST.EXE is not built (make win16fixtures)")
        return 77
    for p in ("mega.img", "mbr.bin", "megak/kernel.sys", "megak/boothd.bin",
              "megak/apps/win16.o88", "megak/kdos/DOS.O88"):
        if not os.path.exists(os.path.join(B, p)):
            sys.exit("win16ovr: build/%s is not built - `make && make mega`" % p)
    out = tempfile.mkdtemp(prefix="w16ovr-")
    c = hd(out)
    box = os88bochs.Box(out, [], cimg=c, cgeom=os88bochs.GEOM, noa=True,
                        bootc=True)
    try:
        box.up()
        box.until(lambda: any("armed" in x for x in result(c)),
                  "OVRTEST's 'armed' line", 240)
        got = result(c)
        print("   " + "\n   ".join(got), flush=True)
        want(got and got[0] == "ovrtest: 0 wrong",
             "1. the handle descriptor reaches GlobalSize, and GlobalReAlloc "
             "keeps the selector with the new length and the old bytes")
        want(len(got) > 1 and got[1].startswith("ovrtest: armed"),
             "2. OVRTEST reached its overrun")
        try:
            box.until(lambda: any("CLOSED" in x for x in box.faults()),
                      "kern_mega closing WIN16 for the fault", 60)
        except RuntimeError:
            pass
        box.guest_sleep(5)
        fl = box.faults()
        print("   " + ("\n   ".join(fl) or "(no fault line)"), flush=True)
        want(any("CLOSED" in x and " 0D at " in x for x in fl),
             "3. kern_mega closed WIN16 for a #GP (vector 0D)")
        want(not any("SURVIVED" in x for x in result(c)),
             "3. ...and the write one byte past the block never completed")
        box.shot(os.path.join(out, "end.png"))
    except RuntimeError as e:
        want(False, "the machine stopped answering or a step missed: %s" % e)
    finally:
        box.stop()
        if not keep:
            shutil.rmtree(out, ignore_errors=True)
        else:
            print("kept: %s" % out)
    if FAILS:
        print("win16ovr: %d FAILED" % len(FAILS))
        return 1
    print("win16ovr: the handle descriptor follows its block, and an overrun "
          "faults")
    return 0


if __name__ == "__main__":
    t0 = time.time()
    r = main()
    print("win16ovr: %.0fs" % (time.time() - t0))
    sys.exit(r)
