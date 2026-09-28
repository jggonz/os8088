#!/usr/bin/env python3
"""Native guest gameplay, pixel equivalence and XT cycle measurements.

Standalone opt-in gate: requires the user's local Dr. Mario reference assets.
"""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get('DRMARIO_SOURCE','../NES-Games-Disassembly/Dr. Mario'))
sys.path.insert(0,str(ROOT/'tools'))
import os88marty as M
import os88ui
import os88geom as G


def symbols():
    source=(ROOT/'apps/drmario/drmario.asm').read_text()
    names=re.findall(r'^VAR (dm_\w+),',source,re.M)
    for file in ('drmario.asm','game.inc','video.inc','anim.inc'):
        names += re.findall(r'^(dm_\w+):', (ROOT/'apps/drmario'/file).read_text(),re.M)
    with tempfile.TemporaryDirectory() as td:
        p=Path(td)/'probe.asm';b=Path(td)/'probe.bin'
        p.write_text(source+'\n'+'\n'.join('dw '+n for n in names))
        subprocess.run(['nasm','-f','bin','-I','apps/','-I','apps/drmario/',
            '-I','build/drmario-art/','-o',str(b),str(p)],cwd=ROOT,check=True)
        return dict(zip(names,struct.unpack('<%dH'%len(names),b.read_bytes()[-2*len(names):])))


class Probe:
    def __init__(self,ui,sym):
        self.m=ui.m;self.sym=sym
        w=ui.window('DrMarco')
        raw=self.m.read(self.m.sym('wm_wins'),G.MAX_WIN*G.WIN_SIZE)
        self.base=struct.unpack_from('<H',raw,w.i*G.WIN_SIZE+G.W_SEG)[0]<<4
    def addr(self,n):return self.base+self.sym['dm_'+n]
    def data(self,n,size=1):return self.m.read(self.addr(n),size)
    def b(self,n):return self.data(n)[0]
    def put(self,n,data):self.m.write(self.addr(n),bytes([data]) if isinstance(data,int) else bytes(data))
    def call(self,n,**args):
        m=self.m;saved=m.regs()
        regs=('ax','bx','cx','dx','si','di','bp','sp','ss','ds','es','flags')
        m.cmd(cmd='park',cs=self.base>>4,ip=self.sym['dm_'+n])
        for r in regs:m.setreg(r,args.get(r,saved[r]))
        sp=(saved['sp']-2)&65535
        m.setreg('sp',sp)
        m.write((saved['ss']<<4)+sp,struct.pack('<H',saved['ip']))
        start=m.status()['cycles'];m.bp_exec((saved['cs']<<4)+saved['ip']);m.run()
        assert m.wait_stop(30)=='breakpoint',n+' never returned'
        ms=(m.status()['cycles']-start)/M.GUEST_HZ*1000
        for r in regs:m.setreg(r,saved[r])
        return ms


def capture(m,path):
    w,h,pixels=m.fbuf(0);M.write_png_rgb(str(path),w,h,pixels)


def video(p,tag):
    m=p.m
    if tag=='cga':return m.read(0xb8000,16384)
    # VGA debugger peeks ignore the selected read plane. Execute MOVSB in
    # the guest for all four planes, preserving borrowed code/data/registers.
    saved=m.regs();stub=p.sym['dm_queue'];scratch=p.sym['dm_fontvga']
    oldstub=m.read(p.base+stub,9);olddata=m.read(p.base+scratch,6144)
    m.write(p.base+stub,bytes.fromhex('fa b8 00 a0 8e d8 f3 a4 90'))
    index=m.inb(0x3ce);m.outb(0x3ce,4);selected=m.inb(0x3cf)
    result=[]
    for plane in range(4):
        m.outb(0x3cf,plane)
        for start in range(0,19200,6144):
            count=min(6144,19200-start)
            m.cmd(cmd='park',cs=p.base>>4,ip=stub)
            for r,v in [('es',p.base>>4),('si',start),('di',scratch),('cx',count),
                        ('flags',saved['flags']&~0x600)]:m.setreg(r,v)
            m.bp_exec(p.base+stub+8);m.run()
            assert m.wait_stop(30)=='breakpoint'
            result.append(m.read(p.base+scratch,count))
    m.outb(0x3cf,selected);m.outb(0x3ce,index)
    m.write(p.base+stub,oldstub);m.write(p.base+scratch,olddata)
    m.cmd(cmd='park',cs=saved['cs'],ip=saved['ip'])
    for r in ('ax','bx','cx','dx','si','di','bp','sp','ss','ds','es','flags'):m.setreg(r,saved[r])
    return b''.join(result)


def pixels(p,tag,raw):
    h=240 if tag=='vga' else 200
    if tag=='vga':
        return [[raw[(x&3)*19200+y*80+x//4] for x in range(320)] for y in range(h)]
    return [[(raw[(y&1)*8192+(y//2)*80+x//4]>>(6-2*(x&3)))&3 for x in range(320)] for y in range(h)]


def check_pixels(p,tag,raw):
    # Independent source decoder, not the generated native cache under test.
    chrdata=(SOURCE/'CHR_ROM.chr').read_bytes()
    scene=p.data('scene',128);px=pixels(p,tag,raw)
    viruspose=p.data('viruspose',8)
    height,top=(12,32) if tag=='vga' else (10,24)
    for i,cell in enumerate(scene):
        group,color=cell>>4,cell&3
        tile=None if not cell else (0x80,0x60,0x70,0x40,0x50,0xd0)[group]+3-color if group<6 else None
        for y in range(height):
            for x in range(16):
                if cell==0x60:expected=7 if tag=='vga' else 3
                elif tile is None:expected=0
                else:
                    yy=y*8//height
                    bit=x//2 if group==5 and viruspose[color+4*((i//8)&1)] else 7-x//2
                    v=((chrdata[tile*16+yy]>>bit)&1)+2*((chrdata[tile*16+yy+8]>>bit)&1)
                    expected=(0,3,2,1)[v]
                assert px[top+(i//8)*height+y][96+(i%8)*16+x]==expected,(tag,i,x,y,cell)
    # Compare every actor to the compiler's uncompressed pose oracle, including
    # black restoration pixels. The guest uses compact native span streams.
    for actor,pose in enumerate(p.data('actorpose',4)):
        art=Image.open(ROOT/f'build/drmario-art/dm-{tag}-actor{actor}-{pose}.png')
        if actor==0:
            x0,x1,y0,y1=240,320,64 if tag=='vga' else 52,172 if tag=='vga' else 144
        else:
            x0=240+(actor-1)*24;x1=x0+24
            y0=174 if tag=='vga' else 144;y1=y0+26
        for y in range(y0,y1):
            for x in range(x0,x1):
                assert px[y][x]==art.getpixel((x,y)),('actor',tag,actor,pose,x,y)
    # Preview stays intact on both adapters (including after the first HUD).
    a,b=p.data('next',2)
    for half,color in enumerate((a,b)):
        tile=(0x60 if half==0 else 0x70)+3-color
        for y in range(height):
            for x in range(16):
                yy=y*8//height;bit=7-x//2
                v=((chrdata[tile*16+yy]>>bit)&1)+2*((chrdata[tile*16+yy+8]>>bit)&1)
                expected=(0,3,2,1)[v]
                assert px[(52 if tag=='vga' else 40)+y][240+half*16+x]==expected,('preview',tag,x,y)
    footer=p.data('footer',64).split(b'\0')[0]
    glyphs=p.data('glyphs',768)
    for i,ch in enumerate(footer):
        for y in range(8):
            bits=glyphs[(ch-32)*8+y]
            for x in range(8):
                expected=(7 if tag=='vga' else 3) if bits&(128>>x) else 0
                assert px[(230 if tag=='vga' else 190)+y][32+i*8+x]==expected,('footer',tag,i,x,y)
    # Negative control: a changed bottle pixel must fail this same oracle.


def repaint(p,tag):
    p.call('render');before=video(p,tag);check_pixels(p,tag,before)
    p.call('invalidate');p.call('render')
    assert before==video(p,tag),'incremental pixels differ from a full repaint'
    return before


def fixture(p,cells,state=2):
    board=bytearray(128)
    for i,v in cells.items():board[i]=v
    p.put('board',board);p.put('state',state);p.put('paused',0)
    p.put('dirty',2);p.put('huddirty',1);p.put('delay',1)
    p.put('viruses',sum(v>=0x50 for v in board))
    p.put('colorcount',[0]+[sum(v==0x50+c for v in board) for c in range(1,4)])
    return board


def gameplay(p,tag):
    result={}
    # Compare the actual 8088 RLE decoder against an uncompressed image.
    result['background_ms']=p.call('background')
    raw=video(p,tag)
    art=Image.open(ROOT/f'build/drmario-art/drmarco-{tag}-art.png')
    assert bytes(v for row in pixels(p,tag,raw) for v in row)==bytes(art.getdata())
    p.call('invalidate');p.call('framepaint')
    # All levels, bounded construction, exact counts and no pre-cleared runs.
    times=[]
    for level in range(21):
        p.put('level',level);p.put('seed',struct.pack('<H',0x1234+level*997))
        times.append(p.call('newgame'))
        board=p.data('board',128)
        assert sum(v>=0x50 for v in board)==4*(level+1)
        assert list(p.data('colorcount',4))==[0]+[board.count(0x50+c) for c in range(1,4)]
        p.call('findmatches');assert p.b('matched')==0
    result['max_level_setup_ms']=max(times)
    # Cross intersection: 7 unique cells, counted exactly once.
    cells={8*8+x:0x51 for x in range(1,5)}
    cells.update({y*8+2:0x51 for y in range(6,10)})
    fixture(p,cells);p.call('findmatches')
    assert sum(p.data('marks',128))==7
    p.put('score',bytes(4));p.put('speed',0);p.call('remove')
    assert p.b('viruses')==0 and not any(p.data('board',128))
    assert p.data('colorcount',4)==bytes(4)
    assert int.from_bytes(p.data('score',4),'little')==9500
    # Row boundary must not wrap a horizontal match.
    fixture(p,{6:2,7:2,8:2,9:2});p.call('findmatches');assert p.b('matched')==0
    # Horizontal partner survives a vertical clear and becomes a loose half.
    fixture(p,{64:0x11,65:0x22,56:1,48:1,40:0x51,127:0x53})
    p.call('findmatches');assert sum(p.data('marks',128))==4
    p.call('remove');assert p.data('board',128)[65]==2
    # Linked horizontal pair: one blocked destination holds both cells.
    board=fixture(p,{104:0x11,105:0x22,113:0x53,90:0x31,98:0x42})
    p.call('gravity');got=p.data('board',128)
    assert got[104:106]==board[104:106] and got[113]==0x53
    assert got[98]==0x31 and got[106]==0x42 and got[90]==0
    assert p.b('moved')==1
    # Single half and linked vertical pair settle without moving viruses.
    fixture(p,{0:3,9:0x31,17:0x42,120:0x51})
    for _ in range(18):p.call('gravity')
    got=p.data('board',128)
    assert got[112]==3 and got[120]==0x51 and got[113]==0x31 and got[121]==0x42
    assert p.b('moved')==0
    # Rotation wall kick at right wall; a blocked kick rolls back atomically.
    fixture(p,{127:0x53},0);p.put('x',7);p.put('y',4);p.put('rotation',1)
    p.call('rotate',ax=1);assert (p.b('x'),p.b('rotation'))==(6,2)
    p.put('x',7);p.put('rotation',1);b=bytearray(p.data('board',128));b[38]=1;p.put('board',b)
    p.call('rotate',ax=1);assert (p.b('x'),p.b('rotation'))==(7,1)
    # Spawn collision/game over.
    fixture(p,{3:0x51});p.call('spawn');assert p.b('state')==3
    # Clear resolution -> level-complete; no new capsule until Enter.
    fixture(p,{});p.call('tick');assert p.b('state')==4
    # Natural lock -> match -> flash -> remove -> gravity -> clear.
    fixture(p,{120:0x51,121:0x51,122:0x51},0)
    p.put('x',3);p.put('y',15);p.put('rotation',0);p.put('colors',bytes([1,2]));p.put('falltimer',200)
    p.call('tick');assert p.b('state')==1
    repaint(p,tag)
    for _ in range(30):p.call('tick')
    assert p.b('state')==4 and p.b('viruses')==0
    repaint(p,tag)
    # A second match forms only after the first clear lets a loose half fall.
    cells={y*8+3:0x51 for y in range(12,16)}
    cells.update({120:0x52,121:0x52,122:0x52,91:2})
    fixture(p,cells);p.call('findmatches');p.call('remove')
    for _ in range(40):p.call('tick')
    assert p.b('viruses')==0 and p.b('state')==4
    # Check every tile variant, all bottle rows and both preview halves.
    cells={i:((i//3)%6)*16+i%3+1 for i in range(128)}
    fixture(p,cells,3);p.put('next',bytes([1,3]));p.put('huddirty',1)
    raw=repaint(p,tag)
    broken=bytearray(raw);broken[32*80+24 if tag=='vga' else 12*80+24]^=1
    try:check_pixels(p,tag,broken)
    except AssertionError:pass
    else:raise AssertionError('pixel oracle accepted corrupted VRAM')
    # Ordinary move changes at most four cells; cost includes scanning shadow.
    fixture(p,{127:0x53},0);p.put('x',3);p.put('y',4);p.put('rotation',0);p.put('colors',bytes([1,2]))
    p.call('render');p.call('move',ax=1)
    result['move_render_ms']=p.call('render');repaint(p,tag)
    result['idle_render_ms']=min(p.call('render') for _ in range(8))
    assert result['move_render_ms']<12.0,result
    assert result['idle_render_ms']<0.1,result
    # Pause must hold the model and board, while a mode reentry repaints it.
    p.put('paused',1);before=p.data('board',128);y=p.b('y')
    for _ in range(5):p.call('tick')
    assert before==p.data('board',128) and y==p.b('y')
    return result


def animation_checks(p,tag):
    result={}
    p.put('level',20);p.call('newgame');p.call('render')
    p.put('blinktimer',31)  # include one close/reopen in the timing sample
    board=p.data('board',128);sequence=p.data('sequence',128);seed=p.data('seed',2)
    # Run the complete staggered dance without moving the capsule. Verify
    # every phase against source pixels and an independent full repaint.
    times=[];doctimes=[];virtimes=[];shots=[]
    for beat in range(12):
        for tick in range(4):
            p.call('animtick')
            elapsed=p.call('render');times.append(elapsed)
            if p.b('animclock')==0 and p.b('animslot')%2==0:doctimes.append(elapsed)
            else:virtimes.append(elapsed)
        raw=repaint(p,tag)
        shots.append(native_image(p,tag,raw))
    result['max_animation_render_ms']=max(times)
    result['max_doctor_render_ms']=max(doctimes)
    result['max_virus_render_ms']=max(virtimes)
    assert board==p.data('board',128) and sequence==p.data('sequence',128) and seed==p.data('seed',2)
    assert len({im.tobytes() for im in shots})>3,'animation did not change pixels'
    shots[0].save(ROOT/f'build/drmario-proof/{tag}-dance.gif',save_all=True,
                  append_images=shots[1:],duration=73,loop=0)
    # All eye/mascot transitions; every destination
    # must restore pixels written by ANY previous pose, not just its neighbor.
    for old in range(4):
        for new in range(4):
            p.put('actorpose',[old&1,old,old,old]);p.put('animdirty',1);p.call('render')
            p.put('actorpose',[new&1,new,new,new]);p.put('animdirty',1)
            repaint(p,tag)
    # Clear only red: other colors keep dancing; red gets dizzy, then vanishes.
    fixture(p,{120:0x52,121:0x52,122:0x52,123:0x52,112:0x51,127:0x53})
    p.call('findmatches');p.call('remove')
    assert p.data('colorcount',4)==bytes([0,1,0,1])
    p.put('animslot',2);p.put('animclock',3);p.call('animtick')
    assert p.data('actorpose',4)[2]==2
    repaint(p,tag)
    for _ in range(48):p.call('animtick')
    assert p.data('actorpose',4)[2]==3
    repaint(p,tag)
    # Occasional blinking also works in terminal states, without head movement.
    for state in (3,4):
        p.put('state',state);p.put('blinktimer',0);p.put('huddirty',1)
        p.put('actorpose',bytes([0])+p.data('actorpose',4)[1:]);p.put('animdirty',1)
        repaint(p,tag)
        p.put('animpending',0)
        opened=pixels(p,tag,video(p,tag))
        for beat in range(1,34):
            p.put('animslot',1);p.put('animclock',3);p.call('tick')
            assert p.b('actorpose')==(1 if beat==32 else 0)
            if beat>=32:
                raw=repaint(p,tag);now=pixels(p,tag,raw)
                changed={(x,y) for y in range(len(now)) for x in range(320) if now[y][x]!=opened[y][x]}
                if beat==32:
                    assert changed and all(268<=x<288 and (94 if tag=='vga' else 78)<=y<(103 if tag=='vga' else 85) for x,y in changed)
                else:assert not changed,'reopening did not restore the portrait'
    # Freeze between the even-row and odd-row updates, then resume exactly
    # that pending half-beat. Reentry must preserve the mixed row phases too.
    p.put('animslot',0);p.put('animclock',3);p.call('tick')
    assert p.b('animpending')==1
    p.put('paused',1);p.put('huddirty',1);p.call('render')
    frozen=p.data('animstart',p.sym['dm_animend']-p.sym['dm_animstart'])
    for _ in range(30):p.call('tick')
    assert frozen==p.data('animstart',len(frozen))
    # Full mode entry restores the current poses even halfway through a reaction.
    before=video(p,tag);p.call('invalidate');p.call('framepaint')
    assert before==video(p,tag)
    p.put('paused',0);p.put('huddirty',1);p.call('tick')
    assert p.b('animpending')==0 and p.data('viruspose',8)[1]==p.data('viruspose',8)[5]
    repaint(p,tag)
    print(tag,'animation',result,flush=True)
    assert result['max_animation_render_ms']<40,result
    return result


def native_image(p,tag,raw):
    rgb=((0,0,0),(48,96,252),(252,48,64),(252,224,32),(20,40,120),
         (120,16,20),(120,96,8),(252,252,252)) if tag=='vga' else (
         (0,0,0),(85,255,85),(255,85,85),(255,255,85))
    px=pixels(p,tag,raw)
    return Image.frombytes('RGB',(320,len(px)),bytes(c for row in px for v in row for c in rgb[v]))


def live_key(p,name,seconds=.15,held=False):
    # Separate make/break in guest time. A same-host-instant debugger pair
    # can drop break codes in the emulated XT keyboard (latched scans remain
    # down); physical keys and QEMU sendkey both give the controller time.
    m=p.m;m.bp_exec();m.key(name,up=False);m.run()
    press=seconds if held else min(.04,seconds)
    M.pace(m,press);m.key(name,down=False);M.pace(m,max(.06,seconds-press))
    m.pause();m.bp_exec(p.addr('input'));m.run()
    assert m.wait_stop(30)=='breakpoint'


def input_checks(p,tag):
    p.put('level',0);p.put('speed',0);p.call('newgame');p.call('render')
    live_key(p,'ArrowLeft',.4,held=True)
    assert p.b('x')<=1,'held left did not repeat independently of BIOS typematic'
    p.put('x',3);p.put('y',3);p.put('rotation',0);p.put('dirty',2);p.call('render')
    repaint(p,tag)
    live_key(p,'KeyX');assert p.b('rotation')==3,'clockwise edge failed'
    repaint(p,tag)
    live_key(p,'KeyZ');assert p.b('rotation')==0,'counterclockwise edge failed'
    repaint(p,tag)
    live_key(p,'KeyP');assert p.b('paused')==1
    board=p.data('board',128);y=p.b('y')
    p.m.bp_exec();p.m.run();M.pace(p.m,.8);p.m.pause()
    p.m.bp_exec(p.addr('input'));p.m.run();assert p.m.wait_stop(30)=='breakpoint'
    assert p.b('y')==y and p.data('board',128)==board
    live_key(p,'KeyP',.005);assert p.b('paused')==0, ('resume',p.b('paused'),p.data('keys',2),p.data('pressed',2))
    fixture(p,{});p.call('tick');assert p.b('state')==4
    live_key(p,'Enter');assert p.b('level')==1 and p.b('viruses')==8
    p.put('score',struct.pack('<I',12345));live_key(p,'KeyN')
    assert p.data('score',4)==bytes(4) and p.b('viruses')==8


def arm(tag,sym,input_only=False):
    machine={'vga':'os8088_xt_vga','cga':'os8088_5150_cga_gla'}[tag]
    out=ROOT/'build/drmario-proof';out.mkdir(exist_ok=True)
    with os88ui.boot(str(ROOT/'build/os8088-360.img'),apps=str(ROOT/'build/drmario360.img'),machine=machine) as ui:
        m=ui.m
        ui.open_drive('B');ui.open('DRMARCO.O88');ui.settle()
        p=Probe(ui,sym)
        capture(m,out/(tag+'-launcher.png'))
        m.pause();m.bp_exec(p.addr('input'));m.key('Enter');m.run()
        assert m.wait_stop(30)=='breakpoint','fullscreen entry did not finish'
        assert p.b('fs')==1
        assert p.b('cga')==(tag=='cga')
        assert sum(v>=0x50 for v in p.data('board',128))==4
        assert p.b('viruses')==4
        assert struct.unpack('<HH',p.data('fsi',6)[2:])==((320,240) if tag=='vga' else (320,200))
        print(tag,'entered fullscreen',flush=True)
        m.bp_exec();m.run();M.pace(m,.2);m.pause()
        capture(m,out/(tag+'-play.png'))
        m.bp_exec(p.addr('input'));m.run();assert m.wait_stop(30)=='breakpoint'
        result={} if input_only else gameplay(p,tag)
        if not input_only:result.update(animation_checks(p,tag))
        input_checks(p,tag)
        p.put('level',8);p.call('newgame');p.call('render')
        raw=repaint(p,tag)
        # Export the full 320x240 planes: MartyPC's 25MHz VGA display aperture
        # remains at 400 scanlines after a 400 -> 480 sync change, cropping
        # 40 logical Mode X rows (vga/mod.rs update_clock only shrinks it).
        rgb=((0,0,0),(48,96,252),(252,48,64),(252,224,32),(20,40,120),
             (120,16,20),(120,96,8),(252,252,252)) if tag=='vga' else (
             (0,0,0),(85,255,85),(255,85,85),(255,255,85))
        px=pixels(p,tag,raw)
        M.write_png_rgb(str(out/(tag+'-native.png')),320,len(px),
                        bytes(c for row in px for v in row for c in rgb[v]))
        # Resume actual input delivery, then Escape and reenter the bracket.
        p.put('paused',1);savedboard=p.data('board',128)
        m.bp_exec();m.key('Escape');m.run();M.pace(m,.3);ui.settle()
        assert p.b('fs')==0 and p.data('board',128)==savedboard
        m.pause();m.bp_exec(p.addr('input'));m.key('Enter');m.run()
        assert m.wait_stop(30)=='breakpoint' and p.b('fs')==1
        assert p.data('board',128)==savedboard
        repaint(p,tag)
        print(tag,result,flush=True)
        return result


def qemu_display():
    """Second display implementation for Mode X; never a timing source."""
    import ethernet as E
    import os88qemu as Q
    import os88sym
    import dispcp
    from PIL import Image
    Q.own()
    subprocess.run(['make','test','TESTIMG=build/os8088-360.img',
                    'TESTAPPS=build/drmario360.img'],cwd=ROOT,check=True,
                   stdout=subprocess.DEVNULL)
    m=E.Qemu();mo=E.Mouse();S=os88sym.linear
    try:
        Q.pace(m,8);E.settle(m)
        dispcp.open_drive(m,mo,S,E.settle,'B')
        w=next(w for w in G.windows(m,S) if w.visible and w.title=='Disk')
        dispcp.open_named(m,mo,S,E.settle,w.x,w.y,'DRMARCO.O88')
        win=next(w for w in G.windows(m,S) if w.visible and w.title=='DrMarco')
        raw=m.read(S('wm_wins'),G.MAX_WIN*G.WIN_SIZE)
        base=struct.unpack_from('<H',raw,win.i*G.WIN_SIZE+G.W_SEG)[0]<<4
        sym=symbols()
        m.hmp('sendkey ret');Q.pace(m,2)
        assert m.read(base+sym['dm_fs'],1)==b'\x01'
        assert struct.unpack('<HH',m.read(base+sym['dm_fsi']+2,4))==(320,240)
        m.hmp('sendkey p');Q.pace(m,.2)
        path=ROOT/'build/drmario-proof/vga-qemu.ppm'
        m.hmp('screendump "'+str(path)+'"')
        im=Image.open(path)
        assert im.size in ((320,240),(640,480)),('wrong Mode X dimensions',im.size)
        im.save(path.with_suffix('.png'))
        print('QEMU Mode X display:',im.size,flush=True)
        m.hmp('sendkey esc');Q.pace(m,.3)
        m.hmp('screendump "'+str(path)+'"')
        assert m.read(base+sym['dm_fs'],1)==b'\x00'
        restored=Image.open(path);assert restored.size==(640,480)
        restored.save(path.with_name('vga-qemu-restored.png'))
    finally:Q.kill()


if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--arm',choices=('vga','cga','all'),default='all')
    ap.add_argument('--input-only',action='store_true')
    ap.add_argument('--source',type=Path,default=SOURCE)
    ap.add_argument('--qemu-display',action='store_true')
    a=ap.parse_args()
    SOURCE=a.source
    if a.qemu_display:
        qemu_display();sys.exit(0)
    s=symbols()
    result={tag:arm(tag,s,a.input_only) for tag in (('vga','cga') if a.arm=='all' else (a.arm,))}
    (ROOT/'build/drmario-proof/results.json').write_text(json.dumps(result,indent=2)+'\n')
