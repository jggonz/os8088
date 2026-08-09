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


def syms(defines=(), check=True):
    """{name: offset within its OWN segment} for every label in the kernel.

    Unchanged for `.text` and `.bss`, which are KERNEL_SEG's; `linear()` is
    what turns any of them into an address, because only it knows the segment.

    `check` compares the temporary build against `build/kernel.bin` and
    raises if they differ - which means the tree has been edited since the
    last `make`, and every address below would describe a kernel that is not
    the one running.
    """
    return _load(defines, check)[0]


def sections(defines=(), check=True):
    """{name: the section it is in}, for anything that needs to know."""
    return _load(defines, check)[1]


def segment_of(name, defines=(), check=True):
    """The segment `name` is addressed through at run time."""
    off, sect, equ = _load(defines, check)
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


def _load(defines=(), check=True):
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
           "-I", os.path.join(ROOT, "build") + os.sep]
    for d in defines:
        cmd += ["-D" + d]
    cmd += ["-o", binf, asm]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError("nasm failed:\n" + r.stderr)

    built = os.path.join(ROOT, "build", "kernel.bin")
    if check and os.path.exists(built):
        if open(binf, "rb").read() != open(built, "rb").read():
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
