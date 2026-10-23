#!/usr/bin/env python3
"""x3xtbench: the XT backend part on a real x86 against tools/x3simxt.py
(SPEC.md 121.4; the x3xtexact soak row).

    python3 tests/x3xtbench/x3xtbench.py [--only NAME..] [--profile P..] [--keep]

tests/x3bench's mechanism (the STANDARD track's), with the XT part in the
STANDARD part's place: a raw hard-disk image - tests/x3bench/x3bboot.asm,
tests/x3xtbench/x3xtbench.asm (which %includes the SHIPPING
apps/xtmp3/x3pars.inc), build/x3front.bin and build/x3xt.bin as `make` built
them, and the fixtures - booted in QEMU. Every case of tests/xtmp3/X3XT.txt
runs: the fixture opened and staged by the shipping front end, every granule
record through the XT part's GRANULE (U8 mono, as the engine asks for it), and
the PCM's SHA-256, its length and the part's clip count (STATS) compared with
the golden line. A case that differs is reported with its first differing
sample, decoded by x3simxt for the comparison.

QEMU is exact about the work, which is what bit-exactness needs; MartyPC is
for the cycles (tools/x3xtmarty.py). Exit 0 only when every case matched.
"""
import argparse
import hashlib
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tests', 'x3bench'))
import numpy as np       # noqa: E402
import x3bench as B      # noqa: E402  (the STANDARD track's helpers: QEMU, framing)
import x3simxt as SX     # noqa: E402

BUILD = B.BUILD
FIXDIR = B.FIXDIR
DIR_SECTORS = 16
PNUM = {'std': 0, 'fast': 1, 'turbo': 2, 'speech': 3}


def build():
    """The parts are make's (the row declares them as `wants` and passes
    --nobuild instead)."""
    B.sh(['make', '-s', os.path.relpath(os.path.join(BUILD, 'x3front.bin'), ROOT),
          os.path.relpath(os.path.join(BUILD, 'x3xt.bin'), ROOT)])


def harness(tmp):
    """The boot sector and the harness, assembled into the run's own
    directory (nothing of build/ is written)."""
    B.sh(B.NASM + ['-o', os.path.join(tmp, 'x3bboot.bin'), 'tests/x3bench/x3bboot.asm'])
    B.sh(B.NASM + ['-o', os.path.join(tmp, 'x3xtbench.bin'), 'tests/x3xtbench/x3xtbench.asm'])


def make_image(cases, path, tmp):
    """cases: [(name, profile, div, chunk)]"""
    boot = open(os.path.join(tmp, 'x3bboot.bin'), 'rb').read()
    harness = open(os.path.join(tmp, 'x3xtbench.bin'), 'rb').read()
    front = open(os.path.join(BUILD, 'x3front.bin'), 'rb').read()
    xt = open(os.path.join(BUILD, 'x3xt.bin'), 'rb').read()
    # the harness's memory map (x3xtbench.asm): FRONT at 1800:0 below the XT
    # part at 1E00:0, which has 64 KB before the stack
    assert len(front) <= 0x6000 and len(xt) <= 0x10000, (len(front), len(xt))
    lba = 1 + DIR_SECTORS
    body = B.pad(harness)
    lba += B.sectors(harness)
    front_lba = lba
    body += B.pad(front)
    lba += B.sectors(front)
    xt_lba = lba
    body += B.pad(xt)
    lba += B.sectors(xt)
    files = {}
    for c in cases:
        if c[0] not in files:
            data = open(os.path.join(FIXDIR, c[0]), 'rb').read()
            files[c[0]] = (lba, len(data))
            body += B.pad(data)
            lba += B.sectors(data)
    d = b'X3XB' + struct.pack('<HHHHHH', front_lba, B.sectors(front), xt_lba,
                              B.sectors(xt), len(cases), B.sectors(harness))
    for name, prof, div, chunk in cases:
        flba, fsz = files[name]
        d += struct.pack('<IIBBH12s8x', flba, fsz, PNUM[prof], div, chunk, name.encode())
    assert len(d) <= DIR_SECTORS * 512, 'too many cases for the directory'
    d += b'\0' * (DIR_SECTORS * 512 - len(d))
    img = boot + d + body
    img += b'\0' * ((-len(img)) % (1024 * 1024))
    with open(path, 'wb') as f:
        f.write(img)


def run(only, profiles, timeout, keep):
    gold = SX.read_golden()
    cases = []
    for (name, prof), line in gold.items():
        if only and name not in only:
            continue
        if profiles and prof not in profiles:
            continue
        div = int(line.split()[2][1:])
        cases.append((name, prof, div, 1500))
    tmp = tempfile.mkdtemp(prefix='x3xtbench-', dir=BUILD)
    img = os.path.join(tmp, 'x3xtbench.img')
    out = os.path.join(tmp, 'x3xtbench.out')
    harness(tmp)
    make_image(cases, img, tmp)
    secs = B.run_qemu(img, out, timeout)
    raw = open(out, 'rb').read() if os.path.exists(out) else b''
    if not keep:
        for f in os.listdir(tmp):
            os.unlink(os.path.join(tmp, f))
        os.rmdir(tmp)
    got, done = B.split_cases(raw)
    bad = 0
    wm = 0
    for (name, prof, div, _), recs in zip(cases, got):
        w = gold[(name, prof)].split()
        want_n, want_hash, want_clip = int(w[3]), w[4], int(w[6])
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
        elif h != want_hash or len(pcm) != want_n:
            msg = 'PCM differs (%d samples, the model %d)' % (len(pcm), want_n)
            ref, _, _ = SX.decode(os.path.join(FIXDIR, name), prof)
            a = np.frombuffer(pcm, 'u1')
            m = min(len(a), len(ref))
            d = np.nonzero(a[:m] != ref[:m])[0]
            if len(d):
                k = int(d[0])
                n = 576 // div
                msg += '; first at sample %d (granule %d): %d != %d' % (k, k // n, a[k], ref[k])
        elif z:
            clip = struct.unpack_from('<4xI', z[0], 0)[0]
            if clip != want_clip:
                msg = 'clip %d, the model %d' % (clip, want_clip)
        print('%-13s %-6s d%d %s' % (name, prof, div, msg or 'exact'))
        bad += msg is not None
    if len(got) < len(cases):
        print('x3xtbench: only %d of %d cases reported (QEMU %.1f s)' % (len(got), len(cases), secs))
        bad += len(cases) - len(got)
    print('x3xtbench: %d case(s), %d not exact, stack low-water %d bytes, QEMU %.1f s' % (
        len(cases), bad, wm, secs))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--only', nargs='*', default=())
    ap.add_argument('--profile', nargs='*', default=())
    ap.add_argument('--keep', action='store_true')
    ap.add_argument('--timeout', type=int, default=900)
    ap.add_argument('--nobuild', action='store_true')
    a = ap.parse_args()
    if not a.nobuild:
        build()
    return 1 if run(a.only, a.profile, a.timeout, a.keep) else 0


if __name__ == '__main__':
    sys.exit(main())
