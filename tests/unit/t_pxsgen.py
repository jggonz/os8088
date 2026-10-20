#!/usr/bin/env python3
"""PIXELSTEIN 3D's generated includes are what their generators produce
(SPEC.md 97.12).

    python3 tests/unit/t_pxsgen.py

HOST-SIDE AND FAST - 0.3s - so it runs inside every `make`. apps/pixelstein/
pxtab.inc (the sine, tangent and fan tables) and pxlev.inc (the level
directory) are COMMITTED text, the way apps/paccman/pmc_rom.c and
apps/skies/csart.inc are, so that a plain build needs neither the generator's
arithmetic re-run nor a level checked; what holds them to their tools is this
row. It regenerates each into a temporary file and compares byte for byte;
a difference means somebody edited the include, or edited a level or a
constant and did not run `make pxsgen`, and either way the package would
assemble against numbers the reference renderer does not share.

pxart.inc joins the list when wave 2 writes it (a missing file is not a
failure: the row checks what exists and says which).

`t_paccman`'s mould, and `fast` rather than `soak` on its rule: the includes
are reached from a Makefile recipe and from two host tools, and a level edit
that leaves a stale directory fails at LAUNCH on a 4.77 MHz machine three
boots later rather than here.
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, eq, done                         # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PXS = os.path.join(ROOT, "apps", "pixelstein")

# include, generator, the generator's extra arguments - and the option that
# names the output, "-o" but for pxhuda.inc (wave 4, SPEC.md 97.13: the HUD
# masters' bits, which pxsart.py writes beside pxart.inc from the same run)
GENERATED = (
    ("pxtab.inc", "tools/pxstab.py", ()),
    ("pxlev.inc", "tools/pxslevel.py", ("--no-sweep",)),
    ("pxart.inc", "tools/pxsart.py", ()),
    ("pxhuda.inc", "tools/pxsart.py", (), "--hud"),
)


def regenerate(tool, args, flag="-o"):
    fd, tmp = tempfile.mkstemp(prefix="pxsgen_", suffix=".inc")
    os.close(fd)
    try:
        r = subprocess.run([sys.executable, os.path.join(ROOT, tool), flag, tmp]
                           + list(args), capture_output=True, text=True, cwd=ROOT)
        if r.returncode:
            return None, (r.stdout + r.stderr).strip()[-600:]
        return open(tmp).read(), ""
    finally:
        os.unlink(tmp)


def main():
    seen = 0
    for row in GENERATED:
        inc, tool, args = row[:3]
        flag = row[3] if len(row) > 3 else "-o"
        path = os.path.join(PXS, inc)
        if not os.path.exists(os.path.join(ROOT, tool)):
            print("t_pxsgen: %s has no generator yet (%s) - a later wave's" % (inc, tool))
            continue
        if not os.path.exists(path):
            print("t_pxsgen: %s is not written yet - a later wave's" % inc)
            continue
        want, err = regenerate(tool, args, flag)
        if not check(want is not None, "%s runs" % tool, why=err):
            continue
        have = open(path).read()
        check(have == want, "apps/pixelstein/%s is what %s generates" % (inc, tool),
              why="run `make pxsgen` and commit the result; a stale include is a "
                  "package assembled against numbers tools/pxssim.py does not share",
              got=have.encode(), want=want.encode())
        seen += 1
        print("  pxsgen: %s matches %s (%d lines)" % (inc, tool, have.count("\n")))
    eq(seen >= 2, True, "at least the two wave-0 includes were checked",
       why="a row that checked nothing is an absent gate")
    done("t_pxsgen")


if __name__ == "__main__":
    main()
