#!/usr/bin/env python3
"""megainst - the INSTALL wizard under kern_mega, through HDD.DRV's raw door
(KERN-MEGA-MEMORY-PLAN 7, MC7 as built: the installer's Next).

    make mega && python3 tests/megainst.py [--keep]

The owner ran the installer on kern_mega (86Box, the mr286) and its first
Next faulted the machine: `kern_mega FAULT 0D err 0000 at 1154:07BE`. The
selector was HDD.DRV's code alias and 07BE its `rep movsw` in hd_raw's
bounce. MC7 step 6 had widened that bounce from "our own image above 1MB" to
ANY selector, and the bounce copied the buffer through DS:BX - which was
right while the only buffers that reached it were HDD.DRV's own. HDDTOOL.DRV
reaches the resident through HSV_RAW with a buffer in ITS image (hd_mbr at
3400h), and on kern_mega that is a selector too: so the copy read HDD.DRV's
own data segment at the tool's offset and ran off its limit. The bounce now
copies from and to the caller's ES:BX, and carries CX sectors rather than
one (the installer's FAT chunk, hd_kfat_get, is three).

Under Bochs at 16MB: kern_mega with SYSTEM.CFG wanting HDD.DRV, the
kern_mega arm's INSTALL.O88 started by MEGARUN off C:, and the wizard walked
by its default button (Enter) from Welcome to the end - Welcome, the drive,
the partition (the crossing that faulted: the tool's hd_part_load), ERASE
(so the formatter's sectors cross too), the confirm page, the install itself
onto C: with B: as the apps disk (hd_kplan's three-sector FAT chunks through
the bounce), and Next off its log. Then C:'s KERNEL.SYS is read back on the
host with mtools and must be build/megak/kernel.sys byte for byte. Every page is read out of the package's own memory ([in_page],
off build/megak/apps/install.map). Any `kern_mega FAULT` or `CLOSED` line
fails it at the step it appeared after.

Broken on purpose (docs/WRITING-TESTS.md 1): with the parent commit's
hd_raw (drivers/hdd/mount.inc, the bounce copying through DS:BX) it fails at
the partition page - `kern_mega FAULT vector 0D` with IP 07BE, the owner's
fault to the offset. NOT gated here: the bounce's sector count. Put back to
one sector, the install still ends right on a fresh volume (the FAT chunk's
other two sectors read as the zeros a fresh FAT is), and the 512 bytes the
transfer writes past a 1KB claim land somewhere nothing checks.
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
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
import os88bochs  # noqa: E402

SRC = open(os.path.join(ROOT, "apps", "install", "install.asm")).read()
PKG = os.path.join(ROOT, "build", "megak", "apps", "install.o88")
MAP = os.path.join(ROOT, "build", "megak", "apps", "install.map")
APPS = os.path.join(ROOT, "build", "megaapps.img")
CFG_HDD = 1 << 1                # drv_cfgbit: HDD.DRV is bit 1


def const(name):
    m = re.search(r"^%s\s+equ\s+(\d+)" % name, SRC, re.M)
    if not m:
        sys.exit("megainst: %s is not an `equ` in apps/install/install.asm"
                 % name)
    return int(m.group(1))


P = {n: const(n) for n in ("P_WELCOME", "P_DRIVE", "P_SLOT", "P_MODE",
                            "P_CONFIRM", "P_APPS", "P_DONE", "P_STOP",
                            "P_BUSY")}
NAMES = {v: k for k, v in P.items()}


class Fail(Exception):
    pass


def installed(c, d):
    """The install's result, read on the HOST with mtools: the first
    partition's KERNEL.SYS must be the one the system floppy carries - the
    wizard says Done after a copy, and only the bytes say what was copied."""
    import struct
    import subprocess
    with open(c, "rb") as f:
        lba = struct.unpack_from("<I", f.read(512), 446 + 8)[0]
    k = os.path.join(d, "kernel.sys")
    r = subprocess.run(["mcopy", "-n", "-i", "%s@@%d" % (c, lba * 512),
                        "::KERNEL.SYS", k], capture_output=True, text=True,
                       env=dict(os.environ, MTOOLS_SKIP_CHECK="1"))
    if r.returncode:
        raise Fail("no KERNEL.SYS on C: after the install: %s"
                   % r.stderr.strip())
    with open(k, "rb") as f, open(os.path.join(ROOT, "build", "megak",
                                               "kernel.sys"), "rb") as g:
        if f.read() != g.read():
            raise Fail("C:'s KERNEL.SYS is not build/megak/kernel.sys")


def main():
    keep = "--keep" in sys.argv
    for f in (PKG, MAP, APPS, os.path.join(ROOT, "build", "mega.img"),
              os.path.join(ROOT, "build", "megarun.o88")):
        if not os.path.exists(f):
            sys.exit("megainst: %s is missing - `make mega megarun` first"
                     % os.path.relpath(f, ROOT))
    d = tempfile.mkdtemp(prefix="megainst-")
    t0 = time.time()
    box = os88bochs.Box(d, [PKG], cfg=CFG_HDD, bimg=APPS, btype="1_44")
    bad = []

    def faulted():
        return box.faults()

    def byte(label):
        return box.pkg_read("INSTALL", label, 1, pkgmap=MAP)[0]

    def page():
        return byte("in_page")

    def wait(cond, what, secs):
        end = time.time() + secs
        while time.time() < end:
            f = faulted()
            if f:
                raise Fail("%s: %s" % (what, f[0]))
            try:
                if cond():
                    return
            except RuntimeError:
                pass                    # no INSTALL instance yet
            time.sleep(0.5)
        raise Fail("timed out waiting for %s" % what)

    def at(want, what, secs=120.0):
        def ok():
            pg = page()
            if pg == P["P_STOP"] and want != P["P_STOP"]:
                raise Fail("the install STOPPED %s" % what)
            return pg == want
        wait(ok, "INSTALL to show %s (%s)" % (NAMES[want], what), secs)
        print("   ok    %-10s %s" % (NAMES[want][2:], what), flush=True)

    def enter():
        box.key("Return")
    try:
        if not box.wait_pm(300):
            raise Fail("kern_mega never reached protected mode")
        box.up()
        box.megarun()
        at(P["P_WELCOME"], "on launch", 120.0)
        # MEGARUN's folder walk FINISHED before anything changes C:: it
        # starts a package every five seconds off a FILE_FIND ordinal, and
        # once the install has erased C: and written the system, its stale
        # folder lists SYSTEM\'s and it starts the Task Manager and the
        # rest, which take the focus the wizard's Enter is for (a layout
        # race: the kernel's size decides which cluster the folder was)
        wait(lambda: any(ln.startswith("MEGARUN: done")
                         for ln in box.lines()), "MEGARUN's folder to end",
             60.0)
        enter()
        at(P["P_DRIVE"], "after Welcome: the driver and the drives")
        enter()                         # THE CROSSING THAT FAULTED: the
        at(P["P_SLOT"], "after the drive - the tool's partition read "
           "through HSV_RAW")
        enter()
        wait(lambda: page() in (P["P_MODE"], P["P_CONFIRM"]),
             "INSTALL to leave the partition page", 120.0)
        if page() == P["P_MODE"]:
            if not byte("in_wipe"):     # ERASE: the formatter's writes too
                box.key("Down")
                wait(lambda: byte("in_wipe") == 1,
                     "the arrow key to pick Erase", 30.0)
            print("   ok    MODE       Erase", flush=True)
            enter()
            at(P["P_CONFIRM"], "after keep or erase")
        enter()                         # Install
        wait(lambda: page() == P["P_BUSY"] and byte("in_logend"),
             "the Installing page to end", 900.0)
        print("   ok    BUSY       the install ended", flush=True)
        enter()
        wait(lambda: page() in (P["P_DONE"], P["P_APPS"], P["P_STOP"]),
             "INSTALL to go on from its log", 120.0)
        if page() == P["P_STOP"]:
            raise Fail("the install STOPPED after its log")
        print("   ok    %-10s after Next on the log" % NAMES[page()][2:],
              flush=True)
        f = faulted()
        if f:
            raise Fail("at the end: %s" % f[0])
    except Fail as e:
        bad.append(str(e))
        box.shot(os.path.join(d, "fail.png"))
    finally:
        box.stop()
    if not bad:
        try:
            installed(os.path.join(d, "c.img"), d)
            print("   ok    C:'s KERNEL.SYS is kern_mega's, byte for byte",
                  flush=True)
        except Fail as e:
            bad.append(str(e))
    print("megainst: %.0fs" % (time.time() - t0))
    if bad:
        for b in bad:
            print("FAIL: %s" % b)
        print("megainst: the boot's files are in %s" % d)
        return 1
    if not keep:
        shutil.rmtree(d, ignore_errors=True)
    else:
        print("megainst: kept %s" % d)
    print("megainst: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
