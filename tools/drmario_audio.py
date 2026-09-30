#!/usr/bin/env python3
"""Compile DrMarco's committed NES note data to bounded native phrase lists.

Decode $DC83/$DDAE/$DE30, including repeats and global tempo/transposition.
Envelopes, vibrato, noise and DPCM are deliberately arranged as OPL patches /
speaker tones, not emulated. All four NES channels are walked in frame order
because the control bytes change shared state, even on an omitted voice.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

# Gameplay $A264, clear $A261, title $9841, options $99D3, loss $D4B4.
SONGS = [('fever',4), ('chill',2), ('clear_fever',9), ('clear_chill',10),
         ('title',6), ('options',7), ('loss',12)]


def read_rom(path):
    rom = {}
    for line in path.read_text().splitlines():
        m = re.search(r'01:([0-9A-F]{4}): ((?:[0-9A-F]{2} )+)',line)
        if m:
            raw=bytes.fromhex(m[2])
            literal=re.search(r'\.byte\s+((?:\$[0-9A-F]{2}(?:,?\s*)?)+)(?:;|$)',line)
            if literal: raw=bytes(int(b,16) for b in re.findall(r'\$([0-9A-F]{2})',literal[1]))
            for i,b in enumerate(raw):
                a=int(m[1],16)+i
                if a in rom and rom[a]!=b: raise ValueError(f'conflicting byte {a:04x}')
                rom[a]=b
    return rom


def decode(rom, song):
    def word(a): return rom[a] | rom[a+1]<<8
    header=0xe26e+rom[0xe260+song]
    transpose,tempo=rom[header],rom[header+1]
    channels=[]
    for ch in range(4):
        order=word(header+2+ch*2)
        channels.append(dict(order=order, ptr=0, wait=0, duration=0, repeat=0,
                             repeatptr=0, blocks=[], seen={}, loop=None,
                             done=order==0xffff, inactive=order==0xffff, events=[]))
    stopped=False
    for frame in range(60000):
        for ch,c in enumerate(channels):
            if c['inactive']: continue
            if c['wait']:
                c['wait']-=1
                if c['wait']: continue
            for _ in range(1024):
                if not c['ptr']:
                    order=c['order']; ptr=word(order)
                    if ptr==0xffff:
                        order=word(order+2); c['order']=order; ptr=word(order)
                    if ptr==0:
                        stopped=True; break  # NES stops the entire song
                    key=(order,transpose,tempo,c['duration'])
                    if not c['done'] and key in c['seen']:
                        c['loop']=c['seen'][key]; c['done']=True
                    c['events']=[]
                    if not c['done']:
                        c['seen'][key]=len(c['blocks'])
                        c['blocks'].append(c['events'])
                    c['ptr']=ptr
                b=rom[c['ptr']]; c['ptr']+=1
                if b==0:
                    c['ptr']=0; c['order']+=2; continue
                if b==0x9f:
                    c['ptr']+=2; continue # patch/envelope; native patches instead
                if b in (0x9c,0x9e):
                    v=rom[c['ptr']]; c['ptr']+=1
                    if b==0x9c: transpose=v
                    else: tempo=v
                    continue
                if b==0xff:
                    if c['repeat']:
                        c['repeat']-=1; c['ptr']=c['repeatptr']
                    continue
                if b&0xc0==0xc0:
                    c['repeat']=(b&63)-1; c['repeatptr']=c['ptr']; continue
                if b&0xb0==0xb0:
                    c['duration']=rom[0xe204+tempo+(b&15)]
                    b=rom[c['ptr']]; c['ptr']+=1
                duration=c['duration']
                if not 1<=duration<=255: raise ValueError(f'bad duration {duration}')
                hz=0
                if ch<3 and rom[0xe16b+b]:
                    n=(b+transpose if transpose<128 else b-(transpose&127)-1)&255
                    period=((rom[0xe16a+n]&7)<<8)|rom[0xe16b+n]
                    hz=round(1789773/((32 if ch==2 else 16)*(period+1)))
                    if not 19<=hz<=6208: raise ValueError(f'bad pitch {hz}')
                c['events'].append((duration,hz))
                c['wait']=duration
                break
            else: raise ValueError('unbounded source command chain')
            if stopped: break
        if stopped or all(c['done'] for c in channels): break
    else: raise ValueError('song did not terminate or loop')
    for c in channels[:3]:
        if stopped:
            # Source $DDAA stops every voice together, even mid-note.
            remaining=frame
            for block in c['blocks']:
                kept=[]
                for duration,hz in block:
                    if remaining>0: kept.append((min(duration,remaining),hz))
                    remaining-=duration
                block[:]=kept
            c['loop']=None
        # No empty phrases can reach the native bounded decoder.
        if c['loop'] is not None:
            c['loop']=sum(bool(b) for b in c['blocks'][:c['loop']])
        c['blocks']=[b for b in c['blocks'] if b]
    return channels[:3]


def build(source,out):
    path=source/'bank_FF.asm'; rom=read_rom(path)
    songs={name:decode(rom,number) for name,number in SONGS}
    hz=[0]+sorted({h for voices in songs.values() for v in voices for b in v['blocks'] for d,h in b if h})
    if len(hz)>256: raise ValueError('pitch table exceeds byte index')
    phrases={}; lists=[]; report={}
    for name,voices in songs.items():
        report[name]=[]
        for ch,v in enumerate(voices):
            ids=[]
            original=v['blocks']
            if v['loop'] is not None:
                v['loop']=sum((len(b)+15)//16 for b in original[:v['loop']])
            v['blocks']=[b[i:i+16] for b in original for i in range(0,len(b),16)]
            for block in v['blocks']:
                data=tuple(x for d,h in block for x in (d,hz.index(h)))
                if data not in phrases: phrases[data]=len(phrases)
                ids.append(phrases[data])
            label=f'dm_song_{name}_{ch}'
            lists.append(label+':')
            for i,p in enumerate(ids):
                lists += [f'{label}_{i}: dw dm_phrase_{p}']
            loop='0' if v['loop'] is None else f'{label}_{v["loop"]}'
            lists += [f'    dw 0,{loop}']
            report[name].append(dict(notes=sum(len(b) for b in v['blocks']),
                                     frames=sum(d for b in v['blocks'] for d,h in b),loop=v['loop']))
    text=['; Generated from reference/drmario/bank_FF.asm. Do not edit.', 'dm_song_table:']
    for name,_ in SONGS: text.append('    dw '+','.join(f'dm_song_{name}_{ch}' for ch in range(3)))
    text += ['dm_note_hz:', '    dw '+','.join(map(str,hz))]+lists
    for data,p in phrases.items():
        text += [f'dm_phrase_{p}:', '    db '+','.join(map(str,data))+',0']
    out.mkdir(parents=True,exist_ok=True)
    (out/'dm-music.inc').write_text('\n'.join(text)+'\n')
    report['source_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
    report['phrase_bytes']=sum(len(d)+1 for d in phrases)
    (out/'dm-music.json').write_text(json.dumps(report,indent=2)+'\n')
    print('DrMarco music:',len(phrases),'phrases,',report['phrase_bytes'],'phrase bytes,',len(hz),'pitches')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();build(a.source,a.output)
