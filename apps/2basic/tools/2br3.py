#!/usr/bin/env python3
"""2BR3 host-side builder, converter, inspector and validator (B35).

Python 3.9+, standard library only. The B35 native interpreter can read a
bounded 8KB subset of this format. The host format supports up to 61,440B.
"""
from __future__ import annotations
import argparse
from dataclasses import dataclass
import re
import shlex
import struct
from pathlib import Path

MAGIC=b'2BR3'
HEADER_SIZE=24
ENTRY_SIZE=24
MAX_SIZE=61440
MAX_ITEMS=255
TYPES={'IMG1':1,'IMG4':2,'SPRITES1':3,'TEXT':4,'DATA':5,'WORDS16':6}
TYPE_NAMES={v:k for k,v in TYPES.items()}
HEADER_FMT='<4sBBHHHHBBH6s'
ENTRY_FMT='<12sBBHHHHH'
assert struct.calcsize(HEADER_FMT)==HEADER_SIZE
assert struct.calcsize(ENTRY_FMT)==ENTRY_SIZE
LEGACY_MAGIC=b'2BR2\x10\x10\x63\x01'

@dataclass(frozen=True)
class Entry:
    name: str
    kind: int
    flags: int
    width: int
    height: int
    payload: bytes
    aux: int=0
    offset: int=0


def clean_name(name:str)->str:
    name=name.upper()
    if not re.fullmatch(r'[A-Z][A-Z0-9_]{0,11}',name):
        raise ValueError(f'invalid resource name {name!r} (1..12 A-Z, 0-9, underscore; start with letter)')
    return name


def pixels_bmp(path:Path, expect_bpp:int)->tuple[int,int,list[list[int]]]:
    raw=path.read_bytes()
    if len(raw)<62 or raw[:2]!=b'BM':raise ValueError(f'{path}: not a BMP')
    data_off=struct.unpack_from('<I',raw,10)[0]
    dib,w,h,planes,bpp,comp=struct.unpack_from('<IiiHHI',raw,14)
    if dib<40 or w<=0 or h==0 or w>512 or abs(h)>512 or planes!=1 or bpp!=expect_bpp or comp!=0:
        raise ValueError(f'{path}: expected uncompressed Windows {expect_bpp}bpp BMP; got {w}x{h} {bpp}bpp, compression={comp}')
    palette_start=14+dib
    palette_bytes=4*(1<<bpp)
    if data_off<palette_start+palette_bytes:raise ValueError(f'{path}: truncated/missing palette')
    pitch=((w*bpp+31)//32)*4
    if data_off+pitch*abs(h)>len(raw):raise ValueError(f'{path}: truncated pixel rows')
    invert=False
    if bpp==1:
        # Choose the visually dark palette index as 1 (ink) and light as 0.
        p0=raw[palette_start:palette_start+3]
        p1=raw[palette_start+4:palette_start+7]
        invert=sum(p0)<sum(p1)
    image=[]
    for y in range(abs(h)):
        iy=(abs(h)-1-y) if h>0 else y
        row=raw[data_off+iy*pitch:data_off+(iy+1)*pitch]
        if bpp==1:
            image.append([((row[x//8]>>(7-x%8))&1) ^ int(invert) for x in range(w)])
        else:
            image.append([(row[x//2]>>(4 if x%2==0 else 0))&15 for x in range(w)])
    return w,abs(h),image


def pack_pixels(pixels:list[list[int]], bpp:int)->bytes:
    if not pixels or not pixels[0]:raise ValueError('empty image')
    w=len(pixels[0]); out=bytearray()
    for row in pixels:
        if len(row)!=w:raise ValueError('ragged image')
        if bpp==1:
            out.extend(sum((p&1)<<(7-i) for i,p in enumerate(row[b:b+8])) for b in range(0,w,8))
        elif bpp==4:
            out.extend((((row[i]&15)<<4)|((row[i+1]&15) if i+1<w else 0)) for i in range(0,w,2))
        else: raise ValueError('unsupported bpp')
    return bytes(out)


def sprite_bank(name:str,width:int,height:int,count:int,paths:list[Path])->Entry:
    if not 1<=width<=128 or not 1<=height<=128 or not 1<=count<=255:
        raise ValueError('sprite size and count out of range')
    pieces=[]
    for path in paths:
        w,h,pixels=pixels_bmp(path,1)
        if w%width or h%height:raise ValueError(f'{path}: BMP sheet {w}x{h} not divisible by sprite {width}x{height}')
        for y in range(0,h,height):
            for x in range(0,w,width):
                pieces.append([row[x:x+width] for row in pixels[y:y+height]])
    if len(pieces)<count:raise ValueError(f'bank {name}: only {len(pieces)} sprites found; expected {count}')
    payload=b''.join(pack_pixels(p,1) for p in pieces[:count])
    return Entry(clean_name(name),TYPES['SPRITES1'],1,width,height,payload,count)


def from_legacy(source:Path,bank_name:str,count:int)->Entry:
    blob=source.read_bytes()
    if len(blob)!=3176 or blob[:8]!=LEGACY_MAGIC:
        raise ValueError(f'{source}: expected 3,176-byte 2BR2')
    if not 1<=count<=99:raise ValueError('legacy count must be 1..99')
    return Entry(clean_name(bank_name),TYPES['SPRITES1'],1,16,16,blob[8:8+count*32],count)


def parse_manifest(path:Path)->tuple[Path,list[Entry]]:
    out=None; entries=[];base=path.parent
    for num,line in enumerate(path.read_text(encoding='utf-8-sig').splitlines(),1):
        tokens=shlex.split(line,comments=True,posix=False)
        tokens=[v.strip('"') for v in tokens]
        if not tokens:continue
        op=tokens.pop(0).upper()
        try:
            if op=='OUT' and len(tokens)==1:
                if Path(tokens[0]).name!=tokens[0] or not tokens[0].upper().endswith('.RES'):
                    raise ValueError('OUT must name a .RES in this manifest folder')
                out=base/tokens[0]
            elif op in ('IMG1','IMG4') and len(tokens)==2:
                name=clean_name(tokens[0]); bpp=1 if op=='IMG1' else 4
                w,h,px=pixels_bmp(base/tokens[1],bpp)
                entries.append(Entry(name,TYPES[op],1 if bpp==1 else 0,w,h,pack_pixels(px,bpp)))
            elif op=='SPRITES1' and len(tokens)>=5:
                name=clean_name(tokens[0]);w,h,count=map(int,tokens[1:4])
                entries.append(sprite_bank(name,w,h,count,[base/p for p in tokens[4:]]))
            elif op=='LEGACY' and len(tokens)==3:
                entries.append(from_legacy(base/tokens[1],tokens[0],int(tokens[2])))
            elif op in ('TEXT','DATA') and len(tokens)==2:
                name=clean_name(tokens[0]);payload=(base/tokens[1]).read_bytes()
                entries.append(Entry(name,TYPES[op],0,0,0,payload))
            elif op=='WORDS16' and len(tokens)==2:
                name=clean_name(tokens[0]);raw=(base/tokens[1]).read_text('ascii')
                values=[int(x) for x in re.split(r'[,\s]+',raw.strip()) if x]
                if any(not -32768<=x<=32767 for x in values):raise ValueError('WORDS16 values must be signed 16-bit')
                entries.append(Entry(name,TYPES[op],0,0,0,b''.join(struct.pack('<h',x) for x in values),len(values)))
            else:
                raise ValueError(f'unknown directive or wrong argument count: {op}')
        except (ValueError,OSError) as exc:
            raise ValueError(f'{path}:{num}: {exc}') from exc
    if out is None:raise ValueError('missing OUT NAME.RES')
    return out,entries


def create(entries:list[Entry])->bytes:
    if len(entries)>MAX_ITEMS:raise ValueError(f'too many entries ({len(entries)} > {MAX_ITEMS})')
    if len({e.name for e in entries})!=len(entries):raise ValueError('duplicate resource name')
    # Keep manifest order for sprite-bank indexing and deterministic builds.
    data_start=HEADER_SIZE+ENTRY_SIZE*len(entries)
    if data_start>MAX_SIZE:raise ValueError('directory exceeds maximum')
    directory=bytearray();data=bytearray(); cursor=data_start
    for e in entries:
        if len(e.payload)>0xffff:raise ValueError(f'{e.name}: too large')
        if cursor%2: data.append(0);cursor+=1
        if cursor+len(e.payload)>MAX_SIZE:raise ValueError(f'resource pack exceeds {MAX_SIZE:,} bytes')
        name=e.name.encode('ascii').ljust(12,b'\x00')
        directory.extend(struct.pack(ENTRY_FMT,name,e.kind,e.flags,e.width,e.height,cursor,len(e.payload),e.aux))
        data.extend(e.payload);cursor+=len(e.payload)
    body=bytes(directory+data)
    checksum=sum(body)&0xffff
    header=struct.pack(HEADER_FMT,MAGIC,3,0,len(entries),HEADER_SIZE,data_start,cursor,ENTRY_SIZE,0,checksum,b'\x00'*6)
    blob=header+body
    assert len(blob)==cursor
    validate(blob)
    return blob


def validate(blob:bytes)->list[Entry]:
    if len(blob)<HEADER_SIZE:raise ValueError('truncated 2BR3 header')
    magic,version,flags,count,diroff,dataoff,total,entrysize,reserved,checksum,tail=struct.unpack_from(HEADER_FMT,blob)
    if magic!=MAGIC or version!=3 or flags or entrysize!=ENTRY_SIZE or reserved or tail!=b'\x00'*6:
        raise ValueError('invalid 2BR3 header/version')
    if total!=len(blob) or total>MAX_SIZE or count>MAX_ITEMS or diroff!=HEADER_SIZE or dataoff!=HEADER_SIZE+count*ENTRY_SIZE:
        raise ValueError('invalid 2BR3 lengths or index')
    if (sum(blob[HEADER_SIZE:])&0xffff)!=checksum:raise ValueError('checksum mismatch')
    entries=[];previous_end=dataoff; names=set();regions=[]
    for i in range(count):
        raw,kind,flags,w,h,off,length,aux=struct.unpack_from(ENTRY_FMT,blob,diroff+i*ENTRY_SIZE)
        try: name=clean_name(raw.rstrip(b'\x00').decode('ascii'))
        except (UnicodeError,ValueError) as exc:raise ValueError(f'index {i}: bad name') from exc
        if name in names:raise ValueError('duplicate name')
        names.add(name)
        if kind not in TYPE_NAMES or (flags&~1) or off%2 or off<dataoff or off+length>total:
            raise ValueError(f'{name}: invalid type/flags/offset')
        if kind in (1,2,3):
            bpp=4 if kind==2 else 1
            if not w or not h or w>512 or h>512:raise ValueError(f'{name}: invalid dimensions')
            n=aux if kind==3 else 1
            if n<1 or n>255 or length!=((w*bpp+7)//8)*h*n:
                raise ValueError(f'{name}: image length/count mismatch')
        elif kind==6:
            if length%2 or aux!=length//2 or w or h:raise ValueError(f'{name}: bad WORDS16')
        else:
            if w or h or aux:raise ValueError(f'{name}: unexpected dimensions/aux')
        regions.append((off,off+length,name))
        entries.append(Entry(name,kind,flags,w,h,blob[off:off+length],aux,off))
    regions.sort()
    for (_,end,name),(next_start,_,next_name) in zip(regions,regions[1:]):
        if next_start<end:raise ValueError(f'overlapping data regions: {name}, {next_name}')
    return entries


def command_build(args):
    out,entries=parse_manifest(args.manifest)
    blob=create(entries)
    path=args.output or out
    path.write_bytes(blob)
    print(f'Wrote {path} — {len(blob):,} bytes, {len(entries)} named resources')
    command_list_blob(blob)


def command_list_blob(blob):
    for e in validate(blob):
        dims=f' {e.width}x{e.height}' if e.width else ''
        aux=f' x{e.aux}' if e.kind==3 else (f' [{e.aux} words]' if e.kind==6 else '')
        print(f'  {e.name:<12s} {TYPE_NAMES[e.kind]:<9s}{dims}{aux}: {len(e.payload):,} bytes at +{e.offset}')


def command_convert(args):
    entry=from_legacy(args.legacy,args.name,args.count)
    blob=create([entry]);args.output.write_bytes(blob)
    print(f'Converted {args.legacy} to {args.output} — {len(blob):,} bytes, {entry.aux} sprites')


def command_check(args):
    blob=args.file.read_bytes();entries=validate(blob)
    print(f'{args.file}: VALID 2BR3, {len(blob):,} bytes, {len(entries)} named resources')
    if args.list:command_list_blob(blob)


def command_extract(args):
    entries=validate(args.file.read_bytes());name=clean_name(args.name)
    match=next((e for e in entries if e.name==name),None)
    if match is None: raise ValueError(f'no resource named {name}')
    payload=match.payload
    if args.index is not None:
        if match.kind!=TYPES['SPRITES1']:raise ValueError('--index requires SPRITES1')
        if not 0<=args.index<match.aux:raise ValueError('sprite index out of range')
        size=((match.width+7)//8)*match.height
        payload=payload[args.index*size:(args.index+1)*size]
    args.output.write_bytes(payload)
    print(f'{args.output}: {len(payload)} raw payload bytes')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(required=True,dest='command')
    b=sub.add_parser('build',help='build 2BR3 from an ASCII manifest');b.add_argument('manifest',type=Path);b.add_argument('-o','--output',type=Path);b.set_defaults(func=command_build)
    c=sub.add_parser('convert',help='convert 2BR2 pack to named sprite bank');c.add_argument('legacy',type=Path);c.add_argument('output',type=Path);c.add_argument('--name',required=True);c.add_argument('--count',type=int,default=99);c.set_defaults(func=command_convert)
    v=sub.add_parser('verify',help='validate header, checksum, entries and ranges');v.add_argument('file',type=Path);v.add_argument('--list',action='store_true');v.set_defaults(func=command_check)
    x=sub.add_parser('extract',help='extract one named raw payload');x.add_argument('file',type=Path);x.add_argument('name');x.add_argument('output',type=Path);x.add_argument('--index',type=int);x.set_defaults(func=command_extract)
    args=p.parse_args()
    try:args.func(args)
    except (ValueError,OSError) as exc:p.error(str(exc))

if __name__=='__main__':main()
