#!/usr/bin/env python3
"""Decode the optional-bank badge records independently of the sprite packer."""
from pathlib import Path
import struct
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import os88btscenes as scenes
def main():
    raw=scenes.build_scene_bank()
    offsets=struct.unpack_from('<14H',raw)
    assert offsets[0]>=44 and list(offsets)==sorted(set(offsets))
    decoded=[]
    for off in offsets[4:]:
        at=off-16
        assert raw[at:at+2]==bytes((16,16)) and at+130<=len(raw)
        pixels=[p for byte in raw[at+2:at+130] for p in (byte>>4,byte&15)]
        assert sum(p!=13 for p in pixels)>=32,'invisible badge'
        decoded.append(pixels)
    for i in range(5):
        color,gray=decoded[i],decoded[i+5]
        assert [p==13 for p in color]==[p==13 for p in gray],'gray changed transparency'
        assert set(gray)<={7,8,13,15} and len(set(color)-{13})>=2
    assert len(raw)+16<=65000
    print(f'boratype_badges:10visible16px records, palette/transparency/directory bounds pass ({len(raw)}bytes)')
if __name__=='__main__':main()
