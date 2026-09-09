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
       "apps/c64/c64cpu.inc", "apps/c64/c64band.inc",
       # ...and apps/apple2, which is the same construction one machine along
       # (docs/APPLE2-SPEC.md, its memory and screen sections): the core's
       # scratch offsets (A2_SCR_*), the composer's band stride and group
       # count, and the run reasons the C compares a2_m.reason against are all
       # typed out in the .inc AND in the C. A drifted A2_SCR_WLO reads the
       # wrong scratch word and presents as a stale screen; a drifted
       # A2_RUN_JAM is a machine that never stops.
       "apps/apple2/a2cpu.inc", "apps/apple2/a2band.inc",
       "apps/apple2/a2mem.inc",
       # ...and a2fsx.inc, whose A2_FSXW - the Apple's 280-pixel raster, one
       # byte a pixel - is typed out in a2scr.c as well: the composer writes
       # it and the frame loop strides by it, and a drift is a picture that
       # walks sideways one line at a time rather than an error.
       "apps/apple2/a2fsx.inc"]

# ...and the kernel, whole. `kernel/*.inc` + `kernel.asm`: 44 files, of which
# the hand-written list named five. The knob-only files (band.inc, moudiag.inc)
# come with it and that is right - a constant is a constant whether or not the
# block around it is compiled, and a knob build is where a drifted one would
# be found LAST.
KERNEL_GLOB = os.path.join(ROOT, "kernel", "*.inc")

# ...and the C side of those, which cannot `%include` an .inc any more than a
# host tool can.  `#define NAME VALUE`, same one-value-everywhere rule.
CDEF = ["apps/c64/c64.c", "apps/c64/c64scr.c",
        "apps/apple2/apple2.c", "apps/apple2/a2scr.c"]

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
                 "docs/plans/KERN-SMALL-CUT-PLAN.md D1) and the SDK keeps 14: "
                 "taskmgr sizes SS_TSTATE from it, so a package built at 14 "
                 "reading a 7-slot snapshot over-allocates and is safe, where "
                 "the reverse overflows",
    "MEM_MAX": "kern_small has 20 claim records "
               "(docs/plans/KERN-SMALL-CUT-PLAN.md D7) and the SDK keeps 32, which "
               "is CLAIM_SNAPSHOT_SIZE's input - same direction, same reason",
}

# --- constants mirrored under DIFFERENT NAMES --------------------------------
# The gate above pairs by NAME, so a mirror that was deliberately spelled
# differently is invisible to it. ALIAS is that case written down: each entry
# is (file, name, file, name), and both must resolve to the same number.
ALIAS = [
    # SPEC.md 13.14.5. Word allocates its three combos with a LITERAL because
    # WDVAR cannot evaluate an include's equ, and the drop-down records are
    # packed back to back - so a size that drifts is not a build error, it is
    # os88ui_drop writing over the next control's rect.
    ("apps/os88ui.inc", "OS88UI_DR_SIZE", "apps/word/word.asm", "WD_DREC_SZ"),
    # ...and a THIRD spelling, in Python: skiesui walks the two records by
    # stride to prove one does not overlap the next, which is the very defect
    # a stale size causes.
    ("apps/os88ui.inc", "OS88UI_DR_SIZE", "tests/skiesui.py", "DR_SIZE"),
]
DEFINE = re.compile(r"^%define\s+([A-Z][A-Z0-9_]*)\s+([^\s;]+)", re.M)

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

    # ...the ones spelled differently on purpose (SPEC.md 13.14.5),
    for fa, na, fb, nb in ALIAS:
        def anyof(rel, name):
            for pat in (EQU, DEFINE, PYCONST, CCONST):
                v = defs(rel, pat).get(name)
                if v is not None:
                    return v
            return None

        a, b = anyof(fa, na), anyof(fb, nb)
        check(a is not None and b is not None and a == b,
              "%s (%s) agrees with %s (%s)" % (na, fa, nb, fb),
              "the same quantity under two names is still two constants, and "
              "this one sizes a record the library writes past the end of",
              got="%s=%s; %s=%s" % (na, a, nb, b), want="one value")

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

    # ...and the mirrored LAYOUTS: a package's register file is a nasm `resw`
    # block with NAMED OFFSETS and a C struct read over the same bytes, and
    # the field ORDER is the layout (docs/C64-SPEC.md's register plan,
    # docs/APPLE2-SPEC.md section 4.1). A field inserted on one side alone
    # makes the C read the wrong word - a stale screen or a wrong PC, never an
    # error. One row per (C file, struct, .inc, offset prefix): a second
    # package is an ENTRY here and not a second copy of the block.
    for cfile, sname, incfile, pfx in [
            ("apps/c64/c64.c",       "c64_mach", "apps/c64/c64cpu.inc",     "CM_"),
            ("apps/apple2/apple2.c", "a2_mach",  "apps/apple2/a2cpu.inc",   "AM_")]:
        fields = structfields(cfile, sname)
        cpu = defs(incfile, EQU)
        # A `continue` here is a row that reports GREEN having checked
        # nothing: a rename of `struct a2_mach`, or a reflow that puts it on
        # one line, makes the regex miss and the whole block vanish from the
        # count this row exists to be evidence of. So it is a CHECK.
        check(bool(fields), "struct %s was found in %s" % (sname, cfile),
              "the layout check reads the struct with a regex; if it stops "
              "matching, every field below silently stops being checked")
        check(bool(cpu), "%s defines the %s offsets" % (incfile, pfx),
              "the layout check reads the core's `equ`s; without them there "
              "is nothing to compare the struct against")
        if not fields or not cpu:
            continue
        for i, f in enumerate(fields):
            # structfields() UPPERCASES every name it returns (its docstring
            # says so, and its last statement is `out.append(f.upper())`), so
            # both packages spell an offset the same way and there is one arm,
            # not two. The `pfx == "AM_"` conditional that used to be here
            # produced the identical string on both sides and read as though
            # the two packages differed.
            name = pfx + f
            check(cpu.get(name) == i * 2,
                  "struct %s.%s is %s in %s"
                  % (sname, f.lower(), name, os.path.basename(incfile)),
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

    # ...and the same shape once more, over a LITERAL rather than a constant:
    # the SDK's three overlay refusals against the kernel's TOAST_MAX.
    # apps/cc/crt0.asm assembles `No <NAME>.OVL`, `No RAM: <NAME>` and
    # `Old <NAME>.OVL` out of CC_PKG_NAME, so their length is different in
    # every C package that includes the SDK; OSAPI_TOAST TRUNCATES what is
    # longer than TOAST_MAX rather than refusing it, so all three were being
    # cut off the glass in all seven C packages - `APPLE2.OVL is not on this
    # disk` reaching the reader as `APPLE2.OVL is not on thi` - and nothing
    # in the tree was looking.
    #
    # THE BOUND IS THE NAME FIELD AND NOT THIS TREE'S NAMES, which is the
    # whole reason this row is not a list of seven package names: crt0.asm
    # %fatals a CC_PKG_NAME longer than 15 and docs/C-TOOLCHAIN.md tells
    # authors 15 is legal, so a literal sized by the longest name that
    # happens to exist today (7) fails inside somebody else's build the day
    # an 11-character package arrives. Both arms are checked - the declared
    # cap, and every %define CC_PKG_NAME in the tree, so a name too long for
    # the FIELD is caught by the header's own %fatal and one too long for a
    # MESSAGE is caught here.
    #
    # AND THE SECOND ARM IS DEFENCE IN DEPTH, WHICH IS SAID HERE BECAUSE IT
    # CANNOT FIRE ON ITS OWN IN A HEALTHY TREE: every name really is <=
    # name_cap (crt0.asm %fatals otherwise), so the cap arm is the strictly
    # stronger test and the per-name arm is silent whenever it passes. What
    # the per-name arm is for is the day one of the two things it does NOT
    # depend on breaks - the %fatal fence being deleted, the cap being raised
    # without re-sizing the literals, or the `%if cc__namelen > (\d+)` regex
    # above quietly matching something smaller - and then it reports the
    # PACKAGE, by path, rather than an abstract ceiling. It was EXERCISED
    # rather than assumed: a scratch tests/_toastprobe/probe.asm declaring a
    # 17-character CC_PKG_NAME makes this arm and only this arm fail
    # (`cc_ovm_mem with CC_PKG_NAME 'SEVENTEENCHARSXYZ' ... is 25
    # characters`, exit 1) while the cap arm stays quiet at 23 - which is
    # also how the four-conversion/five-argument TypeError that had kept it
    # from ever running was found.
    toast_src = open(os.path.join(ROOT, "kernel", "toast.inc")).read()
    m = re.search(r"^TOAST_MAX\s+equ\s+(\d+)", toast_src, re.M)
    check(m, "kernel/toast.inc still defines TOAST_MAX",
          "this row reads the cap out of the kernel rather than typing 24 a "
          "third time; a renamed constant must fail rather than skip",
          got="TOAST_MAX equ <n>" if m else "<not found>", want="TOAST_MAX equ <n>")
    toast_max = int(m.group(1)) if m else 0

    crt0_path = os.path.join(ROOT, "apps", "cc", "crt0.asm")
    crt0 = open(crt0_path).read()
    mc = re.search(r"%if\s+cc__namelen\s*>\s*(\d+)", crt0)
    check(mc, "apps/cc/crt0.asm still %fatals on CC_PKG_NAME's length",
          "the declared cap is what the three overlay refusals have to fit; "
          "without it this row would only know the names that exist today",
          got=("cap %s" % mc.group(1)) if mc else "<not found>",
          want="a %if cc__namelen > <n> fence")
    name_cap = int(mc.group(1)) if mc else 0

    # every C package's name, from the one place each of them states it
    ccnames = {}
    for f in (glob.glob(os.path.join(ROOT, "apps", "*", "*.asm"))
              + glob.glob(os.path.join(ROOT, "tests", "*", "*.asm"))):
        for d in re.finditer(r"^\s*%define\s+CC_PKG_NAME\s+'([^']*)'",
                             open(f).read(), re.M):
            ccnames[d.group(1)] = os.path.relpath(f, ROOT)
    check(ccnames, "the tree still declares C package names to size against",
          "a corpus of zero is a gate that has stopped looking",
          got="%d name(s)" % len(ccnames), want="at least one CC_PKG_NAME")

    # ...and the literals, as crt0.asm assembles them: a quoted run
    # contributes its own characters and CC_PKG_NAME contributes a name's.
    ovm = []
    for line in crt0.splitlines():
        m2 = re.match(r"^(cc_ovm_[A-Za-z0-9_]*)\s*:?\s*db\s+(.*)$", line)
        if not m2:
            continue
        rest = m2.group(2)
        fixed = sum(len(t) for t in re.findall(r"'([^']*)'", rest))
        uses = len(re.findall(r"\bCC_PKG_NAME\b", rest))
        ovm.append((m2.group(1), fixed, uses))
    check(len(ovm) == 3,
          "apps/cc/crt0.asm still carries three cc_ovm_* refusals",
          "the overlay loader raises three - missing, out of memory and "
          "stale - and a row that finds fewer has stopped looking rather "
          "than passing",
          got="%d literal(s)" % len(ovm), want="3")

    long = []
    for label, fixed, uses in ovm:
        n = fixed + uses * name_cap
        if n > toast_max:
            long.append("%s is %d characters with a %d-character name"
                        % (label, n, name_cap))
        for nm, where in sorted(ccnames.items()):
            n = fixed + uses * len(nm)
            if n > toast_max:
                long.append("%s with CC_PKG_NAME %r (%s) is %d characters"
                            % (label, nm, where, n))
    check(not long,
          "every SDK overlay refusal fits TOAST_MAX for a full-length "
          "CC_PKG_NAME",
          "OSAPI_TOAST TRUNCATES rather than refusing, so the consequence is "
          "in the half that is cut. Size the prose from the NAME FIELD (%d "
          "characters, crt0.asm's own %%fatal) and not from the longest name "
          "in the tree today - a message that fits only a short name is a "
          "build failure in a package that has not been written yet"
          % name_cap,
          got="; ".join(long) or "none",
          want="every cc_ovm_* literal <= TOAST_MAX %d with a %d-character "
               "name" % (toast_max, name_cap))

    print("t_mirror: %d names mirrored across %d asm/c files, %d host-tool "
          "copies, %d local constants scanned, %d os88parts copies, %d SDK "
          "toast(s) against TOAST_MAX %d for %d CC_PKG_NAME(s)"
          % (len(mirrored), len(asm) + len(CDEF), pychecked, copies, pcopies,
             len(ovm), toast_max, len(ccnames)))
    done("t_mirror")


if __name__ == "__main__":
    main()
