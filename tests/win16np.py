#!/usr/bin/env python3
"""win16np - discardable code named by its selector is reloaded at #NP on
kern_mega (SPEC.md 124.26.12, 50.6.11 SEG_TYPE CX = 2 and 3, KERN-MEGA-PLAN
14.143; WIN16-NEXT O8).

    make && make mega && make win16fixtures && python3 tests/win16np.py [--keep]

Under Bochs, like win16np: kern_mega boots off C: with WIN16, its DOS.O88
and PMNP.EXE (tests/win16/pmnp.c) in C:\\WIN16 and an \\AUTOSTRT link that
opens it. PMNP is protected-mode-only with six discardable code segments named
by their number (a far-pointer table in its data and direct far calls). It
squeezes the global heap (GlobalAlloc until it refuses, so every segment not
in use goes) before each way of touching a segment that is not present - a far
call, a data read, a bare `mov es`, a pointer given to WIN16's lstrcpy, a
LockSegment, a window procedure WIN16 calls - and writes PMNP.TXT beside
itself:

  1. "pmnp: 0 wrong, ...": every touch reloaded the segment, and the bytes
     read after a reload are the bytes read before the first discard;
  2. WIN16's own counters: `w16_cnload` and `w16_cndrop` both moved (segments
     were loaded and discarded: a kept segment would move neither);
  3. the machine took no fault: no "kern_mega CLOSED" or FATAL line.

Broken on purpose: with the kernel's 0Bh stub as it was (a #NP is fatal), or
with `w16_npent` not registered, the first far call into a segment that is not
present closes WIN16 and there is no PMNP.TXT. The fixture not built is a
SKIP.
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
FIX = os.path.join(B, "win16", "PMNP.EXE")
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
    lnk = os.path.join(out, "PMNP.LNK")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88lnk.py"),
                    lnk, "WIN16\\PMNP.EXE", "--vol", "C",
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
    """PMNP.TXT's lines off C:'s first partition, [] while there is none"""
    with open(c, "rb") as f:
        mbr = f.read(512)
    lba = int.from_bytes(mbr[0x1BE + 8:0x1BE + 12], "little")
    r = subprocess.run(["mtype", "-i", "%s@@%d" % (c, lba * 512),
                        "::/WIN16/PMNP.TXT"], env=mt_env(),
                       capture_output=True)
    if r.returncode:
        return []
    return [x for x in r.stdout.decode("latin-1").splitlines() if x]


def counters(box):
    """(w16_cnload, w16_cndrop) out of WIN16's image in the guest: the package's
    base (Box.pkg_base) and the offsets win16.map gives them"""
    base = box.pkg_base("WIN16")
    if base is None:
        return None, None
    mp = open(os.path.join(B, "megak", "apps", "win16.map")).read()
    out = []
    for nm in ("w16_cnload", "w16_cndrop"):
        m = re.search(r"^\s*([0-9A-F]+)\s+[0-9A-F]+\s+%s\s*$" % nm, mp, re.M)
        out.append(int.from_bytes(box.read(base + int(m.group(1), 16), 2),
                                  "little"))
    return out[0], out[1]


def main():
    keep = "--keep" in sys.argv
    if not os.path.exists(FIX):
        print("win16np: SKIP - PMNP.EXE is not built (make win16fixtures)")
        return 77
    for p in ("mega.img", "mbr.bin", "megak/kernel.sys", "megak/boothd.bin",
              "megak/apps/win16.o88", "megak/kdos/DOS.O88"):
        if not os.path.exists(os.path.join(B, p)):
            sys.exit("win16np: build/%s is not built - `make && make mega`" % p)
    out = tempfile.mkdtemp(prefix="w16np-")
    c = hd(out)
    box = os88bochs.Box(out, [], cimg=c, cgeom=os88bochs.GEOM, noa=True,
                        bootc=True)
    try:
        try:
            box.up()
            box.until(lambda: bool(result(c)), "PMNP.TXT", 300)
        except RuntimeError:
            if not box.fault():
                raise
        got = result(c)
        print("   " + "\n   ".join(got), flush=True)
        want(got and got[0].startswith("pmnp: 0 wrong"),
             "1. every touch of a not-present code segment reloaded it, and "
             "the bytes read after a reload are the bytes read first")
        load, drop = counters(box)
        print("   w16_cnload %s, w16_cndrop %s" % (load, drop), flush=True)
        want(load and drop,
             "2. WIN16 loaded and discarded segments while PMNP ran "
             "(w16_cnload and w16_cndrop moved)")
        box.guest_sleep(3)
        fl = box.faults()
        print("   " + ("\n   ".join(fl) or "(no fault line)"), flush=True)
        want(not fl and not box.fault(),
             "3. the machine took no fault and WIN16 was not closed")
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
        print("win16np: %d FAILED" % len(FAILS))
        return 1
    print("win16np: discarded code is reloaded at #NP")
    return 0


if __name__ == "__main__":
    t0 = time.time()
    r = main()
    print("win16np: %.0fs" % (time.time() - t0))
    sys.exit(r)
