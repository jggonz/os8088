#!/usr/bin/env python3
"""An install that KEEPS the volume's files, and then boots (SPEC.md 52.10.15).

    python3 tests/instkeep.py

The fixture is the MartyPC XT-IDE disk as it ships: a DOS 3.3 partition with
IO.SYS at cluster 2. Before the machine starts, the HOST (mtools, on a copy of
the partition) punches two holes in it - LTEMM.EXE and NANSI.SYS deleted - and
adds what a user would have: USER.TXT in the root, SYSTEM/APPDATA/NOTE.CFG,
and a SYSTEM.CFG of their own (hidden + system, generation 7, every driver
off). Then the installer runs with the Erase box as it defaults over a volume,
CLEAR.

That fixture is chosen so the interesting half is FORCED: cluster 2 belongs to
IO.SYS, so the kernel cannot go where a format puts it, the two holes are
below the only run long enough for it, and the boot sector must therefore add
a non-zero BOOTHD_KOFS - which is the whole of what a kept volume needs from
boot/boothd.asm.

ASSERTED ON THE HOST, with instdeep's own FAT reader:
  1. the user's three files are byte-identical, SYSTEM.CFG included - the
     install disks carry none of them, and a kept SYSTEM.CFG is the rule;
  2. DOS is still there (COMMAND.COM, IO.SYS);
  3. the installed tree is (SYSTEM/TASKMGR.O88, APPS, ...), KERNEL.SYS is the
     floppy's bytes, its chain is ONE RUN, and the VBR's BOOTHD_KOFS and
     BOOTHD_KSECS say exactly where and how long it is;
  4. the pad is gone (KPAD.TMP);
...and then the partition is BOOTED, hdboot's way: a desktop from drive C:.

BREAK IT ON PURPOSE, and both were: with BOOTHD_KOFS written as 0 the host
check names it and the boot reads IO.SYS's sectors as a kernel and never
reaches a desktop; with the pad skipped the kernel lands in the holes (38-41,
56-57, 61-63, 67...), hd_kverify refuses the commit, the MBR and VBR are left
as DOS had them - the partition still boots DOS - and the row times out
waiting for a commit that correctly never came.
"""
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty as M                                       # noqa: E402
import instdeep as ID                                       # noqa: E402
import hdboot as HB                                         # noqa: E402
import os88build                                            # noqa: E402

SECTOR = 512
BOOTHD_KOFS, BOOTHD_KSECS = 504, 508
USER_TXT = b"the user's own file, which an install must not touch\r\n" * 40
NOTE_CFG = b"an application's own settings\r\n"
USER_CFG = (b"O88CFG\0\0" + (7).to_bytes(2, "little") + b"DW" + bytes([1, 2])
            + (0).to_bytes(2, "little") + b"\0\0")


def mtools(*args, env=None):
    e = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    subprocess.run(args, check=True, env=e, stdout=subprocess.DEVNULL)


def stage(vhd):
    """Punch the holes and plant the user's files, through a copy of the
    partition that mtools can open."""
    blob = bytearray(open(vhd, "rb").read())
    e = blob[446:462]
    lba, secs = struct.unpack_from("<II", e, 8)
    part = bytes(blob[lba * SECTOR:(lba + secs) * SECTOR])
    with tempfile.TemporaryDirectory() as td:
        img = os.path.join(td, "part.img")
        open(img, "wb").write(part)
        for name, data in (("USER.TXT", USER_TXT), ("NOTE.CFG", NOTE_CFG),
                           ("SYSTEM.CFG", USER_CFG)):
            open(os.path.join(td, name), "wb").write(data)
        mtools("mdel", "-i", img, "::/LTEMM.EXE", "::/NANSI.SYS")
        mtools("mcopy", "-i", img, os.path.join(td, "USER.TXT"), "::/")
        mtools("mcopy", "-i", img, os.path.join(td, "SYSTEM.CFG"), "::/")
        mtools("mattrib", "-i", img, "+h", "+s", "::/SYSTEM.CFG")
        mtools("mmd", "-i", img, "::/SYSTEM", "::/SYSTEM/APPDATA")
        mtools("mcopy", "-i", img, os.path.join(td, "NOTE.CFG"),
               "::/SYSTEM/APPDATA/")
        part = open(img, "rb").read()
    blob[lba * SECTOR:(lba + secs) * SECTOR] = part
    open(vhd, "wb").write(blob)


def check(vhd):
    blob = open(vhd, "rb").read()
    lba = struct.unpack_from("<I", blob, 446 + 8)[0]
    v = ID.partition(vhd)
    tree = v.tree()
    bad = []
    for path, want in (("USER.TXT", USER_TXT),
                       ("SYSTEM/APPDATA/NOTE.CFG", NOTE_CFG),
                       ("SYSTEM.CFG", USER_CFG)):
        got = v.read(path)
        if got != want:
            bad.append("%s was not kept (%s)" % (
                path, "missing" if got is None else "%d bytes" % len(got)))
    for p in ("COMMAND.COM", "IO.SYS", "SYSTEM/TASKMGR.O88", "APPS",
              "SYSTEM/FONTS/TALLX.F88"):
        if p not in tree:
            bad.append("%s is missing" % p)
    if "KPAD.TMP" in tree:
        bad.append("the allocator's pad KPAD.TMP was left behind")

    src = ID.Vol(open(os88build.at("build/os8088-360.img"), "rb").read())
    want = src.read("KERNEL.SYS")
    got = v.read("KERNEL.SYS")
    if got != want:
        bad.append("KERNEL.SYS is not the floppy's bytes")
    first = [c for n, a, c, s in v.entries(0) if n == "KERNEL.SYS"][0]
    chain = v.chain(first)
    if chain != list(range(first, first + len(chain))):
        bad.append("KERNEL.SYS is not ONE RUN: %s" % chain[:12])
    vbr = blob[lba * SECTOR:(lba + 1) * SECTOR]
    kofs = struct.unpack_from("<I", vbr, BOOTHD_KOFS)[0]
    ksecs = struct.unpack_from("<H", vbr, BOOTHD_KSECS)[0]
    if kofs != (first - 2) * v.spc:
        bad.append("the VBR's BOOTHD_KOFS is %d and the kernel starts %d "
                   "sectors into the data area" % (kofs, (first - 2) * v.spc))
    if ksecs != (len(want) + SECTOR - 1) // SECTOR:
        bad.append("the VBR's BOOTHD_KSECS is %d" % ksecs)
    print("  kept: USER.TXT, NOTE.CFG, SYSTEM.CFG, DOS; KERNEL.SYS at cluster "
          "%d (%d clusters, one run), BOOTHD_KOFS %d" % (first, len(chain),
                                                         kofs))
    if first == 2:
        bad.append("the kernel went to cluster 2, so this run never exercised "
                   "BOOTHD_KOFS - the fixture is wrong, not the installer")
    return bad


def boot_c(m, when):
    """GLaBIOS's menu, C, and a desktop - or say which boot did not."""
    m.advance(frames=300)
    m.key("KeyC")                               # GLaBIOS: boot the hard disk
    for _ in range(160):
        m.advance(frames=30)
        w, h, px = m.fbuf()
        if len(HB.ink_groups(px, w, (4, 8))) > 20:
            print("  booted from C: %s" % when)
            return
    sys.exit("instkeep: FAIL - no desktop from drive C: %s. The VBR reads "
             "the kernel from BOOTHD_KOFS sectors into the data area "
             "(SPEC.md 52.10.15)" % when)


def main():
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    run_dir = M.stage_run_dir("instkeep")
    vhd = os.path.join(run_dir, ID.VHD_REL)
    stage(vhd)

    # 1. FROM THE FLOPPY, onto the DOS partition, keeping it.
    with M.launch("build/os8088-360.img", apps="build/apps360.img",
                  machine=ID.MACHINE, run_dir=run_dir) as m:
        M.settle(m)
        mo, ix, iy = ID.open_installer(m)
        ID.run_install(m, mo, ix, iy)           # the box as it defaults: KEEP
    bad = check(vhd)
    if bad:
        sys.exit("instkeep: " + "; ".join(bad))

    # 2. BOOT IT, and then UPGRADE IT IN PLACE from the machine it is running:
    #    the booted-from volume is a target when its files are kept. The new
    #    system disk goes in B:, because A: must hold a blank for the BIOS to
    #    offer C: at all - which is also the case the source probe has to find
    #    by itself. A byte of the MBR's unused tail is the host's marker that
    #    the second install really committed: its code is otherwise the same
    #    bytes the first one wrote.
    if not os.path.exists(HB.BLANK):
        with open(HB.BLANK, "wb") as f:
            f.write(bytes(368640))
    blob = bytearray(open(vhd, "rb").read())
    blob[400] = 0xA5
    open(vhd, "wb").write(blob)
    with M.launch(HB.BLANK, apps="build/os8088-360.img", machine=ID.MACHINE,
                  boot=0, run_dir=run_dir) as m:
        boot_c(m, "after the kept install")
        m.run()                                 # advance() left it paused
        M.settle(m)
        mo, ix, iy = ID.open_installer(m)
        ID.run_install(m, mo, ix, iy)
    if open(vhd, "rb").read(512)[400] != 0:
        sys.exit("instkeep: the in-place upgrade never committed its MBR")
    bad = check(vhd)
    if bad:
        sys.exit("instkeep: after the in-place upgrade: " + "; ".join(bad))

    # 3. ...and it still boots.
    with M.launch(HB.BLANK, machine=ID.MACHINE, boot=0, run_dir=run_dir) as m:
        boot_c(m, "after the in-place upgrade")
    print("instkeep: the user's files survived two installs, the kernel is "
          "one run past DOS, and the partition boots after each")


if __name__ == "__main__":
    main()
