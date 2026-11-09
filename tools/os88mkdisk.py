#!/usr/bin/env python3
"""os88mkdisk - "...and make a disk of it" (SPEC.md 98.2.12.1), shared.

The encoder's window (tools/os88vencgui.py) puts a video and its player on
a disk, and the Video Battle builder (tools/os88vbt.py `disk`,
tools/os88vbtgui.py's Build tab) puts a battle and its engine on one. Both
come through here, so there is one list of disks and one way of making
each:

  - the four floppies os88disk.py builds, for B: beside a system disk;
  - three hard disks os88hdd.py builds - the geometries of the machines
    the videos and battles are made for - which BOOT when the boot files
    are at hand (a built tree's build/, or the bundle's boot/ beside the
    tool) and are made formatted and unbootable when they are not.

`disk_of(files, disk)` answers the command line and the image; it runs
nothing. Imports nothing but the standard library.
"""
import os
import shutil
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_HERE)

# A hard disk is (tag, cylinders, heads, sectors, an ST11's layout)
#   label                                                 kind  what
DISKS = [
    ("a 360 KB floppy", "fd", 360),
    ("a 720 KB floppy", "fd", 720),
    ("a 1.2 MB floppy", "fd", 1200),
    ("a 1.44 MB floppy", "fd", 1440),
    ("a 20 MB hard disk: ST-225 on an ST11M (5150/XT)", "hd",
     ("ST11M", 615, 4, 17, True)),
    ("a 32 MB hard disk: ST-238R on an ST11R (XT)", "hd",
     ("ST11R", 615, 4, 26, True)),
    ("a 32 MB hard disk: IDE (286 and up)", "hd",
     ("IDE", 250, 15, 17, False)),
]
DISK_LABELS = [d[0] for d in DISKS]
# the disk a storage profile implies, until the person picks another
DISK_OF_PROFILE = {"5150-st225": 4, "5150-picomem2": 4, "floppy": 0,
                   "286-slow": 6, "286-fast": 6, "286-picomem1": 6,
                   "286-speeddemon": 6,
                   "486-dx2-66": 6, "lossless": 6}
# what a hard disk needs to BOOT - the kernel, its boot records and HDD.DRV
# - and the drivers a booting one takes besides when they are there
HD_BOOT = (("", "kernel.sys"), ("", "boothd.bin"),       # (os88hdd lays the
                                                       # kernel down itself)
           ("", "mbr.bin"), ("HDD.DRV", "hdd.drv"))
HD_DRV = (("CTRL.DRV", "ctrl.drv"), ("SOUND.DRV", "sound.drv"),
          ("HIBER.DRV", "hiber.drv"))
# the usable bytes of each floppy's data area, for a refusal before a
# failure: FAT12, 512-byte sectors (SPEC.md 19)
FD_BYTES = {360: 354 * 1024, 720: 713 * 1024, 1200: 2371 * 512,
            1440: 2847 * 512}


def boot_dir(build=None):
    """Where a hard disk's boot files come from: build/, or the bundle's
    boot/ BESIDE this tool (SPEC.md 98.2.13) - so a tool handed to people
    with no os8088 tree makes a hard disk that boots"""
    b = build or os.path.join(_ROOT, "build")
    for d in (b, os.path.join(_HERE, "boot")):
        if all(os.path.exists(os.path.join(d, f)) for _, f in HD_BOOT):
            return d
    return b


def hd_missing(build=None):
    """What the boot files' folder (boot_dir) lacks for a hard disk that
    BOOTS: [] when it can make one. Anything missing makes the disk
    unbootable, not refused"""
    b = boot_dir(build)
    return [f for _, f in HD_BOOT if not os.path.exists(os.path.join(b, f))]


def fits(files, disk):
    """None when FILES' bytes fit DISKS[disk], else what they need, in
    bytes, against what it holds"""
    label, kind, what = DISKS[disk]
    if kind != "fd":
        return None
    need = sum((os.path.getsize(p) + 1023) // 1024 * 1024 for _, p in files)
    return None if need <= FD_BYTES[what] else (need, FD_BYTES[what])


def disk_of(files, disk, img, build=None, stage=None):
    """(the command line, the image IMG) for DISKS[disk] holding FILES,
    (8.3 name, path) pairs, in their root. A floppy takes them as they are
    (a copy under its 8.3 name in STAGE, a directory, when a file's own
    name differs); a hard disk BOOTS when the boot files are at hand and
    takes the drivers too - without them it is formatted, not bootable,
    and takes FILES alone"""
    label, kind, what = DISKS[disk]
    b = boot_dir(build)
    if kind == "fd":
        argv = [sys.executable, os.path.join(_HERE, "os88disk.py"), "-o",
                img, "--size", str(what)]
        for name, p in files:
            if os.path.basename(p).upper() != name:
                q = os.path.join(stage, name)
                shutil.copyfile(p, q)
                p = q
            argv.append(p)
        return argv, img
    tag, cyls, heads, spt, st11 = what
    argv = [sys.executable, os.path.join(_HERE, "os88hdd.py"), "--out", img,
            "--cyls", str(cyls), "--heads", str(heads), "--spt", str(spt)]
    if st11:
        argv.append("--st11")
    if hd_missing(b):
        argv.append("--noboot")
        boot = ()
    else:
        argv += ["--kernel", os.path.join(b, "kernel.sys"),
                 "--vbr", os.path.join(b, "boothd.bin"),
                 "--mbr", os.path.join(b, "mbr.bin")]
        boot = HD_BOOT + HD_DRV
    for nm, f in boot:
        if nm and os.path.exists(os.path.join(b, f)):
            argv += ["--file", "%s=%s" % (nm, os.path.join(b, f))]
    for name, p in files:
        argv += ["--file", "%s=%s" % (name, p)]
    return argv, img
