#!/usr/bin/env python3
"""Derive WireFleet's three committed PCM clips from their CC0 originals.

SPEC.md 114.11. HOST-ONLY and run once: the build never reads the originals
(apps/wirefleet/sfx/CREDITS.md names each one, its URL and its SHA-256),
only the 8 kHz unsigned 8-bit mono WAVs this writes beside it, which
tools/wirefleet_sfx.py packs into the lazy part WFLET.SFX. Standard library
only. The originals are OGG Vorbis; decode each to 16-bit PCM WAV first
(macOS: `afconvert -f WAVE -d LEI16 in.ogg out.wav`; elsewhere `ffmpeg -i
in.ogg out.wav`) and pass the WAVs:

    tools/wirefleet_sfxderive.py --launch thrusterFire_000.wav \\
        --boom lowFrequency_explosion_001.wav \\
        --crunch explosionCrunch_000.wav --splash cannon_miss.wav
"""
import argparse
import math
import struct
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'apps/wirefleet/sfx'
RATE = 8000
COUNT = 4096                    # 0.512 s: one 4KB ring each (SPEC.md 34.5.3)


def load(path):
    """Mono floats in -1..1 and the source rate."""
    with wave.open(str(path)) as w:
        if w.getsampwidth() != 2:
            raise ValueError('%s: want 16-bit PCM' % path)
        ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        raw = struct.unpack('<%dh' % (n * ch), w.readframes(n))
    return [sum(raw[i:i + ch]) / (ch * 32768.0) for i in range(0, len(raw), ch)], rate


def resample(x, rate, seconds):
    """Windowed-sinc low-pass at 3.6 kHz, sampled at RATE: what an SB's 8 kHz
    can hold without folding the source's top octave back down as hiss."""
    cut, taps = 3600.0 / rate, 48
    out = []
    for k in range(int(seconds * RATE)):
        t = k * rate / RATE
        c = int(t)
        acc = norm = 0.0
        for j in range(c - taps, c + taps + 1):
            if 0 <= j < len(x):
                d = j - t
                s = 2 * cut * (math.sin(2 * math.pi * cut * d) / (2 * math.pi * cut * d) if d else 1.0)
                win = 0.5 + 0.5 * math.cos(math.pi * d / (taps + 1))
                acc += x[j] * s * win
                norm += s * win
        out.append(acc / norm if norm else 0.0)
    return out


def shape(x, attack, release):
    """Fade in over `attack` samples and out over the last `release`, peak
    normalised to 0.95, then a gentle tanh so an 8-bit sample is not mostly
    quiet; first and last sample land on the 128 midpoint."""
    n = len(x)
    y = []
    for i, v in enumerate(x):
        g = min(1.0, i / attack) if attack else 1.0
        if i >= n - release:
            g *= ((n - 1 - i) / release) ** 1.5
        y.append(v * g)
    peak = max(abs(v) for v in y) or 1.0
    y = [math.tanh(1.6 * v / peak) / math.tanh(1.6) * 0.95 for v in y]
    data = bytes(max(1, min(255, 128 + round(v * 127))) for v in y)
    return b'\x80' + data[1:-1] + b'\x80'


def write(name, data):
    assert len(data) == COUNT
    with wave.open(str(OUT / name), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(RATE)
        w.writeframes(data)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    for k in ('launch', 'boom', 'crunch', 'splash'):
        p.add_argument('--' + k, type=Path, required=True)
    a = p.parse_args()
    sec = COUNT / RATE
    x, r = load(a.launch)
    write('launch.wav', shape(resample(x, r, sec), 160, 1800))
    boom, r1 = load(a.boom)
    crunch, r2 = load(a.crunch)
    boom, crunch = resample(boom, r1, sec), resample(crunch, r2, sec)
    write('explode.wav', shape([0.65 * b + 0.45 * c for b, c in zip(boom, crunch)], 8, 1400))
    x, r = load(a.splash)
    write('splash.wav', shape(resample(x, r, sec), 8, 1600))
    print('wirefleet_sfxderive: three %d-sample clips at %d Hz' % (COUNT, RATE))


if __name__ == '__main__':
    main()
