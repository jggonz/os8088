#!/usr/bin/env python3
"""PageDesk's one-sentence-per-PDE_* table fits the empty-view card (QA F14).

    python3 tests/unit/t_pderrlen.py

apps/pagedesk/pdudraw.c draws the failed-open sentence on ONE line of the empty
view's card, cut at the card's width; with both panels open the VGA canvas is
330 px, which is 39 cells of the 8-pixel face. Every `return "..."` in
apps/pagedesk/pduerr.c's pdu_errtext must therefore be at most 39 characters,
or its tail is lost on the glass (F14 read `Not enough memory. Close a window
and` with `try again.` cut off). Host-side, ~0 s.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MAXCH = 39


def main():
    src = open(os.path.join(ROOT, "apps", "pagedesk", "pduerr.c")).read()
    body = src[src.index("pdu_errtext(int code)"):src.index("pdu_ferr_pde")]
    rows = re.findall(r'return\s+"((?:[^"\\]|\\.)*)"', body)
    check(len(rows) >= 25, "pdu_errtext has its sentences (%d)" % len(rows))
    for r in rows:
        check(len(r) <= MAXCH, "%d chars <= %d: %r" % (len(r), MAXCH, r))
    return done('t_pderrlen')


if __name__ == "__main__":
    sys.exit(main())
