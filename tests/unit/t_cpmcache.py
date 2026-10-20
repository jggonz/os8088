#!/usr/bin/env python3
"""t_cpmcache: the committed CP/M cache matches the pins (SPEC.md 74.6.1).

    python3 tests/unit/t_cpmcache.py

`apps/runcpm/cache/cpmcache.zip` is what the RUNCPM disks and the live media
are built from, so a pin that moves without a repack sends the next clean
build back to Google Drive a file at a time - silently, because the fetch
scripts fall back to the network rather than failing. This row makes that
drift loud: every pinned file present and matching, and nothing in the zip
that no pin names.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                             # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import cpmcache                                             # noqa: E402

problems = cpmcache.check()
check(not problems, "apps/runcpm/cache/cpmcache.zip agrees with "
      "getruncpm.PINNED and getcpmsw.PINNED",
      why="a pin the zip lacks is a clean build downloading again; "
          "`tools/cpmcache.py --pack` after fetching is the fix",
      got="\n          ".join(problems), want="no problems")
check(len(cpmcache.pins()) > 80, "both PINNED tables were read",
      why="a check over an empty pin list passes on anything")
done("t_cpmcache")
