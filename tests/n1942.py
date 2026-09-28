#!/usr/bin/env python3
"""Private-cartridge gate: make 1942test NESROM=/path/1942.nes.

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

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import os88marty as M
import os88ui
import os88geom as G


def assets():
    spec = importlib.util.spec_from_file_location('assets1942', ROOT/'tools/1942assets.py')
    mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
    rom = bytearray(40976); rom[:6] = b'NES\x1a\x02\x01'
    pal = json.loads((ROOT/'apps/1942/palette.json').read_text())
    assert 'n_playerart:' in mod.convert(rom, pal)
    for bad in (rom[:-1], b'bad', rom[:6]+b'\x10'+rom[7:], rom[:6]+b'\x04'+rom[7:]):
        try: mod.convert(bad, pal)
        except ValueError: pass
        else: raise AssertionError('invalid cartridge accepted')
    for key, value in (('register',64), ('map',[4]*16)):
        bad = json.loads(json.dumps(pal)); bad['cga'][0][key] = value
        try: mod.convert(rom,bad)
        except ValueError: pass
        else: raise AssertionError('invalid palette accepted')
    print('asset validation: valid input and six rejection controls passed', flush=True)


def symbols():
    source = (ROOT/'apps/1942/1942.asm').read_text()
    names = re.findall(r'^VAR (n_\w+),', source, re.M)
    code = ['n_frame_end','n_refresh','n_present','n_rand','n_dac','n_hud','n_erase','n_update','n_draw']
    source = source.replace('OS88_IMAGE_END','')
    source += '\n'+'\n'.join('dw '+n+'-os88_image_end' for n in names)
    source += '\n'+'\n'.join('dw '+n for n in code)+'\nOS88_IMAGE_END\n'
    with tempfile.TemporaryDirectory() as td:
        asm, out = Path(td)/'symbols.asm', Path(td)/'symbols.bin'
        asm.write_text(source)
        subprocess.run(['nasm','-f','bin','-I','apps/','-I','apps/1942/','-I','build/',
                        '-o',str(out),str(asm)],cwd=ROOT,check=True)
        values = struct.unpack('<%dH'%(len(names)+len(code)),out.read_bytes()[-2*(len(names)+len(code)):])
    bss = struct.unpack_from('<H',(ROOT/'build/1942.bin').read_bytes(),8)[0]
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
        m.write((saved['ss']<<4)+sp,old)
        for r,v in saved.items():
            if r in REGS:m.setreg(r,v)
    def fixture(self):
        for k,size in [('enemies',12*14),('blasts',8*6)]:
            self.m.write(self.base+self.off['n_'+k],bytes(size))
        for k,size in [('shots',16*4),('bullets',16*8)]:
            self.m.write(self.base+self.off['n_'+k],b'\xff'*size)
        for k,v in dict(px=120,py=208,lives=3,rolls=3,roll=0,grace=0,
                        scorelo=0,scorehi=0,spawnwait=1000,spawned=0,frames=0,
                        stage=1,weapon=1,fire=0,picky=65535).items():self.put(k,v)
        for k,v in dict(state=1,paused=0,bossmade=0,xheld=0).items():self.put(k,v,1)
        self.call('n_refresh')
    def enemy(self,x=100,y=100,hp=1,kind=0):
        self.m.write(self.base+self.off['n_enemies'],struct.pack('<7H',x,y,0,hp,kind,0,1))
    def shot(self,x=108,y=110):
        self.m.write(self.base+self.off['n_shots'],struct.pack('<2H',x,y))
    def bullet(self):
        self.m.write(self.base+self.off['n_bullets'],struct.pack('<4H',124,210,0,0))
    def canvas(self):
        raw=self.m.read(self.get('canvas')<<4,61440)
        if self.get('cga',1):return raw
        result=bytearray(61440)
        for p in range(4):result[p::4]=raw[p*15360:(p+1)*15360]
        return bytes(result)

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
            for reg,v in [('es',seg),('si',start),('di',61440),('cx',count),('flags',saved['flags']&~0x600)]:m.setreg(reg,v)
            m.bp_exec(stub+8);m.run();assert m.wait_stop(30)=='breakpoint'
            data.extend(m.read(scratch,count))
        planes.append(data)
    m.outb(0x3cf,selected);m.outb(0x3ce,5);m.outb(0x3cf,mode);m.outb(0x3ce,index)
    m.write(stub,old);m.write(scratch,oldbuf)
    m.cmd(cmd='park',cs=saved['cs'],ip=saved['ip'])
    for reg in REGS:m.setreg(reg,saved[reg])
    return planes


def check_video(g,tag):
    canvas=g.canvas()
    if tag=='vga':
        actual=vga_planes(g)
        for p in range(4):
            expected=bytearray(19200)
            for y in range(240):expected[y*80+8:y*80+72]=canvas[y*256+p:y*256+256:4]
            assert actual[p]==expected,('VGA mismatch',p,next((i for i,(a,b) in enumerate(zip(actual[p],expected)) if a!=b),None))
    else:
        pal=json.loads((ROOT/'apps/1942/palette.json').read_text())['cga'][g.get('profile',1)]
        actual=g.m.read(0xb8000,16384);expected=bytearray(16384)
        for y in range(200):
            for group in range(80):
                b=0
                for p in range(4):b=(b<<2)|pal['map'][canvas[(y*6//5)*256+(group*4+p)*4//5]]
                expected[(y&1)*8192+(y//2)*80+group]=b
        assert actual==expected,('CGA mismatch',next((i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b),None))


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


def run(tag,off,code):
    machine={'vga':'os8088_xt_vga','cga':'os8088_5150_cga_gla'}[tag]
    with os88ui.boot('build/os8088-360.img',apps='build/1942-360.img',machine=machine) as ui:
        m=ui.m;ui.open_drive('B');ui.open('1942.O88');g=Game(ui,off,code)
        desktop_mode=m.video()['mode']
        m.key('Enter');g.frame()
        assert g.get('infs',1)==1 and g.get('error',1)==0
        assert g.data('fsi',16)[14]==(8 if tag=='vga' else 2)
        check_video(g,tag)
        g.key('Enter');assert g.get('state',1)==1
        m.key('ArrowLeft',up=False);m.key('Space',up=False);g.frame(8)
        m.key('ArrowLeft',down=False);m.key('Space',down=False);g.frame(2)
        assert g.get('px')<120 and g.get('fire')>0,'held movement/fire failed'
        check_video(g,tag)
        # Pause advances neither simulation nor its timer.
        g.key('KeyP');assert g.get('paused',1)==1
        frozen=g.get('frames');g.frame(3);assert g.get('frames')==frozen
        g.key('KeyP');assert g.get('paused',1)==0
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
        canvas=g.canvas();g.call('n_refresh');assert g.canvas()==canvas,'stale incremental canvas'
        g.call('n_present');check_video(g,tag)
        # Let a complete video raster scan out without moving the simulation.
        g.put('paused',1,1);g.frame(2);capture(g,tag,'gameplay');g.put('paused',0,1)
        # Near miss is the negative control for the collision fixture.
        g.fixture();g.enemy();g.shot(x=90);g.frame()
        assert g.get('scorelo')==0 and g.data('enemies',14)[12]==1
        g.fixture();g.enemy();g.shot();g.frame()
        assert g.get('scorelo')==100 and g.data('enemies',14)[12]==0
        # Orange enemy -> pickup -> dual gun.
        g.fixture();g.enemy(hp=1,kind=1);g.shot();g.frame()
        assert g.get('scorelo')==500 and g.get('picky')!=65535
        g.put('pickx',g.get('px')+4);g.put('picky',g.get('py'));g.frame()
        assert g.get('weapon')==2 and g.get('scorelo')==1000
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
        # Boss threshold, kill, stage roll refill, and last-stage completion.
        g.fixture();g.put('stage',4);g.put('spawned',24);g.frame()
        assert struct.unpack('<7H',g.data('enemies',14))[4]==2
        g.enemy(x=100,y=100,hp=1,kind=2);g.shot();g.frame()
        assert g.get('stage')==5 and g.get('scorelo')==3000 and g.get('rolls')==3
        g.fixture();g.put('stage',32);g.put('spawned',24);g.put('bossmade',1,1);g.frame()
        assert g.get('state',1)==3
        # Cycled mapping affects output but preserves the simulation.
        g.key('KeyN')
        if tag=='cga':
            for expected in (1,2,0):
                g.key('KeyC');assert g.get('profile',1)==expected;check_video(g,tag)
        # Re-enter twice: unchanged state and restored desktop mode.
        for _ in range(2):
            g.key('KeyP');assert g.get('paused',1)==1
            old=g.get('frames');m.bp_exec();m.key('Escape');m.run()
            M.until(m,lambda _:g.get('infs',1)==0,'desktop restore',limit=30)
            M.ui_done(m);assert g.get('frames')==old
            assert m.video()['mode']==desktop_mode,'desktop video mode not restored'
            m.key('Enter');g.frame();assert g.get('infs',1)==1
            check_video(g,tag);g.key('KeyP')
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
