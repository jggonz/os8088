"""Cartridge table decoders for the native 1942 engine (no emulation).

Addresses refer to the pinned PRG accepted by 1942nes.Cartridge. Bounds and
terminators are checked even though the payload is digest-pinned. Extracted
records live only in the build directory. Native fallback tables are original.
"""

def position(cart, address):
    # Actor coordinates have a one-bit page, with a 240-line Y page. $B1D7
    # subtracts the screen origin ($80,$70), not ($80,$80).
    return [(cart.read(address)&1)*256+cart.read(address+1)-128,
            (cart.read(address+2)&1)*240+cart.read(address+3)-112]

def vectors(cart, address):
    def signed(word):return word if word<32768 else word-65536
    return [[signed(cart.word(address+i*4)),signed(cart.word(address+i*4+2))]
            for i in range(16)]

def campaign(cart):
    stages=[]
    for stage in range(1,33):
        p=cart.word(0xe26f+(((-stage*8)&255)>>2))
        rows=[]
        for _ in range(128):
            page,y,kind=(cart.read(p+i) for i in range(3));p+=3
            if (page,y,kind)==(0,0,0):break
            if page>7 or y>240 or kind>48:raise ValueError('invalid cartridge encounter')
            # $D9CF compares the scroll register, not the first row below
            # the HUD. Adding the HUD height delays every encounter.
            rows.append([(8-page)*240-y,kind])
        else:raise ValueError('unterminated cartridge stage')
        if rows!=sorted(rows,key=lambda r:r[0]):raise ValueError('unordered cartridge stage')
        stages.append(rows)
    waves=[[cart.read(0xeb19+i*4+j) for j in range(4)] for i in range(49)]
    paths=[]
    for kind in range(10,49):
        p=cart.word(0xfd4a+(kind-10)*2);points=[]
        for _ in range(128):
            if cart.read(p)==255:break
            points.append(position(cart,p));p+=4
        else:raise ValueError('unterminated cartridge path')
        paths.append(points)
    starts=[]
    # $F054 masks with 7, including the unusual eighth entry overlapping the
    # reversal table. Preserve the ROM's masked page bits for that entry.
    for address,count in ((0xf3ec,16),(0xf12e,8),(0xf550,8)):
        starts.append([position(cart,address+i*4) for i in range(count)])
    bonus=[]
    for percent in range(101):
        if percent<50:value=0
        elif percent==100:value=100000
        else:
            address=(0xdfbe if percent%10>=5 else 0xdf58)+(percent//10-5)*8
            value=sum(cart.read(address+i)*factor for i,factor in enumerate((10000,1000,100,10)))
        bonus.append(value)
    # $ADC9 uses a quantized quadrant lookup; $FB00 sequences bomber shots.
    aim=[cart.read(0xb1fb+i) for i in range(256)]
    fire=[]
    for i in range(3):
        p=cart.word(0xfd2f+i*2)
        fire.append([cart.read(p+j) for j in range(cart.read(p)+2)])
    return dict(velocity=vectors(cart,0xb2fb),circle=vectors(cart,0xb33b),
                centers=[position(cart,0xfc53+i*4) for i in range(32)],
                reverse=[cart.read(0xf14a+i) for i in range(16)],
                aim=aim,fire=fire,starts=starts,bonuses=bonus,stages=stages,waves=waves,paths=paths,
                bosses=[(256-cart.read(0xde58+i))//8+1 for i in range(4)])

def sounds(cart):
    """Decode $A25B note/duration stream; preserve restart and explicit rests.

The cartridge $A288 frequency lookup is a big-endian timer shifted by octave.
Noise is reduced to an audible speaker pitch; envelope/duty are deliberately
not represented as PCM. Returned durations are NES frames.
"""
    out=[]
    for sid in range(23):
        start=cart.word(0xa413+sid*2);channel=cart.read(start);p=start+4
        notes=[];loop=False
        for _ in range(512):
            token=cart.read(p);p+=1
            if token>=0xf0:
                cmd=token&15
                if cmd==0:loop=True;break
                if cmd in (2,5,6):p+=1;continue
                break
            duration=cart.read(p);p+=1
            if token>=0xc0:hz=0
            elif channel==4:hz=120+(15-(token&15))*90
            else:
                n=token>>4
                timer=(cart.read(0xa3f9+n*2)<<8)|cart.read(0xa3fa+n*2)
                timer>>=token&15
                hz=round(1789773/((32 if channel==3 else 16)*(timer+1)))
            notes.append([min(12000,hz),max(1,duration)])
        else:raise ValueError('unterminated cartridge sound')
        out.append(dict(notes=notes,loop=loop))
    return out

def fallback():
    import math
    aim=[round(math.atan2(y,x)*8/math.pi) for y in range(16) for x in range(16)]
    waves=[[kind,24,1,8] for kind in (3,4,6,0)]
    waves += [[10,6,1,5],[11,6,1,5],[32,1,1,1],[48,1,1,1]]
    stages=[[[96+i*96,(i+s)%8] for i in range(17)] for s in range(32)]
    paths=[[[32,24],[192,64],[192,176],[32,208],[32,248]]] * 39
    notes=[dict(notes=[[440,6],[660,6],[880,12]],loop=False) for _ in range(23)]
    bonus=[0 if p<50 else 100000 if p==100 else (500,1000,1500,2000,2500,3000,5000,8000,10000,20000)[(p-50)//5] for p in range(101)]
    starts=[[[32+i*24,-16] for i in range(8)] for _ in range(3)]
    return dict(aim=aim,fire=[[1,5,0],[1,5,0],[1,5,0]],starts=starts,bonuses=bonus,stages=stages,waves=waves,paths=paths,bosses=[7,15,23,31]),notes

# ---- Gameplay score -------------------------------------------------------
# Three long looping tracks, chosen by stage tier. Every one is built from the
# Game Over hook (its chromatic A-C#-C-B-Bb turn) plus motifs from the stage
# and fanfare cues, laid out as an intro and then a multi-section form that
# loops from its first main section. One FM voice carries everything, so a
# held note is "arpeggiated" (lead / fifth / third / fifth) to imply harmony.
_NOTE={'C':0,'D':2,'E':4,'F':5,'G':7,'A':9,'B':11}
def _midi(name):
    m=_NOTE[name[0]];i=1
    if name[i:i+1]=='#':m+=1;i+=1
    elif name[i:i+1]=='b':m-=1;i+=1
    return 12*(int(name[i:])+1)+m
def _hz(m):return round(440*2**((m-69)/12))
def _chord(name,shift):
    minor=name.endswith('m');base=name[:-1] if minor else name
    root=_midi(base+'3')+shift
    return root,root+(3 if minor else 4),root+7
def _bar(chord,text,shift):
    """[(midi or None, units, arp_tones or None)] for one bar of 16 units."""
    r,t,f=_chord(chord,shift);out=[]
    for tok in text.split():
        name,units=tok.split(':');arp=units.endswith('+');units=int(units.rstrip('+'))
        out.append((None if name=='R' else _midi(name)+shift,units,(t,f) if arp else None))
    assert sum(u for _,u,_ in out)==16,(chord,text)
    return out
def _events(bars,shift):
    ev=[]
    for chord,text in bars:
        for m,u,arp in _bar(chord,text,shift):
            if arp is None or m is None:ev.append((m,u));continue
            t,f=arp;steps=max(1,u//2);cyc=[m,f,t,f]
            for i in range(steps):ev.append((cyc[i%4],2 if i<steps-1 else u-2*(steps-1)))
    return ev
def _ticks(ev,q):
    out=[];cum=0;done=0
    for m,u in ev:
        cum+=u*q/4;end=int(cum+0.5);t=max(1,end-done);done+=t
        out.append([0 if m is None else _hz(m),t])
    return out
_THEME_LO=[('A','A4:3 A4:1 C#5:4 C#5:2 C5:2 B4:2 Bb4:2'),('A','A4:3 A4:1 C#5:4 E5:4 D5:2 C#5:2'),
           ('F#m','C#5:2 D5:2 F5:2 F#5:2 A5:8+'),('D','F#5:4 D5:2 E5:2 E5:8+')]
_THEME_HI=[('A','A5:3 A5:1 C#6:4 C#6:2 C6:2 B5:2 Bb5:2'),('A','A5:3 A5:1 C#6:4 E6:4 D6:2 C#6:2'),
           ('D','F#5:2 A5:2 D6:4 C#6:4 B5:2 A5:2'),('E','G#5:4 B5:4 E6:8+')]
_THEME_LOW=[('A','A4:3 A4:1 C#5:4 C#5:2 C5:2 B4:2 Bb4:2'),('A','A4:3 A4:1 C#5:4 E5:4 D5:2 C#5:2'),
            ('D','F#5:2 A5:2 D6:4 C#6:4 B5:2 A5:2'),('E','G#5:4 B5:4 E5:8+')]
_MARCH=[('Am','A4:2 G#4:2 G4:2 F#4:2 F4:2 G4:2 A4:2 B4:2'),
        ('Am','E5:2 D5:2 C#5:2 B4:2 A4:2 B4:2 E5:2 F5:2'),
        ('F','A4:2 C5:2 F5:2 C5:2 A4:2 C5:2 F5:2 A5:2'),
        ('E','G#4:2 B4:2 E5:2 B4:2 G#4:2 B4:2 E5:2 G#5:2'),
        ('Am','A5:2 G#5:2 G5:2 F#5:2 F5:2 G5:2 A5:2 B5:2'),
        ('Dm','D5:2 F5:2 A5:2 F5:2 D5:2 F5:2 A5:2 D6:2'),
        ('E','E5:2 G#5:2 B5:2 G#5:2 E5:2 D5:2 B4:2 G#4:2'),
        ('Am','A4:4 R:2 A4:2 C5:2 E5:2 A5:4+')]
_BRIDGE=[('D','F#5:8+ A5:8+'),('E','G#5:8+ B5:8+'),('C#m','E5:8+ G#5:4 E5:4'),
         ('F#m','C#6:8+ A5:4 F#5:4'),('D','D6:8+ A5:8+'),('E','B5:4 G#5:4 E5:4 B4:4'),
         ('A','C#6:8+ E6:8+'),('E','E5:2 F#5:2 G#5:2 A5:2 B5:8+')]
_FANFARE=[('A','E5:2 F#5:2 E5:4 A5:8+'),('D','A5:2 B5:2 C#6:2 D6:2 E6:8+'),
          ('F#m','C#6:4 A5:4 F#5:4 A5:4'),('E','B5:8+ G#5:4 B5:4'),
          ('A','E6:2 F#6:2 E6:4 A6:8+'),('D','D6:4 A5:4 F#5:4 D5:4'),
          ('E','E5:2 G#5:2 B5:2 E6:2 G#6:8+'),('A','A5:16+')]
_INTRO=[('A','A4:16+'),('E','E5:8+ B4:8+')]
def _track(q,shift,plan,loop):
    ev=[];mark=0
    for i,(sect) in enumerate(plan):
        if i==loop:mark=len(ev)
        ev+=_events(sect,shift)
    notes=_ticks(ev,q);idx=sum(1 for _ in _events([x for s in plan[:loop] for x in s],shift))
    return dict(notes=notes+[[0,max(2,q)]],loop=True,loop_at=idx)
def gameplay_tracks():
    """Three loops, ~75-100 s each, for stage tiers 1-10, 11-21 and 22-32."""
    a=_track(8,0,[_INTRO,_THEME_LO+_THEME_HI,_THEME_HI+_THEME_LOW,_BRIDGE,_MARCH,_FANFARE,_THEME_HI+_THEME_LO],1)
    b=_track(7,3,[_INTRO,_FANFARE,_MARCH,_THEME_LO+_THEME_HI,_BRIDGE,_FANFARE],1)
    c=_track(6,-3,[_INTRO,_MARCH,_THEME_LOW+_THEME_HI,_MARCH,_BRIDGE,_FANFARE,_THEME_HI+_THEME_LO],1)
    return [a,b,c]

def assembly(data,audio):
    out=['; Build-time cartridge/native gameplay tables.']
    for name in ('velocity','circle','centers','reverse'):
        if name in data:
            out.append('n_'+name+':')
            for row in data[name]:
                out.append('    dw '+','.join(map(str,row if isinstance(row,list) else [row])))
    out.append('n_aimtable: db '+','.join(map(str,data['aim'])))
    out.append('n_firepatterns: dw n_firepattern0,n_firepattern1,n_firepattern2')
    for i,pattern in enumerate(data['fire']):
        out.append('n_firepattern%d: db '%i+','.join(map(str,pattern)))
    out.append('n_smallstarts: dw n_starts0,n_starts1,n_starts2')
    out.append('n_startcounts: dw '+','.join(str(len(x)) for x in data['starts']))
    for i,positions in enumerate(data['starts']):
        out.append('n_starts%d:'%i)
        out.extend('    dw %d,%d'%tuple(p) for p in positions)
    out.append('n_bonuses: dd '+','.join(map(str,data['bonuses'])))
    out.append('n_stageevents: dw '+','.join('n_events%d'%i for i in range(32)))
    for i,events in enumerate(data['stages']):
        out.append('n_events%d:'%i)
        out.extend('    dw %d,%d'%tuple(e) for e in events)
        out.append('    dw 65535,0')
    out.append('n_waves:')
    out.extend('    dw '+','.join(map(str,w)) for w in data['waves'])
    out.append('n_paths: dw '+','.join('n_path%d'%i for i in range(39)))
    for i,points in enumerate(data['paths']):
        out.append('n_path%d:'%i)
        out.extend('    dw %d,%d'%tuple(p) for p in points)
        out.append('    dw 32767,32767')
    out.append('n_soundtab: dw '+','.join('n_sound%d'%i for i in range(len(audio))))
    for i,s in enumerate(audio):
        out.append('n_sound%d:'%i)
        if 'loop_at' in s:                     # gameplay tracks: ticks, interior loop
            for j,(hz,t) in enumerate(s['notes']):
                if j==s['loop_at']:out.append('n_sound%d_loop:'%i)
                out.append('    dw %d,%d'%(hz,t))
            out.append('    dw 65535,n_sound%d_loop'%i)
            continue
        out.extend('    dw %d,%d'%(hz,max(1,(t*182+300)//600)) for hz,t in s['notes'])
        out.append('    dw 65535,%s'%('n_sound%d'%i if s['loop'] else '0'))
    return out
