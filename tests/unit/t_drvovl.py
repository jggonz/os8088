#!/usr/bin/env python3
"""A driver-loaded OVERLAY is COMPRESSED like the rest (SPEC.md 20.13.5.1).
    python3 tests/unit/t_drvovl.py
TWO `.drv` FILES ARE NOT LOADED BY THE KERNEL. `RAMPAGE.DRV` is read by
RAMDISK.DRV and `HDDTOOL.DRV` by HDD.DRV, each with `OSAPI_FILE_READ` and its
own header check - never through `drv_load`. For a cycle that made them the
two files `PKGZ` must NOT touch: under the v4 BODY format a compressed driver
was a container only `drv_expand` inside `drv_load` could open, so a packed
overlay arrived at its loader as its own compressed bytes, the header check
refused it, and what the user saw was `Ram Disk needs the system disk` - the
driver loaded, its Control Panel cells published, and every control on the
page inert. That shipped, and it cost the RAM disk to save 646 bytes.
Since SPEC.md 20.13.3.1 a compressed driver IS a 'CZ' file and
`OSAPI_FILE_READ` is the transparent read (20.14.3): each overlay's loader
takes a claim the Makefile cuts from the IMAGE (`-DRAMPAGE_KB` and
`-DHDTOOL_KB`, off the `.bin`), reads at offset 0 with that capacity, and
what lands is the image, expanded - so both overlays ship packed by the same
rule as every other driver, and `tests/rdup.py` and `tests/hddcp.py` drive
the pages off them.
**THIS GATE IS THE RULE, not the two names.** It reads the drivers' own source
for the file names they load and checks each one against the build: whenever
any OTHER shipped driver is a 'CZ' file, an overlay is one too. A plain
overlay beside packed drivers is a Makefile rule that fell back - the exact
shape of the cycle above, seen from the other side - and `make PKGZ=`, which
packs nothing, asserts nothing here. A third overlay is covered the day it is
written rather than the day somebody notices its page is dead.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                           # noqa: E402
import os88drv                                            # noqa: E402
import os88build                                          # noqa: E402

# A driver naming another driver's FILE is the marker: `db 'HDDTOOL.DRV', 0`.
NAMED = re.compile(rb"db\s+'([A-Z0-9_]{1,8}\.DRV)'\s*,\s*0")


def overlays():
    """Every `.DRV` a DRIVER loads by name, out of the drivers' own source."""
    out = {}
    for dirpath, _, names in os.walk(os.path.join(ROOT, "drivers")):
        for n in names:
            if not n.endswith((".asm", ".inc")):
                continue
            p = os.path.join(dirpath, n)
            with open(p, "rb") as f:
                for m in NAMED.finditer(f.read()):
                    out.setdefault(m.group(1).decode(),
                                   os.path.relpath(p, ROOT))
    return out


def main():
    found = overlays()
    check(bool(found),
          "the driver sources still name their overlays",
          "an empty read means the `db 'X.DRV', 0` idiom has changed and this "
          "gate would pass by testing nothing - the failure dispcp.py and "
          "mkclick.py were both in (docs/WRITING-TESTS.md 1)")
    build = os.path.dirname(os88build.at("build/kernel.bin"))
    if not os.path.isabs(build):
        build = os.path.join(ROOT, build)
    ovl = {n.split(".")[0].lower() + ".drv" for n in found}
    others = 0
    for f in os.listdir(build):
        if f.endswith(".drv") and f not in ovl:
            with open(os.path.join(build, f), "rb") as fh:
                if fh.read(2) == b"CZ":
                    others += 1
    for name in sorted(found):
        stem = name.split(".")[0].lower()
        p = os88build.at("build/%s.drv" % stem)
        if not os.path.isabs(p):
            p = os.path.join(ROOT, p)
        if not os.path.exists(p):
            continue                    # not built in this tree; t_image covers
        with open(p, "rb") as f:        # whether it should have been
            raw = f.read()
        img = os88drv.image_unwrap(raw)
        check(len(img) >= 32 and img[:2] == b"O8",
              "%s expands to a driver-shaped image" % name,
              "what %s will read through OSAPI_FILE_READ is the IMAGE, and "
              "its header check runs against these bytes" % found[name],
              got=img[:4].hex(), want="'O8', version 4")
        if others:
            check(raw[:2] == b"CZ",
                  "%s is compressed with the rest" % name,
                  "%d other shipped drivers are 'CZ' files and this is not: "
                  "the rule for it fell back to `python3 tools/os88drv.py` "
                  "without $(PKGZARG). %s reads it with OSAPI_FILE_READ into "
                  "a claim cut from the image, which expands a 'CZ' file by "
                  "construction (SPEC.md 20.13.5.1)" % (others, found[name]),
                  got="file %d, image %d" % (len(raw), len(img)),
                  want="a 'CZ' container")
    return done("t_drvovl")


if __name__ == "__main__":
    sys.exit(main())
