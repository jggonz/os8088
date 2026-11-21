#!/usr/bin/env python3
"""Independent pixel reader for whole-frame and legacy fallback BTAN records."""
from pathlib import Path
import struct
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import os88btanim as pack
import os88lz as lz

def pixels(raw,w,h,planes):
    stride=w//8;plane=stride*h
    out=[]
    for y in range(h):
        for x in range(w):
            value=sum(((raw[p*plane+y*stride+x//8]>>(7-x%8))&1)<<p for p in range(planes))
            out.append(15 if planes==1 and value else value)
    return out

def validate(banks,frames):
    assert len(banks)==5
    for b in banks:
        assert b[:4]==b'BTAN' and struct.unpack_from('<H',b,4)[0]==1
        assert struct.unpack_from('<H',b,6)[0]==len(b)<=65000,'bank length header'
    assert struct.unpack_from('<HH',banks[0],8)==(24,270)
    assert len(banks[0])>2176 and len(banks[2])>16
    for part in (4,6,7):assert len(banks[part-3])==57616
    signatures={}
    for frame in range(30):
        part=(4,6,7)[frame//10];base=16+(frame%10)*5760
        raw=banks[part-3][base:base+5760]
        assert pixels(raw,96,120,4)==frames[frame*3][3],'whole-frame pixels/crop changed'
        for slot in range(9):
            variant=0 if slot<5 else 1 if slot<7 else 2
            strip=slot if variant==0 else slot-(5 if variant==1 else 7)
            w=96 if variant==0 else 40;planes=1 if variant==2 else 4
            expected=frames[frame*3+variant][3][strip*24*w:(strip+1)*24*w]
            src,off,n,sig=struct.unpack_from('<4H',banks[0],16+(frame*9+slot)*8)
            key=(w,planes,bytes(expected))
            if key not in signatures:signatures[key]=len(signatures)+1
            assert sig==signatures[key],'legacy strip signature changed'
            if variant==0:
                assert (src,off,n)==(part,base+strip*288,0),'raw directory mapping'
                chunk=b''.join(banks[src-3][off+p*1440:off+p*1440+288] for p in range(4))
            else:
                assert src==3 and off>=2176 and 0<n<=1154 and off+n<=len(banks[0])
                chunk=lz.lz4_decompress(banks[0][off:off+n],w//8*24*planes)
            assert pixels(chunk,w,24,planes)==expected,'directory strip pixels changed'

def main():
    with tempfile.TemporaryDirectory(prefix='boratype-animpack-') as tmp:
        paths=[Path(tmp)/f'BTAN{i}.BIN' for i in range(5)]
        pack.build(*paths);banks=[p.read_bytes() for p in paths];frames=pack.read()
        validate(banks,frames)
        broken=[bytearray(b) for b in banks];broken[1][6]^=1
        try:validate(broken,frames)
        except AssertionError:pass
        else:raise AssertionError('wrong raw-bank length accepted')
        broken=[bytearray(b) for b in banks];broken[0][18]^=1
        try:validate(broken,frames)
        except AssertionError:pass
        else:raise AssertionError('misaligned raw picture address accepted')
    print('boratype_animpack:30whole pictures/270strips/signatures pass; bad length/address rejected')
if __name__=='__main__':main()
