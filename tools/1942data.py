"""Cartridge table decoders for the native 1942 engine (no emulation).

Addresses refer to the pinned PRG accepted by 1942nes.Cartridge. Bounds and
terminators are checked even though the payload is digest-pinned. Extracted
records live only in the build directory. Native fallback tables are original.
"""

def campaign(cart):
    stages=[]
    for stage in range(1,33):
        p=cart.word(0xe26f+(((-stage*8)&255)>>2))
        rows=[]
        for _ in range(128):
            page,y,kind=(cart.read(p+i) for i in range(3));p+=3
            if (page,y,kind)==(0,0,0):break
            if page>7 or y>240 or kind>48:raise ValueError('invalid cartridge encounter')
            rows.append([16+(7-page)*240+240-y,kind])
        else:raise ValueError('unterminated cartridge stage')
        if rows!=sorted(rows,key=lambda r:r[0]):raise ValueError('unordered cartridge stage')
        stages.append(rows)
    waves=[[cart.read(0xeb19+i*4+j) for j in range(4)] for i in range(49)]
    paths=[]
    for kind in range(10,49):
        p=cart.word(0xfd4a+(kind-10)*2);points=[]
        for _ in range(128):
            if cart.read(p)==255:break
            x=cart.read(p)*256+cart.read(p+1)-128
            y=cart.read(p+2)*240+cart.read(p+3)-128
            points.append([x,y]);p+=4
        else:raise ValueError('unterminated cartridge path')
        paths.append(points)
    starts=[]
    for address,count in ((0xf3ec,16),(0xf12e,7),(0xf550,8)):
        starts.append([[cart.read(address+i*4)*256+cart.read(address+i*4+1)-128,
                        cart.read(address+i*4+2)*240+cart.read(address+i*4+3)-128] for i in range(count)])
    bonus=[]
    for percent in range(101):
        if percent<50:value=0
        elif percent==100:value=100000
        else:
            address=(0xdfbe if percent%10>=5 else 0xdf58)+(percent//10-5)*8
            value=sum(cart.read(address+i)*factor for i,factor in enumerate((10000,1000,100,10)))
        bonus.append(value)
    return dict(starts=starts,bonuses=bonus,stages=stages,waves=waves,paths=paths,
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
    waves=[[kind,24,1,8] for kind in (3,4,6,0)]
    waves += [[10,6,1,5],[11,6,1,5],[32,1,1,1],[48,1,1,1]]
    stages=[[[96+i*96,(i+s)%8] for i in range(17)] for s in range(32)]
    paths=[[[32,24],[192,64],[192,176],[32,208],[32,248]]] * 39
    notes=[dict(notes=[[440,6],[660,6],[880,12]],loop=False) for _ in range(23)]
    notes[14]=dict(notes=[[440,12],[0,6],[660,12],[440,12],[880,24]],loop=True)
    bonus=[0 if p<50 else 100000 if p==100 else (500,1000,1500,2000,2500,3000,5000,8000,10000,20000)[(p-50)//5] for p in range(101)]
    starts=[[[32+i*24,-16] for i in range(8)] for _ in range(3)]
    return dict(starts=starts,bonuses=bonus,stages=stages,waves=waves,paths=paths,bosses=[7,15,23,31]),notes

def gameplay_music(game_over):
    """A quieter-register, flowing loop of the Game Over melody (NES frames).

    Keep the source cue intact. Shorten its held cadences for a repeatable
    phrase and discard the decoder's clipped one-frame tail, which would
    otherwise become a shrill chirp on every lap.
    """
    notes=[[(hz+1)//2,max(1,(min(t,56)*3+2)//4)]
           for hz,t in game_over['notes'] if not (hz==12000 and t==1)]
    return dict(notes=notes+[[0,14]],loop=True)

def assembly(data,audio):
    out=['; Build-time cartridge/native gameplay tables.']
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
        out.extend('    dw %d,%d'%(hz,max(1,(t*182+300)//600)) for hz,t in s['notes'])
        out.append('    dw 65535,%s'%('n_sound%d'%i if s['loop'] else '0'))
    return out
