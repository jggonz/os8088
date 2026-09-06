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

SERIAL ROWS.  Emulator tests carry `serial=True` and share a lane behind the
host-side rows, which fan out across the others.  That used to be forced:
every emulator test drove one debug server on one fixed port, and two rows in
parallel would not fail - the second would silently drive the FIRST one's
machine.  It is not forced any more.  `os88marty.launch` gives every instance
its own port, its own run directory and its own disks (docs/MARTYPC-DEBUG.md),
so `--marty-jobs N` widens that lane to N.

WHAT STILL RUNS ALONE, and it is no longer about the emulator.  TWO flags,
and they are different claims:

  * `builds=True` - the row shells out to `make`, so it rewrites `build/`
    under any row reading it.  It cannot share the TREE, and
    `tests/unit/t_registry.py` checks this one against the script rather than
    trusting it.
  * `alone=True` - the row's ANSWER needs the machine to itself.  A row whose
    assertion is a RATE cannot share four cores with two other guests; nor can
    one whose clicks are paced by a host-timed settle.  It can share the tree
    and only needs the CORES.

Both keep the row out of the shared lane whatever `--marty-jobs` says, and
both land it in the one-at-a-time lane of the SAME run - so "the whole soak
except the rate rows, then the rate rows" is one command now and not two.

WHY THE DEFAULT IS 1.  Not caution about the isolation - `tests/martyconc.py`
is the gate on that - but arithmetic.  Every instance runs its guest as fast
as the host will let it, so N of them on an N-core box is the ceiling and past
it each row takes LONGER in host seconds.  Guest cycle counts are unaffected
(they are counted, not timed), but a row's declared `secs`, its timeout and
`settle`'s patience are all host seconds, so raising this trades wall-clock
for slack that some rows have not got.  Raise it deliberately, with the box in
mind: `--marty-jobs 3` on a four-core machine.

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
    # WIREFRAME is an instrument and does not ship (SPEC.md 78.9), so `all`
    # builds wire.o88 and NO shipped floppy carries it - the disk comes from
    # `make wiredisk` and nothing in the suite runs that. Without this, the
    # three rows that drive it (wireflick, wirefps, uilat) FAIL on a tree that
    # simply has not built it, and a failure meaning "this box has no disk"
    # buries the failures that mean something. Named for the artifact, per the
    # note above.
    if os.path.exists(os.path.join(ROOT, "build/wire360.img")):
        caps.add("wiredisk")
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
        p = subprocess.Popen(row.cmd, cwd=ROOT, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True)
    except OSError as e:
        return Result(row, False, False, time.time() - t0, str(e), "could not run")
    try:
        so, se = p.communicate(timeout=row.timeout)
        out = so + se
        ok = p.returncode == 0
        reason = "" if ok else "exit %d" % p.returncode
    except subprocess.TimeoutExpired:
        # SIGTERM, NOT SIGKILL. subprocess.run(timeout=) kills the row
        # outright, and a killed Python runs no atexit - which is where every
        # QEMU launcher's teardown lives (tests/os88qemu.py). So a row that
        # timed out left its emulator running, holding build/qmp.sock, and
        # the next row drove THAT machine. terminate() lets the row exit
        # normally and take its guest with it; only a row that will not go
        # inside ten seconds is killed.
        p.terminate()
        try:
            so, se = p.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
            so, se = p.communicate()
        out = (so or "") + (se or "")
        ok, reason = False, "TIMEOUT after %ds" % row.timeout
        if "qemu" in row.needs:
            out += _sweep_qemu()
    return Result(row, ok, False, time.time() - t0, out, reason)


def _sweep_qemu():
    """Stop every QEMU a timed-out row can have left, by PIDFILE.

    The belt under the braces above: a row killed after ignoring SIGTERM ran
    no atexit either. Every launcher in tests/ daemonizes with `-pidfile
    build/<x>.pid` - qemu.pid for most, ps2.pid for the PS/2 row - so the
    sweep is over build/*.pid, through os88qemu.kill, which is by pidfile and
    never by `pkill -f` (its docstring says why). The default pidfile's
    socket is cleared with it; a private one's socket is that row's own.
    """
    import glob
    import os88qemu
    swept = []
    for pf in sorted(glob.glob(os.path.join(ROOT, "build", "*.pid"))):
        sock = os88qemu.SOCK if pf == os88qemu.PIDFILE else None
        os88qemu.kill(pf, sock)
        swept.append(os.path.basename(pf))
    return ("\nos88test: swept %s after the timeout\n" % ", ".join(swept)
            if swept else "")


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
    ap.add_argument("-x", "--exclude", metavar="GLOB", action="append",
                    default=[],
                    help="drop rows whose name matches, AFTER -k (repeatable). "
                         "It was written for one shape - a row whose assertion "
                         "is a RATE, excluded from a wide run and taken in a "
                         "second serial one - and that shape is alone=True on "
                         "the row now, in ONE run. What is left is the ordinary "
                         "use: dropping a row on purpose. Excluding one is a "
                         "decision, so the run prints which it dropped and a "
                         "green result cannot quietly be a green result over "
                         "less.")
    ap.add_argument("-j", type=int, default=min(4, (os.cpu_count() or 2)),
                    help="parallel lanes for the host-side rows")
    ap.add_argument("--marty-jobs", type=int, dest="mj",
                    default=int(os.environ.get("OS88_MARTY_JOBS", "1")),
                    help="how many EMULATOR rows may run at once (default 1). "
                         "Instances are isolated, so this is a question about "
                         "how many cores the box has, not about safety - see "
                         "the header. Rows marked builds=True (cannot share the "
                         "TREE) or alone=True (cannot share the CORES) run "
                         "alone whatever this says.")
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
    if a.exclude:
        dropped = [r.name for r in want
                   if any(fnmatch.fnmatch(r.name, g) for g in a.exclude)]
        want = [r for r in want if r.name not in dropped]
        print("os88test: -x dropped %d row(s): %s"
              % (len(dropped), " ".join(sorted(dropped)) or "(none matched)"))
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
    par = [r for r in want if not r.serial and not r.builds]
    ser = [r for r in want if r.serial or r.builds]
    # ...`builds` keeps a row OUT of the shared lane whatever the row says
    # about serial: buildmatrix and bmshare both drive make against build/,
    # and the 47 par rows read build/kernel.bin and build/*.img meanwhile
    # THREE LANES, not two. A row that builds keeps the tree to itself; an
    # emulator row that does not build can share the machine with another,
    # because every instance has its own port, directory and disks now.
    mj = max(1, a.mj)
    conc = [r for r in ser
            if mj > 1 and "marty" in r.needs and not r.builds and not r.alone]
    ser = [r for r in ser if r not in conc]
    if conc:
        print("os88test: %d emulator row(s) in a lane of %d; %d run alone"
              % (len(conc), mj, len(ser)))

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
    # ...and the emulator lane LAST, never beside the builders above: a `make`
    # halfway through rewriting build/kernel.bin is exactly what an emulator
    # row must not be reading.
    if conc:
        with concurrent.futures.ThreadPoolExecutor(max_workers=mj) as ex:
            futs = {ex.submit(run_row, r, caps, a.strict, a.verbose): r
                    for r in conc}
            for f in concurrent.futures.as_completed(futs):
                res = f.result()
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
