#!/usr/bin/env python3
"""lmpool.py - share LOOM's phase tables.

--share-from FILE: FILE's `lmpool share:` lines name tables in PHASES
(`a b | c d | e`) - storage no two of which is live at once - and the named
`.bss` reservations are laid from one base, the phases overlapping and the
members of one phase end to end. apps/loom/loom.h's PHASE STORE block says
which and why, and why it is done here rather than as a C union (SmallerC
loses its indexed addressing through any union member: +1,202 bytes of code
for the same bss). A name that is not a plain `resb` array in .bss is refused
rather than skipped. It runs on tools/cc8086.py's OUTPUT, build/loom.gen.asm,
and touches .bss only, so the code nasm assembles is the code cc8086.py
checked.

ITS FIRST JOB, THE LITERAL POOL, IS NOW EVERY C PACKAGE'S: tools/ccpeep.py's
pool_literals (SPEC.md 73.15) folds a literal that repeats an earlier one, or
is the tail of a longer one, onto one copy - what this file did for LOOM
alone (-2,795 bytes of resident .rodata) because the pass that wrote it could
not edit a shared tool.

usage: lmpool.py --share-from FILE IN.gen.asm -o OUT.gen.asm  (IN may be OUT)
"""
import re
import sys

SECTION = re.compile(r'^\s*section\s+(\S+)')


RESB = re.compile(r'^\s*resb\s+(\d+)\s*$')
ALIGNB = re.compile(r'^\s*alignb\s+2\s*$')


def shares(path):
    """The phase groups a C file declares, one `lmpool share:` line each:
    PHASES separated by `|`, each a list of C names laid end to end."""
    groups = []
    for ln in open(path, encoding='latin-1'):
        m = re.search(r'lmpool share:(.*?)(\*/|$)', ln)
        if m:
            groups.append([p.split() for p in m.group(1).split('|')])
    return groups


def share(lines, groups):
    """Lay every phase of a group from ONE base, and reserve the largest.

    Each name must be a plain `.bss` reservation exactly as SmallerC writes
    one - an optional `alignb 2`, `_<name>:`, `resb <n>` - and found exactly
    once, or this refuses: a name that moved, or that stopped being an array,
    must not be shared by accident. Every member starts on a word, so a word
    array keeps the alignment it was declared with."""
    sizes = {}
    where = {}
    sec = None
    for k, ln in enumerate(lines):
        m = SECTION.match(ln)
        if m:
            sec = m.group(1)
            continue
        m = re.match(r'^_(\w+):\s*$', ln)
        if m and sec == '.bss' and k + 1 < len(lines):
            r = RESB.match(lines[k + 1])
            if r:
                if m.group(1) in where:
                    raise SystemExit('lmpool: _%s reserved twice' % m.group(1))
                a = k - 1 if k and ALIGNB.match(lines[k - 1]) else k
                where[m.group(1)] = (a, k + 2)
                sizes[m.group(1)] = int(r.group(1))
    drop = set()
    at = {}
    saved = 0
    for gi, phases in enumerate(groups):
        base = 'lm_phase%d' % gi
        widest = 0
        body = []
        held = 0
        for ph in phases:
            off = 0
            for name in ph:
                if name not in where:
                    raise SystemExit('lmpool: share: _%s is not a plain .bss '
                                     'array in this assembly' % name)
                off = (off + 1) & ~1
                body.append('_%s equ %s + %d' % (name, base, off))
                off += sizes[name]
                held += sizes[name]
                a, b = where[name]
                drop.update(range(a, b))
            widest = max(widest, off)
        first = min(where[n][0] for ph in phases for n in ph)
        at[first] = ['\talignb 2', base + ':', '\tresb %d' % widest] + body
        saved += held - widest
    out = []
    for k, ln in enumerate(lines):
        if k in at:
            out.extend(at[k])
        elif k not in drop:
            out.append(ln)
    return out, saved


def main(argv):
    groups = []
    while '--share-from' in argv:
        i = argv.index('--share-from')
        groups += shares(argv[i + 1])
        del argv[i:i + 2]
    if not groups or len(argv) != 3 or argv[1] != '-o':
        sys.stderr.write(__doc__)
        return 2
    lines = open(argv[0], encoding='latin-1').read().split('\n')
    out, shared = share(lines, groups)
    print('lmpool: %s: %d phase group(s), %d byte(s) of .bss shared'
          % (argv[0], len(groups), shared))
    open(argv[2], 'w', encoding='latin-1').write('\n'.join(out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
