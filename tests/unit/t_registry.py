#!/usr/bin/env python3
"""Every test in tests/ is registered in a tier, or says why it is not.

    python3 tests/unit/t_registry.py

This is the check that keeps the suite from going back to what it was.  There
were ninety test scripts here and no list of them, so running "the tests"
meant remembering which ones existed - and the ones nobody remembered were
exactly the ones that had stopped working.  A registry fixes that once; this
row is what stops it rotting, because a test added next month is invisible to
`tools/os88test.py` unless somebody puts it in `tests/suite.py`, and nothing
would have said so.

So: every runnable `tests/*.py` must be either

  * a row in `tests/suite.py` (any tier - `soak` is a real answer and costs
    nobody any budget), or
  * in UNREGISTERED below WITH A REASON.

The reason matters more than the exemption.  "Needs a build prerequisite" and
"needs hardware nothing here has" are facts about the test that a reader
should be able to find without running it; an unexplained exemption is how a
test that has simply broken gets filed as one that was never meant to run.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tests"))
from harness import check, done                           # noqa: E402
import suite                                              # noqa: E402

# builds=True with no `make` in the row, and why. A row here WRITES build/ by
# some other route, so the flag is right and the reverse check below would
# otherwise ask for it to be dropped. Same rule as UNREGISTERED: the reason is
# the point, and an unexplained entry is how a stale flag survives.
BUILDS_WITHOUT_MAKE = {
    "fdlgthumb": "a KNOB gate, so it may not call the fixture helper at all - "
                 "that runs `make`, and the Makefile's VIDSTAMP rule removes "
                 "build/kernel.bin whenever the knob set differs, which would "
                 "delete the very kernel the row is about to test. It builds "
                 "build/fdthumb.img with nasm and os88pkg.py directly instead, "
                 "which is still writing build/ under anything beside it",
}

# Not registered, and why. Keep the reason specific and true.
UNREGISTERED = {
    # --- library and support code, not tests ---
    "dispcells.py": "the CELLS-not-calls counter two gates share (SPEC.md "
                    "11.3.3), not a test",
    "os88qemu.py": "the teardown every QEMU launcher registers, written once "
                   "rather than thirteen times - library, not a test. What "
                   "checks it is `t_qemuown`, which asserts every launcher "
                   "calls it (docs/HANDOFF-SOAK-FINDINGS.md B9)",
    "benchlib.inc": "a benchmark library, not a test",
    "trklog.inc": "tracker's logging build, %included by apps/tracker",
    "trkscrl.inc": "tracker's scroll-gate build, %included by apps/tracker",
    "npbench.inc": "a benchmark body, %included",
    "harness.py": "tests/unit/'s check library - check(), eq(), done() - "
                  "imported by every t_*.py there, not a test",

    # --- need a build prerequisite the default build does not make ---
    "brclick.py": "needs `make browsertest` (build/brtest360.img)",
    "brfetch.py": "needs `make browsertest` (build/brtest360.img)",
    "brlink.py": "needs `make browsertest` (build/brtest360.img)",
    "brnav.py": "needs `make browsertest` (build/brtest360.img)",
    "brscroll.py": "needs `make browsertest` (build/brtest360.img)",
    "brtable.py": "needs `make browsertest` (build/brtest360.img)",
    "brreload.py": "needs `make browsertest` (build/brtest360.img)",
    "brtest.py": "needs `make browsertest` (build/brtest360.img)",
    "brtoolbar.py": "needs `make browsertest` (build/brtest360.img)",
    "ethernet.py": "needs `make ethertest` and QEMU - MartyPC has no NIC "
                   "(SPEC.md 72.9)",
    "ethcfg.py": "needs `make ethertest` and QEMU",
    "netprof.py": "needs QEMU, and it leaves ETHPROF=1 KNOB builds of "
                  "ether.drv and the ethertest disk in build/ - a suite row "
                  "running before the next `make` would test the wrong "
                  "driver (SPEC.md 72.15)",
    "ftpdpix.py": "needs `make ftpdtest`, QEMU, and TWO builds - it boots the "
                  "reference face (FTPDSLOW=1) and the optimised one and "
                  "compares window pixels (SPEC.md 77.14)",
    "ftpd.py": "needs `make ftpdtest` and QEMU with ETHFWD=1 - the FTP server "
               "LISTENS, so the client has to reach INTO the guest, and "
               "MartyPC has no NIC at all (SPEC.md 77)",
    "rczex.py": "needs the RunCPM fetch (`make runcpm-src`) and the C toolchain",
    "rczex_ocr.py": "needs the RunCPM fetch and an OCR dependency",
    "proxytest.py": "drives tools/os88proxy.py against a live network",
    "proxyguitest.py": "drives the proxy GUI, needs a display",
    "socktest.py": "needs `make socktest` and QEMU networking",
    "telnet.py": "needs QEMU networking",

    # --- A/B gates: each needs a SECOND kernel built with a knob, so it is a
    #     two-build session rather than a row (the knob itself is kept alive
    #     by t_buildmatrix; this is the behaviour half) ---
    "heapsame.py": "A/B gate - needs `make HEAPCOMPACT=0` as a reference build",
    "swcolsame.py": "A/B gate - needs `make NOCOLFAST=1` as a reference build",
    # pkgthumb.py IS registered (four rows). Its `frotz` mode is not one of
    # them: a Z-machine interpreter needs a STORY, and the two ways to get one
    # are a network fetch or the Inform compiler, neither of which is a build
    # dependency anybody has (SPEC.md 13.10.7).
}


def _invokes_make(path):
    """Does this test shell out to `make`? Read, not guessed at.

    A quoted `make` as the first element of an argv list is one spelling, and
    the fixture helper in tools/ is the other - and the second is the one that
    got away. That helper runs `make` for a test's own scratch disk, so the
    literal lives one level down, and THIRTEEN registered rows called it from
    the shareable emulator lane with builds=False: exactly the "rewrites the
    tree another row is reading" this check exists to stop, invisible to it
    because the grep was pointed at the caller.

    So the second half keys on the IMPORT and not on the call. A call can be
    `need(DISK)` as easily as `need("build/x.img")` - two of the thirteen are
    - and requiring a quoted first argument found eleven of them and left two
    looking like rows that declare builds=True and build nothing.

    A test that grows a subtler one still - a shell string, a variable - has
    to be caught by its author; the point of the gate is the ordinary case,
    which is the one that gets forgotten.

    THE EXAMPLE THAT WOULD GO HERE IS DELIBERATELY NOT WRITTEN OUT: a docstring
    quoting the pattern matches it, and this file's own row then fails the
    check it implements. That is not a hypothetical - it is what the first
    version of this function did.
    """
    try:
        with open(path) as f:
            body = f.read()
    except OSError:
        return False
    # The second half is spelled with `\s+` between every word ON PURPOSE, so
    # that this file does not contain the import it looks for and match itself.
    # Written the obvious way it did - twice, in one afternoon: the make-detector
    # above says the same thing about its own docstring, and the pieces were
    # first joined with a NON-raw `'fixture\\b'`, where `\\b` is a BACKSPACE and
    # not a word boundary, so the pattern silently matched nothing at all.
    return bool(re.search(r'\[\s*"make"|"make"\s*,', body)
                or re.search(r'^\s*(?:from\s+os88fixture\s+import'
                             r'|import\s+os88fixture\b)', body, re.M))


def main():
    reg = {}
    for r in suite.rows():
        for part in r.cmd:
            if part.startswith("tests/") and part.endswith(".py"):
                reg[os.path.basename(part)] = r.name

    # BOTH directories. This walked the top level only, so a t_*.py added to
    # tests/unit/ with no row was invisible to the one gate meant to see it -
    # the same failure one level down. Names are unique across the two (a
    # unit test is t_<x>.py, a script is <x>.py), so one map serves both.
    tdir = os.path.join(ROOT, "tests")
    udir = os.path.join(tdir, "unit")
    found = 0
    walk = [(tdir, f, "tests/%s") for f in sorted(os.listdir(tdir))] + \
           [(udir, f, "tests/unit/%s") for f in sorted(os.listdir(udir))]
    for d, f, rel in walk:
        if not f.endswith((".py", ".inc")) or f == "suite.py":
            continue
        found += 1
        if f in reg or f in UNREGISTERED:
            continue
        check(False, "%s is not registered" % (rel % f),
              "add a row to tests/suite.py - `soak` costs no budget and makes it "
              "discoverable and runnable - or put it in UNREGISTERED here with a "
              "reason. A test nobody can find is a test nobody runs",
              got="not in suite.py and not exempted", want="a row, or a reason")
    check(found > 100, "the walk found the tests (%d files)" % found,
          "a listing that came back short would pass every row above")

    # ...and the exemption list must not outlive the files it names.
    for f in UNREGISTERED:
        check(os.path.exists(os.path.join(tdir, f))
              or os.path.exists(os.path.join(udir, f)),
              "UNREGISTERED names tests/%s, which exists" % f,
              "the file is gone - drop the row, or a stale exemption silently "
              "exempts nothing")
    for f in UNREGISTERED:
        check(f not in reg, "tests/%s is exempted OR registered, not both" % f,
              "two answers to one question drift apart - drop the UNREGISTERED row")

    # ...and a row that BUILDS must say so, because the runner acts on it.
    #
    # Emulator instances are isolated now (docs/MARTYPC-DEBUG.md), so a marty
    # row no longer has to run alone - `tools/os88test.py --marty-jobs` puts
    # several in one lane. What still cannot share is `build/`: a row that
    # shells out to `make` rewrites the tree another row is reading. That is
    # what `builds=True` marks, and it is CHECKED here rather than trusted,
    # because getting it wrong produces the worst kind of suite - one that
    # fails one run in five, in a different row each time, for no visible
    # reason. Adding a `make` to a test is therefore a decision that shows up
    # as a failing gate rather than as a flake three weeks later.
    for r in suite.rows():
        scripts = [c for c in r.cmd
                   if c.startswith("tests/") and c.endswith(".py")]
        makes = [c for c in scripts if _invokes_make(os.path.join(ROOT, c))]
        if makes and not r.builds:
            check(False, "row %s shells out to make and is not builds=True" % r.name,
                  "%s invokes `make`, so this row rewrites build/ under any row "
                  "running beside it. Mark the row builds=True and the runner "
                  "gives it the tree to itself" % ", ".join(makes),
                  got="builds=False", want="builds=True")
        elif r.builds and not makes and r.name not in BUILDS_WITHOUT_MAKE:
            check(False, "row %s is builds=True and builds nothing" % r.name,
                  "the flag costs the row its parallelism, so a stale one is a "
                  "slower suite for no reason. Drop it, or say here why the row "
                  "writes build/ without a `make`",
                  got="builds=True", want="a `make` in %s"
                       % (", ".join(scripts) or "its command"))

    print("t_registry: %d files in tests/, %d registered, %d exempted, "
          "%d build" % (found, len(reg), len(UNREGISTERED),
                        sum(1 for r in suite.rows() if r.builds)))
    done("t_registry")


if __name__ == "__main__":
    main()
