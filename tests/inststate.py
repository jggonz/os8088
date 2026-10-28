#!/usr/bin/env python3
"""The installer's State column says what a slot IS (SPEC.md 52.10.4.2).

It used to answer only whether an install would work - `Ready`, `Too Small`,
`Unusable`, `No Room`, `Booted From` - so the user's C:, a FAT volume nobody
had mounted and somebody else's Linux read `Ready`, `Ready` and `Unusable`.
The column is `[letter ': '] body [', ' reason]` now.

    python3 tests/inststate.py

THREE DISKS, each os8088_xt_hdd's VHD with slots 2-4 of its partition table
rewritten before the machine boots (slot 1 is the machine's own 31M FAT16,
which HDD.DRV mounts at boot as C:):

  a: Linux (83h), a FAT12 entry over sectors with no boot record in them and
     under HIW_MINSEC, and a type nobody names (2Bh);
  b: a FAT16 entry past FAT16's own ceiling (5,000,000 sectors - it was the
     65,535-sector one until SPEC.md 18.7.5), an Extended entry, and a
     FAT16 entry whose own CHS is not its LBA at this geometry (52.2.6);
  c: no MBR signature at all - an unpartitioned disk.

The column is the ENGINE's words (hd_iw_rowtext in HDDTOOL.DRV), and since
SPEC.md 52.10.4.4 its one reader is the INSTALL.O88 wizard, which HDI_SLOTS
hands them to as HDIS_TEXT. So the row starts the wizard from the Control
Panel's Install button, goes to its partition page, and reads the four
records the wizard holds - out of the package's own memory, because on the
glass the slots an install may not take are GREYED, and grey is a dither on
1bpp that no glyph match reads. What the wizard draws from them is
tests/install.py's subject. Nothing is clicked past the partition page, so
nothing is written.

BREAK IT ON PURPOSE: the tree before 52.10.4.2 reads `Ready` on slot 1 and
`Unusable` on slots 2-4 of `a` - the first expectation fails on its first row.
"""
import os
import re
import shutil
import struct
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty as M                                       # noqa: E402
import instdeep as ID                                       # noqa: E402

import instwiz as IW                                        # noqa: E402

API = open("apps/os88api.inc").read()
HDIS_TEXT, HDIS_TEXTSZ, HDIS_SIZE = (
    int(re.search(r"^%s\s+equ\s+(\d+)" % n, API, re.M).group(1))
    for n in ("HDIS_TEXT", "HDIS_TEXTSZ", "HDIS_SIZE"))

# (type, lba, sectors, bad CHS) for slots 2..4, and what each row must read
DISKS = {
    "a": ([(0x83, 70000, 20000, 0), (0x01, 40000, 1000, 0),
           (0x2B, 90000, 5000, 0)],
          ["C: FAT16", "Linux", "Not Formatted, Too Small", "Type 2Bh"]),
    "b": ([(0x06, 70000, 5000000, 0), (0x05, 300000, 5000, 0),
           (0x04, 400000, 30000, 1)],
          ["C: FAT16", "FAT16, Too Big", "Extended",
           "FAT16, Wrong Geometry"]),
    "c": (None,
          ["Unpartitioned"] * 4),
}


def stage(run_dir, layout):
    vhd = os.path.join(run_dir, ID.VHD_REL)
    b = bytearray(open(vhd, "rb").read())
    if layout is None:
        b[510:512] = b"\0\0"
    else:
        for i, (t, lba, n, badchs) in enumerate(layout):
            e = bytearray(16)
            e[4] = t
            if badchs:
                e[1], e[2], e[3] = 7, 5, 9      # a CHS that is not this LBA
            struct.pack_into("<II", e, 8, lba, n)
            o = 446 + 16 * (i + 1)
            b[o:o + 16] = e
    open(vhd, "wb").write(b)


def slot_text(wz, i):
    raw = wz.mem("in_slots", HDIS_TEXTSZ, i * HDIS_SIZE + HDIS_TEXT)
    return raw.split(b"\0")[0].decode("latin-1")


def one(name):
    layout, expect = DISKS[name]
    tag = "inststate-%d-%s" % (os.getpid(), name)
    run_dir = M._private_run_dir(M.base_run_dir(), tag)
    try:
        stage(run_dir, layout)
        with M.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=ID.MACHINE, run_dir=run_dir, boot=False) as m:
            m.run(); M.kernel_up(m)   # a state gate; every act is a click
            wz = ID.open_installer(m)
            wz.next(IW.P["P_DRIVE"], "after Welcome", limit=120.0)
            wz.next(IW.P["P_SLOT"], "after the drive", limit=120.0)
            bad = []
            for i, want in enumerate(expect):
                got = slot_text(wz, i)
                print("  %s slot %d: %-26r %s" % (name, i + 1, want,
                                                  "ok" if got == want else
                                                  "READS %r" % got))
                if got != want:
                    bad.append("disk %s slot %d reads %r, not %r"
                               % (name, i + 1, got, want))
            return bad
    finally:
        shutil.rmtree(run_dir, ignore_errors=True)


def main():
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    for img in ("build/os8088-360.img", "build/apps360.img"):
        if not os.path.exists(img):
            sys.exit("no %s - `make` first" % img)
    bad = []
    for name in sorted(DISKS):
        bad += one(name)
    if bad:
        sys.exit("inststate: " + "; ".join(bad) + " (SPEC.md 52.10.4.2)")
    print("inststate: all twelve rows read what the slot is")


if __name__ == "__main__":
    main()
