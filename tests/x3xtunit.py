#!/usr/bin/env python3
"""The XT backend part (apps/xtmp3/x3xt.asm) run on the host against its
model (tools/x3simxt.py), in Unicorn's x86 core (SPEC.md 121.4).

    python3 tests/x3xtunit.py [--profile P ...] [--fixture F ...] [--granules N]
                              [--expand] [--synthetic N] [-v]

  --expand     CAPS and OPEN for each profile, every expanded table compared
               byte for byte with tools/x3xtgen.py's expanded(); every
               X3E_ refusal (x3gran.inc); a damaged XTGEN.BIN and an
               unbiased recurrence caught by the Fletcher check; the S16
               format and the gain against their definitions
  --fixture    decode fixtures granule by granule through GRANULE and compare
               each granule's PCM, CX, AX and STATS with the model's
  --synthetic  N random records with disagreeing block types and random
               plans, to reach the split path no LAME fixture takes
  (none)       all three: four fixtures, 30 synthetic granules a profile

Every call is checked against x3gran.inc's contract: the registers kept
but AX and the outputs, DF clear, IF untouched, at most 64 bytes of stack.

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
import tempfile

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
CLAIM_SEG = 0x2200
REC_SEG = 0x8000
OUT_SEG = 0x8200
ARG_SEG = 0x8300                 # CAPS' and OPEN's records, STATS'
STUB = 0x0500
SS_SEG = 0x9000
PNUM = {'std': 0, 'fast': 1, 'turbo': 2, 'speech': 3}

# apps/xtmp3/x3gran.inc
G_HDR = 32
G_CSZ = 6 + 2304
G_CH0 = G_HDR
G_PL = G_CH0 + 2 * G_CSZ
G_SIZE = G_PL + 576
X3E = {1: 'PROFILE', 2: 'DIV', 3: 'CHAN', 4: 'FMT', 5: 'MEM', 6: 'TABLE', 7: 'RECORD',
       8: 'NOTOPEN', 9: 'ABI'}


def build(out):
    inc = os.path.join(os.path.dirname(out), 'x3xtk.inc')
    G.emit(inc)
    G.xtgen(os.path.join(os.path.dirname(out), 'XTGEN.BIN'))
    subprocess.run(['nasm', '-f', 'bin', '-w+error', '-I', os.path.join(ROOT, 'apps', 'xtmp3') + '/',
                    '-I', os.path.dirname(out) + '/', '-I', os.path.join(ROOT, 'apps') + '/', '-o', out,
                    os.path.join(ROOT, 'apps', 'xtmp3', 'x3xt.asm')], check=True)
    G.seal(out)
    return open(out, 'rb').read()


class Part:
    """The part at PART_SEG, called through its header's vectors as
    x3gran.inc says: DS = its segment, a 64-byte stack budget checked."""

    def __init__(self, image):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, 1 << 20)
        self.uc.mem_write(PART_SEG * 16, image)
        self.image = image
        self.segs = []
        self.low = 0x10000
        self.clobber = True

    def call(self, verb, **regs):
        uc = self.uc
        vec = struct.unpack_from('<H', self.image, 6 + 2 * verb)[0]
        stub = bytes([0x9A]) + struct.pack('<HH', vec, PART_SEG) + b'\xF4'
        uc.mem_write(STUB, stub)
        names = {'ax': UC_X86_REG_AX, 'bx': UC_X86_REG_BX, 'cx': UC_X86_REG_CX,
                 'dx': UC_X86_REG_DX, 'si': UC_X86_REG_SI, 'di': UC_X86_REG_DI,
                 'bp': UC_X86_REG_BP, 'ds': UC_X86_REG_DS, 'es': UC_X86_REG_ES}
        sentinel = {'bx': 0x1111, 'cx': 0x2222, 'dx': 0x3333, 'si': 0x4444, 'di': 0x5555,
                    'bp': 0x6666, 'es': 0x7777}
        for k, r in names.items():
            uc.reg_write(r, sentinel.get(k, 0))
        regs.setdefault('ds', PART_SEG)
        outs = regs.pop('_outs', ())
        for k, v in regs.items():
            uc.reg_write(names[k], v & 0xFFFF)
        before = {k: uc.reg_read(r) for k, r in names.items()}
        uc.mem_write(SS_SEG * 16, bytes([0xA5]) * 0x10000)
        uc.reg_write(UC_X86_REG_SS, SS_SEG)
        uc.reg_write(UC_X86_REG_SP, 0xFFFE)
        uc.reg_write(UC_X86_REG_CS, 0)
        uc.reg_write(UC_X86_REG_IP, STUB)
        uc.reg_write(UC_X86_REG_FLAGS, 0x0202)
        uc.emu_start(STUB, STUB + 5, timeout=0, count=400_000_000)
        out = {k: uc.reg_read(r) for k, r in names.items()}
        fl = uc.reg_read(UC_X86_REG_FLAGS)
        out['cf'] = fl & 1
        out['sp'] = uc.reg_read(UC_X86_REG_SP)
        st = bytes(uc.mem_read(SS_SEG * 16, 0x10000))
        used = 0x10000 - next(i for i in range(0x10000) if st[i] != 0xA5) - 2
        self.low = min(self.low, 0xFFFE - used)
        out['stack'] = used + 2     # the far call's own four included (it counts from FFFE)
        # the contract: every register but AX and the outputs kept, DF clear, IF kept
        keep = [k for k in names if k != 'ax' and k not in outs]
        out['kept'] = all(out[k] == before[k] for k in keep)
        out['df'] = (fl >> 10) & 1
        out['if'] = (fl >> 9) & 1
        return out

    def read(self, seg, off, n):
        return bytes(self.uc.mem_read(seg * 16 + off, n))

    def write(self, seg, off, b):
        self.uc.mem_write(seg * 16 + off, bytes(b))

    def caps(self, prof, div, och=1):
        r = self.call(0, ax=PNUM[prof] | (div << 8), cx=och, es=ARG_SEG, di=0)
        if r['cf']:
            return None, r
        c = self.read(ARG_SEG, 0, 18)
        offer, nseg = c[0], c[1]
        paras = list(struct.unpack_from('<6H', c, 2))[:nseg]
        tkb, partkb = struct.unpack_from('<HH', c, 14)
        return dict(offer=offer, nseg=nseg, paras=paras, tkb=tkb, partkb=partkb), r

    def open(self, prof, div, ver=25, sri=1, fmt=1, gain=0, och=1, nseg=None, segs=None):
        cp, r = self.caps(prof, div)
        if cp is None:
            return r
        if segs is None:
            segs, at = [], CLAIM_SEG
            for p_ in cp['paras']:
                segs.append(at)
                at += p_
            assert at <= REC_SEG, 'the claim does not fit the harness map'
        self.segs = segs
        rec = struct.pack('<BBBBBBBBHBB', PNUM[prof], div, och, fmt, 1, ver, sri, 0, gain,
                          cp['nseg'] if nseg is None else nseg, 0)
        rec += struct.pack('<6H', *(list(segs) + [0] * (6 - len(segs))))
        self.write(ARG_SEG, 0, rec)
        r = self.call(1, es=ARG_SEG, di=0)
        if not r['cf'] and self.clobber:
            # OPEN's half is only OPEN's: GRANULE must run with it gone (the
            # engine may shrink the claim to X3C_PARTKB), so overwrite it
            tail, end = struct.unpack_from('<HH', self.image, 20)   # the seal (x3xtgen.py)
            self.write(PART_SEG, tail, bytes([0xCC]) * (end - tail))
        return r

    def granule(self, rec):
        self.write(REC_SEG, 0, rec)
        r = self.call(3, es=REC_SEG, si=0, dx=OUT_SEG, di=0, _outs=('cx',))
        return r

    def stats(self):
        self.call(4, es=ARG_SEG, di=0)
        b = self.read(ARG_SEG, 0, 32)
        g, c, sat = struct.unpack_from('<III', b, 0)
        stage = struct.unpack_from('<8H', b, 12)
        silent = struct.unpack_from('<I', b, 28)[0]
        return dict(gran=g, clip=c, sat=sat, stage=stage, silent=silent)


def open_part(part, profile, div, ver=25, sri=1):
    return part.open(profile, div, ver=ver, sri=sri)


def check_expand(image, verbose):
    fails = 0
    for prof in G.PNAMES:
        part = Part(image)
        L = G.layout(prof)
        div = 2 if prof == 'speech' else 1
        cp, r = part.caps(prof, div)
        want = [G.paras(nb) for _, nb in L['segs']]
        if cp is None or cp['paras'] != want or cp['offer'] != 15:
            print('x3xtunit: CAPS %s = %r, want %r' % (prof, cp, want))
            fails += 1
            continue
        r = part.open(prof, div, ver=25 if div == 2 else 1, sri=1)
        if r['cf']:
            print('x3xtunit: OPEN %s refused (%s)' % (prof, X3E.get(r['ax'], r['ax'])))
            fails += 1
            continue
        if not r['kept'] or r['stack'] > 64:
            print('x3xtunit: OPEN %s broke the contract: kept %r, stack %d' % (prof, r['kept'], r['stack']))
            fails += 1
        nw = len(L['win'])
        regions = [('W%d rows' % g, part.segs[g], 0) for g in range(nw)]
        regions += [('ENCV', part.segs[nw], G.SY['ENCV']), ('LEE', part.segs[nw], G.SY['LEE']),
                    ('RQL', part.segs[nw + 1], G.H1['RQL']), ('IMR', part.segs[nw + 1], G.H1['IMR']),
                    ('ENCR', part.segs[nw + 1], G.H1['ENCR']), ('AA', part.segs[nw + 1], G.H1['AA']),
                    ('IMWIN', part.segs[nw + 2], G.H2['IMWIN'])]
        for (name, seg, off), want in zip(regions, G.expanded(prof)):
            got = part.read(seg, off, len(want))
            if got != want:
                bad = next(i for i in range(len(want)) if got[i] != want[i])
                print('x3xtunit: %s %s differs at byte %d (%02x, want %02x)' % (
                    prof, name, bad, got[bad], want[bad]))
                fails += 1
        if verbose:
            print('x3xtunit: %s CAPS %s, %d KB of tables, part %d KB; OPEN ok, stack %d bytes' % (
                prof, cp['paras'], cp['tkb'], cp['partkb'], r['stack']))

    def refused(r, want, what):
        if not r['cf'] or X3E.get(r['ax']) != want:
            print('x3xtunit: %s: %r, want X3E_%s' % (what, X3E.get(r['ax']) if r['cf'] else 'accepted', want))
            return 1
        return 0
    part = Part(image)
    fails += refused(part.call(3, es=REC_SEG, si=0, dx=OUT_SEG, di=0), 'NOTOPEN', 'GRANULE before OPEN')
    fails += refused(part.call(0, ax=PNUM['std'] | (2 << 8), cx=1, es=ARG_SEG, di=0), 'DIV', 'CAPS std at 2')
    fails += refused(part.call(0, ax=7 | (1 << 8), cx=1, es=ARG_SEG, di=0), 'PROFILE', 'CAPS profile 7')
    fails += refused(part.call(0, ax=PNUM['std'] | (1 << 8), cx=2, es=ARG_SEG, di=0), 'CHAN', 'CAPS stereo')
    # the SPEECH tables take 2 or 4 and nothing else - on ANY version, since
    # XT Standard divided by its source runs on them too (x3x_opentail's
    # rule, SPEC.md 121.4). So a divisor is refused for being 1 or 3, never
    # for the stream it would run on
    fails += refused(part.open('speech', 1, ver=1, sri=0), 'DIV', 'speech at 1')
    fails += refused(part.open('speech', 3, ver=25, sri=0), 'DIV', 'speech at 3')
    for div, ver, what in ((2, 1, 'speech 2:1 on MPEG-1'), (4, 25, 'speech 4:1 on MPEG-2.5')):
        r = Part(image).open('speech', div, ver=ver, sri=0)   # (an accepted
        if r['cf']:                                          # OPEN spends the
            print('x3xtunit: %s refused (%s), want accepted'  # part's tail)
                  % (what, X3E.get(r['ax'], r['ax'])))
            fails += 1
    fails += refused(part.open('std', 1, ver=3, sri=0), 'RECORD', 'version 3')
    fails += refused(part.open('std', 1, ver=1, sri=3), 'RECORD', 'rate index 3')
    fails += refused(part.open('std', 1, fmt=2), 'FMT', 'format 2')
    fails += refused(part.open('std', 1, och=2), 'CHAN', 'two channels')
    fails += refused(part.open('std', 1, nseg=4), 'MEM', 'four segments')
    fails += refused(part.open('std', 1, segs=[CLAIM_SEG, 0, 0x4000, 0x5000, 0x6000]), 'MEM', 'a zero segment')
    g = bytearray(image)
    at = g.find(XT.gen_input())
    g[at + 3000] ^= 1
    fails += refused(Part(bytes(g)).open('std', 1), 'TABLE', 'a damaged XTGEN.BIN')
    # an expansion gone wrong (a row built without its rounding bias, in the
    # part's own code - x3xttab's own negative control) fails the check
    g = bytearray(image)
    pat = bytes([0xB8, 0x00, 0x80, 0x31, 0xD2])      # mov ax,8000h / xor dx,dx
    k = g.find(pat)
    assert k > 0, 'the recurrence bias is not where the control expects it'
    g[k + 2] = 0x00                                  # mov ax,0
    fails += refused(Part(bytes(g)).open('std', 1), 'TABLE', 'an unbiased recurrence, unsealed')
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(bytes(g))
    G.seal(f.name)                                   # the tail check passes now:
    g = open(f.name, 'rb').read()                    # the EXPANSION's must see it
    os.unlink(f.name)
    fails += refused(Part(g).open('std', 1), 'TABLE', 'an unbiased recurrence, sealed')
    # an intact part takes OPEN after OPEN (calibration probes rungs on one
    # fetch): its tail is read-only, so its check passes every time
    part = Part(image)
    part.clobber = False
    for prof, div, ver in (('std', 1, 1), ('fast', 1, 1), ('speech', 2, 25), ('std', 1, 25)):
        r = part.open(prof, div, ver=ver, sri=1)
        if r['cf']:
            print('x3xtunit: OPEN %s after OPEN on an intact part refused (%s)' % (prof, X3E.get(r['ax'])))
            fails += 1
    # a part whose OPEN half is gone (shrunk to X3C_PARTKB) refuses a second
    # OPEN instead of running what is there
    part = Part(image)
    part.open('std', 1)
    fails += refused(part.open('fast', 1), 'TABLE', 'OPEN after the tail is gone')
    # GRANULE refuses a record of another version or rate than OPEN's
    part = Part(image)
    part.open('std', 1, ver=25, sri=1)
    h = S.Header()
    h.version, h.sr_idx, h.mode, h.mode_ext = 2, 1, 3, 0
    fails += refused(part.granule(record_bytes(h, [(False, None, None, None)], None)), 'RECORD',
                     'a record of another version')
    return fails


def check_formats(image, verbose):
    """S16 is the U8 sample widened, (u8 - 128) << 8; a gain is applied to
    that int16, floored and saturated, before either is written."""
    fails = 0
    data = open(os.path.join(S.FIXDIR, 'NIGHTSIG.MP3'), 'rb').read()
    h0 = S.Stream(data).h0
    for fmt, gain in ((0, 0), (1, 384), (0, 300), (0, 64), (1, 0), (0, 2000)):
        part = Part(image)
        part.open('std', 1, ver=h0.version, sri=h0.sr_idx, fmt=fmt, gain=gain)
        dec = SX.XTDecoder('std', 1)
        for n, (h, ch, pl) in enumerate(SX.records(data)):
            if n >= 60:
                break
            want = dec.granule(h, ch, pl)
            part.granule(record_bytes(h, ch, pl))
            s16 = (want.astype(np.int64) - 128) << 8
            if gain:
                s16 = np.clip((s16 * gain) >> 8, -32768, 32767)
            if fmt == 0:
                got = np.frombuffer(part.read(OUT_SEG, 0, 2 * len(want)), '<i2')
                ok = np.array_equal(got, s16)
            else:
                got = np.frombuffer(part.read(OUT_SEG, 0, len(want)), 'u1')
                ok = np.array_equal(got, ((s16 >> 8) & 255) ^ 0x80)
            if not ok:
                print('x3xtunit: format %d gain %d differs at granule %d' % (fmt, gain, n))
                fails += 1
                break
        if verbose:
            print('x3xtunit: format %s gain %d %s' % ('S16' if fmt == 0 else 'U8', gain,
                                                    'ok' if not fails else 'FAILED'))
    return fails


def record_bytes(h, chans, plan, gr=0, intensity=None):
    """x3sim's view of a granule as the FRONT part writes it (x3gran.inc)."""
    b = bytearray(G_SIZE)
    b[0] = h.version
    b[1] = h.sr_idx
    b[2] = len(chans)
    b[3] = h.mode
    b[4] = h.mode_ext
    b[5] = h.granules if hasattr(h, 'granules') else 1
    b[6] = gr
    ms = h.mode == 1 and bool(h.mode_ext & 2)
    isx = (h.mode == 1 and bool(h.mode_ext & 1)) if intensity is None else intensity
    if plan is None:
        b[7] = 0
    elif isx:
        b[7] = 2
    else:
        b[7] = 1 if ms else 0
    if plan is not None and h.version != 1 and isx:
        b[8] = getattr(chans[1][1], 'is_scale', 0) or 0
    b[9] = 1 if ms else 0
    struct.pack_into('<H', b, 10, {1: (44100, 48000, 32000), 2: (22050, 24000, 16000),
                                   25: (11025, 12000, 8000)}[h.version][h.sr_idx])
    import x3tab_iso as ISO
    b[16:29] = bytes(ISO.SFB_SHORT[(h.version, h.sr_idx)])
    for c, (ok, gi, ix, q) in enumerate(chans):
        o = G_CH0 + c * G_CSZ
        if not ok:
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
    if plan is not None and b[7] == 2:
        for l, p in enumerate(plan):
            b[G_PL + l] = 1 if p == 'ms' else (0 if p == 'lr' else 0x80 | p[1])
    return bytes(b)


def run_fixture(image, name, prof, limit, verbose, stack=[0]):
    data = open(os.path.join(S.FIXDIR, name), 'rb').read()
    st = S.Stream(data)
    div = SX.profile_div(prof, st.h0.rate)
    if div is None:
        return 0, 0
    part = Part(image)
    r = part.open(prof, div, ver=st.h0.version, sri=st.h0.sr_idx)
    if r['cf']:
        print('x3xtunit: OPEN %s refused (%s)' % (prof, X3E.get(r['ax'], r['ax'])))
        return 1, 0
    dec = SX.XTDecoder(prof, div)
    n = 0
    clip0 = 0
    for h, chans, plan in SX.records(data):
        want = dec.granule(h, chans, plan)
        r = part.granule(record_bytes(h, chans, plan))
        got = part.read(OUT_SEG, 0, len(want))
        stack[0] = max(stack[0], r['stack'])
        if r['cf'] or r['cx'] != len(want) or r['ax'] != dec.stats['clip'] - clip0 \
                or not r['kept'] or r['df'] or not r['if'] or r['stack'] > 64:
            print('x3xtunit: %s %s granule %d: GRANULE answered %r (clips %d)' % (
                name, prof, n, r, dec.stats['clip'] - clip0))
            return 1, n
        clip0 = dec.stats['clip']
        if got != want.tobytes():
            bad = next(i for i in range(len(want)) if got[i] != want[i])
            print('x3xtunit: %s %s granule %d differs from sample %d: got %s want %s' % (
                name, prof, n, bad, list(got[bad:bad + 8]), list(want[bad:bad + 8])))
            return 1, n
        n += 1
        if limit and n >= limit:
            break
    z = part.stats()
    if z['gran'] != n or z['clip'] != dec.stats['clip']:
        print('x3xtunit: %s %s STATS %r against %d granules, %d clips' % (name, prof, z, n, dec.stats['clip']))
        return 1, n
    if verbose:
        print('x3xtunit: %-13s %-6s d%d %4d granules ok, stack %d bytes, %s' % (
            name, prof, div, n, stack[0], z))
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
        if part.open(prof, div, ver=version, sri=sri)['cf']:
            print('x3xtunit: synthetic OPEN refused')
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
            r = part.granule(record_bytes(h, chans, plan, intensity=plan is not None))
            got = part.read(OUT_SEG, 0, len(want))
            if r['cf'] or got != want.tobytes() or r['stack'] > 64 or not r['kept']:
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
    ap.add_argument('--fixture', nargs='*', default=None)
    ap.add_argument('--granules', type=int, default=0)
    ap.add_argument('--expand', action='store_true')
    ap.add_argument('--synthetic', type=int, default=0)
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    out = os.path.join(ROOT, 'build', 'x3xtunit', 'x3xt.bin')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    image = build(out)
    fails = 0
    every = not (a.expand or a.synthetic or a.fixture)
    if a.expand or every:
        fails += check_expand(image, a.v)
        fails += check_formats(image, a.v)
    if a.synthetic or every:
        for prof in a.profile:
            fails += synthetic(image, prof, a.synthetic or 30, 7, a.v)
    if a.fixture or every:
        for name in a.fixture or ('X8K16.MP3', 'IS22.MP3', 'MONO44.MP3', 'BBADSIDE.MP3'):
            for prof in a.profile:
                f, n = run_fixture(image, name, prof, a.granules, a.v)
                fails += f
    print('x3xtunit: %s, %d failure(s)' % ('ok' if not fails else 'FAILED', fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
