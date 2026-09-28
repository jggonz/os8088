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
                 "fdthumb.img with nasm and os88pkg.py directly instead, "
                 "which is still writing the tree the run is reading - "
                 "through os88build.at, so it writes where it reads "
                 "(docs/plans/SOAK-PARALLEL.md 14.2), but into a shared directory "
                 "either way",
}

# Not registered, and why. Keep the reason specific and true.
UNREGISTERED = {
    # --- the rows of a RETIRED package (SPEC.md 20.16, apps/RETIRED.txt) ---
    # These two drive PACMAN.O88, which `all` no longer builds (SPEC.md
    # 89.12), so a registered row would need `wants=("build/pacman.o88",)`
    # and would spend an emulator boot on a program that ships nowhere. They
    # are KEPT rather than deleted for the same reason the source and SPEC.md
    # 89 are: `make pacman` still builds the package, so the record can be
    # run by whoever wants to look at it - and a retirement that deleted the
    # only way to check the thing being retired is a claim nobody can audit.
    # If the decision is ever reversed, these go back in a tier; while it
    # stands, they are documentation with a shebang.
    "pacman.py": "drives PACMAN.O88, which is RETIRED - `make pacman` still "
                 "builds it and this still runs, but no tier spends an "
                 "emulator boot on a package that ships nowhere",
    "t_pacman.py": "the Atari maze's reachability and sprite check, for the "
                   "RETIRED PACMAN.O88. Host-side and 0.1s, so the cost is "
                   "not the argument - it is that `all` no longer builds the "
                   "package it is about, and a fast row every contributor "
                   "pays for may not be about a program that ships nowhere "
                   "(docs/WRITING-TESTS.md 2.1)",

    # --- library and support code, not tests ---
    "dispcells.py": "the CELLS-not-calls counter two gates share (SPEC.md "
                    "11.3.3), not a test",
    "pxslib.py": "PIXELSTEIN 3D's guest reader (SPEC.md 97.12) - the "
                 "package's symbols out of nasm's map, its window and part-0 "
                 "segment, the loader's handoff words, the pinned scenes "
                 "poked and the arrays and shadow read back - imported by "
                 "tests/pixelstein.py and tests/pxssim.py, and not a test: "
                 "dispcp.py's lesson one entry up, a library registered as a "
                 "row is a row that cannot fail",
    "dispcp.py": "the Control Panel's Display page driven from a script - "
                 "the shared one, imported by 104 files in this directory and "
                 "the most-reused thing in it. It was REGISTERED as a soak "
                 "row until it was noticed reporting `ok` in 0.1s against 60 "
                 "declared: 22 function definitions and no call, so the row "
                 "booted nothing, asserted nothing and could never fail. A "
                 "green row that tests nothing is worse than B4's three rows "
                 "that failed where they meant to skip, because nobody "
                 "investigates a pass",
    "dosmap.py": "symbol offsets for the DOS box and for a probe running "
                 "INSIDE it - library, not a test. It exists because "
                 "dispapps._map's `defines` argument means exactly ONE thing "
                 "(-DAPP_SMALL, compared against build/smallapp/), and its "
                 "source path is apps/<app>/<app>.asm, which "
                 "tests/dostrap/dospkt.asm is not. Both differences fail as a "
                 "message about the wrong subject. tests/dosxlat.py is the "
                 "registered row that reads through it",
    "os88qemu.py": "the teardown every QEMU launcher registers, written once "
                   "rather than thirteen times - library, not a test. What "
                   "checks it is `t_qemuown`, which asserts every launcher "
                   "calls it",
    "benchlib.inc": "a benchmark library, not a test",
    "trklog.inc": "tracker's logging build, %included by apps/tracker",
    "trkscrl.inc": "tracker's scroll-gate build, %included by apps/tracker",
    "npbench.inc": "a benchmark body, %included",
    "harness.py": "tests/unit/'s check library - check(), eq(), done() - "
                  "imported by every t_*.py there, not a test",
    "mkclick.py": "a GENERATOR, not a test: it writes build/click.mod - a "
                  "metronome module for judging A/V sync by eye and ear - "
                  "and asserts nothing. It was REGISTERED as a soak row "
                  "declaring 10s and reported `ok` in 0.0s, which is "
                  "dispcp.py's failure exactly (a green row that tests "
                  "nothing, and nobody investigates a pass); it also WRITES "
                  "build/, which a row under a frozen run may not "
                  "(docs/plans/SOAK-PARALLEL.md 14.2). Run it by hand when a field "
                  "sync question needs the module",

    "skiesprof.py": "an INSTRUMENT, not a test: it breaks CLEAR SKIES' frame "
                    "down IN FLIGHT - five moving profiles, every stage "
                    "bracketed at its CALL SITE so the accounting is exact "
                    "and adds to 99.9% of the loop - and asserts nothing "
                    "(SPEC.md 88.12.1). It is the only one of the three that "
                    "measures a frame which had to step the flight model, "
                    "redraw a changed panel field or refill a rolled horizon; "
                    "skiesperf and skiescount both pause the world",

    "skiesink.py": "an INSTRUMENT, not a test: it checks CLEAR SKIES' "
                   "one-frame INK-INSIDE-ITS-SPAN invariant, which "
                   "docs/FIELD-NOTES.md 41 made true and tests/skiesspan.py "
                   "is the registered gate for. It takes a ROLL SWEEP and a "
                   "frame count on the command line and is where a report of "
                   "surviving ink gets diagnosed - the pixels either side of "
                   "the leak, three rows deep, printed as a picture. The row "
                   "asserts; this explains",
    "skiescount.py": "an INSTRUMENT, not a test: it COUNTS CLEAR SKIES' faces "
                     "and edges and prices each term by ADDING it - every arm "
                     "draws the identical picture, where skiesperf.py's "
                     "patch-out takes a stage's consequences with it - and "
                     "asserts nothing (SPEC.md 88.11.1, 88.4.2.1). Needs "
                     "`make skiesprobe`; it is where "
                     "docs/plans/SKIES-FRAME-PLAN.md's numbers came from",

    "skiesperf.py": "an INSTRUMENT, not a test: it prices CLEAR SKIES' frame "
                    "on MartyPC to the cycle - a breakpoint on cs_render and "
                    "each drawing stage patched out for its delta, on a scene "
                    "pinned by poke - and asserts nothing (SPEC.md 88.12). It "
                    "is where every number in 88.12 came from",
    "tankperf.py": "an INSTRUMENT, not a test: it prices TANK ATTACK's frame "
                   "on MartyPC to the cycle - a breakpoint on tk_render, and "
                   "each drawing stage patched out for its delta - and "
                   "asserts nothing (SPEC.md 85.3.4). It is where every "
                   "number in 85.3.4 to 85.3.6 came from, kept because the "
                   "two obvious measurements were both wrong: a sampled "
                   "profile read the walk at twice its share, and counting "
                   "frames over guest seconds is quantised to a whole frame",

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
    "socktest.py": "needs `make socktest`, and it is MINUTES: the cable is "
                   "stepped a nibble at a time, so a page fetch is ~13 of "
                   "them. **AND IT DOES NOT NEED QEMU NETWORKING**, which is "
                   "what this reason said - it runs under MartyPC with "
                   "tests/lptlink/partner.py as the far end and real host "
                   "sockets behind that, no NIC anywhere. Measured while "
                   "tests/doscable.py was being written, which is the same "
                   "arrangement one layer up and IS registered: socktest "
                   "fetches its page correctly and then fails its own "
                   "handle-leak assertion with `8 of 4 handles free after the "
                   "close`, which reads as a bug in that assertion rather "
                   "than in the wire. Nobody has been running it to notice",

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
    # A `make` CARRYING `BUILD=` IS NOT ONE OF THESE. It names a destination
    # of its own, so it cannot rewrite the shared tree - which is the only
    # thing this flag is about. `tests/unit/t_bmshare.py` has done that since
    # it was written and was marked builds=True anyway, because the detector
    # looked for the word `make` and not for where the output went.
    for argv in re.findall(r'\[[^\[\]]*"make"[^\[\]]*\]', body):
        if "BUILD=" not in argv:
            return True
    if re.search(r'"make"\s*,', body) and "BUILD=" not in body:
        return True
    return bool(re.search(r'^\s*(?:from\s+os88fixture\s+import'
                          r'|import\s+os88fixture\b)', body, re.M))


def _private_build(path):
    """Does this test build into a PRIVATE tree (tools/os88build.py)?

    A third spelling, and the one that means the OPPOSITE of the two above.
    `os88build.tree()` runs `make BUILD=<a directory of its own>`, so the row
    really does invoke make - and it does not touch `build/`, which is the
    only thing `builds=True` is about. A row that spells `BUILD=` itself
    counts too: `t_bmshare` and `t_buildmatrix` were both doing this before
    os88build existed, and the first of them was marked `builds=True` for
    years because the detector looked for the word `make` rather than for
    where the output went. A row like that must be builds=FALSE,
    or the flag puts it back in the one-at-a-time lane for a hazard it no
    longer has.

    Checked in both directions below, because both mistakes are silent: a
    private builder marked `builds=True` costs the soak its parallelism for
    nothing, and a shared-tree builder marked False is the corruption the
    original check exists to stop.
    """
    try:
        with open(path) as f:
            body = f.read()
    except OSError:
        return False
    # **`tree()` OR `BUILD=`, NEVER THE BARE IMPORT.** Importing os88build is
    # not building anything: `os88build.at()` is a PATH RESOLVER and rows
    # import it to spell `build/x.img` correctly under a frozen run
    # (docs/plans/SOAK-PARALLEL.md 14.2) - eight of them do, and none of those
    # builds a tree. Keying on the import therefore told `fdlgthumb` to drop
    # a flag it genuinely needs: that row builds its fixture with nasm and
    # os88disk directly and writes whichever tree the run reads.
    return bool(re.search(r'\bos88build\.tree\s*\(', body)
                or re.search(r'\b_B\.tree\s*\(', body)
                or "BUILD=" in body)


def _hand_defines(path):
    """A module-level `DEFINES = ("X", ...)` beside a private tree.

    **DERIVED, NEVER RESTATED** (docs/plans/SOAK-PARALLEL.md 8.4). A knob's
    make VARIABLE and its nasm DEFINE are not the same string - `VGADIRTY=1`
    compiles `-DVGA_DIRTY` - so `os88build` asks `make -n` for the mapping and
    `Tree.apply()` sets it as os88sym's module default. A row that then types
    the set out again has made a second copy of that mapping, and three rows
    had: `["DISK_COUNTERS"]` twice and `("BOOT_PROFILE", "MOU_DIAG")` once,
    against a derived set carrying `KERN_BIG` and `KERN_KNOB` beside each.

    It ASSEMBLED, which is exactly why it survived - but as a different cache
    key from the one `apply()` had just set, so each row re-assembled the
    whole kernel a second time to reach the same answer. `fddpark` is the row
    that died of this mirror going stale, and it is the reason the check is
    here rather than in a comment.

    An empty tuple is the converted shape (`DEFINES = ()`, filled from
    `t.defines`), so only a NON-EMPTY literal is a finding.
    """
    try:
        with open(path) as f:
            body = f.read()
    except OSError:
        return False
    return bool(re.search(r'^DEFINES\s*=\s*[\[(]\s*["\']', body, re.M))


def main():
    reg = {}
    for r in suite.rows():
        for part in r.cmd:
            if part.startswith("tests/") and part.endswith(".py"):
                reg[os.path.basename(part)] = r.name

    # ONE NAME, ONE ROW, across every tier. `tools/os88soak.py` journals a
    # run BY NAME and `--resume` excludes by name, so two rows sharing one
    # made the second's verdict vanish: the fast `paccman` reported ok in
    # 0.0s, the soak `paccman` FAILED an hour later, and `status` and
    # done.txt both said ok - the failure was only in run.log's tail.
    seen = {}
    for r in suite.rows():
        if r.name in seen:
            check(False, "row name %r is registered twice (%s and %s)"
                  % (r.name, seen[r.name], r.tier),
                  "the soak journals and resumes BY NAME, so the second "
                  "row's verdict is lost - rename one",
                  got="two rows", want="one")
        seen[r.name] = r.tier

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
        priv = [c for c in scripts if _private_build(os.path.join(ROOT, c))]
        # A ROW THAT BUILDS ONLY PRIVATELY MUST NOT BE builds=True. It runs
        # `make` into a directory of its own, so it cannot rewrite the tree
        # under anything - and the flag would cost it the shared lane for a
        # hazard it has given up. This is the check that lets the flag
        # actually go away as rows are converted, rather than being dropped by
        # hand and drifting back.
        #
        # `not makes` is the important half: a row may do BOTH, and
        # `t_buildmatrix` does - it builds 81 knob kernels out of tree and
        # still asks the shared tree for `build/associco.inc`. That row keeps
        # the flag, and correctly: it also runs itself at -j4, so it wants the
        # box rather than a share of it.
        # **A DECLARED ROW IS THE THIRD ANSWER, and it is not checked here.**
        # `makes and wants and not builds` is the converted shape: the row
        # still reaches `make`, and the runner has already built what it
        # asks for, so the call is a no-op and the row can share the lane.
        # Whether the declaration is COMPLETE cannot be settled by reading the
        # script - `need(DISK)` and `need(a.apps)` are as common here as a
        # literal path - so os88fixture answers it instead, exactly, at the
        # call: under the runner an undeclared target is an error and not a
        # build. A wrong `wants=` therefore fails the row that owns it rather
        # than the run beside it, which is the property this flag is for.
        # A `wants=` ENTRY IS A PATH, NEVER A MAKE TARGET, and getting that
        # wrong is a row that SKIPS FOR EVER. tools/os88test.py's prebuild
        # runs `make <entry>` and then asks `os.path.exists(ROOT/<entry>)`,
        # so a phony target builds fine, exits 0, and still reports as an
        # artefact that "would not build" - after which every row wanting it
        # skips. `btngesture` landed with `wants=("marty",)`, which is the
        # emulator CAPABILITY and belongs in `needs=`: it never ran once, and
        # nobody investigates a skip (docs/WRITING-TESTS.md 1). The registry's
        # own comment on `wants` already says "Paths, not make targets"; this
        # is that sentence made checkable.
        for w in r.wants:
            check("/" in w, "row %s wants `%s`, which is not a path" % (r.name, w),
                  "the runner builds a wants= entry with `make <it>` and then "
                  "tests os.path.exists on it, so a phony target can never "
                  "satisfy the check and the row skips on every run. An "
                  "emulator or toolchain requirement goes in needs=; a build "
                  "artefact goes here as the path the row opens",
                  got=repr(w), want="a build/... path, or needs=(...)")
        if priv and not makes and r.builds:
            check(False, "row %s builds PRIVATELY and is still builds=True"
                  % r.name,
                  "%s uses tools/os88build.py, which builds into a tree of "
                  "its own and never writes build/. The flag puts it back in "
                  "the one-at-a-time lane for a hazard it no longer has"
                  % ", ".join(priv),
                  got="builds=True", want="builds=False")
        elif makes and not priv and not r.builds and not r.wants:
            check(False, "row %s shells out to make and is not builds=True" % r.name,
                  "%s invokes `make`, so this row rewrites build/ under any row "
                  "running beside it. Declare what it builds in `wants=` - the "
                  "runner then builds it BEFORE any row starts and "
                  "os88fixture.need does nothing at run time - or mark the row "
                  "builds=True and the runner gives it the tree to itself"
                  % ", ".join(makes),
                  got="builds=False", want="builds=True or wants=(...)")
        for c in priv:
            if _hand_defines(os.path.join(ROOT, c)):
                check(False,
                      "row %s hand-types the nasm defines its tree already "
                      "knows" % r.name,
                      "%s builds a private tree, so `Tree.apply()` has "
                      "already derived the -D set from `make -n` and set it "
                      "as os88sym's default. A literal `DEFINES = (...)` "
                      "beside it is a second copy of the variable-to-define "
                      "mapping, which is what fddpark died of; take "
                      "`t.defines` instead" % c,
                      got="DEFINES = (...)", want="DEFINES = t.defines")
        if r.builds and not makes and not priv \
                and r.name not in BUILDS_WITHOUT_MAKE:
            check(False, "row %s is builds=True and builds nothing" % r.name,
                  "the flag costs the row its parallelism, so a stale one is a "
                  "slower suite for no reason. Drop it, or say here why the row "
                  "writes build/ without a `make`",
                  got="builds=True", want="a `make` in %s"
                       % (", ".join(scripts) or "its command"))

    # ...AND NOT ONE OF THEM MAY NAME AN ABSOLUTE CHECKOUT PATH.
    #
    # A script that hard-codes the tree it was written in does not fail in a
    # git worktree - it SILENTLY TESTS THE OTHER TREE. Measured: run from a
    # worktree, `tests/dispapp.py` reported "no MartyPC run directory at
    # /home/user/os8088/build/martypc/run", the MAIN checkout, and on a box
    # where that tree HAS been built (the normal case) it would have booted
    # that kernel and those floppies while reporting on the worktree.
    # `tests/arkpuwipe.py` had already written the consequence down - os88sym
    # re-assembles ROOT/kernel/kernel.asm and compares it against
    # ROOT/build/kernel.bin, so a literal ROOT answers about a different
    # kernel from the image being booted - and fixed itself alone, which is
    # exactly how one file's note fails to reach the other thirty-seven.
    #
    # It went unnoticed because nothing here ran from a worktree until
    # parallel agents did; the literal is correct in the checkout it was
    # written in, and every one of these scripts passed there.
    #
    # The check is the PATH SHAPE and not one project's directory: any
    # absolute path into a home or checkout root inside a string literal.
    # A comment may name one - two files explain the trap in prose.
    home = re.compile(r"""["'](/(?:home|Users)/[^"'\n]*)["']""")
    _td = os.path.join(ROOT, "tests")
    for name in sorted(os.listdir(_td)) + \
            ["unit/" + n for n in sorted(os.listdir(os.path.join(_td, "unit")))]:
        if not name.endswith(".py"):
            continue
        path = os.path.join(_td, name)
        try:
            src = open(path, encoding="utf-8").read()
        except OSError:
            continue
        hits = []
        for n, line in enumerate(src.split("\n"), 1):
            if line.lstrip().startswith("#"):
                continue                                  # prose may name one
            m = home.search(line)
            if m:
                hits.append((n, m.group(1)))
        for n, lit in hits:
            check(False,
                  "tests/%s:%d hard-codes an absolute checkout path" % (name, n),
                  "a literal root is right in the checkout it was written in "
                  "and WRONG in a git worktree, where it does not fail - it "
                  "silently reads the OTHER tree's build/ and kernel. Derive "
                  "it: `_OS88_ROOT = os.path.dirname(os.path.dirname("
                  "os.path.abspath(__file__)))`, then os.path.join off that",
                  got=lit, want="a path derived from __file__")

    print("t_registry: %d files in tests/, %d registered, %d exempted, "
          "%d build" % (found, len(reg), len(UNREGISTERED),
                        sum(1 for r in suite.rows() if r.builds)))
    done("t_registry")


if __name__ == "__main__":
    main()
