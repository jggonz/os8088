#!/usr/bin/env python3
"""Pack WireFleet's credited CC0 clips into its lazy part WFLET.SFX.

SPEC.md 114.11: a 16-byte header ('WFLS', the bank's length, a byte sum,
the clip count and length, the rate) and three 4,096-sample 8 kHz unsigned
8-bit clips - launch, explosion, splash - in the order the package's
WF_PCM_* numbers them. Standard library only.
"""
import argparse
import hashlib
import json
import struct
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ORDER = ('launch.wav', 'explode.wav', 'splash.wav')
COUNT = 4096


def bank():
    base = ROOT / 'apps/wirefleet/sfx'
    rows = {r['file']: r for r in json.loads((base / 'samples.json').read_text())}
    data = bytearray()
    for name in ORDER:
        path = base / name
        if hashlib.sha256(path.read_bytes()).hexdigest() != rows[name]['sha256']:
            raise ValueError('sample digest mismatch: %s' % path)
        with wave.open(str(path)) as w:
            if (w.getnchannels(), w.getsampwidth(), w.getframerate(),
                    w.getnframes()) != (1, 1, 8000, COUNT):
                raise ValueError('sample format mismatch: %s' % path)
            raw = w.readframes(COUNT)
        if raw[0] != 128 or raw[-1] != 128 or max(raw) - min(raw) < 100:
            raise ValueError('bad sample envelope: %s' % path)
        data += raw
    size = 16 + len(data)
    return struct.pack('<4s5HH', b'WFLS', size, sum(data) & 0xffff, len(ORDER),
                       COUNT, 8000, 0) + data


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('-o', type=Path, required=True)
    args = ap.parse_args()
    args.o.parent.mkdir(parents=True, exist_ok=True)
    args.o.write_bytes(bank())
