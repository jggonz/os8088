#!/usr/bin/env python3
"""The typefaces are in SYSTEM/FONTS on every shipped system image, and
nowhere else (SPEC.md 19.8, 19.8.1).

    python3 tests/unit/t_fonts.py

THE PATH IS A CONTRACT BETWEEN TWO FILES THAT NEVER SEE EACH OTHER. The
Makefile writes the faces to a folder; `apps/os88type.inc`'s `ty_gofonts`
walks to one. Nothing links them, nothing checks them against each other, and
when they disagree the failure is SILENT AND TOTAL: `ty_scan` answers CF=1,
every Font menu on the machine is the kernel's own 8x8 cell and one item long,
and no code path anywhere says why. That is exactly the shape of failure a
person stops seeing - a Font menu with one entry looks like a Font menu.

So this row reads both ends. The DISK end is the walk below; the WALKER end is
the `SYSTEM` and `FONTS` string literals in `apps/os88type.inc`, checked
against the components of the path the Makefile actually wrote. A rename on
either side fails here, at `make` time, rather than on the glass.

IT IS DELIBERATELY NOT A BYTE CHECK. `t_pkg` already compares every file on
every image against the `build/` artefact of the same name - and it does it BY
NAME, which is precisely why it cannot see this: move the folder and every one
of its rows still matches. What is asserted here is location, and only that.

WHY THE ROOT MATTERS TOO. The move was made for the root's sake (SPEC.md
19.8.1) - the boot floppy's own window is what a person opens, and a folder of
files nobody double-clicks had no business being one of the four things in it.
A build that puts the faces back on the root is a build that still WORKS if
the walker moved with it, so "not in the root" is its own assertion rather
than a corollary of the one above.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from harness import check, eq, done                        # noqa: E402
import t_image                                             # noqa: E402

# Every shipped SYSTEM image. The apps disks carry no faces at all - a face is
# the machine's and not an application's (SPEC.md 19.8) - so they are not here.
IMAGES = ["os8088.img", "os8088-720.img", "os8088-120.img", "os8088-360.img"]

FONTS_AT = "SYSTEM/FONTS/"
LICENCE = "LICENSE TXT"          # SPEC.md 6.4.1: it rides WITH the faces


def faces_in_tree():
    """The families `make` was going to write, read off faces/ - the same way
    the Makefile's $(FACESRC) reads it, so a new face needs no edit here."""
    return sorted(p.stem.upper() for p in (ROOT / "faces").glob("*.t88"))


def name11(path, name):
    return path + name[:8].strip() + ("." + name[8:].strip()
                                      if name[8:].strip() else "")


want = faces_in_tree()
check(len(want) >= 2, "faces/ holds at least two .t88 sources",
      "with none, every check below is vacuously true and this row would go "
      "green on a tree with no typefaces in it at all", got=want)

for img in IMAGES:
    p = ROOT / "build" / img
    if not p.exists():
        check(False, "%s exists" % img, "`make` first - this row reads what "
              "the build just wrote, and a missing image is not a pass")
        continue
    v = t_image.Vol(p.read_bytes(), img)
    rows = [(path, n.decode("ascii", "replace"), attr)
            for path, n, attr, _c, _s in v.walk()]

    got = sorted(n[:8].strip() for path, n, _a in rows
                 if path == FONTS_AT and n[8:].strip() == "F88")
    eq(got, want, "%s: SYSTEM/FONTS holds every face in faces/" % img,
       "the Makefile's $(FACESARG) and faces/ have gone out of step, or the "
       "folder moved - either way a Font menu on this disk is short")

    check(any(path == FONTS_AT and n == LICENCE for path, n, _a in rows),
          "%s: SYSTEM/FONTS/LICENSE.TXT rides with the faces" % img,
          "SPEC.md 6.4.1 - the SIL OFL asks that its notice travel with the "
          "derivative, so it travels on the disk and not only in the tree")

    stray = sorted(name11(path, n) for path, n, _a in rows
                   if n[8:].strip() == "F88" and path != FONTS_AT)
    eq(stray, [], "%s: no .F88 outside SYSTEM/FONTS" % img,
       "ty_gofonts walks to exactly one folder, so a face anywhere else is "
       "bytes on the disk that nothing can ever open")

    # ...AND THEY ARE PACKED WHENEVER THE DRIVERS ARE (SPEC.md 6.4.1,
    # 20.13.5): a 'CZ' face expands into ty_open's claim through the
    # transparent read, and the licence into Note Pad's. `make PKGZ=` packs
    # nothing, which is why the test is against the image's own drivers and
    # not against a constant - a plain face beside packed drivers is the
    # Makefile's $(BUILD)/faces/ rule falling back.
    def head(clus, size):
        if size == 0 or clus < 2:
            return b""
        ch, _ = v.chain(clus)
        at = v.cluster_lba(ch[0]) * v.byts
        return bytes(v.blob[at:at + 8])
    files = [(path, n, clus, size) for path, n, attr, clus, size in v.walk()
             if not attr & t_image.A_DIR]
    packed = [n for path, n, c, sz in files
              if path == "" and n[8:] == b"DRV" and head(c, sz)[:2] == b"CZ"]
    if packed:
        for path, n, c, sz in files:
            if path == FONTS_AT and (n[8:].strip() == b"F88"
                                     or n == LICENCE.encode()):
                eq(head(c, sz)[:2], b"CZ",
                   "%s: %s is packed like the drivers" % (img, name11(path, n.decode())),
                   "%d drivers on this image are 'CZ' files and this is not: "
                   "the Makefile's $(BUILD)/faces/ or $(FACELIC) rule fell "
                   "back to the plain file" % len(packed))

    root_folders = sorted(n[:8].strip() for path, n, attr in rows
                          if path == "" and attr & t_image.A_DIR)
    check("FONTS" not in root_folders,
          "%s: the root has no FONTS folder" % img,
          "SPEC.md 19.8.1 moved it INTO SYSTEM/ for the root's sake - the "
          "boot floppy's own window is what a person opens", got=root_folders)

# --- and the other end of the contract ---------------------------------------
# ty_gofonts descends by NAME, one ty_dive per component, and the names are
# string literals in the library. Nothing else in the tree spells them.
lib = (ROOT / "apps" / "os88type.inc").read_text()
for label, want_name in (("ty_s_system", "SYSTEM"), ("ty_s_fonts", "FONTS")):
    m = re.search(r"^%s:\s*db\s*'([^']*)'\s*,\s*0\s*$" % label, lib, re.M)
    check(m is not None and m.group(1) == want_name,
          "apps/os88type.inc: %s is '%s'" % (label, want_name),
          "the walker's own spelling of one component of %s. It is compared "
          "with the path the images above were built at, because the two ends "
          "of this contract are in different files and nothing links them"
          % FONTS_AT.rstrip("/"),
          got=(m.group(1) if m else None), want=want_name)

check(FONTS_AT.rstrip("/").split("/") == ["SYSTEM", "FONTS"],
      "the path this row checks is the one ty_gofonts walks",
      "two ty_dive calls, in this order - if a third component is ever added, "
      "the loop above and ty_gofonts both need it and this line is the tell")

done("t_fonts")
