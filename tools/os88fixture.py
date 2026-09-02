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
    stamp = os.path.join(ROOT, "build", "buildnum.inc")
    before = None
    if os.path.exists(stamp):
        with open(stamp, "rb") as f:
            before = f.read()
    try:
        for t in targets:
            r = subprocess.run(["make", "-s", t], cwd=ROOT,
                               capture_output=True, text=True)
            if r.returncode:
                sys.exit("%s: `make %s` failed:\n%s%s"
                         % (os.path.basename(sys.argv[0]) or "os88fixture",
                            t, r.stdout, r.stderr))
    finally:
        if before is not None and os.path.exists(stamp):
            with open(stamp, "rb") as f:
                if f.read() != before:
                    with open(stamp, "wb") as w:
                        w.write(before)
