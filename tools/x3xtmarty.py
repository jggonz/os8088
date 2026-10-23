#!/usr/bin/env python3
"""x3xtmarty.py - the XT part (apps/xtmp3/x3xt.asm) measured on MartyPC's
cycle-accurate IBM 5150 at 4.77 MHz (SPEC.md 121.11; docs/xtmp3/XT.md).

    python3 tools/x3xtmarty.py [--fixture F ...] [--profile P ...]
                               [--stages N] [--granules N] [--json OUT]

A DESIGN measurement, taken once and written into docs/xtmp3/XT.md: not a
suite row. tools/x3cycmarty.py's shape: GLaBIOS boots a floppy whose boot
sector spins (so the BIOS has programmed the PIT and the refresh DMA, which
the 8088's real speed includes), the debugger pauses it and writes the part,
XTGEN.BIN and a granule record into RAM, and a stub that loads the verb's
registers, far-calls it and stops on `done` is PARKED on (IF = 0, the queue
flushed). The cycle count from the park to `done`, less the same stub
calling CLOSE, is the verb.

  OPEN       once a profile: the expansion and the Fletcher check, by region
  GRANULE    every granule of the fixture end to end (and its PCM compared
             with tools/x3simxt.py's - the 8088 core agreeing with the model
             is a second exactness witness), and --stages N of them with a
             breakpoint at every stage boundary for the split

Audio seconds a wall second = (576 / rate per granule) / (cycles / 4,772,727).
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tests'))
import x3sim as S          # noqa: E402
import x3simxt as SX       # noqa: E402
import x3xtgen as G        # noqa: E402
import x3xttab as XT       # noqa: E402

HZ = 4772727.0
PART_SEG = 0x1000
CLAIM_SEG = 0x2200
REC_SEG = 0x8000
OUT_SEG = 0x8200
ARG_SEG = 0x8300
STUB = 0x0500
PNUM = {'std': 0, 'fast': 1, 'turbo': 2, 'speech': 3}


def build(d, fast_min=None):
    inc = os.path.join(d, 'x3xtk.inc')
    G.emit(inc)
    G.xtgen(os.path.join(d, 'XTGEN.BIN'))
    if fast_min is not None:            # a measurement of the IMDCT switch
        t = open(inc).read().replace('X3X_FAST_MIN equ %d' % G.FAST_MIN,
                                     'X3X_FAST_MIN equ %d' % fast_min)
        open(inc, 'w').write(t)
    wrap = os.path.join(d, 'x3xtmap.asm')
    mp = os.path.join(d, 'x3xt.map')
    with open(wrap, 'w') as f:
        f.write('[map symbols %s]\n%%include "x3xt.asm"\n' % mp)
    out = os.path.join(d, 'x3xt.bin')
    subprocess.run(['nasm', '-f', 'bin', '-w+error', '-I', os.path.join(ROOT, 'apps', 'xtmp3') + '/',
                    '-I', d + '/', '-o', out, wrap], check=True)
    G.seal(out)
    global IMAGE
    IMAGE = open(out, 'rb').read()
    sym = {}
    for line in open(mp):
        w = line.split()
        if len(w) == 3 and w[2].startswith('x3x_'):
            try:
                sym[w[2]] = int(w[0], 16)
            except ValueError:
                pass
    return open(out, 'rb').read(), sym


IMAGE = b''


def stub(verb, ax=0, bx=0, cx=0, dx=0, si=0, di=0, ds=PART_SEG, es=0):
    """Load the registers, far-call the verb through the part header's
    vector (x3gran.inc: DS = the part's segment), stop on `done`."""
    b = bytearray()
    b += b'\xFA'                                    # cli
    b += b'\xB8' + struct.pack('<H', 0x7000) + b'\x8E\xD0'   # mov ax,7000h / mov ss,ax
    b += b'\xBC' + struct.pack('<H', 0xFFFE)        # mov sp,FFFEh
    b += b'\xB8' + struct.pack('<H', es) + b'\x8E\xC0'
    b += b'\xB8' + struct.pack('<H', ds) + b'\x8E\xD8'
    b += b'\xBE' + struct.pack('<H', si)
    b += b'\xBF' + struct.pack('<H', di)
    b += b'\xBA' + struct.pack('<H', dx)
    b += b'\xBB' + struct.pack('<H', bx)
    b += b'\xB9' + struct.pack('<H', cx)
    b += b'\xB8' + struct.pack('<H', ax)
    b += b'\x9A' + struct.pack('<HH', struct.unpack_from('<H', IMAGE, 6 + 2 * verb)[0], PART_SEG)
    done = len(b)
    b += b'\x90\xEB\xFD'                            # done: nop / jmp done
    return bytes(b), STUB + done


def boot_image(path):
    sec = bytearray(512)
    sec[0:2] = b'\xeb\xfe'
    sec[510:512] = b'\x55\xaa'
    open(path, 'wb').write(bytes(sec) + bytes(368640 - 512))


class Bench:
    def __init__(self, m, image, sym):
        self.m, self.image, self.sym = m, image, sym
        m.pause()
        m.write(PART_SEG * 16, image)
        self.base = 0
        self.base = self.call(5)[0]                  # CLOSE: the stub alone

    def open(self, prof, div, ver, sri, trace=None):
        """CAPS, the segments laid end to end from CLAIM_SEG, then OPEN."""
        m = self.m
        m.pause()
        self.call(0, ax=PNUM[prof] | (div << 8), cx=1, es=ARG_SEG, di=0)
        c = bytes(m.read(ARG_SEG * 16, 18))
        nseg = c[1]
        paras = struct.unpack_from('<6H', c, 2)[:nseg]
        segs, at = [], CLAIM_SEG
        for p_ in paras:
            segs.append(at)
            at += p_
        rec = struct.pack('<BBBBBBBBHBB', PNUM[prof], div, 1, 1, 1, ver, sri, 0, 0, nseg, 0)
        rec += struct.pack('<6H', *(list(segs) + [0] * (6 - nseg)))
        m.write(ARG_SEG * 16, rec)
        return self.call(1, trace=trace, es=ARG_SEG, di=0)

    def call(self, verb, trace=None, **regs):
        m = self.m
        m.pause()
        code, done = stub(verb, **regs)
        m.write(STUB, code)
        if trace is None:
            m.bp_exec(done)
            m.cmd(cmd='park', cs=0, ip=STUB)
            c0 = m.status()['cycles']
            m.run()
            if m.wait_stop(600.0) is None or ((m.status()['cs'] << 4) + m.status()['ip']) != done:
                raise RuntimeError('the verb never reached done')
            c1 = m.status()['cycles']
            r = m.regs()
            m.bp_exec()
            return c1 - c0 - self.base, r, None
        import os88marty
        names = dict(trace)
        m.cmd(cmd='park', cs=0, ip=STUB)
        c0 = m.status()['cycles']
        targets = [PART_SEG * 16 + a for a in names] + [done]
        with os88marty.bp_trace(m, *targets) as tr:
            tr.wait(1, '%05X' % done, limit=3600.0)
        hits = [(names.get(h['addr'] - PART_SEG * 16, 'done'), h['cycles'] - c0) for h in tr.hits]
        m.pause()
        r = m.regs()
        return hits[-1][1] - self.base, r, hits


GMARK = ('x3x_granule', 'x3x_hybrid', 'x3x_hybrid.noaa', 'x3x_produce', 'x3x_produce.win',
         'x3x_produce.wd', 'x3x_produce.allz', 'x3x_produce.sd', 'x3x_hybrid.nx',
         'x3x_synth', 'x3x_synth.ld', 'x3x_synth.wd')
STAGE = {('x3x_granule', 'x3x_hybrid'): 'requantise + stereo',
         ('x3x_hybrid.nx', 'x3x_hybrid'): 'requantise + stereo',
         ('x3x_hybrid', 'x3x_hybrid.noaa'): 'antialias',
         ('x3x_produce', 'x3x_produce.win'): 'IMDCT',
         ('x3x_produce', 'x3x_produce.allz'): 'IMDCT',
         ('x3x_produce', 'x3x_produce.sd'): 'IMDCT (short, with its window)',
         ('x3x_produce.win', 'x3x_produce.wd'): 'IMDCT window',
         ('x3x_produce.wd', 'x3x_hybrid.nx'): 'overlap',
         ('x3x_produce.allz', 'x3x_hybrid.nx'): 'overlap',
         ('x3x_produce.sd', 'x3x_hybrid.nx'): 'overlap',
         ('x3x_synth', 'x3x_synth.ld'): 'matrixing (Lee + V codes)',
         ('x3x_synth.ld', 'x3x_synth.wd'): 'window',
         ('x3x_synth.wd', 'done'): 'output copy'}


def imdct_costs(hits, sblog):
    """(nonzero lines, cycles from x3x_produce to its .win) for every long
    nonzero subband of a traced granule, aligned with the model's log (one
    entry a produce call, -1 for a short subband)."""
    out = []
    k = 0
    for i, (name, cyc) in enumerate(hits):
        if name != 'x3x_produce':
            continue
        if k >= len(sblog):
            break
        n = sblog[k]
        k += 1
        nxt = hits[i + 1] if i + 1 < len(hits) else None
        if n > 0 and nxt and nxt[0] == 'x3x_produce.win':
            w = hits[i + 2] if i + 2 < len(hits) else None
            out.append((n, nxt[1] - cyc, (w[1] - nxt[1]) if w else None))
    return out


def stages(hits):
    out = {}
    prev = ('x3x_granule', 0)
    for name, cyc in hits:
        k = STAGE.get((prev[0], name), 'loop and dispatch')
        out[k] = out.get(k, 0) + (cyc - prev[1])
        prev = (name, cyc)
    return out


OMARK = ('x3x_open', 'x3x_xwin', 'x3x_xsy', 'x3x_xh1', 'x3x_xh2', 'x3x_fl32', 'x3x_fl32.r')


def open_stages(hits):
    """The expansion by region, and the Fletcher check (every x3x_fl32 span
    plus XTGEN's own, which is inline before x3x_xwin)."""
    top = {}
    fl = 0
    cur, at = 'XTGEN check and setup', 0
    lab = {'x3x_xwin': 'window rows', 'x3x_xsy': 'ENCV and Lee planes',
           'x3x_xh1': 'RQL, IMDCT rows, ENCR, antialias', 'x3x_xh2': 'IMWIN, reset'}
    fstart = None
    for name, cyc in hits:
        if name == 'x3x_fl32':
            fstart = cyc
            continue
        if name == 'x3x_fl32.r':
            fl += cyc - fstart
            continue
        if name in lab or name == 'done':
            top[cur] = top.get(cur, 0) + (cyc - at)
            cur, at = lab.get(name, 'done'), cyc
    for k in list(top):
        pass
    return top, fl


def run(args):
    import os88marty
    from x3xtunit import record_bytes
    res = {'part_bytes': None, 'open': {}, 'granule': {}}
    with tempfile.TemporaryDirectory() as d:
        image, sym = build(d, args.fastmin)
        res['part_bytes'] = len(image)
        img = os.path.join(d, 'spin.img')
        boot_image(img)
        with os88marty.launch(img, machine='os8088_5150_cga_gla', boot=6.0,
                              label='x3xtmarty') as m:
            b = Bench(m, image, sym)
            print('stub baseline %d cycles; part %d bytes' % (b.base, len(image)))
            for prof in args.profile:
                div = 2 if prof == 'speech' else 1
                tr = [(sym[n], n) for n in OMARK]
                h0 = S.Stream(open(os.path.join(S.FIXDIR, args.fixture[0]), 'rb').read()).h0
                ver = 25 if prof == 'speech' and div == 2 else h0.version
                cyc, r, hits = b.open(prof, div, ver if prof != 'speech' else 25, h0.sr_idx, trace=tr)
                top, fl = open_stages(hits)
                ok = not (r['flags'] & 1) if 'flags' in r else None
                res['open'][prof] = {'cycles': cyc, 'seconds': cyc / HZ, 'regions': top,
                                     'fletcher': fl, 'ok': ok}
                print('OPEN %-6s %9d cycles = %.3f s (Fletcher %d = %.3f s) %s' % (
                    prof, cyc, cyc / HZ, fl, fl / HZ, {k: round(v / HZ, 3) for k, v in top.items()}))
                for name in args.fixture:
                    data = open(os.path.join(S.FIXDIR, name), 'rb').read()
                    st = S.Stream(data)
                    pdiv = SX.profile_div(prof, st.h0.rate)
                    if pdiv is None:
                        continue
                    b.open(prof, pdiv, st.h0.version, st.h0.sr_idx)
                    dec = SX.XTDecoder(prof, pdiv)
                    if args.fastmin is not None:
                        dec.fast_min = args.fastmin
                    imd = []
                    rows = []
                    staged = []
                    recs = list(SX.records(data))
                    nmax = args.granules or len(recs)
                    # the stage split on granules spread over the track
                    pick = set(range(len(recs) // 4, len(recs), max(1, len(recs) // (2 * max(1, args.stages)))))
                    pick = set(sorted(pick)[:args.stages])
                    mism = 0
                    for gi, (h, chans, plan) in enumerate(recs[:nmax]):
                        dec.sblog = [] if gi in pick else None
                        want = dec.granule(h, chans, plan)
                        m.write(REC_SEG * 16, record_bytes(h, chans, plan))
                        if gi in pick:
                            tr = [(sym[n], n) for n in GMARK]
                            cyc, r, hits = b.call(3, trace=tr, es=REC_SEG, si=0, dx=OUT_SEG, di=0)
                            staged.append(stages(hits))
                            imd += imdct_costs(hits, dec.sblog)
                        else:
                            cyc, r, _ = b.call(3, es=REC_SEG, si=0, dx=OUT_SEG, di=0)
                        got = bytes(m.read(OUT_SEG * 16, len(want)))
                        if got != want.tobytes():
                            mism += 1
                        rows.append(cyc)
                    rate = st.h0.rate // pdiv
                    sec = 576.0 / st.h0.rate
                    mean = sum(rows) / len(rows)
                    agg = {}
                    for s_ in staged:
                        for k, v in s_.items():
                            agg[k] = agg.get(k, 0) + v / len(staged)
                    key = '%s %s' % (name, prof)
                    res['granule'][key] = {
                        'granules': len(rows), 'divisor': pdiv, 'rate_out': rate,
                        'cycles_mean': mean, 'cycles_min': min(rows), 'cycles_max': max(rows),
                        'speed': sec / (mean / HZ), 'pcm_mismatch': mism,
                        'stages': agg, 'staged_granules': len(staged), 'imdct': imd}
                    print('%-13s %-6s d%d: %d granules, %.0f cycles a granule (min %d max %d), '
                          '%.3fx real time, PCM %s' % (
                              name, prof, pdiv, len(rows), mean, min(rows), max(rows),
                              sec / (mean / HZ), 'bit-exact' if not mism else '%d MISMATCHES' % mism))
                    for k, v in sorted(agg.items(), key=lambda kv: -kv[1]):
                        print('    %-32s %9.0f  %5.1f%%' % (k, v, 100 * v / sum(agg.values())))
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--fixture', nargs='*', default=['AFTERHRS.MP3', 'X8K16.MP3'])
    ap.add_argument('--profile', nargs='*', default=list(G.PNAMES))
    ap.add_argument('--stages', type=int, default=8)
    ap.add_argument('--granules', type=int, default=0)
    ap.add_argument('--json', metavar='OUT')
    ap.add_argument('--fastmin', type=int, default=None,
                    help='build with this X3X_FAST_MIN (the IMDCT switch measurement)')
    a = ap.parse_args()
    res = run(a)
    if a.json:
        with open(a.json, 'w') as f:
            json.dump(res, f, indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
