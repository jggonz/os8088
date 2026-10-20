#!/usr/bin/env python3
"""An install ACROSS BUILDS from a machine booted off its hard disk (SPEC.md
52.10.16).

    python3 tests/instcross.py [--break]

**The field case, made reproducible.** A machine booted from C: put a NEWER
system disk in a floppy drive and installed it, keeping its files - the
ordinary way to upgrade - and the next boot froze at `Booting HDD0`. Every
file on C: was the new disk's; the volume boot record was the OLD build's,
because the installer wrote the copy built into the `HDDTOOL.DRV` it was
running, and on a machine booted from C: that tool is C:'s. One byte of
`KZ_HD` was enough. No row could see it: every install in the suite runs one
build on both sides.

So this row makes the two sides DIFFERENT builds. The stock tree goes onto
the disk first; then the knob pair `BOOTPROF=1 MOUDIAG=1` - knobhd's, whose
`BLOB_SEG` is three rungs from the stock kernel's, so any sector from the
wrong build fails loudly - is the system disk in B: while the machine runs
the stock kernel from C:.

  1. A STOCK install, from the stock floppy (the erasing one).
  2. Boot C:, and offer a knob system disk whose BOOTHD.SYS is HIDDEN (its
     directory entry renamed): a disk from before 52.10.16. The installer
     must REFUSE it and write nothing - the VBR and KERNEL.SYS stay stock.
  3. Boot C:, offer the real knob system disk, install keeping files. The
     VBR must be the knob tree's boothd.bin (bar the BPB and the two patch
     words the installer owns) and KERNEL.SYS the knob kernel.
  4. Boot C:, and reach a desktop on the knob kernel.

**BREAK IT ON PURPOSE with `--break`**: step 3's disk carries the STOCK
build's boot sector as its BOOTHD.SYS, which is byte for byte what the old
installer wrote from its own image - so step 4 must fail to reach a desktop,
and the host check in step 3 names the sector. That is the field defect, and
the row going red on it is what makes the green one mean something.

Step 2's refusal is ATTRIBUTED by construction rather than read off the
screen: its floppy differs from step 3's in one directory entry's extension,
and step 3, with the same clicks, commits.

MEASURED (MartyPC, os8088_xt_hdd): green in 466s wall beside the --break arm,
215s of CPU; --break fails at step 4 with "the VBR is the STOCK build's boot
sector" named in step 3.

It ERASES the VHD (its own clone, per instance) and builds the knob pair into
a private tree (tools/os88build.py), shared with tests/knobhd.py's.
"""
import argparse
import hashlib
import os
import struct
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty as M                                       # noqa: E402
import os88build                                            # noqa: E402
import instdeep as ID                                       # noqa: E402
import hdboot as HB                                         # noqa: E402

SECTOR = 512
BPB_END, BOOTHD_KOFS = 62, 504
KNOBS = ["BOOTPROF=1", "MOUDIAG=1"]     # knobhd's pair, and its tree


def code(sector):
    """What a VBR takes from BOOTHD.SYS: the jump, and everything from the
    end of the BPB up to the two patch words, plus the signature."""
    return sector[0:3] + sector[BPB_END:BOOTHD_KOFS] + sector[510:512]


def vbr_and_kernel(vhd):
    blob = open(vhd, "rb").read()
    lba = struct.unpack_from("<I", blob, 446 + 8)[0]
    vbr = blob[lba * SECTOR:(lba + 1) * SECTOR]
    return vbr, ID.partition(vhd).read("KERNEL.SYS")


def doctored(src, out, how, sector=None):
    """A copy of system floppy `src` with its BOOTHD.SYS hidden ('hide') or
    replaced by `sector` ('swap'). FAT12, root directory, by hand."""
    b = bytearray(open(src, "rb").read())
    v = ID.Vol(bytes(b))
    root = v.root_lba * v.bps
    for o in range(root, root + v.root_secs * v.bps, 32):
        if b[o:o + 11] == b"BOOTHD  SYS":
            if how == "hide":
                b[o + 8:o + 11] = b"OLD"        # BOOTHD.OLD: no longer found
            else:
                c = struct.unpack_from("<H", b, o + 26)[0]
                at = (v.data_lba + (c - 2) * v.spc) * v.bps
                b[at:at + SECTOR] = sector
            break
    else:
        sys.exit("instcross: %s carries no BOOTHD.SYS - the build is not "
                 "SPEC.md 52.10.16's" % src)
    open(out, "wb").write(b)
    return out


def boot_c(m):
    """GLaBIOS's menu, C, and a desktop. True if one came up."""
    m.advance(frames=300)
    m.key("KeyC")                               # GLaBIOS: boot the hard disk
    for _ in range(160):
        m.advance(frames=30)
        w, h, px = m.fbuf()
        if len(HB.ink_groups(px, w, (4, 8))) > 20:
            return True
    return False


def offer(run_dir, vhd, floppy, commit):
    """Boot C:, put `floppy` in B:, Install keeping files. With `commit`,
    wait for the MBR the way instkeep does; without it, wait for the drive to
    stay still - a refusal writes nothing at all."""
    blob = bytearray(open(vhd, "rb").read())
    blob[400] = 0xA5                    # the MBR's unused tail: an install
    open(vhd, "wb").write(blob)         # that commits rewrites it to 0
    with M.launch(HB.BLANK, apps=floppy, machine=ID.MACHINE, boot=0,
                  run_dir=run_dir) as m:
        if not boot_c(m):
            sys.exit("instcross: FAIL - the STOCK install would not boot "
                     "from C:, so nothing after it means anything")
        m.run()                         # advance() left it paused
        M.settle(m)
        mo, ix, iy = ID.open_installer(m)
        if commit:
            ID.run_install(m, mo, ix, iy)
            return
        bx = ix + ID.HIW_B0X + ID.HIW_BW0 // 2
        by = iy + ID.HIW_BY + ID.HIW_BH // 2
        mo.click(bx, by, settle=3.0)    # arms
        mo.click(bx, by, settle=0)      # goes - and must stop at once
        M.quiesce(m, lambda: hashlib.sha1(open(vhd, "rb").read(8 << 20))
                  .digest(), guest=2.0, stable=8, budget=240.0,
                  what="the drive to stay still")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--break", dest="brk", action="store_true",
                    help="give the knob disk the STOCK boot sector, which is "
                         "what the pre-52.10.16 installer wrote: must FAIL")
    a = ap.parse_args()
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")

    print("  building the knob pair (%s)..." % " ".join(KNOBS))
    knob = os88build.tree(*KNOBS)
    stock_vbr = open(os88build.at("build/boothd.bin"), "rb").read()
    knob_vbr = open(knob.img("boothd.bin"), "rb").read()
    stock_k = open(os88build.at("build/kernel.sys"), "rb").read()
    knob_k = open(knob.img("kernel.sys"), "rb").read()
    if code(stock_vbr) == code(knob_vbr):
        sys.exit("instcross: the knob pair's boot sector is the stock one, so "
                 "this row would pass whatever the installer wrote - pick "
                 "knobs that move BLOB_SEG")

    run_dir = M.stage_run_dir("instcross")
    vhd = os.path.join(run_dir, ID.VHD_REL)
    if not os.path.exists(HB.BLANK):
        with open(HB.BLANK, "wb") as f:     # no signature: GLaBIOS offers C:
            f.write(bytes(368640))

    # 1. the STOCK build onto the disk, the erasing install
    print("  1. a stock install from the stock floppy")
    with M.launch("build/os8088-360.img", apps="build/apps360.img",
                  machine=ID.MACHINE, run_dir=run_dir) as m:
        M.settle(m)
        ID.install(m)
    vbr, k = vbr_and_kernel(vhd)
    if code(vbr) != code(stock_vbr) or k != stock_k:
        sys.exit("instcross: FAIL - the stock install is not the stock build "
                 "(VBR %s, kernel %s)" % (code(vbr) == code(stock_vbr),
                                          k == stock_k))

    # 2. a knob disk from BEFORE 52.10.16: refused, nothing written
    print("  2. booted from C:, offered a system disk with no BOOTHD.SYS")
    old = doctored(knob.img("os8088-360.img"),
                   os.path.join(run_dir, "old360.img"), "hide")
    offer(run_dir, vhd, old, commit=False)
    vbr, k = vbr_and_kernel(vhd)
    if open(vhd, "rb").read(512)[400] != 0xA5:
        sys.exit("instcross: FAIL - a disk with no BOOTHD.SYS was INSTALLED "
                 "(the MBR was rewritten); SPEC.md 52.10.16 refuses it")
    if code(vbr) != code(stock_vbr) or k != stock_k:
        sys.exit("instcross: FAIL - the refused install still wrote: VBR "
                 "stock %s, kernel stock %s" % (code(vbr) == code(stock_vbr),
                                                k == stock_k))
    print("     refused, and C: is untouched")

    # 3. the real knob disk (or, with --break, one carrying the STOCK sector
    #    - what the old installer wrote out of its own image)
    if a.brk:
        print("  3. --break: the knob disk carries the STOCK boot sector")
        src = doctored(knob.img("os8088-360.img"),
                       os.path.join(run_dir, "brk360.img"), "swap", stock_vbr)
    else:
        print("  3. booted from C:, offered the knob system disk")
        src = knob.img("os8088-360.img")
    offer(run_dir, vhd, src, commit=True)
    if open(vhd, "rb").read(512)[400] != 0:
        sys.exit("instcross: FAIL - the knob install never committed its MBR")
    vbr, k = vbr_and_kernel(vhd)
    bad = []
    if k != knob_k:
        bad.append("KERNEL.SYS is not the knob kernel")
    if code(vbr) != code(knob_vbr):
        bad.append("the VBR is %s boot sector, not the knob tree's - it "
                   "far-calls another kernel's blob and freezes at `Booting "
                   "HDD0` (SPEC.md 52.10.16)"
                   % ("the STOCK build's" if code(vbr) == code(stock_vbr)
                      else "an unknown"))
    if bad and not a.brk:
        sys.exit("instcross: FAIL - " + "; ".join(bad))
    for b in bad:
        print("     (--break) " + b)

    # 4. ...and the knob kernel boots from C:
    print("  4. booting C: on the knob kernel")
    with M.launch(HB.BLANK, machine=ID.MACHINE, boot=0, run_dir=run_dir) as m:
        ok = boot_c(m)
    if not ok:
        sys.exit("instcross: FAIL - no desktop from C: after the cross-build "
                 "install" + (" (EXPECTED under --break: this is the field "
                              "defect)" if a.brk else ""))
    if a.brk:
        sys.exit("instcross: --break reached a desktop with the wrong boot "
                 "sector, so the row cannot tell the defect from the fix")
    print("instcross: a hard-disk-booted stock machine refused an old disk, "
          "installed a knob build's own boot sector, and booted it")


if __name__ == "__main__":
    main()
