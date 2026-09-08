#!/usr/bin/env python3
"""The small build of a package must cost the SHIPPED one nothing (SPEC.md 27.16).

    python3 tests/unit/t_appsmall.py

`make smallapps` assembles a package a second time with -DAPP_SMALL, and two
claims hang off that. Both fail silently, which is why they are here.

**One: the gate costs the full build ZERO.** It is the same claim
docs/history/KERN-SPLIT-PLAN.md 6 set for the first removal from `kern_small`, and it
fails the same way - a `%ifdef` written round one line too many, or a field
moved out of a gated block "while we are here", and the shipped package
changes for a feature it still has. Nothing errors. So this ASSEMBLES THE
PACKAGE BOTH WAYS and compares the default arm against `build/<pkg>.o88`
byte for byte.

**Two: the small build is actually smaller.** A gate that stops reaching the
source is a gate that carries nothing, and the symptom is a floppy that is
simply the ordinary one under another name - `make smallapps` would still
build, still boot, and still be pointless. The margin below is deliberately
loose: it is here to catch ZERO, not to pin a number that a later feature
would have to keep.

What it does NOT check is behaviour - that a small-built package still edits,
saves and draws. Nothing host-side can: it wants a machine, and
`os8088_5150_gla_128k` under MartyPC is where that is answered.
"""
import hashlib
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                             # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
ROOT = os.path.abspath(ROOT)

sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88pkg                                              # noqa: E402

# (package, source, the shipped .o88 it must still equal)
PKGS = [("notepad", "apps/notepad/notepad.asm", "build/notepad.o88"),
        ("paint", "apps/paint/paint.asm", "build/paint.o88"),
        ("calc", "apps/calc/calc.asm", "build/calc.o88"),
        ("solitaire", "apps/solitaire/solitaire.asm", "build/solitair.o88"),
        ("taskmgr", "apps/taskmgr/taskmgr.asm", "build/taskmgr.o88")]

# ...and the arms BETWEEN the two, which no floppy carries and which nothing
# else would keep assembling (SPEC.md 28.12). The Task Manager's gates are a
# LADDER - `TMF_HEAP` off alone, then `TMF_MEM` off as well - and the middle
# rung is a real configuration with a measured cost that somebody may yet
# choose to ship. A rung nothing builds is a rung that silently stops
# building, which is exactly why the knob kernels are in `make test-full`'s
# matrix.
#
# Defining a flag on the command line is how a rung is selected: the source
# defines both under `%ifndef APP_SMALL`, so -DAPP_SMALL -DTMF_MEM leaves the
# memory view in and takes only the heap page out.
#
# (source, extra defines, what the rung is, must be smaller than)
RUNGS = [("apps/taskmgr/taskmgr.asm", ["-DTMF_MEM"],
          "taskmgr: the heap page alone (SPEC.md 28.12)", "taskmgr")]

# The least a small build must save to be worth having, as a fraction of the
# full build's image + bss.
#
# IT IS A FLOOR UNDER "THE DEFINE STOPPED REACHING THE SOURCE", NOT A TARGET,
# and the spread is wide on purpose: Note Pad saves ~34%, Paint ~16% (its
# canvas, undo image and clipboard are heap claims that already tier
# themselves, so its gates can only reach the resident part), Calculator ~24%,
# and SOLITAIRE ~6%.
#
# Solitaire is the floor case and the reason this number came DOWN from 10%.
# Its size pass took 123 bytes out of BOTH builds - SPEC.md 43.11 derives the
# hollow pips instead of storing them, and a shared epilogue ladder replaced
# 28 sites - and bytes taken off both arms do not show in a small/full ratio
# at all. A package can be thoroughly optimised and have very little LEFT that
# is optional; that is a good outcome, not a failing gate.
MIN_SAVING = 0.05


# The package defines the Makefile built the SHIPPED .o88 with - $(PKGSBDEF),
# which is SBDRAGOFF=/SBRATE= and nothing else today. `make test-fast` passes
# them as OS88_PKGDEFS; run by hand under a plain build the variable is empty
# and so is the list. Without this, the "default arm is the shipped bytes"
# check below assembled with NO defines and compared against a package built
# WITH them, which is right in a plain build and a wrong failure under
# `make SBDRAGOFF=1`.
DEFS = os.environ.get("OS88_PKGDEFS", "").split()


def build(src, out, small):
    cmd = ["nasm", "-f", "bin", "-w+error", "-I", "apps/"] + DEFS
    if small:
        cmd += ["-DAPP_SMALL"]
    cmd += ["-o", out, src]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return r.returncode == 0, r.stderr.strip()


def claim(path):
    """image + bss - what ONE instance takes out of the heap (SPEC.md 20.1)."""
    with open(path, "rb") as f:
        h = f.read(32)
    return int.from_bytes(h[8:10], "little") + int.from_bytes(h[10:12], "little")


def md5(path):
    """...of the IMAGE, which for a shipped package is not the file.

    Every shipped `.o88` is LZ4 on the disk now (SPEC.md 20.13.4) and the
    thing this file compares is an ASSEMBLY - so the shipped side has to be
    unwrapped or the check degenerates into "compression changes bytes", which
    it does, and says nothing about which arm was built. image_unwrap puts the
    flags byte back too, so a plain package and a compressed one made from the
    same source hash the same.
    """
    with open(path, "rb") as f:
        return hashlib.md5(os88pkg.image_unwrap(f.read())).hexdigest()


def main():
    tmp = tempfile.mkdtemp(prefix="appsmall.")
    for name, src, shipped in PKGS:
        full = os.path.join(tmp, name + ".full.bin")
        small = os.path.join(tmp, name + ".small.bin")

        ok, err = build(src, full, False)
        check(ok, "%s assembles with no define" % name, got=err, want="exit 0")
        ok, err = build(src, small, True)
        check(ok, "%s assembles with -DAPP_SMALL" % name,
              "the small arm is built by `make smallapps` and by nothing in "
              "`all`, so this is the only thing keeping it assembling at all",
              got=err, want="exit 0")
        if not ok:
            continue

        shipped_path = os.path.join(ROOT, shipped)
        if os.path.exists(shipped_path):
            check(md5(full) == md5(shipped_path),
                  "%s: the shipped .o88 is the DEFAULT arm of this source" % name,
                  "what this proves is WIRING, not history: the package on the "
                  "shipped floppy was assembled from the current source with "
                  "the current $(PKGSBDEF) (%s) and WITHOUT -DAPP_SMALL. A "
                  "Makefile that built a shipped disk with the small arm, or "
                  "with a define the test was not told about, fails here. It "
                  "does NOT prove byte-identity with the pre-APP_SMALL "
                  "package - both sides are the same source, so that is a "
                  "tautology this check cannot state" % (" ".join(DEFS) or "none"),
                  got=md5(full), want=md5(shipped_path))

        cf, cs = claim(full), claim(small)
        saved = (cf - cs) / float(cf) if cf else 0.0
        check(cs < cf, "%s: the small build claims less than the full one" % name,
              "if these match, -DAPP_SMALL has stopped reaching the source and "
              "build/smallapps*.img is the ordinary floppy under another name",
              got="%d vs %d bytes" % (cs, cf), want="smaller")
        check(saved >= MIN_SAVING,
              "%s: the small build saves at least %d%%" % (name, MIN_SAVING * 100),
              "a floor under 'the gates carry nothing', not a target - the "
              "real figure is ~34%% and is allowed to move",
              got="%.1f%%" % (saved * 100), want=">= %d%%" % (MIN_SAVING * 100))
    rungs(tmp)
    disks(tmp)
    done("t_appsmall")


def rungs(tmp):
    """the intermediate arms: they assemble, and they sit between the two."""
    for i, (src, extra, what, pkg) in enumerate(RUNGS):
        out = os.path.join(tmp, "rung%d.bin" % i)
        cmd = (["nasm", "-f", "bin", "-w+error", "-I", "apps/"] + DEFS +
               ["-DAPP_SMALL"] + extra + ["-o", out, src])
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        check(r.returncode == 0, "%s assembles" % what,
              "nothing in the tree builds this arm, so this row is the only "
              "thing that keeps it compiling (%s)" % " ".join(extra),
              got=r.stderr.strip(), want="exit 0")
        if r.returncode:
            continue
        full = os.path.join(tmp, pkg + ".full.bin")
        small = os.path.join(tmp, pkg + ".small.bin")
        if not (os.path.exists(full) and os.path.exists(small)):
            continue
        mid, cf, cs = claim(out), claim(full), claim(small)
        check(cs < mid < cf, "%s: it lands BETWEEN the two arms" % what,
              "a rung that saves nothing has stopped reaching the source; one "
              "that saves as much as the full small build means the flag "
              "below it is carrying everything and the ladder is a fiction",
              got="%d (full %d, small %d)" % (mid, cf, cs),
              want="small < this < full")


# The disks themselves: SPEC.md 42.22.1 - every gated package that reaches a
# small floppy must be the SMALL build, in EVERY folder it lands in.
#
# The Makefile's SMALLBASE substitution is what arranges that, and for one
# cycle it covered the APPS list and not the GAMES one - so Solitaire shipped
# TWICE on smallapps360.img, the small build in APPS/ and the full build in
# GAMES/, and which one ran was whichever the loader found first. The comment
# above SMALLPKGS predicted that failure exactly and the guard did not cover
# it, which is why this reads the built image instead of the variable.
SMALL_IMGS = ["build/smallapps360.img", "build/smallapps.img",
              "build/smallk/small360.img", "build/small360.img",
              "build/small.img"]


def disks(tmp):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        from t_image import Vol
    except Exception as e:                                  # pragma: no cover
        check(False, "the FAT12 reader loads", got=str(e), want="t_image.Vol")
        return

    want = {}
    for name, src, shipped in PKGS:
        base = os.path.basename(shipped).upper()            # e.g. PAINT.O88
        small = os.path.join(ROOT, "build", "smallapp", os.path.basename(shipped))
        full = os.path.join(ROOT, "build", os.path.basename(shipped))
        if os.path.exists(small) and os.path.exists(full):
            want[base] = (os.path.getsize(small), os.path.getsize(full),
                          max(os.path.getmtime(small), os.path.getmtime(full)))

    seen_any = False
    for rel in SMALL_IMGS:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            continue
        seen_any = True
        # **A STALE DISK IS NOT A WRONG LIST**, and telling them apart is the
        # whole of why the test below asks what it asks. `make` builds the
        # small SYSTEM disks and NOT `smallapps`, so any change to a gated
        # package leaves these images older than the packages they carry -
        # and an equality test against the small build's size then fails for
        # a reason that has nothing to do with the property. The image is
        # skipped when it predates the packages; what is checked on a CURRENT
        # image is that the copy on it is not the FULL build, which is the
        # defect this exists for (a Makefile list that forgot $(SMALLBASE)
        # ships both and the loader takes whichever it finds first).
        newest = max((w[2] for w in want.values()), default=0)
        if want and os.path.getmtime(path) < newest:
            check(True, "%s is older than build/smallapp - skipped, run "
                        "`make smallapps`" % rel)
            continue
        with open(path, "rb") as f:
            v = Vol(f.read(), rel)
        for folder, name11, attr, clus, size in v.walk():
            nm = (name11[:8].decode("ascii", "replace").strip() + "." +
                  name11[8:].decode("ascii", "replace").strip()).upper()
            if nm not in want:
                continue
            wsize, fsize, _ = want[nm]
            check(size != fsize or wsize == fsize,
                  "%s: %s%s is not the FULL build" % (rel, folder, nm),
                  "a gated package reached a small floppy at the FULL build's "
                  "size. The Makefile substitutes $(SMALLBASE) for the small "
                  "path per list, so a list that forgot it ships both copies "
                  "and the loader picks whichever it finds first",
                  got="%d bytes" % size,
                  want="not %d (the full build); the small build is %d"
                       % (fsize, wsize))
    if not seen_any:
        check(True, "small floppies present to walk (none built - skipped)")


if __name__ == "__main__":
    main()
