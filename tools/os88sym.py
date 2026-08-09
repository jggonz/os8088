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
"""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KERNEL_SEG = 0x0060                      # SPEC.md 2 - one place, one meaning

_cache = {}


def syms(defines=(), check=True):
    """{name: offset in KERNEL_SEG} for every label in the kernel.

    `check` compares the temporary build against `build/kernel.bin` and
    raises if they differ - which means the tree has been edited since the
    last `make`, and every address below would describe a kernel that is not
    the one running.
    """
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
        f.write("[map symbols %s]\n" % mapf)     # is what `check` proves
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

    out = {}
    with open(mapf) as f:
        for line in f:
            parts = line.split()
            # "  <realaddr>  <virtaddr>  <name>" in the symbols section
            if len(parts) == 3 and parts[2] and parts[2][0].isalpha():
                try:
                    out[parts[2]] = int(parts[1], 16)
                except ValueError:
                    pass
    if not out:
        raise RuntimeError("nasm produced an empty symbol map (%s)" % mapf)
    _cache[key] = out
    return out


def linear(name, defines=()):
    """The 20-bit address a debugger wants: KERNEL_SEG:offset flattened."""
    s = syms(defines)
    if name not in s:
        raise KeyError("no kernel symbol %r (a package's symbols are in its "
                       "own segment and are not here)" % name)
    return KERNEL_SEG * 16 + s[name]


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
    if want_all:
        for n in sorted(s, key=lambda k: s[k]):
            print("%-28s %04X  %05X" % (n, s[n], KERNEL_SEG * 16 + s[n]))
        return 0
    if not names:
        print(__doc__.strip())
        return 2
    for n in names:
        if n not in s:
            print("%-28s ?" % n)
            continue
        print("%-28s %04X  %05X" % (n, s[n], KERNEL_SEG * 16 + s[n]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
