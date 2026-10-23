#!/usr/bin/env python3
"""Where kern_mega's kernel segment goes, against kern_big's - read off the two maps.

    python3 tools/os88megaseg.py                         # build/ and build/megak/
    python3 tools/os88megaseg.py --big B.map --mega M.map
    python3 tools/os88megaseg.py --labels                # kern_mega-only labels too
    python3 tools/os88megaseg.py --json

docs/plans/KERN-MEGA-SEGMENTS-PLAN.md is the question this answers: kern_mega
is kern_big plus a protected-mode layer, and both put `.text` + `.bss` in ONE
64KB segment (KERN_CODE_MAX). Every byte kern_big adds there is a byte off
kern_mega's headroom too, so what decides when kern_mega overflows is the
DIFFERENCE between the two windows - and that difference is what this prints,
per section and per source file.

It also prints the second wall, `.cold`'s slot above 1MB: kern_mega copies
`.cold` to MEGA_COLD_LIN and the XMEM pool starts at MEGA_XPOOL (mega.inc),
so kern_big's `.cold` growing eats that slot as well.

HOW. Nothing is assembled. Each map's `[map all]` symbol table gives every
label's address in its section; a label's size is the distance to the next
label in the same section (so a section's labels sum to its length exactly),
and a label is filed under the kernel source file that defines it. A local
label (`foo.bar`) folds into `foo`; a label no file defines at the start of a
line - one a macro made - is filed as `?macro`. The figures are therefore
exact per SECTION and close per FILE: data a file defines after another file's
label is billed to that label's file.

Both maps must be from the same build number, or the build stamp's three bytes
(SPEC.md 14.2) show up as a difference that is not one.
"""
import argparse
import collections
import glob
import json
import os
import re
import sys

KERN_CODE_MAX = 65536
WATCH = ('.text', '.bss', '.cold', '.mega')
MEGA_FILES = ('mega.inc', 'megados.inc', 'megapark.inc')


def sections(txt):
    out = {}
    for m in re.finditer(r'^\s*([0-9A-F]+)\s+([0-9A-F]+)\s+([0-9A-F]+)\s+([0-9A-F]+)\s+'
                         r'(progbits|nobits)\s+(\S+)\s*$', txt, re.M):
        out[m.group(6)] = (int(m.group(1), 16), int(m.group(4), 16))
    return out


def symbols(txt):
    syms = collections.defaultdict(list)
    for blk in re.finditer(r'^---- Section (\S+) -+\n\n(?:Real\s+Virtual\s+Name|Virtual\s+Name)\n'
                           r'(.*?)(?=\n\n|\Z)', txt, re.M | re.S):
        for line in blk.group(2).splitlines():
            p = line.split()
            if len(p) >= 2:
                syms[blk.group(1)].append((int(p[-2], 16), p[-1]))
    return syms


def definers(root):
    deff = {}
    files = sorted(glob.glob(os.path.join(root, 'kernel', '*.inc'))) + \
        [os.path.join(root, 'kernel', 'kernel.asm')] + \
        sorted(glob.glob(os.path.join(root, 'apps', 'os88*.inc')))
    for f in files:
        with open(f, errors='replace') as fh:
            for line in fh:
                m = re.match(r'^([A-Za-z_]\w*)(?::|\s+(?:db|dw|dd|resb|resw|resd|times)\b)', line)
                if m:
                    deff.setdefault(m.group(1), os.path.basename(f))
    return deff


def attribute(path, deff):
    txt = open(path).read()
    secs, syms = sections(txt), symbols(txt)
    res = {}
    for s in WATCH:
        if s not in secs:
            res[s] = dict(vstart=0, length=0, file=collections.Counter(), label=collections.Counter())
            continue
        vstart, length = secs[s]
        end = vstart + length
        lst = sorted(set(syms.get(s, [])))
        perf, perl = collections.Counter(), collections.Counter()
        for i, (a, n) in enumerate(lst):
            nxt = lst[i + 1][0] if i + 1 < len(lst) else end
            sz = max(0, min(nxt, end) - a)
            top = n.split('.')[0] or n
            f = deff.get(top, '?macro')
            perf[f] += sz
            perl[(f, top)] += sz
        if lst:
            perf['(head)'] += lst[0][0] - vstart
        res[s] = dict(vstart=vstart, length=length, file=perf, label=perl)
    return res


def map_equs(path, names):
    """The values a `[map all]` map lists for NAMES (its `VALUE  NAME` lines)."""
    out = {}
    for line in open(path, errors='replace'):
        m = re.match(r'^([0-9A-Fa-f]{8})\s+(\w+)\s*$', line)
        if m and m.group(2) in names:
            out[m.group(2)] = int(m.group(1), 16)
    return out


def mega_consts(root):
    src = open(os.path.join(root, 'kernel', 'mega.inc'), errors='replace').read()
    out = {}
    for name in ('MEGA_COLD_LIN', 'MEGA_XPOOL'):
        m = re.search(r'^%s\s+equ\s+(0x[0-9A-Fa-f]+|\d+)' % name, src, re.M)
        if m:
            out[name] = int(m.group(1), 0)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--big', default='build/kernel.map')
    ap.add_argument('--mega', default='build/megak/kernel.map')
    ap.add_argument('--root', default='.')
    ap.add_argument('--labels', action='store_true', help="list kern_mega's own labels per section")
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--check', metavar='FILE',
                    help="exit 1 when kern_mega's overhead in the segment is above the "
                         "limit FILE records (tests/megaseg.txt): the ratchet")
    a = ap.parse_args()
    for p in (a.big, a.mega):
        if not os.path.exists(p):
            sys.exit('os88megaseg: no %s - `make` builds kern_big, `make mega` kern_mega' % p)
    deff = definers(a.root)
    big, mega = attribute(a.big, deff), attribute(a.mega, deff)
    k = mega_consts(a.root)

    win_b = big['.text']['length'] + big['.bss']['length']
    win_m = mega['.text']['length'] + mega['.bss']['length']
    out = dict(window=dict(big=win_b, mega=win_m, big_left=KERN_CODE_MAX - win_b,
                           mega_left=KERN_CODE_MAX - win_m, overhead=win_m - win_b))
    if 'MEGA_COLD_LIN' in k and 'MEGA_XPOOL' in k:
        # `.cold` at MEGA_COLD_LIN, `.mega` at its rung's distance above it
        # (MEGA_HI_LIN = MEGA_COLD_LIN + COLD_PARA * 16, mega.inc)
        slot = k['MEGA_XPOOL'] - k['MEGA_COLD_LIN']
        cold = mega['.cold']['length']
        # ...and the park's scratch on `.mega`'s tail (MEGA_SCR_OFF + MPS_SIZE,
        # megados.inc; KERN-MEGA-PLAN 14.45), which kernel.asm's guard counts
        eq = map_equs(a.mega, ('MEGA_SCR_OFF', 'MPS_SIZE'))
        mtop = mega['.mega']['length']
        if len(eq) == 2:
            mtop = eq['MEGA_SCR_OFF'] + eq['MPS_SIZE']
        used = ((cold + 511) // 512) * 512 + mtop
        out['cold_high'] = dict(slot=slot, cold=cold, mega=mega['.mega']['length'],
                                scratch=mtop - mega['.mega']['length'],
                                used=used, left=slot - used)
    out['sections'] = {}
    for s in WATCH:
        files = set(big[s]['file']) | set(mega[s]['file'])
        rows = [(f, mega[s]['file'].get(f, 0), big[s]['file'].get(f, 0)) for f in files]
        rows = [r for r in rows if r[1] != r[2]]
        rows.sort(key=lambda r: -abs(r[1] - r[2]))
        out['sections'][s] = dict(mega=mega[s]['length'], big=big[s]['length'],
                                  files=[dict(file=f, mega=mm, big=bb, delta=mm - bb) for f, mm, bb in rows])
        if a.labels:
            own = [(v, l) for (f, l), v in mega[s]['label'].items() if f in MEGA_FILES]
            own.sort(reverse=True)
            out['sections'][s]['mega_labels'] = [dict(label=l, bytes=v) for v, l in own]
    if a.json:
        json.dump(out, sys.stdout, indent=1)
        print()
        return
    w = out['window']
    print('os88megaseg: window  .text+.bss  kern_big %6d (%5d left)   kern_mega %6d (%5d left)'
          % (w['big'], w['big_left'], w['mega'], w['mega_left']))
    print('os88megaseg: kern_mega carries %d bytes more than kern_big in the segment, so it'
          ' overflows when kern_big\'s .text+.bss grows another %d bytes' % (w['overhead'], w['mega_left']))
    if 'cold_high' in out:
        c = out['cold_high']
        print('os88megaseg: above 1MB  .cold %d + .mega %d (from its rung) + its scratch %d '
              '= %d of the %d between MEGA_COLD_LIN and MEGA_XPOOL (%d left)'
              % (c['cold'], c['mega'], c['scratch'], c['used'], c['slot'], c['left']))
    if a.check:
        limit = None
        for line in open(a.check):
            line = line.split('#')[0].strip()
            if line:
                limit = int(line)
                break
        if limit is None:
            sys.exit('os88megaseg: %s holds no limit' % a.check)
        if w['overhead'] > limit:
            grew = [r for r in out['sections']['.text']['files'] + out['sections']['.bss']['files']
                    if r['delta'] > 0][:6]
            sys.exit('os88megaseg: kern_mega carries %d bytes more than kern_big in the kernel '
                     'segment, against the %d %s allows. Protected-mode-only code belongs in '
                     '`.mega` (docs/plans/KERN-MEGA-SEGMENTS-PLAN.md 3.2); only what runs in '
                     'REAL mode after the switch has to be low. If it is that, raise the figure '
                     'in %s and say why. Biggest kern_mega shares: %s'
                     % (w['overhead'], limit, a.check, a.check,
                        ', '.join('%s %+d' % (r['file'], r['delta']) for r in grew)))
        print('os88megaseg: overhead %d within the %d %s allows' % (w['overhead'], limit, a.check))
        return
    for s in WATCH:
        sec = out['sections'][s]
        print('\n  %s  kern_mega %d, kern_big %d, delta %+d' % (s, sec['mega'], sec['big'],
                                                            sec['mega'] - sec['big']))
        for r in sec['files']:
            print('    %-16s mega %6d  big %6d  %+6d' % (r['file'], r['mega'], r['big'], r['delta']))
        for r in sec.get('mega_labels', []):
            print('      %6d  %s' % (r['bytes'], r['label']))


if __name__ == '__main__':
    main()
