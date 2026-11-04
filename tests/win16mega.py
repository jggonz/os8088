#!/usr/bin/env python3
"""win16mega - a WIN16 test fixture on kern_mega (SPEC.md 124.31, 50.6.11),
under Bochs, for the fixtures whose own rows are kern_big's (QEMU/MartyPC, real
mode) and so never run the arm that changed.

    make && make mega && make win16fixtures && python3 tests/win16mega.py FIXTURE [--keep]

FIXTURE is one of BIGBMP, CTLPROC, DGROUP, MEMDC, MENU. kern_mega boots off C:
with WIN16, its DOS.O88 and the fixture in C:\\WIN16 and an \\AUTOSTRT link that
opens it (tests/win16csalias.py's rig). Bochs cannot read a MessageBox off a
stack the way the kern_big rows do, so each fixture also writes <NAME>.TXT
beside itself (tests/win16/restxt.h) with the line the MessageBox shows, and
the row reads that file:

  BIGBMP   "BIGBMP OK": tiled bitmaps past W16_BMMAX (SPEC.md 124.25.17), the
           arm Paintbrush and Centipede need (kern_mega's selectors over a
           tiled block, mega_bclip);
  CTLPROC  "CTLPROC OK": 75 registered classes force the class table to grow
           (SPEC.md 124.25.18), which on kern_mega frees the old block
           through W16VOF + w16_gfreeh - run nowhere until this row;
  DGROUP   "DGROUP OK": GlobalHandle/Size/ReAlloc/GMEM_MODIFY of the
           program's own DGROUP (SPEC.md 124.9.2), whose kern_mega
           GMEM_MODIFY fix had review only;
  MEMDC    "MEMDC OK". KNOWN TO FAIL on kern_mega today: a #GP at 122C:8896
           (~/Repos/w16next-logs/bigbmp-report.md, bigbmp-verify.md section
           4), unattributed. The row exists so the final pass shows whether
           it still does; a red here is that known fault, not a regression;
  MENU     File > Exit (SPEC.md 124, astexit): MENU.EXE writes "menu: up" and,
           when its message loop ends, "menu: ended". The row presses Alt+F,
           X; the program must end, WIN16's instance be gone and the machine
           take no fault (no "kern_mega CLOSED").

For all five: no fault line ("kern_mega FAULT" / "CLOSED") and the fixture not
built is a SKIP.
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
FIXTURES = {"BIGBMP": "BIGBMP OK", "CTLPROC": "CTLPROC OK",
            "DGROUP": "DGROUP OK", "MEMDC": "MEMDC OK", "MENU": None}
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)
    return cond


def mt_env():
    return dict(os.environ, MTOOLS_SKIP_CHECK="1")


def hd(out, name):
    """C: - mega.img's files (its kernel and ASSOC.DAT aside), WIN16 in
    C:\\WIN16 with the fixture, and the \\AUTOSTRT link that opens it"""
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
    lnk = os.path.join(out, "FIX.LNK")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88lnk.py"),
                    lnk, "WIN16\\%s.EXE" % name, "--vol", "C",
                    "--kind", "document"], check=True, capture_output=True)
    fix = os.path.join(B, "win16", name + ".EXE")
    args += ["WIN16:" + os.path.join(B, "megak", "apps", "win16.o88"),
             "WIN16:" + os.path.join(B, "megak", "kdos", "DOS.O88"),
             "WIN16:" + fix, "AUTOSTRT:" + lnk]
    c = os.path.join(out, "c.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", c, "--hdd", "--mbr", os.path.join(B, "mbr.bin"),
                    "--boot", os.path.join(B, "megak", "boothd.bin"),
                    "--kernel", os.path.join(B, "megak", "kernel.sys"),
                    "--geometry", "%d/%d/%d" % os88bochs.GEOM,
                    "--folder", "SYSTEM/APPDATA/WIN16"] + args,
                   check=True, capture_output=True)
    return c


def result(c, name):
    """<NAME>.TXT's lines off C:'s first partition, [] while there is none"""
    with open(c, "rb") as f:
        mbr = f.read(512)
    lba = int.from_bytes(mbr[0x1BE + 8:0x1BE + 12], "little")
    r = subprocess.run(["mtype", "-i", "%s@@%d" % (c, lba * 512),
                        "::/WIN16/%s.TXT" % name], env=mt_env(),
                       capture_output=True)
    if r.returncode:
        return []
    return [x for x in r.stdout.decode("latin-1").splitlines() if x]


def win16_gone(box):
    return not any(n.upper() in ("WIN16", "MENU")
                   for _, n, _, _ in box.instances())


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    keep = "--keep" in sys.argv
    name = args[0].upper() if args else ""
    if name not in FIXTURES:
        sys.exit("usage: win16mega.py {%s} [--keep]" % "|".join(FIXTURES))
    tag = "win16mega " + name
    fix = os.path.join(B, "win16", name + ".EXE")
    if not os.path.exists(fix):
        print("%s: SKIP - %s.EXE is not built (make win16fixtures)" % (tag, name))
        return 77
    for p in ("mega.img", "mbr.bin", "megak/kernel.sys", "megak/boothd.bin",
              "megak/apps/win16.o88", "megak/kdos/DOS.O88"):
        if not os.path.exists(os.path.join(B, p)):
            sys.exit("%s: build/%s is not built - `make && make mega`" % (tag, p))
    out = tempfile.mkdtemp(prefix="w16mg-")
    c = hd(out, name)
    box = os88bochs.Box(out, [], cimg=c, cgeom=os88bochs.GEOM, noa=True,
                        bootc=True)
    try:
        try:
            box.up()
            if name == "MENU":
                box.until(lambda: "menu: up" in result(c, name),
                          "MENU.TXT 'menu: up'", 240)
                box.capture()
                box.guest_sleep(3)
                box.key("alt+f")
                box.guest_sleep(1)
                box.key("x")
                box.until(lambda: "menu: ended" in result(c, name),
                          "MENU.TXT 'menu: ended' (File > Exit)", 120)
            else:
                box.until(lambda: bool(result(c, name)), name + ".TXT", 300)
        except RuntimeError:
            if not box.fault():
                raise
        got = result(c, name)
        print("   " + ("\n   ".join(got) or "(no result file)"), flush=True)
        if name == "MENU":
            want(bool(got) and got[0] == "menu: ended",
                 "1. File > Exit ended the program (its loop returned)")
            try:
                box.until(lambda: win16_gone(box),
                          "WIN16's instance to end", 90)
                gone = True
            except RuntimeError:
                gone = False
            want(gone, "2. the WIN16 instance is gone")
        else:
            want(bool(got) and got[0] == FIXTURES[name],
                 "1. %s says %r (anything else names the checks that failed)"
                 % (name, FIXTURES[name]))
        box.guest_sleep(3)
        fl = box.faults()
        print("   " + ("\n   ".join(fl) or "(no fault line)"), flush=True)
        want(not fl and not box.fault(),
             "%d. the machine took no fault and WIN16 was not closed by one"
             % (3 if name == "MENU" else 2))
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
        print("%s: %d FAILED" % (tag, len(FAILS)))
        return 1
    print("%s: passed" % tag)
    return 0


if __name__ == "__main__":
    t0 = time.time()
    r = main()
    print("win16mega: %.0fs" % (time.time() - t0))
    sys.exit(r)
