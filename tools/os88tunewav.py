#!/usr/bin/env python3
"""os88tunewav - a short tune as a WAV the Audio player takes (SPEC.md 86.6)

Writes the opening of Fur Elise as mono 8-bit unsigned PCM at 8,000 Hz: the
rate an 8088 plays 1:1, straight out of the look-ahead ring with no resample
(SPEC.md 86.21), so on vm/xt-covox it is the cleanest thing Audio can sound
through the Covox (SPEC.md 34.14). It rides `make xt-covox`'s listening disk.

INTEGER ARITHMETIC ONLY, so the bytes are the same on every host - the
toolchain is deterministic on purpose (CLAUDE.md), and libm's sin() is not
promised to round alike everywhere. The tone is a phase accumulator into a
committed 64-entry sine table (fundamental plus two harmonics), under a decay
that is a shift-and-subtract per sample.

    python3 tools/os88tunewav.py -o build/FURELISE.WAV
    python3 tools/os88tunewav.py --selfcheck
"""
import argparse
import struct
import sys

RATE = 8000
# sin(2*pi*i/64) * 127, rounded - the table, not a call to sin()
SINE = [0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126,
        127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12]
SINE = SINE + [-v for v in SINE]

# (semitones from A4, beats): the first sixteen bars' melody, twice
PHRASE = [(7, 1), (6, 1), (7, 1), (6, 1), (7, 1), (2, 1), (5, 1), (3, 1),
          (0, 3), (-9, 1), (-5, 1), (0, 1), (2, 3), (-5, 1), (-1, 1), (2, 1),
          (3, 3), (-5, 1), (7, 1), (6, 1), (7, 1), (6, 1), (7, 1), (2, 1),
          (5, 1), (3, 1), (0, 3), (-9, 1), (-5, 1), (0, 1), (2, 3), (-5, 1),
          (3, 1), (2, 1), (0, 4)]
BEAT = RATE * 19 // 100                         # 0.19 s
# A4 = 440 Hz as a 16.16 phase step through a 64-entry table, then one
# semitone at a time by 2^(1/12) in 16.16 (69433 / 65536 = 1.0594631)
A4STEP = 440 * 64 * 65536 // RATE
SEMI = 69433


def step(semis):
    s = A4STEP
    for _ in range(abs(semis)):
        s = s * SEMI // 65536 if semis > 0 else s * 65536 // SEMI
    return s


def render():
    out = bytearray()
    for semis, beats in PHRASE:
        st = step(semis)
        ph = 0
        env = 32767
        for k in range(BEAT * beats):
            i = ph >> 16
            v = (SINE[i & 63] * 5 + SINE[(2 * i) & 63] * 2
                 + SINE[(3 * i) & 63]) // 8          # |v| <= 127
            a = min(k, 40) * 32767 // 40            # a 5 ms attack
            s = v * (env * a // 32767) // 32767
            out.append(128 + s * 110 // 127)
            ph = (ph + st) & 0x3FFFFF
            env -= env >> 9                         # ~ e^(-t / 64 ms)
    out += bytes([128]) * (RATE // 2)
    return bytes(out)


def wav(pcm):
    fmt = struct.pack('<HHIIHH', 1, 1, RATE, RATE, 1, 8)
    body = (b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt
            + b'data' + struct.pack('<I', len(pcm)) + pcm
            + (b'\0' if len(pcm) & 1 else b''))
    return b'RIFF' + struct.pack('<I', len(body)) + body


def selfcheck():
    a = wav(render())
    assert a == wav(render()), 'not deterministic'
    assert a[:4] == b'RIFF' and a[8:12] == b'WAVE'
    tag, ch, rate, _, align, bits = struct.unpack('<HHIIHH', a[20:36])
    assert (tag, ch, rate, align, bits) == (1, 1, 8000, 1, 8)
    pcm = a[44:44 + struct.unpack('<I', a[40:44])[0]]
    assert min(pcm) > 0 and max(pcm) < 255, 'clipped'
    assert max(pcm) - min(pcm) > 150, 'too quiet'
    assert abs(step(12) - 2 * A4STEP) * 1000 < 2 * A4STEP, 'octave'
    print(f'os88tunewav: selfcheck ok ({len(a)} bytes, '
          f'{len(pcm) / RATE:.1f} s at {RATE} Hz)')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('-o', '--out')
    ap.add_argument('--selfcheck', action='store_true')
    a = ap.parse_args()
    if a.selfcheck:
        selfcheck()
    if a.out:
        with open(a.out, 'wb') as f:
            f.write(wav(render()))
    elif not a.selfcheck:
        ap.error('-o or --selfcheck')
    return 0


if __name__ == '__main__':
    sys.exit(main())
