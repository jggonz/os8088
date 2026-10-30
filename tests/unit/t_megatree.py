#!/usr/bin/env python3
"""A FROZEN SOAK'S TREE IS `build/` TO THE kern_mega HARNESS.

    python3 tests/unit/t_megatree.py

docs/plans/SOAK-PARALLEL.md 17.13. Under `tools/os88soak.py` every row reads
a frozen tree (`$OS88_TREE`, build/trees/plain-*), and `os88build.at` hands
it `build/trees/<tree>/os8088-360.img` for `build/os8088-360.img`. Three
places in the kern_mega harness asked "is this the shipped image?" as
`dirname(p) == build/` and so answered NO for exactly the image they were
written to recognise:

  * `os88megacommon.mega_images` left kern_BIG's system disk in A: and
    Bochs waited 240 s for a `kern_mega:` line that kernel never prints
    (mega-lzcomp, red at a lane width of ONE - it was never load);
  * `os88qmpmega._scratch` took the tree's `vesa.img` for a row's scratch,
    symlinked it into Bochs's run directory and copied kern_mega's A: back
    over it at close - so every kern_big QEMU row that booted the fixture
    afterwards booted kern_mega (vesa, vesa8, vesacp, vesafsx, vesablit4,
    vesagor: `vid_kind 134 ... DRVR_ERR 139`);
  * and every backend's `OS88_MEGA_BUILD` fallback read mega.img and
    megak/kernel.map out of the OPERATOR'S build/ while the row's own disks
    came out of the tree.

All three pass alone, where nothing is frozen. This asserts the answers with
`$OS88_TREE` set, and ratchets the fallback spelling out of tools/ and tests/
so a new backend cannot reintroduce it.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

fails = []


def check(ok, what):
    print("%s  %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        fails.append(what)


def main():
    tree = os.path.join(ROOT, "build", "trees", "t_megatree-fake")
    os.environ.pop("OS88_MEGA_BUILD", None)
    os.environ["OS88_TREE"] = tree
    import os88build
    import os88megacommon
    import os88qmpmega

    check(os.path.realpath(os88build.mega_build()) == os.path.realpath(tree),
          "mega_build() is the run's tree under $OS88_TREE")
    a, b = os88megacommon.mega_images(
        os88build.at("build/os8088-360.img"), os88build.at("build/apps360.img"))
    check(a == os.path.join(tree, "mega.img"),
          "the tree's os8088-360.img boots as the TREE's mega.img (got %s)"
          % os.path.relpath(a, ROOT))
    check(b == os.path.join(tree, "megaapps.img"),
          "the tree's apps360.img is the tree's megaapps.img (got %s)"
          % os.path.relpath(b, ROOT))
    a, _ = os88megacommon.mega_images(
        os.path.join(ROOT, "build", "os8088-360.img"), None)
    check(a == os.path.join(tree, "mega.img"),
          "build/'s own os8088-360.img is still a shipped image")
    a, _ = os88megacommon.mega_images(
        os.path.join(ROOT, "build", "tmp", "os8088-360.img"), None)
    check(a.endswith(os.path.join("tmp", "os8088-360.img")),
          "a scratch copy under build/tmp goes in as it is")
    img = os88build.at("build/os8088-360.img")
    os88qmpmega.MEGA = True
    try:                    # (the fake tree has no files: a twin attempt
        got = os88qmpmega.mega_twin(img)    # stats the image and raises)
    except OSError as e:
        got = e
    check(got == img,
          "mega_twin leaves the tree's shipped system disk to mega_images")
    for p in (os88build.at("build/vesa.img"), os88build.at("build/apps.img"),
              os.path.join(ROOT, "build", "vesa.img"),
              os.path.join(ROOT, "build", "trees", "knob-0", "vesa.img")):
        check(not os88qmpmega._scratch(p),
              "%s is shared: a guest may not write it back"
              % os.path.relpath(p, ROOT))
    check(os88qmpmega._scratch(os.path.join(ROOT, "build", "tmp", "x.img")),
          "build/tmp/x.img is a row's scratch")

    # THE RATCHET: the fallback that read build/ under a frozen run
    pat = re.compile(r'OS88_MEGA_BUILD"\)\s*or\s*os\.path\.join\(ROOT,\s*"build"\)')
    for top in ("tools", "tests"):
        for dp, dn, fn in os.walk(os.path.join(ROOT, top)):
            dn[:] = [d for d in dn if d not in ("__pycache__", "martypc")]
            for f in fn:
                if not f.endswith(".py"):
                    continue
                p = os.path.join(dp, f)
                with open(p, encoding="utf-8", errors="replace") as fh:
                    if pat.search(fh.read()):
                        check(False, "%s: OS88_MEGA_BUILD falls back to "
                              "build/ - use os88build.mega_build()"
                              % os.path.relpath(p, ROOT))
    print("t_megatree: %s" % ("FAIL" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
