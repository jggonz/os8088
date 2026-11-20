#!/usr/bin/env python3
"""Build/check Boratype BTV1 local PCM voice packs (BORATYPE-SPEC 6).

build MANIFEST.json -o BORAT.VPK [--redistributable]
check BORAT.VPK [--wav-dir DIR]

The manifest has rate (default8000), clips[{id,tag,caption,file,priority,
expression,source_url,attribution,rights,permission}]. A permission string is
required only for --redistributable, and MUST identify the actual permission.
Source files are local; this tool never downloads. ffmpeg decodes and resamples;
integer host conversion removes DC, trims exterior silence, keeps10ms guards,
limits gain to2x and peaks to80%, then emits unsigned8-bit mono. No clipping or
silent truncation. Packs are deterministic for identical decoded PCM.
"""
import argparse
import array
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import wave

MAX_PACK=28672
HEADER=16
ROW=56
TAGS={'welcome':1,'error':2,'low':3,'poor':4,'back':5,'idle':6,
      'recovery':7,'streak':8,'success':9,'record':10}

class VoiceError(ValueError): pass

def require(ok,message):
    if not ok: raise VoiceError(message)

def checksum(data): return (sum(data[:10])+sum(data[12:]))&65535

def decode(path,rate):
    proc=subprocess.run(['ffmpeg','-v','error','-nostdin','-i',str(path),
        '-f','s16le','-acodec','pcm_s16le','-ac','1','-ar',str(rate),'-'],
        stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    require(proc.returncode==0,f'{path}: decode failed: {proc.stderr.decode(errors="replace")}')
    samples=array.array('h'); samples.frombytes(proc.stdout)
    if sys.byteorder!='little': samples.byteswap()
    require(len(samples)>rate//50,f'{path}: empty/very short audio')
    require(len(samples)<=rate*30,f'{path}: use a short source, max30seconds')
    dc=sum(samples)//len(samples)
    clean=[s-dc for s in samples]
    peak=max(abs(s) for s in clean)
    require(peak>=328,f'{path}: silent audio')
    threshold=max(100,peak//100)
    first=next(i for i,v in enumerate(clean) if abs(v)>=threshold)
    last=len(clean)-next(i for i,v in enumerate(reversed(clean)) if abs(v)>=threshold)
    guard=rate//100
    clean=clean[max(0,first-guard):min(len(clean),last+guard)]
    # Integer rational gain: <=2x; peaks <=102/128 of the byte range.
    gain=min(65536*2,(26112*65536)//peak)
    pcm=bytes(max(0,min(255,128+((s*gain//65536)//256))) for s in clean)
    pcm+=b'\x80'*((-len(pcm))%2048)
    require(2048<=len(pcm)<=16384,
        f'{path}: {len(pcm)}bytes padded; clip limit16384. Trim or choose lower rate explicitly.')
    return pcm

def build(manifest,out,redistributable=False):
    src=Path(manifest); doc=json.loads(src.read_text())
    rate=doc.get('rate',8000); clips=doc.get('clips',[])
    require(isinstance(rate,int) and 4000<=rate<=11025,'rate must be4000..11025')
    require(1<=len(clips)<=12,'need1..12clips')
    off=HEADER+len(clips)*ROW
    rows=bytearray(); payload=bytearray(); seen=set(); report=[]
    for clip in clips:
        ident=clip['id']; tag=TAGS.get(clip['tag'],clip['tag'])
        pri=clip.get('priority',1); expr=clip.get('expression',1)
        require(type(ident) is int and 0<=ident<12 and ident not in seen,'IDs unique0..11')
        seen.add(ident)
        require(type(tag) is int and 1<=tag<=10,'invalid event tag')
        require(type(pri) is int and 0<=pri<=3,'priority must be0..3')
        require(type(expr) is int and 0<=expr<=6,'expression must be0..6')
        cap=clip['caption'].encode('ascii')
        require(1<=len(cap)<=47 and all(32<=c<=126 for c in cap),'caption must be1..47printable ASCII')
        require(clip.get('rights') and clip.get('source_url') and clip.get('attribution'),
                'each clip needs rights, source_url and attribution metadata')
        if redistributable:
            require(clip.get('permission') and clip.get('redistribution') is True,
                    'redistribution requires explicit permission record for every clip')
        path=(src.parent/clip['file']).resolve(); pcm=decode(path,rate)
        rows+=struct.pack('<BBBBHH48s',ident,tag,pri,expr,off,len(pcm),cap)
        payload+=pcm
        report.append({**clip,'pcm_bytes':len(pcm),'pcm_sha256':hashlib.sha256(pcm).hexdigest(),
                       'source_sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
        off+=len(pcm)
    require(off<=MAX_PACK,f'pack is{off}bytes, limit{MAX_PACK}; choose a smaller working set or lower rate')
    data=bytearray(struct.pack('<4sBBHHH4s',b'BTV1',len(clips),0,rate,off,0,b'\0'*4))+rows+payload
    struct.pack_into('<H',data,10,checksum(data))
    check(data)
    Path(out).write_bytes(data)
    Path(str(out)+'.sources.json').write_text(json.dumps({'format':'BTV1','rate':rate,
        'redistributable':redistributable,'clips':report},indent=2)+'\n')
    return data

def check(data,wav_dir=None):
    require(HEADER<=len(data)<=MAX_PACK,'pack length out of bounds')
    magic,n,fmt,rate,total,checkword,res=struct.unpack_from('<4sBBHHH4s',data)
    require(magic==b'BTV1' and fmt==0 and res==b'\0'*4,'bad header/version/format')
    require(1<=n<=12 and 4000<=rate<=11025,'bad count/rate')
    require(total==len(data) and checkword==checksum(data),'length/integrity mismatch')
    require(HEADER+n*ROW<len(data),'truncated rows')
    offset=HEADER+n*ROW; seen=set(); result=[]
    for i in range(n):
        ident,tag,pri,expr,off,count,cap=struct.unpack_from('<BBBBHH48s',data,HEADER+i*ROW)
        require(ident<12 and ident not in seen,'duplicate/out of range ID'); seen.add(ident)
        require(1<=tag<=10 and pri<=3 and expr<=6,'bad tag/priority/expression')
        require(off==offset and 2048<=count<=16384 and count%2048==0,'bad PCM span')
        require(off+count<=len(data),'PCM past end')
        require(b'\0' in cap,'unterminated caption')
        caption=cap.split(b'\0')[0]
        require(caption and all(32<=c<=126 for c in caption),'bad caption')
        if wav_dir:
            dest=Path(wav_dir);dest.mkdir(parents=True,exist_ok=True)
            with wave.open(str(dest/f'{ident:02d}.wav'),'wb') as wav:
                wav.setparams((1,1,rate,0,'NONE','not compressed'));wav.writeframes(data[off:off+count])
        result.append({'id':ident,'tag':tag,'caption':caption.decode(),'seconds':round(count/rate,3)})
        offset+=count
    require(offset==len(data),'unreferenced trailing data')
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='cmd',required=True)
    b=sub.add_parser('build');b.add_argument('manifest');b.add_argument('-o',required=True)
    b.add_argument('--redistributable',action='store_true')
    c=sub.add_parser('check');c.add_argument('pack');c.add_argument('--wav-dir')
    args=p.parse_args()
    try:
        if args.cmd=='build': data=build(args.manifest,args.o,args.redistributable)
        else: data=Path(args.pack).read_bytes()
        print(json.dumps(check(data,getattr(args,'wav_dir',None)),indent=2))
    except (VoiceError,OSError,KeyError,TypeError,UnicodeError,json.JSONDecodeError,struct.error) as error:
        p.exit(1,f'os88btvoice: {error}\n')
if __name__=='__main__': main()
