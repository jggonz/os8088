#!/usr/bin/env python3
"""os88sym - where a kernel symbol actually lives, for the debugger.

Reading a kernel flag over MartyPC or QMP means naming an address, and every
session so far has guessed one out of `nasm -l`'s listing. **That listing is
wrong for anything in `.bss`.** The address column and the bracketed operand
bytes are both SECTION-RELATIVE and are fixed up afterwards, so `menu_bovr`
reads as 0x0879 in the listing and is at 0xCBA4 in the binary - a plausible
small number, inside `.text`, that a script will happily read a byte from
forever. Two sessions have lost time to exactly that, one of them concluding
a feature was broken from a flag that was never the flag.

`nasm`'s `bin` backend has a `[map]` directive that answers properly, and it
is used here the way `kernsize.py` uses its markers: on a TEMPORARY COPY of
`kernel/kernel.asm`, with the result asserted byte-identical to the kernel
this tree just built. So the shipped source carries nothing, and a map that
described a different binary is an error rather than a subtle wrong answer.

    python3 tools/os88sym.py fpg_on menu_bovr        # offset AND linear
    python3 tools/os88sym.py --all | grep ^dock_

    from os88sym import syms, linear
    m.read(linear("fpg_on"), 1)                     # ...or m.sym() in os88marty

Knob builds (`VIDEO=`, `DISKCNT=1`, ...) move everything: pass the same
`-D`s with `--define`, or the addresses will be a different kernel's.

**AND THE SECTION DECIDES THE SEGMENT.** This answered `KERNEL_SEG:offset` for
every symbol for as long as it existed, which is right for `.text` and `.bss`
and wrong for the others: `.cold` runs at COLD_SEG and `.lowbss` lives at
LOW_SEG (SPEC.md 2.1/2.6), while `.boot2` and `.ovl` have no fixed segment at
all - stage 2 chooses it and publishes it in `[spl_fseg]`. That is 25KB of resident
code and every task stack, disk buffer and claim record - and the wrong answer
is a small plausible address INSIDE the kernel image that reads back happily
and means nothing, which is the exact failure this file was written to stop,
one section over. It cost a session: `mem_tab` resolved into `.text`, the
claim map read as 32 rows of noise, and a heap with 505KB free looked full.
So the map is parsed with `[map all]` now, symbols are attributed to their
section, and the four segment bases are read out of the map's own equates -
so a rung that moves cannot desync them.
"""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KERNEL_SEG = 0x0060                      # SPEC.md 2 - one place, one meaning

# Which segment each section is addressed through at run time. The VALUES come
# out of the map's own equates, so only this mapping lives here - and a section
# nobody has named is an error, not a guess at KERNEL_SEG (which is how the
# bug in the header went unnoticed: the wrong answer was always plausible).
SECTION_SEG = {".text": "KERNEL_SEG", ".bss": "KERNEL_SEG",
               ".cold": "COLD_SEG", ".lowbss": "LOW_SEG",
               # STAGE 2's BLOB (SPEC.md 2.9.5/2.9.6): the loader, the loading
               # screen and the boot overlay, one image at one address - which
               # stage 2 CHOOSES, copying itself to HEAP_SEG and publishing the
               # segment in [spl_fseg]. It is a kernel constant in the map, but
               # a kernel started some other way never wrote the word, so the
               # honest answer is the same one the on-demand modules get: read
               # [spl_fseg] out of the guest first. `.ovl` used to be FAT_SEG.
               ".boot2": None, ".ovl": None,
               # ...and the overlay's OTHER half (SPEC.md 2.5.2): `.ovlw` is
               # emitted at the FAT window's file offset, so stage 2's one
               # contiguous read lands it at FAT_SEG - a ladder constant, and
               # the kernel reaches it as `call FAT_SEG:`. Fixed until the
               # first mount overwrites it, which is the same caveat `.cold`
               # carries and does not change the answer
               ".ovlw": "FAT_SEG",
               # the planar decoder's buffers, a rung of their own above
               # .lowbss so a mono machine's heap can start under them
               # (SPEC.md 39.22)
               ".vgabuf": "VGABUF_SEG",
               # ...and the on-demand modules (SPEC.md 2.8), which have NO
               # fixed segment and never will: each is split out of the image
               # into a file of its own and loaded into a HEAP CLAIM when its
               # feature is asked for, so its base is different every boot.
               # `None` is the honest answer and is not the same as "nobody
               # has named this section yet" - segment_of says which of the
               # two it is hit, and `--all` prints the row with a dash instead
               # of dying on it. Finding one of these at run time is a
               # two-step read the caller has to do for itself: the kernel's
               # own row says where the claim went (tests/xmcheck.py does
               # exactly that for XMEM.DRV, SPEC.md 41.12).
               ".modc": None, ".modf": None, ".modl": None, ".modh": None,
               ".modp": None, ".modd": None, ".modmap": None}
# Every section either kernel emits, and the list is the UNION of both builds:
# `.modh` is hibernate's (SPEC.md 87) and `.modp`/`.modd` are kern_small's
# (SPEC.md 22.3.0, 38.0), so no kernel emits all six module sections and each
# build's assembly simply never produces the rows it has no section for. A
# section MISSING here is not silently wrong, it is `segment_of` raising "add
# it rather than assuming KERNEL_SEG" - loud rather than wrong, which is why
# `.modp`/`.modd` went unnoticed from the day the module split created them,
# and `.ovlw` from the day SPEC.md 2.5.2 split the overlay: `--all` died on
# the first `.ovlw` symbol of EITHER kernel, and `linear()` on any of the 246
# boot-overlay labels (`mouse_init`, `desk_init`, `wm_init`...) raised too.

_cache = {}


def _parse_map(path):
    """-> ({name: virtual offset}, {name: section}, {equate: value})

    nasm's `[map all]` groups the symbol listing under `---- Section .x ----`
    headers and lists every EQU under `---- No Section ----`. Two columns
    precede the name, real and virtual; virtual is the one an offset within
    the section's own segment.
    """
    off, sect, equ = {}, {}, {}
    cur = None
    insyms = False
    for line in open(path):
        s = line.strip()
        if s.startswith("-- Symbols"):
            insyms = True
            continue
        if not insyms:
            continue
        if s.startswith("---- Section "):
            cur = s.split()[2]
            continue
        if s.startswith("---- No Section"):
            cur = None
            continue
        parts = s.split()
        if len(parts) == 2 and parts[1][0].isalpha():        # "Value  NAME"
            try:
                equ[parts[1]] = int(parts[0], 16)
            except ValueError:
                pass
        elif len(parts) == 3 and cur and parts[2][0].isalpha():
            try:
                off[parts[2]] = int(parts[1], 16)
                sect[parts[2]] = cur
            except ValueError:
                pass
    return off, sect, equ


_DEFAULT = ()


def default_defines(*names):
    """The --define set every call below falls back to when given none.

    A knob kernel in `build/` is a DIFFERENT kernel, and this module refuses an
    address unless the map it built matches it byte for byte - so a harness
    driving a `KFZ=1` or `VIDEO=` build has to pass that knob to every lookup
    it makes, and there are dozens of them scattered across the test tree. One
    call here is what stops each of those being a place to forget it: set it
    once beside the knob that built the image, and the refusal stays exact
    rather than being worked around.
    """
    global _DEFAULT
    _DEFAULT = tuple(names)


def syms(defines=(), check=True):
    """{name: offset within its OWN segment} for every label in the kernel.

    Unchanged for `.text` and `.bss`, which are KERNEL_SEG's; `linear()` is
    what turns any of them into an address, because only it knows the segment.

    `check` compares the temporary build against `build/kernel.bin` and
    raises if they differ - which means the tree has been edited since the
    last `make`, and every address below would describe a kernel that is not
    the one running.
    """
    return _load(defines or _DEFAULT, check)[0]


def sections(defines=(), check=True):
    """{name: the section it is in}, for anything that needs to know."""
    return _load(defines or _DEFAULT, check)[1]


def equates(defines=(), check=True):
    """{name: value} for every `equ` - the CONSTANTS, not the addresses.

    `syms()` answers labels and nothing else, so a tool that wanted SCH_STACK
    got its own default back and quietly reported a 256-byte slice whatever the
    kernel had been built with. A constant a tool mirrors is a constant that
    goes stale; this is the one that cannot.
    """
    return _load(defines or _DEFAULT, check)[2]


def segment_of(name, defines=(), check=True):
    """The segment `name` is addressed through at run time."""
    off, sect, equ = _load(defines or _DEFAULT, check)
    if name not in off:
        raise KeyError("no kernel symbol %r" % name)
    s = sect[name]
    if s not in SECTION_SEG:
        raise RuntimeError("section %s has no segment in SECTION_SEG - add it "
                           "rather than assuming KERNEL_SEG" % s)
    nm = SECTION_SEG[s]
    if nm is None:
        raise RuntimeError(
            "%s is in %s, which has NO FIXED SEGMENT. An on-demand module "
            "(SPEC.md 2.8) is loaded into a heap claim; `.boot2` and `.ovl` "
            "are stage 2's blob, which puts itself where it likes (2.9.5). "
            "Either way this offset is only meaningful against a base the "
            "kernel chose at run time - read it out of the guest first "
            "([spl_fseg] for the blob)." % (name, s))
    if nm == "KERNEL_SEG":
        return KERNEL_SEG
    if nm not in equ:
        raise RuntimeError("the map has no %s equate to place section %s"
                           % (nm, s))
    return equ[nm]


def _modcut(blob):
    """Where the on-demand modules begin, out of the image's own trailer.

    SPEC.md 2.8: the kernel assembles whole and tools/os88mod.py splits it, so
    an assembly done here is longer than the shipped build/kernel.bin by
    however much module code the tree currently carries.  Returns len(blob)
    when there is no trailer, so a kernel built before this existed - or one
    built with the sections empty - still compares whole.
    """
    if len(blob) < 16 or int.from_bytes(blob[-2:], "little") != 0x384F:
        return len(blob)
    off = int.from_bytes(blob[-6:-2], "little")
    if off + 6 > len(blob) or blob[off:off + 4] != b"O8MM":
        return len(blob)
    n = blob[off + 4]
    if n == 0:
        return off
    return int.from_bytes(blob[off + 6:off + 10], "little")


_SHIPPED_DEFS = ("KERN_BIG", "KERN_SMALL", "KERNSIZE", "KERN_KNOB")


def _load(defines=(), check=True):
    # $OS88_DEFINES is how a tool that never asked for a knob still finds the
    # right map. Every helper here takes `defines`, and the ones layered above
    # it - os88geom.word, sucheck.fb, Marty.sym - do not thread it through, so
    # driving a knob-built kernel meant the byte-identity check below refusing
    # and the whole session dying at the first symbol. The check is RIGHT (a
    # map of a different kernel is a wrong answer, not a missing one); what was
    # missing was a way to tell it. Comma or space separated, e.g.
    #   OS88_DEFINES=NODRAGCACHE python3 tools/winmove.py sol
    env = os.environ.get("OS88_DEFINES", "")
    if env:
        defines = tuple(defines) + tuple(
            d for d in env.replace(",", " ").split() if d)
    # ...and $OS88_BUILD is the same idea for WHICH BUILD, because both the
    # generated includes and the image this map is checked against live in a
    # build directory, and `build/` is only one of them. A sub-make with
    # BUILD= set - `make field`, `make small`, any knob built into a directory
    # of its own - has its own associco.inc and its own kernel.bin, and
    # hardcoding build/ here meant the check compared a knob's map against the
    # PLAIN kernel and refused. That refusal is right and the map was right;
    # what was missing was a way to say which pair to use.
    bdir = os.environ.get("OS88_BUILD", "") or os.path.join(ROOT, "build")
    # ...and $OS88_ICODIR the same idea one file along: since the Makefile's
    # ICODIR, associco.inc need not be in $(BUILD) at all - a build that wants
    # only a knob KERNEL takes it from the default build rather than rebuilding
    # four byte-identical packages to make its own. Added to the include path,
    # never replacing $bdir, and empty on every ordinary build.
    idir = os.environ.get("OS88_ICODIR", "")
    if not os.path.isabs(bdir):
        bdir = os.path.join(ROOT, bdir)
    # ...resolved against ROOT and not the cwd, exactly as $bdir is: the
    # Makefile passes it relative ("build") and a caller with a different
    # working directory would otherwise get an include path pointing at a
    # directory that does not exist, and nasm would report a missing file.
    if idir and not os.path.isabs(idir):
        idir = os.path.join(ROOT, idir)
    # A KNOB KERNEL IS NOT BOUND BY KERN_BUDGET (kernel.asm guard 1), and the
    # Makefile says so with -DKERN_KNOB. A tool re-assembling one for its
    # symbol map has to say the same thing or nasm refuses a kernel that
    # `make` built happily - which reads as "the map is broken" rather than as
    # a missing define. KERN_SMALL is not a knob for this purpose: it is a
    # shipped configuration with a budget of its own.
    if any(d.split("=")[0] not in _SHIPPED_DEFS for d in defines):
        defines = tuple(defines) + ("KERN_KNOB",)
    key = (bdir,) + tuple(defines)   # ...and the DIRECTORY, or two builds
                                     # share one map and the second gets the
                                     # first's addresses
    if key in _cache:
        return _cache[key]

    src = os.path.join(ROOT, "kernel", "kernel.asm")
    with open(src) as f:
        body = f.read()
    tmp = tempfile.mkdtemp(prefix="os88sym")
    mapf = os.path.join(tmp, "k.map")
    asm = os.path.join(tmp, "kernel.asm")
    binf = os.path.join(tmp, "k.bin")
    with open(asm, "w") as f:            # the directive emits no bytes, which
        f.write("[map all %s]\n" % mapf)         # is what `check` proves
        f.write(body)

    cmd = ["nasm", "-f", "bin", "-w+error",
           "-I", os.path.join(ROOT, "kernel") + os.sep,
           "-I", os.path.join(ROOT, "apps") + os.sep,
           "-I", bdir + os.sep] + \
          (["-I", idir + os.sep]
           if idir and os.path.normpath(idir) != os.path.normpath(bdir) else [])
    for d in defines:
        cmd += ["-D" + d]
    cmd += ["-o", binf, asm]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError("nasm failed:\n" + r.stderr)

    built = os.path.join(bdir, "kernel.bin")
    if check and os.path.exists(built):
        # build/kernel.bin is the assembled image with the on-demand modules
        # CUT OFF (SPEC.md 2.8), so compare against the same prefix rather
        # than the whole thing.  The trailer os88mod.py reads is what says
        # where that cut falls, and reusing it here is what stops this file
        # growing a second opinion about the layout.
        mine = open(binf, "rb").read()
        mine = mine[:_modcut(mine)]
        if mine != open(built, "rb").read():
            raise RuntimeError(
                "the map describes a DIFFERENT kernel from %s: run `make` "
                "(or pass the knob's --define, or $OS88_DEFINES, and "
                "$OS88_BUILD for a sub-make's own directory) before trusting "
                "any address from it." % os.path.relpath(built, ROOT))

    out, sect, equ = _parse_map(mapf)
    if not out:
        raise RuntimeError("nasm produced an empty symbol map (%s)" % mapf)
    _cache[key] = (out, sect, equ)
    return _cache[key]


def linear(name, defines=()):
    """The 20-bit address a debugger wants: <this symbol's segment>:offset.

    NOT always KERNEL_SEG - see the header. `.cold` and `.lowbss` each
    run somewhere else, and getting that wrong yields a readable address in
    the wrong place rather than an error.
    """
    s = syms(defines)
    if name not in s:
        raise KeyError("no kernel symbol %r (a package's symbols are in its "
                       "own segment and are not here)" % name)
    return segment_of(name, defines) * 16 + s[name]


def main(argv):
    defines, names, want_all = [], [], False
    it = iter(argv)
    for a in it:
        if a == "--define":
            defines.append(next(it))
        elif a == "--all":
            want_all = True
        else:
            names.append(a)
    s = syms(defines)
    sect = sections(defines)

    def row(n):
        # An on-demand module's symbol has no fixed segment (see SECTION_SEG),
        # and `--all` listing every symbol must not die on the 576 of them:
        # it prints the OFFSET, which is the only true thing about them, and a
        # dash where a segment would be. It was an unhandled RuntimeError, so
        # `--all` had been dead since SPEC.md 2.8 landed and every caller of
        # it - tests/xmcheck.py among them - died with it.
        try:
            seg = segment_of(n, defines)
        except RuntimeError:
            if SECTION_SEG.get(sect[n], "") is not None:
                raise
            print("%-28s %-8s %5s:%04X  %5s" % (n, sect[n], "-", s[n], "-"))
            return
        print("%-28s %-8s %04X:%04X  %05X"
              % (n, sect[n], seg, s[n], seg * 16 + s[n]))

    if want_all:
        for n in sorted(s, key=lambda k: (sections(defines)[k], s[k])):
            row(n)
        return 0
    if not names:
        print(__doc__.strip())
        return 2
    for n in names:
        if n not in s:
            print("%-28s ?" % n)
            continue
        row(n)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
