#!/usr/bin/env python3
"""A constant written down twice must say the same thing in both places.

    python3 tests/unit/t_mirror.py

There is no linker in this tree (SPEC.md: everything is `nasm -f bin`), so
nothing resolves a symbol across two files.  When the kernel, the boot sector,
the SDK and the host tools all need the same number, the number is TYPED OUT
in each of them - and CLAUDE.md names the consequence in two separate places:

    "APP_MAX_SIZE is mirrored in kernel/kernel.asm, apps/os88api.inc and
     tools/os88pkg.py - change them together and rebuild every .o88."

    "Two files carry KERNEL_SEG - kernel/kernel.asm and boot/boot.asm - plus
     apps/os88api.inc, because it is baked into every package's far-call
     targets."

Both are instructions to a human to remember something, which is the same
class of gate as *"sort the %defines by address and look for a duplicate"* and
fails the same way.  A half-applied change assembles perfectly: the boot
sector loads the kernel to one segment and the kernel believes it is at
another, and what you get is a machine that dies before the first pixel with
nothing to read.

THE LIST MAINTAINS ITSELF, which is the point.  Nothing here enumerates which
constants are mirrored - it takes every `NAME equ VALUE` from each file and
checks that any name defined in MORE THAN ONE of them agrees everywhere.  So
a constant that becomes mirrored tomorrow is covered tomorrow, with nobody
remembering to add it.  Today that is 20 names across the kernel and the SDK,
including `KERNEL_SEG`, `APP_MAX_SIZE`, `TITLE_H`, `MBAR_H`, the colour
indices and the SPEC.md 57 debug-registry tags.

The host tools are checked too, and the self-maintaining property has to be
said again there rather than assumed: PY_MIRROR below is a HAND-WRITTEN list,
and it names two files.  `KERNEL_SEG = 0x0060` is typed out in fifteen Python
files in this tree.  So the Python side is covered by `tools/os88geom.scan()`
instead, which walks every `.py` in `tools/` and `tests/` for an assignment
whose NAME is one os88geom mirrors and whose value has drifted - the same
"nothing enumerates it" shape as the asm side, over 156 local copies rather
than two.  It had no caller at all until this check acquired one, and four
copies of `MBAR_H` had gone stale behind it.  PY_MIRROR stays for the two
constants os88geom does not carry an authority for.

ONE LIMITATION, stated so nobody trusts it further: a definition inside a
`%if` is read at its first spelling, so a constant that legitimately differs
per build would be compared at one arm.  None does today; if one ever should,
put it in DIVERGENT below with the reason rather than deleting the check.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                           # noqa: E402
import os88geom as geom                                   # noqa: E402

ASM = ["kernel/kernel.asm", "kernel/splash.inc", "boot/boot.asm",
       "boot/boothd.asm", "apps/os88api.inc", "apps/os88ui.inc",
       "drivers/os88drv.inc",
       # The claim table's own namespace (SPEC.md 50.2/50.6): the SDK block
       # under "mirrored from kernel/memory.inc" is the Task Manager's only
       # way to name a kernel tag, and a RENUMBER there is silent in exactly
       # the way this file exists for - the heap page would go on decoding
       # the old word and label the wrong claim. tests/unit/t_ktags.py is the
       # other half: that every tag reaches the SDK at all, which a mirror
       # check by construction cannot see.
       "kernel/memory.inc",
       # The screen saver's private ABI (SPEC.md 79.3): five verbs, the
       # settings block's four offsets, the mode bits and the minutes clamp,
       # all written out in the kernel AND in the overlay because an overlay
       # cannot include a kernel header.  That is exactly the shape this file
       # exists for, and it was the first ABI in the tree with nothing at all
       # watching it.
       "kernel/blank.inc", "drivers/saver/saver.asm",
       "drivers/saver/svcfg.inc",
       # The socket ABI's two ends (SPEC.md 72.20): netpkg.inc is the header a
       # package includes and tcp.inc is the driver's own, and the TS_* a
       # reader tests by name are typed out in both. netpkg.inc's own comment
       # already promises this row keeps them honest, and until now it did
       # not name either file.
       "drivers/net/netpkg.inc", "drivers/ether/tcp.inc",
       # apps/c64 is a C package whose assembly half and C half type the same
       # constants out twice (docs/C64-SPEC.md, its memory and screen
       # sections): the core's scratch offsets, the composer's band stride.
       # A drifted C64_SCR_WLO reads the wrong scratch words and presents as
       # a stale screen, not as an error.
       "apps/c64/c64cpu.inc", "apps/c64/c64band.inc"]

# ...and the C side of those, which cannot `%include` an .inc any more than a
# host tool can.  `#define NAME VALUE`, same one-value-everywhere rule.
CDEF = ["apps/c64/c64.c", "apps/c64/c64scr.c"]

# Constants a host tool spells out for itself, and where the truth lives.
PY_MIRROR = {
    "KERNEL_SEG":   ["tools/os88sym.py", "tools/os88marty.py"],
    "APP_MAX_SIZE": ["tools/os88pkg.py"],
}

# Names that are DELIBERATELY different between two files. Empty today. A row
# here is a decision, so give the reason - an unexplained exemption is how a
# real divergence gets filed as an intended one.
DIVERGENT = {}

EQU = re.compile(r"^([A-Z][A-Z0-9_]*)\s+equ\s+([^\s;]+)", re.M)
PYCONST = re.compile(r"^([A-Z][A-Z0-9_]*)\s*=\s*([^\s#]+)", re.M)
CCONST = re.compile(r"^#define\s+([A-Z][A-Z0-9_]*)\s+([^\s/]+)\s*(?:/\*|$)",
                    re.M)


def num(v):
    """The value as an int where that is possible, else the text."""
    t = v.strip().rstrip("hH")
    try:
        if v.lower().startswith("0x"):
            return int(v, 16)
        if v.lower().endswith("h"):
            return int(t, 16)
        return int(v, 0)
    except ValueError:
        return v.strip()


def defs(rel, pattern):
    path = os.path.join(ROOT, rel)
    if not os.path.exists(path):
        return {}
    with open(path, errors="replace") as f:
        src = f.read()
    out = {}
    for name, val in pattern.findall(src):
        out.setdefault(name, num(val))          # first spelling wins - see above
    return out


def structfields(rel, name):
    """The field names of `struct <name>`, in order, uppercased."""
    path = os.path.join(ROOT, rel)
    if not os.path.exists(path):
        return []
    with open(path, errors="replace") as f:
        src = f.read()
    m = re.search(r"struct\s+%s\s*\{(.*?)\}" % name, src, re.S)
    if not m:
        return []
    out = []
    for line in m.group(1).split(";"):
        line = re.sub(r"/\*.*?\*/", " ", line, flags=re.S)
        line = line.split("/*")[0]
        parts = line.split()
        if len(parts) < 2:
            continue
        for f in " ".join(parts[1:]).split(","):
            f = f.strip().strip("*").strip()
            if re.match(r"^[a-z_][a-z0-9_]*$", f):
                out.append(f.upper())
    return out


def main():
    tables = {rel: defs(rel, EQU) for rel in ASM}
    tables.update({rel: defs(rel, CCONST) for rel in CDEF})

    where = {}
    for rel, d in tables.items():
        for name, val in d.items():
            where.setdefault(name, []).append((rel, val))

    mirrored = {n: v for n, v in where.items() if len(v) > 1}
    for name, places in sorted(mirrored.items()):
        if name in DIVERGENT:
            continue
        vals = {v for _, v in places}
        check(len(vals) == 1,
              "%s agrees across %s" % (name, ", ".join(p for p, _ in places)),
              "there is no linker here - a constant mirrored in two files is "
              "two constants, and a half-applied change assembles cleanly in "
              "both. If this divergence is deliberate, put it in DIVERGENT",
              got="; ".join("%s=%s" % (p, v) for p, v in places),
              want="one value")

    # ...and the Python side, which cannot include anything at all.
    truth = tables["kernel/kernel.asm"]
    pychecked = 0
    for name, tools in PY_MIRROR.items():
        if name not in truth:
            check(False, "%s is defined in kernel/kernel.asm" % name,
                  "PY_MIRROR names it as the authority; if it moved, point this at "
                  "the new home rather than dropping the check")
            continue
        for rel in tools:
            got = defs(rel, PYCONST).get(name, "<not defined>")
            check(got == truth[name], "%s in %s matches the kernel" % (name, rel),
                  "a host tool with a stale copy validates or builds against a "
                  "kernel that is not this one (CLAUDE.md names both of these)",
                  got=got, want=truth[name])
            pychecked += 1

    # ...and the one mirrored LAYOUT: apps/c64's 6510 register file is a nasm
    # `resw` block with CM_* offsets and a C struct read over the same bytes,
    # and the field ORDER is the layout (docs/C64-SPEC.md's register plan).
    # A field inserted on one side alone makes the C read the wrong word.
    fields = structfields("apps/c64/c64.c", "c64_mach")
    cpu = defs("apps/c64/c64cpu.inc", EQU)
    if fields and cpu:
        for i, f in enumerate(fields):
            name = "CM_" + f
            check(cpu.get(name) == i * 2,
                  "struct c64_mach.%s is %s in c64cpu.inc" % (f.lower(), name),
                  "the C struct and the core's resw block are one layout typed "
                  "out twice; a field inserted on one side reads the wrong word",
                  got=cpu.get(name, "<not defined>"), want=i * 2)

    # ...and every OTHER local copy of a kernel constant in a host script.
    # tools/os88geom.py mirrors 69 of them and checks each against the kernel
    # source at import; `scan` is the other direction - the copies that did
    # NOT come through it. A correct copy is only the seed of the next stale
    # one, so the scan is the gate and the copy is allowed to stay.
    stale = [c for c in geom.scan(ROOT) if c[3] != c[4]]
    copies = len(geom.scan(ROOT))
    check(not stale,
          "no host script carries a stale copy of a kernel constant",
          "the cost of one is not the wrong number - it is the DAY spent "
          "believing the feature under test is broken. tools/os88geom.py's "
          "header names three that did exactly that. Import it from "
          "tools/os88geom.py rather than retyping the value",
          got="; ".join("%s:%d %s = %d, kernel says %d" % (p, ln, n, mine, k)
                        for p, ln, n, mine, k in stale) or "none",
          want="every copy equal to the kernel, or imported from os88geom")

    print("t_mirror: %d names mirrored across %d asm/c files, %d host-tool "
          "copies, %d local constants scanned"
          % (len(mirrored), len(ASM) + len(CDEF), pychecked, copies))
    done("t_mirror")


if __name__ == "__main__":
    main()
