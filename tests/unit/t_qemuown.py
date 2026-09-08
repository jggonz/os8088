#!/usr/bin/env python3
"""Every test that LAUNCHES a QEMU has to kill it again.

THE DEFECT THIS GUARDS.  `make test` daemonises the emulator and returns, so
the process outlives the script that started it.  For most of this tree's life
the only thing that ever killed one was the NEXT run's kill-stale — which
reaches an instance in the same checkout with the same pidfile, and nothing
else.  A row that FAILED simply left its emulator running.

Measured, and it is why this file exists: two `qemu-system-i386` processes
**five hours old**, left by two `trkscrl` classification runs in two different
worktrees, still holding `build/os8088.img`.  `ps2mouse` — which launches with
a pidfile of its own, so it had never had a chance to kill them — failed the
pre-merge gate with `Failed to get "write" lock`, and passed the moment they
were killed.  **The cost of the leak is paid by an unrelated row, hours later,
wearing a message about the wrong subject.**  docs/plans/HANDOFF-SOAK-FINDINGS.md B9.

THE RULE IS NARROW ON PURPOSE, because a wide one would need an exception list
and an exception list is where this class comes back:

    a file that launches QEMU must register a teardown with os88qemu.own()

It says nothing about HOW the file drives the guest, nothing about `finally`,
and nothing about files that merely DRIVE an instance somebody else started —
`tests/xmcheck.py` takes a socket on its command line and does exactly that,
and killing another session's machine is the failure this guards rather than
one to move.  That is why the rule keys on the LAUNCH and not on the socket.

WHAT COUNTS AS A LAUNCH, and both halves are needed:

  * an argv list whose head is the Makefile and whose next word is the test
    target — the nine that boot that way.  Spelled out here it would match
    `t_registry`'s own make-detector and declare THIS file a row that builds;
    that gate's docstring says the same thing about itself, and this comment
    is the second time the tree has paid for the lesson.
  * a command carrying `-pidfile` — the three that build their own, and which
    deliberately never write `qemu-system-i386` whole on a command line (a
    `pkill -f` would match the killing shell itself: exit 144, nothing dead).
    So this cannot key on the binary's name either.

MUTATION-TESTED BOTH WAYS while it was written: deleting an `own()` call fails
the row naming that file, and deleting the launch makes the file stop being
asked.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from harness import check, done                                # noqa: E402

TESTS = os.path.dirname(HERE)

# A launch, either way round. The Makefile form is written several ways -
# subprocess.run, subprocess.Popen, a `cmd = [...]` bound first - so the match
# is on the ARGUMENT LIST rather than on the call.
#
# ASSEMBLED FROM PARTS, and not written out, for the reason
# `tests/unit/t_registry.py:_invokes_make` gives about its own docstring: that
# gate looks for a quoted `make` at the head of an argv list, so a file
# containing the literal declares ITSELF a row that shells out to the
# Makefile - and this file's row then fails a check about a thing it does not
# do. Written out first, and t_registry caught it within the minute.
_M, _T = '"' + "make" + '"', '"' + "test" + '"'
# TWO SPELLINGS OF THE SAME LAUNCH, and the second one is NOT WRITTEN OUT
# HERE for the reason the docstring gives about the first: a file that spells
# the pattern matches itself, and this one duly counted itself as a launcher
# the moment the comment described it. The argv list is the older spelling;
# the four rows that moved to os88fixture's restoring make wrapper call it by
# name with the test target as the first argument. The detector's count
# dropped 12 -> 10 the moment they did, which is what the liveness check below
# is for, and it fired within the minute: a launcher this file cannot see is a
# launcher nothing checks owns its instance.
_F = "os88" + "fixture" + r"\.make\(\s*"
LAUNCH = re.compile("(?:" + _M + r",\s*" + _T + "|" + _F + _T + r")")
PIDFILE = re.compile(r'-pidfile')
OWN = re.compile(r'\bos88qemu\.own\s*\(')


def main():
    launchers = []
    for name in sorted(os.listdir(TESTS)):
        if not name.endswith(".py") or name == "os88qemu.py":
            continue
        src = open(os.path.join(TESTS, name)).read()
        if LAUNCH.search(src) or PIDFILE.search(src):
            launchers.append((name, bool(OWN.search(src))))

    check(len(launchers) >= 12,
          "the launch detector still finds the files that launch QEMU",
          "if this drops to nothing the row passes vacuously and the guard is "
          "off - the count is the detector's own liveness check, not a budget",
          got=len(launchers), want=">= 12")

    for name, owns in launchers:
        check(owns, "tests/%s launches QEMU and registers a teardown" % name,
              "`make test` daemonises the emulator: it outlives this script "
              "unless somebody kills it. Add `os88qemu.own()` at the launch "
              "site - and at the launch site ONLY, so a script that drives an "
              "instance it did not start does not kill somebody else's "
              "machine (tests/os88qemu.py)")

    print("t_qemuown: %d QEMU launchers, %d own their instance"
          % (len(launchers), sum(1 for _, o in launchers if o)))
    done("t_qemuown")


if __name__ == "__main__":
    main()
