#!/usr/bin/env python3
"""The category disks' LAYOUT, which is the only thing about them nobody else
checks.

    python3 tests/unit/t_catdisk.py

SPEC.md 24.6's three disks are already read three times over: `t_image` walks
their FAT12 and their BPB, `t_diskverify` runs the tree's own fsck on them,
and `t_pkg` proves every file on them identical to the artifact it was built
from.  All three pass on a disk whose contents are WRONG, because none of
them is about contents - and what makes a category disk a category disk is
entirely its layout:

  * THE PACKAGES ARE AT THE ROOT.  The whole proposition is that choosing
    the disk has already said what you came for, so there is no APPS/ to
    click into.  A `$(addprefix APPS:,...)` copied from the apps disk's
    argument list is a one-word edit that no other gate can see.
  * MEDIA/ AND SYSTEM/APPDATA/ EXIST, empty or not.  They are passed with
    `--folder` precisely because nothing may ship into them (SPEC.md 19.9,
    38.10), so they are the two things a build can silently stop making:
    drop `$(APPDATAFOLDER)` and every other row still goes green.
  * THE ASSOC.DAT IS THERE AND IS WARM.  It costs nothing and is easy to
    lose - `os88disk.py` writes one only for a volume it was handed
    PACKAGES for, so a disk whose packages moved into a folder still gets
    one and a disk built from data alone gets none at all.
  * THERE IS NO WORD.OVL.  Word's second segment is a PART inside
    WORD.O88 now (SPEC.md 68.10), where it was a file that had to ride
    beside the package - so a WORD.OVL on a disk is a stale build artefact
    carried by a recipe nobody updated, and the next person to read that
    disk's file list would reasonably conclude Word still needs it.

WHAT THIS DELIBERATELY DOES NOT CHECK is which packages are on which disk.
SPEC.md 24.6.1 is explicit that the membership list is a decision with a
date on it, remade every time the geometry runs out; a gate that pinned it
would fail on purpose every time the owner re-curated, which is the shape of
a test people learn to edit rather than read.  So the assertions here are
about the disks' SHAPE, plus one membership fact that is not a preference
(no WORD.OVL, which nothing reads any more).

FAST, on t_image's argument: it reads the shipped images that every build
already produces, costs milliseconds, and needs no emulator.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402
from t_image import Vol, read                             # noqa: E402

# os88disk.py's own header, mirrored the way t_image mirrors the BPB: read
# from the format rather than imported, so a change to the writer that
# breaks the reader shows up here instead of agreeing with itself.
ASC_MAGIC = b"OS88AC"
ASC_HDR, ASC_ROW = 16, 88          # a VERSION 2 row (SPEC.md 54.3.2)

DISKS = ["office360.img", "network360.img", "games360.img"]

# Passed with --folder on all three, so they exist whatever ships into them.
FOLDERS = ["MEDIA", "SYSTEM", "SYSTEM/APPDATA"]


def tree_of(v):
    """{path: (attr, first cluster, size)} for the whole volume.

    Vol.walk is t_image's own FAT12 walker, written from the format rather
    than imported from os88disk.py - which is the property that makes this
    file worth anything (t_image's docstring carries the argument). Reusing
    it here keeps ONE hand-written reader in the tree instead of two that
    can disagree.
    """
    out = {}
    for path, name11, attr, clus, size in v.walk():
        stem = name11[:8].decode("ascii", "replace").strip()
        ext = name11[8:11].decode("ascii", "replace").strip()
        out[path + stem + ("." + ext if ext else "")] = (attr, clus, size)
    return out


def contents(v, clus):
    """A file's bytes, by its first cluster."""
    chain, _ = v.chain(clus)
    blob = b""
    for c in chain:
        off = v.cluster_lba(c) * v.byts
        blob += v.blob[off:off + v.spc * v.byts]
    return blob


def main():
    seen = 0
    for img in DISKS:
        p = os.path.join(ROOT, "build", img)
        if not check(os.path.exists(p), "%s exists" % img, "run `make` first"):
            continue
        seen += 1
        v = Vol(read(p), img)
        tree = tree_of(v)

        # 1. every package at the ROOT, and none of them in a folder.
        root_pkgs = [n for n in tree if n.endswith(".O88") and "/" not in n]
        buried = [n for n in tree if n.endswith(".O88") and "/" in n]
        check(root_pkgs, "%s: packages are at the root" % img,
              "SPEC.md 24.6: choosing a category disk has already said what "
              "you came for, so there is no folder to click into", got=root_pkgs)
        check(not buried, "%s: no package is in a folder" % img,
              "an APPS:/GAMES: prefix copied from the apps disk's argument "
              "list puts them back, and nothing else notices", got=buried)

        # 2. the two folders that exist because --folder made them.
        for f in FOLDERS:
            got = tree.get(f)
            check(got is not None and (got[0] & 0x10),
                  "%s: %s/ exists" % (img, f),
                  "SPEC.md 19.9 and 38.10: a Save defaults into MEDIA/ and an "
                  "app's own state goes in SYSTEM/APPDATA/, so an application "
                  "that had to create either would need a 'disk is full' path "
                  "nobody tests", got=got)

        # 3. a warm ASSOC.DAT with rows in it (SPEC.md 54.7).
        if check("ASSOC.DAT" in tree, "%s: has an ASSOC.DAT" % img,
                 "os88disk.py writes one for any volume it is handed packages "
                 "for, so its ABSENCE means the packages stopped being passed "
                 "as packages"):
            blob = contents(v, tree["ASSOC.DAT"][1])
            check(blob[:6] == ASC_MAGIC, "%s: ASSOC.DAT is one" % img,
                  "the header is read from SPEC.md 54.7's format here rather "
                  "than imported from the writer", got=blob[:6], want=ASC_MAGIC)
            check(len(blob) > 6 and blob[6] == 2,
                  "%s: ASSOC.DAT is version 2" % img,
                  "SPEC.md 54.3.2: the rows carry the glyph column, and the "
                  "stride below is read against that", got=blob[6:7])
            napp = blob[7]
            check(napp == len(root_pkgs),
                  "%s: ASSOC.DAT caches all %d packages" % (img, len(root_pkgs)),
                  "a row per package is what makes the folder list without a "
                  "header read each, and what seeds the machine's icons and "
                  "extension hints off THIS volume (SPEC.md 54.7.1)",
                  got=napp, want=len(root_pkgs))
            # ...and every row's folder word is 0, which is what "at the root"
            # means on the wire: the FAT convention the open path reads.
            roots = [struct.unpack("<H", blob[ASC_HDR + i * ASC_ROW + 10:
                                              ASC_HDR + i * ASC_ROW + 12])[0]
                     for i in range(napp)]
            check(all(r == 0 for r in roots),
                  "%s: every cached row names the root" % img,
                  "SPEC.md 54.7.1 patches each row with the directory its "
                  "program lives in, and 0 is the root - a non-zero one here "
                  "is a package the cache will look for in a folder that is "
                  "not where it is", got=roots)

        # 4. no WORD.OVL: Word's second segment is a part of WORD.O88.
        check("WORD.OVL" not in tree, "%s: no WORD.OVL" % img,
              "SPEC.md 68.10 made Word's second segment a PART of WORD.O88 - "
              "nothing reads a WORD.OVL, so this one is a recipe that was "
              "not updated")

    check(seen == len(DISKS), "every category disk was read", got=seen)
    print("t_catdisk: %d disks" % seen)
    done("t_catdisk")


if __name__ == "__main__":
    main()
