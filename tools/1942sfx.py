#!/usr/bin/env python3
"""Pack the credited CC0 WAV derivatives into the optional native PCM bank."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import wave

ROOT=Path(__file__).resolve().parents[1]


def bank():
    base=ROOT/'apps/1942/sfx'
    records=json.loads((base/'samples.json').read_text())
    data=bytearray()
    for row,count in zip(records,(1024,3072,4096)):
        path=base/row['file']
        if hashlib.sha256(path.read_bytes()).hexdigest()!=row['sha256']:
            raise ValueError('sample digest mismatch: '+str(path))
        with wave.open(str(path)) as w:
            if (w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getnframes())!=(1,1,8000,count):
                raise ValueError('sample format mismatch: '+str(path))
            raw=w.readframes(count)
        if len(raw)!=count or raw[0]!=128 or raw[-1]!=128 or max(raw)-min(raw)<100:
            raise ValueError('bad sample envelope: '+str(path))
        data.extend(raw)
    if len(records)!=3 or len(data)!=8192:raise ValueError('sample count mismatch')
    return struct.pack('<4s6H',b'N42S',8208,sum(data)&65535,1024,3072,4096,8000)+data


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('-o',type=Path,required=True)
    args=ap.parse_args();args.o.parent.mkdir(parents=True,exist_ok=True)
    args.o.write_bytes(bank())
