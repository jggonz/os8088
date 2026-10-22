#!/usr/bin/env python3
"""VESABENCH on QEMU: the correctness run of the packed display's bench
(SPEC.md 120.8, the plan's S7).

    make vesabench && python3 tests/vesabench.py [--only 16|8]

Boots build/vesabench.img (1024x768x16) and then build/vesabnc8.img
(800x600x8), each with build/vxbench.img in B:, whose \\AUTOSTRT link opens
VESABNCH.O88 at boot. The package runs its rows on its first paint and saves
VESABNCH.TXT beside itself on B:; this waits for the save, quits QEMU, and
reads the report back off the floppy image with mtools, printing it whole.

What is asserted is BEHAVIOUR, never a time (QEMU runs the guest at host
speed): the report exists, names the mode the disk asked for, every row
has a number, and `BLITD says DRAWN` - the depth blit was not refused. The
numbers are printed for the record; the ones that mean anything are taken
on 86Box's 486 (`make 486-vesa`) and written up under docs/reports/.
"""
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                              # noqa: E402
import os88build as _B                                      # noqa: E402
import os88fixture                                          # noqa: E402
import os88qemu                                             # noqa: E402
import os88sym                                              # noqa: E402
import vesa                                                 # noqa: E402
from os88geom import VID_VESA                               # noqa: E402

RUNS = {"16": ("build/vesabench.img", 1024, 768, 16),
        "8": ("build/vesabnc8.img", 800, 600, 8)}


def launch(sysimg, bimg):
    em = "qemu" + "-system-i386"
    subprocess.run(
        em + " -machine pc,vmport=off -vga std"
        " -drive file=%s,format=raw,if=floppy -boot a"
        " -drive file=%s,format=raw,if=floppy,index=1"
        " -serial msmouse"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
        % (_B.at(sysimg), _B.at(bimg), vesa.SOCK, vesa.PIDFILE),
        cwd=ROOT, shell=True, check=True)
    os88qemu.own(vesa.PIDFILE, vesa.SOCK)


def report(bimg):
    """VESABNCH.TXT out of the B: image, or None."""
    if not shutil.which("mcopy"):
        return None
    out = os.path.join(ROOT, "build", "vesabnch-%s.txt" % os.path.basename(bimg))
    if os.path.exists(out):
        os.unlink(out)
    r = subprocess.run(["mcopy", "-n", "-i", _B.at(bimg), "::VESABNCH.TXT", out],
                       capture_output=True)
    if r.returncode or not os.path.exists(out):
        return None
    return open(out, "rb").read().decode("ascii", "replace")


def one(key):
    sysimg, ww, wh, bpp = RUNS[key]
    bimg = "build/vxbench.img"
    os88fixture.need(sysimg, bimg)
    # a fresh copy of the B: image for each run: the package WRITES to it
    scratch = "build/vxbench-%s.img" % key
    shutil.copyfile(_B.at(bimg), _B.at(scratch))
    vesa.kill_stale()
    launch(sysimg, scratch)
    q = heapmap.Qmp(vesa.SOCK)
    for _ in range(200):
        try:
            q.hmp("info status")
            break
        except OSError:
            time.sleep(0.1)
    fails = []
    os88qemu.acted(q, lambda: vesa.byte(q, "vid_kind") == VID_VESA, 120.0,
                   "vid_kind = VID_VESA")
    if vesa.byte(q, "vid_kind") != VID_VESA:
        return ["%s: the desktop did not come up on VID_VESA" % key]
    # the save lands when the package's first paint has run every row: poll
    # the image's directory through mtools rather than the guest's memory
    deadline = time.time() + 300
    txt = None
    while time.time() < deadline:
        time.sleep(3)
        q.hmp("info status")             # keep the socket honest
        txt = report(scratch)
        if txt and "4F05h/FILL share" in txt:
            break
    q.hmp("quit")
    time.sleep(1)
    txt = report(scratch)
    if not txt:
        return ["%s: no VESABNCH.TXT on B: after 300 s (mtools: %s)"
                % (key, "present" if shutil.which("mcopy") else "MISSING")]
    print("== %s ==" % sysimg)
    print(txt)
    def kv(label):
        m = re.search(r"^%s\s+(\S+)" % re.escape(label), txt, re.M)
        return m.group(1) if m else None
    if kv("display width") != str(ww) or kv("display height") != str(wh):
        fails.append("%s: the report names %sx%s, want %dx%d"
                     % (key, kv("display width"), kv("display height"), ww, wh))
    if kv("bits per pixel") != str(bpp):
        fails.append("%s: bits per pixel %s, want %d" % (key, kv("bits per pixel"), bpp))
    if kv("adapter kind (4=VESA)") != "4":
        fails.append("%s: adapter kind %s, want 4" % (key, kv("adapter kind (4=VESA)")))
    if not re.search(r"^BLITD says\s+DRAWN", txt, re.M):
        fails.append("%s: BLITD was refused or the row is missing" % key)
    for row in ("4F05h BH=1 get", "FILL 624x200", "FILL 624x1", "FILL 8x8",
                "BLITD 624x16 x2", "BLIT4 624x16 x2", "FONT_RUN 78 cells",
                "bl_paint page", "window switches, est", "4F05h/FILL share, 0.1%"):
        if not re.search(r"^%s\s+\d" % re.escape(row), txt, re.M):
            fails.append("%s: no number on the row %r" % (key, row))
    return fails


def main():
    keys = ["16", "8"]
    if "--only" in sys.argv:
        keys = [sys.argv[sys.argv.index("--only") + 1]]
    fails = []
    for k in keys:
        fails += one(k)
    if fails:
        print("vesabench: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("vesabench: ok - the bench ran and saved itself at %s"
          % " and ".join("%dx%dx%d" % RUNS[k][1:] for k in keys))
    return 0


if __name__ == "__main__":
    sys.exit(main())
