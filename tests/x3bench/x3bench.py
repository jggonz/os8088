#!/usr/bin/env python3
"""x3bench: the XTMP3 decoder on a real x86, against tools/x3sim.py (SPEC.md 121.4).

    python3 tests/x3bench/x3bench.py [--front] [--pcm] [--only NAME..] [--keep]

Builds a raw hard-disk image - tests/x3bench/x3bboot.asm, the harness
tests/x3bench/x3bench.asm (which %includes the SHIPPING apps/xtmp3/x3pars.inc),
the package's two parts build/x3front.bin and build/x3std.bin as `make` built
them, and the fixtures of tests/xtmp3/ - boots it in QEMU, and reads what the
harness writes to the debug console.

--front  every fixture's open (title, artist, frames, duration, the stream's
         identity) against x3sim.Stream, and every granule's RECORD - each
         channel's ok flag, block type, mixed, window switching, last non-zero
         line, ix and q, and the stereo plan - against x3sim's own
         granule_spectra / quarter_exponents / stereo_plan; the verdict
         counters against Stream.counts.
--pcm    every one of tests/xtmp3/X3SIM.txt's 141 cases (each fixture at
         divisors 1, 2 and 4, and the mono downmixes) through the STANDARD
         part's GRANULE: the PCM's SHA-256, its length and its clip count
         against the golden line. A case that differs is reported with its
         first differing sample (decoded by x3sim for the comparison).

Both by default. Exit 0 only when every case matched. QEMU is exact about the
work, which is what bit-exactness needs; MartyPC is for the cycles.
"""
import argparse
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import numpy as np   # noqa: E402
import x3sim as X    # noqa: E402

BUILD = os.environ.get('BUILD', os.path.join(ROOT, 'build'))
FIXDIR = os.path.join(ROOT, 'tests', 'xtmp3')
NASM = ['nasm', '-f', 'bin', '-w+error', '-I', 'apps/', '-I', 'apps/xtmp3/',
        '-I', BUILD + '/']

# record layout (apps/xtmp3/x3gran.inc)
G_HDR = 32
C_SZ = 32
DIR_SECTORS = 16


def sh(cmd, **kw):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.stdout.write(r.stdout + r.stderr)
        raise SystemExit('x3bench: %s failed' % ' '.join(cmd[:4]))
    return r


def build(need_std):
    """The parts are make's; the harness and its boot sector are built here."""
    targets = [os.path.join(BUILD, 'x3front.bin')]
    if need_std:
        targets.append(os.path.join(BUILD, 'x3stdp.bin'))
    sh(['make', '-s'] + [os.path.relpath(t, ROOT) for t in targets])
    sh(NASM + ['-o', os.path.join(BUILD, 'x3bboot.bin'), 'tests/x3bench/x3bboot.asm'])
    sh(NASM + ['-o', os.path.join(BUILD, 'x3bench.bin'), 'tests/x3bench/x3bench.asm'])


def sectors(b):
    return (len(b) + 511) // 512


def pad(b):
    return b + b'\0' * (sectors(b) * 512 - len(b))


def make_image(cases, need_std, path):
    """cases: [(name, div, mono, mode, ringk, chunk)]"""
    boot = open(os.path.join(BUILD, 'x3bboot.bin'), 'rb').read()
    harness = open(os.path.join(BUILD, 'x3bench.bin'), 'rb').read()
    front = open(os.path.join(BUILD, 'x3front.bin'), 'rb').read()
    std = open(os.path.join(BUILD, 'x3stdp.bin'), 'rb').read() if need_std else b''
    lba = 1 + DIR_SECTORS
    body = pad(harness)
    lba += sectors(harness)
    front_lba = lba
    body += pad(front)
    lba += sectors(front)
    std_lba = lba
    body += pad(std)
    lba += sectors(std)
    files = {}
    for c in cases:
        if c[0] not in files:
            data = open(os.path.join(FIXDIR, c[0]), 'rb').read()
            files[c[0]] = (lba, len(data))
            body += pad(data)
            lba += sectors(data)
    d = b'X3BD' + struct.pack('<HHHHHH', front_lba, sectors(front), std_lba,
                              sectors(std), len(cases), sectors(harness))
    for name, div, mono, mode, ringk, chunk in cases:
        flba, fsz = files[name]
        d += struct.pack('<IIBBBBH12s6x', flba, fsz, div, mono, mode, ringk, chunk,
                         name.encode())
    assert len(d) <= DIR_SECTORS * 512, 'too many cases for the directory'
    d += b'\0' * (DIR_SECTORS * 512 - len(d))
    img = boot + d + body
    img += b'\0' * ((-len(img)) % (1024 * 1024))     # a whole number of MB
    with open(path, 'wb') as f:
        f.write(img)


def run_qemu(img, out, timeout):
    if os.path.exists(out):
        os.unlink(out)
    cmd = ['qemu-system-i386', '-drive', 'file=%s,format=raw,if=ide' % img,
           '-boot', 'c', '-display', 'none', '-no-reboot', '-m', '4',
           '-chardev', 'file,id=dc,path=%s' % out,
           '-device', 'isa-debugcon,iobase=0xe9,chardev=dc',
           '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x01']
    t0 = time.time()
    try:
        subprocess.run(cmd, timeout=timeout, capture_output=True)
    except subprocess.TimeoutExpired:
        print('x3bench: QEMU timed out after %d s' % timeout)
    return time.time() - t0


def records(raw):
    p = 0
    while p + 6 <= len(raw):
        if raw[p] != ord('R'):
            raise ValueError('framing lost at %d' % p)
        t = chr(raw[p + 1])
        n = struct.unpack_from('<I', raw, p + 2)[0]
        yield t, raw[p + 6:p + 6 + n]
        p += 6 + n


def split_cases(raw):
    """[[(type, payload)]] per case, and whether Q was seen."""
    cases, cur, done = [], [], False
    for t, b in records(raw):
        if t == 'E':
            cases.append(cur)
            cur = []
        elif t == 'Q':
            done = True
        elif t == 'F':
            cur.append((t, b))
            cases.append(cur)
            cur = []
        else:
            cur.append((t, b))
    return cases, done


# --- what x3sim says ---------------------------------------------------------

def plan_bytes(plan):
    out = bytearray(576)
    for i, p in enumerate(plan):
        out[i] = 1 if p == 'ms' else (0 if p == 'lr' else 0x80 | p[1])
    return bytes(out)


def expected_records(data):
    """x3sim's view of every granule record, in decode order."""
    st = X.Stream(data)
    dec = X.Decoder(st, 'int', 1)
    out = []
    for h, side, md, verdict in st.frames():
        if verdict == X.F_TRUNC:
            break
        r = X.BitReader(md) if verdict == X.F_OK else None
        prev = [None, None]
        for gr in range(h.granules):
            rec = dict(ver=h.version, sri=h.sr_idx, nch=h.nch, mode=h.mode,
                       mext=h.mode_ext, ngr=h.granules, gr=gr, ms=int(h.ms),
                       rate=h.rate, ch=[None, None], plan=0, pl=None, issc=0)
            sp = None
            if verdict == X.F_OK:
                sp = dec.granule_spectra(h, side, gr, r, prev)
            for ch in range(h.nch):
                if sp is None or not sp[ch][3]:
                    continue
                gi, scf, ix, ok = sp[ch]
                nz = 576
                while nz and ix[nz - 1] == 0:
                    nz -= 1
                q = X.quarter_exponents(h, gi, scf, h.ms and h.nch == 2)
                rec['ch'][ch] = dict(bt=gi.block_type, mix=gi.mixed, ws=gi.win_switch,
                                     nz=nz, ix=list(ix[:nz]), q=[int(v) for v in q[:nz]])
            if sp is not None and h.nch == 2 and h.mode == 1 and sp[0][3] and sp[1][3]:
                if h.intensity:
                    rec['plan'] = 2
                    rec['pl'] = plan_bytes(X.stereo_plan(h, sp[1][0], sp[1][1], sp[1][2]))
                else:
                    rec['plan'] = 1 if h.ms else 0
                if not h.mpeg1 and h.intensity:
                    rec['issc'] = getattr(sp[1][0], 'is_scale', 0) or 0
            out.append(rec)
    return out, st


def parse_record(b):
    hdr = b[:G_HDR]
    rec = dict(ver=hdr[0], sri=hdr[1], nch=hdr[2], mode=hdr[3], mext=hdr[4], ngr=hdr[5],
               gr=hdr[6], plan=hdr[7], issc=hdr[8], ms=hdr[9],
               rate=struct.unpack_from('<H', hdr, 10)[0], ch=[None, None], pl=None)
    p = G_HDR
    for ch in range(2):
        ok, bt, mix, ws = b[p:p + 4]
        nz = struct.unpack_from('<H', b, p + 4)[0]
        p += 6
        if ok:
            ix = list(struct.unpack_from('<%dh' % nz, b, p))
            p += 2 * nz
            q = list(struct.unpack_from('<%dh' % nz, b, p))
            p += 2 * nz
            rec['ch'][ch] = dict(bt=bt, mix=mix, ws=ws, nz=nz, ix=ix, q=q)
    if rec['plan'] == 2:
        rec['pl'] = b[p:p + 576]
    return rec


def compare_record(i, want, got):
    for k in ('ver', 'sri', 'nch', 'mode', 'mext', 'ngr', 'gr', 'ms', 'rate', 'plan'):
        if want[k] != got[k]:
            return 'granule %d: %s %r != %r' % (i, k, got[k], want[k])
    if want['plan'] == 2 and want['issc'] != got['issc']:
        return 'granule %d: issc %r != %r' % (i, got['issc'], want['issc'])
    for ch in range(want['nch']):
        w, g = want['ch'][ch], got['ch'][ch]
        if (w is None) != (g is None):
            return 'granule %d ch %d: ok %r != %r' % (i, ch, g is not None, w is not None)
        if w is None:
            continue
        for k in ('bt', 'mix', 'ws', 'nz'):
            if w[k] != g[k]:
                return 'granule %d ch %d: %s %r != %r' % (i, ch, k, g[k], w[k])
        for k in ('ix', 'q'):
            if w[k] != g[k]:
                j = next(j for j in range(len(w[k])) if w[k][j] != g[k][j])
                return 'granule %d ch %d: %s[%d] %r != %r' % (i, ch, k, j, g[k][j], w[k][j])
    if want['plan'] == 2 and want['pl'] != got['pl']:
        j = next(j for j in range(576) if want['pl'][j] != got['pl'][j])
        return 'granule %d: plan[%d] %02x != %02x' % (i, j, got['pl'][j], want['pl'][j])
    return None


INFO = struct.Struct('<IIHBBIIII4sBBBBHHIIHHIHII')


def parse_info(b):
    (fsize, want, wantn, stage, ref, start, aend, first, audio, hdr, ver, sri,
     nch, flags, rate, kbps, frames, nbytes, delay, pad, durms, dur, skip, _scanp) = \
        INFO.unpack_from(b, 0)
    def txt(o):
        s = b[o:o + 41]
        return s[:s.index(0)].decode('latin-1') if 0 in s else s.decode('latin-1')
    return dict(ref=ref, start=start, aend=aend, first=first, audio=audio, ver=ver,
                sri=sri, nch=nch, flags=flags, rate=rate, kbps=kbps, frames=frames,
                durms=durms, dur=dur, skip=skip, title=txt(68), artist=txt(109),
                delay=delay, pad=pad)


REFNAMES = {1: X.REFUSE_FREE, 2: X.REFUSE_L1, 3: X.REFUSE_L2, 4: X.REFUSE_NOSYNC}


def check_open(name, data, info):
    st = X.Stream(data)
    if st.refused:
        got = REFNAMES.get(info['ref'])
        return None if got == st.refused else 'refusal %r != %r' % (got, st.refused)
    if info['ref']:
        return 'refused %r, x3sim opens it' % REFNAMES.get(info['ref'])
    si = st.info
    want = dict(start=st.start, aend=st.end, first=st.first, ver=si['version'],
                sri=st.h0.sr_idx, nch=si['nch'], rate=si['rate'], kbps=si['bitrate'],
                frames=si.get('frames', 0), title=si.get('title', ''),
                artist=si.get('artist', ''), skip=st.counts['skipped'])
    if 'delay' in si:
        want['delay'] = si['delay']
    if 'padding' in si:
        want['pad'] = si['padding']
    for k, v in want.items():
        if info[k] != v:
            return '%s %r != %r' % (k, info[k], v)
    exact = bool(info['flags'] & 1)
    if exact != si['exact']:
        return 'exact %r != %r' % (exact, si['exact'])
    if abs(info['durms'] - si['duration'] * 1000) > 1:
        return 'duration %d ms != %.1f' % (info['durms'], si['duration'] * 1000)
    audio = st.first + (st.h0.size if st.tagframe else 0)
    if info['audio'] != audio:
        return 'audio %d != %d' % (info['audio'], audio)
    return None


COUNTS = ('frames', X.F_SHORTRES, X.F_BADSIDE, X.F_TRUNC, X.F_CHANGE, X.F_BADGR,
          'resync', 'skipped')


def check_counts(st, b):
    vals = struct.unpack_from('<8IH', b, 0)
    for k, v in zip(COUNTS, vals):
        if st.counts[k] != v:
            return 'count %s %d != %d' % (k, v, st.counts[k])
    if st.counts['maxreach'] != vals[8]:
        return 'maxreach %d != %d' % (vals[8], st.counts['maxreach'])
    return None


# --- the runs ---------------------------------------------------------------

def decodable():
    out = []
    for rec in X.fixture_list():
        out.append(rec['name'])
    return out


def run(cases, need_std, timeout, keep):
    tmp = tempfile.mkdtemp(prefix='x3bench-', dir=BUILD)
    img = os.path.join(tmp, 'x3bench.img')
    out = os.path.join(tmp, 'x3bench.out')
    make_image(cases, need_std, img)
    secs = run_qemu(img, out, timeout)
    raw = open(out, 'rb').read() if os.path.exists(out) else b''
    if not keep:
        for f in (img, out):
            if os.path.exists(f):
                os.unlink(f)
        os.rmdir(tmp)
    return raw, secs


def front_pass(names, timeout, keep):
    # small rings and chunks, so MORE, the wrap and a scan across a refill run
    cases = [(n, 1, 0, 0, 12, 700) for n in names]
    raw, secs = run(cases, False, timeout, keep)
    got, done = split_cases(raw)
    bad = 0
    wm = 0
    for (name, *_), recs in zip(cases, got):
        data = open(os.path.join(FIXDIR, name), 'rb').read()
        msg = None
        info = None
        grans = []
        cnt = None
        for t, b in recs:
            if t == 'I':
                info = parse_info(b)
            elif t == 'G':
                grans.append(parse_record(b))
            elif t == 'C':
                cnt = b
            elif t == 'W':
                wm = max(wm, struct.unpack('<H', b)[0])
            elif t == 'F':
                msg = 'harness: ' + b.decode('latin-1')
        if msg is None and info is None:
            msg = 'no open record'
        if msg is None:
            msg = check_open(name, data, info)
        if msg is None and not info['ref']:
            want, st = expected_records(data)
            if len(want) != len(grans):
                msg = '%d granules, x3sim %d' % (len(grans), len(want))
            for i, (w, g) in enumerate(zip(want, grans)):
                m = compare_record(i, w, g)
                if m:
                    msg = m
                    break
            if msg is None and cnt is not None:
                msg = check_counts(st, cnt)
        print('%-13s front %s' % (name, msg or 'ok'))
        bad += msg is not None
    if len(got) < len(cases):
        print('x3bench: only %d of %d cases reported (QEMU %.1f s)' % (len(got), len(cases), secs))
        bad += len(cases) - len(got)
    print('x3bench front: %d case(s), %d failure(s), stack low-water %d bytes, QEMU %.1f s' % (
        len(cases), bad, wm, secs))
    return bad


def pcm_pass(only, timeout, keep):
    gold = X.read_golden()
    cases = []
    for (name, div, mono), line in gold.items():
        if only and name not in only:
            continue
        cases.append((name, div, 1 if mono else 0, 1, 13, 1500))
    raw, secs = run(cases, True, timeout, keep)
    got, done = split_cases(raw)
    bad = 0
    wm = 0
    for (name, div, mono, *_), recs in zip(cases, got):
        line = gold[(name, div, bool(mono))]
        w = line.split()
        want_n, want_ch, want_hash = int(w[3]), int(w[4][1:]), w[5]
        want_clip, want_sat = int(w[7]), int(w[9])
        pcm = b''.join(b for t, b in recs if t == 'P')
        fail = [b for t, b in recs if t == 'F']
        z = [b for t, b in recs if t == 'Z']
        for t, b in recs:
            if t == 'W':
                wm = max(wm, struct.unpack('<H', b)[0])
        h = hashlib.sha256(pcm).hexdigest()
        msg = None
        if fail:
            msg = 'harness: ' + fail[0].decode('latin-1')
        elif h != want_hash:
            n = len(pcm) // (2 * want_ch)
            msg = 'PCM differs (%d samples, x3sim %d)' % (n, want_n)
            ref = X.decode_file(os.path.join(FIXDIR, name), 'int', div, bool(mono))[0]
            a = np.frombuffer(pcm[:len(pcm) - len(pcm) % (2 * want_ch)], '<i2').reshape(-1, want_ch)
            m = min(len(a), len(ref))
            d = np.nonzero((a[:m] != ref[:m]).any(axis=1))[0]
            if len(d):
                k = int(d[0])
                msg += '; first at sample %d (granule %d, slot %d): %s != %s' % (
                    k, k // (576 // div), (k % (576 // div)) // (32 // div),
                    list(a[k]), list(ref[k]))
        elif z:
            clip, sat = struct.unpack_from('<4xII', z[0], 0)
            if clip != want_clip or sat != want_sat:
                msg = 'clip %d sat %d, x3sim %d %d' % (clip, sat, want_clip, want_sat)
        print('%-13s d%d %-6s %s' % (name, div, 'mono' if mono else 'native', msg or 'exact'))
        bad += msg is not None
    if len(got) < len(cases):
        print('x3bench: only %d of %d cases reported (QEMU %.1f s)' % (len(got), len(cases), secs))
        bad += len(cases) - len(got)
    print('x3bench pcm: %d case(s), %d not exact, stack low-water %d bytes, QEMU %.1f s' % (
        len(cases), bad, wm, secs))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--front', action='store_true')
    ap.add_argument('--pcm', action='store_true')
    ap.add_argument('--only', nargs='*', default=())
    ap.add_argument('--keep', action='store_true', help='keep the image and the output')
    ap.add_argument('--timeout', type=int, default=900)
    ap.add_argument('--nobuild', action='store_true')
    a = ap.parse_args()
    both = not (a.front or a.pcm)
    if not a.nobuild:
        build(a.pcm or both)
    bad = 0
    if a.front or both:
        names = [n for n in decodable() if not a.only or n in a.only]
        bad += front_pass(names, a.timeout, a.keep)
    if a.pcm or both:
        bad += pcm_pass(a.only, a.timeout, a.keep)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
