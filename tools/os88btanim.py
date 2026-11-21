#!/usr/bin/env python3
"""Original supplied Borat grid -> exact pixel sources -> bounded LZ4 bands.

Only `draft` imports Pillow. Build uses the existing standard-library LZ4
codec and independently round-trips every strip. Parts3/4/5 are read-only banks;
record directory entries are part,offset,packed_length,content_signature.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import os88bbart as bb
import os88lz as lz
from os88btscenes import build_scene_bank

ROOT=Path(__file__).resolve().parents[1]
ART=ROOT/'apps/boratype/art'
VARIANTS=(('big',96,120,4),('small',40,48,4),('mono',40,48,1))
STATIC=(0,0,17,18,29,21,24)
ROWS=24
HEADER=16
COUNT=30*9
DIRECTORY=HEADER+COUNT*8
LIMIT=65000

def read(path=None):
    lines=iter(Path(path or ART/'animation.txt').read_text().splitlines());out=[]
    for line in lines:
        if not line or line.startswith('#'):continue
        tag,name,sw,sh=line.split();w,h=int(sw),int(sh)
        if tag!='sprite':raise ValueError('sprite header')
        rows=[next(lines) for _ in range(h)]
        if any(len(r)!=w or set(r)-set(bb.CHARS.replace('.','')) for r in rows):raise ValueError('sprite pixels')
        pix=[bb.CHARS.index(c) for row in rows for c in row]
        if name.startswith('mono') and set(pix)-{0,15}:raise ValueError('explicit mono')
        out.append((name,w,h,pix))
    expected=[(name+str(i),w,h) for i in range(30) for name,w,h,_ in VARIANTS]
    if [(n,w,h) for n,w,h,_ in out]!=expected:raise ValueError('30 complete frame variants required')
    return out

def planar(pix,w,h,planes):
    return bytes(sum((((pix[y*w+x*8+k]>>(plane if planes==4 else 0))&1)<<(7-k)) for k in range(8))
                 for plane in range(planes) for y in range(h) for x in range(w//8))

def banks(frames):
    data=[bytearray(b'BTAN'+struct.pack('<6H',1,30,ROWS,COUNT,0,0)+bytes(COUNT*8)),bytearray(b'BTAN'+struct.pack('<6H',1,0,24,0,0,0)),bytearray(b'BTAN'+struct.pack('<6H',1,0,24,0,0,0))]
    data[2]+=build_scene_bank()
    unique={};records=[];signatures=[]
    for frame in range(30):
        for variant,(_,w,h,planes) in enumerate(VARIANTS):
            _,_,_,pix=frames[frame*3+variant]
            for y in range(0,h,ROWS):
                raw=planar(pix[y*w:(y+ROWS)*w],w,ROWS,planes)
                key=(w,planes,raw)
                if key not in unique:
                    packed=lz.lz4_compress(raw,depth=64,parse='fast')
                    assert lz.lz4_decompress(packed,len(raw))==raw
                    bank=next((i for i,b in enumerate(data) if len(b)+len(packed)+1<=LIMIT),None)
                    if bank is None:raise ValueError('animation exceeds three bounded banks')
                    if len(data[bank])&1:data[bank].append(0)
                    off=len(data[bank]);data[bank]+=packed
                    sig=len(unique)+1;unique[key]=(3+bank,off,len(packed),sig)
                records.append(unique[key]);signatures.append(unique[key][3])
    assert len(records)==COUNT
    for body in data:struct.pack_into('<H',body,6,len(body))
    for i,rec in enumerate(records):struct.pack_into('<4H',data[0],HEADER+i*8,*rec)
    return data,records

def build(out0,out1,out2):
    readframes=read();data,records=banks(readframes)
    for path,body in zip((out0,out1,out2),data):
        if path is None:continue
        Path(path).parent.mkdir(parents=True,exist_ok=True);Path(path).write_bytes(body)
    # Check directory coverage, per-strip bounds and content-equivalent cache IDs.
    for body in data:
        assert body[:4]==b'BTAN' and struct.unpack_from('<H',body,4)[0]==1
        assert struct.unpack_from('<H',body,6)[0]==len(body)<=LIMIT
    for i,(part,off,n,sig) in enumerate(records):
        frame,slot=divmod(i,9);variant=0 if slot<5 else 1 if slot<7 else 2
        strip=slot if variant==0 else slot-(5 if variant==1 else 7)
        _,w,h,planes=VARIANTS[variant];pix=readframes[frame*3+variant][3]
        expected=planar(pix[strip*ROWS*w:(strip+1)*ROWS*w],w,ROWS,planes)
        assert off>=HEADER and off+n<=len(data[part-3]) and 0<n<=1154 and sig
        assert lz.lz4_decompress(data[part-3][off:off+n],len(expected))==expected
    print('os88btanim: banks '+str([len(b) for b in data])+', '+str(len(set(records)))+' unique strips, max raw1152B')

def draft(grid,scene):
    from PIL import Image
    ART.mkdir(exist_ok=True)
    im=Image.open(grid).convert('RGB');back=Image.open(scene).convert('RGB')
    assert im.size==(1254,1254) and back.size==(1448,1086),(im.size,back.size)
    xs=(158,343,525,705,886,1067,1251);ys=(158,360,562,759,959,1164)
    scenic=back.crop((302,126,365,304))
    neutral=im.crop((xs[0]+3,ys[0]+3,xs[1]-3,ys[1]-3))
    cache={}
    def ink(rgb):
        if rgb not in cache:cache[rgb]=min([i for i in range(16) if i!=13],key=lambda i:sum((rgb[k]-bb.EGA[i][k])**2 for k in range(3)))
        return cache[rgb]
    text=['# Original supplied5x6 grid. See ANIMATION-SOURCE.json; no build-time PNG.']
    pixels={}
    for frame in range(30):
        row,col=divmod(frame,6)
        cell=im.crop((xs[col]+3,ys[row]+3,xs[col+1]-3,ys[row+1]-3))
        for name,w,h,np in VARIANTS:
            foreground=cell.resize((w,h),Image.Resampling.NEAREST)
            if row<2:
                # Keep the supplied neutral camera/body fixed for small motion.
                # Eyes/mouth come from the corresponding actual source cells.
                stable=neutral.resize((w,h),Image.Resampling.NEAREST)
                bounds=(28,34,78,53) if row==0 else (28,53,80,82)
                box=tuple(v*w//96 if i%2==0 else v*h//120 for i,v in enumerate(bounds))
                if row==1 or col in (1,2,3):stable.paste(foreground.crop(box),box[:2])
                foreground=stable
            backdrop=scenic.resize((w,h),Image.Resampling.NEAREST)
            rows=[];pix=[]
            for y in range(h):
                line=''
                for x in range(w):
                    r,g,b=foreground.getpixel((x,y));rgb=backdrop.getpixel((x,y)) if b>r+25 and b>g+20 and r<55 and g<55 else (r,g,b)
                    if np==1:
                        lum=(rgb[0]*3+rgb[1]*6+rgb[2])//10;k=15 if lum>[32,160,224,96][(y%2)*2+x%2] else 0
                    else:k=ink(rgb)
                    line+=bb.CHARS[k];pix.append(k)
                rows.append(line)
            pixels[(name,frame)]=rows;text.extend([f'sprite {name}{frame} {w} {h}',*rows,''])
    (ART/'animation.txt').write_text('\n'.join(text)+'\n')
    static=['# Original provided grid/scenery. Exact source metadata: ANIMATION-SOURCE.json.']
    blink=['# Original grid closed-eye pose; matches new scenic static art.']
    for name,w,h in [('big',96,120),('small',40,48),('mono',40,48),('cga',32,40)]:
        for expression,frame in enumerate(STATIC):
            if name=='cga':
                big=pixels[('big',frame)];c=Image.new('RGB',(96,120));c.putdata([bb.EGA[bb.CHARS.index(v)] for row in big for v in row]);c=c.resize((w,h),Image.Resampling.NEAREST)
                rows=[''.join(bb.CHARS[min([0,11,5,15],key=lambda k:sum((c.getpixel((x,y))[j]-bb.EGA[k][j])**2 for j in range(3)))] for x in range(w)) for y in range(h)]
            else:rows=pixels[(name,frame)]
            static.extend([f'sprite {name}{expression} {w} {h}',*rows,''])
    (ART/'sprites.txt').write_text('\n'.join(static)+'\n')
    for name,w,h in [('big',96,120),('small',40,48),('mono',40,48),('cga',32,40)]:
        if name=='cga':
            c=Image.new('RGB',(96,120));c.putdata([bb.EGA[bb.CHARS.index(v)] for row in pixels[('big',2)] for v in row]);c=c.resize((w,h),Image.Resampling.NEAREST)
            rows=[''.join(bb.CHARS[min([0,11,5,15],key=lambda k:sum((c.getpixel((x,y))[j]-bb.EGA[k][j])**2 for j in range(3)))] for x in range(w)) for y in range(h)]
        else:rows=pixels[(name,2)]
        blink.extend([f'sprite {name}7 {w} {h}',*rows,''])
    (ART/'blink.txt').write_text('\n'.join(blink)+'\n')
    for source,target in [(grid,'provided-animation-grid.png'),(scene,'provided-lesson-scene.png')]:
        (ART/target).write_bytes(Path(source).read_bytes())
    meta={'grid_sha256':hashlib.sha256(Path(grid).read_bytes()).hexdigest(),'scene_sha256':hashlib.sha256(Path(scene).read_bytes()).hexdigest(),
          'x_boundaries':xs,'y_boundaries':ys,'cell_inset':3,'scenic_crop':[302,126,365,304],'small_motion_composite':'neutral body/camera; idle source eye patch[28,34,78,53],talk mouth patch[28,53,80,82],scaledwithvariant; full reaction cells unchanged',
          'resize':'nearest-neighbour; full source-cell extent, preserving hand gestures','navy_key':'B>R+25,B>G+20,R<55,G<55',
          'palette':'fixedEGA; index13reserved; mono explicit2x2threshold','static_frames':STATIC,
          'provenance':'User supplied original assets; no generated replacement, movie frame, downloaded media or build service.'}
    (ART/'ANIMATION-SOURCE.json').write_text(json.dumps(meta,indent=2)+'\n');read()

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);sp=ap.add_subparsers(dest='cmd',required=True)
    p=sp.add_parser('draft');p.add_argument('grid');p.add_argument('scene')
    p=sp.add_parser('build');p.add_argument('--out0');p.add_argument('--out1');p.add_argument('--out2');p.add_argument('--bank',type=int,choices=(0,1,2));p.add_argument('--output')
    a=ap.parse_args()
    if a.cmd=='draft':draft(a.grid,a.scene)
    else:
        outputs=[a.out0,a.out1,a.out2]
        if a.bank is not None:
            if not a.output:ap.error('--bank requires --output')
            outputs=[a.output if i==a.bank else None for i in range(3)]
        if not any(outputs):ap.error('an output is required')
        build(*outputs)
