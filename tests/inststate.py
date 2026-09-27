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

Every row is READ out of the framebuffer against the kernel's own glyph table,
tests/instrest.py's apparatus: a string rendered on the host, searched for in
that row's band in both polarities (the selected row is drawn inverted). The
installer is never clicked past its open, so nothing is written.

BREAK IT ON PURPOSE: the tree before 52.10.4.2 reads `Ready` on slot 1 and
`Unusable` on slots 2-4 of `a` - the first expectation fails on its first row.
"""
import os
import shutil
import struct
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty as M                                       # noqa: E402
import instdeep as ID                                       # noqa: E402
import instrest as IR                                       # noqa: E402

# inst.inc's row geometry, content-relative
HIW_LX, HIW_R0Y, HIW_ROWH, HIW_W = 8, 24, 12, 356

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


def row_has(rows, ix, iy, i, want):
    x0, y0 = ix + HIW_LX, iy + HIW_R0Y + i * HIW_ROWH
    for ink in (want, [[1 - p for p in r] for r in want]):
        if IR.find(rows, x0, y0 - 1, HIW_W - 12, 10, ink):
            return True
    return False


def one(name):
    layout, expect = DISKS[name]
    tag = "inststate-%d-%s" % (os.getpid(), name)
    run_dir = M._private_run_dir(M.base_run_dir(), tag)
    try:
        stage(run_dir, layout)
        with M.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=ID.MACHINE, run_dir=run_dir) as m:
            M.settle(m)
            tab = IR.glyph_table(m)
            mo, ix, iy = ID.open_installer(m)
            IR.park(mo)
            M.settle(m)
            rows = IR.screen(m)[2]
            bad = []
            for i, want in enumerate(expect):
                ok = row_has(rows, ix, iy, i, IR.render(want, tab))
                print("  %s slot %d: %-26r %s" % (name, i + 1, want,
                                                  "ok" if ok else "MISSING"))
                if not ok:
                    bad.append("disk %s slot %d does not read %r"
                               % (name, i + 1, want))
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
