#!/usr/bin/env python3
"""os88build - a PRIVATE build tree per knob, so a test never clobbers `build/`.

    import os88build
    t = os88build.tree("NOPLANE=1").apply()     # builds (or reuses), and
    img = t.img("os8088-360.img")               # points os88sym at it
    with os88marty.launch(img, apps=t.img("apps360.img")) as m:
        ...

`.apply()` IS THE CALL, not `env=`. `os88marty.launch` takes no `env`
argument - the emulator needs no environment, the SYMBOL READER does - so
`launch(..., env=t.env)` is a TypeError in the row's first second, which is
how three converted rows failed. `t.env` is for a SUBPROCESS you spawn
yourself; inside one process, `apply()` sets both the environment and
os88sym's module default, which is the half `env` cannot reach.

    python3 tools/os88build.py list             # what trees exist
    python3 tools/os88build.py clean            # remove them all

WHY THIS EXISTS, and it is the whole `builds=True` problem.

A row that wants a knob kernel had exactly one place to put it: `build/`.  So
it ran `make NOPLANE=1`, used the result, and ran a bare `make` in a `finally`
to put the tree back - two full builds for one measurement, and in between,
`build/kernel.bin` was a kernel nobody else asked for.  Any other row reading
the tree in that window drives a kernel its symbol map describes perfectly and
that nobody wanted.  That is why fifty-eight rows are marked `builds=True`,
why they run ONE AT A TIME however wide the lane, and why they are the floor
of a soak that no amount of parallelism improves: 3.3 declared hours.

It is also, less obviously, the whole reason `tools/martylock.py` existed.  Two
agents in one checkout could not both work, because either might `make`.  Take
away the shared destination and the hazard goes with it.

THE FIX IS FOUR LINES OF MAKEFILE THAT WERE ALREADY THERE.  `$(BUILD)` is a
variable and the Makefile uses it 965 times; `make BUILD=<dir>` has always
worked and produces a BYTE-IDENTICAL image (verified against the in-tree
build).  `tools/os88sym.py` has honoured `$OS88_BUILD` and `$OS88_DEFINES` for
just as long.  Nothing had to be invented - what was missing was somewhere to
put the trees and a helper that made using one shorter than not.

WHAT THIS GUARANTEES, and it is worth being precise because the old rule was
"one build at a time, globally":

  * two rows with DIFFERENT knobs never touch the same file;
  * two rows with the SAME knobs share one tree: the second waits for the
    first's BUILD, and a row that wants to BUILD one waits for the rows
    READING it (_Lock downgrades to a shared hold rather than dropping it,
    which is what stopped `msegnomem` reading a kernel.bin mid-rewrite);
  * `build/` itself is never written by a row at all, so a person or another
    agent may `make` in the checkout while a soak runs.

THE LOCK IS `flock`, HELD ONLY ACROSS THE BUILD, and that is the argument for
deleting `martylock.py` rather than reusing it.  A lease was needed there
because a holder worked across many shells and PID liveness could not answer
"is the holder alive".  A build lock is taken and released inside ONE process,
so the kernel releases it when that process dies, however it dies - no lease,
no expiry, no `break` command, nothing to get wedged and nothing an agent has
to remember.

THE DEFINES ARE DERIVED, NEVER RESTATED.  A knob's make VARIABLE and its nasm
DEFINE are not the same string - `VGADIRTY=1` compiles `-DVGA_DIRTY` - and
`os88sym` needs the define, or it re-assembles a different kernel and refuses
the map with a message about a stale build.  That trap cost this file's author
twenty minutes before it was written down, so `defines` comes out of `make -n`
on the kernel target and nobody has to know the mapping.
"""
import errno
import fcntl
import hashlib
import os
import re
import shutil
import subprocess
import tempfile
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Under build/, so `make clean` sweeps them and .gitignore already covers it.
# A tree is ~1.2 MB at DEFAULT_TARGETS - the 360KB pair and the packages - so
# twenty of them is 24 MB and disk is not a consideration here. A tree is as
# big as its `targets` though: tests/unit/t_nasm3.py asks for all nine
# floppies plus kern_small's two and kern_emu's, and that one is ~16 MB.
TREES = os.path.join(ROOT, "build", "trees")

# What a row almost always wants: the 360KB pair, which is what MartyPC's
# machines mount. Named here rather than defaulted per call site so that two
# rows asking for "the same knob" really do share a tree.
DEFAULT_TARGETS = ("os8088-360.img", "apps360.img")


class Tree(object):
    """One private build directory, and how to point a test at it."""

    __slots__ = ("dir", "args", "defines", "targets")

    def __init__(self, d, args, defines, targets):
        self.dir, self.args = d, tuple(args)
        self.defines, self.targets = tuple(defines), tuple(targets)

    def img(self, name):
        return os.path.join(self.dir, name)

    @property
    def env(self):
        """The environment a symbol reader needs to describe THIS kernel.

        Both halves are required and each fails differently without the other:
        without `OS88_BUILD` the identity check compares this map against
        `build/kernel.bin` and refuses; without `OS88_DEFINES` it re-assembles
        the plain kernel and refuses against the knob's image. The refusal is
        right both times - a map of a different kernel is a wrong answer, not
        a missing one - which is exactly why it is worth handing out ready-made.
        """
        e = dict(os.environ)
        e["OS88_BUILD"] = self.dir          # which kernel the map describes
        if self.defines:
            e["OS88_DEFINES"] = " ".join(self.defines)
        return e

    def apply(self):
        """Point THIS process's symbol reader at this tree, and return self.

        Call it before the first `syms()`, and call it again when switching
        arms - os88sym caches on (directory, defines), so the two arms of an
        A/B get different entries and neither is stale.

        IT SETS THE MODULE DEFAULT AS WELL AS THE ENVIRONMENT, and that is the
        half a row cannot do for itself. A row threading `defines` through its
        own lookups still calls LIBRARY helpers that do not take them -
        `os88marty.no_saver` resolves `ss_idle`, `Marty.sym` resolves whatever
        it is asked for - and those go to os88sym's module default. Setting
        only `OS88_BUILD` therefore fails exactly where the row is not
        looking: `blitcut` died inside `no_saver`, three frames below its own
        code, with a message about a stale build.
        """
        # **`OS88_BUILD` ONLY. A PRIVATE TREE IS NOT THE RUN'S TREE.** The
        # first version set both, and `plain()` reads $OS88_TREE - so a row
        # taking an A/B got the KNOB tree back from `plain()` on its second
        # pass, applied the SHIPPED defines to it, and os88sym refused the map
        # naming a directory the row had never asked for. tests/dispseam.py
        # died that way on its second machine, in the arm with no knob in it.
        #
        # The two variables have owners: $OS88_TREE is the RUNNER's, set once
        # for a whole frozen run (14.2), and nothing here may take it over.
        os.environ["OS88_BUILD"] = self.dir
        os.environ.pop("OS88_DEFINES", None)
        if self.defines:
            os.environ["OS88_DEFINES"] = " ".join(self.defines)
        import os88sym
        os88sym.default_defines(*self.defines)
        return self

    def __repr__(self):
        return "<Tree %s %s>" % (os.path.relpath(self.dir, ROOT),
                                 " ".join(self.args) or "plain")


def plain():
    """The SHARED tree as a Tree - for the other arm of an A/B.

    A row that compares a knob kernel against the shipped one needs to put the
    symbol reader back between the two, and `plain().apply()` is that. It
    builds nothing and writes nothing: the shared tree is what `make`
    maintains and what a row may READ, and the whole point of this module is
    that a row never writes it.

    **"SHARED" IS `at("build")` AND NOT LITERALLY `build/`** (14.2). Under a
    frozen run the directory every row reads is the run's own tree, so
    answering `build/` here would send exactly the rows that take an A/B -
    blitcut, blitplane, dispseam, curdisk, fatwpin, kzboot - back to the
    operator's directory for their SHIPPED arm while the knob arm came out of
    a tree. That is the one arrangement worse than either: half a row against
    a directory somebody may be building in, and only on the arm that is
    supposed to be the control. With $OS88_BUILD unset this is `build/`, which
    is what every interactive run gets.
    """
    root = tree_root()
    return Tree(os.path.abspath(root) if root else os.path.join(ROOT, "build"),
                (), (), DEFAULT_TARGETS)


def _key(args, targets):
    """A stable directory name for one (knobs, targets) pair.

    The knobs are SORTED, so `make A=1 B=1` and `make B=1 A=1` are one tree
    rather than two identical ones - and the hash covers the targets too,
    because a tree built for `os8088.img` has not got `os8088-360.img` in it
    and reusing it would hand a row a path that does not exist.
    """
    tag = "-".join(a.split("=")[0].lower() for a in sorted(args)) or "plain"
    h = hashlib.sha1(("\0".join(sorted(args)) + "|"
                      + "\0".join(sorted(targets))).encode()).hexdigest()[:8]
    return "%s-%s" % (tag[:40], h)


# A TREE PAYS FOR ITS OWN ASSEMBLY AND NOTHING ELSE.
#
# Not this file's idea: `tests/unit/t_buildmatrix.py` found it, uses it for all
# 81 of its rows, and its header is the account - each row used to be a whole
# cold build "of which the assembly under test was under a third". Both
# variables here are gates whose answer is a function of `kernel/` alone, so
# running them once per tree is one answer N times:
#
#   NOOVLCHK=1    the overlay gate takes no argument and expands no %ifdef.
#   NOKERNSIZE=1  the size REPORT, which the Makefile's own comment calls "the
#                 single most expensive thing in a knob build and pure waste
#                 when the caller is going to discard the text" - it
#                 RE-ASSEMBLES the finished kernel to measure it.
#
# NEITHER CHANGES A BYTE of any kernel, which is the property that makes them
# safe, and `tests/unit/t_bmshare.py` is the gate that asserts it. Neither is
# in $(KNOBS) or the build stamp, for the same reason.
#
# `ICODIR=build` is the third variable that pair uses and it is NOT here: it
# shares the default build's packages, which is right for a knob that reaches
# only the kernel and WRONG for one in $(PKGSBDEF), and getting that
# distinction wrong means a row silently no longer assembling the package it
# exists for. t_buildmatrix derives the exclusion from the Makefile; a tree
# here does not know which knob it was given, so it pays for its own packages.
NO_GATES = ["NOOVLCHK=1", "NOKERNSIZE=1"]


# The two things a private tree must NOT rebuild, and may share.
#
# Both are PINNED UPSTREAM ARTEFACTS - `tools/martypc/UPSTREAM` names a commit,
# `tools/setup-cc.sh` fetches SmallerC at one - so every tree's copy would be
# identical by construction, and `make clean` deliberately spares both. The C
# one is not optional: `CC_SC := $(BUILD)/cc/SmallerC`, so a tree without the
# link fails any C package with "The C compiler is not built. Run
# tools/setup-cc.sh" - which is true of that directory and false of the
# checkout, and reads as a broken machine.
#
# NOTHING ELSE MAY BE SHARED. docs/plans/completed/HANDOFF-KERNEL-SIZE-P3.md 3 says why in one
# line: a shared writable DISK is what contaminated pass 2's first bisect.
SHARED = ("cc", "martypc")


def _sweep_truncated(d):
    """Delete zero-length build products before make looks at the tree.

    A TREE LEFT HALF-BUILT IS POISON, and make cannot see it. An interrupted
    `make` - a timeout, a killed agent, a container reclaimed - can leave an
    output file created and empty, and an empty file is NEWER than everything
    it was built from: make reports the tree up to date and the next consumer
    fails somewhere else entirely. Measured: `build/trees/plain-1c902f1e`
    kept a 0-byte `kernel-full.bin`, and `t_buildmatrix`'s `make small` row
    failed with `os88mod: kernel image is impossibly short` - which reads as
    kern_small being broken and is a truncated file nobody rebuilt.

    A zero-length product is never legitimate here (every rule in the
    Makefile writes bytes), so removing it is safe and make does the rest.
    One `listdir` per tree, only at the top level, which is where every
    artefact a row asks for lives.
    """
    try:
        names = os.listdir(d)
    except OSError:
        return
    for n in names:
        p = os.path.join(d, n)
        try:
            if os.path.isfile(p) and not os.path.islink(p) \
                    and os.path.getsize(p) == 0:
                os.remove(p)
        except OSError:
            pass


def _share_instruments(d):
    """Link the pinned instruments into a tree rather than rebuilding them."""
    for name in SHARED:
        src = os.path.join(ROOT, "build", name)
        dst = os.path.join(d, name)
        if os.path.exists(src) and not os.path.exists(dst):
            try:
                os.symlink(src, dst)
            except OSError:
                pass


def _goals(d, targets):
    """Turn a row's `targets` into make goals against the private tree.

    Two kinds, told apart by whether the name looks like a FILE:

      * `os8088-360.img` is an artefact, and make wants it by path, so it is
        joined onto the tree - `make BUILD=<d> <d>/os8088-360.img`;
      * `small` is a PHONY target whose own prerequisites are already spelled
        `$(BUILD)/...`, so joining it would ask make for a file that has no
        rule. It is passed verbatim and `BUILD=<d>` does the rest.

    The test is a dot in the last path component, which is what every artefact
    in this tree has and no phony target does.
    """
    out = []
    for t in targets:
        out.append(os.path.join(d, t) if "." in os.path.basename(t) else t)
    return out


# Shared holds taken by this process, kept alive for its lifetime: see
# _Lock.__exit__. Never read - the list IS the reference.
_HELD = []


class _Lock(object):
    """flock on one tree's own lock file: EXCLUSIVE to build, SHARED to read.

    Per TREE and not global: two rows with different knobs never meet here,
    and two with the same knob meet for the length of one `make`.

    **AND THEN THE READER KEEPS A SHARED HOLD**, which the first version did
    not and a soak caught. The claim was that after the build "both read the
    same finished tree", resting on the second row's `make` being a no-op. It
    is not reliably: `msegnomem` and `mseglazy` share the DISKCNT=1 tree, the
    second re-entered it while the first was running, and os88sym read a
    kernel.bin mid-rewrite - "the map describes a DIFFERENT kernel ... first
    difference at 0x72a, and the file was written 2.7 s ago". The message had
    already worked out what happened; nothing had stopped it happening.

    So the hold is downgraded rather than dropped, and lives as long as the
    reading process. A second row's BUILD then waits for the readers, and two
    readers never wait for each other. It costs serialisation only between
    rows that share a tree, which is exactly the set that can collide.

    Everything the original argument rested on is unchanged: it is still one
    flock, still per tree, and the kernel still drops it when the holder exits
    however it exits - no lease, no expiry, nothing to get wedged.

    The kernel drops it when the holder exits, so there is no lease to expire,
    nothing to renew, and no way to leave one behind. That is the property
    `martylock.py` could not have and had to work around with leases: it was
    held ACROSS many shells, so no PID could answer for it.
    """

    def __init__(self, path):
        self.path, self.fh = path, None

    def __enter__(self):
        d = os.path.dirname(self.path)
        try:
            os.makedirs(d)
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise
        self.fh = open(self.path, "w")
        fcntl.flock(self.fh, fcntl.LOCK_EX)
        return self

    def __exit__(self, *a):
        """Downgrade to SHARED and KEEP it - see the class docstring.

        The handle is parked in a module-level list because closing it is what
        releases an flock: a Tree that went out of scope would drop the hold
        silently and put the race back with nothing to show for it.
        """
        try:
            fcntl.flock(self.fh, fcntl.LOCK_SH)
            _HELD.append(self.fh)
        except Exception:               # a downgrade that cannot be taken is
            try:                        # not worth failing a row over: fall
                fcntl.flock(self.fh, fcntl.LOCK_UN)
            finally:
                self.fh.close()
        self.fh = None


# THE COMPRESSED KERNEL'S FOUR DEFINES ARE NOT REPORTED, and that is the
# Makefile's rule rather than this one's: `$(KZDEF)` on the assembler line is
# PASS ONE'S PLACEHOLDERS (KZ_SECS=0, KZ_RPARA=0), and `build/kernel.bin` on
# disk is pass TWO - re-assembled with the real numbers by the `kernel.sys`
# rule (SPEC.md 2.9.13). So the line `make -n` prints is the one set of values
# that is certainly wrong for the image a map gets checked against. The
# Makefile says who owns them - "a tool that re-assembles the kernel for a
# SYMBOL MAP reads the same json itself" - and tools/os88sym.py does exactly
# that, from build/kernel.kz.json, but only if nobody has already named KZIP.
# Reporting them therefore did not merely pass stale numbers, it TOOK THE JSON
# OUT OF PLAY: `KZIP` with no value at all reached nasm and boot2.asm answered
# six "expression syntax error"s about a kernel that builds perfectly.
def _kz(d):
    n = d.split("=")[0]
    return n == "KZIP" or n.startswith("KZ_")


def defines_for(args, target="kernel-full.bin", build=None):
    """The nasm defines `make <args>` would compile the kernel with.

    ASKED, never restated. A knob's make variable and its nasm define differ
    often enough to be a trap - VGADIRTY=1 gives -DVGA_DIRTY, and passing the
    variable name to os88sym re-assembles the PLAIN kernel and refuses the map
    with a message about a stale build, which reads as "run make" and is not.

    A DEFINE'S VALUE IS PART OF IT. Ten knobs assemble to `-DX=<n>` rather
    than a bare `-DX` (RTC=, QUANTUM=, HERCSEG=, SBRATE=, ...), and a name
    without its number re-assembles a DIFFERENT kernel - or, where the value
    is what an expression is built from, no kernel at all. `_kz` below is the
    one family deliberately left out, for a reason of its own.

    **`make -n` IS NOT A DRY RUN OF THE MAKEFILE'S PARSE**, and this function
    was written with a bug that proves it. Two `$(shell ...)` assignments run
    at parse time whatever goal is asked for and whatever `-n` says:

      * `BUILDNUM` regenerates `$(BUILD)/buildnum.inc`; harmless.
      * `$(VIDSTAMP)`'s rule **deletes `$(BUILD)/kernel.bin`, kernel-full.bin,
        three drivers and every boot sector** whenever the knob set differs
        from the one that built that directory. That is deliberate - it is
        what stops a knob build silently booting the previous configuration -
        and it means a `make -n` with a knob in it, pointed at `build/`, is a
        DESTRUCTIVE command.

    The first draft defaulted `build` to `build/`, and `os88build.py defines
    NOPLANE=1` duly emptied the shared tree. So the directory is REQUIRED to
    be a private one: passing none makes a throwaway rather than reaching for
    the default. `tools/os88fixture.py` carries the same warning from the
    other end ("DO NOT CALL IT FROM A KNOB GATE") and it is the same trap.

    `--always-make` because an up-to-date tree prints no recipe at all, and
    the recipe is the whole answer. It costs nothing under `-n`.
    """
    tmp = None
    if build is None:
        tmp = tempfile.mkdtemp(prefix="os88def")
        build = tmp
    b = build
    try:
        rel = os.path.relpath(b, ROOT) if not b.startswith(tempfile.gettempdir()) else b
        out = subprocess.run(["make", "-n", "--always-make", "BUILD=%s" % rel]
                             + list(args) + [os.path.join(rel, target)],
                             cwd=ROOT, capture_output=True, text=True).stdout
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)
    for line in out.replace("\\\n", " ").splitlines():
        if "nasm" in line and "kernel.asm" in line and " -o " in line:
            return tuple(d for d in
                         (m.group(1) for m in
                          re.finditer(r"-D([A-Za-z_][A-Za-z_0-9]*(?:=[^\s]+)?)",
                                      line))
                         if not _kz(d))
    return ()


# --------------------------------------------------------------------------
# The OTHER assembler: nasm 3
# --------------------------------------------------------------------------
#
# THE TREE ASSEMBLES UNDER NASM 2.16 AND THAT IS NOT THE SAME QUESTION AS
# whether it assembles under nasm 3.  CONTRIBUTING.md's floor is 2 here, and
# the reason it can be is a construct this branch does not use - but the
# implication people read off it is the wrong way round: 2.16 being ENOUGH
# does not make 3.x equivalent, and every distribution that has moved on
# (Homebrew ships 3.x today) hands the tree to an assembler nothing here ever
# ran.  nasm 3 REFUSES things 2.x accepted: `add di, mod_fp - mod_tab*7` is
# `invalid operand type` there and assembles silently under 2.16, which is
# how kernel/mod.inc reached a merge un-buildable for half the people who
# would try it (commit 799c5a9, SPEC.md 2.8's mod_fpr).
#
# So the assembler is a CAPABILITY, probed like every other one in
# tools/os88test.py - a box without one SKIPS the row rather than passing it.
NASM3_MIN_MAJOR = 3

# In order, and the order is the point. An explicit path wins; then the
# side-by-side spellings a person installing a second nasm actually uses; then
# `nasm` ITSELF, because on a box whose only nasm is 3.x - which is what a
# `brew install nasm` gives today - there is no second binary to find and the
# row must still run. Probing the default last is what stops a box with both
# from testing the one it already tests.
NASM3_NAMES = ("nasm3", "nasm-3", "nasm")


def nasm_version(path):
    """(major, minor) out of `<path> -v`, or None if it does not answer.

    Read rather than assumed: a name is not a version, and `nasm3` on PATH
    has been a symlink to 2.16 on at least one box. The probe below turns
    that into "no nasm 3 here" - a SKIP - instead of a row that assembles the
    tree twice with the same assembler and reports a pass about a version it
    never ran.
    """
    try:
        out = subprocess.run([path, "-v"], capture_output=True, text=True,
                             timeout=30)
    except (OSError, subprocess.SubprocessError):
        return None
    m = re.search(r'^NASM version (\d+)\.(\d+)', (out.stdout or "") + (out.stderr or ""))
    return (int(m.group(1)), int(m.group(2))) if m else None


def nasm3():
    """The path to an nasm >= 3 on this box, or None.

    `$OS88_NASM3` names one explicitly - which is how a developer points the
    gate at a build that is not on PATH, and how the container that has no
    packaged nasm 3 runs it at all.
    """
    named = os.environ.get("OS88_NASM3")
    cands = [named] if named else [shutil.which(n) for n in NASM3_NAMES]
    for p in cands:
        if not p:
            continue
        v = nasm_version(p)
        if v and v[0] >= NASM3_MIN_MAJOR:
            return p
    return None


def tree_root():
    """The run's own build tree, or None - `$OS88_TREE`.

    **IT IS NOT `$OS88_BUILD`, and conflating the two cost a soak.** They
    answer different questions:

      * `$OS88_BUILD` says WHICH KERNEL a symbol map describes, and it has
        named a SUB-directory since long before any of this - three registry
        rows spell `OS88_BUILD=build/smallk`, and `tests/vmmouse.py` spells
        `build/emuk`. Those directories hold a kernel and its generated
        includes; they hold no floppies at all.
      * `$OS88_TREE` says WHERE THE RUN'S ARTEFACTS LIVE, which is what a
        path like `build/small360.img` has to be resolved against.

    `at()` keyed on the first, so a row pointing the symbol reader at
    kern_small silently redirected every DISK it opened into `build/smallk`
    as well - `FileNotFoundError: build/smallk/small360.img`, on a tree that
    had the image exactly where the row asked for it. Five rows, and it reads
    as a missing build.
    """
    return os.environ.get("OS88_TREE") or None


def _why(out, err):
    """The part of a failed `make` that says WHY, not the last 2000 bytes.

    A knob build ends with `kernsize`'s BUILT WITH A KNOB banner, and that
    banner is what a tail-2000 capture returns - so `vgadirty` and three
    t_buildmatrix rows reported a failure whose message was six lines of
    advice about rebuilding. The lines that matter are make's own error and
    nasm's, and they are findable by name.
    """
    keep = []
    for line in (err + "\n" + out).splitlines():
        if re.search(r"\*\*\* |error:|Error \d|No rule to make|"
                     r"refus|cannot |not found|Traceback", line):
            keep.append(line)
    tail = (err.strip() or out.strip()).splitlines()[-12:]
    return "\n".join(keep[-12:] or tail)


def use_build(sub):
    """Point the symbol reader at a SUB-directory of the build, and answer it.

        os88build.use_build("build/emuk")       # tests/vmmouse.py

    **`os.environ.setdefault` IS WRONG HERE and was, in three rows.** The
    intent it spelled is right - "a session driving this by hand with its own
    $OS88_BUILD keeps it" - but under a frozen run the RUNNER sets that
    variable for every row (docs/plans/SOAK-PARALLEL.md 14.2), so setdefault
    found it already set and the row's own choice never happened. Measured:
    tests/vmmouse.py kept the tree's plain kernel and died at its first symbol
    with "the map describes a DIFFERENT kernel", naming `<tree>/kernel.bin`
    where it wanted `<tree>/emuk/kernel.bin`.

    The two cases are now distinguishable, which is what `$OS88_TREE` bought:
    a value equal to the run's tree is the runner's default and not a choice,
    and anything else is somebody's decision and is left alone.
    """
    cur = os.environ.get("OS88_BUILD")
    root = tree_root()
    if cur and not (root and os.path.abspath(cur) == os.path.abspath(root)):
        return cur                      # a deliberate one: leave it
    os.environ["OS88_BUILD"] = at(sub)
    return os.environ["OS88_BUILD"]


def at(path):
    """A `build/...` path, resolved against the run's tree (`$OS88_TREE`).

    **THE ONE THING THAT LETS A SOAK SURVIVE SOMEBODY ELSE'S `make`**
    (docs/plans/SOAK-PARALLEL.md 14.2). Rows name `build/os8088-360.img` and its
    siblings as literal strings - 725 of them across 256 files, and 356 are
    those two images - so pointing a run at a frozen tree cannot be done by
    editing the callers. It is done where the path is USED instead, and there
    are few of those: os88marty's launch stages both floppies in one loop,
    `scratch_disk` reads its inputs in another, and os88sym has honoured
    $OS88_BUILD since it was written.

    Anything that is not under `build/` is returned unchanged, so a row that
    names /tmp, an absolute path or a private tree of its own is untouched -
    and with $OS88_BUILD unset this is the identity function, which is what
    every interactive run and every standalone `python3 tests/x.py` gets.

    It resolves the string, not the file: a path that does not exist in the
    tree comes back pointing into the tree, and the caller's own open() says
    so. Falling back to `build/` on a miss would be worse - it would half-run
    a soak against the directory the tree exists to avoid, and only sometimes.
    """
    root = tree_root()
    if not root or not isinstance(path, str):
        return path
    q = path.replace("\\", "/")
    # **A PATH ALREADY UNDER `build/trees/` IS ALREADY RESOLVED**, and
    # re-basing it is how a tree ends up inside another tree. Only this module
    # writes that prefix, so it is an exact marker of "somebody has already
    # answered this question". Measured: `tests/fatwpin.py` applies its plain
    # tree and then builds a `FATWNONE=1` one, and the `make` it spawns
    # inherits $OS88_TREE - so the boothd rule's own
    # `OS88_BUILD=build/trees/fatwnone-...` came back as
    # `<plain tree>/trees/fatwnone-...`, a directory that does not exist.
    # os88sym then had no kernel.bin to check against, the map went unchecked,
    # and `-DKZ_HD` vanished exactly as the placeholder bug made it vanish -
    # same symptom, a different cause, and one that only bites the rows that
    # build a SECOND tree.
    if q.startswith("build/trees/"):
        return path
    if q == "build" or q.startswith("build/"):
        return os.path.join(root, q[len("build/"):]) if q != "build" else root
    return path


def tree(*args, **kw):
    """Build (or reuse) a private tree for these make arguments.

        t = os88build.tree("NOPLANE=1")
        t = os88build.tree("VGADIRTY=1", targets=("os8088.img", "apps.img"))

    Reuse is `make`'s own: the directory persists, so a second call with the
    same knobs re-runs make over an up-to-date tree and returns in under a
    second. Deleting the directory is always safe.
    """
    targets = tuple(kw.pop("targets", DEFAULT_TARGETS))
    quiet = kw.pop("quiet", True)
    if kw:
        raise TypeError("tree() got %s" % ", ".join(sorted(kw)))
    args = tuple(a for a in args if a)

    d = os.path.join(TREES, _key(args, targets))
    with _Lock(d + ".lock"):
        try:
            os.makedirs(d)
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise
        _sweep_truncated(d)
        _share_instruments(d)
        # RELATIVE, and it has to be. The Makefile spells the C toolchain's
        # PATH as `$(CURDIR)/$(CC_SC)` where `CC_SC := $(BUILD)/cc/SmallerC`,
        # so an ABSOLUTE BUILD produces `$(CURDIR)//tmp/...` - a path that
        # does not exist - and every C package fails with `smlrpp` not found
        # rather than with anything about the directory. make runs with
        # cwd=ROOT here, so a relative BUILD is the same tree and the
        # concatenation comes out right.
        rel = os.path.relpath(d, ROOT)
        cmd = (["make", "BUILD=%s" % rel] + NO_GATES + list(args)
               + _goals(rel, targets))
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(
                "os88build: `%s` failed:\n%s"
                % (" ".join(cmd), _why(r.stdout, r.stderr)))
        if not quiet:
            sys.stderr.write(r.stdout)
        defines = defines_for(args, build=d)
    return Tree(d, args, defines, targets)


def _cli():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("verb", nargs="?", default="list",
                    choices=["list", "clean", "build", "defines"])
    ap.add_argument("args", nargs="*", help="make arguments, for build/defines")
    a = ap.parse_args()
    if a.verb == "list":
        if not os.path.isdir(TREES):
            print("os88build: no private trees")
            return 0
        tot = 0
        for n in sorted(os.listdir(TREES)):
            p = os.path.join(TREES, n)
            if not os.path.isdir(p):
                continue
            sz = sum(os.path.getsize(os.path.join(p, f))
                     for f in os.listdir(p)
                     if os.path.isfile(os.path.join(p, f)))
            tot += sz
            print("  %-44s %6.1f MB" % (n, sz / 1e6))
        print("os88build: %.1f MB in %s" % (tot / 1e6,
                                            os.path.relpath(TREES, ROOT)))
    elif a.verb == "clean":
        shutil.rmtree(TREES, ignore_errors=True)
        print("os88build: removed %s" % os.path.relpath(TREES, ROOT))
    elif a.verb == "defines":
        print(" ".join(defines_for(a.args)) or "(none)")
    else:
        t = tree(*a.args, quiet=False)
        print("os88build: %s\n  defines: %s" % (t.dir, " ".join(t.defines)))
    return 0


if __name__ == "__main__":
    sys.exit(_cli())
