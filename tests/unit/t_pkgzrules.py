#!/usr/bin/env python3
"""t_pkgzrules: EVERY SHIPPED PACKAGE AND OVERLAY IS BUILT BY A RULE THAT PACKS
IT (SPEC.md 20.13.5, 73.14.1).

    python3 tests/unit/t_pkgzrules.py

`PKGZ` is a plain `make`'s default, and what reaches a package is ONE WORD in
its rule - `$(OS88PKG)` rather than a bare `python3 tools/os88pkg.py`, or
`$(PKGZARG)` on the end of one - plus `$(PKGZSTAMP)` among its prerequisites
so that flipping the knob rebuilds it. Leaving the word out is SILENT: the
package assembles, loads and runs exactly as before, and the only symptom is a
file that is bigger on every disk it rides. It had been left out four times
over by the time this row was written: the six small-build packages (SPEC.md
20.13.5.2), then FROTZ, FPTEST and IMGTEST, then the three open-coded C rules
(LOOM, CHELLO, COVL) - LOOM.O88 shipping at 52,760 bytes for a 28,534-byte
file - and every C package's OVERLAY, which shipped exactly as os88ovl.py cut
it until SPEC.md 73.14.1 (LOOM.OVL 43,702 bytes for 17,929).

WHY IT READS THE RULES AND NOT THE FILES. Most of what this is about - the C
packages, FROTZ, LOOM - is built ON DEMAND, so a check of build/'s contents
would see nothing in the very tree `all` runs it in. And a plain file is not
by itself a defect: os88pkg.py's `--compress-if` writes a package plain when
packing would not pay (CHELLO, HELLO, a package with PARTS), with a line
saying why. What is a defect is a rule that never ASKS. So this asks make for
its own database (`make -p -q`, which runs no recipe) and follows every file
the release ships back to the rule that produces it:

  .o88   the producing recipe runs os88pkg.py WITH a compression flag
         (`$(OS88PKG)`, `$(PKGZARG)`, or the flag as an eval'd rule expands
         it), and the rule depends on $(PKGZSTAMP);
  .OVL   the producing recipe is os88lz.py --wrap (CC_OVLZ_RULE, in
         apps/cc/Makefile.inc), and the rule depends on $(PKGZSTAMP). An
         overlay a disk names straight out of os88ovl.py is the defect -
         `$(BUILD)/<NAME>.OVL` is the plain cut the host readers want, and a
         disk recipe names `$(CC_OVLZ)/<NAME>.OVL`.

"Shipped" is $(LIVEARGS) - the live media, the one artefact whose premise is
COMPLETENESS (SPEC.md 80.6, tests/unit/t_livefull.py), so everything any
release carries is in it - plus every $(SHIPIMGS) floppy's prerequisites,
plus GATES below: the capability gates that exist to exercise the shipped
path and must therefore take it.

IT RUNS MAKE WITH A PRIVATE `BUILD=`, never against build/. Parsing the
Makefile runs its $(shell) stamps, and with a knob in the environment
$(VIDSTAMP)'s deletes build/kernel.bin (docs/plans/SOAK-PARALLEL.md 8); the
private tree is under build/ only so that nothing outside the checkout is
touched, it is keyed to the PID, and it is removed on the way out. PKGZ=lz4
is passed explicitly, so the row asks about the shipping arm whatever the
tree under it was built with.

IT CAN GO RED, and it proves so every run: before reading the real rules it
breaks two of them IN MEMORY - FROTZ's recipe with its compression flag
struck, and LOOM's disk pointed at the plain cut - and checks that each one
is reported. A checker that passes everything fails here.
"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                            # noqa: E402

# Capability gates that are not shipped software but exist to exercise the
# path shipped software takes - so a plain one would test the wrong path.
GATES = ("fptest.o88", "imgtest.o88", "chello.o88", "covl.img")

# A packing word, in either form the database can show it: a variable the
# top-level Makefile expands at run time, or the flag an eval'd rule (the C
# SDK's CC_PACKAGE) has already expanded into its recipe.
PACK_O88 = re.compile(r"\$\(OS88PKG\)|\$\(PKGZARG\)|--compress-if=|--compress=")
RUNS_PKG = re.compile(r"\$\(OS88PKG\)|os88pkg\.py")
PACK_OVL = re.compile(r"os88lz\.py --wrap")


def make_db(bdir):
    """make's own database for a tree rooted at BUILD=bdir, and three of its
    variables. Runs no recipe: -q asks, -p prints, and the goal does not
    exist, so nothing is rebuilt."""
    env = {k: v for k, v in os.environ.items()
           if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "PKGZ")}
    r = subprocess.run(["make", "-p", "-q", "-f", "Makefile",
                        "BUILD=" + bdir, "PKGZ=lz4", ".t-pkgzrules-none"],
                       cwd=ROOT, env=env, capture_output=True, text=True)
    return r.stdout


def parse(text):
    """{target: (prerequisites, recipe lines)} out of the '# Files' section,
    and {name: value} for the simply-expanded variables."""
    rules, var = {}, {}
    lines = text.split("\n")
    infiles = False
    i = 0
    while i < len(lines):
        ln = lines[i]
        i += 1
        if ln.startswith("# Files"):
            infiles = True
            continue
        if ln.startswith("# files hash-table stats"):
            infiles = False
            continue
        if not infiles:
            m = re.match(r"^([A-Za-z0-9_]+) := (.*)$", ln)
            if m:
                var[m.group(1)] = m.group(2)
            continue
        if not ln or ln[0] in "#\t ":
            continue
        m = re.match(r"^([^:]+?):{1,2}(?![:=])\s*(.*)$", ln)
        if not m or re.search(r"(^|\s)\S*[:+?]?=", m.group(2)):
            continue                    # a target-specific variable, not a rule
        prereqs = m.group(2).replace("|", " ").split()
        recipe = []
        while i < len(lines) and lines[i]:
            if lines[i].startswith("\t"):
                recipe.append(lines[i][1:])
            i += 1
        rules[m.group(1)] = (prereqs, recipe)
    return rules, var


def producer(rules, target):
    """The rule whose recipe makes TARGET: an empty recipe (`X.OVL: y.o88 ;`)
    means the first prerequisite's recipe wrote it."""
    seen = set()
    while target in rules and target not in seen:
        seen.add(target)
        pre, rec = rules[target]
        if any(l.strip() for l in rec) or not pre:
            return target
        target = pre[0]
    return target


def judge(rules, shipped, stamp):
    """[(path, what is wrong)] for every shipped .o88/.OVL whose rule does
    not pack it."""
    bad = []
    for path in sorted(shipped):
        low = path.lower()
        if not (low.endswith(".o88") or low.endswith(".ovl")):
            continue
        tgt = producer(rules, path)
        if tgt not in rules:
            bad.append((path, "no rule makes it"))
            continue
        pre, rec = rules[tgt]
        body = "\n".join(rec)
        if low.endswith(".ovl"):
            if not PACK_OVL.search(body):
                bad.append((path, "made by %s's recipe, which does not pack it "
                                  "- a disk names $(CC_OVLZ)/<NAME>.OVL, the "
                                  "copy CC_OVLZ_RULE writes" % tgt))
                continue
        else:
            if not RUNS_PKG.search(body):
                continue                # copied or assembled elsewhere:
                                        # its own producer is judged instead
            if not PACK_O88.search(body):
                bad.append((path, "%s's recipe runs os88pkg.py with no "
                                  "compression flag - use $(OS88PKG)" % tgt))
                continue
        if stamp not in pre:
            bad.append((path, "%s does not depend on $(PKGZSTAMP), so "
                              "flipping PKGZ would not rebuild it" % tgt))
    return bad


def main():
    bdir = os.path.join("build", ".t_pkgzrules-%d" % os.getpid())
    try:
        text = make_db(bdir)
    finally:
        shutil.rmtree(os.path.join(ROOT, bdir), ignore_errors=True)
    rules, var = parse(text)
    stamp = var.get("PKGZSTAMP", "")
    if not check(len(rules) > 1000 and stamp,
                 "make printed its database (%d rules, PKGZSTAMP %r)"
                 % (len(rules), stamp),
                 why="without the database this row would pass on nothing"):
        done("t_pkgzrules")

    def b(name):
        return bdir + "/" + name

    shipped = set()
    for word in var.get("LIVEARGS", "").split():
        shipped.add(word.split(":")[-1])
    for img in var.get("SHIPIMGS", "").split():
        shipped.update(rules.get(img, ([], []))[0])
    for g in GATES:
        if g.endswith(".img"):
            shipped.update(rules.get(b(g), ([], []))[0])
        else:
            shipped.add(b(g))
    shipped = {p for p in shipped if p.startswith(bdir + "/")}

    # THE NEGATIVE CONTROL: two rules broken in memory must both be caught.
    for name in ("frotz.o88", "LOOM.OVL"):
        check(any(b(n) in shipped for n in (name, "ovlz/" + name)),
              "the negative control's subject %s is shipped" % name,
              why="the control below would prove nothing about a file "
                  "this row does not look at")
    broken = dict(rules)
    pre, rec = broken[b("frotz.o88")]
    broken[b("frotz.o88")] = (pre, [PACK_O88.sub("python3 tools/os88pkg.py", l)
                                    for l in rec])
    caught = judge(broken, {b("frotz.o88")}, stamp)
    check(len(caught) == 1, "a FROTZ rule with its compression flag struck "
          "is reported", got=caught, want="one failure",
          why="the checker could not go red, so its green means nothing")
    caught = judge(rules, {b("LOOM.OVL")}, stamp)
    check(len(caught) == 1, "a disk naming the plain $(BUILD)/LOOM.OVL is "
          "reported", got=caught, want="one failure",
          why="the checker could not go red, so its green means nothing")

    # THE REAL RULES.
    n = sum(1 for p in shipped if p.lower().endswith((".o88", ".ovl")))
    bad = judge(rules, shipped, stamp)
    for path, what in bad:
        check(False, "%s: %s" % (path.replace(bdir, "$(BUILD)", 1), what),
              why="it ships UNPACKED - the same program, a bigger file on "
                  "every disk (SPEC.md 20.13.5)")
    check(n >= 50, "the shipped set is the whole release (%d .o88/.OVL files)"
          % n, why="a short list is a parse that missed $(LIVEARGS)")
    print("t_pkgzrules: %d shipped .o88/.OVL files, every rule packs"
          % n if not bad else "t_pkgzrules: %d of %d do not pack" % (len(bad), n))
    done("t_pkgzrules")


if __name__ == "__main__":
    main()
