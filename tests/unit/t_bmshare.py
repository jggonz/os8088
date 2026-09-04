#!/usr/bin/env python3
"""The three variables t_buildmatrix.py builds with change NO kernel byte.

    python3 tests/unit/t_bmshare.py

`ICODIR=`, `NOOVLCHK=1` and `NOKERNSIZE=1` exist for one caller - the knob
matrix - and each of them removes work from a build:

  ICODIR=build    take associco.inc and the four packages it is reduced from
                  out of the default build instead of rebuilding them
  NOOVLCHK=1      do not run the overlay gate in this build (the matrix runs
                  it once, over the same source, as a check of its own)
  NOKERNSIZE=1    do not re-assemble the finished kernel for a size report

Removing work from a build is exactly the change that can be wrong in silence.
None of the three is in `$(KNOBS)` or in the stamp, because none of them is
meant to reach a byte - and "meant to" is what this row turns into a fact: it
builds ONE knob kernel both ways and compares the images.

THE SECOND HALF IS THE EXCLUSION, and it is the one that would rot.  `ICODIR`
is safe only for a knob that does not reach a PACKAGE build, and two of the
matrix's knobs do: SBDRAGOFF= and SBRATE= feed `$(PKGSBDEF)`, which is on
notepad's own nasm line.  t_buildmatrix derives that list from the Makefile
rather than keeping a copy, and this checks both ends of the derivation - that
it still names those two, and that the difference it is protecting is real.
If notepad ever stops changing under the knob, this fails and somebody re-reads
the exclusion rather than carrying it for ever.
"""
import hashlib
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402
# ...and the matrix's own derivation, imported rather than restated: a second
# copy of the rule is a second thing to keep in step (importing runs no build -
# t_buildmatrix does its work under `if __name__ == "__main__"`).
from t_buildmatrix import PKG_VARS, NOWASTE, shares       # noqa: E402


def md5(p):
    with open(p, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def build(tag, variables, target="kernel.bin"):
    """One out-of-tree build; returns (ok, path, tail-of-output)."""
    out = os.path.join(ROOT, "build", "bms-" + tag)
    shutil.rmtree(out, ignore_errors=True)
    cmd = ["make", "BUILD=" + os.path.relpath(out, ROOT)] + variables + \
          [os.path.relpath(os.path.join(out, target), ROOT)]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=600)
    p = os.path.join(out, target)
    return (r.returncode == 0 and os.path.exists(p), p,
            "\n".join((r.stdout + r.stderr).strip().splitlines()[-6:]))


def main():
    # 1. THE KERNEL IS THE SAME KERNEL. VIDEO=cga is a row of the matrix and a
    #    knob that reaches the kernel and nothing else, so it is the case the
    #    sharing is for.
    slow_ok, slow, slow_err = build("slow", ["VIDEO=cga"])
    fast_ok, fast, fast_err = build("fast", ["VIDEO=cga", "ICODIR=build"] + NOWASTE)
    check(slow_ok, "the reference knob build still builds",
          "without it there is nothing to compare against", got=slow_err, want="exit 0")
    check(fast_ok, "the same knob build with ICODIR/NOOVLCHK/NOKERNSIZE builds",
          "these are what tests/unit/t_buildmatrix.py passes on every row",
          got=fast_err, want="exit 0")
    if slow_ok and fast_ok:
        check(md5(slow) == md5(fast),
              "the two are byte-identical",
              "if they are not, the matrix has been assembling a DIFFERENT "
              "kernel from the one a plain `make <knob>` produces, and every "
              "row of it has been answering a question nobody asked",
              got=open(fast, "rb").read(), want=open(slow, "rb").read())
    for d in ("bms-slow", "bms-fast"):
        shutil.rmtree(os.path.join(ROOT, "build", d), ignore_errors=True)

    # 2. THE EXCLUSION STILL READS, and still names the two knobs it is for.
    check(PKG_VARS == {"SBDRAGOFF", "SBRATE"},
          "the package-knob list derived from $(PKGSBDEF) is the expected pair",
          "t_buildmatrix reads this out of the Makefile so it cannot go stale "
          "there; this is the other end - a NEW name here is fine and wants "
          "this line updated, an EMPTY set means the derivation broke and "
          "every row would start sharing packages it must not",
          got=sorted(PKG_VARS), want=["SBDRAGOFF", "SBRATE"])
    check(not shares(["SBDRAGOFF=1"]) and not shares(["SBRATE=2"]),
          "a row whose knob reaches a package does NOT share",
          "sharing there would stop apps/notepad being assembled under the "
          "knob at all - green, and one assembly gate poorer")
    check(shares(["VIDEO=cga"]) and shares(["BAND=1"]),
          "a row whose knob reaches only the kernel DOES share",
          "if nothing shares, the matrix is paying the old price and this "
          "file is measuring nothing")

    # 3. ...and the difference it protects is REAL: notepad really does come
    #    out different under the knob.
    ok, own, err = build("pkg", ["SBDRAGOFF=1"], "notepad.o88")
    check(ok, "notepad builds under SBDRAGOFF=1", got=err, want="exit 0")
    shipped = os.path.join(ROOT, "build", "notepad.o88")
    if ok and os.path.exists(shipped):
        check(md5(own) != md5(shipped),
              "notepad under SBDRAGOFF=1 differs from the default build's",
              "this is WHY those rows may not share. If the two ever match, "
              "$(PKGSBDEF) has stopped reaching notepad and the exclusion is "
              "carrying nothing - re-read it rather than deleting it, because "
              "the same variable is on four other packages' nasm lines",
              got=md5(own), want="anything but " + md5(shipped))
    shutil.rmtree(os.path.join(ROOT, "build", "bms-pkg"), ignore_errors=True)

    done("t_bmshare")


if __name__ == "__main__":
    main()
