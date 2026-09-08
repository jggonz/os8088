"""Build a test's own fixture before it runs (tests/suite.py).

    from os88fixture import need
    need("build/muptest.img")

WHY THIS EXISTS.  `all` does not build anything under `tests/` and nothing
there ships, so a gate whose scratch disk is one of those artifacts finds
nothing on a clean tree: it dies in `os88marty.launch`'s copy with a
FileNotFoundError, in about a tenth of a second, before it has reached the
emulator at all.  That is not a failing gate, it is an ABSENT one - and it
reads as a failing one, which is worse than either.  Seven registered rows
were in that state (drvcall, fdlgup, mouseup, calcflick, fsxdisp, heapcheck,
trkrate); tests/unit/t_registry.py's UNREGISTERED list is the other honest
answer to the same question, and the one the browser and socket gates take.

`make` IS THE DEPENDENCY GRAPH, so this asks it rather than open-coding the
nasm/os88pkg/os88disk ladder each fixture needs.  A fixture embeds the SDK
(apps/os88api.inc, apps/os88ui.inc), so a cached one built against an earlier
kernel is exactly the stale-scratch-disk trap tests/dispclose.py warns about
- and make's own rules already name those includes as prerequisites, so
asking make is what keeps that true rather than a comment promising it.

**DO NOT CALL IT FROM A KNOB GATE.**  `make` here runs with no knob
variables, and the Makefile's `$(VIDSTAMP)` rule removes `build/kernel.bin`
at PARSE time whenever the knob set differs from the one that built it -
deliberately, so that a knob change cannot boot the previous configuration.
So a gate for `make SBDRAG=1` that asks here for its fixture deletes the very
kernel it is about to test, from a make that then reports "up to date", and
the run continues against whatever the floppy still carries.
tests/fdlgthumb.py builds its fixture with nasm and os88pkg.py directly for
exactly that reason.

It does NOT delete the artifact first.  Where a test WRITES to its own image
- QEMU mounts one writable - the guest's write leaves it newer than
everything it was built from and make cannot see the difference, so that test
removes the file itself before calling here; tests/brnav.py is the worked
example.  MartyPC's launch copies each floppy into the run directory, so the
gates that go through it never dirty the original.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _declared(target):
    """A target as `tests/suite.py` SPELLS it, whatever form the row passed.

    `wants=` is written in `build/x` form and `$OS88_PREBUILT` carries it
    verbatim, but a row that resolves its own artefact - which it must, or it
    reads the shared build/ while the run reads its tree (tests/unit/
    t_artpath.py) - passes `<tree>/x`, absolute. Comparing those two strings
    says "not declared" about an artefact declared and built.

    So the comparison is done in ONE spelling: anything inside the run's tree
    is mapped back to `build/...` first. `os88build.at()` is the other
    direction of this and the two are inverses. Outside a frozen run there is
    no tree and this is the identity.
    """
    import os88build
    root = os88build.tree_root()
    if not root or not isinstance(target, str):
        return target
    a, r = os.path.abspath(target), os.path.abspath(root)
    if a == r or a.startswith(r + os.sep):
        rel = os.path.relpath(a, r).replace(os.sep, "/")
        return "build" if rel == "." else "build/" + rel
    return target


def need(*targets):
    """Run `make` for each target, from the repo root. Exits on failure.

    THE BUILD NUMBER IS PUT BACK, and that is not tidiness. `BUILDNUM` is a
    `:=` shell assignment, so `tools/buildnum.py` rewrites build/buildnum.inc
    at PARSE time on EVERY make whatever target was asked for - it has to be,
    because the thing it depends on is HEAD moving and no tracked file's mtime
    moves with a commit. The Makefile's own note says the rest takes care of
    itself: "when it does change, kernel.bin's ordinary prerequisite does the
    rest". That is true of `make`, which builds kernel.bin, and FALSE of a
    narrow target that does not - so building a fixture here after a commit
    left buildnum.inc one ahead of the kernel that embeds it, and every later
    row died in tools/os88sym.py's byte-identity check saying "the map
    describes a DIFFERENT kernel" about a tree that was perfectly consistent.
    Measured: one gate ran green, its own `make` moved the number, and the
    five rows after it failed.

    Rebuilding the kernel instead would be worse and silent - kernel.bin at
    N+1 while the floppies still carry N, so the guest boots one kernel and
    the symbol reader describes another. Restoring the two bytes is the whole
    fix; the next real `make` regenerates and rebuilds both together.
    """
    # **THE RUNNER MAY HAVE BUILT THESE ALREADY, and then this does nothing.**
    # `Row(wants=...)` names a row's artefacts to tests/suite.py, os88test's
    # `prebuild` builds them all before any row starts, and it publishes what
    # it built in $OS88_PREBUILT. A target in that list is current and nothing
    # else is going to change it, so asking make for it again buys nothing -
    # and it costs the one thing the flag it lets a row drop is about: a
    # `make` in the SHARED tree while other rows are reading it.
    #
    # UNDER THE RUNNER AN UNDECLARED TARGET IS AN ERROR, not a build. That is
    # the half a static check cannot do: `need(DISK)` and `need(a.apps)` are
    # as common here as `need("build/x.img")`, so no reader of the script can
    # say whether `wants=` covers them - but this can, exactly, at the moment
    # it matters. Building it quietly instead is the silent race the row was
    # marked `builds=True` to prevent, now with the mark removed.
    #
    # Standalone - no $OS88_PREBUILT - none of this applies and the build
    # below runs as it always has.
    have = os.environ.get("OS88_PREBUILT")
    if have is not None:
        done = set(have.split())
        missing = [t for t in targets if _declared(t) not in done]
        if not missing:
            return
        sys.exit(
            "%s: `need(%s)` asks for %s, which tests/suite.py does not "
            "declare. The runner builds a row's artefacts BEFORE any row "
            "runs; one built here instead rewrites build/ under everything "
            "beside it. Add it to this row's `wants=` (paths `make <path>` "
            "builds), or mark the row builds=True."
            % (os.path.basename(sys.argv[0]) or "os88fixture",
               ", ".join(repr(t) for t in targets), ", ".join(missing)))

    for t in targets:
        r = make("-s", t)
        if r.returncode:
            sys.exit("%s: `make %s` failed:\n%s%s"
                     % (os.path.basename(sys.argv[0]) or "os88fixture",
                        t, r.stdout, r.stderr))


def make(*args):
    """One `make` from the repo root that leaves build/ AS IT FOUND IT.

    The stamp restore below was `need`'s and is everybody's: `BUILDNUM` is a
    `:=` shell assignment, so tools/buildnum.py rewrites build/buildnum.inc at
    PARSE time on EVERY make whatever target was asked for - **including
    `make -n`, which is not a dry run of the parse.** So a row that only ever
    reads (`make -n` to lift a recipe out) and a row that only ever launches
    (`make test`) both change a file under build/, and a soak running beside
    them is reading that directory.

    It is the whole of what those rows leave behind. Restoring it is what lets
    them say they write nothing, and this is a shared routine rather than four
    copies because the next one will forget.

    Returns the CompletedProcess; the caller decides what a failure means.
    """
    # **AND IT GOES TO THE RUN'S OWN TREE when there is one** (14.2).
    # `make test` is the launcher for every QEMU row here, and its
    # prerequisites are $(TESTIMG)/$(TESTAPPS) - so without this the guest
    # boots the SHARED build/ images while everything else in the run reads
    # the frozen tree, which is the one arrangement worse than either. The
    # tree already contains them, so the goals are up to date and the recipe
    # just launches.
    #
    # The socket and pidfile stay under build/ because the Makefile spells
    # them literally, and that is fine: they are the row's own runtime files
    # and no `make` writes them.
    args = list(args)
    # THE RUN'S TREE, not $OS88_BUILD: a row may point the latter at a
    # sub-directory of a build (`build/smallk`), and `make BUILD=build/smallk`
    # is a build of its own in the wrong place.
    import os88build
    root = os88build.tree_root()
    if root:
        args.insert(0, "BUILD=%s" % os.path.relpath(root, ROOT))

    # **THE STAMP TO RESTORE IS THE ONE THIS MAKE WILL WRITE**, which is the
    # tree's when there is one. It was hardcoded to `build/buildnum.inc`, so
    # under a frozen run this saved and restored a file the make never touched
    # while letting the TREE's drift: after a commit mid-run the tree carried
    # BUILD_NUM 150 beside a kernel.bin built at 149, and four rows died with
    # "the map describes a DIFFERENT kernel" about a tree that was fine either
    # side of the window. The freeze protects `build/` from the run; this is
    # the run being protected from itself.
    stamp = os.path.join(root or os.path.join(ROOT, "build"), "buildnum.inc")
    before = None
    if os.path.exists(stamp):
        with open(stamp, "rb") as f:
            before = f.read()
    try:
        return subprocess.run(["make"] + args, cwd=ROOT,
                              capture_output=True, text=True)
    finally:
        if before is not None and os.path.exists(stamp):
            with open(stamp, "rb") as f:
                if f.read() != before:
                    with open(stamp, "wb") as w:
                        w.write(before)


def recipe(goal, contains):
    """The one recipe line `make` would run for `goal`, lifted out.

        cmd, out = os88fixture.recipe("build/os8088.img", "os88disk.py")

    Three traps, and a row that scrapes `make -n` by hand hits all of them.
    Two rows did, and both failed a frozen soak with `Nothing to be done`:

      * **THE GOAL HAS TO BE RESOLVED** (docs/plans/SOAK-PARALLEL.md 14.2). `make`
        below is passed `BUILD=<tree>`, where the rule is spelled
        `$(BUILD)/os8088.img` - so asking for the literal `build/os8088.img`
        asks for a file that HAS no rule under that BUILD, and make says so
        in the one sentence that reads like a satisfied dependency.
      * **`--always-make`**, because an up-to-date target prints no recipe at
        all and the recipe is the whole answer. It costs nothing under `-n`.
      * **the stamp**, which `make` above already puts back: `-n` is not a dry
        run of the PARSE, and the parse rewrites `buildnum.inc`.

    Returns (the command line with continuations joined, the resolved goal) so
    the caller can rewrite the output path it is about to redirect.
    """
    import os88build
    # RELATIVE TO ROOT, the way `make` above spells `BUILD=`. An ABSOLUTE goal
    # matches no rule under a relative BUILD, and make's answer to a goal it
    # has no rule for but can see on disk is `Nothing to be done` - the one
    # sentence that reads like a satisfied dependency. That is this helper's
    # own third trap, hit on the first run after it was written.
    g = os.path.relpath(os88build.at(goal), ROOT)
    r = make("-n", "--always-make", g)
    if r.returncode:
        raise SystemExit("os88fixture: `make -n %s` failed:\n%s%s"
                         % (g, r.stdout[-800:], r.stderr[-800:]))
    out = r.stdout.replace("\\\n", " ")
    lines = [l.strip() for l in out.splitlines() if contains in l]
    if len(lines) != 1:
        raise SystemExit(
            "os88fixture: `make -n %s` printed %d recipe line(s) containing "
            "%r, wanted exactly one:\n%s" % (g, len(lines), contains, out))
    return lines[0], g
