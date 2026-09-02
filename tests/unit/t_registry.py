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
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tests"))
from harness import check, done                           # noqa: E402
import suite                                              # noqa: E402

# Not registered, and why. Keep the reason specific and true.
UNREGISTERED = {
    # --- library and support code, not tests ---
    "dispcells.py": "the CELLS-not-calls counter two gates share (SPEC.md "
                    "11.3.3), not a test",
    "benchlib.inc": "a benchmark library, not a test",
    "trklog.inc": "tracker's logging build, %included by apps/tracker",
    "trkscrl.inc": "tracker's scroll-gate build, %included by apps/tracker",
    "npbench.inc": "a benchmark body, %included",

    # --- need a build prerequisite the default build does not make ---
    "brclick.py": "needs `make browsertest` (build/brtest360.img)",
    "brfetch.py": "needs `make browsertest` (build/brtest360.img)",
    "brlink.py": "needs `make browsertest` (build/brtest360.img)",
    "brnav.py": "needs `make browsertest` (build/brtest360.img)",
    "brscroll.py": "needs `make browsertest` (build/brtest360.img)",
    "brtest.py": "needs `make browsertest` (build/brtest360.img)",
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
    # pkgthumb.py IS registered (four rows). Its `frotz` mode is not one of
    # them: a Z-machine interpreter needs a STORY, and the two ways to get one
    # are a network fetch or the Inform compiler, neither of which is a build
    # dependency anybody has (SPEC.md 13.10.7).
}


def main():
    reg = {}
    for r in suite.rows():
        for part in r.cmd:
            if part.startswith("tests/") and part.endswith(".py"):
                reg[os.path.basename(part)] = r.name

    tdir = os.path.join(ROOT, "tests")
    found = 0
    for f in sorted(os.listdir(tdir)):
        if not f.endswith((".py", ".inc")) or f == "suite.py":
            continue
        found += 1
        if f in reg or f in UNREGISTERED:
            continue
        check(False, "tests/%s is not registered" % f,
              "add a row to tests/suite.py - `soak` costs no budget and makes it "
              "discoverable and runnable - or put it in UNREGISTERED here with a "
              "reason. A test nobody can find is a test nobody runs",
              got="not in suite.py and not exempted", want="a row, or a reason")

    # ...and the exemption list must not outlive the files it names.
    for f in UNREGISTERED:
        check(os.path.exists(os.path.join(tdir, f)),
              "UNREGISTERED names tests/%s, which exists" % f,
              "the file is gone - drop the row, or a stale exemption silently "
              "exempts nothing")
    for f in UNREGISTERED:
        check(f not in reg, "tests/%s is exempted OR registered, not both" % f,
              "two answers to one question drift apart - drop the UNREGISTERED row")

    print("t_registry: %d files in tests/, %d registered, %d exempted"
          % (found, len(reg), len(UNREGISTERED)))
    done("t_registry")


if __name__ == "__main__":
    main()
