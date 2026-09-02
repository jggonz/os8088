#!/usr/bin/env python3
"""os88rtf.py - generate an RTF document (SPEC.md 73.12.3) from the same
line-based markup (.wtx) that tools/os88doc.py turns into a native .DOC.

Usage: os88rtf.py SOURCE.wtx -o OUT.RTF

The output is DETERMINISTIC: the same source produces the same bytes, no
timestamps - the shipped WELCOME.RTF can be rebuilt byte-for-byte, which is
what lets tools/os88disk.py pin a volume's every byte (CLAUDE.md).

THE MARKUP PARSER IS os88doc.py's, imported rather than copied. That file
owns .wtx and this one owns the RTF spelling of it; a second parser would be
a second dialect, and the two documents would drift in the direction nobody
tests. (The .DOC format itself is the opposite case on purpose - it has a
SECOND implementation in tools/wordfmt.py, because there the point is to check
a binary layout against an independent reading of the same spec, SPEC.md
65.4.2. Markup has no such spec to disagree about.)

WHAT IT EMITS is what apps/cword/cwrtfio.c's writer emits, in the same order
and with the same defaults (cw_rrb in apps/cword/cwrtftbl.c: paper 12240 x
15840, margins 1800/1800/1440/1440, \\deftab720, \\fs24), so a file this
produces is the shape of one cword saved. It is ordinary RTF 1 - any reader
takes it - and the paragraph properties the markup carries are emitted in
full even though cword's reader is documented to drop them (SPEC.md 73.12.3):
the file is not the place to encode one reader's narrowing.
"""

import argparse
import importlib.util
import os
import sys

# os88doc.py is a sibling and not a module on the path - and it must be found
# from THIS file's directory rather than the caller's cwd, because the Makefile
# runs it from the repo root and a person runs it from anywhere.
_SPEC = importlib.util.spec_from_file_location(
    "os88doc", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "os88doc.py"))
_DOC = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_DOC)
parse_line = _DOC.parse_line
TWIPCELL = _DOC.TWIPCELL                # one cell = 1/10 inch = 144 twips

# The character spans .wtx can set, in the order they are emitted, each with
# the control word that turns it on and the one that turns it off. Word
# underline and double underline are their own keywords in the RTF cword
# reads; small caps is not one cword has, and \scaps is emitted anyway because
# this is a file format and not a screen (an unknown control word is ignored
# by that reader without losing its text - apps/cword/cwrtfio.c).
SPANS = [
    ('b', '\\b ',      '\\b0 '),
    ('i', '\\i ',      '\\i0 '),
    ('u', '\\ul ',     '\\ulnone '),
    ('w', '\\ulw ',    '\\ulnone '),
    ('d', '\\uldb ',   '\\ulnone '),
    ('k', '\\scaps ',  '\\scaps0 '),
]
ATTR = _DOC.ATTR                        # the same bit per span letter

# The paragraph alignment .wtx's ALIGN values mean, by index.
JC = ['\\ql', '\\qc', '\\qr', '\\qj']
# ...and its line spacing: single, one and a half, double, in twips of line
# height. 240 is RTF's 12pt line, which is what \sl0 means anyway.
SL = [0, 360, 480]


def rtf_escape(ch):
    """One document character as RTF source."""
    if ch in ('\\', '{', '}'):
        return '\\' + ch
    if ch == '\t':
        return '\\tab '
    return ch


def paragraph(pap, chars, out):
    """One .wtx line as one RTF paragraph, ending in \\par."""
    flags, left, first, right = pap
    align = flags & 3
    spacing = (flags >> 2) & 3
    before = (flags >> 4) & 1

    # \pard first, so a paragraph never inherits the last one's properties -
    # this is the whole reason the generator can emit properties per paragraph
    # rather than tracking what changed.
    out.append('\\pard')
    if align:
        out.append(JC[align])
    if left:
        out.append('\\li%d' % (left * TWIPCELL))
    if first:
        # .fi is SIGNED in the markup (a hanging indent is negative) and the
        # byte the .DOC carries is two's complement; here it is just a number.
        n = first - 256 if first > 127 else first
        out.append('\\fi%d' % (n * TWIPCELL))
    if right:
        out.append('\\ri%d' % (right * TWIPCELL))
    if spacing:
        out.append('\\sl%d' % SL[spacing])
    if before:
        out.append('\\sb%d' % TWIPCELL)
    out.append(' ')

    attr = 0
    for code, a in chars:
        for key, on, off in SPANS:
            bit = ATTR[key]
            if (a & bit) != (attr & bit):
                out.append(on if (a & bit) else off)
        attr = a
        out.append(rtf_escape(chr(code)))
    # Close what is still open: '}' would end their scope, but a reader that
    # concatenates files is a real thing and the keywords cost nothing.
    for key, on, off in SPANS:
        if attr & ATTR[key]:
            out.append(off)
    out.append('\\par\r\n')


def build(src_lines):
    out = ['{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fmodern os8088 8x8;}}\r\n',
           '\\paperw12240\\paperh15840\\margl1800\\margr1800'
           '\\margt1440\\margb1440\\deftab720\r\n',
           '\\pard\\plain\\f0\\fs24\r\n']
    for line in src_lines:
        pap, chars = parse_line(line)
        paragraph(pap, chars, out)
    out.append('}\r\n')
    return ''.join(out).encode('ascii')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source')
    ap.add_argument('-o', '--out', required=True)
    args = ap.parse_args()

    lines = []
    with open(args.source, 'r', encoding='ascii') as fh:
        for raw in fh.read().split('\n'):
            line = raw.rstrip('\r')
            if line.startswith(';'):
                if line.startswith(';;'):   # ';;' escapes a literal leading ';'
                    lines.append(line[1:])
                continue
            lines.append(line)
    while lines and lines[-1] == '':        # a trailing newline is not a
        lines.pop()                         # paragraph

    body = build(lines)
    with open(args.out, 'wb') as fh:
        fh.write(body)
    print('os88rtf: %s -> %s (%d bytes, %d paragraph(s))'
          % (args.source, args.out, len(body), len(lines)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
