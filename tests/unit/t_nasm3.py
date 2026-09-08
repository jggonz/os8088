#!/usr/bin/env python3
"""The whole tree, assembled by NASM 3.

    python3 tests/unit/t_nasm3.py [--nasm PATH] [-j N] [--no-knobs]

WHAT THIS IS DEFENDING, in one line: **an assembler that takes this tree is
not the same claim as THE assembler that takes it.**  CONTRIBUTING.md's floor
here is nasm 2, and everything in the tree is checked against whichever nasm
the box happens to have - which on this container, on CI and on every Debian
or Ubuntu box is 2.16.  Homebrew's is 3.x.  So the tree is assembled every
day by 2.x and shipped to people whose only assembler is 3.x, and nothing
between the two ever asked whether it still builds.

IT DOES NOT ALWAYS, AND THE INCIDENT IS THE REASON THIS FILE EXISTS.
`kernel/mod.inc`'s `mod_fpr` computed a table displacement as

    add di, mod_fp - mod_tab*7

which nasm 2.11 through 2.16 assemble without a word and nasm 3 refuses -
`invalid operand type`, in every spelling, because a LABEL times a constant is
not a scalar there.  It reached a merge that way and had to be adapted after
the fact (commit 799c5a9; `MOD_TAB_OFF equ mod_tab - $$` is the fix, and the
comment there says why).  Nobody was careless: the construct assembles
perfectly on the assembler everybody in the loop was running.  A gate is the
only thing that closes that gap, and this is it.

WHAT IT ASSEMBLES.  An nasm-3 refusal is a property of a SOURCE CONSTRUCT, so
it is refused wherever it appears - which turns the question into "has nasm 3
seen the assembly people actually compile", and the answer has two halves
(PHONY_TARGETS below says where the line is drawn and what is deliberately
outside it):

  * the SHIPPED SET - the artefacts the Makefile's own `all:` rule names, read
    out of it rather than listed here (shipped_targets() below).  That is the
    kernel, every package, every driver and the boot sector in all four
    geometries: the overwhelming majority of the tree's assembly, and where
    the mod.inc break was.
  * the CONFIGURATIONS `all` NEVER BUILDS - `small`, `smallapps` and `emu` for
    the two other kernels and the APP_SMALL package arms, and then every knob
    in tests/unit/t_buildmatrix.py's roster, which is `%ifdef` code no ordinary
    build compiles.  That file's own header is the argument for the second
    half and it applies here unchanged: an arm nothing assembles rots in
    silence, and it takes the gate that uses it with it.

The roster is IMPORTED from t_buildmatrix and not restated, so a knob added to
the Makefile joins this row on the same day it joins that one.  t_bmshare does
the same thing with the same reasoning; importing runs no build, because that
file does its work under `if __name__ == "__main__"`.

WHAT IT DOES NOT ASSERT, deliberately: that nasm 3 and nasm 2 emit the SAME
BYTES.  They do not, and it is legitimate - `drivers/xmem/xmem.asm`'s 32-bit
movers come out with the operand-size and address-size prefixes in the other
order (`66 67` against `67 66`), four bytes of `xmem.drv`, semantically
identical and reordered by the assembler's own encoder.  Asserting identity
would make this row red about a difference nobody should fix, and would bury
the one it exists for.  What the tree pins is that ITS OWN TOOLING is
deterministic (CLAUDE.md: os88disk.py fixes the volume serial and every FAT
timestamp), which is a claim about a fixed assembler and not across two.

WHAT IT COSTS: 165s cold on a four-core container and 128s once the tree is
there, of which the knob half is ~120s and is cold every run - the arms are
built and swept one at a time.  The tree itself is ~16 MB, larger than the
2-image trees os88build's own note is sized for, because this one holds all
nine floppies plus kern_small's two and kern_emu's; `make clean` sweeps it.

WHERE THE ASSEMBLER COMES FROM.  `tools/os88build.py`'s `nasm3()`, probed by
`-v` and never by a name: `$OS88_NASM3`, then `nasm3`/`nasm-3` on PATH, then
plain `nasm` if that is already 3.x - which is the ordinary case on macOS and
the reason the row must not insist on a second binary.  A box with no nasm 3
SKIPS (the runner's `nasm3` capability), because a skip is the box declining
to answer and this row must never report a pass about a version it never ran.
"""
import argparse
import concurrent.futures
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                          # noqa: E402
from harness import check, done                           # noqa: E402
# The knob roster, imported rather than copied - see the header. t_bmshare
# already takes PKG_VARS/NOWASTE/shares out of the same file for the same
# reason: a second copy of the list is a second thing to keep in step, and
# the way it fails is that this row goes on passing while it stops covering
# whatever was added.
from t_buildmatrix import KNOBS, build                    # noqa: E402

MAKEFILE = os.path.join(ROOT, "Makefile")

# The three targets `all` does not reach that are still PRODUCTS - the two
# other kernels and the APP_SMALL package arms - passed to os88build.tree()
# by NAME. Its `_goals` tells a phony target from an artefact by looking for
# a dot in the basename, and each of these already spells its own
# prerequisites `$(BUILD)/...`, so `BUILD=<tree>` does the rest.
#
# WHERE THE LINE IS, because the next reader will want to move it: this row
# assembles what `all` builds plus the KERNELS AND PACKAGE ARMS it does not.
# It does NOT build the on-demand instrument and application disks (`bench`,
# `wiredisk`, `netbench`, `bootdiag`, the lz* fixtures) even though several
# are seconds - they are gates and benches rather than the thing people
# compile, and taking them would mean a hand list of targets in this file,
# which is the one thing every derived list here exists to avoid. `wire.o88`
# and `recorder.o88` ARE covered, because the Makefile deliberately names
# them in `all` for exactly this reason and says so there; that decision
# belongs to the Makefile and not to this file.
PHONY_TARGETS = ("small", "smallapps", "emu")


def _makefile():
    return open(MAKEFILE, encoding="utf-8").read()


def _expand(txt, s, depth=0):
    """`$(VAR)` out of the Makefile's own definitions, except `$(BUILD)`.

    $(BUILD) is left standing ON PURPOSE. It is what marks a prerequisite as
    an ARTEFACT rather than a phony target below, and expanding it would tie
    this to the default value of a variable every caller here overrides.
    """
    if depth > 8:
        return s

    def sub(m):
        name = m.group(1)
        if name == "BUILD":
            return m.group(0)
        d = re.search(r'^%s\s*:?=[ \t]*(.*)$' % re.escape(name), txt, re.M)
        return _expand(txt, d.group(1).strip(), depth + 1) if d else m.group(0)

    return re.sub(r'\$\((\w+)\)', sub, s)


def _md5(p):
    with open(p, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def _first_error(text):
    """The one line a reader acts on, out of make's whole capture.

    `harness.check` trims a value at 160 characters and a refused build is
    twenty lines of which ONE names the file, the line and the construct - so
    passing the capture straight to `got=` hides exactly the part the `why`
    below promises. The capture is PRINTED and this goes in `got=`, which
    gives the reader the summary in the verdict and the detail above it.
    """
    for line in text.splitlines():
        if "error:" in line:
            return line.strip()
    return (text.strip().splitlines() or ["no output"])[-1].strip()


def _report(what, text):
    for line in text.strip().splitlines():
        print("    %s| %s" % (what, line), flush=True)


def shipped_targets():
    """Every artefact `all` builds, READ OUT OF the Makefile's `all:` rule.

    Derived for the reason every derived list in this directory is derived:
    the nine floppies were three geometries until 1.2MB arrived (SPEC.md 19),
    and a hand list here would have gone on reporting a pass for a geometry
    nasm 3 had never seen. `checkdocs`, `cc-note` and `test-fast` fall out
    without being named, because they are phony and do not expand to a path
    under $(BUILD) - and `test-fast` MUST fall out: it reads the shipped
    artefacts in the run's own tree, which is not the one this row builds.
    """
    txt = _makefile()
    m = re.search(r'^all:((?:[^\n]*\\\n)*[^\n]*)', txt, re.M)
    if not m:
        return ()
    out = []
    for p in _expand(txt, m.group(1).replace("\\\n", " ")).split():
        if p.startswith("$(BUILD)/"):
            out.append(p[len("$(BUILD)/"):])
    return tuple(out)


def _finish(before, shipped_kernel):
    """The clobber guard, then the verdict - on EVERY exit and not just the
    happy one. A row that gave up early is exactly when it is worth asking
    whether it left a foreign kernel behind."""
    if before:
        check(_md5(shipped_kernel) == before,
              "the shipped kernel was not clobbered",
              "this row is builds=False because everything it assembles goes "
              "into a tree of its own. If that stops being true it puts a "
              "kernel built by ANOTHER ASSEMBLER on top of the shipped one, "
              "and nothing afterwards says so (CLAUDE.md's cgak note)")
    done("t_nasm3")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nasm", help="the nasm 3 to use (default: probed)")
    ap.add_argument("-j", type=int, default=min(4, os.cpu_count() or 2))
    ap.add_argument("--no-knobs", action="store_true",
                    help="the shipped set only, dropping the ~120s knob half")
    a = ap.parse_args()

    n3 = a.nasm or os88build.nasm3()
    if not n3:
        # NOT a failure and not a pass: this box cannot answer. The registry
        # row names the `nasm3` capability so the runner skips it before ever
        # getting here; this is for somebody running the file by hand.
        sys.exit("t_nasm3: no nasm 3 on this box. Install one, or point "
                 "$OS88_NASM3 at it. `nasm -v` must say 3.x or newer - "
                 "CONTRIBUTING.md's floor here is 2, which is why this is a "
                 "capability and not a build requirement.")
    # THE ROW IS `builds=False`, AND THIS IS WHAT THAT RESTS ON. Everything
    # below goes into a private tree keyed on the assembler, so the kernel
    # the rest of the run is reading must come out untouched - and a build
    # that landed in the shared tree would put a kernel assembled by the
    # OTHER assembler on top of the shipped one with nothing afterwards
    # saying so (CLAUDE.md's cgak note; t_buildmatrix carries the same guard
    # for the same reason). Under a frozen soak `at` is the run's tree, which
    # is the copy this row could actually reach.
    shipped_kernel = os88build.at("build/kernel.bin")
    before = _md5(shipped_kernel) if os.path.exists(shipped_kernel) else None

    ver = os88build.nasm_version(n3)
    if not check(bool(ver) and ver[0] >= os88build.NASM3_MIN_MAJOR,
                 "%s is an nasm 3" % n3,
                 "a name is not a version - `nasm3` on PATH has been a "
                 "symlink to 2.16 - and assembling the tree twice with the "
                 "same assembler would report a pass about a version this "
                 "box never ran",
                 got=("%d.%02d" % ver if ver else "no version out of `-v`"),
                 want=">= %d.0" % os88build.NASM3_MIN_MAJOR):
        _finish(before, shipped_kernel)

    vs = "%d.%02d" % ver
    shipped = shipped_targets()
    if not check(len(shipped) >= 9,
                 "the shipped set still reads out of the Makefile's `all:` "
                 "(%d artefacts)" % len(shipped),
                 "the regex over the `all:` rule found nothing or next to "
                 "nothing, so this row would assemble a fraction of the tree "
                 "and report a pass for the rest. SPEC.md 19: there are nine "
                 "shipped floppies, plus wire.o88 and recorder.o88",
                 got=list(shipped), want=">= 9 artefacts under $(BUILD)"):
        _finish(before, shipped_kernel)

    # ONE TREE AND ONE `make`, which is also how a person meets this: the
    # first refusal stops the build, and the file and line in nasm's own
    # message is the actionable half. The tree is keyed on the assembler
    # path, so it is never the one a plain `make` maintains and never one
    # another knob row is using.
    print("t_nasm3: assembling with nasm %s (%s)" % (vs, n3), flush=True)
    try:
        t = os88build.tree("NASM=" + n3, targets=shipped + PHONY_TARGETS)
        ok, err = True, ""
    except RuntimeError as e:
        ok, err = False, str(e)[-2500:]

    if not ok:
        _report("shipped", err)
    check(ok, "the shipped tree and both other kernels assemble under nasm %s"
              % vs,
          "nasm 3 REFUSES constructs 2.x accepts - `add di, mod_fp - "
          "mod_tab*7` is `invalid operand type` there and silent under 2.16 - "
          "so a tree that builds on this box's default assembler can be "
          "un-buildable for everyone on 3.x. That is not hypothetical: it is "
          "commit 799c5a9, and it reached a merge. The lines above are nasm's "
          "own and name the file",
          got=_first_error(err) if err else "no error", want="exit 0")

    if not ok:
        # Every knob arm shares this tree's packages, so they would all fail
        # for the same reason and bury it. Say so rather than printing 99
        # copies of one break.
        print("t_nasm3: knob arms NOT attempted - they share the tree above",
              flush=True)
        _finish(before, shipped_kernel)

    for img in [x for x in shipped if x.endswith(".img")]:
        check(os.path.exists(t.img(img)), "...and it produced %s" % img,
              "a build that exits 0 and writes no image has lost a "
              "prerequisite, which is a different break from a refusal and "
              "looks like a pass without this",
              got=t.dir, want=img)

    if a.no_knobs:
        print("t_nasm3: knob arms skipped (--no-knobs)", flush=True)
        _finish(before, shipped_kernel)

    # THE %IFDEF ARMS. Out of tree beside the one above and sharing ITS
    # packages - not `build/`'s, which were assembled by the OTHER assembler
    # and would leave each arm half-answered.
    shared = os.path.relpath(t.dir, ROOT)
    extra = ["NASM=" + n3]
    print("t_nasm3: %d knob arms, -j%d" % (len(KNOBS), a.j), flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.j) as ex:
        for name, kok, _size, kerr in ex.map(
                lambda kv: build(*kv, shared=shared, extra=extra), KNOBS):
            if not kok:
                _report(name, kerr)
            check(kok, "make %s assembles under nasm %s" % (name, vs),
                  "a %ifdef arm no ordinary build compiles, and no ordinary "
                  "build compiles it under nasm 3 either - so it can be "
                  "refused there while every tier stays green. The arm is "
                  "the A/B half of a gate somewhere in tests/, and it takes "
                  "that gate with it",
                  got=_first_error(kerr) if kerr else "no kernel produced",
                  want="a kernel")

    print("t_nasm3: nasm %s took the shipped set (%d artefacts), kern_small, "
          "the APP_SMALL packages, kern_emu and %d knob arms"
          % (vs, len(shipped), len(KNOBS)))
    _finish(before, shipped_kernel)


if __name__ == "__main__":
    main()
