#!/usr/bin/env python3
"""Standalone native graphics and XT performance gate: make 1942test.

Real 8088 instructions on MartyPC, VGA and genuine CGA. Fixtures exercise
collisions, grace/roll immunity, pickups, bosses and completion. Every display
byte is checked against an independent presenter, including the bottom 40
Mode X rows. Also compares incremental composition to a full refresh.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import os88marty as M
import os88ui
import os88geom as G
import os88build


def rom_build():
    inc=(ROOT / os88build.at('build/1942art.inc')).read_text()
    if 'N_ROM equ 1' not in inc:return None
    p=(ROOT / os88build.at('build/.1942source')).read_text()
    return ROOT/p


def assets():
    bank=(ROOT/os88build.at('build/1942.SFX')).read_bytes()
    assert struct.unpack('<4s6H',bank[:16])==(b'N42S',8208,sum(bank[16:])&65535,1024,3072,4096,8000)
    samples=[]
    for name in ('shot','explosion','loss'):
        with wave.open(str(ROOT/'apps/1942/sfx'/ (name+'.wav'))) as w:
            assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(1,1,8000)
            data=w.readframes(w.getnframes());samples.append(data)
            assert data[0]==data[-1]==128 and len(set(data))>32 and max(data)-min(data)>100
    assert bank[16:]==b''.join(samples) and len(bank)==8208
    spec = importlib.util.spec_from_file_location('assets1942', ROOT/'tools/1942assets.py')
    mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
    pal=json.loads((ROOT / os88build.at('build/1942-palette.json')).read_text())
    sprites=json.loads((ROOT / os88build.at('build/1942-sprites.json')).read_text())
    mod.validate(pal,sprites)
    for key,value in [('rgb',[[0,0,1]]*64),('cga_map',[4]*64),('cga',[])]:
        bad=dict(pal);bad[key]=value
        try:mod.validate(bad,sprites)
        except ValueError:pass
        else:raise AssertionError('bad palette accepted')
    for cga in (False,True):
        split=next(i for i,s in enumerate(sprites) if s['name']=='loop0')
        bank=mod.bank(sprites[:split],pal,cga)
        extra=mod.bank(sprites[split:],pal,cga)
        assert len(extra)<65536
        assert len(bank)<65536 and struct.unpack_from('<H',bank,4)[0]==len(bank)
        for bank,subset in ((bank,sprites[:split]),(extra,sprites[split:])):
            for i,sprite in enumerate(subset):
                record=struct.unpack_from('<H',bank,8+i*2)[0]
                for phase in range(4):
                    stream=struct.unpack_from('<H',bank,record+2+phase*2)[0]
                    touched=set()
                    if cga and bank[record]&128:
                        while bank[stream]!=0xcb:
                            assert bank[stream]==0x26
                            op,modrm=bank[stream+1:stream+3]
                            assert (op,modrm) in ((0xc6,0x85),(0xc7,0x85),(0x80,0xa5),(0x80,0x8d),(0x81,0xa5),(0x81,0x8d))
                            size=2 if op in (0xc7,0x81) else 1
                            off=struct.unpack_from('<H',bank,stream+3)[0]
                            assert off//80<sprite['h'] and off%80+size<=21
                            stream+=5+size
                        continue
                    while True:
                        off=struct.unpack_from('<H',bank,stream)[0];stream+=2
                        if off==65535:break
                        n=bank[stream];stream+=1
                        assert n>0 and off//80<sprite['h'] and off%80+n<=21
                        assert not touched.intersection(range(off,off+n))
                        touched.update(range(off,off+n))
                        if cga:
                            kind=bank[stream];stream+=1;assert kind in (0,1)
                            stream+=n*(1+kind)
                        else:stream+=n
                        assert stream<=len(bank)
    if rom_build():
        spec=importlib.util.spec_from_file_location('nes1942',ROOT/'tools/1942nes.py')
        nes=importlib.util.module_from_spec(spec);spec.loader.exec_module(nes)
        rom=rom_build().read_bytes()
        with tempfile.TemporaryDirectory() as td:
            path=Path(td)/'bad.nes'
            for bad in (rom[:-1],rom[:100]+bytes([rom[100]^1])+rom[101:],b'BAD!'+rom[4:]):
                path.write_bytes(bad)
                try:nes.Cartridge(path)
                except ValueError:pass
                else:raise AssertionError('unsupported cartridge accepted')
        campaign=json.loads((ROOT/os88build.at('build/1942-campaign.json')).read_text())
        def byte(a):return rom[16+a-0x8000]
        def pointer(a):return byte(a)+256*byte(a+1)
        assert len(campaign['stages'])==32 and campaign['bosses']==[7,15,23,31]
        for stage,events in enumerate(campaign['stages'],1):
            p=pointer(0xe26f+((256-stage*8)&255)//4)
            expected=[]
            while any(byte(p+i) for i in range(3)):
                expected.append([1920-byte(p)*240-byte(p+1),byte(p+2)]);p+=3
            assert events==expected
        assert campaign['aim']==[byte(0xb1fb+i) for i in range(256)]
        for i,pattern in enumerate(campaign['fire']):
            p=pointer(0xfd2f+i*2)
            assert pattern==[byte(p+j) for j in range(byte(p)+2)]
        # Independently read every path, including aliases in later stages.
        def position(p):
            return [(byte(p)&1)*256+byte(p+1)-128,
                    (byte(p+2)&1)*240+byte(p+3)-112]
        assert campaign['waves']==[[byte(0xeb19+4*i+j) for j in range(4)] for i in range(49)]
        for kind,points in enumerate(campaign['paths'],10):
            p=pointer(0xfd4a+(kind-10)*2);expected=[]
            while byte(p)!=255:
                expected.append(position(p));p+=4
            assert points==expected,('path',kind)
        for entries,(address,count) in zip(campaign['starts'],((0xf3ec,16),(0xf12e,8),(0xf550,8))):
            assert entries==[position(address+4*i) for i in range(count)]
        for name,address in (('velocity',0xb2fb),('circle',0xb33b)):
            expected=[list(struct.unpack_from('<hh',rom,16+address-0x8000+4*i)) for i in range(16)]
            assert campaign[name]==expected,name
        assert campaign['centers']==[position(0xfc53+4*i) for i in range(32)]
        assert campaign['reverse']==[byte(0xf14a+i) for i in range(16)]
        print('cartridge: all 32 schedules, 49 waves, 39 paths and motion tables checked',flush=True)
        assert campaign['bonuses'][50]==500 and campaign['bonuses'][100]==100000
        audio=json.loads((ROOT/os88build.at('build/1942-audio.json')).read_text())
        assert len(audio)==23 and audio[14]['loop'] and not audio[21]['loop']
        # Game Over stays the cartridge cue; gameplay varies its melody.
        spec=importlib.util.spec_from_file_location('data1942',ROOT/'tools/1942data.py')
        data=importlib.util.module_from_spec(spec);spec.loader.exec_module(data)
        original=data.sounds(nes.Cartridge(rom_build()))
        assert audio[7]==original[7] and not audio[7]['loop']
        melody=[(hz,t) for hz,t in original[7]['notes'] if hz<12000]
        assert audio[14]['notes']==[[(hz+1)//2,max(1,(min(t,56)*3+2)//4)] for hz,t in melody]+[[0,14]]
        for tag in ('V','C'):
            world=(ROOT / os88build.at('build/WORLD.'+tag+'42')).read_bytes()
            assert world[:4]==b'N42W' and struct.unpack_from('<H',world,4)[0]==len(world)<=32768
            assert max(world[8:264])<23
            assert world[8:264]==rom[16+0x44b:16+0x54b]
            assert world[264:5784]==rom[16+0x54b:16+0x1adb]
    cached,commands=mod.vga_cache(sprites)
    values={k:int(v) for line in commands if ' equ ' in line for k,v in [line.split(' equ ')]}
    assert values['N_CACHE_START']+values['N_CACHE_BYTES']<=65536
    assert values['N_CACHE_START']==2*values['N_PAGE_BYTES']+19200
    print('asset validation: both banks, stream bounds and rejection controls passed',flush=True)

def symbols():
    source = (ROOT/'apps/1942/1942.asm').read_text()
    names = re.findall(r'^VAR (n_\w+),', source, re.M)
    code = ['n_about','n_frontprepare','n_paint','n_key','n_click','n_frontpaint','n_fronttimer','n_frontload','n_vfile','n_cfile','n_scenesums','n_scenecheck','n_loadgfx','n_sprite','n_frame_end','n_refresh','n_present','n_rand','n_dac','n_hud','n_erase','n_update','n_draw','n_loading','n_readbank','n_spawn','n_stageinit','n_scripts','n_awardpow','n_results','n_kill','n_addscore','n_depart','n_flightphaseupdate','n_campaignnew','n_turn','n_audio','n_audio_open','n_audio_close','n_pcm_open','n_pcm_close','n_pcm_halt','n_pcmfile','n_audio_quiet','n_audio_step','n_audio_fmnote','n_music','n_effect','n_tone','n_soundtab','n_event','n_highmsg','n_freeslot','n_hitplayer','n_move_bullets','n_stageevents','n_move_enemies','n_move_shots','n_enemyfire','n_fireangle','n_firerank']
    code += ['n_scriptspawn','n_scrollstep','n_nextstage','n_loop']
    if rom_build():code += ['n_motioninit','n_cartmove']
    source = source.replace('OS88_IMAGE_END','')
    source += '\n'+'\n'.join('dw '+n+'-os88_image_end' for n in names)
    source += '\n'+'\n'.join('dw '+n for n in code)+'\nOS88_IMAGE_END\n'
    with tempfile.TemporaryDirectory() as td:
        asm, out = Path(td)/'symbols.asm', Path(td)/'symbols.bin'
        asm.write_text(source)
        subprocess.run(['nasm','-f','bin','-I','apps/','-I','apps/1942/',
                        '-I',str(ROOT / os88build.at('build'))+'/',
                        '-o',str(out),str(asm)],cwd=ROOT,check=True)
        values = struct.unpack('<%dH'%(len(names)+len(code)),out.read_bytes()[-2*(len(names)+len(code)):])
    bss = struct.unpack_from('<H',(ROOT / os88build.at('build/1942.bin')).read_bytes(),8)[0]
    return dict(zip(names,[bss+v for v in values[:len(names)]])),dict(zip(code,values[len(names):]))


class Game:
    def __init__(self,ui,off,code):
        self.m,self.off,self.code=ui.m,off,code
        w=ui.window('1942')
        raw=self.m.read(self.m.sym('wm_wins'),G.MAX_WIN*G.WIN_SIZE)
        self.base=struct.unpack_from('<H',raw,w.i*G.WIN_SIZE+G.W_SEG)[0]<<4
    def data(self,k,n=2): return self.m.read(self.base+self.off['n_'+k],n)
    def get(self,k,n=2): return int.from_bytes(self.data(k,n),'little')
    def put(self,k,v,n=2): self.m.write(self.base+self.off['n_'+k],v.to_bytes(n,'little'))
    def frame(self,n=1):
        self.m.bp_exec(self.base+self.code['n_frame_end'])
        for _ in range(n):
            self.m.run(); assert self.m.wait_stop(30)=='breakpoint','frame did not finish'
    def key(self,k):
        self.m.key(k);self.frame(2)
    def call(self,k):
        m=self.m;saved=m.regs();sp=(saved['sp']-2)&65535
        old=m.readseg(saved['ss'],sp,2)
        m.write((saved['ss']<<4)+sp,struct.pack('<H',saved['ip']))
        m.cmd(cmd='park',cs=self.base>>4,ip=self.code[k])
        for reg in REGS:m.setreg(reg,saved[reg])
        m.setreg('sp',sp)
        m.bp_exec((saved['cs']<<4)+saved['ip']);m.run()
        assert m.wait_stop(30)=='breakpoint',k+' did not return'
        result=m.regs()
        m.write((saved['ss']<<4)+sp,old)
        for r,v in saved.items():
            if r in REGS:m.setreg(r,v)
        return result
    def fixture(self):
        self.put('stage',1)
        self.call('n_stageinit')
        for k,size in [('enemies',12*24),('blasts',8*6)]:
            self.m.write(self.base+self.off['n_'+k],bytes(size))
        for k,size in [('shots',16*4),('bullets',16*8)]:
            self.m.write(self.base+self.off['n_'+k],b'\xff'*size)
        for k,v in dict(px=120,py=208,lives=3,rolls=3,roll=0,grace=0,
                        scorelo=0,scorehi=0,spawnwait=1000,spawned=0,frames=0,
                        stage=1,weapon=1,fire=0,picky=65535,flightphase=0,distance=0,kills=0,wings=0,players=1,nextlife=400,extended=0,secretkills=0,secretlimit=200).items():self.put(k,v)
        for k,v in dict(state=1,paused=0,bossmade=0,xheld=0).items():self.put(k,v,1)
        self.call('n_refresh')
    def enemy(self,x=100,y=100,hp=1,kind=0):
        self.m.write(self.base+self.off['n_enemies'],struct.pack('<7H',x,y,0,hp,kind,0,1)+bytes(10))
    def shot(self,x=108,y=110):
        self.m.write(self.base+self.off['n_shots'],struct.pack('<2H',x,y))
    def bullet(self):
        self.m.write(self.base+self.off['n_bullets'],struct.pack('<4H',124,210,0,0))
    def canvas(self):
        if self.get('cga',1):
            raw=self.m.read(0xb8000,16384);start=self.get('cgastart')
            out=bytearray(16384)
            for y in range(200):
                bank=(y&1)*8192;row=(y//2)*80
                for x in range(80):out[bank+row+x]=raw[bank+((start+row+x)&8191)]
            return bytes(out)
        planes=vga_planes(self)
        return bytes(planes[x%4][y*80+8+x//4] for y in range(240) for x in range(256))

REGS=('ax','bx','cx','dx','si','di','bp','sp','ss','ds','es','flags')


def vga_planes(g):
    """Actual MOVSB reads: the debugger's direct VGA peek only sees plane 0."""
    m=g.m;saved=m.regs();stub=g.base+g.code['n_rand'];old=m.read(stub,9)
    seg=g.get('canvas');scratch=(seg<<4)+61440;oldbuf=m.read(scratch,4096)
    m.write(stub,bytes.fromhex('fa b8 00 a0 8e d8 f3 a4 90'))
    index=m.inb(0x3ce);m.outb(0x3ce,5);mode=m.inb(0x3cf);m.outb(0x3cf,mode&~8)
    m.outb(0x3ce,4);selected=m.inb(0x3cf);planes=[]
    for plane in range(4):
        m.outb(0x3cf,plane);data=bytearray()
        for start in range(0,19200,4096):
            count=min(4096,19200-start)
            m.cmd(cmd='park',cs=g.base>>4,ip=g.code['n_rand'])
            for reg,v in [('es',seg),('si',g.get('showbase')+start),('di',61440),('cx',count),('flags',saved['flags']&~0x600)]:m.setreg(reg,v)
            m.bp_exec(stub+8);m.run();assert m.wait_stop(30)=='breakpoint'
            data.extend(m.read(scratch,count))
        planes.append(data)
    m.outb(0x3cf,selected);m.outb(0x3ce,5);m.outb(0x3cf,mode);m.outb(0x3ce,index)
    m.write(stub,old);m.write(scratch,oldbuf)
    m.cmd(cmd='park',cs=saved['cs'],ip=saved['ip'])
    for reg in REGS:m.setreg(reg,saved[reg])
    return planes


def reference(g,tag):
    pal=json.loads((ROOT / os88build.at('build/1942-palette.json')).read_text())
    sprites=json.loads((ROOT / os88build.at('build/1942-sprites.json')).read_text())
    byname={s['name']:s for s in sprites}
    cga=tag=='cga';w,h=(320,200) if cga else (256,240)
    band=14 if cga else 16
    rom=rom_build()
    if rom:
        # Independently read the cartridge CHR/metatile/route tables, not the
        # compiled WORLD.GFX or guest's circular cache.
        raw=rom.read_bytes();prg=raw[16:32784];chr=raw[32784:]
        def read(a):return prg[a-32768]
        frame=bytearray(w*h)
        for y in range(band,h):
            sy=g.get('worldy')+y-band;page=g.get('worldpage')
            if sy>=h:sy-=h;page=(page-1)&7
            logical=sy*6//5 if cga else sy
            mapid=read(0x844b+(g.get('scrollstage')-1)*8+page)
            if mapid==2 and g.get('bossmade',1) and not g.get('bosslive',1):mapid=1
            for x in range(w):
                sx=x*4//5 if cga else x
                meta=read(0x854b+mapid*240+(logical//16)*16+sx//16)
                tile=256+read(0x9adb+meta*4+((logical//8)&1)*2+((sx//8)&1))
                off=tile*16+logical%8;bit=7-sx%8
                pixel=((chr[off]>>bit)&1)|(((chr[off+8]>>bit)&1)<<1)
                ink=1+4*read(0x9edb+meta)+pixel if pixel else 1
                frame[y*w+x]=pal['cga_map'][ink] if cga else ink
    else:
        scene=('sea','reef','port')[g.get('scene')]
        bg=(ROOT/('apps/1942/art/'+scene+'.idx')).read_bytes()
        frame=bytearray(w*h)
        scroll=g.get('scroll')//80
        for y in range(band,h):
            sy=band+(y-band+scroll)%(h-band)
            for x in range(w):
                ink=bg[(sy*6//5 if cga else sy)*256+(x*4//5 if cga else x)]
                frame[y*w+x]=pal['cga_map'][ink] if cga else ink
    frame[:w*(14 if cga else 16)]=bytes([0 if cga else 1])*(w*(14 if cga else 16))
    def draw(name,x,y):
        sprite=byname[name];sw,sh=sprite['w'],sprite['h'];pixels=bytes.fromhex(sprite['pixels'])
        if not cga and ((name not in ('carrier','ship','island','wing','roll') and not name.startswith(('font','dir','loop','pow','title'))) or name in ('dir28_3','dir28_4','dir28_5','dir28_11','dir28_12','dir28_13')):x &= ~1
        if x<0 or y<0 or x+sw>256 or y+sh>240:return
        if cga:
            x=x*5//4;y=y*5//6;dw=(sw*5+3)//4;dh=(sh*5+5)//6
        else:dw,dh=sw,sh
        for dy in range(dh):
            for dx in range(dw):
                ink=pixels[min(sh-1,dy*6//5)*sw+min(sw-1,dx*4//5)] if cga else pixels[dy*sw+dx]
                if ink:frame[(y+dy)*w+x+dx]=pal['cga_map'][ink] if cga else ink
    def text(line,x,y):
        for char in line:
            draw('font%d'%ord(char),x,y);x+=8
    state=g.get('state',1)
    if state==1:
        for i in range(12):
            x,y,vx,hp,kind,age,on,path,etype,group,aux,vy=struct.unpack('<12H',g.m.read(g.base+g.off['n_enemies']+i*24,24))
            if on and not (rom and kind==2):
                name=('enemy','elite','boss','blue','scout','diver','bomber','heavy','secret')[kind]
                if kind<6 and kind!=2:
                    sx=-1 if vx&32768 else 1 if vx else 0
                    sy=-1 if vy&32768 else 1 if vy else 0
                    angle={(-1,-1):7,(0,-1):0,(1,-1):1,(-1,0):6,(0,0):4,(1,0):2,(-1,1):5,(0,1):4,(1,1):3}[sx,sy]
                    base={1:'10',4:'40',5:'60'}.get(kind,'28')
                    if base!='60' and age&4:angle+=8
                    name='dir%s_%d'%(base,angle)
                draw(name,x,y)
        if not g.get('grace') or not g.get('frames')&2:
            name='loop%d'%min(6,(48-g.get('roll'))//7) if g.get('roll') else sprites[g.get('bank')]['name']
            draw(name,max(0,min(224,g.get('px')-4)) if g.get('roll') else g.get('px'),g.get('py'))
        if g.get('wings')&1:draw('wing',g.get('px')-20,g.get('py')+4)
        if g.get('wings')&2:draw('wing',g.get('px')+28,g.get('py')+4)
        for key,n,size,name in [('shots',16,4,'shot'),('bullets',16,8,'bullet'),('blasts',8,6,'blast')]:
            for i in range(n):
                raw=g.m.read(g.base+g.off['n_'+key]+i*size,size);x,y=struct.unpack_from('<2H',raw)
                if name=='blast':
                    life=struct.unpack_from('<H',raw,4)[0]
                    if life:draw('blast%d'%((8-life)//2),x,y)
                else:draw(name,x,y)
        draw('bonus' if g.get('picktype')==5 else 'pow%d'%g.get('picktype'),g.get('pickx'),g.get('picky'))
        phase=g.get('flightphase')
        if phase in (1,4):text('PLAYER %d  READY'%(g.get('activeplayer')+1),64,88)
        if phase==3:
            text('MISSION RESULTS',56,64)
            text('DOWNED %03d%%'%g.get('percent'),48,96)
            text('PERCENT    %06d'%g.get('percentbonus',4),40,112)
            text('ROLL BONUS %05d'%g.get('rollbonus'),40,128)
            text('ENTER TO CONTINUE',48,152)
    else:
        if rom:
            for i in range(4):draw('title%d'%i,i*64,56)
        else:text('1 9 4 2',100,56)
        line,x=('ENTER TO TAKE OFF',64) if state==0 else ('GAME OVER',92) if state==2 else ('MISSION COMPLETE',64)
        text(line,x,120)
        if state==0:
            text('1 OR 2 PLAYERS    %d'%g.get('players'),24,144)
            text('SPACE FIRES  X ROLLS',52,168)
        else:text('N FOR A NEW GAME',68,152)
        high=g.get('highlo')+65536*g.get('highhi')
        text('HIGH %06d'%(high%1000000),80,200)
    hud=g.m.read(g.base+g.code['n_hud'],24).split(b'\0')[0].decode()
    text(hud,8,4)
    if cga:
        packed=bytearray(16384)
        for y in range(200):
            for x in range(80):
                p=frame[y*320+x*4:y*320+x*4+4]
                packed[(y&1)*8192+(y//2)*80+x]=(p[0]<<6)|(p[1]<<4)|(p[2]<<2)|p[3]
        return bytes(packed)
    return bytes(frame)

def check_video(g,tag):
    if tag=='vga':
        index=g.m.inb(0x3ce);g.m.outb(0x3ce,5);mode=g.m.inb(0x3cf);g.m.outb(0x3ce,index)
        assert mode&0x40,'VGA lost the 256-color shift mode'
    actual=g.canvas();expected=reference(g,tag)
    if actual!=expected:
        bad=[i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b]
        print('mismatch state', {k:g.get(k) for k in ('frames','scroll','worldy','worldpage','cgastart','counts','oldcount')}, 'count',len(bad),'first',[(i,actual[i],expected[i]) for i in bad[:20]],flush=True)
        (ROOT/'build/1942-actual.raw').write_bytes(actual)
        (ROOT/'build/1942-expected.raw').write_bytes(expected)
    assert actual==expected,(tag+' renderer mismatch',next((i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b),None))

def capture(g,tag,name):
    m=g.m
    # Cropped apertures retain the preceding 400-line mode's height in the
    # pinned MartyPC. The DEBUG raster has the complete 480-line Mode X frame.
    if tag=='vga':
        v=m.video();a=v['apertures'][0];w,h,rgb=m.fbuf(3)
        x,y=a['x'],a['y'];assert x+640<=w and y+480<=h
        data=b''.join(rgb[((y+i)*w+x)*3:((y+i)*w+x+640)*3] for i in range(480))
        M.write_png_rgb(str(ROOT/'build'/('1942-'+tag+'-'+name+'.png')),640,480,data)
    else:
        w,h,rgb=m.fbuf();M.write_png_rgb(str(ROOT/'build'/('1942-'+tag+'-'+name+'.png')),w,h,rgb)


def pacing(g,tag):
    """Cartridge cadence, bounded pools and firing gates on the real 8088."""
    m=g.m
    def bullets():
        raw=g.data('bullets',128)
        return sum(struct.unpack_from('<H',raw,i+2)[0]!=65535 for i in range(0,128,8))
    def fire(x=120,y=140,kind=0,etype=3,vy=2,prime=True):
        g.enemy(x=x,y=y,kind=kind)
        m.write(g.base+g.off['n_enemies']+16,struct.pack('<H',etype))
        m.write(g.base+g.off['n_enemies']+22,struct.pack('<H',vy&65535))
        if prime:g.put('enemyfirecount',63)
        m.setreg('si',g.off['n_enemies']);g.call('n_enemyfire')
    for args in ({'y':32},{'y':190},{'x':8},{'kind':1,'etype':10},
                 {'kind':6,'etype':32},{'vy':-2},{'kind':8,'etype':49}):
        g.fixture();fire(**args);assert bullets()==0,('forbidden enemy shot',args)
    g.fixture();g.put('py',100);fire(y=180);assert bullets()==0,'rearward shot'
    g.fixture();fire();assert bullets()==1,'nearby incoming fighter stayed silent'
    for _ in range(64):fire(prime=False)
    assert bullets()==1,'shared cartridge firing counter ran too quickly'
    fire(prime=False);assert bullets()==2
    for _ in range(12):fire()
    assert bullets()==8,'enemy projectile pool must be eight slots'
    before=g.data('bullets',128);fire();assert g.data('bullets',128)==before
    # Full 16-sector aiming uses the cartridge's quadrant table, including
    # the down-facing cone and the opposite heading used by reversing planes.
    for dx,dy,expected in ((0,80,8),(80,0,4),(-80,0,12),(0,-80,0),(80,80,6),(-80,80,10)):
        m.setreg('ax',dx&65535);m.setreg('bx',dy&65535)
        assert g.call('n_fireangle')['ax']==expected,('aim quadrant',dx,dy)
    if rom_build():
        g.fixture();g.put('bigfirecount',15)
        fire(kind=7,etype=48);assert bullets()==0 and g.get('burstleft')==3
        for _ in range(4):fire(kind=7,etype=48)
        assert bullets()==0,'bomber fired before its cartridge interval'
        fire(kind=7,etype=48);assert bullets()==1
        for _ in range(5):fire(kind=7,etype=48)
        assert bullets()==2 and g.get('burstleft')==1,'bomber sequence'
        for _ in range(5):fire(kind=7,etype=48)
        assert bullets()==3 and g.get('burstleft')==0,'bomber burst completion'
        assert struct.unpack_from('<hh',g.data('bullets',24),4)==(1,3),'signed bomber aim offset'
    # First-stage cartridge opening: two simultaneous 50-tick waves with
    # batches of two and one. Do not accelerate them to ~17 native frames.
    g.fixture()
    wave=g.off['n_waveslive']
    for i,batch in enumerate((2,1)):
        m.write(g.base+wave+i*16,struct.pack('<8H',50,50,3,50,batch,0,0,65535))
    for tick in range(1,201):
        g.put('distance',0);g.call('n_scripts')
        expected=min(8,(tick//50)*3)
        assert g.get('spawned')==expected,('wave timing/budget',tick,g.get('spawned'))
    pending=struct.unpack('<16H',g.data('waveslive',32))
    assert pending[3]+pending[11]==92,'full scene lost pending aircraft'
    for i in range(3):
        m.setreg('si',g.off['n_enemies']+i*24);m.setreg('ax',0);g.call('n_depart')
    for tick in range(1,51):
        g.put('distance',0);g.call('n_scripts')
        assert g.get('spawned')==(11 if tick==50 else 8),'full pool accumulated timer debt'
    g.fixture();g.put('enemyfirecount',42);g.put('burstleft',5);g.call('n_stageinit')
    assert g.get('enemyfirecount')==g.get('burstleft')==0,'new stage retained firing state'
    print(tag+': cartridge wave cadence, eight-plane/bullet pools, proximity, heading and firing counters passed',flush=True)



def campaign_flow(g,tag):
    """Run all stages and motion families in the assembled guest, without I/O."""
    m=g.m
    def guest(body):
        # Bounded scratch code; no renderer runs during these calls.
        origin=g.off['n_rects']
        with tempfile.TemporaryDirectory() as td:
            asm,out=Path(td)/'step.asm',Path(td)/'step.bin'
            asm.write_text('bits 16\norg %d\n'%origin+body+'\nret\n')
            subprocess.run(['nasm','-f','bin','-o',str(out),str(asm)],check=True)
            payload=out.read_bytes();assert len(payload)<256
        old=m.read(g.base+origin,len(payload));m.write(g.base+origin,payload)
        g.code['campaign_test']=origin
        try:return g.call('campaign_test')
        finally:m.write(g.base+origin,old)
    # Route time is independent of the display adapter and its row rounding.
    g.fixture()
    guest('mov cx, 400\n.loop: push cx\ncall %d\npop cx\nloop .loop'%g.code['n_scripts'])
    assert g.get('distance')==300 and g.get('routefrac')==0
    g.fixture();g.enemy();g.bullet();g.put('distance',1791);g.put('routefrac',3)
    g.call('n_scripts')
    assert g.get('endclear')==1 and g.data('bullets',128)==b'\xff'*128
    assert not any(g.data('enemies',12*24)[12::24]),'final page retained aircraft'
    g.put('rebuild',0,1);g.call('n_scripts')
    assert g.get('rebuild',1)==0,'final-page cleanup repeated'
    if not rom_build():return
    data=json.loads((ROOT/os88build.at('build/1942-campaign.json')).read_text())
    # ROM opening after takeoff: 90 updates to the first event, then its
    # 50-update timer (including the enqueue update). All three are divers.
    g.fixture();g.put('distance',45);g.put('entryseq',0)
    guest('mov cx,139\n.loop: push cx\ncall %d\npop cx\nloop .loop'%g.code['n_scripts'])
    assert g.get('spawned')==3,'opening wave timing'
    opening=[struct.unpack_from('<12H',g.data('enemies',72),i*24) for i in range(3)]
    assert [a[0] for a in opening]==[16,32,192]
    assert all(a[1]==65536-48 and a[8]==3 for a in opening),'opening must enter from above'
    def spawn(kind,seq=0):
        g.fixture();g.put('entryseq',seq)
        wave=g.off['n_waveslive']
        m.write(g.base+wave,struct.pack('<8H',1,1,kind,1,1,0,0,65535))
        m.setreg('di',wave);g.call('n_scriptspawn')
    def move(n):
        return guest('mov cx,%d\n.loop: push cx\nmov si,%d\ncall %d\npop cx\njc .end\nloop .loop\n.end:'%
                     (n,g.off['n_enemies'],g.code['n_cartmove']))
    def xy():return struct.unpack('<hh',g.data('enemies',4))
    # Straight portions independently traced through the original 6502 ROM.
    # Coordinate checkpoints are before any player collision or firing.
    for kind,expected in ((3,(34,-8)),(6,(96,-4)),(34,(32,273)),(40,(224,273)),(48,(64,261))):
        spawn(kind);move(11)
        assert xy()==expected,('ROM trajectory',kind,xy(),expected)
    spawn(48);g.put('px',200);m.setreg('si',g.off['n_enemies']);g.call('n_motioninit')
    assert xy()==(192,288),'bomber must enter on player half'
    # Shared sequential entries, not a new random choice for each airplane.
    for seq in range(16):
        spawn(3,seq)
        assert list(xy())==data['starts'][1][seq&7]
        assert g.get('entryseq')==seq+1
    # Full reversals take eight updates, then reflect the original heading.
    spawn(3);move(64)
    assert struct.unpack_from('<H',g.data('motion',24),8)[0]>0,'missing fighter reversal'
    move(12);assert struct.unpack_from('<h',g.data('enemies',24),22)[0]<0
    # Every scheduled family must leave instead of following adjacent records
    # back into view. Offscreen departure also releases the formation slot.
    for kind in sorted({w[0] for w in data['waves']}):
        spawn(kind)
        result=move(1400)
        assert result['flags']&1,('aircraft never departed',kind,xy())
    # Stage 3's five staggered orange rows are one formation, not five POWs.
    for escaped in (False,True):
        g.fixture();g.put('stage',3);g.call('n_stageinit')
        g.put('event',g.get('event')+6*4);g.put('distance',591);g.put('routefrac',3)
        guest('mov cx,25\n.loop: push cx\ncall %d\npop cx\nloop .loop'%g.code['n_scripts'])
        assert g.get('spawned')==5
        for i in range(5):
            m.setreg('si',g.off['n_enemies']+i*24)
            if escaped and i==0:
                m.setreg('ax',0);g.call('n_depart')
            else:g.call('n_kill')
            if i<4:assert g.get('picky')==65535,'partial staggered formation rewarded'
        assert (g.get('picky')!=65535)==(not escaped)
    body="""mov cx,2500
.loop:
    push cx
    call {scripts}
    mov si,{enemies}
.actor:
    cmp word [si+12],0
    je .next
    inc word [si+10]
    cmp word [si+16],255
    je .depart
    push si
    call {move}
    pop si
    jnc .next
.depart:
    xor ax,ax
    call {depart}
.next:
    add si,24
    cmp si,{end}
    jb .actor
    pop cx
    loop .loop
""".format(scripts=g.code['n_scripts'],enemies=g.off['n_enemies'],move=g.code['n_cartmove'],
             depart=g.code['n_depart'],end=g.off['n_enemies']+12*24)
    for stage,events in enumerate(data['stages'],1):
        g.put('stage',stage);g.call('n_stageinit');start=g.get('event')
        guest(body)
        assert g.get('event')==start+4*len(events),('unconsumed stage event',stage,(g.get('event')-start)//4,len(events),g.get('distance'))
        assert g.get('distance')==1875,('stage route stalled',stage)
        assert bool(g.get('bossmade',1))==(stage in data['bosses']),('boss stage',stage)
        assert not any(g.data('enemies',12*24)[12::24]),('landing aircraft',stage)
        assert g.get('endclear')==1 and g.data('bullets',128)==b'\xff'*128
        g.put('flightphase',0);g.call('n_nextstage')
        assert g.get('flightphase')==2,('stage did not land',stage)
        if stage%8==0:print(tag+': campaign stages 1..%d passed'%stage,flush=True)
    g.fixture();g.call('n_refresh')
    print(tag+': all 32 stages completed; sequential entries, ROM trajectory checkpoints and all scheduled motion families passed',flush=True)

def rules(g,tag):
    """Exercise native game rules through the assembled 8086 entry points."""
    m=g.m
    # A formation is awarded only after every member dies. An escaped member
    # makes the otherwise identical control formation ineligible.
    for escaped in (False,True):
        g.fixture()
        wave=g.off['n_waveslive']
        m.write(g.base+wave,struct.pack('<8H',1,5,10,0,1,5,0,2))
        for i in range(5):
            actor=struct.pack('<12H',80+i*16,80,0,1,1,0,1,0,10,wave,0,2)
            m.write(g.base+g.off['n_enemies']+i*24,actor)
        for i in range(5):
            # Game.call preserves SI, permitting a real per-enemy death call.
            m.setreg('si',g.off['n_enemies']+i*24)
            if escaped and i==0:
                m.setreg('ax',0);g.call('n_depart')
            else:g.call('n_kill')
            if i<4:assert g.get('picky')==65535,'early formation reward'
        assert (g.get('picky')!=65535)==(not escaped),'formation completion/escape rule'
        if not escaped:assert g.get('picktype')==2
    # All distinct POW effects, including the negative control for wing hits.
    g.fixture();g.put('picktype',2);g.call('n_awardpow');assert g.get('wings')==3
    g.put('picktype',3);g.call('n_awardpow');assert g.get('rolls')==4
    g.put('picktype',4);g.call('n_awardpow');assert g.get('lives')==4
    old=g.get('scorelo');g.put('picktype',5);g.call('n_awardpow');assert g.get('scorelo')-old==5000
    g.fixture();g.put('wings',3)
    m.write(g.base+g.off['n_bullets'],struct.pack('<4H',g.get('px')-16,g.get('py')+8,0,0))
    g.call('n_move_bullets');assert g.get('wings')==2 and g.get('lives')==3
    g.fixture();g.put('wings',3);g.enemy(x=g.get('px')+32,y=g.get('py'),kind=0)
    g.call('n_move_enemies');assert g.get('wings')==1 and g.get('lives')==3
    g.fixture();g.enemy(kind=0);g.put('picktype',1);g.call('n_awardpow')
    assert g.data('enemies',14)[12]==0 and g.get('kills')==1
    # Secret-plane thresholds, collectible value, and repeat threshold.
    g.fixture();g.put('secretkills',199);g.call('n_scripts')
    assert not any(g.data('enemies',12*24)[12::24])
    g.put('secretkills',200);g.call('n_scripts')
    assert struct.unpack_from('<H',g.data('enemies',9*24),8*24+8)[0]==8
    assert g.get('secretkills')==0 and g.get('secretlimit')==150
    m.setreg('si',g.off['n_enemies']+8*24);g.call('n_kill')
    assert g.get('picktype')==5 and g.get('picky')!=65535
    # Fallback retains its native maneuvers; cartridge motion has its own
    # fixed-point trajectory and campaign checks below.
    g.fixture();g.put('grace',100)
    actor=struct.pack('<12H',60,210,1,1,3,1,1,0,3,0,0,2)
    m.write(g.base+g.off['n_enemies'],actor);g.call('n_move_enemies')
    raw=struct.unpack('<12H',g.data('enemies',24))
    if not rom_build():assert raw[7]==1 and raw[11]==65533
    actor=struct.pack('<12H',60,180,1,1,4,1,1,0,6,0,0,2)
    m.write(g.base+g.off['n_enemies'],actor);g.call('n_move_enemies')
    raw=struct.unpack('<12H',g.data('enemies',24))
    if not rom_build():assert raw[7]==1 and raw[2]==3 and raw[11]==0
    g.put('px',0);g.call('n_move_enemies')
    if not rom_build():assert struct.unpack_from('<H',g.data('enemies',24),4)[0]==3
    # Boundary checks on percentage brackets and the 100% award (>16 bits).
    for kills,expected in ((49,0),(50,500),(55,1000),(80,5000),(85,8000),(99,20000),(100,100000)):
        g.fixture();g.put('spawned',100);g.put('kills',kills);g.put('rolls',2)
        g.call('n_results')
        assert g.get('percent')==kills and g.get('percentbonus',4)==expected
        assert g.get('scorelo')+65536*g.get('scorehi')==expected+2000
    g.call('n_refresh');g.frame(2);check_video(g,tag);capture(g,tag,'results')
    g.fixture();g.put('wings',3);g.frame(2);check_video(g,tag);capture(g,tag,'wingmen')
    # Extend awards at 20,000 and 80,000; both words of score carry correctly.
    g.fixture();m.setreg('ax',19950);g.call('n_addscore');assert g.get('lives')==3
    m.setreg('ax',50);g.call('n_addscore');assert g.get('lives')==4
    m.setreg('ax',60000);g.call('n_addscore');assert g.get('lives')==5
    assert g.get('scorelo')+65536*g.get('scorehi')==80000
    # Alternation preserves each player's score, stage, lives and extend state.
    g.fixture();g.put('players',2);g.call('n_campaignnew');g.put('lives',2);g.put('scorelo',12300)
    g.call('n_turn');assert g.get('activeplayer')==1 and g.get('scorelo')==0 and g.get('lives')==3
    g.put('lives',1);g.put('scorelo',300);g.call('n_turn')
    assert g.get('activeplayer')==0 and g.get('scorelo')==12300 and g.get('lives')==2
    # Keep P1 dead and prove the surviving player retains control.
    g.put('lives',0);g.call('n_turn');assert g.get('activeplayer')==1
    g.put('lives',0);g.call('n_turn');assert g.get('state',1)==2
    g.fixture();g.put('activeplayer',0)
    print(tag+': formations, POWs, wing loss, score brackets, extends and two-player turns passed',flush=True)


def clock_pacing(g,tag):
    """Exercise the real frame limiter with rendering taking zero guest time.

    This covers the fast-CPU path even on the XT: consecutive cheap frames
    must each cross a system tick, not merely one FASTTICK interrupt.
    """
    m=g.m;g.frame();saved=m.regs()
    ticks=m.sym('ticks')
    tick=lambda:int.from_bytes(m.read(ticks,2),'little')
    def park():
        m.cmd(cmd='park',cs=g.base>>4,ip=g.code['n_frame_end'])
        for reg in REGS:m.setreg(reg,saved[reg])
    def finish(start):
        g.put('frametick',start)
        park()
        before=m.status()['cycles']
        m.bp_exec(g.base+g.code['n_loop']);m.run()
        assert m.wait_stop(30)=='breakpoint','frame limiter did not return'
        return m.status()['cycles']-before
    durations=[]
    for _ in range(8):
        start=tick();durations.append(finish(start))
        assert (tick()-start)&65535==1,'fast frame escaped before a system tick'
    # After the first partial tick, each cheap frame should occupy ~54.9ms.
    fps=7*M.GUEST_HZ/sum(durations[1:])
    assert 18.0<fps<18.4,(tag,'fast-frame cap',fps)
    before=tick()
    m.write(ticks,b'\xff\xff')
    try:
        finish(65535)
        assert tick()==0,'frame limiter hung or skipped at tick wrap'
    finally:
        m.write(ticks,struct.pack('<H',(before+1)&65535))
    # Slow rendering has already spent its frame budget; never add a wait
    # or replay missed simulation steps, including across tick wrap.
    for age in (1,4):
        start=tick();cycles=finish((start-age)&65535)
        assert cycles<M.GUEST_HZ/100,'overdue frame incurred another wait'
    park()
    print('%s: fast frames capped at %.2f Hz; tick wrap and overdue frames passed'%(tag,fps),flush=True)


def combat_bench(g,tag):
    """Hold a crowded formation on screen; no quiet-scene FPS substitution."""
    g.fixture();g.put('grace',60000);g.put('weapon',3)
    m=g.m;m.key('Space',up=False);g.frame(2)
    durations=[]
    for frame in range(64):
        # Refill on the host without advancing guest cycles. The actual frame
        # still moves, collides, erases and draws every object through game code.
        for i in range(12):
            g.m.write(g.base+g.off['n_enemies']+i*24,struct.pack('<12H',24+(i%6)*36,28+(i//6)*40,1,100,0,i,1,0,0,0,0,2))
        for i in range(16):
            g.m.write(g.base+g.off['n_bullets']+i*8,struct.pack('<4H',24+(i%8)*28,112+(i//8)*32,0,3))
            g.m.write(g.base+g.off['n_shots']+i*4,struct.pack('<2H',22+(i%8)*28,176+(i//8)*16))
        for i in range(4):
            g.m.write(g.base+g.off['n_blasts']+i*6,struct.pack('<3H',32+i*48,80,8-frame%8))
        before=m.status()['cycles'];g.frame();durations.append(m.status()['cycles']-before)
    m.key('Space',down=False)
    fps=len(durations)*M.GUEST_HZ/sum(durations)
    slow=M.GUEST_HZ/max(durations)
    print('%s crowded combat: %.2f fps average, %.2f fps slowest frame (12 planes, 16 bullets, 16 shots, 4 blasts)'%(tag,fps,slow),flush=True)
    times=[]
    for point in ('n_erase','n_update','n_draw','n_present','n_frame_end'):
        m.bp_exec(g.base+g.code[point]);m.run();assert m.wait_stop(30)=='breakpoint'
        times.append((point,m.status()['cycles']))
    print(tag+' crowded stages ms',[(times[i][0],round((times[i+1][1]-times[i][1])/M.GUEST_HZ*1000,2)) for i in range(4)],flush=True)
    check_video(g,tag);capture(g,tag,'combat')
    assert fps>=5.0 and slow>=5.0,tag+' crowded combat missed the 5fps XT floor'


def run(tag,off,code):
    machine={'vga':'os8088_xt_vga','cga':'os8088_5150_cga_gla'}[tag]
    with os88ui.boot('build/os8088-360.img',apps='build/1942-360.img',machine=machine) as ui:
        m=ui.m;ui.open_drive('B');ui.open('1942.O88');g=Game(ui,off,code)
        desktop_mode=m.video()['mode']
        m.bp_exec(g.base+code['n_readbank']);m.key('Enter');m.run()
        assert m.wait_stop(30)=='breakpoint','loading screen did not precede I/O'
        loading=g.canvas()
        fonts={s['name']:s for s in json.loads((ROOT / os88build.at('build/1942-sprites.json')).read_text())}
        expected=bytearray(61440 if tag=='vga' else 16384)
        for i,char in enumerate('1942  LOADING GRAPHICS'):
            pixels=bytes.fromhex(fonts['font%d'%ord(char)]['pixels'])
            for y in range(8):
                for x in range(8):
                    if not pixels[y*8+x]:continue
                    if tag=='vga':expected[(112+y)*256+44+i*8+x]=4
                    else:
                        py=96+y;px=76+i*8+x
                        expected[(py&1)*8192+(py//2)*80+px//4]|=3<<(6-2*(px%4))
        assert loading==expected,'loading text must be complete before graphics I/O'
        g.frame()
        assert g.get('infs',1)==1 and g.get('error',1)==0
        assert g.data('fsi',16)[14]==(8 if tag=='vga' else 2)
        check_video(g,tag);capture(g,tag,'takeoff')
        assert g.get('state',1)==1
        assert g.get('flightphase')==1
        g.frame(34);assert g.get('flightphase')==0
        clock_pacing(g,tag)
        m.key('ArrowLeft',up=False);g.frame();m.key('Space',up=False);g.frame(8)
        m.key('ArrowLeft',down=False);g.frame();m.key('Space',down=False);g.frame(2)
        assert g.get('px')<116 and any(struct.unpack_from('<H',g.data('shots',64),i+2)[0]!=65535 for i in range(0,64,4)),'held movement/fire failed'
        check_video(g,tag)
        # Pause advances neither simulation nor its timer.
        g.key('KeyP');assert g.get('paused',1)==1
        assert m.inb(0x61)&3==0,'pause left the speaker enabled'
        frozen=g.get('frames');g.frame(3);assert g.get('frames')==frozen
        g.key('KeyP');assert g.get('paused',1)==0
        g.key('KeyM');assert g.get('sound',1)==0 and m.inb(0x61)&3==0
        audio=(g.get('musicptr'),g.get('effectptr'));g.frame(3)
        assert audio==(g.get('musicptr'),g.get('effectptr')),'muting advanced audio streams'
        g.key('KeyM');assert g.get('sound',1)==1
        g.key('KeyX');assert g.get('rolls')==2 and g.get('roll')>0
        g.bullet();g.frame();assert g.get('lives')==3,'roll failed to protect'
        # Natural gameplay, and actual guest-clock frame rate.
        g.key('KeyN');m.key('Space',up=False)
        start=m.status()['cycles'];g.frame(90);elapsed=(m.status()['cycles']-start)/M.GUEST_HZ
        m.key('Space',down=False);g.frame()
        print('%s: %.2f fps across 90 native frames; simulation=%d'%(tag,90/elapsed,g.get('frames')),flush=True)
        times=[]
        for point in ('n_erase','n_update','n_draw','n_present','n_frame_end'):
            m.bp_exec(g.base+code[point]);m.run();assert m.wait_stop(30)=='breakpoint'
            times.append((point,m.status()['cycles']))
        print('stages ms',[(times[i][0],round((times[i+1][1]-times[i][1])/M.GUEST_HZ*1000,2)) for i in range(4)],flush=True)
        check_video(g,tag)
        canvas=g.canvas();g.call('n_refresh');g.call('n_present')
        assert g.canvas()==canvas,'stale incremental composition'
        check_video(g,tag)
        # Let a complete video raster scan out without moving the simulation.
        g.put('paused',1,1);g.frame(2);capture(g,tag,'gameplay');g.put('paused',0,1)
        combat_bench(g,tag)
        pacing(g,tag)
        campaign_flow(g,tag)
        rules(g,tag)
        g.fixture();g.call('n_scenecheck')
        for i,kind in enumerate((0,1,2,3,4,5,6,7)):
            m.write(g.base+g.off['n_enemies']+i*24,struct.pack('<7H',16+(i%4)*60,28+(i//4)*70,1,100,kind,0,1)+bytes(10))
        g.frame();check_video(g,tag)
        # Force a route rich in land, cross native row / metatile / map and
        # circular-cache boundaries, then compare every displayed pixel.
        if rom_build():
            g.put('stage',9);g.call('n_scenecheck')
            g.put('worldpage',4);g.put('worldy',1);g.put('scene',65535)
            g.call('n_scenecheck');g.frame(4);check_video(g,tag)
            assert g.get('worldpage')==5,'route page did not advance'
            g.put('grace',1000);g.frame(125);check_video(g,tag)
            g.put('paused',1,1);before=(g.get('scroll'),g.get('worldy'),g.get('worldpage'))
            g.frame(3);assert before==(g.get('scroll'),g.get('worldy'),g.get('worldpage'))
            g.put('paused',0,1)

        # Near miss is the negative control for the collision fixture.
        g.fixture();g.enemy();g.shot(x=90);g.frame()
        assert g.get('scorelo')==0 and g.data('enemies',14)[12]==1
        g.fixture();g.enemy();g.shot();g.frame()
        assert g.get('scorelo')==50 and g.data('enemies',14)[12]==0
        # A single orange enemy is not an entire formation.
        g.fixture();g.enemy(hp=1,kind=1);g.shot();g.frame()
        assert g.get('scorelo')==100 and g.get('picky')==65535
        g.put('picktype',0);g.put('pickx',g.get('px')+4);g.put('picky',g.get('py'));g.frame()
        assert g.get('weapon')==2 and g.get('scorelo')==1100
        # Roll uses an edge, not the held key level.
        g.fixture();m.key('KeyX',up=False);g.frame(12)
        assert g.get('rolls')==2
        g.bullet();g.frame();assert g.get('lives')==3
        m.key('KeyX',down=False);g.frame(2)
        # Contact, respawn grace, zero lives, and restart.
        g.fixture();g.bullet();g.frame();assert g.get('lives')==2 and g.get('grace')>0
        g.bullet();g.frame();assert g.get('lives')==2
        for life in (1,0):
            g.put('grace',0);g.bullet();g.frame();assert g.get('lives')==life
        assert g.get('state',1)==2
        g.key('KeyN');assert g.get('lives')==3 and g.get('state',1)==1
        # Cartridge boss stage, results and explicit stage advancement.
        g.fixture();g.put('stage',7);g.call('n_scenecheck')
        g.put('worldpage',6);g.put('worldy',46 if tag=='cga' else 56)
        g.put('distance',1296);g.put('rebuild',1,1);g.frame(3)
        assert g.get('bosslive',1)==1
        check_video(g,tag);capture(g,tag,'boss')
        for i in range(12):
            actor=g.data('enemies',12*24)[i*24:(i+1)*24]
            if actor[8]==2 and actor[12]:
                m.setreg('si',g.off['n_enemies']+i*24);g.call('n_kill');break
        assert g.get('bosslive',1)==0 and g.get('scorelo')>=20000
        g.frame(2);check_video(g,tag)
        g.fixture();g.put('stage',32);g.put('distance',1824);g.put('bossmade',1,1);g.frame()
        assert g.get('flightphase')==2
        g.put('py',120);g.frame();assert g.get('flightphase')==3
        g.put('flightphasetime',0);g.frame();assert g.get('state',1)==3
        # Cycled mapping affects output but preserves the simulation.
        g.key('KeyN')
        if tag=='cga':
            for expected in (1,2,0):
                g.key('KeyC');assert g.get('profile',1)==expected;check_video(g,tag)
        else:
            pal=json.loads((ROOT/os88build.at('build/1942-palette.json')).read_text())
            for stage,key in ((29,'late_rgb'),(1,'rgb')):
                g.put('stage',stage);g.frame(2);m.outb(0x3c7,0)
                assert [m.inb(0x3c9) for _ in range(len(pal[key])*3)]==[v*63//255 for c in pal[key] for v in c]
        # Re-enter twice: unchanged state and restored desktop mode.
        for _ in range(2):
            g.key('KeyP');assert g.get('paused',1)==1
            old=g.get('frames');route=(g.get('worldy'),g.get('worldpage'))
            m.bp_exec();m.key('Escape');m.run()
            M.until(m,lambda _:g.get('infs',1)==0,'desktop restore',limit=30)
            M.ui_done(m);assert g.get('frames')==old
            assert m.video()['mode']==desktop_mode,'desktop video mode not restored'
            m.key('Enter');g.frame();assert g.get('infs',1)==1
            if rom_build():assert route==(g.get('worldy'),g.get('worldpage')),'resume reset route'
            g.key('KeyP');check_video(g,tag)
        # Every scenery bank is exercised through the actual guest file loader.
        for stage,scene in ((5,1),(9,2),(1,0)):
            g.put('stage',stage);g.frame(2);assert g.get('scrollstage' if rom_build() else 'scene')==(stage if rom_build() else scene)
            check_video(g,tag)
            g.put('paused',1,1);g.frame(2);capture(g,tag,'scene%d'%scene);g.put('paused',0,1)
        # Negative control: change the expected checksum, so an otherwise valid
        # stage is rejected before it can replace the resident backdrop.
        at=g.base+code['n_scenesums']+((2 if tag=='cga' else 0) if rom_build() else (1+(3 if tag=='cga' else 0))*2)
        saved=m.read(at,2);m.write(at,struct.pack('<H',(int.from_bytes(saved,'little')+1)&65535))
        g.put('stage',5)
        if rom_build():g.put('scene',65535)
        m.bp_exec();m.run()
        M.until(m,lambda _:g.get('infs',1)==0 and g.get('error',1)==2,'damaged bank rejection',limit=60)
        M.ui_done(m);assert m.video()['mode']==desktop_mode
        m.write(at,saved);m.key('Enter');g.frame();assert g.get('error',1)==0
        # A missing companion file must likewise return a usable desktop.
        m.bp_exec();m.key('Escape');m.run();M.ui_done(m)
        at=g.base+code['n_cfile' if tag=='cga' else 'n_vfile'];saved=m.read(at,1);m.write(at,b'X')
        m.key('Enter');m.run()
        M.until(m,lambda _:g.get('infs',1)==0 and g.get('error',1)==2,'missing bank rejection',limit=60)
        M.ui_done(m);assert m.video()['mode']==desktop_mode
        m.write(at,saved);m.key('Enter');g.frame();assert g.get('error',1)==0
        check_video(g,tag)
        m.bp_exec();m.key('Escape');m.run();M.ui_done(m)
        print(tag+': controls, collisions, pickup, roll/grace, boss, victory, palettes, full video and restore passed',flush=True)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--assets-only',action='store_true')
    ap.add_argument('--adapter',choices=('vga','cga','both'),default='both')
    a=ap.parse_args();assets()
    if a.assets_only:return
    off,code=symbols()
    for tag in (('vga','cga') if a.adapter=='both' else (a.adapter,)):run(tag,off,code)

if __name__=='__main__':main()
