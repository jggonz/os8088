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

It also refuses a LABEL WIDER THAN THE VALUE COLUMN.  `bl_kv` writes the
label at column 0 and the number right-aligned in a field starting at
`BL_C_N`, so a label of more than `BL_C_N` characters has its own tail
overwritten by its own value - and the row still prints, still lines up with
its neighbours, and reads as a truncated word run into a number.  A field
report came back with four of them at once (`adapters available (he0006`),
which is how this got written: the labels had been counted by eye.  The
column is read out of `benchlib.inc` rather than repeated here, so moving it
moves the rule.

A label handed to `bl_sline` is a whole LINE and may be as wide as the
report, so the rule applies only to labels that reach something which prints
a value beside them - which is decided by what consumes `SI` next.

Run by `make bench`; also runnable by hand on any harness that uses benchlib.
"""

import os
import re
import sys

BODY = re.compile(r'^\s*mov\s+word\s+\[bl_body\]\s*,\s*(\w+)', re.I)
RUN = re.compile(r'^\s*call\s+bl_run\b', re.I)
LABEL = re.compile(r'^\s*mov\s+si\s*,\s*(gb_r_\w+|sb_r_\w+)', re.I)

# `sb_l_foo:  db '  something', 0`
DECL = re.compile(r"^\s*(\w+):\s+db\s+'([^']*)'\s*,\s*0", re.I)
SETSI = re.compile(r'^\s*mov\s+si\s*,\s*(\w+)\s*$', re.I)
CALL = re.compile(r'^\s*call\s+(\w+)', re.I)
COLN = re.compile(r'^(BL_C_\w+)\s+equ\s+(\d+)', re.I | re.M)

# The routines that print a VALUE beside the label in SI, and WHICH COLUMN
# each writes at - because they do not all use the same one, and a rule with
# a single column is wrong in both directions. `sb_us` puts a microsecond
# figure at BL_C_US, so a 27-character label is fine in front of it and the
# identical label in front of `sb_num` is not. Explicit, because the
# alternative is chasing `call` chains - and it errs SHORT: a printer missing
# from this map makes the rule report less, never block a good build.
VALUE_PRINTERS = {
    'bl_kv': 'BL_C_N', 'bl_kvs': 'BL_C_N',
    'sb_num': 'BL_C_N', 'sb_hex': 'BL_C_N', 'sb_v2': 'BL_C_N',
    'sb_mb': 'BL_C_N', 'sb_mbx': 'BL_C_N',
    'sb_cb': 'BL_C_N', 'sb_cbx': 'BL_C_N',
    'sb_rah_row': 'BL_C_N',
    'sb_us': 'BL_C_US',
    'gb_num': 'BL_C_N', 'gb_hex': 'BL_C_N',
}


def value_columns(path):
    """The BL_C_* map, out of the benchlib.inc beside this harness."""
    here = os.path.dirname(os.path.abspath(path))
    for cand in (os.path.join(here, 'benchlib.inc'),
                 os.path.join(os.path.dirname(here), 'benchlib.inc')):
        if os.path.exists(cand):
            txt = open(cand, encoding='utf-8', errors='replace').read()
            return {m.group(1).upper(): int(m.group(2))
                    for m in COLN.finditer(txt)}
    return None


def check_labels(path):
    cols = value_columns(path)
    if not cols:
        return []
    lines = open(path, encoding='utf-8', errors='replace').readlines()
    width, decl_line = {}, {}
    for n, line in enumerate(lines, 1):
        m = DECL.match(line.split(';')[0])
        if m:
            width[m.group(1)] = len(m.group(2))
            decl_line[m.group(1)] = n
    # ...and which of them are handed to something that prints a value. SI is
    # consumed by the NEXT call whatever it is, so one pending label and a
    # clear on every call is the whole model - `mov si, header` followed by
    # `call bl_sline` is a line, not a row.
    valued, pending = {}, None
    for line in lines:
        line = line.split(';')[0].rstrip()
        m = SETSI.match(line)
        if m:
            pending = m.group(1)
            continue
        m = CALL.match(line)
        if m and pending is not None:
            col = cols.get(VALUE_PRINTERS.get(m.group(1), ''))
            if col is not None:           # ...and the NARROWEST one wins: the
                valued[pending] = min(    # same label may head two rows
                    valued.get(pending, col), col)
            pending = None
    return sorted((decl_line[n], n, width[n], col)
                  for n, col in valued.items()
                  if n in width and width[n] > col)


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
        for n, name, w, col in check_labels(path):
            print('%s:%d: %s is %d characters against a value column at %d - '
                  'its own value overwrites its tail' % (path, n, name, w, col),
                  file=sys.stderr)
            rc = 1
        for n, label, body, body_line in check(path):
            print('%s:%d: bl_run for %s CARRIES its body (%s, last set at '
                  'line %d) - set [bl_body] for this row' %
                  (path, n, label or '?', body or 'never set', body_line),
                  file=sys.stderr)
            rc = 1
    if rc == 0:
        print('benchlint: every bl_run sets its own body, and every\n'
              '            key/value label fits its column')
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv))
