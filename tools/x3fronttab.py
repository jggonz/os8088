#!/usr/bin/env python3
"""x3fronttab: XTMP3's FRONT END tables, made on the host (SPEC.md 121.3).

    python3 tools/x3fronttab.py --out build/x3ftab.inc
    python3 tools/x3fronttab.py --selfcheck

The front end (apps/xtmp3/x3pars.inc in the primary, apps/xtmp3/x3front.asm
the FRONT part) needs the ISO tables tools/x3sim.py's parser reads - the
scalefactor bands, pretab, the slen and partition tables, linbits, the tree a
table number uses, the bitrates and the sample rates.  They are written down
ONCE, in tools/x3tab_iso.py; this file only lays them out for the 8086, so the
target and the reference model read the same numbers.  The Huffman lookup
entries themselves are x3tables.py's (x3std.bin's HUFDIR and HUF), sliced out
of that blob by the FRONT part, not made here.

The output is one NASM include of two macros: X3FTAB_FRONT (the FRONT part's
data) and X3FTAB_PARSE (the primary's), plus equates.  Every table's layout is
written beside it in the output.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import x3tab_iso as ISO   # noqa: E402
import x3tables as TB     # noqa: E402

VERSIONS = (1, 2, 25)                 # the order of a (version, rate) index:
                                      # vi * 3 + sri, vi = VERSIONS.index(v)


def cum(widths):
    c = [0]
    for w in widths:
        c.append(c[-1] + w)
    return c


def tree_index():
    """Table number 0..31 (and 32, 33 for count1 A, B) -> the HUFDIR row of
    its tree (x3tables' PAIR_TREES + COUNT1_TREES order), 0xFF for 0, 4, 14."""
    order = list(TB.PAIR_TREES) + list(TB.COUNT1_TREES)
    out = []
    for t in range(34):
        if t >= 32:
            out.append(order.index(t))
        elif t in (0, 4, 14):
            out.append(0xFF)
        else:
            out.append(order.index(ISO.TREE[t]))
    return out


def tables():
    """[(macro, label, kind, values, comment)]."""
    front = []
    lc, sc, sw = [], [], []
    for v in VERSIONS:
        for s in range(3):
            lc += cum(ISO.SFB_LONG[(v, s)])
            sc += cum(ISO.SFB_SHORT[(v, s)])
            sw += list(ISO.SFB_SHORT[(v, s)])
    front.append(('x3ft_lc', 'dw', lc, '9 x 23: the long bands\' line boundaries, '
                  'by (version, rate) = vi*3 + sri, vi 0 MPEG-1 / 1 MPEG-2 / 2 MPEG-2.5'))
    front.append(('x3ft_sc', 'db', sc, '9 x 14: the short bands\' boundaries, lines a window'))
    front.append(('x3ft_sw', 'db', sw, '9 x 13: the short bands\' widths (the record\'s X3G_SFBS)'))
    front.append(('x3ft_pre', 'db', list(ISO.PRETAB), 'pretab, 22 long bands'))
    front.append(('x3ft_slen1', 'db', [x for p in ISO.SLEN_MPEG1 for x in p],
                  'MPEG-1 scalefac_compress -> slen1, slen2'))
    front.append(('x3ft_nrsfb', 'db', [x for t in ISO.NR_OF_SFB for k in t for x in k],
                  'MPEG-2 nr_of_sfb[table 0..5][kind long/short/mixed][4]'))
    front.append(('x3ft_lin', 'db', list(ISO.LINBITS), 'linbits of table 0..31'))
    front.append(('x3ft_tree', 'db', tree_index(),
                  'table 0..31, count1 A/B as 32/33 -> its tree\'s HUFDIR row; FFh none'))
    parse = []
    br = []
    for mp1 in (True, False):
        for layer in (1, 2, 3):
            br += list(ISO.BITRATE[mp1][layer])
    parse.append(('x3p_tbr', 'dw', br, 'kbps: [MPEG-1 L1 L2 L3, MPEG-2/2.5 L1 L2 L3][index 0..14]'))
    rates = [ISO.SAMPLE_RATE[v][s] for v in VERSIONS for s in range(3)]
    parse.append(('x3p_tsr', 'dw', rates, 'Hz by (version, rate) = vi*3 + sri'))
    return front, parse


def emit(rows, macro):
    out = ['%%macro %s 0' % macro]
    for label, kind, vals, comment in rows:
        out.append('; %s' % comment)
        out.append('%s:' % label)
        for i in range(0, len(vals), 16):
            out.append('    %s %s' % (kind, ', '.join(str(x) for x in vals[i:i + 16])))
    out.append('%endmacro')
    return out


def make_inc():
    front, parse = tables()
    lines = ['; x3ftab.inc - made by tools/x3fronttab.py from tools/x3tab_iso.py; do not edit.',
             '; The XTMP3 front end\'s tables (SPEC.md 121.3): X3FTAB_FRONT in the FRONT',
             '; part, X3FTAB_PARSE in the primary.',
             'X3FT_LC_N equ 23',
             'X3FT_SC_N equ 14',
             'X3FT_SW_N equ 13',
             'X3FT_TREE_NONE equ 0xFF']
    lines += emit(front, 'X3FTAB_FRONT')
    lines += emit(parse, 'X3FTAB_PARSE')
    return '\n'.join(lines) + '\n'


def selfcheck():
    bad = []
    front, parse = tables()
    d = {r[0]: r[2] for r in front + parse}
    for vi, v in enumerate(VERSIONS):
        for s in range(3):
            k = vi * 3 + s
            lc = d['x3ft_lc'][k * 23:(k + 1) * 23]
            sc = d['x3ft_sc'][k * 14:(k + 1) * 14]
            if lc[-1] != 576:
                bad.append('long bands of %d/%d end at %d' % (v, s, lc[-1]))
            if sc[-1] * 3 != 576:
                bad.append('short bands of %d/%d end at %d' % (v, s, sc[-1]))
            if max(sc) > 255:
                bad.append('a short boundary past a byte')
            if d['x3p_tsr'][k] != ISO.SAMPLE_RATE[v][s]:
                bad.append('rate %d/%d' % (v, s))
    tr = d['x3ft_tree']
    order = list(TB.PAIR_TREES) + list(TB.COUNT1_TREES)
    for t in range(32):
        if t in (0, 4, 14):
            if tr[t] != 0xFF:
                bad.append('table %d has a tree' % t)
        elif order[tr[t]] != ISO.TREE[t]:
            bad.append('table %d tree' % t)
    if order[tr[32]] != 32 or order[tr[33]] != 33:
        bad.append('count1 trees')
    if any(x > 255 for r in front for x in r[2] if r[1] == 'db'):
        bad.append('a byte table past a byte')
    if make_inc() != make_inc():
        bad.append('two generations differ')
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', metavar='INC')
    ap.add_argument('--selfcheck', action='store_true')
    a = ap.parse_args()
    rc = 0
    if a.selfcheck:
        bad = selfcheck()
        for m in bad:
            print('x3fronttab: FAIL %s' % m)
        print('x3fronttab: selfcheck %s' % ('ok' if not bad else 'FAILED'))
        rc = 1 if bad else 0
    if a.out:
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        with open(a.out, 'w') as f:
            f.write(make_inc())
    if not (a.selfcheck or a.out):
        ap.print_help()
    return rc


if __name__ == '__main__':
    sys.exit(main())
