#!/usr/bin/env python3
"""os88test - the regression suite, in two tiers with a WALL-CLOCK BUDGET.

    python3 tools/os88test.py fast        # every build. Budget 30s.
    python3 tools/os88test.py full        # before a merge. Budget 10 min.
    python3 tools/os88test.py --list      # what is registered, and why
    python3 tools/os88test.py fast -k api # just the rows whose name matches

WHY THIS EXISTS.  This tree had ninety test scripts and no way to run them.
Each one is a real gate - `tests/dockmark.py` and `tests/heapsame.py` are
better written than most of the code they check - and each one had to be
remembered, by name, by somebody who already suspected the bug it catches.
That is exactly the failure mode `tools/checkdocs.py`'s header describes one
level up: *a check nobody types has accumulated 34 findings*.  A merge does
not know which of the ninety it should have run, so it ran none.

THE BUDGET IS THE FEATURE, and it is why this is a runner rather than a
shell script that calls everything.  A suite with no ceiling grows until it
is too slow to run, and a suite too slow to run is not run - which is the
state this repo was already in with zero seconds on the clock.  So each tier
declares a wall-clock budget and THE RUNNER FAILS WHEN THE TIER OVERRUNS IT,
green tests or not.  Adding a test that does not fit is therefore a visible,
failing decision about what to take out or move down a tier, made by the
author who added it, rather than a slow drift discovered by whoever finally
gives up on the suite.

Each row also declares its OWN expected seconds, and the runner reports any
row that overran its declaration by more than `SLIP`.  Without that the tier
budget is spent by whichever test happens to run last, and the row that
actually got slower is invisible.

THE TWO TIERS ANSWER DIFFERENT QUESTIONS.

  fast   Host-side only: no emulator, no floppy, no video.  It reads what
         `make` just built - the kernel binary, the packages, the images -
         and checks the invariants that break SILENTLY.  It is cheap enough
         to hang off the default build, which is the only placement that
         makes a gate unskippable (`checkdocs` and `os88ovlchk.py` are the
         precedent).

  full   fast, plus the build matrix `all` never builds (kern_small and
         every knob), plus the emulator tests that put pixels on a screen.

WHAT A TIER MAY NOT DO.  `fast` may not build anything: it runs after `make`
and inspects its output, so a `fast` row that shells out to `make` is
measuring the build, not the tree.  `full` may build, and does.

SERIAL ROWS.  Every emulator test in this tree drives MartyPC's debug server
on 127.0.0.1:9001 - one port, one connection (docs/MARTYPC-DEBUG.md: a
second client does not error, it HANGS) - so those rows carry `serial=True`
and share one lane while the host-side rows fan out across the others.  Two
emulator rows run in parallel would not fail, which is worse: the second
would silently drive the FIRST one's machine.

CAPABILITIES.  A row names what it needs (`marty`, `qemu`, `cc`, `net`) and
is SKIPPED, loudly, when the machine has not got it - a container with no
MartyPC build still gets the whole host-side tier rather than a wall of red.
`--strict` turns a skip into a failure, which is what CI wants once the
capability is known to be there.
"""
import argparse
import concurrent.futures
import fnmatch
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tests"))

# The tier ceilings, in seconds. These are the numbers in the request that
# made this suite exist and they are not advisory - see the header.
# The tier ceilings, in seconds. `soak` has none by design - it is where a
# test goes when it is worth having and does not fit the gate.
BUDGET = {"fast": 30, "full": 600, "soak": None}

# How far a row may overrun its own declared `secs` before it is reported.
# Generous on purpose: this is here to catch a row that got 3x slower, not
# to police a loaded machine.
SLIP = 2.0

GREEN, RED, YELLOW, DIM, OFF = "\033[32m", "\033[31m", "\033[33m", "\033[2m", "\033[0m"
if not sys.stdout.isatty() or os.environ.get("NO_COLOR"):
    GREEN = RED = YELLOW = DIM = OFF = ""


def capabilities():
    """What this machine can actually run.

    Probed rather than configured, because the answer differs between a
    developer's box, this container and the field machine's owner - and a
    suite that has to be configured before it runs is one more thing to be
    wrong.
    """
    caps = set()
    if shutil.which("nasm"):
        caps.add("nasm")
    if os.path.exists(os.path.join(ROOT, "build/martypc/run/martypc_headless")):
        caps.add("marty")
    if shutil.which("qemu-system-i386") or shutil.which("qemu-system-x86_64"):
        caps.add("qemu")
    # The BINARY, not the directory: build/cc exists from the moment
    # tools/setup-cc.sh starts cloning, so a half-finished or failed setup
    # would grant the capability and turn a skip into a confusing build
    # error. Every other probe here names the artifact it needs; this one
    # named its parent.
    if os.access(os.path.join(ROOT, "build/cc/SmallerC/smlrcc"), os.X_OK):
        caps.add("cc")
    return caps


class Result:
    __slots__ = ("row", "ok", "skipped", "secs", "output", "reason")

    def __init__(self, row, ok, skipped, secs, output, reason=""):
        self.row, self.ok, self.skipped = row, ok, skipped
        self.secs, self.output, self.reason = secs, output, reason


def run_row(row, caps, strict, verbose):
    missing = set(row.needs) - caps
    if missing and not strict:
        return Result(row, True, True, 0.0, "", "needs " + ",".join(sorted(missing)))

    t0 = time.time()
    try:
        p = subprocess.run(row.cmd, cwd=ROOT, capture_output=True, text=True,
                           timeout=row.timeout)
        out = p.stdout + p.stderr
        ok = p.returncode == 0
        reason = "" if ok else "exit %d" % p.returncode
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or "") + (e.stderr or "") if isinstance(e.stdout, str) else ""
        ok, reason = False, "TIMEOUT after %ds" % row.timeout
    except OSError as e:
        out, ok, reason = str(e), False, "could not run"
    return Result(row, ok, False, time.time() - t0, out, reason)


def kernel_is_stale(rows):
    """Is build/kernel.bin still what this source assembles to?

    THE TRAP THIS CLOSES, which cost three runs in one session. Every emulator
    gate resolves kernel symbols through tools/os88sym.py, which re-assembles
    kernel.asm and refuses to hand back an address unless the result is
    byte-identical to build/kernel.bin. The About box's build number is the
    COMMIT COUNT (SPEC.md 14.2), so **making a commit is enough to invalidate
    it** - three bytes of .text move and nothing else does. Run the suite
    after committing, or commit while it is running, and every marty row dies
    in a traceback saying "the map describes a DIFFERENT kernel", which points
    at the kernel and not at you: five green gates went red mid-run that way,
    and the same message is what a genuinely stale tree gives.

    So it is asked ONCE, here, and named. It is not rebuilt: a `VIDEO=`, `RTC=`
    or any other knob kernel in build/ differs from the plain assembly on
    purpose (the Makefile's VIDSTAMP exists for exactly that), and a preflight
    `make` would silently overwrite the build somebody is testing. os88sym
    takes the knob's --define for the same reason.

    Only rows that read a symbol are worth stopping for, which is also what
    keeps this from firing inside the `make` that `all` runs: the fast tier
    needs nothing but nasm.
    """
    if not any({"marty", "qemu"} & set(r.needs) for r in rows):
        return None
    try:
        import os88sym
        os88sym.syms(())
    except Exception as e:                       # noqa: BLE001 - any refusal
        if "DIFFERENT kernel" in str(e):
            return ("build/kernel.bin is not what kernel.asm assembles to, so "
                    "every symbol every emulator row reads would be wrong.\n"
                    "         Run `make`. If you committed since the last one, "
                    "that is the whole cause - the\n"
                    "         About box's build number is the commit count "
                    "(SPEC.md 14.2), so a commit moves\n"
                    "         three bytes of .text. For a knob build, pass its "
                    "--define instead.")
        return "tools/os88sym.py could not read the kernel: %s" % e
    return None


def main():
    ap = argparse.ArgumentParser(
        description="Run the os8088 regression suite.",
        epilog="Rows are declared in tests/suite.py; add one there.")
    ap.add_argument("tier", nargs="?", default="fast",
                    choices=["fast", "full", "soak"],
                    help="fast (every build), full (pre-merge), soak (everything)")
    ap.add_argument("-k", metavar="GLOB", action="append", default=[],
                    help="only rows whose name matches (repeatable)")
    ap.add_argument("-j", type=int, default=min(4, (os.cpu_count() or 2)),
                    help="parallel lanes for the host-side rows")
    ap.add_argument("--list", action="store_true", help="print the registry and exit")
    ap.add_argument("--strict", action="store_true",
                    help="a missing capability is a FAILURE, not a skip")
    ap.add_argument("--no-budget", action="store_true",
                    help="report the tier budget but do not fail on it")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="print every row's output, not just the failures")
    a = ap.parse_args()

    import suite
    rows = suite.rows()

    if a.list:
        w = max(len(r.name) for r in rows)
        for r in rows:
            print("%-*s  %-5s %5.1fs  %-12s %s"
                  % (w, r.name, r.tier, r.secs, ",".join(r.needs) or "-", r.why))
        print("\n%d rows. Budgets: %s" % (len(rows), "  ".join(
            "%s=%s" % (k, ("%ds" % v) if v else "none") for k, v in BUDGET.items())))
        return 0

    # The tiers are CUMULATIVE - fast < full < soak - because a row worth
    # running on every build is worth running before a merge too. Keeping
    # them in one order means the cheap checks report first.
    order = {"fast": 0, "full": 1, "soak": 2}
    want = [r for r in rows if order[r.tier] <= order[a.tier]]
    if a.k:
        want = [r for r in want if any(fnmatch.fnmatch(r.name, g) for g in a.k)]
    if not want:
        print("os88test: no rows matched", file=sys.stderr)
        return 1

    stale = kernel_is_stale(want)
    if stale:
        print("%sos88test: %s%s" % (RED, stale, OFF))
        return 1

    caps = capabilities()
    declared = sum(r.secs for r in want)
    cap = BUDGET[a.tier]
    print("os88test: %s tier - %d rows, %.0fs declared, budget %s  (caps: %s)"
          % (a.tier, len(want), declared, ("%ds" % cap) if cap else "none",
             ",".join(sorted(caps)) or "none"))
    print()

    t0 = time.time()
    results = []
    par = [r for r in want if not r.serial]
    ser = [r for r in want if r.serial]

    def report(res):
        if res.skipped:
            print("%sSKIP%s %-28s %s(%s)%s" % (YELLOW, OFF, res.row.name, DIM, res.reason, OFF))
        elif res.ok:
            slip = "" if res.secs <= res.row.secs * SLIP + 1 else \
                "  %s(declared %.0fs)%s" % (YELLOW, res.row.secs, OFF)
            print("%s ok %s %-28s %5.1fs%s" % (GREEN, OFF, res.row.name, res.secs, slip))
        else:
            print("%sFAIL%s %-28s %5.1fs  %s" % (RED, OFF, res.row.name, res.secs, res.reason))
        if res.output and (a.verbose or not (res.ok or res.skipped)):
            for line in res.output.rstrip().splitlines():
                print("       | " + line)

    # The host-side rows fan out; the emulator rows share one lane behind
    # them, for the port reason in the header.
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.j)) as ex:
        futs = {ex.submit(run_row, r, caps, a.strict, a.verbose): r for r in par}
        for f in concurrent.futures.as_completed(futs):
            res = f.result()
            results.append(res)
            report(res)
    for r in ser:
        res = run_row(r, caps, a.strict, a.verbose)
        results.append(res)
        report(res)

    wall = time.time() - t0
    failed = [r for r in results if not r.ok]
    skipped = [r for r in results if r.skipped]
    print()
    print("os88test: %d passed, %d failed, %d skipped in %.1fs (budget %s)"
          % (len(results) - len(failed) - len(skipped), len(failed), len(skipped),
             wall, ("%ds" % cap) if cap else "none"))

    over = cap is not None and wall > cap
    if over:
        print("%sos88test: OVER BUDGET by %.1fs.%s The tier ceiling is not advisory - "
              "move a row down a tier or make it cheaper before adding another."
              % (RED, wall - cap, OFF))
    if failed:
        print("%sfailed:%s %s" % (RED, OFF, " ".join(r.row.name for r in failed)))
    return 1 if failed or (over and not a.no_budget) else 0


if __name__ == "__main__":
    sys.exit(main())
