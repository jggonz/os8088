#!/usr/bin/env python3
"""Read graphics tables from a user's 1942 NROM cartridge (never downloaded).

Addresses are CPU addresses in the 32KB PRG. The payload digest pins the
layout independently of iNES / NES 2.0 header differences. See SPEC 99.2.
No emulator, Pillow, or third-party Python dependency is required.
"""
import hashlib
from pathlib import Path
import struct

PAYLOAD_SHA256 = 'e868400409c70876b98dad2cca87b8e9ee31877b0cccbbd8405be5c54922722a'
# RGB approximation of the NES colors used by the cartridge's $A770 palette.
RGB = {0x00:(84,84,84),0x02:(8,16,144),0x0a:(0,80,0),0x0f:(0,0,0),
       0x10:(152,150,152),0x12:(48,50,236),0x16:(152,34,32),
       0x19:(8,124,0),0x1a:(0,118,40),0x20:(236,238,236),
       0x26:(236,106,100),0x28:(160,170,0),0x29:(116,196,0),
       0x38:(204,210,120)}

class Cartridge:
    def __init__(self, path):
        data=Path(path).read_bytes()
        if (len(data)!=40976 or data[:6]!=b'NES\x1a\x02\x01'
                or data[6]&0xf4 or data[7]&0xf0
                or hashlib.sha256(data[16:]).hexdigest()!=PAYLOAD_SHA256):
            raise ValueError('unsupported 1942 cartridge: expected the pinned 32KB PRG / 8KB CHR NROM payload')
        self.prg=data[16:32784];self.chr=data[32784:]
    def read(self, address):return self.prg[address-0x8000]
    def word(self, address):return self.read(address)|self.read(address+1)<<8
    def tile(self, number):
        p=self.chr[number*16:number*16+16]
        return bytes(((p[y]>>(7-x))&1)|(((p[y+8]>>(7-x))&1)<<1) for y in range(8) for x in range(8))
    def sprite(self, name, frame, palette, box=None):
        # $C45A: count, coordinate-layout index, then (CHR tile, flip) pairs.
        a=self.word(0xc565+frame*2);n=self.read(a)
        coords=self.word(0xce0e+self.read(a+1)*2)
        attrs=self.word(0xd010+palette*2)
        signed=lambda x:x if x<128 else x-256
        parts=[(signed(self.read(coords+i*2)),signed(self.read(coords+i*2+1)),
                self.read(a+2+i*2),self.read(a+3+i*2),self.read(attrs+i)) for i in range(n)]
        left=min(p[0] for p in parts);top=min(p[1] for p in parts)
        w=max(p[0] for p in parts)+8-left;h=max(p[1] for p in parts)+8-top
        if box:
            left-=(box[0]-w)//2;top-=(box[1]-h)//2;w,h=box
        pixels=bytearray(w*h)
        for x,y,t,flip,attr in parts:
            if t==255:continue
            tile=self.tile(t)
            for py in range(8):
                for px in range(8):
                    v=tile[(7-py if flip&2 else py)*8+(7-px if flip&1 else px)]
                    if v:pixels[(y-top+py)*w+x-left+px]=17+(attr&3)*4+v
        return dict(name=name,w=w,h=h,pixels=pixels.hex(),nes_frame=frame)
    def world(self):
        # $81EE: route -> page at $854B + page*240 (16x15 metatiles).
        # $824F: metatile -> four CHR indices; $82E7 supplies its palette.
        route=bytes(self.read(0x844b+i) for i in range(256))
        maps=bytes(self.read(0x854b+i) for i in range(23*240))
        tiles=[];index={};meta=bytearray()
        for m in range(256):
            pal=self.read(0x9edb+m)
            for q in range(4):
                raw=self.tile(256+self.read(0x9adb+m*4+q))
                pixels=bytes(1+pal*4+v if v else 1 for v in raw)
                if pixels not in index:index[pixels]=len(tiles);tiles.append(pixels)
                meta.append(index[pixels])
        assert max(route)<23 and len(tiles)<=256
        data=b'N42W'+bytes(4)+route+maps+meta+b''.join(tiles)
        data=bytearray(data);struct.pack_into('<H',data,4,len(data))
        assert len(data)<=32768
        return data

def import_art(path, sprites, pal):
    cart=Cartridge(path)
    pal=dict(pal)
    # Index 0 stays transparent; the four background and four sprite palettes
    # retain separate slots even where their RGB colors happen to agree.
    pal['rgb']=[[0,0,0]]+[list(RGB[cart.read(0xa770+i)]) for i in range(32)]
    pal['rgb'][4]=[236,238,236]  # original embedded HUD/effects ink
    pal['cga_map']=[0,0,1,3,3,0,3,1,2,0,1,2,0,0,3,1,0,
                    0,2,3,1,0,3,3,2,0,2,1,3,0,2,3,1]
    definitions={'player':(0x00,0,(24,24)), 'bankleft':(0x00,0,(24,24)),
        'bankright':(0x00,0,(24,24)), 'roll':(0x01,0,(24,24)),
        'enemy':(0x28,0,None),'elite':(0x28,1,None),'blue':(0x44,2,None),
        'boss':(0x72,3,None),'blast0':(0x90,3,(24,24)),
        'blast1':(0x91,3,(24,24)),'blast2':(0x92,3,(24,24)),
        'blast3':(0x93,3,(24,24)), 'scout':(0x40,2,None),
        'diver':(0x60,0,None),'bomber':(0x58,3,None),'heavy':(0x68,3,None),
        'enemybank':(0x29,0,None)}
    out=[]
    for s in sprites:
        if s['name'] in definitions:
            frame,group,box=definitions[s['name']];s=cart.sprite(s['name'],frame,group,box)
        elif not s['name'].startswith('font'):
            # Original projectiles/pickups are small code-authored effects.
            s=dict(s);s['pixels']=bytes(min(v,7) for v in bytes.fromhex(s['pixels'])).hex()
        out.append(s)
    return out,pal,cart.world()
