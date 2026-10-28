#!/usr/bin/env python3
"""No shipped disk carries an .OVL beside a package (SPEC.md 19.11).

    python3 tests/unit/t_noovl.py [IMAGE ...]

A program is ONE FILE. Anything a package needs that is not its image - code
it far-calls, data it reads - is a PART of the .o88 (SPEC.md 20.12), so a user
browsing APPS sees programs and nothing else, and a copy of the package is a
copy of the program. SHEET's CHART.OVL and MACRO.OVL came back onto the apps
disks after the round that took every other overlay inside its package; they
are parts of SHEET.O88 now (SPEC.md 82.16.12), and this row is what keeps the
next one from arriving the same way.

Two halves, both host-side and both well under a second:

  1. THE DISKS. Every FAT12 floppy image `all` builds is walked by this file's
     own reader, every folder of it, and a folder holding an .OVL beside an
     .O88 fails, naming both. The shipped images are REQUIRED; any other
     floppy found under build/ (an on-demand application disk, the small,
     emu and mega kernels' disks, the everything set) is checked when it has
     been built, except the test fixtures in EXEMPT, which exist to test the
     C SDK's sidecar mode and ship nowhere.

  2. THE RECIPES, because most on-demand disks are not built by `all` and
     half 1 cannot see them. In the Makefile an .OVL may be only a CUT (os88ovl.py's
     `-o`), a PART (`--part`), the askable-by-name rule for a cut, or a C
     package's overlay declared `part` (CC_PACKAGE's fifth argument). Any
     other mention - a disk list, an image's prerequisite, CC_OVLZ_RULE, the
     packed copy a disk would carry - is an overlay on its way to a floppy.
"""
import glob
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

# The images `all` builds - $(SHIPIMGS) in the Makefile: system and apps in
# four geometries, the six 360KB-only disks (SPEC.md 24.4, 24.6) and the
# second 720KB apps disk (24.7), and the 128KB machine's two (24.5.7).
SHIPPED = ["build/os8088.img", "build/os8088-120.img", "build/os8088-720.img",
           "build/os8088-360.img", "build/apps.img", "build/apps120.img",
           "build/apps720.img", "build/apps360.img", "build/media360.img",
           "build/office360.img", "build/network360.img",
           "build/games360.img", "build/emu360.img", "build/util360.img",
           "build/more720.img", "build/small360.img", "build/small720.img"]

# Built disks that may carry an .OVL beside a package ON PURPOSE.
EXEMPT = {
    "cfsx.img": "tests/cfsx: the C SDK's sidecar-overlay mode's own fixture",
    "cfsx360.img": "tests/cfsx: the C SDK's sidecar-overlay mode's own fixture",
}

# The Makefile lines an .OVL may appear on (half 2), as regexes over a line
# with its comment stripped. Anything else naming one fails.
OVL_ALLOWED = [
    re.compile(r"os88ovl\.py\b.*-o\s+\S+\.OVL\b"),     # the cut itself
    re.compile(r"--part\s+\S+\.OVL\b"),                # appended as a part
    re.compile(r"^\S+\.OVL\s*:.*;\s*$"),               # askable by name
    re.compile(r"call CC_PACKAGE,[^,]*,[^,]*,\w+\.OVL,.*,part\)\)$"),  # C part
]
OVL_RECIPE_EXEMPT = re.compile(r"cfsx", re.I)          # EXEMPT's fixture

# What to DO about a failure - printed with every one, so the reader is not
# left to reverse-engineer the rule from the check's name.
HOWTO = """
    WHAT TO DO: put that code INSIDE your .o88, as a PART. A package never
    ships an .OVL beside itself - a user in the folder sees it as a program,
    and copying the .o88 leaves it behind.

    1. Read the parts documentation first:
         SPEC.md 20.12          the parts standard (what a part is, the rules)
         SPEC.md 20.12.4        lazy parts: op_fetch / op_drop / op_lazyok
         apps/os88parts.inc     the header: what you write, the three rules
         SPEC.md 19.11          why there is no .OVL on a disk
    2. In your package: OS88_F_PARTS in OS88_HEADER, %include "os88parts.inc",
       an OS88_PARTS_BEGIN / OS88_PART OP_SEG, OP_LAZY | OP_COMP / ..._END
       table, `call op_load` FIRST in the entry proc, and op_fetch + op_seg
       where you used to read the .OVL (op_movable if its claim may move).
    3. In the Makefile: keep the os88ovl.py cut but name it *.bin, not *.OVL,
       hand it to os88pkg.py with --part, and take the .OVL out of every
       disk list.
    Worked examples: SHEET (apps/sheet/sheet.asm, apps/os88chartovl.inc,
    SPEC.md 82.16.12) and PiXEL (apps/pixel/pixel.asm) for far-called code
    modules; a C package passes `part` to CC_PACKAGE (SPEC.md 73.14.2)."""


def walk(img, ents, path, out):
    """Every file of a FAT12 volume as (folder, NAME.EXT)."""
    bps, spc, res, nfat, nroot = img["geo"]
    data0, csz = img["data0"], spc * bps
    b = img["bytes"]
    for i in range(0, len(ents), 32):
        e = ents[i:i + 32]
        if len(e) < 32 or e[0] == 0:
            break
        if e[0] == 0xE5 or e[11] == 0x0F or e[11] & 8:
            continue
        name = e[:8].decode("latin1").rstrip()
        ext = e[8:11].decode("latin1").rstrip()
        if name in (".", ".."):
            continue
        full = name + ("." + ext if ext else "")
        if e[11] & 0x10:
            c, data, seen = struct.unpack_from("<H", e, 26)[0], b"", set()
            while 2 <= c < 0xFF8 and c not in seen:
                seen.add(c)
                off = data0 + (c - 2) * csz
                data += b[off:off + csz]
                o = res * bps + c + c // 2
                v = struct.unpack_from("<H", b, o)[0]
                c = (v >> 4) if c & 1 else (v & 0xFFF)
            walk(img, data, path + full + "/", out)
        else:
            out.append((path, full.upper()))


def files_of(path):
    b = open(path, "rb").read()
    bps, spc, res, nfat, nroot, tot16, _, spf = struct.unpack_from(
        "<HBHBHHBH", b, 11)
    if not bps or not spc or not spf:
        return None                                    # not a FAT floppy
    tot = tot16 or struct.unpack_from("<I", b, 32)[0]
    nclus = (tot - res - nfat * spf - (nroot * 32 + bps - 1) // bps) // spc
    if nclus >= 4085:
        return None                                    # FAT16: a hard disk
    root = (res + nfat * spf) * bps
    img = {"bytes": b, "geo": (bps, spc, res, nfat, nroot),
           "data0": root + nroot * 32}
    out = []
    walk(img, b[root:root + nroot * 32], "/", out)
    return out


def disks(argv):
    if argv:
        return argv, set(argv)
    found = sorted(set(glob.glob(os.path.join(ROOT, "build", "*.img")) +
                       glob.glob(os.path.join(ROOT, "build", "*", "*.img"))))
    found = [os.path.relpath(p, ROOT) for p in found]
    return SHIPPED + [p for p in found if p not in SHIPPED], set(SHIPPED)


def check_disks(argv):
    images, required = disks(argv)
    walked = 0
    for img in images:
        p = img if os.path.isabs(img) else os.path.join(ROOT, img)
        if not os.path.exists(p):
            check(img not in required, "%s exists" % img, "run `make` first")
            continue
        if os.path.basename(img) in EXEMPT and img not in required:
            continue
        files = files_of(p)
        if files is None:
            continue
        walked += 1
        by = {}
        for folder, name in files:
            by.setdefault(folder, []).append(name)
        for folder, names in sorted(by.items()):
            ovls = [n for n in names if n.endswith(".OVL")]
            pkgs = [n for n in names if n.endswith(".O88")]
            check(not (ovls and pkgs),
                  "%s %s has no .OVL beside a package" % (img, folder),
                  "%s sits beside %s on a shipped disk.%s"
                  % (", ".join(ovls), ", ".join(pkgs), HOWTO),
                  got=", ".join(ovls), want="none")
    print("t_noovl: %d floppy image(s) walked" % walked)


def check_recipes():
    bad = []
    for rel in ("Makefile", "apps/cc/Makefile.inc"):
        for no, line in enumerate(open(os.path.join(ROOT, rel)), 1):
            code = line.split("#", 1)[0].rstrip()
            if ".OVL" not in code or OVL_RECIPE_EXEMPT.search(code):
                continue
            if any(r.search(code.strip()) for r in OVL_ALLOWED):
                continue
            bad.append("%s:%d: %s" % (rel, no, code.strip()))
        for no, line in enumerate(open(os.path.join(ROOT, rel)), 1):
            code = line.split("#", 1)[0]
            if re.search(r"\$\(eval \$\(call CC_OVLZ_RULE,", code):
                bad.append("%s:%d: %s" % (rel, no, code.strip()))
    bad = list(dict.fromkeys(bad))
    check(not bad, "no recipe puts an .OVL on a disk",
          "an .OVL may be a cut, a --part, its askable-by-name rule or a C "
          "package's overlay declared `part`; each line below is an overlay "
          "on its way to a floppy beside its package (SPEC.md 19.11):\n    "
          + "\n    ".join(bad) + "\n" + HOWTO, got=len(bad), want=0)


def main():
    check_disks(sys.argv[1:])
    if not sys.argv[1:]:
        check_recipes()
    done("t_noovl")


if __name__ == "__main__":
    main()
