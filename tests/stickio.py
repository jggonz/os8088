#!/usr/bin/env python3
"""Stickio: compiled data, guest physics, input, pixel oracle, and XT frame costs.
Run make stickiocheck (host) or python3 tests/stickio.py --adapter cga|herc|vga.
"""
from pathlib import Path
import os, array
import argparse, json, math, re, struct, subprocess, sys, tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
import os88build as B

def at(p): return Path(B.at(p))
def symbols():
    src=(ROOT/'apps/stickio/stickio.asm').read_text()
    names=re.findall(r'^VAR (st_\w+),',src,re.M)
    for file in ('stickio.asm','game.inc','actors.inc','video.inc','audio.inc'):
        names+=re.findall(r'^(st_\w+)(?::| equ \$)',(ROOT/'apps/stickio'/file).read_text(),re.M)
    names+=re.findall(r'^(st_\w+):',at('build/stickio-art/assets.inc').read_text(),re.M)
    names=sorted(set(names))
    with tempfile.TemporaryDirectory() as td:
        p=Path(td)/'probe.asm'; b=Path(td)/'probe.bin'
        p.write_text(src+'\n'+'\n'.join('dw '+n for n in names))
        subprocess.run(['nasm','-f','bin','-I',str(ROOT/'apps')+'/',
            '-I',str(ROOT/'apps/stickio')+'/', '-I',str(at('build/stickio-art'))+'/',
            '-o',str(b),str(p)],check=True)
        return dict(zip(names,struct.unpack('<%dH'%len(names),b.read_bytes()[-len(names)*2:])))

def host():
    sym=symbols(); raw=at('build/stickio.bin').read_bytes()
    image,bss=struct.unpack_from('<HH',raw,8)
    assert image==len(raw) and image+bss<=61440-2048
    from stickio_p0 import host as p0_host
    p0_host(raw, sym)
    levels=json.loads(at('build/stickio-art/levels.json').read_text())
    assert len(levels)==30 and len(set(l['name'] for l in levels))==30
    previous=0
    for i,l in enumerate(levels):
        width,mp,ep,np=struct.unpack_from('<4H',raw,sym['st_level%d'%i])
        assert width==l['width']>=previous;previous=width
        dec=bytearray();p=mp
        while len(dec)<width*8:
            count,value=raw[p:p+2];p+=2
            assert count>0 and value<10
            dec.extend([value]*count)
        assert bytes(dec)==bytes(v for c in l['map'] for v in c)
        assert l['map'][width-4][5]==7 and l['map'][width//2][5]==8
        assert l['map'][2][6]==1 and not any(l['map'][2][:6])
        # Walkable floor gaps cannot exceed the walking jump's 60px reach.
        gaps=0
        for col in l['map']:
            gaps=gaps+1 if not col[6] else 0
            assert gaps<=3
        assert len(l['enemies'])<=20
    sprite=raw[sym['st_sprites']:sym['st_sprites']+30*96]
    assert len({sprite[i*96:(i+1)*96] for i in range(12)})==12
    def pixels(b): return [(x>>k)&3 for x in b for k in (6,4,2,0)]
    for i in range(12):
        right=pixels(sprite[i*96:(i+1)*96]); left=pixels(sprite[(i+12)*96:(i+13)*96])
        assert all(v in (0,3) for v in right)
        assert left==[v for y in range(24) for v in right[y*16:y*16+16][::-1]]
    assert len(set(struct.unpack_from('<32H',raw,sym['st_tune%d'%i]) for i in range(6)))==6
    print('host: 30 RLE courses, bounded gaps/enemies, 24 distinct mirrored human poses, 6 themes;',image,'image +',bss,'BSS')

class Probe:
    def __init__(self,ui,sym):
        import os88geom as G
        self.m=ui.m;self.sym=sym
        w=ui.window('Stickio');raw=self.m.read(ui.sym('wm_wins'),G.MAX_WIN*G.WIN_SIZE)
        self.base=struct.unpack_from('<H',raw,w.i*G.WIN_SIZE+G.W_SEG)[0]<<4
    def a(self,n):return self.base+self.sym['st_'+n]
    def data(self,n,size=1):return self.m.read(self.a(n),size)
    def b(self,n):return self.data(n)[0]
    def w(self,n):return struct.unpack('<H',self.data(n,2))[0]
    def put(self,n,value,size=1):self.m.write(self.a(n),int(value).to_bytes(size,'little',signed=value<0))
    def call(self,n,observe=None,**args):
        import os88marty as M
        m=self.m;saved=m.regs();regs=('ax','bx','cx','dx','si','di','bp','sp','ss','ds','es','flags')
        m.cmd(cmd='park',cs=self.base>>4,ip=self.sym['st_'+n])
        for r in regs:m.setreg(r,args.get(r,saved[r]))
        sp=(saved['sp']-2)&65535;m.setreg('sp',sp)
        m.write((saved['ss']<<4)+sp,struct.pack('<H',saved['ip']))
        start=m.status()['cycles'];ret=(saved['cs']<<4)+saved['ip']
        marks={self.a(v):v for v in ('present','transfer')} if n=='render' else {}
        m.bp_exec(ret,*marks);m.run();times={}
        while True:
            assert m.wait_stop(30)=='breakpoint',n+' failed to return'
            st=m.status();addr=(st['cs']<<4)+st['ip']
            if addr==ret:break
            mark=marks[addr];times[mark]=st['cycles']
            if observe:observe(mark)
            m.run()
        elapsed=m.status()['cycles']-start
        if times:elapsed-=times['transfer']-times['present']
        for r in regs:m.setreg(r,saved[r])
        return elapsed

def pixelcheck(p,tag):
    """Independently render every terrain pixel from the source tile matrices.
    Compare background and all composited sprite footprints against physical VRAM.
    """
    import stickio_assets as A
    ts=A.tiles();cam=p.w('cam');width=p.w('width');mp=p.data('map',width*8)
    expected=bytearray()
    for y in range(128):
        row=[ts[mp[((x+cam)//16)*8+y//16]][y%16][(x+cam)%16] for x in range(320)]
        expected.extend(A.pack([row]))
    final=bytearray(expected)
    art=p.data('sprites',30*96)
    objects=[]
    py=p.w('y');py=(py-65536 if py&32768 else py)//256
    vy=p.w('vy');vy=vy-65536 if vy&32768 else vy
    if not p.b('invuln') or not p.b('anim')&4:
        pose=(11 if p.b('land') else (p.w('gait')//3)&7 if p.w('vx') else 8) if p.b('ground') else 9 if vy<0 else 10
        objects.append((p.w('x')-cam,py,pose+12*p.b('facing')))
    nearby=0
    for n in range(p.w('ne')):
        x,y,t,d,age,*_=struct.unpack_from('<HHBbHHHHH',p.data('enemies',p.w('ne')*16),n*16)
        if t and 0<=x-cam<=304 and nearby<6:
            objects.append((x-cam,y,24+2*(t-1)+bool(age&8)));nearby+=1
    for x,y,pose in objects:
        if not(0<=x<=304 and -23<=y<=127):continue
        xb=(x&~3)//4
        for row in range(24):
            if not 0<=y+row<128:continue
            for col in range(4):final[(y+row)*80+xb+col]&=art[pose*96+row*4+col]
    assert p.data('bg',80*128)==final,'RAM composition mismatch'
    # Saved footprints must recover pristine terrain even where sprites overlap.
    restored=bytearray(final)
    boxes=p.data('oldboxes',p.w('nboxes')*4)
    saved=p.data('saved',p.w('nboxes')*96)
    for n in reversed(range(p.w('nboxes'))):
        x,y=struct.unpack_from('<Hh',boxes,n*4)
        for row in range(24):
            if 0<=y+row<128:
                off=(y+row)*80+x//4
                restored[off:off+4]=saved[n*96+row*4:n*96+row*4+4]
    assert restored==expected,'saved terrain restoration mismatch'
    if tag=='vga':return  # VGA's planar debug peek is covered by rastercheck below.
    vram=p.m.read(0xb0000 if tag=='herc' else 0xb8000,32768 if tag=='herc' else 16384)
    for y in range(128):
        sy=y+32
        off=((sy*2)%4)*8192+((sy*2)//4)*90+5 if tag=='herc' else (sy%2)*8192+(sy//2)*80
        assert vram[off:off+80]==final[y*80:y*80+80],('composited pixels',tag,p.w('level'),cam,y,p.data('dirty',256)[y*2:y*2+2].hex(),vram[off:off+8].hex(),final[y*80:y*80+8].hex(),[(i,a,b) for i,(a,b) in enumerate(zip(vram[off:off+80],final[y*80:y*80+80])) if a!=b])
        if tag=='herc':
            off=((sy*2+1)%4)*8192+((sy*2+1)//4)*90+5
            assert vram[off:off+80]==final[y*80:y*80+80],('second Hercules row',y)

def rastercheck(p,tag,proof):
    """Check the card's actual display, including VGA's planar CGA emulation."""
    # MartyPC's MDA framebuffer does not rasterize Hercules graphics; the
    # independent VRAM oracle above checks both physical Hercules scanlines.
    if tag=='herc':return
    import os88marty as M
    m=p.m;p.put('pause',1);p.put('huddirty',1)
    m.bp_exec();m.advance(frames=4)
    w,h,rgb=m.fbuf(0)
    assert (w,h) in ((640,200),(640,400)),(tag,w,h)
    bg=p.data('bg',80*128)
    xscale=2;yscale=h//200
    for y in range(128):
        row=bytes(v for b in bg[y*80:y*80+80] for k in (6,4,2,0)
                  for v in ([255]*6 if (b>>k)&3 else [0]*6))
        for sy in range((y+32)*yscale,(y+33)*yscale):
            off=sy*w*3
            assert rgb[off:off+320*xscale*3]==row,(tag,'display raster',y,sy)
    M.write_png_rgb(str(proof/(tag+'-raster.png')),w,h,rgb)
    m.bp_exec(p.a('input'));m.run();assert m.wait_stop(30)=='breakpoint'
    p.put('pause',0);p.put('huddirty',1)

def guest(tag):
    import os88marty as M,os88ui
    sym=symbols();proof=at('build/stickio-proof');proof.mkdir(exist_ok=True)
    machine={'cga':'os8088_5150_cga_gla','herc':'os8088_5150_herc_gla','vga':'os8088_xt_vga'}[tag]
    with os88ui.boot(str(at('build/os8088-360.img')),apps=str(at('build/stickio360.img')),machine=machine,settle=False) as ui:
        m=ui.m;ui.open_drive('B');ui.open('STICKIO.O88');p=Probe(ui,sym)
        m.pause();m.bp_exec(p.a('input'));m.key('Enter');m.run()
        assert m.wait_stop(30)=='breakpoint','fullscreen did not open'
        assert p.b('fs')==1
        # Keep the live loop parked at input while executing guest routines.
        levels=json.loads(at('build/stickio-art/levels.json').read_text())
        from stickio_p0 import guest as p0_guest
        p0_guest(p)
        costs=[]
        for i,l in enumerate(levels):
            p.put('level',i,2);p.put('checkpoint',32,2);p.call('load')
            assert p.data('map',l['width']*8)==bytes(v for c in l['map'] for v in c)
            assert p.w('ne')==len(l['enemies']) and p.w('x')==32 and p.w('y')==72*256
            assert p.data('enemies',p.w('ne')*16)==b''.join(
                struct.pack('<HHBbHHHHH',x,y,kind,direction,0,0,0,y,y)
                for x,y,kind,direction in l['enemies']), 'authored actor reconstruction'
            p.put('invuln',0);p.call('render');pixelcheck(p,tag)
            # Camera phases and tile changes exercise the incremental path.
            for cam in (4,12,32,(l['width']-20)*16):
                p.put('cam',cam,2);p.call('render');pixelcheck(p,tag)
            p.put('cam',0,2);p.call('render')
            p.put('cam',4,2);costs.append(p.call('render'))
        p.put('level',0,2);p.put('checkpoint',32,2);p.call('load')
        for facing in (0,1):
            for pose in range(12):
                p.put('invuln',0);p.put('facing',facing);p.put('ground',pose not in (9,10))
                p.put('land',5 if pose==11 else 0);p.put('vx',448 if pose<8 else 0,2)
                p.put('gait',pose*3,2);p.put('vy',-100 if pose==9 else 100 if pose==10 else 0,2)
                p.call('render');pixelcheck(p,tag)
        # Overlap, clipping, changed tiles beneath old masks, and scroll restoration.
        p.call('load');p.put('invuln',0);p.put('ne',6,2)
        for x,y in ((32,72),(36,72),(0,-12),(304,120)):
            p.put('x',x,2);p.put('y',y*256,2)
            m.write(p.a('enemies'),b''.join(struct.pack('<HHBbHHHHH',ex,ey,t,1,age,0,0,ey,ey)
                    for ex,ey,t,age in ((32,72,1,0),(36,76,2,8),(40,72,3,0),
                                       (0,0,1,8),(304,126,2,0),(160,60,3,8))))
            p.call('render');pixelcheck(p,tag)
        m.write(p.a('map')+2*8+4,b'\x02');p.put('mapdirty',1)
        p.put('cam',4,2);p.call('render');pixelcheck(p,tag)
        # The old video frame must survive RAM restoration and composition.
        p.call('load');p.put('invuln',0);p.put('ne',0,2);p.call('render')
        video=0xb0000 if tag=='herc' else 0xb8000
        videosize=32768 if tag=='herc' else 16384
        before=m.read(video,videosize);p.put('x',36,2)
        marks=[]
        def before_transfer(mark):
            marks.append(mark)
            assert m.read(video,videosize)==before,'visible erase during RAM composition'
        p.call('render',observe=before_transfer)
        assert marks==['present','transfer']
        assert m.read(video,videosize)!=before,'new pose was not transferred'
        pixelcheck(p,tag)
        rastercheck(p,tag,proof)
        p.call('load')
        p.put('invuln',0);p.put('keys',2)
        x0=p.w('x')
        for _ in range(20):p.call('step')
        assert p.w('x')>x0+20 and p.w('y')==72*256,'walking/gravity'
        p.put('keys',6);p.put('jumpheld',0);p.call('step')
        assert p.w('vy')>32768 and p.w('y')<72*256,'jump did not rise'
        for _ in range(8):p.call('step')
        high=p.w('y');p.put('keys',2);p.call('step')
        assert p.w('vy')>=65536-640 or p.w('vy')<32768,'variable-height release'
        for _ in range(35):p.call('step')
        assert p.w('y')>=high,('jump did not fall',high,p.w('y'),p.w('x'),p.w('vy'))
        # Question block: use the game, hit the underside from below.
        p.call('load');p.put('x',32,2);p.put('y',66*256,2);p.put('vy',-1000,2)
        p.put('ground',0);p.put('coyote',0);p.put('keys',4)
        m.write(p.a('map')+2*8+3,b'\x03')
        p.call('step');assert p.w('coins')>=1 and p.data('map',24)[19]==2,'question block reward'
        # Spring: falling feet cross the pad; bounce must launch upwards.
        p.call('load');p.put('x',32,2);p.put('y',55*256,2);p.put('vy',256,2);p.put('ground',0)
        m.write(p.a('map')+2*8+5,b'\x06');p.call('step')
        assert p.w('vy')==65536-2176,('spring launch',p.w('y'),p.w('vy'),p.b('ground'))
        # One-way ledges allow upward passage and catch crossed descending feet.
        p.call('load');p.put('x',32,2);p.put('y',39*256,2);p.put('vy',512,2);p.put('ground',0)
        m.write(p.a('map')+2*8+4,b'\x09');p.call('step')
        assert p.w('y')==40*256 and p.b('ground')==1,'one-way landing'
        p.put('y',64*256,2);p.put('vy',-1000,2);p.put('ground',0);p.put('keys',4)
        p.call('step');assert p.w('y')<64*256 and p.w('vy')>32768,'one-way ascent'
        # Stomp and side contact exercise enemy collision rather than the reset helper.
        p.call('load');p.put('ne',1,2);p.put('x',64,2);p.put('y',55*256,2);p.put('vy',1024,2)
        p.put('ground',0);p.put('keys',0);p.put('invuln',0)
        m.write(p.a('enemies'),struct.pack('<HHBbHHHHH',64,72,1,1,0,0,0,72,72));p.call('step')
        assert p.data('enemies',5)[4]==0 and p.w('vy')==65536-1152,'enemy stomp'
        p.call('load');p.put('ne',1,2);p.put('x',64,2);p.put('lives',3);p.put('invuln',0)
        m.write(p.a('enemies'),struct.pack('<HHBbHHHHH',64,72,1,1,0,0,0,72,72));p.call('step')
        assert p.b('lives')==2 and p.b('invuln')>0,'enemy side contact'
        p.call('newcourse');p.call('load');p.put('lives',3);p.put('x',32,2)
        m.write(p.a('map')+2*8+5,b'\x05');p.call('step');assert p.b('lives')==2,'spike contact'
        p.call('load');p.put('lives',3);p.put('y',122*256,2);p.put('vy',1024,2);p.put('ground',0)
        for _ in range(2):p.call('step')
        assert p.b('lives')==2 and p.w('y')==72*256,'pit death must precede signed overflow'
        # Death, checkpoint respawn, finite lives and completion are real state transitions.
        p.put('checkpoint',128,2);p.put('lives',3);p.call('respawn');assert p.w('x')==128 and p.b('lives')==2
        p.put('lives',1);p.call('respawn');assert p.b('state')==2 and p.b('lives')==0
        p.put('lives',5);p.put('checkpoint',32,2);p.call('load')
        p.put('x',(p.w('width')-4)*16,2);p.call('step');assert p.b('state')==1
        p.put('level',29,2);p.call('load');p.put('x',(p.w('width')-4)*16,2);p.call('step');assert p.b('state')==3
        p.put('level',0,2);p.put('checkpoint',32,2);p.call('load');p.put('invuln',0);p.call('render')
        # Actual BIOS key delivery: arrows (AL=0), space, and Z held/tapped.
        def sample_key(key,down=True,up=True):
            m.bp_exec(p.a('input'));m.key(key,down=down,up=up);m.run()
            assert m.wait_stop(30)=='breakpoint'
            p.call('input')
        for key,held in (('ArrowRight',2),('ArrowLeft',1),('ArrowUp',0),('ArrowDown',0),('Space',0)):
            p.call('load');p.put('jumpheld',0)
            sample_key(key,up=False)
            assert p.b('keys')&3==held,(key,'movement')
            assert not p.b('keys')&4 and p.b('jumpbuf')==0,(key,'unexpected jump input')
            p.call('step')
            assert p.w('y')==72*256 and p.w('vy')==0,(key,'unexpected jump')
            # Repeated make events model typematic while movement stays held.
            sample_key(key,up=False)
            assert p.b('jumpbuf')==0,(key,'typematic jump')
            sample_key(key,down=False)
        for tapped in (False,True):
            p.call('load');p.put('jumpheld',0)
            sample_key('KeyZ',up=tapped)
            assert p.b('keys')&4 or p.b('jumpbuf')>0,'Z press lost'
            p.call('step')
            assert p.w('y')<72*256 and p.w('vy')>32768,'Z did not jump'
            if not tapped:
                p.put('y',72*256,2);p.put('vy',0,2);p.put('ground',1)
                p.call('step')
                assert p.w('y')==72*256,'held Z retriggered on landing'
                sample_key('KeyZ',down=False)
        for key,field in (('KeyP','pause'),('KeyM','mute')):
            sample_key(key,up=False);assert p.b(field)==1,(key,'command edge lost')
            sample_key(key,down=False)
            sample_key(key,up=False);assert p.b(field)==0,(key,'command did not toggle back')
            sample_key(key,down=False)
        p.call('load');p.put('invuln',0);p.call('render')
        m.bp_exec(p.a('presented'));m.run();assert m.wait_stop(30)=='breakpoint'
        # Capture a complete stable card image, independent of the beam's position.
        vram=m.read(0xb0000 if tag=='herc' else 0xb8000,32768 if tag=='herc' else 16384)
        if tag=='vga':
            w,h,rgb=m.fbuf(0)
        elif tag=='herc':
            w,h=720,348
            rgb=bytes(v for y in range(h) for x in range(w) for v in ([255]*3 if (vram[(y%4)*8192+(y//4)*90+x//8]>>(7-x%8))&1 else [0]*3))
        else:
            w,h=640,400
            rgb=bytes(v for y in range(h) for x in range(w) for v in ([255]*3 if (vram[((y//2)%2)*8192+((y//2)//2)*80+(x//2)//4]>>(6-2*((x//2)%4)))&3 else [0]*3))
        M.write_png_rgb(str(proof/(tag+'-play.png')),w,h,rgb)
        # A scroll and all visible enemies: measure actual work excluding deliberate waits.
        framecycles=[]
        for _ in range(40):
            p.put('keys',10);p.put('invuln',90)
            sim=p.call('step');draw=p.call('render');framecycles.append(sim+draw)
        result={'render_scroll_mean_clocks':round(sum(costs)/len(costs)),
                'work_mean_clocks':round(sum(framecycles)/len(framecycles)),
                'work_max_clocks':max(framecycles),'work_fps':round(M.GUEST_HZ/(sum(framecycles)/len(framecycles)),1)}
        p.put('x',160,2);p.put('keys',10);p.call('camera');p.call('render')
        m.key('ArrowRight',down=True,up=False);m.key('KeyX',down=True,up=False)
        scrollframe=[0]
        simulation_marks=[]
        def advance_camera(_,record):
            simulation_marks.append((p.w('steps'),p.w('truncated'),p.w('dropped'),
                                     p.w('actor_overflow'),p.w('clocklast')))
            scrollframe[0]+=1
            p.put('x',120+scrollframe[0]*4,2);p.put('invuln',90)
        with M.bp_trace(m,p.a('presented'),cap=81,on_hit=advance_camera) as tr:
            tr.until(lambda:len(tr.hits)>=80,'80 live frames',limit=60)
        clocks=[v['cycles'] for v in tr.hits]
        periods=[b-a for a,b in zip(clocks,clocks[1:])]
        result['live_scroll_fps']=round(M.GUEST_HZ/(sum(periods)/len(periods)),1)
        result['live_mean_period_clocks']=round(sum(periods)/len(periods))
        result['live_p99_period_clocks']=sorted(periods)[math.ceil(len(periods)*.99)-1]
        result['retained_presentations']=len(clocks)
        # The pump can observe extra hits while until() exits; its capped trace
        # retains fewer records than the callback. Pair counters with the last
        # retained cycle sample, rather than with a later unrecorded frame.
        first,last=simulation_marks[0],simulation_marks[len(clocks)-1]
        result['catchup_truncations']=(last[1]-first[1])&65535
        result['dropped_steps']=(last[2]-first[2])&65535
        result['simulated_time_ratio']=round(((last[0]-first[0])&65535)*87380/(clocks[-1]-clocks[0]),4)
        result['actor_overflow_events']=(last[3]-first[3])&65535
        assert (((last[0]-first[0])&65535)+result['dropped_steps'])&65535 == ((last[4]-first[4])&65535), 'simulation step accounting'
        m.key('ArrowRight',down=False,up=True);m.key('KeyX',down=False,up=True)
        assert max(framecycles)<M.GUEST_HZ/12,'XT minimum 12 Hz work budget exceeded'
        m.bp_exec();m.key('Escape');m.run();M.until(m,lambda _:p.b('fs')==0,'desktop restore',guest=30)
        assert ui.window('Stickio').visible
        (proof/(tag+'-results.json')).write_text(json.dumps(result,indent=2)+'\n')
        print(tag,'30 levels, incremental pixels, physics, input, exit:',result,flush=True)

def sound(backend):
    import os88marty as M,os88ui
    import importlib.util
    spec=importlib.util.spec_from_file_location('stickio_audio_helpers',ROOT/'tests/drmario_audio.py')
    helpers=importlib.util.module_from_spec(spec);spec.loader.exec_module(helpers)
    adlib_run,driver_symbols=helpers.adlib_run,helpers.driver_symbols
    out=at('build/stickio-proof').resolve();out.mkdir(exist_ok=True)
    os.environ['MARTYPC_WAV']=str(out/('audio-'+backend))
    machine='os8088_xt_vga' if backend=='speaker' else 'os8088_xt_vga_sb'
    with adlib_run(backend) as kw,os88ui.boot(str(at('build/os8088-360.img')),
            apps=str(at('build/stickio360.img')),machine=machine,settle=False,**kw) as ui:
        m=ui.m;ui.open_drive('B');ui.open('STICKIO.O88');p=Probe(ui,symbols())
        m.pause();m.bp_exec(p.a('input'));m.key('Enter');m.run();assert m.wait_stop(30)=='breakpoint'
        assert p.b('fm')==(backend!='speaker'),('FM selection',backend,p.b('fm'),p.w('caps'))
        assert p.b('pcm')==(backend=='sb'),('PCM selection',backend,p.b('pcm'),p.w('caps'))
        ds=driver_symbols();seg=struct.unpack('<H',m.read(m.sym('drv_fseg'),2))[0]<<4
        if backend!='speaker':assert all(v!=255 for v in m.read(seg+ds['opl_own'],4))
        # Every theme sends pitches; every effect goes through the actual backend.
        for world in range(6):
            p.put('level',world*5,2);p.call('load');p.put('musicwait',0);p.put('musicpos',0,2)
            m.bp_exec();m.run();M.pace(m,0.6);m.pause()
            m.bp_exec(p.a('input'));m.step();m.run();assert m.wait_stop(30)=='breakpoint'
            for effect in range(1,6):
                p.call('effect',ax=effect)
                if backend=='sb':assert p.b('stream')==1,('DMA effect rejected',effect)
                m.bp_exec();m.run();M.pace(m,.25);m.pause()
                m.bp_exec(p.a('input'));m.step();m.run();assert m.wait_stop(30)=='breakpoint'
        p.put('pause',1);p.call('audio_silence')
        assert p.b('stream')==0 and p.b('fxwait')==0
        if backend!='speaker':assert all(not(v&32) for v in m.read(seg+ds['opl_b0'],4))
        p.put('mute',1);p.call('effect',ax=2);assert p.b('stream')==0 and p.b('fxwait')==0
        m.bp_exec();m.key('Escape');m.run();M.until(m,lambda _:p.b('fs')==0,'silent desktop return',guest=30)
        assert p.b('pcm')==0
        if backend!='speaker':assert all(v==255 for v in m.read(seg+ds['opl_own'],4))
    peaks={}
    for path in out.glob('audio-'+backend+'.*.wav'):
        raw=bytearray(path.read_bytes());samples=array.array('h',raw[44:])
        peaks[path.name]=max(map(abs,samples),default=0)
        struct.pack_into('<I',raw,4,len(raw)-8);struct.pack_into('<I',raw,40,len(raw)-44);path.write_bytes(raw)
    sink='pc_speaker' if backend=='speaker' else 'adlib_music_synthesizer'
    assert peaks['audio-'+backend+'.'+sink+'.wav']>100,peaks
    if backend=='sb':assert peaks['audio-sb.sound_blaster.wav']>100,peaks
    print(backend,'6 themes, 5 effects, pause/mute/release, captured audio:',peaks,flush=True)

if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('--host',action='store_true');a.add_argument('--adapter',choices=('cga','herc','vga'),default='cga');a.add_argument('--sound',choices=('speaker','adlib','sb'));ns=a.parse_args()
    host() if ns.host else sound(ns.sound) if ns.sound else guest(ns.adapter)
