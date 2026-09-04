#!/usr/bin/env python3
"""Every package and driver, validated again - and the images proved fresh.

    python3 tests/unit/t_pkg.py

`tools/os88pkg.py` and `tools/os88drv.py` validate a package as they stamp it,
and they are thorough.  What neither can answer is a question about the
FLOPPY, because by then their job is over:

  1. IS THE FILE ON THE DISK THE ONE THIS TREE JUST BUILT?  CLAUDE.md is
     blunt about why that matters - *"a stale image is indistinguishable from
     a change that did nothing"* - and it has really happened here: the repo
     used to force-add 41 built artifacts, `make check-images` existed
     precisely to catch them going stale, and it caught two "Rebuild the
     shipped images" commits and a merge that shipped a Paint two fixes out
     of date.  That check went away with the committed binaries.  This is the
     half worth keeping: every file on every image must be byte-identical to
     the artifact in `build/` it was made from.

  2. IS THE HEADER STILL A HEADER once it is on the volume?  A package is
     read off the disk and FAR-CALLED at `PKG_DISP` (+12) with no further
     checking beyond the loader's own, so a wrong byte there sends the kernel
     into the package's data on the first paint (SPEC.md 20.2).  The
     validation is re-done here from the SPEC rather than by importing
     os88pkg, for the reason t_image.py gives at length: a writer and a
     reader that share code agree with each other about the same wrong thing.

  3. DOES THE VERSION BYTE STILL SEPARATE THE TWO SPECIES?  An application is
     v3 and a driver is v4, and that is load-bearing rather than cosmetic
     (SPEC.md 51): `ld_check_hdr` refuses a v4, which is the second of the two
     independent gates stopping a `.DRV` being double-clicked into the
     application loader - the first being that the mount only types `*.O88`.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, eq, done                       # noqa: E402
from t_image import Vol, read, SYSTEM_IMAGES, DATA_IMAGES  # noqa: E402

MAGIC = 0x384F                       # 'O','8'
V_APP, V_DRV, V_MOD = 3, 4, 5        # package / driver / on-demand module
DISPATCH = bytes((0xFF, 0xD5, 0xCB))  # call bp / retf, at +12
HEADER = 32
ICON_END = 96
APP_MAX_SIZE = 0xF000                # kernel/kernel.asm, apps/os88api.inc
DRV_MAX = 40 * 1024                  # DRV_MAX_KB in kernel/driver.inc
# tools/os88drv.py's own list. 0x40 is NOT a kernel driver class: it is a
# driver's own loadable half (OS88_OVERLAY, SPEC.md 52.11), which the kernel
# never loads - its OWNER does - and which is stamped here because the header,
# the dispatcher and the one-claim load discipline are identical.
DRV_CLASSES = {1: "sound", 2: "disk", 3: "debug", 4: "net", 5: "file",
               0x40: "overlay"}
MAP_MAGIC = b"O8MM"
MOD_H_IMG, MOD_H_NENT = 8, 10


def _mod_nent():
    """MOD_NENT, READ OUT OF kernel/mod.inc rather than copied - which is the
    lesson tools/os88mod.py records against itself after a bare 4 here failed
    a build with a message naming a constant this side did not have."""
    import re as _re
    src = open(os.path.join(ROOT, "kernel/mod.inc"), errors="replace").read()
    m = _re.search(r"^MOD_NENT\s+equ\s+(\d+)", src, _re.M)
    return int(m.group(1)) if m else 8


MOD_NENT = _mod_nent()


def app(blob, nm, flags, entry, image, bss):
    """SPEC.md 20.2 - a v3 application package."""
    check(not (flags & 0xF8), "%s: no reserved flag bits" % nm, got=hex(flags),
          why="bit 0 is an embedded icon, bit 1 an association block (SPEC.md "
              "54.6) and bit 2 says the FILE is longer than the image on "
              "purpose (SPEC.md 20.12). Bits 3-7 are nobody's yet")
    lo = ICON_END if flags & 1 else HEADER
    check(lo <= entry < image, "%s: entry +0x%04X is inside the image" % (nm, entry),
          got=hex(entry), want="0x%04X..0x%04X" % (lo, image))
    check(image + bss <= APP_MAX_SIZE, "%s: image + bss fits APP_MAX_SIZE" % nm,
          "image and bss share one 64KB segment (SPEC.md 33)",
          got=image + bss, want="<= %d" % APP_MAX_SIZE)


def driver(blob, nm, cls, entry, image):
    """SPEC.md 51 - a v4 driver. Offset +3 is the CLASS, not flags, and its
    bss ships INSIDE the image, which is what lets drv_load make exactly one
    claim at the size the directory entry already reported."""
    check(cls in DRV_CLASSES, "%s: class %d is one the kernel knows" % (nm, cls),
          got=cls, want=sorted(DRV_CLASSES))
    check(HEADER <= entry < image, "%s: entry +0x%04X is inside the image" % (nm, entry),
          got=hex(entry), want="0x%04X..0x%04X" % (HEADER, image))
    check(image <= DRV_MAX, "%s: fits the kernel's driver claim" % nm,
          got=image, want="<= %d" % DRV_MAX)


def module(blob, nm):
    """SPEC.md 2.8 - an on-demand module: code that ships BESIDE a package.

    The `.drv` on the floppy is the module image ALREADY EXTRACTED from the
    kernel, so it carries the v5 header and no `O8MM` trailer - that map
    lives in `build/kernel.bin`, which is what `tools/os88mod.py` reads to cut
    these out. Looking for a trailer here was this file's second wrong
    assumption about a format it had not read.
    """
    ver, ident, img, nent = blob[2], blob[3], struct.unpack_from("<H", blob, MOD_H_IMG)[0], blob[MOD_H_NENT]
    check(1 <= nent <= MOD_NENT, "%s: declares 1..%d entries" % (nm, MOD_NENT),
          "mod_check refuses anything else at run time, and a module the kernel "
          "refuses is a Control Panel page that does not open", got=nent)
    check(img <= len(blob), "%s: header image size is inside the file" % nm,
          got=img, want="<= %d" % len(blob))


def header(blob, nm):
    """The 32-byte header, checked from SPEC.md 20.2/51/2.8. Returns the version.

    THREE SPECIES SHARE THE MAGIC and they are not the same record: offset +3
    is `flags` in an application, the CLASS in a driver, and part of a module
    id - so a single struct read that "works" for all of them is reading
    different fields and calling them one name. That was this file's first
    version, and it reported nine failures on a perfectly good tree.
    """
    if not check(len(blob) >= HEADER, "%s: shorter than a header" % nm, got=len(blob)):
        return None
    magic, ver, b3, link, entry, image, b6 = struct.unpack_from("<HBBHHHH", blob, 0)
    eq(magic, MAGIC, "%s: magic is 'O8'" % nm)
    if not check(ver in (V_APP, V_DRV, V_MOD),
                 "%s: version is 3 (app), 4 (driver) or 5 (module)" % nm, got=ver):
        return None
    if ver == V_MOD:
        module(blob, nm)
        return ver
    eq(link, 0, "%s: links at org 0" % nm,
       "a v3/v4 package owns a segment and is loaded on a paragraph boundary "
       "(SPEC.md 20.1) - a non-zero link base is a v2 image")
    eq(blob[12:15], DISPATCH, "%s: carries the dispatcher at +12" % nm,
       "the kernel far-calls +12 to reach every callback (SPEC.md 20.2); a "
       "package without it sends the kernel into its own data on the first paint")
    if ver == V_APP and b3 & 4:
        # FLAGS BIT 2: the file is longer than the image ON PURPOSE, and the
        # tail is the package's own to read (SPEC.md 20.12). The truncation
        # guard becomes image <= file, which is still exact about the half
        # the kernel loads - and it is the ONLY thing the kernel learns about
        # parts, so this is where that has to be said.
        check(32 <= image <= len(blob),
              "%s: image %d is inside its %d-byte file" % (nm, image, len(blob)),
              got=image, want="32..%d" % len(blob),
              why="flags bit 2 lifts `image == file size`, not the bound - a "
                  "package whose image runs past its own file is a truncated "
                  "copy however the flag reads")
    else:
        eq(image, len(blob), "%s: image size field matches the file" % nm)
    if ver == V_APP:
        app(blob, nm, b3, entry, image, b6)
    else:
        driver(blob, nm, b3, entry, image)
    return ver


def main():
    build = os.path.join(ROOT, "build")
    arts, apps, drvs, mods = {}, 0, 0, 0
    for f in sorted(os.listdir(build)):
        p = os.path.join(build, f)
        if not os.path.isfile(p) or not f.endswith((".o88", ".drv")):
            continue
        blob = read(p)
        arts[f.upper()] = blob
        ver = header(blob, f)
        if ver == V_APP:
            apps += 1
            eq(f.endswith(".o88"), True, "%s: a v3 package is a .o88" % f)
        elif ver == V_DRV:
            drvs += 1
            eq(f.endswith(".drv"), True, "%s: a v4 driver is a .drv" % f,
               "the mount only types *.O88 into the loader and ld_check_hdr "
               "refuses a v4 - two independent gates, and this is the file half")
        elif ver == V_MOD:
            mods += 1

    # Everything else in build/ that an image can carry, so the freshness
    # check below covers the kernel, the fonts, the logo and README.TXT too.
    for f in sorted(os.listdir(build)):
        p = os.path.join(build, f)
        if os.path.isfile(p) and not f.endswith((".o88", ".drv")):
            arts.setdefault(f.upper(), read(p))

    # ...and every file on every image must BE one of them.
    compared = 0
    for img in SYSTEM_IMAGES + DATA_IMAGES:
        p = os.path.join(build, img)
        if not os.path.exists(p):
            continue
        v = Vol(read(p), img)
        for path, name11, attr, clus, size in v.walk():
            if attr & 0x10:                        # a directory
                continue
            stem, ext = name11[:8].strip().decode(), name11[8:].strip().decode()
            fname = ("%s.%s" % (stem, ext)) if ext else stem
            if fname.upper() not in arts:
                continue                            # generated on the volume
            want = arts[fname.upper()]
            chain, _ = v.chain(clus) if clus else ([], 0)
            got = b"".join(v.blob[v.cluster_lba(c) * v.byts:
                                  v.cluster_lba(c) * v.byts + v.spc * v.byts]
                           for c in chain)[:size]
            eq(got, want, "%s: %s is the artifact this tree built" % (img, fname),
               "a stale image is indistinguishable from a change that did "
               "nothing - it boots, it looks right, and it is the previous build")
            compared += 1

    print("t_pkg: %d packages, %d drivers, %d modules, %d files compared against build/"
          % (apps, drvs, mods, compared))
    done("t_pkg")


if __name__ == "__main__":
    main()
