#!/usr/bin/env python3
"""PageDesk's HOST checks as soak rows (PAGEDESK-SPEC 20, PD-28/PD-29).

    python3 tests/pagedeskhost.py <make target>

Each PageDesk component carries a host harness behind a phony make target -
`pdeng`, `pdefuzz`, `pdflate`, `pdflatefuzz`, `pdrend`, `pdrend-fuzz`,
`pdrend-real`, `pdrend-annot`, `pdwrt`, `pdwrt-real`, `pdnav`, `pdnotes`,
`pdmathtest`, `pdabi-check`, `pdfonts` - and until this file none of them was
a row, so nothing but the agent who wrote one ever ran it. Each target builds
its host binary (cc, sometimes AddressSanitizer + UBSan) and runs its checker
against the committed samples, the stress set and the negative corpus,
checked by qpdf / mutool / Poppler where the target says so. The target is
the contract: this file adds nothing to what it asserts, and prints its tail
on a failure.

WHY IT WRITES build/. Every one of these targets compiles into `build/` and
writes its outputs there (build/pdeng-fix, build/pdrfuzz, build/pdw, ...), and
two of them share binaries (`pdrend-real` and `pdrend-annot` both link
build/pdr_test_real). So the rows are `builds=True`: the runner gives each the
tree to itself. Under a frozen run ($OS88_TREE) the make is pointed at that
tree instead of build/.

`pdflateboot` is NOT one of these rows: it boots PDENG.MOD in QEMU, so it is
an emulator row and its own decision (tests/pagedeskhost.py stays host-only).
"""
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                            # noqa: E402

TARGETS = ("pdeng", "pdefuzz", "pdflate", "pdflatefuzz", "pdrend",
           "pdrend-fuzz", "pdrend-real", "pdrend-annot", "pdwrt",
           "pdwrt-real", "pdnav", "pdnotes", "pdmathtest", "pdabi-check",
           "pdfonts")
# A target whose host run hangs off a STAMP runs nothing when the stamp is
# fresh, and a row that ran nothing would read as a pass: the stamp goes first
STAMPS = {"pdwrt": ("pdw/.host.stamp",)}


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in TARGETS:
        sys.exit("usage: pagedeskhost.py <%s>" % "|".join(TARGETS))
    target = sys.argv[1]
    argv = ["make", "--no-print-directory", target]
    tree = os88build.at("build")
    if os.path.normpath(tree) != "build":       # a frozen run's own tree
        argv.append("BUILD" + "=" + tree)
    for st in STAMPS.get(target, ()):
        try:
            os.unlink(os.path.join(ROOT, tree, st))
        except OSError:
            pass
    t0 = time.time()
    r = subprocess.run(argv, cwd=ROOT, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    out = r.stdout.decode("utf-8", "replace")
    dt = time.time() - t0
    tail = out.strip().splitlines()[-25:]
    if r.returncode:
        print("pagedeskhost %s: FAIL - `make %s` exited %d after %.1fs; its "
              "last lines:" % (target, target, r.returncode, dt))
        for line in tail:
            print("   " + line)
        sys.exit(1)
    # a target that skipped its own work says so; a skip is not a pass
    skip = [ln for ln in out.splitlines() if " SKIP" in ln or ln.startswith("SKIP")]
    for line in tail[-6:]:
        print("   " + line)
    if skip:
        print("pagedeskhost %s: ok in %.1fs, WITH %d skip line(s): %s"
              % (target, dt, len(skip), "; ".join(s.strip() for s in skip[:3])))
    else:
        print("pagedeskhost %s: ok in %.1fs" % (target, dt))


if __name__ == "__main__":
    main()
