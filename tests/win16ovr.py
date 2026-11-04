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
     bytes, and its last byte is writable (w16_phfix, w16s_gwin); and a
     40 KB moveable block grown to 80 KB reads back whole through
     __AHINCR, past 64 KB as well as before it (w16_pblk's tiles after a
     grow, WIN16-NEXT-PLAN N1);
  2. "ovrtest: armed ...": it is about to write one byte past the block;
  3. the write raises #GP(0) at an LDT code selector inside OVRTEST's code
     segment, OVRTEST.TXT never gets "SURVIVED the overrun", and kern_mega
     CLOSES WIN16 for it (mega_fkill attributes the code selector by its
     base, KERN-MEGA-PLAN 14.140, WIN16-NEXT-PLAN Q1) and does not halt;
  4. the close completes: ui_task's mega_fkdo takes the instance down and
     the incident ends ([mega_fkslot] = 0), still with no fatal fault.

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
FIX = os.path.join(B, "win16", "OVRTEST.EXE")
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)
    return cond


def ovr_code():
    """OVRTEST.EXE's one code segment's length, from its NE: the fault's IP
    must be inside it"""
    import os88ne
    ne = os88ne.NE(open(FIX, "rb").read(), "OVRTEST.EXE")
    return max(sg.length for sg in ne.segs if sg.code)


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
        # The \\AUTOSTRT link runs OVRTEST before the desktop's first paint
        # is settled, so its overrun can land inside box.up(): a fault ends
        # every os88bochs wait, and the file says how far it got either way
        try:
            box.up()
            box.until(lambda: any("armed" in x for x in result(c)),
                      "OVRTEST's 'armed' line", 240)
        except RuntimeError:
            if not box.fault():
                raise
        got = result(c)
        print("   " + "\n   ".join(got), flush=True)
        want(got and got[0] == "ovrtest: 0 wrong",
             "1. the handle descriptor reaches GlobalSize, GlobalReAlloc "
             "keeps the selector with the new length and the old bytes, and "
             "a block grown past 64 KB reads back through __AHINCR")
        want(len(got) > 1 and got[1].startswith("ovrtest: armed"),
             "2. OVRTEST reached its overrun")
        box.guest_sleep(5)
        fl = box.faults()
        print("   " + ("\n   ".join(fl) or "(no fault line)"), flush=True)
        # 3. THE #GP, IN THE PROGRAM, AND WHAT BECOMES OF IT. (The toast and
        # the instance are named OVRTEST, not WIN16: w16_ownname puts the
        # program's stem in WIN16's header, which is what I_NAME and
        # mega_fkmsg_mk read - seen under Bochs 2026-11-04.) The write one
        # byte past the block faults (N1); the faulting CS is a SUB or tile of
        # WIN16's memory, not an image's pair, so mega_fkill attributes it by
        # its BASE (KERN-MEGA-PLAN 14.140, WIN16-NEXT-PLAN Q1) and CLOSES
        # WIN16 as a package's own fault is closed: a "kern_mega CLOSED NAME
        # 0D at cccc:iiii" incident, the machine NOT halted ([mega_infault]
        # = 0, so no FATAL line). cccc must be an LDT code selector and iiii
        # inside OVRTEST's code segment.
        m = [re.match(r"kern_mega CLOSED (\S+) (\w\w) at (\w{4}):(\w{4})", x)
             for x in fl]
        m = [x for x in m if x]
        code = ovr_code()
        ok = bool(m) and m[0].group(1) == "OVRTEST" and m[0].group(2) == "0D" \
            and int(m[0].group(3), 16) & 4 != 0 \
            and int(m[0].group(4), 16) < code
        want(ok, "3. the overrun raised #GP(0) at an LDT code selector, inside "
             "OVRTEST's %d-byte code segment, and WIN16 was CLOSED for it "
             "(14.140)" % code)
        want(not any(x.startswith("kern_mega FAULT") for x in fl)
             and not box.fault(),
             "3. ...and the machine was NOT halted: no fatal fault, "
             "[mega_infault] = 0")
        want(not any("SURVIVED" in x for x in result(c)),
             "3. ...and the write one byte past the block never completed")
        # 4. THE CLOSE COMPLETES. mega_fkill only unwinds to WIN16's catch
        # and queues the close; ui_task's mega_fkdo tears the instance down
        # and ends the incident ([mega_fkslot] back to 0 once the record is
        # gone). A machine that names WIN16 and then wedges in the teardown
        # passes step 3 and fails here.
        def closed():
            return box.read("mega_fkslot", 1) == b"\0" and not any(
                n.upper() in ("WIN16", "OVRTEST")
                for _, n, _, _ in box.instances())
        try:
            box.until(closed, "WIN16's close to complete (mega_fkdo)", 60)
            done = True
        except RuntimeError:
            done = False
        want(done and not box.fault(),
             "4. the close completed: no WIN16 (OVRTEST) instance, the "
             "incident over ([mega_fkslot] = 0), and still no fatal fault")
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
