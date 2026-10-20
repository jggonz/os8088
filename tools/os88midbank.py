#!/usr/bin/env python3
"""MIDIRack's wavetable bank: a SoundFont made into MIDIRACK.BNK (SPEC.md
105.8.6), and the synthetic bank its gates use.

    python3 tools/os88midbank.py fetch                 # GeneralUser GS, pinned
    python3 tools/os88midbank.py build -o build/MIDIRACK.BNK [--kb 224]
    python3 tools/os88midbank.py synth -o build/mrsynth/MIDIRACK.BNK
    python3 tools/os88midbank.py check build/MIDIRACK.BNK
    python3 tools/os88midbank.py render SONG.MID BANK -o out.wav [--rate R]

THE SOURCE IS FETCHED, NEVER COMMITTED (CONTRIBUTING.md 6, the Apple II ROM's
rule): GeneralUser GS v2.0.3 by S. Christian Collins, at a pinned commit of
its author's own repository and a pinned SHA-256, into build/midibank-src/.
Its licence lets it be used in software projects and modified to suit
(documentation/LICENSE.txt there, quoted in SPEC.md 105.8.6); `make clean`
spares the download, as it spares the Apple II ROM, so a rebuilt tree needs
no network.

WHAT A BANK IS - one 8-bit sample for each General MIDI program and one for
each drum key, with its loop, its root key and a four-stage envelope, small
enough for an 8088's memory: the whole file is read once and kept. A 30 MB
SoundFont has dozens of velocity layers and key splits per instrument; this
takes the zone that sounds MIDDLE C at a moderate velocity, resamples it to
at most 11,025 Hz, and - when its loop still does not fit the budget - LOWERS
ITS RATE rather than cutting the loop, because a sustained note that stops is
worse than one that is a little duller. A one-shot (a piano's tail, a drum)
that does not fit is cut with a fade.

THE FILE (all little-endian, every sample on a 16-byte boundary so the player
can address one as a segment of its own):

    +0   'MRBK'        +4  dw 1 (version)   +6  dw the sample count
    +8   dd the data's offset (a multiple of 16)
    +12  dd the data's length
    +16  dw the program table's offset (32)   +18 dw the drum table's
    +20  dw the sample table's                 +22..31 zero
    program and drum tables: 128 entries of 8 bytes -
         dw sample (0xFFFF none), db root key, db fine (signed, 1/32 of a
         semitone), db attack, db decay (10 ms units), db sustain (0..127),
         db release (10 ms units)
    sample table: 12 bytes each -
         dw paragraph within the data, dw length, dw loop start, dw loop end
         (0 = no loop), dw rate (Hz), db gain (0..127), db 0
    the data: signed 8-bit samples

`synth` writes the same format from waveforms computed here - a sawtooth-
and-square pair for every program, a noise burst or a pitched thump for every
drum - for the gates (tests/midirack.py --arm wt): deterministic, CC0, no
network, and with harmonic content the pitch oracle can hear.
"""
import argparse
import hashlib
import math
import os
import struct
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRCDIR = os.path.join(ROOT, 'build', 'midibank-src')
SF2 = os.path.join(SRCDIR, 'GeneralUser-GS.sf2')
SF2_URL = ('https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/'
           '684543d5e5efaef08d02be50dcda8d552478fa60/GeneralUser-GS.sf2')
SF2_SHA = '9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe'

HDR = 32
ENT = 8
SMP = 12
MAXRATE = 11025
MINRATE = 4000


# --- the fetch ---------------------------------------------------------------
def fetch():
    if os.path.exists(SF2) and sha(SF2) == SF2_SHA:
        print('os88midbank: %s is present and pinned' % rel(SF2))
        return 0
    os.makedirs(SRCDIR, exist_ok=True)
    tmp = SF2 + '.part'
    print('os88midbank: fetching GeneralUser GS v2.0.3 (32 MB) ...')
    urllib.request.urlretrieve(SF2_URL, tmp)
    got = sha(tmp)
    if got != SF2_SHA:
        os.unlink(tmp)
        print('os88midbank: the download is %s, the pin is %s - refused'
              % (got, SF2_SHA))
        return 1
    os.replace(tmp, SF2)
    print('os88midbank: %s, SHA-256 pinned' % rel(SF2))
    return 0


def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def rel(p):
    return os.path.relpath(p, ROOT)


# --- reading a SoundFont 2 ----------------------------------------------------
class SF2File:
    def __init__(self, path):
        d = open(path, 'rb').read()
        if d[:4] != b'RIFF' or d[8:12] != b'sfbk':
            raise ValueError('%s is not a SoundFont 2' % path)
        self.chunks = {}
        self._walk(d, 12, len(d))
        self.smpl = self.chunks[b'smpl']
        self.phdr = self._recs(b'phdr', '<20sHHHIII')
        self.pbag = self._recs(b'pbag', '<HH')
        self.pgen = self._recs(b'pgen', '<HH')
        self.inst = self._recs(b'inst', '<20sH')
        self.ibag = self._recs(b'ibag', '<HH')
        self.igen = self._recs(b'igen', '<HH')
        self.shdr = self._recs(b'shdr', '<20sIIIIIBbHH')

    def _walk(self, d, i, end):
        while i + 8 <= end:
            cid, n = d[i:i + 4], struct.unpack_from('<I', d, i + 4)[0]
            if cid == b'LIST':
                self._walk(d, i + 12, i + 8 + n)
            else:
                self.chunks[cid] = d[i + 8:i + 8 + n]
            i += 8 + n + (n & 1)

    def _recs(self, cid, fmt):
        b = self.chunks[cid]
        sz = struct.calcsize(fmt)
        return [struct.unpack_from(fmt, b, i) for i in range(0, len(b), sz)]

    # zones: a list of {generator: value} dicts, the global first if any
    def _zones(self, bags, gens, first, last, term):
        out = []
        for b in range(first, last):
            g0, g1 = bags[b][0], bags[b + 1][0]
            z = {}
            for g in range(g0, g1):
                op, amt = gens[g]
                z[op] = amt
            out.append(z)
        glob = {}
        if out and term not in out[0]:
            glob = out.pop(0)
        return glob, out

    def preset(self, bank, prog):
        for i, h in enumerate(self.phdr[:-1]):
            if h[1] == prog and h[2] == bank:
                return self._zones(self.pbag, self.pgen, h[3],
                                   self.phdr[i + 1][3], 41)
        return None

    def instrument(self, n):
        return self._zones(self.ibag, self.igen, self.inst[n][1],
                           self.inst[n + 1][1], 53)


def s16(v):
    return v - 65536 if v >= 32768 else v


def inrange(z, gen, v):
    if gen not in z:
        return True
    lo, hi = z[gen] & 0xFF, z[gen] >> 8
    return lo <= v <= hi


def tc_secs(v, default):
    v = s16(v) if v is not None else default
    return 2.0 ** (v / 1200.0)


def pick(sf, bank, prog, key, vel=100):
    """The zone sounding `key` at `vel`: (sample header, merged generators)."""
    pr = sf.preset(bank, prog)
    if pr is None:
        return None
    pglob, pzones = pr
    for pz in pzones:
        if not (inrange(pz, 43, key) and inrange(pz, 44, vel)):
            continue
        iglob, izones = sf.instrument(pz[41])
        for iz in izones:
            if inrange(iz, 43, key) and inrange(iz, 44, vel):
                g = dict(iglob)
                g.update(iz)
                # the preset's generators ADD to the instrument's, for the
                # ones this reads
                for op in (48, 51, 52, 34, 36, 37, 38):
                    add = pz.get(op, pglob.get(op))
                    if add is not None:
                        g[op] = (s16(g.get(op, 0)) + s16(add)) & 0xFFFF
                return sf.shdr[iz[53]], g
    return None


# --- a sample, made into what the player takes ---------------------------------
def resample(x, src, dst):
    if src == dst:
        return list(x)
    n = int(len(x) * dst / src)
    out = []
    r = src / dst
    for i in range(n):
        p = i * r
        j = int(p)
        f = p - j
        a = x[j]
        b = x[j + 1] if j + 1 < len(x) else a
        out.append(a + (b - a) * f)
    return out


def take(sf, hdr, g, cap, drum):
    name, start, end, ls, le, rate, opitch, corr, link, stype = hdr
    start += s16(g.get(0, 0)) + 32768 * s16(g.get(4, 0))
    end += s16(g.get(1, 0)) + 32768 * s16(g.get(12, 0))
    ls += s16(g.get(2, 0)) + 32768 * s16(g.get(45, 0))
    le += s16(g.get(3, 0)) + 32768 * s16(g.get(50, 0))
    looped = (g.get(54, 0) & 3) in (1, 3) and le > ls + 8
    raw = struct.unpack_from('<%dh' % (end - start), sf.smpl, start * 2)
    keep = le - start if looped else len(raw)
    raw = raw[:keep]
    r = min(rate, MAXRATE)
    while looped and r > MINRATE and len(raw) * r / rate > cap:
        r = max(MINRATE, int(r * 0.85))
    x = resample([v / 32768.0 for v in raw], rate, r)
    lps = int((ls - start) * r / rate) if looped else 0
    lpe = len(x) if looped else 0
    if looped and lps < 0:              # a start moved past the loop's start
        lps = 0                         # by an offset generator: from there
    if looped and lpe - lps < 4:
        looped, lps, lpe = False, 0, 0
    if looped and lpe > cap:            # still too long: the loop is lost
        looped, lps, lpe = False, 0, 0
    if len(x) > cap:                    # a one-shot that does not fit:
        x = x[:cap]                     # cut, with a fade
        fade = min(len(x), 256)
        for i in range(fade):
            x[len(x) - fade + i] *= (fade - i) / fade
    peak = max(1e-6, max(abs(v) for v in x))
    data = bytes((int(round(v / peak * 127)) & 0xFF) for v in x)
    # THE GAIN IS THE SOUNDFONT'S ATTENUATION ALONE, AT HALF ITS dB: every
    # sample is normalised to full scale above, so its recording level is
    # already gone, and an 8-bit mix has no room to spend on a quiet
    # instrument - the first bank, which kept both, played a median of 30 of
    # 127 and peaked at 18 of 127 in Fur Elise
    att = 10 ** (-max(0, s16(g.get(48, 0))) / 400.0)
    gain = max(48, min(127, int(round(127 * att))))
    root = g.get(58, opitch)
    if root > 127:
        root = 60
    cents = corr + s16(g.get(52, 0)) + 100 * s16(g.get(51, 0))
    root -= int(round(cents / 100.0))
    fine = int(round((cents - 100 * round(cents / 100.0)) * 32 / 100.0))
    env = (
        min(255, int(round(tc_secs(g.get(34), -12000) * 100))),
        min(255, int(round(tc_secs(g.get(36), -12000) * 100))),
        max(0, min(127, int(round(127 * 10 ** (-max(0, s16(g.get(37, 0)))
                                                / 200.0))))),
        max(2, min(255, int(round(tc_secs(g.get(38), -12000) * 100)))),
    )
    if drum:
        env = (0, 0, 127 if not looped else env[2], max(env[3], 10))
    return {'data': data, 'lps': lps, 'lpe': lpe, 'rate': r, 'gain': gain,
            'root': root, 'fine': fine, 'env': env}


# --- the file ------------------------------------------------------------------
def write(path, progs, drums, samples):
    """progs / drums: 128 entries of None or (sample index, root, fine, env)."""
    data = bytearray()
    stab = bytearray()
    for s in samples:
        if len(data) % 16:
            data += bytes(16 - len(data) % 16)
        stab += struct.pack('<HHHHHBB', len(data) // 16, len(s['data']),
                            s['lps'], s['lpe'], s['rate'], s['gain'], 0)
        data += s['data']
    pt = bytearray()
    for tab in (progs, drums):
        for e in tab:
            if e is None:
                pt += struct.pack('<HBbBBBB', 0xFFFF, 60, 0, 0, 0, 0, 0)
            else:
                i, root, fine, env = e
                pt += struct.pack('<HBbBBBB', i, root, fine, *env)
    head_n = HDR + len(pt) + len(stab)
    doff = (head_n + 15) // 16 * 16
    hdr = struct.pack('<4sHHII HHH 10s', b'MRBK', 1, len(samples), doff,
                      len(data), HDR, HDR + 128 * ENT, HDR + 256 * ENT,
                      bytes(10))
    out = hdr + pt + stab
    out += bytes(doff - len(out)) + data
    with open(path, 'wb') as f:
        f.write(out)
    return out


def build(out, kb):
    if not os.path.exists(SF2) or sha(SF2) != SF2_SHA:
        print('os88midbank: no pinned SoundFont - run `%s fetch` (or `make '
              'midibank`)' % rel(__file__))
        return 1
    sf = SF2File(SF2)
    budget = kb * 1024 - 8192           # the index and the padding
    caps = (2400, 1100)                 # a program's, a drum's, to start
    while True:
        samples, progs, drums, seen = [], [None] * 128, [None] * 128, {}
        for p in range(128):
            z = pick(sf, 0, p, 60) or pick(sf, 0, p, 72) or pick(sf, 0, p, 48)
            if z:
                progs[p] = add(sf, z, caps[0], False, samples, seen)
        for k in range(27, 88):
            z = pick(sf, 128, 0, k)
            if z:
                drums[k] = add(sf, z, caps[1], True, samples, seen, key=k)
        total = sum(len(s['data']) + 16 for s in samples)
        if total <= budget:
            break
        caps = (int(caps[0] * 0.9), int(caps[1] * 0.9))
    b = write(out, progs, drums, samples)
    print('os88midbank: %d programs, %d drums, %d samples, %d bytes -> %s'
          % (sum(1 for x in progs if x), sum(1 for x in drums if x),
             len(samples), len(b), rel(out)))
    return 0


def add(sf, z, cap, drum, samples, seen, key=None):
    hdr, g = z
    sid = (hdr[1], hdr[2], cap, drum)
    if sid in seen:
        i = seen[sid]
        s = samples[i]
    else:
        s = take(sf, hdr, g, cap, drum)
        i = len(samples)
        samples.append(s)
        seen[sid] = i
    root = s['root']
    if drum and key is not None and 58 not in g:
        root = s['root']
    return (i, root, s['fine'], s['env'])


# --- the synthetic bank: the gates' -------------------------------------------
def synth(out):
    rate = 11025
    samples, progs, drums = [], [None] * 128, [None] * 128
    f0 = 440.0 * 2 ** ((60 - 69) / 12.0)       # middle C
    period = rate / f0
    n = int(round(period * 8))                   # eight cycles: a clean loop
    for kind in range(4):
        x = []
        for i in range(n):
            ph = (i / period) % 1.0
            saw = 2 * ph - 1
            sq = 1.0 if ph < 0.5 else -1.0
            v = [saw, 0.6 * saw + 0.4 * sq, sq * 0.8,
                 math.sin(2 * math.pi * ph) * 0.7 + 0.3 * saw][kind]
            x.append(v)
        data = bytes((int(round(v * 120)) & 0xFF) for v in x)
        samples.append({'data': data, 'lps': 0, 'lpe': n, 'rate': rate,
                        'gain': 110, 'root': 60, 'fine': 0,
                        'env': (1, 20, 90, 15)})
    for p in range(128):
        progs[p] = (p % 4, 60, 0, (1, 20, 90, 15))
    seed = 0xACE1
    noise = []
    for i in range(1500):
        seed = (seed >> 1) ^ (-(seed & 1) & 0xB400)
        noise.append((seed & 0xFF) - 128)
    for i in range(1500):
        noise[i] = int(noise[i] * (1 - i / 1500.0))
    samples.append({'data': bytes(v & 0xFF for v in noise), 'lps': 0,
                    'lpe': 0, 'rate': rate, 'gain': 70, 'root': 60,
                    'fine': 0, 'env': (0, 0, 127, 10)})
    thump = [int(110 * math.sin(2 * math.pi * 80 * i / rate) *
                 (1 - i / 1800.0)) for i in range(1800)]
    samples.append({'data': bytes(v & 0xFF for v in thump), 'lps': 0,
                    'lpe': 0, 'rate': rate, 'gain': 100, 'root': 60,
                    'fine': 0, 'env': (0, 0, 127, 10)})
    for k in range(27, 88):
        drums[k] = (5 if k in (35, 36) else 4, k, 0, (0, 0, 127, 10))
    b = write(out, progs, drums, samples)
    print('os88midbank: synthetic bank, %d samples, %d bytes -> %s'
          % (len(samples), len(b), rel(out)))
    return 0


# --- the independent reader ------------------------------------------------------
def check(path):
    d = open(path, 'rb').read()
    errs = []
    if d[:4] != b'MRBK' or struct.unpack_from('<H', d, 4)[0] != 1:
        print('os88midbank: %s is not a version-1 bank' % path)
        return 1
    ns, doff, dlen = struct.unpack_from('<HII', d, 6)
    po, do, so = struct.unpack_from('<HHH', d, 16)
    if doff % 16 or doff + dlen != len(d):
        errs.append('the data is not where the header says')
    for tab, off in (('program', po), ('drum', do)):
        for i in range(128):
            s, root, fine, a, dc, su, rl = struct.unpack_from('<HBbBBBB', d,
                                                              off + i * ENT)
            if s != 0xFFFF and s >= ns:
                errs.append('%s %d names sample %d of %d' % (tab, i, s, ns))
            if root > 127 or su > 127:
                errs.append('%s %d: root %d sustain %d' % (tab, i, root, su))
    for i in range(ns):
        para, ln, lps, lpe, rate, gain, z = struct.unpack_from('<HHHHHBB', d,
                                                               so + i * SMP)
        if para * 16 + ln > dlen:
            errs.append('sample %d runs past the data' % i)
        if lpe and not (lps < lpe <= ln):
            errs.append('sample %d: loop %d..%d of %d' % (i, lps, lpe, ln))
        if not (MINRATE <= rate <= 44100) or ln == 0:
            errs.append('sample %d: rate %d length %d' % (i, rate, ln))
    for e in errs[:10]:
        print('os88midbank: ' + e)
    print('os88midbank: %s: %d samples, %d bytes, %s'
          % (path, ns, len(d), 'FAILED' if errs else 'ok'))
    return 1 if errs else 0


# --- the reference player: what apps/midirack/mrwt.inc does, on the host ------
def render(song, bank, out, rate=11025, nvoices=16, secs=None):
    """The wavetable's own arithmetic, for LISTENING to a bank before a
    machine plays it: nearest-sample steps in 16.16, the envelope a 128-sample
    span at a time, a volume page 0..31 that scales the signed sample by
    p / 62, and the synth's soft clip, 127 x / (|x| + 64), into 8 bits."""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import os88midi as om
    import wave
    d = open(bank, 'rb').read()
    ns, doff = struct.unpack_from('<HI', d, 6)
    po, do, so = struct.unpack_from('<HHH', d, 16)
    smp = [struct.unpack_from('<HHHHHBB', d, so + i * SMP) for i in range(ns)]

    def entry(off, i):
        return struct.unpack_from('<HBbBBBB', d, off + i * ENT)

    def step_for(e, srate, key, bend):
        n = (key - e[1]) * 32 + e[2] + bend
        return srate / rate * 2 ** (n / 384.0)

    def env_step(t10):
        spans = int(t10 * rate / 12800)
        return (127 << 8) if spans == 0 else max(1, (127 << 8) // spans)

    ev, total = om.timeline(open(song, 'rb').read())
    if secs:
        total = min(total, secs)
    prog, vol, expr, bend, brange = ([0] * 16, [100] * 16, [127] * 16,
                                     [0] * 16, [2] * 16)
    voices = []
    pcm = bytearray()
    ei, t = 0, 0
    nsamp = int((total + 1.0) * rate)
    while t < nsamp:
        while ei < len(ev) and ev[ei][0] * rate <= t:
            _, st, data = ev[ei]
            ei += 1
            ch, k = st & 15, st & 0xF0
            if k == 0x90 and data[1]:
                e = entry(do if ch == 9 else po, data[0] if ch == 9 else
                          prog[ch])
                if e[0] == 0xFFFF:
                    continue
                para, ln, lps, lpe, srate, gain, _ = smp[e[0]]
                amp = data[1] * vol[ch] * expr[ch] // 16129 * gain // 127
                v = {'ch': ch, 'key': data[0], 'e': e, 'base': para * 16 +
                     doff, 'end': lpe or ln, 'll': (lpe - lps) if lpe else 0,
                     'pos': 0.0, 'srate': srate, 'amp': amp,
                     'stage': 1 if e[3] else 2,
                     'env': 0 if e[3] else 127 << 8,
                     'ad': env_step(e[3]), 'dd': env_step(e[4]),
                     'rd': env_step(e[6]), 'sl': e[5] << 8,
                     'drum': ch == 9}
                v['step'] = step_for(e, srate, data[0],
                                     0 if ch == 9 else bend[ch])
                if len(voices) >= nvoices:
                    voices.pop(0)
                voices.append(v)
            elif k in (0x80, 0x90):
                for v in voices:
                    if v['ch'] == ch and v['key'] == data[0] and not v['drum']:
                        v['stage'] = 4
            elif k == 0xC0:
                prog[ch] = data[0]
            elif k == 0xB0:
                if data[0] == 7:
                    vol[ch] = data[1]
                elif data[0] == 11:
                    expr[ch] = data[1]
            elif k == 0xE0:
                b = ((data[1] << 7) | data[0]) - 8192
                bend[ch] = b * brange[ch] // 256
                for v in voices:
                    if v['ch'] == ch and not v['drum']:
                        v['step'] = step_for(v['e'], v['srate'], v['key'],
                                             bend[ch])
        for v in voices:                       # the envelope, a span on
            st = v['stage']
            if st == 1:
                v['env'] = min(127 << 8, v['env'] + v['ad'])
                if v['env'] >= 127 << 8:
                    v['stage'] = 2
            elif st == 2:
                v['env'] = max(v['sl'], v['env'] - v['dd'])
                if v['env'] <= v['sl']:
                    v['stage'] = 3 if v['sl'] else 0
            elif st == 4:
                v['env'] -= v['rd']
                if v['env'] <= 0:
                    v['stage'] = 0
            v['pg'] = (v['amp'] * (max(0, v['env']) >> 8)) >> 9
        voices = [v for v in voices if v['stage']]
        acc = [0] * 128
        for v in voices:
            pos, stp, pg = v['pos'], v['step'], v['pg']
            base, end, ll = v['base'], v['end'], v['ll']
            for i in range(128):
                b = d[base + int(pos)]
                sv = b - 256 if b >= 128 else b
                q = sv * pg
                acc[i] += int((q + (31 if q >= 0 else -31)) / 62)
                pos += stp
                if pos >= end:
                    if not ll:
                        v['stage'] = 0
                        break
                    while pos >= end:
                        pos -= ll
            v['pos'] = pos
        for x in acc:
            pcm.append((127 * x // (abs(x) + 64) + 128) & 0xFF)
        t += 128
    with wave.open(out, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(rate)
        w.writeframes(bytes(pcm))
    print('os88midbank: %s through %s, %.1f s at %d Hz, %d voices -> %s'
          % (os.path.basename(song), os.path.basename(bank), len(pcm) / rate,
             rate, nvoices, out))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('fetch')
    b = sub.add_parser('build')
    b.add_argument('-o', '--out', required=True)
    b.add_argument('--kb', type=int, default=224)
    s = sub.add_parser('synth')
    s.add_argument('-o', '--out', required=True)
    c = sub.add_parser('check')
    c.add_argument('path')
    r = sub.add_parser('render')
    r.add_argument('song')
    r.add_argument('bank')
    r.add_argument('-o', '--out', required=True)
    r.add_argument('--rate', type=int, default=11025)
    r.add_argument('--voices', type=int, default=16)
    r.add_argument('--secs', type=float)
    a = ap.parse_args()
    if a.cmd == 'render':
        return render(a.song, a.bank, a.out, a.rate, a.voices, a.secs)
    if a.cmd == 'fetch':
        return fetch()
    if a.cmd == 'build':
        r = build(a.out, a.kb)
        return r or check(a.out)
    if a.cmd == 'synth':
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        return synth(a.out) or check(a.out)
    return check(a.path)


if __name__ == '__main__':
    sys.exit(main())
