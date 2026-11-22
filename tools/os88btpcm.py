#!/usr/bin/env python3
"""Convert the named local Boratype MP3s into Sound Blaster music and reactions."""
import argparse
from pathlib import Path
import struct
import subprocess
import wave

import numpy as np

RATE = 8000
MUSIC = "borat-sounds-sound"
# Order is the native mixer's 1-based clip ID.
EFFECTS = ("hello-nice-to-meet-you", "what", "naughty-naughty",
           "how-many-fingers-do-you-have", "nice", "is-nice", "high-five",
           "ohhh-la-la", "i-like-you-do-you-like-me", "hahahaha",
           "can-i-touch-it-please", "i-go-to-america", "bing-bong-song-loop")


def convert(source, output, title, music):
    raw = subprocess.check_output(["ffmpeg", "-nostdin", "-v", "error", "-i",
                                   str(source / (title + ".mp3")), "-vn", "-ac", "1",
                                   "-ar", str(RATE), "-f", "s16le", "-"])
    samples = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768
    if not music:
        active = np.flatnonzero(abs(samples) > 0.006)
        if not len(active):
            raise ValueError(f"{title}: silent source")
        samples = samples[max(0, active[0]-80):active[-1]+81].copy()
        if title == "bing-bong-song-loop" and len(samples) > RATE*3:
            samples = samples[:RATE*3]
            samples[-320:] *= np.linspace(1, 0, 320)
    if not len(samples):
        raise ValueError(f"{title}: empty source")
    # Reserve independent headroom. Only the music uses the quarter-gain law.
    level = 32 if music else 95 / max(0.01, float(max(abs(samples))))
    signed = np.clip(np.rint(samples * level), -128, 127).astype(np.int16)
    pcm = (signed + 128).astype('uint8')
    with wave.open(str(output / (title + ".wav")), "wb") as wav:
        wav.setparams((1, 1, RATE, 0, "NONE", "not compressed"))
        wav.writeframes(pcm.tobytes())
    return np.pad(signed, (0, (-len(signed)) % 4)).reshape(-1, 4)


def nearest(samples, centers):
    # Squared Euclidean distance, in bounded host-memory batches.
    norms = (centers * centers).sum(axis=1)
    return np.concatenate([(norms - 2 * part @ centers.T).argmin(axis=1)
                           for part in np.array_split(samples, max(1, len(samples)//1024))])


def bank(tracks, music):
    data = np.concatenate(tracks).astype(np.float32)
    rng = np.random.default_rng(8088)
    training = data[rng.choice(len(data), min(16000, len(data)), replace=False)]
    centers = training[rng.choice(len(training), 256)].copy()
    for _ in range(16):
        ids = nearest(training, centers)
        counts = np.bincount(ids, minlength=256)
        for i in range(4):
            centers[:, i] = np.divide(np.bincount(ids, training[:, i], minlength=256),
                                      counts, out=centers[:, i].copy(), where=counts != 0)
    centers = np.rint(centers)
    centers[0] = 0
    ids = nearest(data, centers).astype('uint8')
    # Header and up to 13 (offset, vector-count) rows precede the dictionary.
    header = bytearray(64)
    total = 1088 + len(ids)
    if total > 65024:
        raise ValueError(f"sample bank exceeds its segment ({total} bytes)")
    struct.pack_into("<4sHHHH",header,0,b"BTP1",RATE,len(tracks),total,int(music))
    pos = 1088
    for i, track in enumerate(tracks):
        struct.pack_into("<HH",header,12+4*i,pos,len(track))
        pos += len(track)
    table = (centers.astype(np.int16) + (128 if music else 0)).astype('uint8')
    return bytes(header) + table.tobytes() + ids.tobytes()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    music = bank([convert(args.source,args.output,MUSIC,True)],True)
    fx = bank([convert(args.source,args.output,t,False) for t in EFFECTS],False)
    (args.output/'BTMUSIC.BTA').write_bytes(music)
    (args.output/'BTCLIPS.BTA').write_bytes(fx)
    (args.output/'BTAUDIO.IDX').write_bytes(struct.pack('<4sHH',b'BTP1',len(music),len(fx)))
    print(f"8 kHz mono: music {len(music)} bytes, 13 effects {len(fx)} bytes")

if __name__ == '__main__':
    main()
