#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/arcmove.py - ARCADE's claims MOVE, and a game played across
# the move carries on (SPEC.md 66, ARCADE-SPEC 14, ARCADE-MEGA-PLAN M1)
#
#     python3 tests/arcmove.py [--game 'Q*bert'] [--game Pac-Man] ...
#
# On MartyPC's VGA XT (kern_big, where a move CHANGES a segment - on kern_mega
# it is a rebase and no proc is called). For each board: pick it, coin it,
# start it and play, Esc back to the window, then post a compaction from
# OUTSIDE at the trivial purge rank - the door OSAPI_MEM_COMPACT posts
# through, tests/pkgmove.py's - and read what it did:
#
#   1. every word ARCADE keeps a claim in names a LIVE claim afterwards, and
#      MC_RLOC says each was declared (a refused declaration looks the same
#      from inside);
#   2. the copies agree with their claims: ar_rseg is the board's ROM part,
#      ar_aseg its sprite or aux part, the view is still the view ('ARCV'),
#      the code part still a code part; Q*bert's G is in qb_gcs, in the
#      view's NV_GSEG/NV_GCS, in the far return its stack keeps between runs,
#      and every yield in G names the N88 part where it is NOW;
#   3. a moved claim's bytes are the bytes it had (G aside: its procs rewrite
#      the segments it holds);
#   4. at least one claim MOVED over the run - else it proved nothing;
#   5. Enter plays on, and the board's frames come at its rate: a G whose
#      stack kept the old CS jumps into whatever is there now.
#
# Red run: qb_gmv a bare `ret` - the stack's CS and the view's words are G's
# old place, and checks 2 and 5 fail on the first Q*bert move.
# =============================================================================
import argparse
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, HERE)
os.chdir(ROOT)
import os88build                    # noqa: E402
import os88geom                     # noqa: E402
import os88marty                    # noqa: E402
import os88sym                      # noqa: E402
import os88ui                       # noqa: E402
from arcspeed import find_view      # noqa: E402

S = os88sym.linear
MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX
NV_GSEG, NV_GSP, NV_GCS = 0x6080, 0x6082, 0x6086
ARC_MAGIC = 0x3341
WORDS = ['ar_vseg', 'ar_core', 'ar_rseg', 'ar_rimgc', 'ar_rimg', 'ar_aseg', 'af_pclaim', 'qb_gseg',
         'qb_gcs', 'qb_rclaim', 'qb_sclaim', 'qb_shseg', 'qb_n88', 'pm_rclaim',
         'pm_shseg']
# the claims each board holds, by the word naming it (ar_core is +2)
HELD = {'Q*bert': ['qb_gseg', 'qb_rclaim', 'qb_sclaim', 'qb_shseg', 'ar_core'],
        'Pac-Man': ['pm_rclaim', 'pm_shseg', 'ar_core', 'ar_aseg'],
        'Space Invaders': ['ar_core', 'ar_aseg', 'ar_rimgc']}


def var_offsets(names):
    """A bss cell's offset in ARCADE, out of the listing: the address an
    instruction naming exactly [name] carries (little-endian in the bytes)."""
    import re
    lst = open(os88build.at('build/arcade.lst')).read()
    out = {}
    for n in names:
        mm = re.search(r'\[([0-9A-F]{2})([0-9A-F]{2})\][^\n]*\[' + n + r'\]', lst)
        if not mm:
            raise SystemExit('arcmove: %s is not in the listing' % n)
        out[n] = int(mm.group(2) + mm.group(1), 16)
    return out


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def yields():
    """Q*bert's yield addresses, from the patch table ARCADE was built with."""
    p = os88build.at('build/arcade/pre/qbpatch.inc')
    vals = []
    for ln in open(p):
        ln = ln.split(';')[0].strip()
        if ln.startswith('db ') or ln.startswith('dw '):
            vals.append(int(ln[3:].strip(), 0))
    out, i = [], 0
    while i < len(vals) and vals[i] != 0xFF:
        kind, addr = vals[i], vals[i + 1]
        if kind == 2:
            out.append(addr)
        i += 3
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', action='append')
    ap.add_argument('--nomove', action='store_true', help='the A/B: no compaction')
    a = ap.parse_args()
    games = a.game or ['Q*bert', 'Pac-Man', 'Space Invaders']
    V = var_offsets(WORDS)
    img = open(os88build.at('build/arcade.bin'), 'rb').read()
    ylist = yields()
    bad = []
    moved_any = []
    with os88ui.boot('build/os8088-360.img', apps=os88build.at('build/arcade720.img'),
                     machine='os8088_xt_vga_720b', quick=True) as ui:
        m = ui.m
        w = ui.path('B:/ARCADE.O88')
        m.run()
        os88marty.guest_sleep(m, 1)

        def base():
            mem = bytes(m.read(0, 0xA0000))  # (the region is MOVABLE too)
            return mem.find(img[0xE00:0xE80]) - 0xE00

        def words():
            b = base()
            out = {n: u16(bytes(m.read(b + V[n], 2))) for n in WORDS}
            out['ar_core'] = u16(bytes(m.read(b + V['ar_core'] + 2, 2)))
            return out

        def claims():
            raw = bytes(m.read(S('mem_tab'), MEM_MAX * MC_SIZE))
            return {u16(raw, i * MC_SIZE): (u16(raw, i * MC_SIZE + 2), u16(raw, i * MC_SIZE + 8))
                    for i in range(MEM_MAX) if u16(raw, i * MC_SIZE)}

        def frames(v):
            return u16(bytes(m.read(v + 0x6044, 2)))

        def post():
            wptr = os88geom.winptr(m, w, S)
            m.write(S('mem_cpq_lvl'), bytes([0xFC]))
            m.write(S('mem_cpq'), bytes([wptr & 0xFF, wptr >> 8]))
            m.run()
            os88marty.until(m, lambda _: u16(m.read(S('mem_cpq'), 2)) == 0,
                            'the posted compaction', poll=0.2, limit=30)
            os88marty.ui_done(m)

        for g in games:
            v = find_view(m)
            m.write(v + 0x6044, b'\0\0')
            m.run()
            ui.menu_pick('Game', g)
            for _ in range(120):            # the pick reads and builds: wait
                os88marty.guest_sleep(m, 0.5)   # for the board to run
                v = find_view(m)
                if frames(v) > 30:
                    break
            else:
                raise SystemExit('arcmove: FAIL - %s never ran' % g)
            os88marty.guest_sleep(m, 4 if g == 'Q*bert' else 1)
            m.key('Digit5')
            os88marty.guest_sleep(m, 1)
            m.key('Digit1')
            os88marty.guest_sleep(m, 1)
            fa = frames(find_view(m))       # the board's own rate, before:
            os88marty.guest_sleep(m, 5)     #   an XT runs Q*bert at its NMI
            rate0 = (frames(find_view(m)) - fa) / 5.0   # and Pac-Man at ~2/s
            m.key('Escape')
            os88marty.guest_sleep(m, 20)    # (a first leave writes the picture)
            m.pause()
            w0, c0 = words(), claims()
            held = [n for n in HELD[g] + ['af_pclaim', 'ar_vseg'] if w0[n]]
            snap = {n: hashlib.sha1(bytes(m.read(w0[n] * 16, c0[w0[n]][0] * 16))).hexdigest()
                    for n in held if w0[n] in c0}
            for n in held:
                if w0[n] not in c0:
                    bad.append('%s: %s (%04X) names no claim before the move' % (g, n, w0[n]))
                elif not c0[w0[n]][1]:
                    bad.append('%s: %s (%04X) is not declared movable' % (g, n, w0[n]))
            if not a.nomove:
                post()
            m.pause()
            w1, c1 = words(), claims()
            moved = [n for n in held if w1[n] != w0[n]]
            moved_any += [(g, n) for n in moved]
            print('arcmove: %s: %s' % (g, ', '.join('%s %04X->%04X' % (n, w0[n], w1[n])
                                                     for n in held)))
            for n in held:
                if w1[n] not in c1:
                    bad.append('%s: %s (%04X) names no claim after the move' % (g, n, w1[n]))
            v = find_view(m)
            if v != w1['ar_vseg'] * 16:
                bad.append('%s: the view is at %05X, ar_vseg says %04X' % (g, v, w1['ar_vseg']))
            if u16(bytes(m.read(w1['ar_core'] * 16, 2))) != ARC_MAGIC:
                bad.append('%s: ar_core+2 (%04X) is not a code part' % (g, w1['ar_core']))
            if g == 'Q*bert':
                G = w1['qb_gseg']
                vw = bytes(m.read(v + NV_GSEG, 8))
                sp = u16(vw, NV_GSP - NV_GSEG)
                cs = u16(bytes(m.read(G * 16 + sp + 18, 2)))
                for what, got in (('ar_rseg', w1['ar_rseg'] == w1['qb_rclaim']),
                                  ('ar_aseg', w1['ar_aseg'] == w1['qb_sclaim']),
                                  ('qb_gcs', w1['qb_gcs'] == G),
                                  ('NV_GSEG', u16(vw, 0) == G),
                                  ('NV_GCS', u16(vw, NV_GCS - NV_GSEG) == G),
                                  ('the stack\'s CS', cs == G),
                                  ('qb_n88', w1['qb_n88'] == w1['ar_core'])):
                    if not got:
                        bad.append('Q*bert: %s does not follow its claim' % what)
                wrong = [y for y in ylist
                         if u16(bytes(m.read(G * 16 + y + 3, 2))) != w1['ar_core']]
                print('arcmove: Q*bert: G %04X, its stack\'s CS %04X, %d yields, %d naming '
                      'another N88' % (G, cs, len(ylist), len(wrong)))
                if wrong:
                    bad.append('Q*bert: %d yields name the N88 part\'s old place' % len(wrong))
            elif g == 'Pac-Man' and w1['ar_rseg'] != w1['pm_rclaim']:
                bad.append('Pac-Man: ar_rseg does not follow its ROM part')
            elif g == 'Space Invaders' and not (w1['ar_rimg'] == w1['ar_rseg'] == w1['ar_rimgc'] + 2):
                bad.append('Space Invaders: ar_rimg/ar_rseg do not follow the .ROM\'s claim')
            for n in moved:
                if n in ('qb_gseg',):
                    continue
                now = hashlib.sha1(bytes(m.read(w1[n] * 16, c1[w1[n]][0] * 16))).hexdigest()
                if now != snap.get(n):
                    bad.append('%s: %s\'s bytes changed in the move' % (g, n))
            m.run()                         # play on: Enter carries on the game
            m.key('Enter')                  # (the bracket's first picture
            os88marty.guest_sleep(m, 3)     #   first: seconds on an XT)
            f0 = frames(find_view(m))
            os88marty.guest_sleep(m, 5)
            rate1 = (frames(find_view(m)) - f0) / 5.0
            print('arcmove: %s: %.1f frames/s before the move, %.1f after' % (g, rate0, rate1))
            if rate1 < max(0.5, rate0 * 0.3):
                bad.append('%s did not play on after the move (%.1f frames/s, %.1f before)'
                           % (g, rate1, rate0))
            m.key('Escape')
            os88marty.guest_sleep(m, 20)
    if not moved_any:
        bad.append('no claim of ARCADE\'s moved: the run proved nothing')
    for b in bad:
        print('arcmove: FAIL - ' + b)
    if bad:
        return 1
    print('arcmove: ok (%d moves)' % len(moved_any))
    return 0


if __name__ == '__main__':
    sys.exit(main())
