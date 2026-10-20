#!/usr/bin/env python3
"""`make` must mean `all`, and the MartyPC preflight must run before the clone.

THE INCIDENT THIS ROW IS MADE OF is not a missing dependency - it is the
`deps` target added to fix one. It was placed near the top of the Makefile so
a reader would find it, and **make takes the first target in the file as the
default goal**, so a bare `make` silently became `make deps`: it printed a
dependency report, built no floppy, and exited **0**. No tier could see it,
because a build that succeeds and produces nothing looks exactly like a build
that had nothing to do. `.DEFAULT_GOAL` is one line and makes the position of
every rule below it a layout question rather than a behavioural one.

WHY THIS IS A TEXT CHECK AND NOT `make -p`, which is the obvious spelling and
was tried first: `make -p` has to PARSE this Makefile, and the parse runs its
`$(shell ...)` calls - **73 seconds**, twice, against a 30-second budget for
the whole fast tier. The property that actually matters is textual anyway
(is the goal NAMED, and named before anything could claim it), so the cheap
check is also the exact one. `make -q all` at 0.3s was measured as an
alternative and rejected: it agrees with a bare `make -q` for the wrong
reason on a tree that is not fully built.

The rest of the row guards the preflight in `tools/martypc/build.sh`, whose
correctness is an ORDERING - a libudev probe placed after the clone would
satisfy any test that merely grepped for it and would still be the
four-minute failure it exists to prevent.
"""
from pathlib import Path
import platform
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]

# A rule line: a target, then a colon that is not `:=`. Anything starting `.`
# is one of make's own special targets (.PHONY, .SUFFIXES) and claims nothing.
RULE = re.compile(r"^([A-Za-z0-9_][A-Za-z0-9_./%$()-]*)\s*::?(?!=)")


def makefile_lines():
    text = (ROOT / "Makefile").read_text(errors="replace")
    for n, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("#") or line.startswith("\t"):
            continue
        yield n, line


class DefaultGoalTests(unittest.TestCase):
    def test_default_goal_is_named_all(self):
        goal = [line for _, line in makefile_lines()
                if line.startswith(".DEFAULT_GOAL")]
        self.assertTrue(
            goal,
            ".DEFAULT_GOAL is not set. `make` then means whichever rule "
            "happens to sit first in the file - which is how `make` once "
            "meant `make deps`, built no floppy and exited 0.")
        self.assertIn(
            "all", goal[0].split("=", 1)[1].split(),
            "`make` with no target is no longer `all`: %s" % goal[0].strip())

    def test_default_goal_precedes_every_rule(self):
        """Naming the goal only helps if nothing can claim it first."""
        goal_at = first_rule = None
        first_rule_name = None
        for n, line in makefile_lines():
            if goal_at is None and line.startswith(".DEFAULT_GOAL"):
                goal_at = n
            m = RULE.match(line)
            if m and first_rule is None:
                first_rule, first_rule_name = n, m.group(1)
        self.assertIsNotNone(first_rule, "no rules found - is this a Makefile?")
        if first_rule_name == "all":
            return                     # `all` is first: the goal is right anyway
        self.assertIsNotNone(goal_at, "no .DEFAULT_GOAL and the first rule is "
                                      "`%s`, not `all`" % first_rule_name)
        self.assertLess(
            goal_at, first_rule,
            "the first rule in the Makefile is `%s` (line %d) and "
            ".DEFAULT_GOAL is not set until line %d, so `make` means `%s`"
            % (first_rule_name, first_rule, goal_at, first_rule_name))

    def test_deps_targets_exist(self):
        """Both spellings the docs tell people to type."""
        names = {m.group(1) for _, line in makefile_lines()
                 if (m := RULE.match(line))}
        for target in ("deps", "deps-check"):
            self.assertIn(target, names,
                          "`make %s` is documented and does not exist" % target)


class SetupScriptTests(unittest.TestCase):
    def test_check_cannot_reach_the_install_path(self):
        """--check reports; it must never install.

        It is what `make deps-check` and any "is this box ready" question
        call, so a --check that shelled out to apt would take tens of seconds
        and stop being typed - which defeats the point of having one.
        """
        body = (ROOT / "tools/setup-linux.sh").read_text()
        self.assertLess(
            body.index('if [ "$CHECK" = 1 ]'), body.index("$APT update"),
            "--check can reach the apt path: it reports and changes nothing")

    def test_check_runs_and_says_something(self):
        r = subprocess.run(["sh", str(ROOT / "tools/setup-linux.sh"), "--check"],
                           cwd=ROOT, capture_output=True, text=True, timeout=120)
        if platform.system() == "Darwin":
            # The script's OWN contract on a Mac: refuse with exit 2 and name
            # tools/setup-macos.sh, before --check is even parsed. The row
            # runs in the fast tier, and the fast tier runs on every `make`,
            # so the maintainer's Mac must see this branch and not a red
            # build. Not (0, 1, 2): 2 is specifically the Darwin refusal, and
            # a Linux box exiting 2 for any other reason must still fail.
            self.assertEqual(r.returncode, 2,
                             "--check on a Mac returned %d; the script refuses "
                             "Darwin with 2" % r.returncode)
            self.assertIn("setup-macos.sh", r.stdout + r.stderr,
                          "the Mac refusal must name tools/setup-macos.sh")
            return
        self.assertIn(r.returncode, (0, 1),
                      "--check returned %d; it reports (0) or names what is "
                      "missing (1)" % r.returncode)
        self.assertTrue((r.stdout + r.stderr).strip(), "--check said nothing")

    def test_syntax(self):
        for rel in ("tools/setup-linux.sh", "tools/martypc/build.sh"):
            r = subprocess.run(["sh", "-n", str(ROOT / rel)],
                               capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0,
                             "%s does not parse: %s" % (rel, r.stderr))


class MartyPreflightTests(unittest.TestCase):
    def test_libudev_probe_precedes_the_clone(self):
        body = (ROOT / "tools/martypc/build.sh").read_text()
        self.assertLess(
            body.index("pkg-config --exists libudev"),
            body.index('git clone "$REPO"'),
            "build.sh probes for libudev AFTER cloning MartyPC, which is the "
            "late failure the probe exists to prevent")

    def test_repair_is_gated_on_root(self):
        """It must not apt-install on a contributor's own workstation.

        A disposable container runs as root and a developer's box does not,
        so that one test is the whole difference between a helpful build
        script and one that installs software behind somebody's back.
        """
        body = (ROOT / "tools/martypc/build.sh").read_text()
        probe = body.index("pkg-config --exists libudev")
        repair = body.find("setup-linux.sh", probe)
        if repair < 0:
            return                     # it does not auto-repair at all: safe
        gate = body.find('[ "$(id -u)" = 0 ]', probe)
        self.assertNotEqual(gate, -1,
                            "build.sh runs tools/setup-linux.sh with no root "
                            "check at all, so `make marty` would apt-install "
                            "on a contributor's own workstation")
        self.assertLess(gate, repair,
                        "build.sh runs the installer before checking it is "
                        "root")


if __name__ == "__main__":
    unittest.main()
