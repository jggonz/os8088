#!/usr/bin/env python3
"""win16csalias - a program that builds code in a global block and far-calls
it gets a REAL code selector on kern_mega (SPEC.md 124.31.6, 50.6.11
SEG_TYPE, KERN-MEGA-PLAN 14.142; Arcade's ASTEROID).

    make && make mega && make win16fixtures && python3 tests/win16csalias.py [--keep]

Under Bochs, like win16ovr: kern_mega boots off C: with WIN16, its DOS.O88 and
CSALIAS.EXE (tests/win16/csalias.c) in C:\\WIN16 and an \\AUTOSTRT link that
opens it. CSALIAS writes CSALIAS.TXT beside itself and the row reads it:

  1. "csalias: 0 wrong": AllocDStoCSAlias and PrestoChangoSelector each give a
     code selector over the block's bytes (LAR says code), a far call through
     it returns 1234h; AllocDStoCSAlias of a selector in no claim (over the
     ROM) is a new code window that FreeSelector gives back;
     PrestoChangoSelector(s, s) flips a window back to data in place, and
     (blk, blk) the block's own handle descriptor, ASTEROID's idiom; and
     PrestoChangoSelector onto a selector the program does not own (its CS)
     answers 0;
  2. the machine took no fault: no "kern_mega CLOSED" or FATAL line.

Broken on purpose: before 14.142 the far call is a #GP at the data selector
and WIN16 is CLOSED, no CSALIAS.TXT. The fixture not built is a SKIP.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import re
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
os.environ["OS88_DEFINES"] = "KERN_MEGA"
import os88bochs  # noqa: E402

B = os.path.join(ROOT, "build")
FIX = os.path.join(B, "win16", "CSALIAS.EXE")
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
    lnk = os.path.join(out, "CSALIAS.LNK")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88lnk.py"),
                    lnk, "WIN16\\CSALIAS.EXE", "--vol", "C",
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
    """CSALIAS.TXT's lines off C:'s first partition, [] while there is none"""
    with open(c, "rb") as f:
        mbr = f.read(512)
    lba = int.from_bytes(mbr[0x1BE + 8:0x1BE + 12], "little")
    r = subprocess.run(["mtype", "-i", "%s@@%d" % (c, lba * 512),
                        "::/WIN16/CSALIAS.TXT"], env=mt_env(),
                       capture_output=True)
    if r.returncode:
        return []
    return [x for x in r.stdout.decode("latin-1").splitlines() if x]


def main():
    keep = "--keep" in sys.argv
    if not os.path.exists(FIX):
        print("win16csalias: SKIP - CSALIAS.EXE is not built (make win16fixtures)")
        return 77
    for p in ("mega.img", "mbr.bin", "megak/kernel.sys", "megak/boothd.bin",
              "megak/apps/win16.o88", "megak/kdos/DOS.O88"):
        if not os.path.exists(os.path.join(B, p)):
            sys.exit("win16csalias: build/%s is not built - `make && make mega`" % p)
    out = tempfile.mkdtemp(prefix="w16csa-")
    c = hd(out)
    box = os88bochs.Box(out, [], cimg=c, cgeom=os88bochs.GEOM, noa=True,
                        bootc=True)
    try:
        try:
            box.up()
            box.until(lambda: bool(result(c)), "CSALIAS.TXT", 240)
        except RuntimeError:
            if not box.fault():
                raise
        got = result(c)
        print("   " + "\n   ".join(got), flush=True)
        want(got and got[0] == "csalias: 0 wrong",
             "1. a code alias of a block's bytes is called, a window flips "
             "back to data in place, and a selector the program does not own "
             "is refused")
        box.guest_sleep(3)
        fl = box.faults()
        print("   " + ("\n   ".join(fl) or "(no fault line)"), flush=True)
        want(not fl and not box.fault(),
             "2. the machine took no fault and WIN16 was not closed")
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
        print("win16csalias: %d FAILED" % len(FAILS))
        return 1
    print("win16csalias: a code alias is called")
    return 0


if __name__ == "__main__":
    t0 = time.time()
    r = main()
    print("win16csalias: %.0fs" % (time.time() - t0))
    sys.exit(r)
