#!/usr/bin/env python3
"""x3marty: the STANDARD decoder's cycles on MartyPC's 4.77 MHz 8088, by stage (SPEC.md 121.11).

    python3 tests/x3bench/x3marty.py [--granules N] [--skip K] [--cases NAME:DIV[:mono] ..]

The x3bench harness (tests/x3bench/x3bench.asm and the package's own parts,
build/x3front.bin and build/x3stdp.bin) on a 360 KB floppy, booted on
MartyPC's 5150 (`os8088_5150_cga`, or its GLaBIOS twin), with an exec
breakpoint at every stage boundary - the harness's marks around x3p_next,
x3p_gran and GRANULE, and inside the STANDARD part each stage's entry, read
off a NASM symbol map of a build that is checked byte for byte against the
one `make` made. MartyPC's cycle counter is read at every stop: a breakpoint
costs the guest nothing, so the cycles between two marks are that stage's.

Each case decodes its first N granules (the harness's C_MAXG), drops the
first K from the averages (the first frames of a stream are reservoir
silence), and checks the PCM: the harness folds every byte it writes into a
CRC-32 (mode 2: MartyPC has no debug console) which must equal the CRC-32 of
x3sim's own PCM for those granules - so the 8088 run is held to the
reference too, not only timed.

Not a suite row on its own: the numbers it prints are docs/xtmp3/STD.md's.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tests', 'x3bench'))
import numpy as np        # noqa: E402
import os88marty          # noqa: E402
import x3bench as B       # noqa: E402
import x3sim as X         # noqa: E402

BUILD = B.BUILD
SEG = {'harness': 0x1000, 'front': 0x2000, 'std': 0x3000}
SRC = {'harness': 'tests/x3bench/x3bench.asm', 'front': 'apps/xtmp3/x3front.asm',
       'std': 'apps/xtmp3/x3std.asm'}
BIN = {'harness': 'x3bench.bin', 'front': 'x3front.bin', 'std': 'x3stdp.bin'}

# the marks: (part, symbol) -> the stage that STARTS there
MARKS = [
    ('harness', 'b_runcase.mk_next', 'parse'),          # x3p_next: the frame
    ('harness', 'b_runcase.mk_gran', 'huffman'),        # x3p_gran: FRONT's GRAN
    ('harness', 'b_granule.mk_std', 'std.entry'),       # GRANULE's far call
    ('std', 'xs_requant', 'requant'),
    ('std', 'xs_stereo', 'stereo'),
    ('std', 'xs_hybrid', 'hybrid.cut'),                 # block, the cut
    ('std', 'xs_reorder', 'reorder'),
    ('std', 'xs_alias', 'alias'),
    ('std', 'xs_scale', 'scale'),
    ('std', 'xs_imdct', 'imdct'),
    ('std', 'xs_hybrid.m_fi', 'freqinv'),
    ('std', 'xs_synth', 'mix'),
    ('std', 'xs_dct32', 'dct'),
    ('std', 'xs_dct16', 'dct'),
    ('std', 'xs_dct8', 'dct'),
    ('std', 'xs_win1', 'window'),
    ('std', 'xs_win2', 'window'),
    ('std', 'xs_win4', 'window'),
    ('std', 'xs_put', 'pcm'),
    ('std', 'xs_granule.m_end', 'std.exit'),
    ('harness', 'b_granule.mk_done', 'harness'),        # outside the decoder
    ('harness', 'b_caseend', 'caseend'),
    ('harness', 'entry.halt', 'halt'),
]
FINE = [
    ('front', 'xf_channel', 'f.side'),                  # side info, the block
    ('front', 'xf_scf1', 'f.scf'),
    ('front', 'xf_scf2', 'f.scf'),
    ('front', 'xf_huff', 'f.huff'),
    ('front', 'xf_blocks', 'f.lmap'),
    ('front', 'xf_qexp', 'f.qexp'),
    ('front', 'xf_stereo', 'f.plan'),
    ('std', 'xs_dct9', 'imdct.dct9'),
    ('std', 'xs_mpf', 'imdct.pair'),
    ('std', 'xs_mpair', 'imdct.pair'),
]
REPORT = ('parse', 'huffman', 'f.side', 'f.scf', 'f.huff', 'f.lmap', 'f.qexp', 'f.plan',
          'imdct.dct9', 'imdct.pair', 'requant', 'stereo', 'reorder', 'alias', 'scale', 'imdct',
          'freqinv', 'hybrid.cut', 'mix', 'dct', 'window', 'pcm', 'std.entry', 'std.exit')


def symbols(part):
    """{name: offset} of a part, from a NASM map of a build that must be the
    byte-identical twin of make's."""
    tmp = tempfile.mkdtemp(prefix='x3marty-')
    mp, out = os.path.join(tmp, 'p.map'), os.path.join(tmp, 'p.bin')
    B.sh(B.NASM + ['--before', '[map symbols %s]' % mp, '-o', out, SRC[part]])
    if open(out, 'rb').read() != open(os.path.join(BUILD, BIN[part]), 'rb').read():
        raise SystemExit('x3marty: the map build of %s is not make\'s' % part)
    syms = {}
    for line in open(mp):
        w = line.split()
        if len(w) == 3:
            try:
                syms[w[2]] = int(w[0], 16)
            except ValueError:
                pass
    return syms


def expected_crc(name, div, mono, granules):
    pcm = X.decode_file(os.path.join(B.FIXDIR, name), 'int', div, mono)[0]
    n = granules * (576 // div)
    return zlib.crc32(np.ascontiguousarray(pcm[:n], dtype='<i2').tobytes()) & 0xFFFFFFFF


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--granules', type=int, default=10)
    ap.add_argument('--skip', type=int, default=3)
    ap.add_argument('--cases', nargs='*', default=[
        'AFTERHRS.MP3:1', 'AFTERHRS.MP3:2', 'AFTERHRS.MP3:4',
        'C128.MP3:1', 'C128.MP3:2', 'C128.MP3:4', 'C128.MP3:1:mono', 'C128.MP3:4:mono'])
    ap.add_argument('--machine', default='os8088_5150_cga')
    ap.add_argument('--fine', action='store_true',
                    help='marks inside FRONT and the IMDCT too (more stops, same cycles)')
    a = ap.parse_args()
    B.build(True)
    syms = {p: symbols(p) for p in SEG}
    bps, where = [], {}
    for part, sym, stage in MARKS + (FINE if a.fine else []):
        if sym not in syms[part]:
            raise SystemExit('x3marty: no symbol %s in %s' % (sym, part))
        flat = SEG[part] * 16 + syms[part][sym]
        bps.append({'type': 'exec', 'addr': flat})
        where[flat] = stage
    crc_addr = SEG['harness'] * 16 + syms['harness']['b_crc']
    case_addr = SEG['harness'] * 16 + syms['harness']['b_case']
    cases = []
    for c in a.cases:
        w = c.split(':')
        cases.append((w[0], int(w[1]), 1 if len(w) > 2 else 0, 2, 13, 1500, a.granules))
    img = os.path.join(BUILD, 'x3marty.img')
    B.make_image(cases, True, img, floppy=(9, 2, 720))
    rows = []
    t0 = time.time()
    with os88marty.launch(img, machine=os88marty.machine(a.machine), boot=0,
                          label='x3marty') as m:
        m.breakpoints(bps)
        acc = {}
        last = None
        gran = -1
        ci = 0
        while True:
            m.go()
            if m.wait_stop(limit=600.0) is None:
                raise SystemExit('x3marty: the guest never reached the next mark')
            st = m.status()
            flat = (st['cs'] << 4) + st['ip']
            cyc = int(st['cycles'])
            stage = where.get(flat)
            if stage is None:
                continue
            if last is not None and gran >= a.skip:
                acc[last[0]] = acc.get(last[0], 0) + cyc - last[1]
            if stage == 'std.entry':
                gran += 1
            if stage in ('caseend', 'halt'):
                if stage == 'halt':
                    break
                name, div, mono = cases[ci][:3]
                got = struct.unpack('<I', bytes(m.read(crc_addr, 4)))[0] ^ 0xFFFFFFFF
                want = expected_crc(name, div, bool(mono), a.granules)
                n = gran + 1 - a.skip
                rows.append((name, div, mono, n, {k: v / n for k, v in acc.items()},
                             got == want))
                print('%s d%d %s: %d granules timed, PCM CRC %s' % (
                    name, div, 'mono' if mono else 'native', n,
                    'exact' if got == want else 'DIFFERS (%08X != %08X)' % (got, want)),
                    flush=True)
                acc, last, gran, ci = {}, None, -1, ci + 1
                continue
            last = (stage, cyc)
    print('MartyPC %s: %.0f s host' % (a.machine, time.time() - t0))
    # the table
    head = '%-10s' % 'stage' + ''.join('%14s' % ('%s d%d%s' % (r[0][:-4], r[1], ' m' if r[2] else ''))
                                      for r in rows)
    print(head)
    for k in REPORT:
        print('%-10s' % k + ''.join('%14.0f' % r[4].get(k, 0) for r in rows))
    tot = []
    for r in rows:
        tot.append(sum(r[4].get(k, 0) for k in REPORT))
    print('%-10s' % 'TOTAL' + ''.join('%14.0f' % t for t in tot))
    print('(huffman = the FRONT part\'s GRAN; with --fine its f.* rows are split out of it,')
    print(' and imdct\'s dct9 and pair rows out of imdct)')
    print('%-10s' % 'x real' + ''.join(
        '%14.3f' % (4772727.0 / (t * X.Stream(open(os.path.join(B.FIXDIR, r[0]), 'rb').read()).h0.rate / 576))
        for r, t in zip(rows, tot)))
    return 0 if all(r[5] for r in rows) else 1


if __name__ == '__main__':
    sys.exit(main())
