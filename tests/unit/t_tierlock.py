#!/usr/bin/env python3
"""THE TIER LOCK: no row joins `fast` or `full` without the owner's say-so.

    python3 tests/unit/t_tierlock.py

WHY. `fast` runs inside every `make` and `full` at every integration, so a
row added to either is charged to everybody who never touched its subject,
on every build, for as long as the row exists. Both tiers had to be recut by
hand (2026-11-11) because rows had drifted in one plausible reason at a
time. docs/WRITING-TESTS.md 2 is the rule for what belongs where; this row
is what makes the rule a gate rather than a paragraph.

WHAT IT CHECKS, off tests/suite.py and tests/tierlock.txt:
  * every `fast` or `full` row has a line in the lock, with the same tier -
    a new row, or a row moved UP, fails here until the owner adds its line;
  * every line names a row that IS in that tier - a row moved DOWN to `soak`
    (or deleted) fails until its line is deleted, so the lock stays the
    exact list and never a list of permissions nobody is using;
  * every line carries a reason after `#`.

It reads two text files and imports the registry: ~0.1 s.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402
sys.path.insert(0, os.path.join(ROOT, "tests"))
import suite                                              # noqa: E402

LOCK = os.path.join(ROOT, "tests", "tierlock.txt")
WHY = ("fast runs in every `make` and full at every integration, so both are "
       "paid by everybody; a row joins either only with the OWNER's "
       "permission and a written reason (docs/WRITING-TESTS.md 2.0). Ask, "
       "then add `<tier> <row>  # owner YYYY-MM-DD: <reason>` to "
       "tests/tierlock.txt - or register the row in `soak`")

locked, bad = {}, []
for n, ln in enumerate(open(LOCK), 1):
    s = ln.strip()
    if not s or s.startswith("#"):
        continue
    m = re.match(r"(fast|full)\s+(\S+)\s+#\s*(\S.*)$", s)
    if not m:
        bad.append("line %d: %r" % (n, s))
        continue
    locked[m.group(2)] = m.group(1)
check(not bad, "every lock line is `<tier> <row>  # <reason>`",
      "a line with no reason is a permission nobody can audit", got=bad, want=[])

tiers = {r.name: r.tier for r in suite.rows()}
live = {k: v for k, v in tiers.items() if v in ("fast", "full")}
new = sorted("%s %s" % (t, k) for k, t in live.items() if locked.get(k) != t)
check(not new, "every fast/full row is in tests/tierlock.txt at its tier",
      WHY, got=new, want=[])
gone = sorted("%s %s (now %s)" % (t, k, tiers.get(k, "unregistered"))
              for k, t in locked.items() if live.get(k) != t)
check(not gone, "every lock line names a row still in that tier",
      "a row moved to soak or deleted: delete its line, which needs no "
      "permission", got=gone, want=[])
done("tierlock: %d fast, %d full" % (
    sum(1 for t in live.values() if t == "fast"),
    sum(1 for t in live.values() if t == "full")))
