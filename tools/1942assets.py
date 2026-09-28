#!/usr/bin/env python3
"""Compile committed original indexed art to adapter-native 8088 sprite banks."""
import argparse
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
ART=ROOT/'apps/1942/art'
def word(n):return struct.pack('<H',n)
def checksum(data):return sum(data)&65535

def validate(pal,sprites):
    rgb=pal['rgb']
    if not 8<=len(rgb)<=256 or rgb[0]!=[0,0,0]:raise ValueError('palette requires black index 0 and 8..256 entries')
    if any(len(c)!=3 or any(type(v)is not int or not 0<=v<256 for v in c) for c in rgb):raise ValueError('invalid RGB')
    if len(pal['cga_map'])!=len(rgb) or any(type(v)is not int or not 0<=v<4 for v in pal['cga_map']):raise ValueError('invalid CGA map')
    if len(pal['cga'])!=3 or any(type(p['register'])is not int or not 0<=p['register']<64 for p in pal['cga']):raise ValueError('invalid CGA profile')
    for s in sprites:
        if not 1<=s['w']<=64 or not 1<=s['h']<=64:raise ValueError('invalid sprite size')
        pixels=bytes.fromhex(s['pixels'])
        if len(pixels)!=s['w']*s['h'] or max(pixels)>=len(rgb):raise ValueError('invalid sprite pixels')

def streams(s,pal,cga):
    w,h=s['w'],s['h'];pix=bytes.fromhex(s['pixels']);out=[]
    for phase in range(4):
        data=bytearray()
        if not cga:
            for y in range(h):
                row=pix[y*w+phase:(y+1)*w:4];x=0
                while x<len(row):
                    if not row[x]:x+=1;continue
                    end=x+1
                    while end<len(row) and row[end]:end+=1
                    data+=word(y*80+x)+bytes([end-x])+row[x:end];x=end
        else:
            sw=(w*5+3)//4;sh=(h*5+5)//6
            for y in range(sh):
                pairs=[]
                for group in range((sw+phase+3)//4):
                    mask=bits=0
                    for p in range(4):
                        x=group*4+p-phase
                        ink=pix[min(h-1,y*6//5)*w+min(w-1,x*4//5)] if 0<=x<sw else 0
                        mask=(mask<<2)|(0 if ink else 3)
                        bits=(bits<<2)|(pal['cga_map'][ink] if ink else 0)
                    pairs.append((mask,bits))
                x=0
                while x<len(pairs):
                    if pairs[x][0]==255:x+=1;continue
                    masked=pairs[x][0]!=0;end=x+1
                    while end<len(pairs) and pairs[end][0]!=255 and (pairs[end][0]!=0)==masked:end+=1
                    data+=word(y*80+x)+bytes([end-x,masked])
                    data+=bytes(v for pair in pairs[x:end] for v in pair) if masked else bytes(p[1] for p in pairs[x:end])
                    x=end
        out.append(data+b'\xff\xff')
    return out

def cga_program(s,pal,phase):
    w,h=s['w'],s['h'];pix=bytes.fromhex(s['pixels']);sw=(w*5+3)//4;sh=(h*5+5)//6
    code=bytearray()
    def emit(off,mask,bits,size):
        if mask==(1<<(size*8))-1:return
        disp=word(off)
        if size==2:
            if mask:code.extend(b'\x26\x81\xa5'+disp+word(mask))
            code.extend((b'\x26\x81\x8d' if mask else b'\x26\xc7\x85')+disp+word(bits))
        else:
            if mask:code.extend(b'\x26\x80\xa5'+disp+bytes([mask]))
            code.extend((b'\x26\x80\x8d' if mask else b'\x26\xc6\x85')+disp+bytes([bits]))
    for y in range(sh):
        pairs=[]
        for group in range((sw+phase+3)//4):
            mask=bits=0
            for p in range(4):
                x=group*4+p-phase
                ink=pix[min(h-1,y*6//5)*w+min(w-1,x*4//5)] if 0<=x<sw else 0
                mask=(mask<<2)|(0 if ink else 3)
                bits=(bits<<2)|(pal['cga_map'][ink] if ink else 0)
            pairs.append((mask,bits))
        x=0
        while x<len(pairs):
            mask,bits=pairs[x]
            if x+1<len(pairs) and mask!=255 and pairs[x+1][0]!=255:
                m,b=pairs[x+1];emit(y*80+x,mask|(m<<8),bits|(b<<8),2);x+=2
            else:emit(y*80+x,mask,bits,1);x+=1
    return code+b'\xcb'       # RETF: validated graphics program, ES:DI only


def bank(sprites,pal,cga):
    data=bytearray(b'N42C' if cga else b'N42V')+bytes(2)+word(len(sprites))+bytes(len(sprites)*2)
    for i,s in enumerate(sprites):
        struct.pack_into('<H',data,8+i*2,len(data));start=len(data)
        compiled=cga and i in (*range(12),14,16,17,18)
        data+=bytes([s['w']|(128 if compiled else 0),s['h']])+bytes(8)
        variants=[cga_program(s,pal,p) for p in range(4)] if compiled else streams(s,pal,cga)
        for p,stream in enumerate(variants):
            struct.pack_into('<H',data,start+2+p*2,len(data));data+=stream
    if len(data)>65500:raise ValueError('sprite bank exceeds one segment')
    struct.pack_into('<H',data,4,len(data));return data

def scene(pixels,pal,cga):
    if len(pixels)!=61440 or max(pixels)>=len(pal['rgb']):raise ValueError('invalid stage pixels')
    if cga:
        data=bytearray()
        for y in range(200):
            for x in range(0,320,4):
                b=0
                for p in range(4):b=(b<<2)|pal['cga_map'][pixels[(y*6//5)*256+(x+p)*4//5]]
                data.append(b)
    else:data=b''.join(pixels[p::4] for p in range(4))
    return b'N42B'+word(len(data)+8)+bytes([int(cga),0])+data

def vga_cache(sprites):
    planes=[bytearray() for _ in range(4)];commands=[];entries=[]
    for i,s in enumerate(sprites):
        if i not in (*range(12),14,16,17,18):entries.append('0');continue
        entries.append('n_cache%d'%i);commands+=['n_cache%d: dw n_cache%d_0,n_cache%d_2'%(i,i,i)]
        w,h=s['w'],s['h'];pix=bytes.fromhex(s['pixels'])
        for phase in (0,2):
            start=len(planes[0]);stride=(w+phase+3)//4;groups={}
            for y in range(h):
                row=[]
                for x in range(stride):
                    mask=0
                    for p in range(4):
                        sx=x*4+p-phase;v=pix[y*w+sx] if 0<=sx<w else 0
                        planes[p].append(v)
                        if v:mask|=1<<p
                    row.append(mask)
                x=0
                while x<stride:
                    if not row[x]:x+=1;continue
                    end=x+1
                    while end<stride and row[end]==row[x]:end+=1
                    groups.setdefault(row[x],[]).append((y*80+x,57600+start+y*stride+x,end-x));x=end
            commands+=['n_cache%d_%d:'%(i,phase)]
            for mask,runs in groups.items():
                commands+=['    dw %d,%d'%((mask<<8)|2,len(runs))]
                commands+=['    dw %d,%d,%d'%r for r in runs]
            commands+=['    dw 0']
    if len(planes[0])>7936:raise ValueError('VGA cache exceeds offscreen memory')
    return b''.join(planes),['N_CACHE_BYTES equ %d'%len(planes[0]),'n_cacheindex: dw '+','.join(entries)]+commands

def write(path,data):
    if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)

def build(out):
    pal=json.loads((ART.parent/'palette.json').read_text());sprites=json.loads((ART/'sprites.json').read_text());validate(pal,sprites)
    out.mkdir(parents=True,exist_ok=True)
    inc=['; Generated from committed original graphics; no cartridge required.']
    for i,s in enumerate(sprites):inc.append('n_%sart equ %d'%(s['name'],i))
    def emit(name,data):
        inc.append(name+':')
        for i in range(0,len(data),16):inc.append('    db '+','.join(map(str,data[i:i+16])))
    emit('n_dac',[v*63//255 for c in pal['rgb'] for v in c]);inc.append('N_COLORS equ %d'%len(pal['rgb']))
    emit('n_cgaregs',[p['register'] for p in pal['cga']])
    cached,commands=vga_cache(sprites);inc+=commands
    write(out/'1942L.GFX',cached)
    inc+=['N_L_SIZE equ %d'%len(cached),'N_L_SUM equ %d'%checksum(cached)]
    for cga in (False,True):
        tag='C' if cga else 'V';data=bank(sprites,pal,cga);write(out/('1942%s.GFX'%tag),data)
        inc+=['N_%s_SIZE equ %d'%(tag,len(data)),'N_%s_SUM equ %d'%(tag,checksum(data))]
        for name in ('SEA','REEF','PORT'):
            data=scene((ART/(name.lower()+'.idx')).read_bytes(),pal,cga);write(out/(name+'.'+tag+'42'),data)
            inc+=['N_%s_%s_SUM equ %d'%(name,tag,checksum(data))]
    # Static address tables remove MUL/DIV from every draw operation.
    for name,vals in [('n_yv',[y*80 for y in range(240)]),('n_yc',[(y*5//6)*80 for y in range(240)]),('n_xc',[x*5//4 for x in range(256)])]:
        inc.append(name+':');inc.extend('    dw '+','.join(map(str,vals[i:i+12])) for i in range(0,len(vals),12))
    write(out/'1942art.inc',('\n'.join(inc)+'\n').encode())
    print('1942 assets: original sprites and three adapter-native scenery banks compiled')

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('-o',type=Path,default=Path('build'));a=ap.parse_args();build(a.o)
if __name__=='__main__':main()
