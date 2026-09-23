#!/usr/bin/env python3
"""Where PXSTEIN.O88 rides, and how big it is (SPEC.md 97.9, 97.10).

    make && python3 tests/pxsdisk.py

Four facts read out of the BUILT FLOPPIES with tests/unit/t_image.py's FAT12
walker - never out of the Makefile's variables, which is the trap SPEC.md
24.5 records (a `filter-out` matching nothing is silent):

  * it is on games360.img (the games category disk, at the volume's root -
    SPEC.md 24.6) and on apps.img (in GAMES/);
  * it is NOT on apps360.img (24.6.1's dated decision), nor on the small
    disks' games (24.5's omission, with 97.9's ground - ASSERTED, since the
    registry's wants= builds build/smallapps360.img for this row), nor on
    combo.img (COMBO_DROP: the 360KB field disk, wants= as well); and
    apps-all.img, which needs the C toolchain, is checked when it exists;
  * the packed file is <= 56KB, the ceiling the Makefile asserts where the
    file is made, read back here off the disk it landed on;
  * its parts run is under SPEC.md 20.12.7's 128 sectors, decoded out of
    the package's own part table with tools/os88parts.py - in UNPACKED
    sectors, which IS the bound (op_load cuts the claim from the unpacked
    total and refuses at 128 on both sides; OP_COMP does not relieve it):
    68 today where the packed file on the floppy carries ~28, so the figure
    is not the disk's and cannot false-pass. The recipe that makes the
    file asserts the same (`os88parts.py --run --max-run 128`); this row is
    the read-back off the built tree.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88parts                                                # noqa: E402
import os88pkg                                                  # noqa: E402
from t_image import Vol                                         # noqa: E402
from t_smallreq import names                                    # noqa: E402

FILE = "PXSTEIN.O88"
MAXZ = 57344
FAIL = []


def check(ok, what):
    print("   %-64s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def vol(path):
    return Vol(open(os.path.join(ROOT, path), "rb").read(), path)


def has(v):
    return [(f, n) for f, n in names(v) if n == FILE]


def main():
    os.chdir(ROOT)
    on = has(vol("build/games360.img"))
    check(on and on[0][0] in ("", "/"), "games360.img carries %s at the root (%s)"
          % (FILE, on))
    on = has(vol("build/apps.img"))
    check(on and "GAMES" in on[0][0].upper(), "apps.img carries %s in GAMES/ (%s)"
          % (FILE, on))
    check(not has(vol("build/apps360.img")), "apps360.img does NOT carry it (24.6.1)")
    # the on-demand disk the registry's wants= builds for this row; run by
    # hand, a missing one is a FAILURE and not a note - the omission it
    # asserts is exactly the "filter-out matching nothing is silent" shape
    small = "build/smallapps360.img"
    if os.path.exists(small):
        check(not has(vol(small)), "smallapps360.img does NOT carry it (24.5, 97.9)")
    else:
        check(False, "%s is not built (`make smallapps`; the registry's wants= does)"
              % small)
    # ...and the 360KB combo (COMBO_DROP), asserted when it exists: `make
    # combo` is NOT a wants= because it does not build on main as of 2237d1ba
    # - "packages need 446 clusters; disk holds 354", re-run on 2026-09-14
    # with this package already dropped - which is a decision for whoever
    # owns the field disk (the Makefile's COMBO_DROP paragraph says the same)
    combo = "build/combo.img"
    if os.path.exists(combo):
        check(not has(vol(combo)), "combo.img does NOT carry it (COMBO_DROP)")
    else:
        print("   (%s not built: `make combo` overflows 354 clusters on main "
              "already, this package dropped; COMBO_DROP names it)" % combo)
    allimg = "build/apps-all.img"
    if os.path.exists(allimg):
        on = has(vol(allimg))
        check(on and "GAMES" in on[0][0].upper(),
              "apps-all.img carries %s in GAMES/ (%s)" % (FILE, on))
    else:
        print("   (%s not built: `make allapps` needs the C toolchain; PLAN 4.4's "
              "arithmetic: 2,720 + 31 clusters of 2,847)" % allimg)
    # the file itself, and its parts run. THE FILE IS NOT THE IMAGE
    # (CLAUDE.md's PKGZ rule): the 56KB ceiling is about the FILE on the
    # floppy, so `raw` is what it reads; the parts table is decoded out of
    # the IMAGE, which is os88pkg.image_unwrap(raw) - a no-op today because
    # os88pkg.py declines to compress a parted container, and the right
    # bytes the day it stops declining, where blob[:image] would have been
    # LZ4 payload and "not a parts package" would have pointed at the
    # package instead of at the compression
    p = os.path.join(ROOT, "build", "pxstein.o88")
    raw = open(p, "rb").read()
    check(len(raw) <= MAXZ, "build/pxstein.o88 is %d bytes, <= %d" % (len(raw), MAXZ))
    blob = os88pkg.image_unwrap(raw)
    image = blob[8] | (blob[9] << 8)
    rows = os88parts.rows(blob[:image])
    run = os88parts.run_sectors(rows)       # the recipe's own reader
    print("   parts: %s" % ", ".join("%d:%s len %d flags %d" % (i, "SEG" if r["kind"] == 0
          else "ASSET", r["len"], r["flags"]) for i, r in enumerate(rows)))
    check(run < 128, "the eager run is %d UNPACKED sectors, under 128 (20.12.7)" % run)
    if FAIL:
        print("pxsdisk: FAIL (%d)" % len(FAIL))
        return 1
    print("pxsdisk: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
