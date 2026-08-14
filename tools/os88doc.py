#!/usr/bin/env python3
"""os88doc.py - generate a native os8088 Word .DOC (SPEC.md 65.4) from a
minimal line-based markup (.wtx).

Usage: os88doc.py SOURCE.wtx -o OUT.DOC

The output is DETERMINISTIC: the same source produces the same bytes, no
timestamps - the shipped WELCOME.DOC can be rebuilt byte-for-byte.

File layout (SPEC.md 65.4): a 16-byte FIB - word magic 0xA59B, word version
1, word text length, word CHP length (== text length), word PAP entry count,
word flags (low byte = the TAIL paragraph's PAP index), 4 pad bytes - then
the text (paragraph mark = 13, tab = 9, printable 32..126), one CHP
attribute byte per character (a paragraph mark's byte is its paragraph's PAP
index), and the PAP table (count x 4 bytes).

Markup (.wtx), one PARAGRAPH per line:
  ; comment            a line starting ';' is dropped (';;' escapes a
                       literal leading ';')
  directives           leading '.'-tokens, before the text, in any order:
                         .c / .r / .j      center / right / justified
                         .sp15 / .sp2      1.5 / double line spacing
                         .open             space before (an open paragraph)
                         .li N / .fi N / .ri N
                                           left / first-line (signed) /
                                           right indent, in cells (1 cell =
                                           1/10 inch)
  {b}...{/b}           inline character spans: b bold, i italic, u underline,
                       w word-underline, d double-underline, k small caps
  a literal TAB        byte 9 (the default-stop tab)

The last line is the TAIL paragraph (no closing mark); every other line ends
in a 13 whose CHP byte is that paragraph's PAP index.
"""

import argparse
import struct
import sys

MAGIC = 0xA59B
VERSION = 1
PAPMAX = 256
MAXTEXT = 30 * 1024                     # WD_MAXKB (SPEC.md 65.3)

ATTR = {'b': 0x01, 'i': 0x02, 'u': 0x04, 'w': 0x08, 'd': 0x10, 'k': 0x20}
ALIGN = {'.c': 1, '.r': 2, '.j': 3}
SPACE = {'.sp15': 1, '.sp2': 2}


def parse_line(line):
    """One source line -> (pap 4-tuple, [(char, attr), ...])."""
    align = spacing = before = left = first = right = 0
    # leading directives
    while True:
        stripped = line.lstrip(' ')
        if not stripped.startswith('.'):
            break
        tok, _, rest = stripped.partition(' ')
        if tok in ALIGN:
            align = ALIGN[tok]
        elif tok in SPACE:
            spacing = SPACE[tok]
        elif tok == '.open':
            before = 1
        elif tok in ('.li', '.fi', '.ri'):
            val, _, rest = rest.lstrip(' ').partition(' ')
            n = int(val)
            if tok == '.li':
                left = n
            elif tok == '.fi':
                first = n
            else:
                right = n
        else:
            raise SystemExit('os88doc: unknown directive %r' % tok)
        line = rest
    if line.startswith(' '):            # one space separates directives from
        line = line[1:]                 # text; further spaces are content

    pap = (align | (spacing << 2) | (before << 4),
           left & 0xFF, first & 0xFF, right & 0xFF)

    chars = []
    attr = 0
    i = 0
    while i < len(line):
        ch = line[i]
        if ch == '{':
            end = line.find('}', i)
            if end < 0:
                raise SystemExit('os88doc: unclosed { in %r' % line)
            tag = line[i + 1:end]
            off = tag.startswith('/')
            key = tag[1:] if off else tag
            if key not in ATTR:
                raise SystemExit('os88doc: unknown span {%s}' % tag)
            if off:
                attr &= ~ATTR[key]
            else:
                attr |= ATTR[key]
            i = end + 1
            continue
        if ch == '\t':
            chars.append((9, attr))
        elif 32 <= ord(ch) <= 126:
            chars.append((ord(ch), attr))
        else:
            raise SystemExit('os88doc: character %r is outside 32..126' % ch)
        i += 1
    return pap, chars


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('source')
    ap.add_argument('-o', '--output', required=True)
    args = ap.parse_args()

    with open(args.source, 'r', encoding='ascii') as f:
        raw = f.read().split('\n')
    if raw and raw[-1] == '':           # a trailing newline is not an extra
        raw.pop()                       # empty paragraph
    lines = []
    for line in raw:
        if line.startswith(';;'):
            lines.append(line[1:])
        elif line.startswith(';'):
            continue
        else:
            lines.append(line)
    if not lines:
        raise SystemExit('os88doc: empty source')

    paps = [(0, 0, 0, 0)]               # entry 0 IS Normal (SPEC.md 65.3)
    text = bytearray()
    chp = bytearray()
    tail = 0
    for n, line in enumerate(lines):
        pap, chars = parse_line(line)
        if pap in paps:
            idx = paps.index(pap)
        else:
            if len(paps) >= PAPMAX:
                raise SystemExit('os88doc: more than %d paragraph formats'
                                 % PAPMAX)
            paps.append(pap)
            idx = len(paps) - 1
        for code, attr in chars:
            text.append(code)
            chp.append(attr)
        if n < len(lines) - 1:          # the mark carries its paragraph's
            text.append(13)             ; chp.append(idx)
        else:
            tail = idx                  # the tail has no mark (SPEC.md 65.4)

    if len(text) > MAXTEXT:
        raise SystemExit('os88doc: %d bytes of text; the ceiling is %d'
                         % (len(text), MAXTEXT))

    fib = struct.pack('<HHHHHH4x', MAGIC, VERSION, len(text), len(chp),
                      len(paps), tail)
    pap_bytes = b''.join(bytes(p) for p in paps)
    with open(args.output, 'wb') as f:
        f.write(fib + bytes(text) + bytes(chp) + pap_bytes)
    sys.stderr.write('os88doc: %d chars, %d formats -> %s (%d bytes)\n'
                     % (len(text), len(paps), args.output,
                        16 + 2 * len(text) + 4 * len(paps)))


if __name__ == '__main__':
    main()
