#!/usr/bin/env python3
"""An install whose two disks are MOUNTED IMAGES on the hard disk (SPEC.md
52.10.17).

    python3 tests/instimg.py [--from SYSTEM.IMG] [--limit SECONDS]

The upgrade a hard-disk machine actually does: a new release's system and
apps disk images are copied onto C:, mounted as drives, and installed onto C:
keeping its files - with no floppy written anywhere. Before 52.10.17 the
installer looked for both disks on FLOPPY volumes alone, and an image on a
hard disk answers its host's VK_FIXED, so neither was found.

  1. The HOST (mtools) builds SYS.IMG out of the 360KB system disk and
     APPS.IMG out of the 360KB apps disk, each with a MARKER only it carries
     (IMGSYS.TXT in the root, APPS/IMGAPPS.TXT), and puts both in the root of
     MartyPC's DOS partition.
  2. An ordinary install from the two floppies, keeping files - so the
     partition boots os8088, the images are still there, and neither marker
     is on C: (asserted: a marker already there would prove nothing).
  3. Boot C: with a blank in A: (GLaBIOS offers C: only past one) and NOTHING
     in B:. Double-click SYS.IMG and APPS.IMG in C:'s window - two DRVC_IMG
     rows - close the window, and Install onto C:, keeping files. The only
     copies of either disk in the machine are the images.
  4. Asserted on the host: the MBR committed again, both markers arrived (so
     the SYSTEM phase read SYS.IMG and the APPS phase read APPS.IMG with no
     swap prompt), KERNEL.SYS is the image's bytes, and the two .IMG files
     are intact. Then C: boots.

BREAK IT ON PURPOSE: `--from` names the system floppy step 2 installs, and so
the HDDTOOL.DRV that step 3 runs. A system disk built before 52.10.17 (or with
hd_iapps_cand's image arm taken out) finds no system disk, writes nothing, and
the commit never comes - `--limit 300` turns that into a five-minute failure
rather than the default ten.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty as M                                       # noqa: E402
import os88ui                                               # noqa: E402
import instdeep as ID                                       # noqa: E402
import instkeep as IK                                       # noqa: E402
import hdboot as HB                                         # noqa: E402
import os88build                                            # noqa: E402

from os88geom import DRVC_IMG, DV_SIZE                      # noqa: E402

SECTOR = 512
SYS_MARK = b"this file is on the system disk IMAGE and nowhere else\r\n"
APPS_MARK = b"this file is on the apps disk IMAGE and nowhere else\r\n"


def arg(name, default):
    if name in sys.argv:
        return sys.argv[sys.argv.index(name) + 1]
    return default


SYS_FLOPPY = arg("--from", os88build.at("build/os8088-360.img"))
APPS_FLOPPY = os88build.at("build/apps360.img")
LIMIT = float(arg("--limit", "600"))


def mtools(*args):
    subprocess.run(args, check=True, env=dict(os.environ,
                                              MTOOLS_SKIP_CHECK="1"),
                   stdout=subprocess.DEVNULL)


def stage(vhd, td):
    """The two images, each with its marker, into the partition's root."""
    sysimg = os.path.join(td, "SYS.IMG")
    appsimg = os.path.join(td, "APPS.IMG")
    shutil.copyfile(SYS_FLOPPY, sysimg)
    shutil.copyfile(APPS_FLOPPY, appsimg)
    for name, data in (("IMGSYS.TXT", SYS_MARK), ("IMGAPPS.TXT", APPS_MARK)):
        open(os.path.join(td, name), "wb").write(data)
    mtools("mcopy", "-i", sysimg, os.path.join(td, "IMGSYS.TXT"), "::/")
    mtools("mcopy", "-i", appsimg, os.path.join(td, "IMGAPPS.TXT"),
           "::/APPS/")

    blob = bytearray(open(vhd, "rb").read())
    lba, secs = struct.unpack_from("<II", blob, 446 + 8)
    part = os.path.join(td, "part.img")
    open(part, "wb").write(blob[lba * SECTOR:(lba + secs) * SECTOR])
    mtools("mcopy", "-i", part, sysimg, appsimg, "::/")
    blob[lba * SECTOR:(lba + secs) * SECTOR] = open(part, "rb").read()
    open(vhd, "wb").write(blob)
    return open(sysimg, "rb").read(), open(appsimg, "rb").read()


def images(ui):
    base = ui._S("dsk_vtab")
    out = []
    for v in range(8):
        r = bytes(ui.m.read(base + DV_SIZE * v, DV_SIZE))
        if r[0] == 1 and r[3] == DRVC_IMG:
            out.append(v)
    return out


def mount(ui, win, name, n):
    ui.open(name, expect=None, win=win)
    M.until(ui.m, lambda _: len(images(ui)) == n,
            "%s to become a drive" % name, guest=60)
    print("  %s mounted: image rows %s" % (name, images(ui)))


def install(m):
    """instdeep's run_install with a limit of our own: the failure this row
    exists to catch is a commit that never comes."""
    mo, ix, iy = ID.open_installer(m)
    bx = ix + ID.HIW_B0X + ID.HIW_BW0 // 2
    by = iy + ID.HIW_BY + ID.HIW_BH // 2
    mo.click(bx, by, settle=3.0)                # Install: arms
    mo.click(bx, by, settle=0)                  # Install: goes
    disk = ID.vhd(m)
    base = open(disk, "rb").read(446)
    try:
        took = M.until(m, lambda _: open(disk, "rb").read(446) != base,
                       "the installer to commit its MBR", limit=LIMIT)
    except M.MartyError:
        sys.exit("instimg: FAIL - the install from the mounted images never "
                 "committed: the installer found no system disk on them "
                 "(SPEC.md 52.10.17)")
    print("  MBR committed after %.0fs" % took)
    M.quiesce(m, lambda: __import__("hashlib").sha1(
                  open(disk, "rb").read(8 << 20)).digest(),
              guest=2.0, stable=8, budget=480.0, what="the drive to go quiet")


def main():
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    if not shutil.which("mcopy"):
        sys.exit("no mtools - `apt-get install mtools`")
    run_dir = M.stage_run_dir("instimg")
    vhd = os.path.join(run_dir, ID.VHD_REL)
    with tempfile.TemporaryDirectory() as td:
        sysimg, appsimg = stage(vhd, td)

    # 2. an ordinary install, from the floppies, keeping the partition's
    #    files - which keeps the two images
    with M.launch(SYS_FLOPPY, apps=APPS_FLOPPY, machine=ID.MACHINE,
                  run_dir=run_dir) as m:
        M.settle(m)
        mo, ix, iy = ID.open_installer(m)
        ID.run_install(m, mo, ix, iy)
    tree = ID.partition(vhd).tree()
    for p in ("SYS.IMG", "APPS.IMG", "KERNEL.SYS"):
        if p not in tree:
            sys.exit("instimg: %s is not on C: after the floppy install" % p)
    for p in ("IMGSYS.TXT", "APPS/IMGAPPS.TXT"):
        if p in tree:
            sys.exit("instimg: %s is on C: BEFORE the image install, so the "
                     "row could not tell the images were read" % p)

    # 3. boot C:, mount both, install from them with no disk in B:
    if not os.path.exists(HB.BLANK):
        with open(HB.BLANK, "wb") as f:
            f.write(bytes(368640))
    blob = bytearray(open(vhd, "rb").read())
    blob[400] = 0xA5                            # instkeep's commit marker
    open(vhd, "wb").write(blob)
    with M.launch(HB.BLANK, machine=ID.MACHINE, boot=0,
                  run_dir=run_dir) as m:
        IK.boot_c(m, "after the floppy install")
        m.run()
        M.settle(m)
        ui = os88ui.UI(m)
        w = ui.open_drive("C")
        mount(ui, w, "SYS.IMG", 1)
        mount(ui, w, "APPS.IMG", 2)
        ui.close(w)
        M.settle(m)
        install(m)

    # 4. what arrived, read by a FAT walker that is not the one that wrote it
    v = ID.partition(vhd)
    tree = v.tree()
    bad = []
    if open(vhd, "rb").read(512)[400] != 0:
        bad.append("the MBR was not written again")
    if v.read("IMGSYS.TXT") != SYS_MARK:
        bad.append("IMGSYS.TXT did not arrive: the SYSTEM phase did not read "
                   "SYS.IMG")
    if v.read("APPS/IMGAPPS.TXT") != APPS_MARK:
        bad.append("APPS/IMGAPPS.TXT did not arrive: the APPS phase did not "
                   "read APPS.IMG")
    if v.read("KERNEL.SYS") != ID.Vol(sysimg).read("KERNEL.SYS"):
        bad.append("KERNEL.SYS is not the image's bytes")
    if v.read("SYS.IMG") != sysimg or v.read("APPS.IMG") != appsimg:
        bad.append("an image's bytes changed under the install")
    for p in ("COMMAND.COM", "IO.SYS", "SYSTEM/TASKMGR.O88"):
        if p not in tree:
            bad.append("%s is missing" % p)
    if bad:
        sys.exit("instimg: FAIL - " + "; ".join(bad))
    print("  both markers arrived, KERNEL.SYS is the image's, both images "
          "intact")

    # ...and it boots
    with M.launch(HB.BLANK, machine=ID.MACHINE, boot=0, run_dir=run_dir) as m:
        IK.boot_c(m, "after the install from the images")
    print("instimg: a hard-disk machine upgraded itself from two mounted "
          "images, no floppy written, and boots")


if __name__ == "__main__":
    main()
