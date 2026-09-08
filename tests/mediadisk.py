#!/usr/bin/env python3
"""The 360KB MEDIA DISK mounts, and the apps disk keeps MEDIA (SPEC.md 24.4).

    make && python3 tests/mediadisk.py [machine]

BEVERLY.MOD is 114 of a 360KB disk's 354 clusters, so at that geometry alone
it comes off the apps disk and rides `build/media360.img`. Two things have to
be true afterwards and neither is visible in the Makefile:

  1. `media360.img` is a volume os8088 MOUNTS. It carries no package at all,
     so os88disk.py writes it no ASSOC.DAT (SPEC.md 54.7 - a cache of nothing
     is nothing), and it is the first shipped disk of that shape.
  2. `MEDIA` is STILL ON the 360KB apps disk. It is where a save DEFAULTS to
     (SPEC.md 38.10), so it has to exist on the volume the user is working on
     whether or not anything shipped is left in it - and a folder that exists
     only because a data file happened to name it is one deletion from gone.

The listing is read out of the GUEST - the acting Disk window's own view
cache - so what is asserted is what the kernel made of the volume, not what
the host tool believes it wrote. `python3 tools/os88disk.py --verify` is the
other half and answers a different question.

**WHAT MEDIA HOLDS IS NOT TYPED HERE, and that is the point of the row rather
than a weakening of it.** It used to be a literal `["BROWSER.HTM",
"GUIDE.TEX", "PAPER.TEX"]`, which asserted the Makefile's data list and not
the machine - so `PKGZ ?= lz4` failed it for doing exactly what
docs/plans/O88-COMPRESSION-PLAN.md 13.4 calls the single most visible thing
compression buys this project: BEVERLY.MOD is 42 clusters wrapped instead of
114, the two-disk split of SPEC.md 24.4 COLLAPSES, and the module rides the
apps disk in MEDIA/ where both players already look. A row that has to be
edited when the payload changes is a row that fails for a shipping decision.

So the expectation comes off the IMAGE, read by tools/os88flush.py's
independent FAT12 reader, and the assertion is that the GUEST's view of the
folder is the same set. That is the comparison the paragraph above describes
and the literal list never made: two readers, one volume.
"""
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88build
import os88flush
import os88marty
import os88mouse
import os88sym
import dispcp

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
SYS_IMG = "build/os8088-360.img"
S = os88sym.linear
fails = []


def check(name, got, want):
    ok = sorted(got) == sorted(want)
    print("  [%s] %s: %r" % ("PASS" if ok else "FAIL", name, got))
    if not ok:
        fails.append("%s: %r, wanted %r" % (name, got, want))


def on_disk(img, folder):
    """What the IMAGE says is in `folder` - the host's independent reading."""
    v = os88flush.Volume(open(os88build.at(img), "rb").read(), img)
    return [e.name for e in v.listdir(folder) if e.name != ".."]


def run(apps, want_root):
    want_media = on_disk(apps, "MEDIA")
    print("B: = %s   MEDIA holds %r on the image" % (apps, want_media))
    with os88marty.launch(SYS_IMG, apps=apps, machine=MACHINE) as m:
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        check("root", [n for n, _ in dispcp.listing(m, S)], want_root)

        # open_named and not a row number: the listing sorts by name
        # (SPEC.md 19.4) and a folder that gains a file renumbers every row
        # after it - dispcp's own block on why that is not a detail.
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "MEDIA")
        check("MEDIA", [n for n, _ in dispcp.listing(m, S) if n != ".."],
              want_media)
        if not want_media:
            fails.append("MEDIA on %s is EMPTY on the image, so the check "
                         "above compares nothing with nothing - the folder "
                         "has to exist AND the reader has to find the files "
                         "in it" % apps)


run("build/media360.img", ["MEDIA"])
run("build/apps360.img", ["APPS", "GAMES", "MEDIA", "SYSTEM"])

if fails:
    print("\nFAILED:\n  " + "\n  ".join(fails))
    sys.exit(1)
print("\nmediadisk: OK on %s" % MACHINE)
