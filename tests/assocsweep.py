#!/usr/bin/env python3
"""What a document's double-click costs BEFORE its program loads (SPEC.md
54.4.2.1).

    make && python3 tests/assocsweep.py [-v] [hd|floppy ...]

Reported from the field on an installed machine: *"no disk in any floppy
drive, go to E:, double-click a .MOD - 11 to 14 seconds before Tracker begins
to load"*.  The program was in C:\\APPS all along; the time went on
`assoc_locate` (SPEC.md 54.4.2) sweeping the empty floppy drives in front of
it, each mounted TWICE, and on re-reading a floppy's boot sector for every
folder it moved between on a disk whose motor had not stopped.

The instrument is a set of breakpoints armed across the double-click:
`assoc_run_x` (the open, handed over once the lock drops), `ld_run_name_x`
(the program found, its load starting), and `dsk_chdir_x`, which a quiet move
reaches ONLY to mount - so every hit on it is one mount attempt, with DL
naming the volume.  Two machines, one leg each:

  hd      a hard-disk boot (os88hdd.py's fixture: KERNEL.SYS and TRACKER.O88
          in C:\\), A: EMPTY, the document in B:\\MEDIA on the media disk.
          ASSERTS that the empty A: is never mounted at all - the boot volume
          is tried before the sweep - and that the locate is two mounts: B:
          (the document's folder) and C:.  Before 54.4.2.1 it was FIVE, A:
          twice, 2,390 ms against 155.
  floppy  a floppy boot, the document in A:\\ (a stub - only its NAME asks
          for TRACKER), the program in B:\\APPS on the apps disk.  ASSERTS no
          volume is mounted twice: a folder move on a volume the locate has
          just mounted is a word (fcp_goto), not a boot-sector re-read.  It was
          four mounts, A: and B: twice each; it is two.

  four    the field's own three-disk layout on the four-drive 5150: A:
          system, B: apps, D: media, the document in D:\\MEDIA. ASSERTS A: is
          never mounted - B: is searched before A: (SPEC.md 54.4.2.2), and
          each floppy visited is a second of spin-up on an AT. It was D D A A
          B B, A: twice for a program that is not there.

-v prints every int 13h in the span with its CHS, run and buffer.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88marty as M                                       # noqa: E402
import os88ui                                               # noqa: E402

B = os.path.join(ROOT, "build")
TEMPLATE = os.path.join(B, "martypc/run/media/hdds/default_xtide.vhd")
VERBOSE = "-v" in sys.argv
fails = []


def say(s):
    print("  " + s, flush=True)


def trace(m, ui):
    """Double-click BEVERLY.MOD in the front Disk window; answer the mounts
    [(ms, volume letter, dir cluster)] and the span in ms."""
    extra = [{"type": "int", "addr": 0x13}] if VERBOSE else []
    with M.bp_trace(m, "assoc_run_x", "dsk_chdir_x", "ld_run_name_x",
                    *extra, regs=True) as tr:
        ui.open("BEVERLY.MOD", expect=None)
        tr.until(lambda: tr.count("ld_run_name_x") >= 1,
                 "the program to be found", limit=600.0)
    base = tr.first("assoc_run_x")["cycles"]
    mounts = []
    for h in tr.hits:
        if h["cycles"] < base:
            continue
        t = 1000.0 * (h["cycles"] - base) / M.GUEST_HZ
        r = h["regs"]
        if h["name"] == "dsk_chdir_x":
            mounts.append((t, chr(65 + (r["dx"] & 0xFF)), r["ax"]))
            say("+%6.0f ms  mount %s:  dir %04X" % mounts[-1])
        elif VERBOSE and h["name"].startswith("?"):
            say("+%6.0f ms      int 13h ah=%02X al=%02X unit %02X c%d h%d "
                "s%d  buf %05X" % (t, r["ax"] >> 8, r["ax"] & 0xFF,
                                    r["dx"] & 0xFF, r["cx"] >> 8,
                                    r["dx"] >> 8, r["cx"] & 0x3F,
                                    (r["es"] << 4) + r["bx"]))
    ms = tr.ms("assoc_run_x", "ld_run_name_x")
    say("assoc_run -> ld_run_name: %.0f ms, %d mount(s)" % (ms, len(mounts)))
    return mounts, ms


def check(what, ok, detail):
    say("%s  %s: %s" % ("ok  " if ok else "FAIL", what, detail))
    if not ok:
        fails.append(what)


def leg_hd():
    print("hd: hard-disk boot, A: empty, B:\\MEDIA\\BEVERLY.MOD, C:\\TRACKER.O88")
    vhd = os.path.join(B, "assocsweep-%d.vhd" % os.getpid())
    subprocess.check_call(
        ["python3", "tools/os88hdd.py", "--template", TEMPLATE, "--out", vhd,
         "--kernel", os.path.join(B, "kernel.sys"),
         "--vbr", os.path.join(B, "boothd.bin"),
         "--mbr", os.path.join(B, "mbr.bin"),
         "--file", "TRACKER.O88=" + os.path.join(B, "tracker.o88")],
        cwd=ROOT, stdout=subprocess.DEVNULL)
    m = M.launch(None, apps=os.path.join(B, "media360.img"),
                 machine=M.machine("os8088_5150_cga_hdd"),
                 extra=["--mount", "hd:0:" + vhd])
    try:
        ui = os88ui.UI(m, verbose=False)
        ui.ready(limit=240)
        ui.path("B:/MEDIA")
        mounts, ms = trace(m, ui)
    finally:
        m.quit()
        os.remove(vhd)
    vols = [v for _, v, _ in mounts]
    check("the empty A: is never mounted", "A" not in vols,
          "mounted %s" % (" ".join(vols) or "nothing"))
    check("C: is reached", vols[-1:] == ["C"], "last mount %s"
          % (vols[-1] if vols else "none"))
    check("two mounts: the document's folder, then C:", len(vols) <= 2,
          "%d" % len(vols))


def leg_floppy():
    print("floppy: A:\\BEVERLY.MOD (a stub), B:\\APPS\\TRACKER.O88")
    img = os.path.join(B, "assocsweep-%d.img" % os.getpid())
    stub = img + ".mod"
    shutil.copyfile(os.path.join(B, "os8088-360.img"), img)
    # The system disk has a dozen clusters free, and nothing on this path
    # reads the document - its NAME is what asks for TRACKER - so a stub.
    with open(stub, "wb") as f:
        f.write(b"\0" * 1084)
    subprocess.check_call(["python3", "tools/os88fat.py", "add", img, stub,
                           "BEVERLY.MOD"], cwd=ROOT,
                          stdout=subprocess.DEVNULL)
    os.remove(stub)
    m = M.launch(img, apps=os.path.join(B, "apps360.img"),
                 machine=M.machine("os8088_5150_cga"))
    try:
        ui = os88ui.UI(m, verbose=False)
        ui.ready(limit=240)
        ui.open_drive("A")
        mounts, ms = trace(m, ui)
    finally:
        m.quit()
        os.remove(img)
    vols = [v for _, v, _ in mounts]
    check("no volume is mounted twice", len(vols) == len(set(vols)),
          " ".join(vols))
    check("B: is reached", "B" in vols, " ".join(vols))


def leg_four():
    print("four: A: system, B: apps, D:\\MEDIA\\BEVERLY.MOD")
    media = os.path.join(B, "assocsweep-%d-d.img" % os.getpid())
    shutil.copyfile(os.path.join(B, "media360.img"), media)
    m = M.launch(os.path.join(B, "os8088-360.img"),
                 apps=os.path.join(B, "apps360.img"),
                 machine=M.machine("os8088_5150_cga_4fdd"),
                 extra=["--mount", "fd:2:" + media])
    try:
        ui = os88ui.UI(m, verbose=False)
        ui.ready(limit=240)
        ui.path("D:/MEDIA")
        mounts, ms = trace(m, ui)
    finally:
        m.quit()
        os.remove(media)
    vols = [v for _, v, _ in mounts]
    check("A: is never mounted", "A" not in vols, " ".join(vols))
    check("B: is reached", vols[-1:] == ["B"], " ".join(vols))


def main():
    legs = [a for a in sys.argv[1:] if not a.startswith("-")] or [
        "hd", "floppy", "four"]
    for leg in legs:
        {"hd": leg_hd, "floppy": leg_floppy, "four": leg_four}[leg]()
    if fails:
        print("assocsweep: FAIL (%s)" % ", ".join(fails))
        sys.exit(1)
    print("assocsweep: PASS")


if __name__ == "__main__":
    main()
