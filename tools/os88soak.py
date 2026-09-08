#!/usr/bin/env python3
"""os88soak - the STANDARD way to run the soak: parallel, in the background,
and survivable.

    python3 tools/os88soak.py check     # can this box answer? what would skip?
    python3 tools/os88soak.py start     # preflight, then run detached
    python3 tools/os88soak.py status    # cheap progress read - SAFE to poll
    python3 tools/os88soak.py stop      # end it, and take its emulators with it

WHY THIS EXISTS.  `make test-soak` runs the soak SERIALLY - `os88test.py soak`
defaults to `--marty-jobs 1` - so the one command in the Makefile is the slow
one, and the parallel invocation lived in two handoff documents as a line to
remember (`docs/plans/completed/HANDOFF-KERNEL-SIZE-P3.md` 3).  Anything a reader has to
remember is something the next reader will not, which is the same sentence
that put `alone=True` on a row instead of `-x` in a runbook.  This file is
that line, made into the command.

It also owns the four things a soak in a container gets wrong, none of which
belong in `os88test.py` - that runs rows, and these are about the RUN:

  1. THE PREFLIGHT.  A capability the box has not got makes a row SKIP, and a
     skip is the box declining to answer rather than a pass.  The pass-3 soak
     reached zero skips only after somebody noticed, by hand, that four disks
     the suite never builds for itself were missing
     (`docs/plans/completed/HANDOFF-KERNEL-SIZE-P4.md` 9).  `check` says so BEFORE the hours
     rather than after them, and prints the command that fixes each one.

  2. THE WIDTH.  One instance per core is the measured ceiling and going past
     it is slower, not broken.  The default here is CORES-1, and the missing
     core is not caution - it is the one the operator's own check-in, an
     editor, or a small side task runs on.  A soak sized to exactly fill the
     box is a soak that anything else on the box perturbs.

  3. SURVIVING.  It runs under `setsid`, so it outlives the shell that
     started it, and every completed row is journalled - so `start --resume`
     after a container is reclaimed re-runs what did not finish rather than
     the whole tier.  A 3-hour run that has to restart from zero because
     something touched the session is the failure this is for.

  4. BEING CHECKED ON.  `status` reads a file.  It starts no emulator, takes
     no lock, spends no CPU worth measuring and CANNOT perturb the run - which
     is the property that makes it safe for an agent to poll on a timer.  The
     rule the run depends on is the other half of point 2: check in with THIS,
     not by running rows beside it.

WHAT IT DOES NOT DO.  It does not decide whether a failure is real.  That is
`docs/plans/HANDOFF-SOAK-FINDINGS.md`'s protocol - re-run alone on HEAD, then at the
base, and bisect only where those two disagree - and the first step is
`os88test.py soak -k <row>` by hand.
"""
import argparse
import errno
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                            # noqa: E402

RUNS = os.path.join(ROOT, "build", "soak")

GREEN, RED, YELLOW, DIM, OFF = "\033[32m", "\033[31m", "\033[33m", "\033[2m", "\033[0m"
if not sys.stdout.isatty() or os.environ.get("NO_COLOR"):
    GREEN = RED = YELLOW = DIM = OFF = ""


# --------------------------------------------------------------------------
# The preflight.
#
# Every entry is (name, present?, why it matters, how to fix it).  The fix is
# a COMMAND, not a description, because the reader of a failed preflight is
# about to type something and the useful thing to give them is the something.
# --------------------------------------------------------------------------

def _apt(pkg):
    """The install line for this host, best effort.

    Named per platform rather than assumed: `tools/setup-macos.sh` installs
    the Mac set and does NOT install Rust, which is the one that catches
    people out on `make marty`.
    """
    if sys.platform == "darwin":
        return "tools/setup-macos.sh   (then `brew install %s` if it is not in it)" % pkg
    return "apt-get install -y %s" % pkg


def requirements():
    """What the soak needs, whether it is here, and the command that gets it.

    ORDERED BY WHAT BLOCKS WHAT.  nasm before the images, the images before
    the on-demand disks, and the emulators last - so a bare box reads the list
    top to bottom and does not install a debugger it cannot yet use.
    """
    B = lambda *p: os.path.join(ROOT, "build", *p)
    req = []

    req.append(("nasm", bool(shutil.which("nasm")),
                "every build. Without it nothing under build/ can be made.",
                _apt("nasm")))

    # THE SECOND ASSEMBLER, and it is not "a newer nasm" - it is the one half
    # the people who build this tree actually have. CONTRIBUTING.md's floor is
    # 2 and every box here answers 2.16, so nothing in any tier ever assembles
    # under 3.x, where constructs 2.x takes are REFUSED (`t_nasm3`'s header
    # has the incident). No distribution here packages one yet, so the fix is
    # a build or a path - which is exactly why it is a capability and the row
    # skips rather than failing.
    req.append(("nasm3", bool(os88build.nasm3()),
                "the `nasm3` row - the only thing that assembles this tree "
                "with an nasm 3, which is what Homebrew installs and what "
                "half the people building it have.",
                "brew install nasm       (macOS: it is 3.x)\n"
                "                  ...or build one and export "
                "OS88_NASM3=<path>/nasm:\n"
                "                     git clone --depth 1 -b nasm-3.02 "
                "https://github.com/netwide-assembler/nasm.git\n"
                "                     cd nasm && sh autogen.sh && "
                "./configure && make"))

    # The shipped artefacts. `all` builds these and the fast tier reads them;
    # a soak against a half-built tree fails rows for the tree's reason.
    imgs = ["os8088-360.img", "apps360.img", "os8088.img", "apps.img"]
    req.append(("shipped images", all(os.path.exists(B(i)) for i in imgs),
                "every emulator row boots one of these.",
                "make"))

    req.append(("qemu", bool(shutil.which("qemu-system-i386")
                             or shutil.which("qemu-system-x86_64")),
                "the seven cases MartyPC cannot host (docs/TESTING.md) - the "
                "286/386 rows, the PS/2 mouse, the RTC write half.",
                _apt("qemu-system-x86")))

    marty = B("martypc", "run", "martypc_headless")
    req.append(("martypc", os.path.exists(marty),
                "the default instrument. Without it EVERY emulator row skips, "
                "which is most of the tier.",
                "make marty      (needs cargo; on Linux also `%s`)"
                % _apt("libudev-dev pkg-config")))

    req.append(("cc", os.access(B("cc", "SmallerC", "smlrcc"), os.X_OK),
                "the C packages - Weave, RunCPM, the C64, cword. Eleven rows.",
                "tools/setup-cc.sh"))

    # THE FOUR DISKS `all` DELIBERATELY DOES NOT BUILD.  This is the item the
    # pass-3 soak found by hand after fifteen runs had skipped on it, and the
    # reason each is absent is a different deliberate decision (SPEC.md 78.9
    # for wire; on-demand application disks for the rest).
    req.append(("wiredisk", os.path.exists(B("wire360.img")),
                "wireflick, wirefps and uilat. TWO OF THOSE ARE RATE ROWS, so "
                "without it a soak skips half its rate lane silently.",
                "make wiredisk"))
    req.append(("weave disks", os.path.exists(B("weave.img")),
                "the Weave family's rows.",
                "make weavedisk loomdisk      (NOT under -j: the recipe races "
                "on build/WEAVE.OVL)"))
    req.append(("c64 disk", os.path.exists(B("c64360.img")),
                "c64part and the C64 rows.",
                "make c64disk"))
    # A FIFTH, and the one that proves the list is worth keeping by hand:
    # skiesdiag's is a private -DCSDIAG TREE rather than a disk `all` chose
    # not to build, so nothing above would ever have named it. It went
    # unbuilt and unnoticed because the row reported its own absence as a
    # pass (tools/os88test.py's probe carries that account).
    req.append(("skiesdiag tree",
                os.path.exists(B("skiesdiag", "apps360.img")),
                "skiesdiag - the ONLY test of Clear Skies' freeze watchdog "
                "(SPEC.md 88.14), an instrument for a machine that has hard "
                "frozen.",
                "make skiesdiag"))

    # **AND EVERY ARTEFACT A ROW DECLARES.** The list above is hand-written
    # and names the four disks somebody noticed; `Row(wants=...)` is the
    # machine-readable version of the same claim and nothing was checking it.
    #
    # WHAT `make -q` CANNOT ANSWER is why this reads recipes instead: it
    # reports 1 for "out of date" and 2 for "no rule", and every artefact here
    # has a rule - so it answers 1 for `build/zmove360.img`, whose failure is
    # that `inform` is not installed. A dry run does not run recipes, so the
    # only thing that can find a missing TOOL is running one, or reading what
    # the rule would run. This does the second, over the target AND its
    # build-tree prerequisites, because zmove360's own recipe is
    # `python3 tools/os88disk.py` and it is `$(BUILD)/zt/ZOPS.Z5` one edge up
    # that wants Inform.
    #
    # It is a heuristic and it says so. The real protection is that
    # `os88test`'s prebuild now costs only the rows that DECLARED a failing
    # artefact: this one missing tool took a five-hour soak down at 37
    # minutes with 0 of 267 rows reported.
    try:
        sys.path.insert(0, os.path.join(ROOT, "tests"))
        import suite as _suite
        arts = sorted({f for r in _suite.rows() for f in getattr(r, "wants", ())})
    except Exception:                                           # noqa: BLE001
        arts = []
    if arts:
        absent = [f for f in arts if not os.path.exists(os.path.join(ROOT, f))]
        try:
            with open(os.path.join(ROOT, "Makefile")) as f:
                mk = f.read()
        except OSError:
            mk = ""
        tools = {}
        for f in absent:
            for line in _recipe_of(mk, f):
                w = line.split()
                cmd = w[0] if w else ""
                # A leading `-` is an argument that got to the front of a line
                # some other way; `/` and `$` are paths and make variables,
                # neither of which this can answer for.
                if (cmd and not cmd.startswith("-") and "/" not in cmd
                        and "$" not in cmd and not shutil.which(cmd)):
                    if f not in tools.setdefault(cmd, []):
                        tools[cmd].append(f)
        req.append(("declared artefacts", not tools,
                    "%d row-declared artefact(s), %d absent; the run builds "
                    "them up front. %s"
                    % (len(arts), len(absent),
                       "; ".join("%s needs `%s`" % (" ".join(v), k)
                                 for k, v in sorted(tools.items()))
                       or "every absent one's recipe names tools that are "
                          "here"),
                    "install it, or accept the skip - only the rows that "
                    "DECLARED it are affected"))
    return req


def _recipe_of(mk, target, seen=None):
    """The recipe lines of `$(BUILD)/<name>`'s rule and of its build-tree
    prerequisites, verbatim. Textual and crude: a miss yields nothing, which
    is the same answer this gave before it existed.
    """
    if seen is None:
        seen = set()
    name = target[len("build/"):] if target.startswith("build/") else target
    if name in seen:
        return []
    seen.add(name)
    i = mk.find("\n$(BUILD)/" + name + ":")
    if i < 0:
        return []
    head = mk[i + 1:].splitlines()
    prereq, k = head[0].split(":", 1)[1], 0
    while prereq.rstrip().endswith("\\") and k + 1 < len(head):
        k += 1
        prereq = prereq.rstrip()[:-1] + head[k]
    # **JOIN THE CONTINUATIONS FIRST.** A recipe line ending in `\` is one
    # shell command spread over several, and reading each physical line's
    # first word makes ARGUMENTS look like programs: `msegz.o88`'s recipe put
    # `--part` and `--part-compress` on their own lines, and the check duly
    # reported that build/msegz.img "needs `part`" - five times, because five
    # rows declare it. A false name here is worse than no check: it is a
    # capability gap that cannot be installed.
    out, cur = [], ""
    for line in head[k + 1:]:
        if not line.startswith("\t"):
            break
        cur += line.lstrip("\t").lstrip("@-").rstrip()
        if cur.endswith("\\"):
            cur = cur[:-1] + " "
            continue
        out.append(cur)
        cur = ""
    if cur:
        out.append(cur)
    for tok in prereq.split():
        if tok.startswith("$(BUILD)/"):
            out += _recipe_of(mk, tok[len("$(BUILD)/"):], seen)
    return out


def _buildnum_current():
    """Does build/buildnum.inc hold THIS commit's number?

    **THE RE-ASSEMBLY CHECK BELOW CANNOT SEE THIS, and that is why it is a
    separate question.** `BUILD_NUM` reaches the kernel through a GENERATED
    include (tools/buildnum.py -> build/buildnum.inc), and os88sym re-assembles
    kernel.asm with `-I build/` - so after a commit with no `make`, the stale
    include and the stale kernel.bin agree with each other perfectly and the
    check passes.

    What then happens is the worst version of this failure. `BUILDNUM :=
    $(shell python3 tools/buildnum.py -o $(BUILDINC))` runs at make PARSE
    time, so the FIRST `make` of the run - any target, from any row - rewrites
    the include in milliseconds, while `build/kernel.bin` is relinked seconds
    or minutes later. Between those two moments every symbol read in the tree
    re-assembles with the new number and compares against a kernel built with
    the old one, and EVERY EMULATOR ROW IN FLIGHT FAILS with "the map
    describes a DIFFERENT kernel".

    Measured, on the run that found it: `buildnum.inc` rewritten at 14:59:14
    and `kernel.bin` at 15:04:08 - a **4m54s window**, nine rows lost in it,
    and every one of them reported as a broken feature.
    """
    inc = os.path.join(ROOT, "build", "buildnum.inc")
    try:
        with open(inc) as f:
            have = re.search(r"BUILD_NUM\s+(\d+)", f.read())
    except OSError:
        return True, ""                          # no include yet: the
                                                 # re-assembly check owns it
    if not have:
        return True, ""
    try:
        want = subprocess.run(["git", "rev-list", "--count", "HEAD"],
                              cwd=ROOT, capture_output=True, text=True,
                              timeout=30)
        if want.returncode:
            return True, ""                      # not a git checkout: not our
        n = int(want.stdout.strip())             # question to answer
    except (OSError, ValueError, subprocess.SubprocessError):
        return True, ""
    if int(have.group(1)) == n:
        return True, ""
    return False, ("build/buildnum.inc says BUILD_NUM %s and this checkout is "
                   "at %d commits - so `make` has not run since the last "
                   "commit. The re-assembly check below CANNOT see this (both "
                   "sides are stale together); what it costs is a window, "
                   "minutes wide, in which every emulator row fails on a "
                   "symbol lookup, opened by the first `make` any row runs."
                   % (have.group(1), n))


def _kernel_matches():
    """Is build/kernel.bin still what kernel.asm assembles to?

    `os88test.py` asks this too, and asks it once before any row runs - but it
    asks at the START OF THE RUN, which for a detached soak is after the
    operator has walked away.  Asking here is asking while somebody is still
    looking at the terminal.

    The cause is almost always a commit: the About box's build number is the
    commit count (SPEC.md 14.2), so committing moves three bytes of .text and
    every symbol every emulator row reads goes wrong at once.

    IT IS NOT THE WHOLE CHECK - see `_buildnum_current` above for the case
    this one is structurally blind to.
    """
    ok, why = _buildnum_current()
    if not ok:
        return False, why
    try:
        import os88sym
        os88sym.syms(())
        return True, ""
    except Exception as e:                       # noqa: BLE001 - any refusal
        if "DIFFERENT kernel" in str(e):
            return False, ("build/kernel.bin is not what kernel.asm assembles "
                           "to. If you have committed since the last `make`, "
                           "that is the whole cause.")
        return False, "tools/os88sym.py could not read the kernel: %s" % e


def _stale_emulators():
    """Emulators already up that nobody in this run owns.

    A stale QEMU from an earlier row holds build/os8088.img for hours and the
    next row fails wearing a message about the wrong subject
    (docs/plans/HANDOFF-SOAK-FINDINGS.md B9); a MartyPC orphan is cheaper but still
    eats a core the width arithmetic below has already promised to somebody.

    Reported, never killed from here.  `os88marty.py reap` kills ORPHANS only
    and leaves live work alone, which is a distinction this function has no
    way to make - another session in another checkout is not stale.
    """
    out = []
    try:
        import os88marty
        live = [d for d in os88marty.instances() if d["alive"]]
        orph = [d for d in live if not d["owner_alive"]]
        if live:
            out.append("%d MartyPC instance(s) already running, %d of them "
                       "ORPHANED (`python3 tools/os88marty.py instances`; "
                       "`reap` clears orphans and leaves live work alone)"
                       % (len(live), len(orph)))
    except Exception:                            # noqa: BLE001
        pass
    try:
        n = subprocess.run(["pgrep", "-cf", "qemu-system-i386"],
                           capture_output=True, text=True).stdout.strip()
        if n and int(n) > 0:
            out.append("%s qemu-system-i386 process(es) already running - "
                       "check `ps -o pid,etime -C qemu-system-i386` and kill "
                       "BY PID (`pkill -f` matches its own shell)" % n)
    except (OSError, ValueError):
        pass
    return out


def preflight(verbose=True):
    """Everything that would make this run answer less than it claims to.

    Returns (blocking, advisory).  BLOCKING is what stops rows running at all;
    ADVISORY is what makes them skip, which is quieter and worse.
    """
    blocking, advisory = [], []
    req = requirements()
    for name, ok, why, fix in req:
        if ok:
            continue
        (blocking if name in ("nasm", "shipped images") else advisory).append(
            (name, why, fix))

    ok, msg = _kernel_matches()
    if not ok:
        blocking.append(("kernel map", msg, "make"))

    # THERE IS NO GLOBAL BUILD LOCK ANY MORE, and its absence is the feature.
    # `tools/martylock.py` used to be checked here: one mutex over MartyPC and
    # over build/kernel.bin's identity, because a row that wanted a knob
    # kernel had nowhere to put it but `build/`. Rows build into private trees
    # now (tools/os88build.py), so two agents in one checkout do not collide
    # and there is nothing here to refuse a run over.

    free = shutil.disk_usage(ROOT).free // (1 << 20)
    if free < 4096:
        advisory.append(("disk", "%d MB free. Each MartyPC instance is ~1 MB "
                         "of run tree, but the knob builds under `builds=True` "
                         "rewrite build/ repeatedly." % free,
                         "delete build artefacts you do not need"))
    return blocking, advisory


def would_skip():
    """Which registered soak rows this box cannot answer, and for what.

    The point of printing it BEFORE the run is that a skip is invisible
    afterwards unless somebody counts: 196 ok out of 200 reads like a good run
    whether the other rows skipped or never existed.
    """
    import suite
    import os88test
    caps = os88test.capabilities()
    order = {"fast": 0, "full": 1, "soak": 2}
    out = {}
    for r in suite.rows():
        if order[r.tier] > 2:
            continue
        missing = set(r.needs) - caps
        if missing:
            out.setdefault(",".join(sorted(missing)), []).append(r.name)
    return out


# --------------------------------------------------------------------------
# The run.
# --------------------------------------------------------------------------

# The artefacts `all` deliberately does not build, and the make target for
# each. PREWARMED before the run rather than left to whichever row gets there
# first, for two reasons that are different sizes:
#
#   * a missing one makes a row SKIP or FAIL, and a skip is the box declining
#     to answer. `check` reports these; `start` now builds them.
#   * `build/muptest.img` is built by ANOTHER ROW OF THE SAME SUITE, so
#     whether `fdlggrey` passes depends on the ORDER rows ran in - and with
#     `--marty-jobs` that order is not fixed. docs/plans/HANDOFF-SOAK-FINDINGS.md B4
#     records that as unfixed and says "either the artefact gets its own build
#     step, or the dependency gets stated". This is the build step.
#
# Building them here also makes every fixture row's own `need()` a NO-OP, so
# the `make` it runs finds an up-to-date tree and writes nothing.
PREWARM = [
    ("build/wire360.img", "wiredisk"),
    ("build/weave.img", "weavedisk"),
    ("build/loom.img", "loomdisk"),
    ("build/c64360.img", "c64disk"),
    ("build/skiesdiag/apps360.img", "skiesdiag"),      # ...and ALWAYS, below
    ("build/muptest.img", "build/muptest.img"),
    ("build/spantest.img", "spantest"),
]


# **EXISTENCE IS NOT FRESHNESS** (docs/WRITING-TESTS.md 13 row 33). A PRIVATE
# TREE is built by a recursive make into a directory of its own, and nothing
# in the shipped graph depends on it - so an edit to apps/skies/ leaves
# build/skiesdiag/ sitting there, existing, describing a package the guest has
# not got. `skiesdiag` checks its own tree and FAILS naming it, which is the
# behaviour row 33 asks for; this is what stops it having to. A no-op
# `make skiesdiag` is 0.9s, so it is cheaper to always run than to reason
# about.
ALWAYS = {"skiesdiag"}


def prewarm(verbose=True):
    """Build the on-demand artefacts, once, before any row runs.

    Serially and never under `-j`: `make -j4 weavedisk loomdisk c64disk` races
    on build/WEAVE.OVL and dies with "No rule to make target", which is a
    Makefile bug this is not the place to fix and a five-second cost to avoid.
    """
    # **A PLAIN `make` FIRST, ALWAYS, AND BEFORE ANY ROW RUNS.** Not for the
    # artefacts - for `build/buildnum.inc`. Every `make` in the tree rewrites
    # it at PARSE time while `build/kernel.bin` is relinked much later, so the
    # first make of a run opens a window, minutes wide, in which every symbol
    # lookup in the suite fails. Doing it here closes the window before the
    # first row exists, and it costs nothing when the tree is already current.
    # See `_buildnum_current`.
    r = subprocess.run(["make", "-s"], cwd=ROOT, capture_output=True,
                       text=True)
    if r.returncode and verbose:
        print("%sos88soak: `make` failed - rows will run against whatever "
              "build/ holds:%s %s" % (YELLOW, OFF, (r.stderr or r.stdout)[-400:]))
    elif verbose:
        print("os88soak: build/ is current")

    made, failed = [], []
    for art, target in PREWARM:
        if os.path.exists(os.path.join(ROOT, art)) and target not in ALWAYS:
            continue
        r = subprocess.run(["make", "-s", target], cwd=ROOT,
                           capture_output=True, text=True)
        (made if r.returncode == 0 else failed).append(target)
        if r.returncode and verbose:
            print("%sos88soak: `make %s` failed:%s %s"
                  % (YELLOW, target, OFF, (r.stderr or r.stdout)[-300:]))
    if verbose and (made or failed):
        print("os88soak: prewarmed %d artefact(s)%s"
              % (len(made), (", %d FAILED" % len(failed)) if failed else ""))
    return made, failed


def _cores():
    """Cores this process may actually use.

    `os.cpu_count()` is the hardware; a container is usually smaller than the
    hardware, and sizing the width off the wrong one is what turns "three
    emulators" into eight.  `sched_getaffinity` is the honest answer where it
    exists.
    """
    try:
        return max(1, len(os.sched_getaffinity(0)))
    except AttributeError:
        return max(1, os.cpu_count() or 1)


def widths(cores, mj=None, hj=None):
    """How wide to run, and WHY it is one less than the box.

    Measured aggregate guest speed against a real 4.77 MHz 8088, four-core
    box: 3.4x at one instance, 13.1x at four, 13.9x at six, 13.4x at eight.
    It is FLAT past the core count - four to six buys 6% and six to eight
    LOSES 4% - so the core count is the ceiling and nothing above it is worth
    paying for.  What three costs against four is not in that series and is
    not claimed here; what it buys is measured, and is the reason for it:
    twelve rows at width 3 with two extra CPU hogs passed 12/12 and ran 1.06x
    slower than the same rows alone (docs/plans/SOAK-PARALLEL.md 1).

    That last core is what a `status` poll, an editor, a `git log` or a small
    side task runs on.  Leaving it is not politeness - a run sized to fill the
    box exactly is one that anything else on the box perturbs, and every
    perturbed row is an hour of somebody deciding whether the failure was
    real.  `docs/plans/HANDOFF-SOAK-FINDINGS.md` is largely a list of people making
    that decision.
    """
    return (mj if mj else max(1, cores - 1),
            hj if hj else max(2, cores))


def _selected(a):
    """The row names this invocation asks for, before any --resume exclusion.

    Computed HERE rather than read back out of the runner's output, because
    the denominator has to survive a resume: a resumed attempt's own header
    counts only what is left, and an attempt with nothing left prints no
    header at all. Reported 3 of 6 on a three-row run is how that looks.
    """
    import fnmatch
    import suite
    order = {"fast": 0, "full": 1, "soak": 2}
    return [r.name for r in suite.rows()
            if order[r.tier] <= order[a.tier]
            and (not a.k or any(fnmatch.fnmatch(r.name, g) for g in a.k))
            and not any(fnmatch.fnmatch(r.name, g) for g in a.exclude)]


def _run_dir(new=False):
    if new:
        d = os.path.join(RUNS, time.strftime("%Y%m%d-%H%M%S"))
        os.makedirs(d, exist_ok=True)
        latest = os.path.join(RUNS, "latest")
        try:
            if os.path.islink(latest) or os.path.exists(latest):
                os.remove(latest)
            os.symlink(os.path.basename(d), latest)
        except OSError:
            pass
        return d
    d = os.path.join(RUNS, "latest")
    return d if os.path.exists(d) else None


def _journal_path(d):
    return os.path.join(d, "done.txt")


def _done_rows(d):
    """Rows this run has already reported on, from the journal.

    The journal is appended by the reader below as each row reports, so a run
    that dies with the container has recorded everything up to the moment it
    went.  That is what makes `--resume` cheaper than a restart: the floor of
    a soak is its `builds=True` lane, and re-running that lane because a
    session went idle is over an hour thrown away.
    """
    p = _journal_path(d)
    if not os.path.exists(p):
        return {}
    out = {}
    for line in open(p):
        parts = line.split(None, 2)
        if len(parts) >= 2:
            out[parts[1]] = parts[0]
    return out


def _frozen_targets(a):
    """What the run's own tree has to contain: the shipped set, plus every
    artefact the selected rows DECLARE.

    The declarations are already the machine-readable list of what `all` does
    not build (14.1), so this is the same union os88test's prebuild takes -
    computed once, up front, into the tree rather than into build/.
    """
    try:
        sys.path.insert(0, os.path.join(ROOT, "tests"))
        import suite
        names = set(_selected(a))
        arts = sorted({f for r in suite.rows() if r.name in names
                       for f in getattr(r, "wants", ())})
    except Exception:                                       # noqa: BLE001
        arts = []
    # THE SHIPPED SET IS SPELLED OUT rather than asked for as `all`, because
    # `all` ends in `checkdocs` and `test-fast` - gates, not artefacts, and
    # ones that would run against the tree for no reason and fail it for
    # somebody else's. These are `all`'s own image list (Makefile), which is
    # every kernel, package and driver the rows read, since each image names
    # them as prerequisites.
    shipped = ("os8088.img", "os8088-120.img", "os8088-720.img",
               "os8088-360.img", "apps.img", "apps120.img", "apps720.img",
               "apps360.img", "media360.img", "wire.o88")
    return shipped + tuple(a[len("build/"):] for a in arts
                           if a.startswith("build/"))


def start(a):
    cores = _cores()
    mj, hj = widths(cores, a.marty_jobs, a.j)

    blocking, advisory = preflight()
    print("os88soak: %d core(s); emulator lane %d, host lane %d" % (cores, mj, hj))
    if advisory:
        skips = would_skip()
        print()
        print("%sos88soak: %d capability gap(s) - rows WILL SKIP, and a skip is "
              "not a pass:%s" % (YELLOW, len(advisory), OFF))
        for name, why, fix in advisory:
            print("  %-14s %s" % (name, why))
            print("  %-14s %sfix:%s %s" % ("", DIM, OFF, fix))
        if skips:
            print()
            for miss, names in sorted(skips.items()):
                print("  %sskips for %-10s %d row(s): %s%s"
                      % (DIM, miss, len(names), " ".join(names[:8])
                         + (" ..." if len(names) > 8 else ""), OFF))
    if blocking:
        print()
        print("%sos88soak: %d blocking problem(s) - not starting:%s"
              % (RED, len(blocking), OFF))
        for name, why, fix in blocking:
            print("  %-14s %s" % (name, why))
            print("  %-14s %sfix:%s %s" % ("", DIM, OFF, fix))
        return 1
    for w in _stale_emulators():
        print("%sos88soak: %s%s" % (YELLOW, w, OFF))
    if not a.no_prewarm:
        prewarm()
    if advisory and not a.anyway:
        print()
        print("Re-run with --anyway to soak with those gaps, or fix them "
              "first. A run with gaps is worth having; one that does not say "
              "so is not.")
        return 1

    d = _run_dir(new=not a.resume) or _run_dir(new=True)
    skip = _done_rows(d) if a.resume else {}
    if a.resume and skip:
        print("os88soak: resuming in %s - %d row(s) already reported"
              % (os.path.relpath(d, ROOT), len(skip)))
        # NOTHING LEFT is a finished run, not a failure. Without this the
        # resume hands os88test an empty selection, which it correctly refuses
        # with "no rows matched" and exit 1 - so the one case a resume is most
        # likely to hit, being asked twice, would report the run as broken.
        left = [n for n in _selected(a) if n not in skip]
        if not left:
            print("os88soak: nothing left to run - every selected row is "
                  "already in %s/done.txt. `status` has the result; drop "
                  "--resume to start over."
                  % os.path.relpath(d, ROOT))
            return 0

    # **THE RUN GETS A TREE OF ITS OWN** (docs/plans/SOAK-PARALLEL.md 14.2), unless
    # --shared-build says otherwise. A soak is two hours and the box is not
    # idle for them: somebody types `make`, and every row that launches after
    # it copies a half-written floppy while every row that resolves a symbol
    # reads a kernel the map no longer describes. Rows leaving build/ alone -
    # which 14.1 finished - is not the same as build/ being safe to work in.
    #
    # `os88build.tree()` is the same machinery the knob rows use, with no
    # knobs: a full `make BUILD=<dir>` into build/trees/, sharing the pinned
    # instruments by symlink. $OS88_BUILD then points os88sym, os88marty's
    # launch and scratch_disk at it (tools/os88build.at), and the shared
    # build/ can be rebuilt under the run without touching it.
    #
    # It is NOT free and the number is worth knowing: a cold tree is a whole
    # build. A warm one is make finding nothing to do, which is what the
    # second run of the day pays.
    env = dict(os.environ, PYTHONUNBUFFERED="1")
    if not a.shared_build:
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import os88build
        try:
            frozen = os88build.tree(targets=_frozen_targets(a))
        except RuntimeError as e:
            print("%sos88soak: could not build the run's own tree:%s\n%s"
                  % (RED, OFF, str(e)[-1200:]), file=sys.stderr)
            return 1
        # BOTH VARIABLES, and they are not the same claim (os88build's
        # `tree_root`): OS88_BUILD says which kernel a symbol map describes -
        # which three registry rows override with `build/smallk` - and
        # OS88_TREE says where this run's artefacts live. Keying the path
        # resolver on the first sent every disk a kern_small row opened into
        # `build/smallk`, and five rows died on a missing image the tree had.
        env["OS88_BUILD"] = frozen.dir
        env["OS88_TREE"] = frozen.dir
        print("os88soak: the run reads %s, so build/ is yours while it runs"
              % os.path.relpath(frozen.dir, ROOT))
    else:
        print("%sos88soak: --shared-build: the run reads build/, so a `make` "
              "while it runs will break rows%s" % (YELLOW, OFF))

    cmd = ["python3", os.path.join("tools", "os88test.py"), a.tier,
           "--marty-jobs", str(mj), "-j", str(hj)]
    for g in a.k:
        cmd += ["-k", g]
    for g in a.exclude:
        cmd += ["-x", g]
    for n in sorted(skip):
        cmd += ["-x", n]
    if a.strict:
        cmd.append("--strict")

    log = os.path.join(d, "run.log")
    meta = {"started": time.time(), "cmd": cmd, "cores": cores,
            "marty_jobs": mj, "host_jobs": hj, "tier": a.tier,
            "resumed": len(skip), "root": ROOT,
            # The denominator, fixed at the first start and carried through
            # every resume - see _selected().
            "total": len(_selected(a))}
    with open(os.path.join(d, "meta.json"), "w") as f:
        json.dump(meta, f, indent=1)

    # setsid, so the run outlives the shell - and the container's idle
    # reclaim is the one thing it cannot outlive, which is what the journal
    # and --resume are for.
    #
    # THE RUN WRITES ITS OWN `finished`, through a shell wrapper, and that is
    # not tidiness: the documented way to hold a session open is a background
    # task that ends when the run does -
    #
    #     until [ -f build/soak/latest/finished ]; do sleep 30; done
    #
    # - and if the marker were only written by `status`, that loop would wait
    # for a poll that may never come. A waiter must be able to see the end
    # without anybody's help.
    inner = " ".join("'%s'" % c.replace("'", "'\\''") for c in cmd)
    wrapper = ("%s; echo $? > '%s'; touch '%s'"
               % (inner, os.path.join(d, "exit"), os.path.join(d, "finished")))
    # APPENDED, not truncated: a --resume reuses this directory, and the log
    # of the attempt that died is the only account of how it died.
    try:
        os.remove(os.path.join(d, "finished"))
    except OSError:
        pass
    with open(log, "ab") as out:
        p = subprocess.Popen(["sh", "-c", wrapper], cwd=ROOT, stdout=out,
                             stderr=subprocess.STDOUT, start_new_session=True,
                             env=env)
    meta["pid"] = p.pid
    with open(os.path.join(d, "meta.json"), "w") as f:
        json.dump(meta, f, indent=1)

    print()
    print("os88soak: started pid %d in %s" % (p.pid, os.path.relpath(d, ROOT)))
    print()
    _protocol(d)
    return 0


def _protocol(d):
    rel = os.path.relpath(d, ROOT)
    print("HOW TO CHECK ON IT - and the rule that keeps the run honest:")
    print()
    print("    python3 tools/os88soak.py status")
    print()
    print("  That reads a file.  It starts no emulator, takes no lock and")
    print("  spends no measurable CPU, so polling it CANNOT perturb the run.")
    print("  The emulator lane is sized to leave one core free for exactly")
    print("  this.  What DOES perturb the run is running rows beside it, or")
    print("  a `make` - both rewrite build/ under the rows reading it.")
    print()
    print("  IN A CONTAINER, HOLD A WAITING TASK FOR THE WHOLE RUN. This is")
    print("  the part that is not optional and not obvious. Observed on the")
    print("  pass-3 soak: an agent that finished its turn with only the")
    print("  background soak running had the soak DIE ABOUT FIVE MINUTES")
    print("  LATER; the same soak under an agent that was WAITING on it ran")
    print("  to the end. `setsid` is not enough - the run outlives the shell")
    print("  that started it and it cannot outlive the container.")
    print()
    print("    until [ -f %s/finished ]; do sleep 30; done" % rel)
    print()
    print("  Run THAT as your background task and do not end the turn until")
    print("  it returns. The run writes the marker itself, so the loop ends")
    print("  on its own when the soak does and nothing has to poll for it to")
    print("  work. A periodic check-in is NOT a substitute: between two of")
    print("  them the session is idle, which is the state that kills the run.")
    print("  Poll `status` as often as you like ON TOP of the held task -")
    print("  it reads a file and cannot perturb anything.")
    print()
    print("  If it dies anyway, nothing is lost but the rows in flight:")
    print()
    print("    python3 tools/os88soak.py start --resume")
    print()
    print("  Every row that reported is journalled in %s/done.txt, and"
          % rel)
    print("  --resume excludes them.  The floor of a soak is its builds=True")
    print("  lane - over an hour that no width helps - so a restart from zero")
    print("  is the expensive way to recover from an idle session.")


_ROW = None


def _parse(log):
    """Rows reported so far, from the runner's own output.

    Parsed rather than journalled by the runner, so that `os88test.py` stays
    the thing that runs rows and knows nothing about this file.  The format is
    its `report()`: a four-character verdict, the row name, then the seconds.
    """
    import re
    global _ROW
    if _ROW is None:
        _ROW = re.compile(r"^\s*(ok|FAIL|SKIP)\s+(\S+)\s+(?:([\d.]+)s)?")
    out = []
    if not os.path.exists(log):
        return out
    for line in open(log, errors="replace"):
        line = line.replace("\033[32m", "").replace("\033[31m", "")
        line = line.replace("\033[33m", "").replace("\033[0m", "")
        line = line.replace("\033[2m", "")
        m = _ROW.match(line)
        if m:
            out.append((m.group(1), m.group(2), float(m.group(3) or 0)))
    return out


def status(a):
    d = _run_dir()
    if not d:
        print("os88soak: no run recorded under build/soak/")
        return 1
    try:
        meta = json.load(open(os.path.join(d, "meta.json")))
    except (OSError, ValueError):
        print("os88soak: %s has no readable meta.json" % d)
        return 1

    log = os.path.join(d, "run.log")
    # DEDUPED BY ROW NAME. The log is appended across --resume attempts, so a
    # row reported by two of them is in it twice; the journal is the set of
    # rows that have an answer, and that is what a count should be over.
    done = _done_rows(d)
    seen, first = [], set()
    with open(_journal_path(d), "a") as j:
        for verdict, name, secs in _parse(log):
            if name in first:
                continue
            first.add(name)
            seen.append((verdict, name, secs))
            if name not in done:
                j.write("%s %s %.1f\n" % (verdict, name, secs))
                done[name] = verdict

    alive = False
    pid = meta.get("pid")
    if pid:
        try:
            os.kill(pid, 0)
            alive = True
        except OSError as e:
            alive = e.errno == errno.EPERM

    # The denominator: what the run was asked for, not what it has done.
    # THE LAST header, not the first, plus whatever a --resume carried in.
    # A resumed attempt excludes the rows already answered, so its own header
    # is the REMAINDER - and reading the first header on a run that has been
    # resumed twice gives a total for an attempt that no longer describes it.
    total = meta.get("total")
    if total is None:                      # a run started before `total` was
        for line in open(log, errors="replace"):   # recorded; fall back to
            if line.startswith("os88test:") and " rows," in line:  # the log
                try:
                    total = int(line.split(" tier - ")[1].split(" rows")[0])
                except (IndexError, ValueError):
                    pass
    ok = [n for v, n, _ in seen if v == "ok"]
    bad = [n for v, n, _ in seen if v == "FAIL"]
    skp = [n for v, n, _ in seen if v == "SKIP"]
    el = time.time() - meta["started"]

    print("os88soak: %s  %s  (%s, lane %d)"
          % (os.path.basename(d),
             "%sRUNNING%s" % (GREEN, OFF) if alive else "%sfinished%s" % (DIM, OFF),
             meta.get("tier", "soak"), meta.get("marty_jobs", 1)))
    print("  elapsed  %s" % _hms(el))
    print("  reported %d%s   ok %d   FAIL %d   SKIP %d"
          % (len(seen), "/%d" % total if total else "", len(ok), len(bad), len(skp)))
    if total and len(seen) and alive:
        rate = el / len(seen)
        print("  eta      ~%s at the rate so far" % _hms(rate * (total - len(seen))))
    if bad:
        print("  %sfailed:%s %s" % (RED, OFF, " ".join(bad)))
    if skp and a.verbose:
        print("  %sskipped:%s %s" % (YELLOW, OFF, " ".join(skp)))

    # WHAT IS RUNNING NOW.  Read off the emulator registry rather than the
    # log, because the log only speaks when a row FINISHES - which is exactly
    # the information a check-in on a stuck run has not got.
    if alive:
        try:
            import os88marty
            live = [x for x in os88marty.instances() if x["alive"]]
            if live:
                now = time.time()
                print("  in flight %d emulator(s): %s"
                      % (len(live), "  ".join(
                          "%s %s" % (x.get("label") or "?",
                                     _hms(now - x.get("started", now)))
                          for x in sorted(live, key=lambda y: y.get("started", 0)))))
        except Exception:                        # noqa: BLE001
            pass
    if not alive and not os.path.exists(os.path.join(d, "finished")):
        open(os.path.join(d, "finished"), "w").close()
    if not alive:
        print()
        print("  tail:")
        lines = open(log, errors="replace").read().rstrip().splitlines()
        for line in lines[-4:]:
            print("    " + line)
    return 0


def _hms(s):
    s = int(s)
    return "%d:%02d:%02d" % (s // 3600, (s % 3600) // 60, s % 60) if s >= 3600 \
        else "%d:%02d" % (s // 60, s % 60)


def stop(a):
    d = _run_dir()
    if not d:
        print("os88soak: no run recorded")
        return 1
    meta = json.load(open(os.path.join(d, "meta.json")))
    pid = meta.get("pid")
    if not pid:
        print("os88soak: that run has no pid")
        return 1
    try:
        # The process GROUP: start_new_session put the runner and every row it
        # spawned in one, so this reaches the rows too.
        os.killpg(os.getpgid(pid), signal.SIGTERM)
        print("os88soak: SIGTERM to process group %d" % pid)
    except OSError as e:
        print("os88soak: could not signal %d: %s" % (pid, e))
        return 1

    # ...AND THEN REAP. Nothing in this tree installs a SIGTERM handler, so a
    # signalled row dies without running atexit and never closes its emulator
    # itself. What saves it is the process GROUP: `start_new_session` put the
    # emulator in it too, so the signal above usually takes the guest as well
    # and what is left is a stale RECORD - measured on a stopped tmowner: "0
    # orphaned emulator(s), 1 stale record(s)". Usually is not always, and a
    # row that spawns differently, or an emulator mid-start, leaves the
    # process.
    #
    # Either way `reap` is the right tool and it is safe unconditionally: it
    # kills ORPHANS and only orphans - an instance whose owner is gone - so it
    # cannot reach another session's live work. That is what makes this worth
    # doing here rather than printing advice about it
    # (docs/plans/HANDOFF-SOAK-FINDINGS.md B9 is the same leak from the QEMU side,
    # where the bill landed on an unrelated row five hours later).
    time.sleep(2.0)                      # let the rows go before judging them
    try:
        import os88marty
        killed, removed = os88marty.reap()
        print("os88soak: reaped %s orphaned emulator(s), %s stale record(s)"
              % (killed, removed))
    except Exception as e:               # noqa: BLE001 - never fail the stop
        print("os88soak: could not reap (%s) - `python3 tools/os88marty.py "
              "reap` clears anything left" % e)
    return 0


def check(a):
    blocking, advisory = preflight()
    cores = _cores()
    mj, hj = widths(cores, a.marty_jobs, a.j)
    print("os88soak: %d core(s) -> emulator lane %d, host lane %d" % (cores, mj, hj))
    print()
    for name, ok, why, fix in requirements():
        print("%s %-14s %s" % ("%s ok %s" % (GREEN, OFF) if ok
                               else "%sMISS%s" % (RED, OFF), name,
                               why if not ok else ""))
        if not ok:
            print("       %-14s %sfix:%s %s" % ("", DIM, OFF, fix))
    ok, msg = _kernel_matches()
    print("%s %-14s %s" % ("%s ok %s" % (GREEN, OFF) if ok
                           else "%sMISS%s" % (RED, OFF), "kernel map",
                           "" if ok else msg))
    for w in _stale_emulators():
        print("%sWARN%s %s" % (YELLOW, OFF, w))
    miss = [t for art, t in PREWARM
            if not os.path.exists(os.path.join(ROOT, art))]
    if miss:
        print("%sWARN%s %d on-demand artefact(s) absent; `start` builds them: "
              "make %s" % (YELLOW, OFF, len(miss), " ".join(miss)))
    skips = would_skip()
    if skips:
        print()
        n = sum(len(v) for v in skips.values())
        print("%d row(s) would SKIP - and a skip is the box declining to "
              "answer, not a pass:" % n)
        for miss, names in sorted(skips.items()):
            print("  %-12s %d: %s" % (miss, len(names), " ".join(names)))
    else:
        print()
        print("%sNo row would skip on this box.%s" % (GREEN, OFF))
    return 0 if not blocking else 1


def main():
    ap = argparse.ArgumentParser(
        description="Run the os8088 soak in parallel, detached and resumable.",
        epilog="Rows live in tests/suite.py; os88test.py runs them.")
    ap.add_argument("verb", nargs="?", default="status",
                    choices=["check", "start", "status", "stop"])
    ap.add_argument("--tier", default="soak", choices=["fast", "full", "soak"])
    ap.add_argument("-k", metavar="GLOB", action="append", default=[],
                    help="only rows matching (passed through)")
    ap.add_argument("-x", "--exclude", metavar="GLOB", action="append",
                    default=[], help="drop rows matching (passed through)")
    ap.add_argument("--marty-jobs", type=int, default=None, dest="marty_jobs",
                    help="emulator lane width (default: cores-1, see widths())")
    ap.add_argument("-j", type=int, default=None, help="host-side lane width")
    ap.add_argument("--resume", action="store_true",
                    help="continue the last run, excluding rows it reported")
    ap.add_argument("--anyway", action="store_true",
                    help="start even though rows will skip")
    ap.add_argument("--no-prewarm", action="store_true", dest="no_prewarm",
                    help="do not build the on-demand artefacts first. They "
                         "are what four rows SKIP without and what makes "
                         "`build/muptest.img` exist before the row that reads "
                         "it rather than after (docs/plans/HANDOFF-SOAK-FINDINGS.md "
                         "B4)")
    ap.add_argument("--shared-build", action="store_true",
                    dest="shared_build",
                    help="read build/ instead of a tree of the run's own - "
                         "faster to start, and a `make` while it runs breaks "
                         "rows (docs/plans/SOAK-PARALLEL.md 14.2)")
    ap.add_argument("--strict", action="store_true",
                    help="a missing capability is a FAILURE, not a skip")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    # `os88soak.py status | head` is the normal way to read this, and Python's
    # default SIGPIPE handling turns the closed pipe into a traceback on the
    # way out - which looks exactly like the tool failing rather than `head`
    # having seen enough.
    try:
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    except (AttributeError, ValueError):         # not POSIX, or not main thread
        pass
    return {"check": check, "start": start, "status": status, "stop": stop}[a.verb](a)


if __name__ == "__main__":
    sys.exit(main())
