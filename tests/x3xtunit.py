#!/usr/bin/env python3
"""The XT backend part (apps/xtmp3/x3xt.asm) run on the host against its
model (tools/x3simxt.py), in Unicorn's x86 core (SPEC.md 121.4).

    python3 tests/x3xtunit.py [--profile P ...] [--fixture F ...] [--granules N]
                              [--expand] [--synthetic N] [-v]

  --expand     OPEN each profile and compare every expanded table, byte for
               byte, with tools/x3xtgen.py's expanded() (and the refusals:
               a damaged XTGEN.BIN, a bad profile or divisor)
  default      decode fixtures granule by granule through GRANULE and compare
               each granule's PCM with the model's
  --synthetic  N random records with disagreeing block types and random
               plans, to reach the split path no LAME fixture takes

A design-time check of the part's arithmetic: the instruction set Unicorn
runs is a superset of the 8086's, so `cpu 8086` in the source is what keeps
the part an 8086 program, and the exactness row (x3xtexact) runs the real
thing on the emulated machine.
"""
import argparse
import os
import random
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX,
                                   UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI,
                                   UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_CS,
                                   UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
                                   UC_X86_REG_IP, UC_X86_REG_FLAGS)
except ImportError:                     # pragma: no cover
    print('x3xtunit: cannot run - unicorn is not installed (pip install unicorn)')
    sys.exit(2)

import numpy as np            # noqa: E402
import x3sim as S             # noqa: E402
import x3simxt as SX          # noqa: E402
import x3xtgen as G           # noqa: E402
import x3xttab as XT          # noqa: E402

PART_SEG = 0x1000
GEN_SEG = 0x2000
CLAIM_SEG = 0x2200
REC_SEG = 0x6000
OUT_SEG = 0x6200
STUB = 0x0500
SS_SEG = 0x7000
PNUM = {'std': 0, 'fast': 1, 'turbo': 2, 'speech': 3}

R_CH0 = 6
R_CHSZ = 6 + 2304
R_PLAN = R_CH0 + 2 * R_CHSZ
R_BYTES = R_PLAN + 576


def build(out):
    inc = os.path.join(os.path.dirname(out), 'x3xtk.inc')
    G.emit(inc)
    subprocess.run(['nasm', '-f', 'bin', '-w+error', '-I', os.path.join(ROOT, 'apps', 'xtmp3') + '/',
                    '-I', os.path.dirname(out) + '/', '-o', out,
                    os.path.join(ROOT, 'apps', 'xtmp3', 'x3xt.asm')], check=True)
    return open(out, 'rb').read()


class Part:
    def __init__(self, image):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, 1 << 20)
        self.uc.mem_write(PART_SEG * 16, image)
        self.uc.mem_write(GEN_SEG * 16, XT.gen_input())
        self.image = image
        self.icount = 0

    def call(self, verb, **regs):
        uc = self.uc
        # call far PART_SEG:3*verb ; hlt
        stub = bytes([0x9A]) + struct.pack('<HH', 3 * verb, PART_SEG) + b'\xF4'
        uc.mem_write(STUB, stub)
        names = {'ax': UC_X86_REG_AX, 'bx': UC_X86_REG_BX, 'cx': UC_X86_REG_CX,
                 'dx': UC_X86_REG_DX, 'si': UC_X86_REG_SI, 'di': UC_X86_REG_DI,
                 'bp': UC_X86_REG_BP, 'ds': UC_X86_REG_DS, 'es': UC_X86_REG_ES}
        for r in names.values():
            uc.reg_write(r, 0)
        for k, v in regs.items():
            uc.reg_write(names[k], v & 0xFFFF)
        uc.reg_write(UC_X86_REG_SS, SS_SEG)
        uc.reg_write(UC_X86_REG_SP, 0xFFFE)
        uc.reg_write(UC_X86_REG_CS, 0)
        uc.reg_write(UC_X86_REG_IP, STUB)
        uc.reg_write(UC_X86_REG_FLAGS, 0x0002)
        uc.emu_start(STUB, STUB + 5, timeout=0, count=200_000_000)
        out = {k: uc.reg_read(r) for k, r in names.items()}
        out['cf'] = uc.reg_read(UC_X86_REG_FLAGS) & 1
        out['sp'] = uc.reg_read(UC_X86_REG_SP)
        return out

    def read(self, seg, off, n):
        return bytes(self.uc.mem_read(seg * 16 + off, n))

    def write(self, seg, off, b):
        self.uc.mem_write(seg * 16 + off, bytes(b))


def open_part(part, profile, div):
    r = part.call(1, ax=PNUM[profile] | (div << 8), es=GEN_SEG, dx=CLAIM_SEG)
    return r


def check_expand(image, verbose):
    fails = 0
    for prof in G.PNAMES:
        part = Part(image)
        L = G.layout(prof)
        caps = part.call(0, ax=PNUM[prof])
        if caps['cf'] or caps['ax'] != L['paras']:
            print('x3xtunit: CAPS %s = %r, want %d paragraphs' % (prof, caps, L['paras']))
            fails += 1
        div = 2 if prof == 'speech' else 1
        r = open_part(part, prof, div)
        if r['cf']:
            print('x3xtunit: OPEN %s refused (AX=%d)' % (prof, r['ax']))
            fails += 1
            continue
        # compare each expanded region with the generator's bytes
        blobs = G.expanded(prof)
        k = 0
        regions = []
        for g, s in enumerate(L['win']):
            regions.append(('W%d rows' % g, L['base']['W%d' % g], 0))
        regions.append(('ENCV', L['base']['SY'], G.SY['ENCV']))
        regions.append(('LEE', L['base']['SY'], G.SY['LEE']))
        regions.append(('RQL', L['base']['H1'], G.H1['RQL']))
        regions.append(('IMR', L['base']['H1'], G.H1['IMR']))
        regions.append(('ENCR', L['base']['H1'], G.H1['ENCR']))
        regions.append(('AA', L['base']['H1'], G.H1['AA']))
        regions.append(('IMWIN', L['base']['H2'], G.H2['IMWIN']))
        for (name, segp, off), want in zip(regions, blobs):
            got = part.read(CLAIM_SEG + segp, off, len(want))
            if got != want:
                bad = next(i for i in range(len(want)) if got[i] != want[i])
                print('x3xtunit: %s %s differs at byte %d (%02x, want %02x)' % (
                    prof, name, bad, got[bad], want[bad]))
                fails += 1
        if verbose:
            print('x3xtunit: %s OPEN ok, %d instructions' % (prof, 0))
    # refusals
    part = Part(image)
    if not open_part(part, 'std', 2)['cf']:
        print('x3xtunit: OPEN std at divisor 2 was not refused')
        fails += 1
    if not open_part(part, 'speech', 1)['cf']:
        print('x3xtunit: OPEN speech at divisor 1 was not refused')
        fails += 1
    g = bytearray(XT.gen_input())
    g[3000] ^= 1
    part.write(GEN_SEG, 0, g)
    r = open_part(part, 'std', 1)
    if not r['cf'] or r['ax'] != 2:
        print('x3xtunit: a damaged XTGEN.BIN was not refused (%r)' % r)
        fails += 1
    return fails


def record_bytes(h, chans, plan):
    b = bytearray(R_BYTES)
    b[0] = h.version
    b[1] = h.sr_idx
    b[2] = len(chans)
    b[3] = h.mode
    b[4] = h.mode_ext
    b[5] = 1 if plan is not None else 0
    for c, (ok, gi, ix, q) in enumerate(chans):
        o = R_CH0 + c * R_CHSZ
        if not ok:
            b[o] = 0
            continue
        b[o] = 1
        b[o + 1] = gi.block_type
        b[o + 2] = gi.mixed
        b[o + 3] = gi.win_switch
        nzl = np.nonzero(np.asarray(ix))[0]
        nz = int(nzl[-1]) + 1 if len(nzl) else 0
        struct.pack_into('<H', b, o + 4, nz)
        struct.pack_into('<576h', b, o + 6, *[int(v) for v in ix])
        struct.pack_into('<576h', b, o + 6 + 1152, *[int(v) for v in q])
    if plan is not None:
        gi_r = chans[1][1]
        sc = getattr(gi_r, 'is_scale', 0) or 0
        for l, p in enumerate(plan):
            if p == 'ms':
                b[R_PLAN + l] = 1
            elif p == 'lr':
                b[R_PLAN + l] = 0
            else:
                b[R_PLAN + l] = 0x80 | p[1] | (0x40 if (sc and h.version != 1) else 0)
    return bytes(b)


def run_fixture(image, name, prof, limit, verbose):
    data = open(os.path.join(S.FIXDIR, name), 'rb').read()
    st = S.Stream(data)
    div = SX.profile_div(prof, st.h0.rate)
    if div is None:
        return 0, 0
    part = Part(image)
    r = open_part(part, prof, div)
    if r['cf']:
        print('x3xtunit: OPEN %s refused (AX=%d)' % (prof, r['ax']))
        return 1, 0
    dec = SX.XTDecoder(prof, div)
    n = 0
    for h, chans, plan in SX.records(data):
        want = dec.granule(h, chans, plan)
        part.write(REC_SEG, 0, record_bytes(h, chans, plan))
        r = part.call(3, es=REC_SEG, si=0, dx=OUT_SEG, di=0)
        got = part.read(OUT_SEG, 0, len(want))
        if r['cf'] or r['ax'] != len(want):
            print('x3xtunit: %s %s granule %d: GRANULE answered %r' % (name, prof, n, r))
            return 1, n
        if got != want.tobytes():
            bad = next(i for i in range(len(want)) if got[i] != want[i])
            print('x3xtunit: %s %s granule %d differs from sample %d: got %s want %s' % (
                name, prof, n, bad, list(got[bad:bad + 8]), list(want[bad:bad + 8])))
            return 1, n
        n += 1
        if limit and n >= limit:
            break
    st = part.read(PART_SEG, 0, 0)
    if verbose:
        print('x3xtunit: %-13s %-6s d%d %4d granules ok' % (name, prof, div, n))
    return 0, n


class _G:
    """A granule's side information, as much of it as the backend reads."""
    pass


def synthetic(image, prof, count, seed, verbose):
    """Random joint-stereo records: block types that disagree, mixed
    blocks, every plan code, |ix| past 15, q past both ends of RQL."""
    rnd = random.Random(seed)
    fails = 0
    for version, sri in ((1, 0), (2, 1), (25, 1)):
        h = S.Header()
        h.version, h.sr_idx, h.nch, h.layer = version, sri, 2, 3
        rate = {1: (44100, 48000, 32000), 2: (22050, 24000, 16000),
                25: (11025, 12000, 8000)}[version][sri]
        div = SX.profile_div(prof, rate)
        if div is None:
            continue
        part = Part(image)
        if open_part(part, prof, div)['cf']:
            return 1
        dec = SX.XTDecoder(prof, div)
        for n in range(count):
            h.mode = 1
            h.mode_ext = rnd.randint(0, 3)
            chans = []
            for c in range(2):
                if rnd.random() < 0.1:
                    chans.append((False, None, None, None))
                    continue
                gi = _G()
                gi.block_type = rnd.choice((0, 0, 1, 2, 2, 3))
                gi.win_switch = 1 if gi.block_type else 0
                gi.mixed = rnd.randint(0, 1) if gi.block_type == 2 else 0
                gi.is_scale = rnd.randint(0, 1)
                gi.big_values = 1
                ix = np.zeros(576, dtype=np.int64)
                top = rnd.randint(0, 576)
                for l in range(top):
                    r = rnd.random()
                    if r < 0.5:
                        continue
                    v = rnd.randint(1, 15) if r < 0.97 else rnd.randint(16, 8206)
                    ix[l] = v if rnd.random() < 0.5 else -v
                q = np.array([rnd.randint(-150, 12) if rnd.random() < 0.03 else rnd.randint(-110, -70)
                              for _ in range(576)], dtype=np.int64)
                chans.append((True, gi, ix, q))
            plan = None
            if chans[0][0] and chans[1][0] and rnd.random() < 0.8:
                plan = []
                for l in range(576):
                    r = rnd.random()
                    if r < 0.4:
                        plan.append('ms')
                    elif r < 0.7:
                        plan.append('lr')
                    else:
                        plan.append(('is', rnd.randint(0, 6 if version == 1 else 30)))
            want = dec.granule(h, chans, plan)
            part.write(REC_SEG, 0, record_bytes(h, chans, plan))
            r = part.call(3, es=REC_SEG, si=0, dx=OUT_SEG, di=0)
            got = part.read(OUT_SEG, 0, len(want))
            if r['cf'] or got != want.tobytes():
                bad = next((i for i in range(len(want)) if got[i] != want[i]), -1)
                print('x3xtunit: synthetic %s v%s granule %d differs at %d: got %s want %s' % (
                    prof, version, n, bad, list(got[bad:bad + 6]), list(want[bad:bad + 6])))
                fails += 1
                break
        if verbose:
            print('x3xtunit: synthetic %-6s v%-2d d%d %d granules, %d split, %.1f%% clipped, %s' % (
                prof, version, div, n + 1, dec.stats['split'],
                100.0 * dec.stats['clip'] / ((n + 1) * 576 // div),
                'ok' if not fails else 'FAILED'))
    return fails


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--profile', nargs='*', default=list(G.PNAMES))
    ap.add_argument('--fixture', nargs='*', default=['X8K16.MP3', 'AFTERHRS.MP3'])
    ap.add_argument('--granules', type=int, default=0)
    ap.add_argument('--expand', action='store_true')
    ap.add_argument('--synthetic', type=int, default=0)
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    out = os.path.join(ROOT, 'build', 'x3xtunit', 'x3xt.bin')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    image = build(out)
    fails = 0
    if a.expand:
        fails += check_expand(image, a.v)
    elif a.synthetic:
        for prof in a.profile:
            fails += synthetic(image, prof, a.synthetic, 7, a.v)
    else:
        for name in a.fixture:
            for prof in a.profile:
                f, n = run_fixture(image, name, prof, a.granules, a.v)
                fails += f
    print('x3xtunit: %s, %d failure(s)' % ('ok' if not fails else 'FAILED', fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
