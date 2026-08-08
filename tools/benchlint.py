#!/usr/bin/env python3
"""benchlint - refuse a benchmark row that measures the wrong body.

`tests/benchlib.inc` times whatever `[bl_body]` points at and labels the row
with whatever `SI` points at.  Both are module words, so a row that does not
set `[bl_body]` measures whatever the PREVIOUS row measured - and the report
still prints, still looks plausible, and is wrong by an arbitrary factor.

That is not hypothetical.  `GFX_FILL 256x128` and `GFX_FILL 256x1` carried
their body from the 64x64 fill; two later commits inserted the GFX_LINE and
GFX_LSTEP blocks between the two, and from then on both rows measured
`gb_b_lstepv8`.  Nobody could see it, because a fill and a line-step happen
to cost about the same: the two rows agreed with each other to 0.4%, the two
derived rows they feed printed a clean `0`, and a whole field set was taken
that way (PERFORMANCE.md Part 9 Set 11).

So the rule this enforces is: EVERY `call bl_run` must be preceded by a
`mov word [bl_body], ...` since the previous one.  Repeating the same body on
consecutive rows is fine and often right - a row pair that changes only the
rectangle wants the identical body - it just has to say so, because saying so
is what an insertion cannot silently break.

Run by `make bench`; also runnable by hand on any harness that uses benchlib.
"""

import re
import sys

BODY = re.compile(r'^\s*mov\s+word\s+\[bl_body\]\s*,\s*(\w+)', re.I)
RUN = re.compile(r'^\s*call\s+bl_run\b', re.I)
LABEL = re.compile(r'^\s*mov\s+si\s*,\s*(gb_r_\w+|sb_r_\w+)', re.I)


def check(path):
    body_line = 0
    body_name = None
    label = None
    prev_run = 0
    bad = []
    for n, line in enumerate(open(path, encoding='utf-8', errors='replace'), 1):
        line = line.split(';')[0]
        m = BODY.match(line)
        if m:
            body_line, body_name = n, m.group(1)
        m = LABEL.match(line)
        if m:
            label = m.group(1)
        if RUN.match(line):
            if body_line < prev_run:
                bad.append((n, label, body_name, body_line))
            prev_run = n
    return bad


def main(argv):
    if len(argv) < 2:
        print('usage: benchlint.py <harness.asm> [...]', file=sys.stderr)
        return 2
    rc = 0
    for path in argv[1:]:
        for n, label, body, body_line in check(path):
            print('%s:%d: bl_run for %s CARRIES its body (%s, last set at '
                  'line %d) - set [bl_body] for this row' %
                  (path, n, label or '?', body or 'never set', body_line),
                  file=sys.stderr)
            rc = 1
    if rc == 0:
        print('benchlint: every bl_run sets its own body')
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv))
