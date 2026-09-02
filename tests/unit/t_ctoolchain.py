#!/usr/bin/env python3
"""The C toolchain still produces a package (SPEC.md 73).

    python3 tests/unit/t_ctoolchain.py

WHY THIS ROW EXISTS, which is the same argument t_buildmatrix.py makes and
the same hole one level over.  `all` builds no C target at all - deliberately,
so a tree without the compiler still builds every shipping floppy (CLAUDE.md,
apps/cc/Makefile.inc) - and until this file the suite had a `cc` capability
that NO ROW USED.  So the largest thing `all` does not build was also the only
such thing with nothing watching it, and it rotted exactly the way that file
predicts: for two releases NO C package assembled, cword and runcpm and
ccsmoke and both capability gates alike, because SPEC.md 73.1 recorded a fact
about nasm that is false (`extern X` for an already-defined X is a
redefinition of X, and passes only when X's value is 0).  Nothing said so,
because nothing ran it.

WHAT IT CHECKS, in the order the chain runs:

  1. The compiler is where the capability probe says it is.  os88test.py adds
     `cc` on the strength of build/cc/SmallerC/smlrcc, so a row that then
     cannot find it is reporting a broken probe, not a broken toolchain.

  2. The four targets BUILD, from a forced-clean start.  ccsmoke is the SDK's
     worked example, chello and covl are the two capability gates, and cword
     is the application - and cword is in here rather than left to `soak`
     because it is where the VOLUME is: 1,327 lowered sites against chello's
     58, 76 overlay functions, 60 external references.  A lowering that is
     wrong once in a thousand sites shows up there and nowhere else.

  3. The generated assembly carries no bare `extern`.  That is the invariant
     itself rather than a consequence of it, so it fails on the change rather
     than on the next thing to depend on it.

  4. A CALL TO A THUNK NOBODY WROTE STILL STOPS THE BUILD, NAMING IT.  This
     is the check that is not "does it build", and it is here because 1-3
     cannot see the case at all: every external reference in the four real
     packages RESOLVES, so nothing above says what happens to one that does
     not.  What must happen is `symbol `_os88_foo' not defined` with the name
     in it, because the alternative - giving an unresolved reference an
     address - compiles the missing call into a jump to offset 0 of the
     package's own segment, its header, and assembles, packages and runs.
     Measured while writing this: stubbing the declarations with `X equ 0`
     wholesale, the way tools/setup-cc.sh does to its own canary, does NOT
     slip past 1-3 - `equ` on an already-defined label is a redefinition
     exactly as `extern` is - so the shape that would slip past is a stub for
     the UNDEFINED ones only: a catch-all in os88thunk.asm, a weak default,
     anything that answers for a name nobody wrote.  That is what this check
     is in front of, and no other check here is.

  5. build/kernel.bin is byte-identical afterwards.  t_buildmatrix.py's rule,
     for the same reason: this row runs `make`, and a row that quietly moved
     a shipped artifact would be worse than no row.

WHAT IS DELIBERATELY NOT HERE.  RunCPM, which is also C: `make runcpm` gates
itself on three host tests of its own and one of them boots QEMU, so it needs
a second capability and a minute of budget to re-cover the compile path these
four already cover.  It belongs beside its own gates, not here.  And nothing
in this file RUNS a package - tests/chello and tests/covl exist to be run and
are `soak` rows of their own; this one is about the chain that produces them.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

SMLRCC = os.path.join(ROOT, "build/cc/SmallerC/smlrcc")

# (make target, the artifacts it must leave). The .o88 is the real assertion -
# os88pkg.py refuses a header whose size word disagrees with the file, so an
# .o88 on disk is a package that validated, not just a file that appeared.
TARGETS = [
    ("cc-smoke", ["build/ccsmoke.o88"],                  ["build/ccsmoke.gen.asm"]),
    ("chello",   ["build/chello.o88"],                   ["build/chello.gen.asm"]),
    ("covl",     ["build/covl.o88", "build/COVL.OVL"],   ["build/covl.gen.asm"]),
    ("cword",    ["build/cword.o88", "build/CWORD.OVL"], ["build/cword.gen.asm"]),
]


def md5(path):
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def make(target, *extra):
    return subprocess.run(["make", target] + list(extra), cwd=ROOT,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True)


def main():
    if not os.access(SMLRCC, os.X_OK):
        check(False, "the C compiler is not at build/cc/SmallerC/smlrcc",
              why="os88test.py grants the `cc` capability on that file, so "
                  "this row should have been skipped rather than run. Run "
                  "tools/setup-cc.sh.")
        done("t_ctoolchain")

    kernel = os.path.join(ROOT, "build/kernel.bin")
    before = md5(kernel) if os.path.exists(kernel) else None

    # From clean, or `make` answers "nothing to be done" and this row passes
    # without having built anything at all - which is the failure mode a build
    # gate can least afford.
    for _, arts, gens in TARGETS:
        for rel in arts + gens:
            p = os.path.join(ROOT, rel)
            if os.path.exists(p):
                os.remove(p)

    for target, arts, gens in TARGETS:
        r = make(target)
        if not check(r.returncode == 0, "`make %s` failed" % target,
                     why="a C package no longer assembles; the tail of the "
                         "build is below.\n" + "\n".join(
                             r.stdout.strip().splitlines()[-12:])):
            continue
        for rel in arts:
            check(os.path.exists(os.path.join(ROOT, rel)),
                  "`make %s` left no %s" % (target, rel),
                  why="the target reported success without producing its "
                      "package; a rule stopped short of os88pkg.py")

        # (3) the invariant itself. A bare `extern` reaching nasm is the
        # two-release outage; a commented one is what cc8086.py leaves.
        for rel in gens:
            p = os.path.join(ROOT, rel)
            if not os.path.exists(p):
                continue
            bad = [n for n, line in enumerate(open(p), 1)
                   if line.strip().lower().startswith("extern")]
            check(not bad,
                  "%s has %d bare `extern` directive(s) (first at line %s)"
                  % (rel, len(bad), bad[0] if bad else "-"),
                  why="nasm reads `extern X` for an already-defined X as a "
                      "redefinition and refuses it unless X is 0, so every "
                      "package with this in it fails to assemble "
                      "(SPEC.md 73.1). tools/cc8086.py comments them out.")

    # (4) the negative control.
    negative_control()

    if before is not None:
        check(os.path.exists(kernel) and md5(kernel) == before,
              "build/kernel.bin changed while building the C targets",
              why="no C rule may touch the shipped kernel; t_buildmatrix.py "
                  "makes the same check for the same reason",
              got=md5(kernel) if os.path.exists(kernel) else "gone",
              want=before)

    print("t_ctoolchain: %d C packages built from clean" % len(TARGETS))
    done("t_ctoolchain")


def negative_control():
    """A call to an `os88_*` with no thunk must fail the build, naming it.

    Built the way a package is - the runtime in front of the compiled C, one
    nasm job - because that is the only arrangement in which the question
    means anything.
    """
    tmp = tempfile.mkdtemp(prefix="ctool-")
    try:
        with open(os.path.join(tmp, "neg.c"), "w") as f:
            f.write('#include "os88.h"\n'
                    'extern int os88_no_such_thunk(int a);\n'
                    'void *os88_main(void) '
                    '{ return (void *)os88_no_such_thunk(1); }\n'
                    'void os88_paint(void *win) { (void)win; }\n')
        with open(os.path.join(tmp, "neg.asm"), "w") as f:
            f.write("%define CC_PKG_NAME 'NEG'\n"
                    '%include "cc/crt0.asm"\n'
                    '%include "neg.gen.asm"\n'
                    "    CC_IMAGE_END\n")

        inc = os.path.join(ROOT, "build/cc/SmallerC/v0100/include")
        env = dict(os.environ)
        env["PATH"] = os.path.dirname(SMLRCC) + os.pathsep + env.get("PATH", "")
        r = subprocess.run([SMLRCC, "-tiny", "-S", "-SI", inc, "-I", inc,
                            "-I", os.path.join(ROOT, "apps/cc"),
                            os.path.join(tmp, "neg.c"),
                            "-o", os.path.join(tmp, "neg.raw.asm")],
                           cwd=ROOT, env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
        if not check(r.returncode == 0,
                     "the negative control would not compile", got=r.stdout):
            return
        r = subprocess.run(["python3", "tools/cc8086.py",
                            os.path.join(tmp, "neg.raw.asm"),
                            "-o", os.path.join(tmp, "neg.gen.asm"), "--quiet"],
                           cwd=ROOT, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
        if not check(r.returncode == 0,
                     "cc8086.py refused the negative control", got=r.stdout):
            return
        r = subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                            "-I", tmp + os.sep, "-o", os.path.join(tmp, "neg.bin"),
                            os.path.join(tmp, "neg.asm")],
                           cwd=ROOT, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
        check(r.returncode != 0,
              "a call to an `os88_*` with no thunk ASSEMBLED",
              why="cc8086.py is giving external references an address instead "
                  "of dropping them. `X equ 0` assembles, packages and runs, "
                  "and compiles the missing call into a jump to offset 0 of "
                  "the package's own segment - its header (SPEC.md 73.1).")
        check("os88_no_such_thunk" in r.stdout,
              "the build failed without naming the missing function",
              why="naming it is the whole diagnostic; SPEC.md 73.1 calls this "
                  "the one failure in the C path that says its own cause",
              got=r.stdout.strip()[:300])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
