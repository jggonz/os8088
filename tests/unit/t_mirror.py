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
remembering to add it.  Today that is 289 names, including `KERNEL_SEG`,
`APP_MAX_SIZE`, `TITLE_H`, `MBAR_H`, the colour indices, the SPEC.md 57
debug-registry tags, the whole `W_*` window record, the whole `SSI_*`
snapshot, the entire fsx ABI and every `FERR_*`.

**IT MAINTAINED ITSELF ON ONE SIDE ONLY UNTIL THE KERNEL WAS GLOBBED**, and
that is the lesson worth keeping: the SDK end was one file and the KERNEL end
was five named by hand, so a constant typed out in the SDK and in any of the
other 39 kernel files was defined in exactly ONE listed file and no comparison
happened at all.  That was 111 names - all of them agreeing, none of them
watched, and `W_W` among them, which is the drift docs/UPSTREAM.md records as
having arrived from upstream three times.  A self-maintaining check with a
hand-written half is a hand-written check.

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

`tools/os88parts.scan()` is the same arrangement for the SDK's own namespace -
`apps/os88parts.inc`'s `OP_BSS` and the bss chain a package's parts standard
publishes (SPEC.md 20.12) - and it is here because that one has already gone
stale once, silently: OP_BSS moved 41 -> 65 -> 69 over three waves, a gate
kept 65, and it then read the package's own table two entries early and
reported the package as broken.

ONE LIMITATION, stated so nobody trusts it further: a definition inside a
`%if` is read at its first spelling, so a constant that legitimately differs
per build would be compared at one arm.  None does today; if one ever should,
put it in DIVERGENT below with the reason rather than deleting the check.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                           # noqa: E402
import os88geom as geom                                   # noqa: E402
import os88parts as parts                                 # noqa: E402

# EVERY KERNEL FILE, BY GLOB - see KERNEL_GLOB below. What is listed HERE is
# only what the glob cannot reach: the boot sectors, the SDK, the drivers and
# the C packages. The kernel half of each pair below used to be named here one
# file at a time, and naming it is what made the check partial - a constant
# typed out in the SDK and in any kernel file the list had not been told about
# was defined in exactly ONE listed file, so nothing compared it. That was 111
# names, including the WHOLE `W_*` window record, the whole `SSI_*` snapshot,
# the entire fsx ABI, every `FERR_*` and `CLIP_MAXKB` - all agreeing, none
# watched. The glob is the docstring's "the list maintains itself" applied to
# the side of the comparison that was not getting it.
ASM = ["boot/boot.asm", "boot/boothd.asm",
       "apps/os88api.inc", "apps/os88ui.inc",
       "drivers/os88drv.inc",
       # The screen saver's private ABI (SPEC.md 79.3): five verbs, the
       # settings block's four offsets, the mode bits and the minutes clamp,
       # all written out in kernel/blank.inc AND in the overlay because an
       # overlay cannot include a kernel header. That is exactly the shape
       # this file exists for, and it was the first ABI in the tree with
       # nothing at all watching it.
       "drivers/saver/saver.asm", "drivers/saver/svcfg.inc",
       # The socket ABI's two ends (SPEC.md 72.20): netpkg.inc is the header a
       # package includes and tcp.inc is the driver's own, and the TS_* a
       # reader tests by name are typed out in both. netpkg.inc's own comment
       # already promises this row keeps them honest, and until now it did
       # not name either file.
       "drivers/net/netpkg.inc", "drivers/ether/tcp.inc",
       # The XMS store's private ABI (SPEC.md 41.12.2): the six verb numbers
       # a caller passes in AL, the six caps-block offsets it reads the
       # answer out of, and XM_ABI_VER, all typed out in kernel/xmem.inc AND
       # in the driver because a driver cannot %include a kernel header - the
       # same shape as drivers/saver above. It is the WORST of them to leave
       # unwatched, because SPEC.md 41.12.4 makes the whole subsystem silent
       # BY DESIGN: a drifted XMV_* is a wrong index into drv_call's service
       # table, so the driver far-calls the wrong verb or a word straddling
       # two entries, and the kernel's answer to every xmem failure is to
       # carry on with no store and tell nobody.
       "drivers/xmem/xmem.asm",
       # apps/c64 is a C package whose assembly half and C half type the same
       # constants out twice (docs/C64-SPEC.md, its memory and screen
       # sections): the core's scratch offsets, the composer's band stride.
       # A drifted C64_SCR_WLO reads the wrong scratch words and presents as
       # a stale screen, not as an error.
       "apps/c64/c64cpu.inc", "apps/c64/c64band.inc"]

# ...and the kernel, whole. `kernel/*.inc` + `kernel.asm`: 44 files, of which
# the hand-written list named five. The knob-only files (band.inc, moudiag.inc)
# come with it and that is right - a constant is a constant whether or not the
# block around it is compiled, and a knob build is where a drifted one would
# be found LAST.
KERNEL_GLOB = os.path.join(ROOT, "kernel", "*.inc")

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
# --- deliberate divergences, with the reason ---------------------------------
#
# The SDK carries the LARGER value and the kernel may be smaller. That is not
# a half-applied change: `apps/os88api.inc` is compiled into every package,
# ONE `.o88` runs on BOTH kernels (SPEC.md 24), and each of these sizes a
# buffer a package hands the kernel to fill. So the safe direction is the
# package over-allocating - it reads a shorter snapshot into a longer buffer -
# and the unsafe one is the kernel writing more records than the package
# reserved. Shrinking the SDK's copy to match kern_small would overflow that
# buffer on kern_big.
#
# SPEC.md 51.0 took the same decision for MEM_P_FATW_N and states the rule.
DIVERGENT = {
    "MAX_TASKS": "kern_small has 7 slots (SPEC.md 8.7, "
                 "docs/KERN-SMALL-CUT-PLAN.md D1) and the SDK keeps 14: "
                 "taskmgr sizes SS_TSTATE from it, so a package built at 14 "
                 "reading a 7-slot snapshot over-allocates and is safe, where "
                 "the reverse overflows",
    "MEM_MAX": "kern_small has 20 claim records "
               "(docs/KERN-SMALL-CUT-PLAN.md D7) and the SDK keeps 32, which "
               "is CLAIM_SNAPSHOT_SIZE's input - same direction, same reason",
}

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
    asm = ASM + sorted(os.path.relpath(p, ROOT).replace(os.sep, "/")
                       for p in glob.glob(KERNEL_GLOB)) + ["kernel/kernel.asm"]
    tables = {rel: defs(rel, EQU) for rel in asm}
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

    # ...and the direction NEITHER of the two above looks in. `scan` only
    # guards a name once it is in `_MIRROR`, and `_MIRROR` only ever held what
    # somebody thought to add - so a kernel constant with SEVEN local copies
    # and no entry was invisible to the record and to its gate alike. That is
    # how VID_CTX_SZ got to nine copies before it drifted. `unmirrored` walks
    # the other way: every `NAME equ <int>` the kernel defines against every
    # `NAME = <int>` a host script does, and a second copy is the threshold
    # because one is a script naming a thing and two is a convention forming.
    # It found seven, 23 copies across 15 scripts, none of them guarded.
    loose = geom.unmirrored(ROOT)
    check(not loose,
          "no kernel constant has copies in two host scripts without an "
          "os88geom entry",
          "a copy that AGREES is the seed of the next one that does not, and "
          "the record cannot notice a name it was never told about. Add the "
          "constant to tools/os88geom.py's _MIRROR - or, if the kernel and "
          "the script mean different things by the same name, to _COLLISIONS "
          "with the reason (BAND_KB is the worked example)",
          got="; ".join("%s (%s = %d) in %s" % (n, f, v, ", ".join(
              "%s:%d" % (pp, ll) for pp, ll, _ in pl)) for n, f, v, pl in loose)
              or "none",
          want="every multiply-copied kernel constant mirrored or exempted")

    # ...and the SDK's own namespace, which is the same shape one authority
    # along. apps/os88parts.inc publishes OP_BSS and the standard's bss chain
    # (SPEC.md 20.12), a host gate turns `os88_image_end` into the package's
    # own words with it, and it has gone 41 -> 65 -> 69 over three waves - the
    # second move was missed, and tests/multiseg.py then read the package's
    # part table two entries early and reported parts as missing while the
    # window beside it said everything was fine.
    pall = parts.scan(ROOT) + parts.pkg_copies(ROOT)
    pstale = [c for c in pall if c[3] != c[4]]
    pcopies = len(pall)
    check(not pstale,
          "no host script carries a stale copy of an os88parts constant",
          "import it from tools/os88parts.py, which parses "
          "apps/os88parts.inc rather than mirroring it. A gate with a stale "
          "OP_BSS disagrees with the package it is testing about the "
          "package's own memory, and reports it as the package being broken. "
          "tools/os88pkg.py's own copies are in here too, through "
          "os88parts.PKG_MIRROR - it spells them OPF_*/PART_* so the scan "
          "cannot find them by name, and a packer writing rows at the wrong "
          "stride produces a table the standard reads as garbage. A mapped "
          "name that has been RENAMED reads as stale rather than vanishing",
          got="; ".join("%s:%d %s = %d, the include says %d" % c
                        for c in pstale) or "none",
          want="every copy equal to the include, or imported from os88parts")

    print("t_mirror: %d names mirrored across %d asm/c files, %d host-tool "
          "copies, %d local constants scanned, %d os88parts copies"
          % (len(mirrored), len(asm) + len(CDEF), pychecked, copies, pcopies))
    done("t_mirror")


if __name__ == "__main__":
    main()
