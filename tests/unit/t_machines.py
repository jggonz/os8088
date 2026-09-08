#!/usr/bin/env python3
"""No row may name a machine whose ROM this tree has not got.

    python3 tests/unit/t_machines.py

WHY.  `tools/martypc/configs/os8088_machines.toml` carries eleven machines
asking for `rom_set = "ibm5150_82_v4"` - the 27 OCT 82 IBM 5150 BIOS, which is
IBM's, has never been licensed for redistribution, and cannot be in this tree
(CONTRIBUTING.md 6).  The MartyPC of the day did not refuse a machine whose
romset was absent - it fell back to `glabios_pc` and said nothing; the pinned
build exits with `ROM set ... not found`, which is loud but still not a row.

So a test naming `os8088_5150_cga` on a box without that ROM runs on a
DIFFERENT MACHINE than it named, passes, and reports a pass about a machine it
never booted.  Nine rows were in that state and four of them were registered;
none had ever run on the ROM it asked for, and nothing anywhere said so
(docs/plans/HANDOFF-SOAK-FINDINGS.md E3, `tests/int0sweep.py`'s own description).

THIS GATE IS HOST-SIDE AND FREE.  It reads the machine table and the test
sources; it boots nothing.  Three checks:

  1. Every machine any test names EXISTS in the TOML.  A typo currently
     produces the same silent fallback as a missing ROM.
  2. No test names an IBM-romset machine directly.  It goes through
     `os88marty.machine()`, which resolves to the GLaBIOS twin unless the
     caller states a reason - so the choice, and the reason, are in the
     source where a reviewer sees them.
  3. Every twin in `os88marty.IBM_TWIN` exists and differs from its original
     in `rom_set` ALONE.  A twin that has drifted is worse than no twin: the
     row runs, and the difference it measures is the config's rather than the
     kernel's.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty                                             # noqa: E402

TOML = os.path.join(ROOT, "tools", "martypc", "configs", "os8088_machines.toml")

# Rows that may still name an IBM machine directly, each with the reason it
# earns one.  EMPTY, and that is the finding rather than an oversight: every
# row that named one was calling a routine through the debugger over memory it
# had zeroed itself, or trapping an interrupt with a breakpoint that fires
# before any ROM handler runs.  None of those touch the BIOS at all.
#
# What WOULD earn a place here is written beside `os8088_5150_cga_gla` in the
# TOML: a disk or boot NUMBER, a PERFORMANCE.md figure meant to be compared
# against the field 5150, or something resting on the CGA mode set.  Add the
# row and the sentence together, or not at all.
ALLOWED_IBM = {}


def machines():
    """name -> {key: value} for every [[machine]] block in the config."""
    src = open(TOML).read()
    out = {}
    for block in re.split(r"\[\[machine\]\]", src)[1:]:
        # Stop at the next top-level table so a machine's own [machine.memory]
        # sub-tables come along and the NEXT machine's do not.
        name = re.search(r'name\s*=\s*"([^"]+)"', block)
        if not name:
            continue
        out[name.group(1)] = block
    return out


def named_by_tests():
    """Every os8088_* machine name that appears in tests/, with its files.

    Source text rather than import, because a row that picks its machine at
    import time is exactly the shape this is checking and importing it would
    run it.
    """
    out = {}
    tdir = os.path.join(ROOT, "tests")
    for dirpath, _dirs, files in os.walk(tdir):
        for f in sorted(files):
            if not f.endswith(".py"):
                continue
            p = os.path.join(dirpath, f)
            src = open(p, errors="replace").read()
            # Only where it is being USED as a machine - a name inside a
            # docstring is provenance, not a launch.  The launch forms are
            # `machine="x"`, `machine = ("x"...`, and a bare "x" in a tuple of
            # machine rows; all three have the name in quotes on a line that
            # is not the start of a comment.
            depth = 0
            for line in src.splitlines():
                s = line.strip()
                if s.startswith("#"):
                    continue
                # A name INSIDE `machine("...")` is the resolver being asked
                # for the twin, which is the whole point - so it is not a
                # direct naming and is taken out before the scan. Doing it by
                # deletion rather than by a negative look-behind keeps the
                # pattern readable and handles two calls on one line.
                line = re.sub(r'\bmachine\(\s*"os8088_[a-z0-9_]+"', "machine(",
                              line)
                # ...AND SO IS ONE HANDED TO A ROUTINE THAT RESOLVES IT
                # ITSELF. os88ui.boot calls os88marty.machine on its own
                # `machine=`, so `boot(img, machine="os8088_5150_cga")` is
                # already the twin - and the whole point of that verb is that
                # a caller does not have to remember. The call usually spans
                # several lines, so this tracks its parens rather than
                # matching one line: RESOLVERS is the list to add to when the
                # next such entry point appears.
                if depth <= 0:
                    depth = 0
                    if any(r in line for r in RESOLVERS):
                        depth = line.count("(") - line.count(")")
                        line = re.sub(r'machine\s*=\s*"os8088_[a-z0-9_]+"',
                                      "machine=", line)
                else:
                    depth += line.count("(") - line.count(")")
                    line = re.sub(r'machine\s*=\s*"os8088_[a-z0-9_]+"',
                                  "machine=", line)
                for m in re.finditer(r'"(os8088_[a-z0-9_]+)"', line):
                    out.setdefault(m.group(1), set()).add(
                        os.path.relpath(p, ROOT))
    return out


# Entry points that call `os88marty.machine` on their own `machine=`, so a
# bare IBM name handed to one of them is already resolved.
RESOLVERS = ("os88ui.boot(", "ui.boot(")


def main():
    bad = []
    table = machines()
    used = named_by_tests()

    # 1. every machine a test names must exist
    for name, files in sorted(used.items()):
        if name not in table:
            bad.append("%s is named by %s and is NOT in %s - MartyPC resolves "
                       "an unknown machine the same silent way it resolves a "
                       "missing ROM"
                       % (name, ", ".join(sorted(files)),
                          os.path.relpath(TOML, ROOT)))

    # 2. no test may name an IBM-romset machine directly
    for name, files in sorted(used.items()):
        if name not in os88marty.IBM_TWIN:
            continue
        for f in sorted(files):
            if ALLOWED_IBM.get(f) or f.endswith("t_machines.py"):
                continue
            bad.append(
                "%s names %s, whose ROM cannot be in this tree - so on a box "
                "without a private copy it runs on %s and says nothing.\n"
                "        Use os88marty.machine(%r), which resolves to the "
                "twin; or, if this row genuinely needs the period ROM, pass "
                "why_ibm=<the reason> and add it to ALLOWED_IBM here."
                % (f, name, os88marty.IBM_TWIN[name], name))

    # 3. every twin must exist, and differ in rom_set alone
    for ibm, twin in sorted(os88marty.IBM_TWIN.items()):
        if ibm not in table:
            bad.append("IBM_TWIN names %s, which is not in the config" % ibm)
            continue
        if twin not in table:
            bad.append("IBM_TWIN maps %s -> %s, and %s is not in the config"
                       % (ibm, twin, twin))
            continue
        a, b = _norm(table[ibm], ibm), _norm(table[twin], twin)
        if a != b:
            bad.append(
                "%s and its twin %s differ in more than rom_set, so a row "
                "moved onto the twin would measure the CONFIG's difference "
                "and call it the kernel's.\n        only-in-%s: %s\n"
                "        only-in-%s: %s"
                % (ibm, twin, ibm, sorted(a - b) or "-", twin, sorted(b - a) or "-"))

    if bad:
        print("t_machines: %d problem(s)" % len(bad))
        for b in bad:
            print("  FAIL %s" % b)
        return 1
    print("t_machines: ok - %d machine(s) named by tests, %d IBM/twin pair(s), "
          "no row naming a ROM this tree has not got"
          % (len(used), len(os88marty.IBM_TWIN)))
    return 0


def _norm(block, name):
    """A machine block as a comparable set of settings.

    The name and the romset are dropped - they are the two things a twin is
    ALLOWED to differ in - and comments and blank lines go with them, so a
    twin carrying its own explanation is still a twin.
    """
    out = set()
    for line in block.splitlines():
        s = line.split("#", 1)[0].strip()
        if not s:
            continue
        if s.startswith("name") or s.startswith("rom_set"):
            continue
        out.add(s)
    return out


if __name__ == "__main__":
    sys.exit(main())
