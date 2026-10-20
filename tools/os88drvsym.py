#!/usr/bin/env python3
"""A DRIVER's symbols, off the assembler line `make` itself would use.

    import os88drvsym
    D = os88drvsym.symbols("sound.bin")          # {name: image offset}
    D = os88drvsym.symbols("sound.bin", ("NOPICOMEM=1",))   # a knob arm
    python3 tools/os88drvsym.py sound.bin sbl_poolseg sbl_up

tools/os88sym.py is this for the KERNEL; nothing was it for a driver, so
every test that wanted an offset inside a `.DRV` re-assembled the source
with a hand-written nasm line - and a hand-written line drifts from the
Makefile's without failing. SOUND.DRV ships with `-DPICOMEM` (the Makefile's
`$(SNDDEF)`, SPEC.md 34.10.3); assembled without it every symbol reads 341
bytes out of place, and the bytes there are still the driver's own, so a
test reads a plausible wrong value rather than crashing. That shape broke
`gorillasmusic`, `1942sound`'s and `drmarioaudio`'s card legs, and
`trackmove`'s pool read, each on the base and each looking like the feature
under test (docs/plans/HANDOFF-APPS-SHARED.md 6.2, 6.3).

So nothing is restated here:

  1. The recipe is ASKED of make - `make -n --always-make` into a throwaway
     BUILD, os88build.defines_for()'s trick and its warning: never `make -n`
     with a knob in it against `build/`, because the Makefile's parse-time
     stamp rules delete artefacts there.
  2. The throwaway directory is swapped for the real one (os88build.at, so a
     frozen soak tree is honoured), the output for a scratch file, and the
     map is asked for with nasm's `--before` - the source file is the
     Makefile's own, unedited, so its `%include`s resolve exactly as they did.
  3. **The result is compared with the artefact on disk, byte for byte**,
     and a mismatch RAISES. That check is the point: a map of some other
     build is the defect this file exists to end, so it cannot be allowed to
     come back as a quiet wrong answer.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88build                                        # noqa: E402

ROOT = os88build.ROOT
_CACHE = {}


class Mismatch(RuntimeError):
    """The re-assembly is not the artefact the machine is running."""


def recipe(target, args=()):
    """The shell line `make <args>` would build build/<target> with,
    pointed at the run's real build directory."""
    target = os.path.basename(target)
    real = os88build.at(os.path.join("build", target))
    rdir = os.path.dirname(real) or "."
    tmp = tempfile.mkdtemp(prefix="os88drv")
    try:
        out = subprocess.run(["make", "-n", "--always-make", "BUILD=%s" % tmp]
                             + list(args) + [os.path.join(tmp, target)],
                             cwd=ROOT, capture_output=True, text=True).stdout
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    want = " -o %s/%s " % (tmp, target)
    for line in out.replace("\\\n", " ").splitlines():
        line = re.sub(r"\s+", " ", line).strip() + " "
        if line.startswith("nasm ") and want in line:
            return line.replace(tmp, rdir).strip(), real
    raise RuntimeError("os88drvsym: make names no nasm recipe for %s%s"
                       % (target, " with %s" % " ".join(args) if args else ""))


def symbols(target, args=(), check=True):
    """{symbol: offset in the image} for build/<target> (e.g. "sound.bin";
    a ".drv" name is taken as its ".bin"). Raises Mismatch when the
    re-assembly is not byte-identical to the artefact on disk."""
    target = os.path.basename(target)
    if target.endswith(".drv"):
        target = target[:-4] + ".bin"
    key = (target, tuple(args), check)
    if key in _CACHE:
        return _CACHE[key]
    line, real = recipe(target, args)
    with tempfile.TemporaryDirectory() as d:
        b, mp = os.path.join(d, "x.bin"), os.path.join(d, "x.map")
        line = line.replace(" -o %s " % real, " -o %s " % b, 1)
        line = line.replace(" -o %s/%s " % (os.path.dirname(real), target),
                            " -o %s " % b, 1)
        if " -o %s " % b not in line + " ":
            raise RuntimeError("os88drvsym: cannot redirect -o in: " + line)
        line = line.replace("nasm ", "nasm --before '[map symbols %s]' " % mp,
                            1)
        r = subprocess.run(["sh", "-c", line], cwd=ROOT,
                           capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError("os88drvsym: %s\n%s" % (line, r.stderr))
        if check:
            try:
                same = open(b, "rb").read() == open(real, "rb").read()
            except FileNotFoundError:
                raise Mismatch("os88drvsym: %s is not built - run `make`"
                               % real)
            if not same:
                raise Mismatch(
                    "os88drvsym: %s is not what `make %s` assembles from "
                    "this source - every offset would be plausible and "
                    "wrong. Rebuild it, or pass the knobs it was built with."
                    % (real, " ".join(args) or target))
        out = {}
        for ln in open(mp):
            f = ln.split()
            if len(f) == 3 and all(c in "0123456789ABCDEFabcdef"
                                   for c in f[0] + f[1]):
                out[f[2]] = int(f[1], 16)
    _CACHE[key] = out
    return out


def main(argv):
    if len(argv) < 2:
        print(__doc__.split("\n\n")[0] + "\n\nusage: os88drvsym.py "
              "<target.bin> [symbol ...] [-- KNOB=v ...]")
        return 2
    args = ()
    if "--" in argv:
        i = argv.index("--")
        argv, args = argv[:i], tuple(argv[i + 1:])
    s = symbols(argv[1], args)
    for n in (argv[2:] or sorted(s, key=s.get)):
        print("%04X  %s" % (s[n], n))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
