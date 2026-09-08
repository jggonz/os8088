#!/usr/bin/env python3
"""MSEG's own offsets, out of nasm's map rather than out of arithmetic.

    from msegsym import sym
    sym("ms_want")      # -> the offset in MSEG's segment

MSEG's bss is an equ chain (SPEC.md 20.5): the standard's `OP_BSS` bytes come
first and the package's words follow. Three gates read those words out of the
guest, and all three used to recompute the chain by hand -

    ms_xwhere = image + OP_BSS + PARTS*2 + 4 + 2 + 2 + 2 + 2

- which is the fixture's layout typed out a fourth time, in Python, with the
part count in it. tools/os88map.py is the general answer (and its header says
why the byte comparison is the whole point); this is MSEG's three arguments to
it, in one place so no gate carries them.
"""
import os
import sys

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "tools"))
from os88map import Syms                                    # noqa: E402

MSEG = Syms("tests/multiseg/mseg.asm", "build/mseg.bin",
            ["apps", "tests/multiseg"])

# ...and the SAME SOURCE built -DMSEG_COMP, which is the compressed-part
# fixture (SPEC.md 20.12.7). It is a second Syms and not a flag on the first
# because the byte comparison is per BUILD: the two binaries differ, and a map
# that described either one of them would be wrong about the other.
MSEGZ = Syms("tests/multiseg/mseg.asm", "build/msegz.bin",
             ["apps", "tests/multiseg"], defines=("MSEG_COMP",))


def sym(name, comp=False):
    return (MSEGZ if comp else MSEG).sym(name)


if __name__ == "__main__":
    m = MSEG.all()
    for k in sorted(m, key=lambda k: m[k]):
        if k.startswith(("ms_", "op_", "os88_")):
            print("  %-16s 0x%04X" % (k, m[k]))
