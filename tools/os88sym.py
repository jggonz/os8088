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
and wrong for the other three: `.cold` runs at COLD_SEG, `.ovl` at FAT_SEG and
`.lowbss` lives at LOW_SEG (SPEC.md 2.1/2.5/2.6). That is 25KB of resident
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
               ".cold": "COLD_SEG", ".ovl": "FAT_SEG", ".lowbss": "LOW_SEG"}

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
    key = tuple(defines)
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
           "-I", os.path.join(ROOT, "build") + os.sep]
    for d in defines:
        cmd += ["-D" + d]
    cmd += ["-o", binf, asm]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError("nasm failed:\n" + r.stderr)

    built = os.path.join(ROOT, "build", "kernel.bin")
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
                "the map describes a DIFFERENT kernel from build/kernel.bin: "
                "run `make` (or pass the knob's --define) before trusting any "
                "address from it.")

    out, sect, equ = _parse_map(mapf)
    if not out:
        raise RuntimeError("nasm produced an empty symbol map (%s)" % mapf)
    _cache[key] = (out, sect, equ)
    return _cache[key]


def linear(name, defines=()):
    """The 20-bit address a debugger wants: <this symbol's segment>:offset.

    NOT always KERNEL_SEG - see the header. `.cold`, `.ovl` and `.lowbss` each
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
        seg = segment_of(n, defines)
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
