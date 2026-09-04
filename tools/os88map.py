#!/usr/bin/env python3
"""A package's OWN symbols, out of nasm's map — and refused if it is a map of
a different build.

    from os88map import Syms
    c64 = Syms("apps/c64/c64.asm", "build/c64.bin", ["apps", "build"])
    c64.sym("_c64_m")           # -> its offset in the package's segment

WHY THE BYTE COMPARISON IS THE WHOLE POINT. Re-assembling a source to read its
map is only sound if the result is the build that is actually running: a map
of a source that has moved on describes a DIFFERENT program and answers plain
WRONG NUMBERS rather than erroring. `tools/os88sym.py` applies that rule to
the kernel and refuses an address unless the re-assembly is byte-identical to
`build/kernel.bin`; `tests/xmcheck.py`'s `ovl_sym` applies it to XMEM.DRV.
This is the same rule for a PACKAGE, and it is here because it was about to be
written a third time (docs/O88-MULTISEG-PLAN.md 11.0.4).

WHAT IT REPLACES. A gate that wants a package's own word had two choices
before: recompute the package's bss layout in Python - which is the package's
equ chain typed out a second time, with its constants in it, and has already
gone stale once in this family - or read a fixed offset and hope. nasm's map
carries every label AND every `equ` with its absolute value, so the package
answers for itself.

A C package works too: SmallerC's globals appear as `_name`, and the runtime's
own labels (`cc_parts_bss`, `op_base`) appear beside them.
"""
import os
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class Syms(object):
    """The symbols of one nasm source, checked against the binary it built."""

    def __init__(self, src, ship, incs=("apps",), prefixes=None):
        self.src = os.path.join(ROOT, src)
        self.ship = os.path.join(ROOT, ship)
        self.incs = [os.path.join(ROOT, i) + os.sep for i in incs]
        self.prefixes = prefixes
        self._map = None

    def _load(self):
        if not os.path.exists(self.ship):
            raise SystemExit("os88map: no %s - build it first."
                             % os.path.relpath(self.ship, ROOT))
        out = {}
        with tempfile.TemporaryDirectory() as td:
            cp = os.path.join(td, "m.asm")
            binp = os.path.join(td, "m.bin")
            mp = os.path.join(td, "m.map")
            with open(cp, "w") as f:
                f.write(open(self.src).read() + "\n[map all %s]\n" % mp)
            args = ["nasm", "-f", "bin", "-w+error"]
            for i in self.incs:
                args += ["-I", i]
            args += ["-o", binp, cp]
            r = subprocess.run(args, capture_output=True, text=True, cwd=ROOT)
            if r.returncode:
                raise SystemExit("os88map: %s would not assemble:\n%s"
                                 % (os.path.relpath(self.src, ROOT),
                                    (r.stderr or r.stdout)[-800:]))
            if open(binp, "rb").read() != open(self.ship, "rb").read():
                raise SystemExit(
                    "os88map: the re-assembly of %s is not byte-identical to "
                    "%s, so this map describes a DIFFERENT build. Rebuild and "
                    "try again." % (os.path.relpath(self.src, ROOT),
                                    os.path.relpath(self.ship, ROOT)))
            for line in open(mp):
                f = line.split()
                # `<real> <virtual> <name>`: labels and equs alike, and the
                # VIRTUAL column is the offset the package's own code uses.
                if len(f) != 3:
                    continue
                if self.prefixes and not f[2].startswith(tuple(self.prefixes)):
                    continue
                try:
                    out[f[2]] = int(f[1], 16)
                except ValueError:
                    pass
        if not out:
            raise SystemExit("os88map: nasm's map of %s carried no symbols - "
                             "the map format has changed under this reader"
                             % os.path.relpath(self.src, ROOT))
        self._map = out

    def sym(self, name):
        """`name`'s offset in the package's segment, or a named error."""
        if self._map is None:
            self._load()
        try:
            return self._map[name]
        except KeyError:
            near = sorted(n for n in self._map if name.strip("_") in n)
            raise KeyError("os88map: %s has no %r.%s"
                           % (os.path.relpath(self.src, ROOT), name,
                              (" Near it: %s" % ", ".join(near[:8])) if near
                              else ""))

    def all(self):
        if self._map is None:
            self._load()
        return dict(self._map)


if __name__ == "__main__":
    import sys
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    s = Syms(sys.argv[1], sys.argv[2], sys.argv[3:] or ["apps"])
    m = s.all()
    for k in sorted(m, key=lambda k: m[k]):
        print("  %-24s 0x%04X" % (k, m[k]))
