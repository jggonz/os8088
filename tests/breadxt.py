#!/usr/bin/env python3
"""Installed Breadboard XT UI, durability and adapter proof on an actual8088.

Requires make breadxt breadxtdisk. Each launch uses private floppy copies.
Run --only vga|cga|herc|256; BREADXT_SHOTS keeps real adapter screenshots.
The VGA leg cold-boots its saved floppy and launches the .BBX association,
then measures real two-channel555 capture before and after a native RB edit.
Root's breadxt_target.py owns numeric ABI, stacks and capacity/performance.
"""
import argparse
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT/'tools'))
import breadxtbbx as BBX
import heapmap
import os88build
import os88flush
import os88geom as G
import os88marty as M
import os88ui
import os88wire

MACHINES = {'vga':'os8088_xt_vga', 'cga':'os8088_5150_cga_gla',
            'herc':'os8088_5150_herc_gla', '256':'os8088_5150_cga_gla_256k',
            'bundle':'os8088_xt_vga'}
DOC_SIZE, DOC_META = 3106, 3072

def meaningful_doc(raw):
    np,nw=struct.unpack_from('<HH',raw,DOC_META)
    return raw[:16*np]+raw[1024:1024+16*nw]+raw[DOC_META:]
RESULT_SIZE, SCOPE_SIZE = 330, 2068
SAVED = 'PLACED.BBX'


def symbols():
    """The linked map, checked against the exact primary binary on this run."""
    build = Path(os88build.at('build')).resolve()
    with tempfile.TemporaryDirectory(prefix='breadxt-map-') as folder:
        folder = Path(folder)
        mp, binary = folder/'symbols.map', folder/'primary.bin'
        subprocess.run(['nasm','-f','bin','-w+error','-I',str(ROOT/'apps')+'/',
                        '-I',str(build)+'/', '--before',f'[map all {mp}]',
                        '-o',str(binary),str(ROOT/'apps/breadxt/breadxt.asm')],
                       cwd=ROOT,check=True)
        assert binary.read_bytes() == (build/'breadxt.bin').read_bytes(), 'stale BreadXT map'
        answer = {}
        for line in mp.read_text().splitlines():
            match = re.match(r'\s*([0-9A-F]+)\s+([0-9A-F]+)\s+(\w+)\s*$',line)
            if match:
                answer[match[3]] = int(match[1],16)
    disk = os88flush.Volume((build/'breadxt360.img').read_bytes())
    assert disk.read('BREADXT.O88') == (build/'breadxt.o88').read_bytes(), 'stale installed package'
    return answer


def shot(ui, kind, name):
    folder = os.environ.get('BREADXT_SHOTS')
    if not folder:
        return
    path = Path(folder);path.mkdir(parents=True,exist_ok=True)
    ui.still()
    target = path/f'{kind}-{name}.png'
    if kind in ('cga','herc','256'):
        w,h,rows = ui.m.vram('herc' if kind=='herc' else 'cga')
        M.write_png(str(target),w,h,rows)
    else:
        w,h,rgb = ui.m.fbuf();M.write_png_rgb(str(target),w,h,rgb)
    print(f'  evidence {target}',flush=True)


def heap(ui):
    return heapmap.Map(ui.m,{n:ui._S(n) for n in ('mem_base','mem_top','spl_live','mem_tab')})


class App:
    def __init__(self,ui,offsets,name='BREADXT.O88'):
        self.ui,self.s = ui,offsets
        self.heap_before=heap(ui)
        self.win = ui.open(name)
        self.p = M.Pkg(ui.m,self.win)
        ui._wait(lambda:any(m[0]=='Parts' for m in ui.menus()),'BreadXT native menus',30)
        self.wait(lambda q:q['bu_initialized'] and not q['op'],'initial application callbacks',60)

    def word(self,name,offset=0):
        return self.p.word(self.s['_'+name]+offset)

    def state(self):
        spans = [(self.s['_bb_doc']+DOC_META,34), (self.s['_bb_result']+320,10),
                 (self.s['_bb_scope']+2048,20)]
        names = ('bb_dirty','bb_valid','bb_running','bb_recompile','bu_op','bu_step_once','bu_flags','bu_mode','bu_initialized')
        raw = self.p.reads(spans+[(self.s['_'+n],2) for n in names])
        meta = struct.unpack('<17H',raw[0]);result=struct.unpack('<5H',raw[1]);scope=struct.unpack('<10H',raw[2])
        q = dict(zip(names,(int.from_bytes(v,'little') for v in raw[3:])))
        q.update(np=meta[0],nw=meta[1],panx=meta[5],pany=meta[6],dt=meta[8],
                 t=result[0]+65536*result[1],diag=result[2],part=result[3],net=result[4],
                 head=scope[0],count=scope[1],cycles=scope[9],
                 period=scope[5]+65536*scope[6],high=scope[7]+65536*scope[8])
        q.update(op=q['bu_op'],valid=q['bb_valid'],running=q['bb_running'],dirty=q['bb_dirty'])
        return q

    def wait(self,predicate,what,guest=120):
        last=[None]
        def ready():
            last[0]=self.state()
            return predicate(last[0])
        try:
            self.ui._wait(ready,what,guest,snapshot=lambda:last[0])
        except Exception:
            print('BreadXT last state:',last[0], 'error=',self.word('bb_error'), 'message=',self.p.read(self.s['_bu_msg'],80).split(b'\0')[0],flush=True)
            raise
        return last[0]

    def quiet(self):
        self.wait(lambda q:not q['op'] and not q['bu_step_once'],'pending command completion',120)

    def open(self,name,count):
        assert not self.state()['dirty'],('would conceal dirty prompt',self.state())
        self.ui.menu_pick('File','Open...');self.ui.chooser_open(getattr(self,'open_prefix','')+name)
        self.open_prefix=''
        self.wait(lambda q:q['np']==count and not q['op'] and not q['dirty'] and not q['valid'] and q['bb_recompile'] and self.p.read(self.s['_bu_name'],13).split(b'\0')[0]==name.encode('ascii'),name+' native load',60)

    def save_as(self,name):
        self.ui.menu_pick('File','Save As...');self.ui.chooser_save(name)
        self.wait(lambda q:not q['op'] and not q['dirty'] and self.p.read(self.s['_bu_name'],13).split(b'\0')[0]==name.encode('ascii'),name+' native save commit',90)

    def save(self):
        self.ui.menu_pick('File','Save')
        self.wait(lambda q:not q['op'] and not q['dirty'],'native save commit',90)

    def step(self):
        before=self.state();self.ui.menu_pick('Simulate','Step')
        q=self.wait(lambda q:not q['op'] and not q['bu_step_once'] and (q['t']!=before['t'] or not before['valid'] and q['valid'] or q['diag']),
                    'one completed numeric timestep',300)
        assert q['valid'] and not q['diag'],q
        return q

    def run_to(self,predicate,what,guest=6000):
        initial=self.state();cycles_before=int(self.ui.m.status()['cycles'])
        self.ui.menu_pick('Simulate','Run')
        progress=[-2500,-1]
        def done(q):
            if q['t']>=progress[0]+2500 or q['cycles']!=progress[1]:
                progress[:]=[q['t'],q['cycles']]
                print(f'  progress {what}: sim{q["t"]/10000:.4f}s cycles={q["cycles"]} guestcycles={int(self.ui.m.status()["cycles"])-cycles_before}',flush=True)
            if q['diag'] and not q['op'] and not q['bb_recompile']:
                raise AssertionError(('unexpected simulation diagnosis',q))
            return predicate(q)
        q=self.wait(done,what,guest)
        self.ui.menu_pick('Simulate','Pause')
        paused=self.wait(lambda q:not q['op'] and not q['running'],'Pause commit',60)
        used=int(self.ui.m.status()['cycles'])-cycles_before
        simulated=(paused['t']-initial['t'])/10000
        print(f'  calculation {what}: simulated{simulated:.6f}s, {used}guest cycles/{used/M.GUEST_HZ:.3f}XTseconds, {simulated/(used/M.GUEST_HZ):.6f}simulated seconds per XTsecond',flush=True)
        return paused

    def reset(self):
        self.ui.menu_pick('Simulate','Reset')
        self.wait(lambda q:not q['op'] and q['t']==0 and not q['running'],'Reset state',90)

    def currents(self):
        return struct.unpack('<64h',self.p.read(self.s['_bb_result']+128,128))

    def doc(self):
        return self.p.read(self.s['_bb_doc'],DOC_SIZE)

    def part(self,index):
        return struct.unpack('<8H',self.p.read(self.s['_bb_doc']+16*index,16))

    def hole_xy(self,hole):
        q=self.state();gx,gy,pitch=(self.word(n) for n in ('bu_gx','bu_gy','bu_pitch'))
        row,col=divmod(hole,30)
        return gx+8+(col-q['panx'])*pitch,gy+(row-q['pany'])*pitch+((row>=7)-(q['pany']>=7))*pitch+5

    def click(self,x,y):
        self.ui.mo.click(x,y,settle=0);M.ui_done(self.ui.m,'BreadXT click')

    def part_click(self,index):
        p=self.part(index);a,b=self.hole_xy(p[2]),self.hole_xy(p[3])
        self.click((a[0]+b[0])//2,(a[1]+b[1])//2)
        assert self.word('bu_selected')==p[0],('wrong part selected',index,self.word('bu_selected'))

    def edit_value(self,index,text,value):
        self.part_click(index);self.ui.type('e')
        self.wait(lambda q:q['bu_mode']==6,'native value editor',30)
        for _ in range(24):self.ui.press('Backspace')
        self.ui.type(text);self.ui.press('Enter')
        self.wait(lambda q:not q['op'] and q['dirty'],'value command committed',60)
        assert self.part(index)[5]==value,(index,self.part(index),value)
        assert not self.state()['running'] and not self.state()['valid'],'edit did not pause/stale'

    def capture(self):
        raw=self.p.read(self.s['_bb_scope'],SCOPE_SIZE)
        head,count=struct.unpack_from('<HH',raw,2048)
        order=[(head-count+i)&255 for i in range(count)]
        ch=[struct.unpack_from('<256h',raw,512*j) for j in range(2)]
        lo=struct.unpack_from('<256H',raw,1024);hi=struct.unpack_from('<256H',raw,1536)
        return [(lo[i]+65536*hi[i],ch[0][i],ch[1][i]) for i in order]

    def close(self):
        if self.state()['dirty']:self.save()
        owner=self.ui.m.read(self.ui._S('wm_owner'),G.MAX_WIN)[self.win.i]
        segment=self.p.seg;overlay=self.p.word(self.s['cc_ovseg'])
        loaded=heap(self.ui)
        held=[c for c in loaded.claims if c.own in (owner,segment)]
        assert held and any(c.seg<=overlay<c.end for c in held),('missing region/overlay claim',segment,overlay,held)
        self.ui.close(self.win)
        self.ui._wait(lambda:not any(c.own in (owner,segment) for c in heap(self.ui).claims),
                      'package claims released',60)
        # Destroyed records are skipped before timer dispatch; the kernel
        # deliberately retains inactive handler/timer words until slot reuse.
        record=G.winptr(self.ui.m,self.win.i)
        assert int.from_bytes(self.ui.m.read(record+G.W_FLAGS,2),'little')==0
        assert self.ui.m.read(self.ui._S('wm_owner')+self.win.i,1)==b'\xff'
        assert owner not in G.instances(self.ui.m),'instance survived close'
        after=heap(self.ui)
        def room(mp):
            runs=mp.runs();return 16*sum(p for _,p in runs),16*max([p for _,p in runs] or [0])
        print(f'  heap free/largest before={room(self.heap_before)} loaded={room(loaded)} after={room(after)}; close released{sum(c.para for c in held)*16}bytes incl overlay and timer',flush=True)


def led(app,negative=False):
    app.open('LEDTEST.BBX',4);app.step()
    if negative:
        app.p.write(app.s['_bb_result']+128+3*2,struct.pack('<h',0))
    currents=app.currents()
    assert (currents[0],currents[2],currents[3])==(-8824,8824,8824),currents[:4]
    print('  installed LED source/load direction verified',flush=True)


def editor(ui,symbols,folder,kind):
    app=App(ui,symbols)
    ui.menu_pick('Parts','Resistor')
    for row,col in ((8,4),(8,9)):
        app.click(*app.hole_xy(row*30+col))
    assert app.word('bu_ready')==1,'placement preview missing'
    ui.press('Enter');app.wait(lambda q:q['np']==1 and not q['op'],'mouse placed resistor',60)
    # Create a bent wire in empty upper strips, through the actual preview.
    ui.type('w');app.click(*app.hole_xy(2*30+4));app.click(*app.hole_xy(2*30+12));ui.type('b')
    app.click(*app.hole_xy(5*30+12));assert app.word('bu_ready')==1
    ui.press('Enter');app.wait(lambda q:q['nw']==1 and not q['op'],'bent wire commit',60)
    ui.menu_pick('Edit','Undo');app.wait(lambda q:q['nw']==0 and not q['op'],'wire Undo',60)
    ui.menu_pick('Edit','Redo');app.wait(lambda q:q['nw']==1 and not q['op'],'wire Redo',60)
    app.save_as(SAVED);before=app.doc();shot(ui,kind,'placed-saved')
    ui.menu_pick('File','New');app.wait(lambda q:q['np']==0 and q['nw']==0 and not q['op'],'New baseline',60)
    app.open(SAVED,1);assert meaningful_doc(app.doc())==meaningful_doc(before),'native reload changed saved source'
    led(app)
    app.edit_value(2,'680',68);app.step();assert app.currents()[2:4]==(4348,4348)
    for item,value,current in (('Undo',33,8824),('Redo',68,4348),('Undo',33,8824)):
        ui.menu_pick('Edit',item);app.wait(lambda q:not q['op'] and app.part(2)[5]==value,item+' changed resistor',60)
        app.step();assert app.currents()[2:4]==(current,current)
    # Bind two real component pins; these stable bindings are persisted too.
    ui.type('p');app.click(*app.hole_xy(app.part(3)[2]))
    assert app.word('bb_doc',3094)==1 and app.word('bb_doc',3098)==app.part(3)[0]
    ui.press('Tab');app.click(*app.hole_xy(app.part(0)[2]));ui.type('v')
    assert app.word('bb_doc',3096)==1 and app.word('bb_doc',3100)==app.part(0)[0]
    app.save_as('RUNLED.BBX');before=app.doc();shot(ui,kind,'led-edited-probed-saved')
    # Independent FAT+BBX reader sees both source documents written by guest.
    flushed=(Path(os.environ['BREADXT_SHOTS']).resolve()/'saved-apps.img') if os.environ.get('BREADXT_SHOTS') else Path(folder)/'saved-apps.img';os88flush.Flush(marty=ui.m).save(1,str(flushed))
    volume=os88flush.Volume(flushed.read_bytes())
    decoded=BBX.decode(volume.read(SAVED))
    assert len(decoded['parts'])==1 and len(decoded['wires'])==1
    assert decoded['wires'][0]['nbend']==1,decoded['wires']
    runnable=BBX.decode(volume.read('RUNLED.BBX'))
    assert len(runnable['parts'])==4 and runnable['parts'][2]['value']==33
    assert runnable['metadata']['probe_id_a']==runnable['parts'][3]['id']
    app.close()
    return flushed,before


def flasher(app,kind):
    app.open('FLASHER.BBX',9)
    for _ in range(2):app.ui.menu_pick('Simulate','Larger timestep')
    app.wait(lambda q:q['dt']==40 and not q['op'],'4ms timestep',60)
    app.reset();slow=app.run_to(lambda q:q['cycles']>=3,'three settled flasher cycles')
    check_flasher(app,slow,68000,0.02)
    shot(app.ui,kind,'flasher-68k-paused')
    # A real edit pauses/stales, then alters the measured waveform period.
    app.edit_value(4,'10k',1000)
    # At a208ms period1ms bounds threshold discretization within2percent.
    for _ in range(2):app.ui.menu_pick('Simulate','Smaller timestep')
    app.wait(lambda q:q['dt']==10,'1ms fast-RB timestep',60)
    app.reset();fast=app.run_to(lambda q:q['cycles']>=3,'three fast-RB cycles')
    check_flasher(app,fast,10000,0.02)
    assert fast['period']<slow['period']/2,(slow,fast)
    # Pause must freeze capacitor/time/capture. Step advances exactly dt.
    t=fast['t'];capture=app.capture();M.guest_sleep(app.ui.m,0.25)
    assert app.state()['t']==t and app.capture()==capture,'Pause advanced state'
    stepped=app.step();assert stepped['t']==t+stepped['dt'],(t,stepped)
    shot(app.ui,kind,'flasher-10k-stepped');app.save_as('FAST555.BBX')
    # Select the endpoint of the real RB-to-threshold connection. This endpoint
    # is not an inserted part lead, so selection resolves the wire itself.
    app.ui.type('v');app.click(*app.hole_xy(9*30+12))
    assert app.word('bu_selected_kind')==2,'timing wire endpoint not selected'
    before=app.state()['nw'];app.ui.menu_pick('Edit','Delete')
    app.wait(lambda q:q['nw']==before-1 and not q['op'],'timing connection deletion',60)
    app.reset();broken=app.run_to(lambda q:q['t']>=3*fast['period'],'broken timing connection observation')
    assert broken['cycles']==0,'removed timing wire still oscillates'
    assert app.currents()[8]>0,'broken timing example should remain latched HIGH'
    shot(app.ui,kind,'flasher-timing-wire-removed')
    app.ui.menu_pick('Edit','Undo');app.wait(lambda q:q['nw']==before and not q['op'],'timing wire restored',60)
    assert not app.state()['dirty'],'Undo did not restore saved source baseline'
    app.reset();recovered=app.run_to(lambda q:q['cycles']>=1,'restored timing oscillation')
    assert recovered['period']>0



def check_flasher(app,state,rb,tolerance):
    period=math.log(2)*(10000+2*rb)*10e-6
    duty=(10000+rb)/(10000+2*rb)
    measured=state['period']/10000;actual_duty=state['high']/state['period']
    assert abs(measured/period-1)<=tolerance,(rb,measured,period)
    assert abs(actual_duty/duty-1)<=tolerance,(rb,actual_duty,duty)
    capture=app.capture();assert len(capture)>10
    assert all(b[0]>a[0] for a,b in zip(capture,capture[1:])),'scope timestamps not increasing'
    assert {v[1] for v in capture}=={0,5000},'scopeA did not capture both levels'
    assert max(v[2] for v in capture)-min(v[2] for v in capture)>1400,'scopeB capacitor trace flat'
    assert all(0<=v[2]<=5000 for v in capture)
    print(f'  RB={rb} period={measured:.6f}s oracle={period:.6f}s duty={actual_duty:.4f} oracle={duty:.4f} dt={state["dt"]/10:g}ms',flush=True)


def diagnostics(app,kind):
    app.open('BROKEN.BBX',3);app.ui.menu_pick('Simulate','Step')
    app.wait(lambda q:q['diag']==21 and not q['op'] and not q['bu_step_once'],'missing LED limiter diagnosis',180)
    assert not app.state()['valid']
    app.ui.menu_pick('View','Help')
    x,y,h=(app.word(n) for n in ('bu_x','bu_y','bu_h'));app.click(x+8,y+h-5)
    assert app.word('bu_mode')==0 and app.word('bu_selected')==app.part(2)[0],'diagnostic Locate failed'
    shot(app.ui,kind,'diagnostic-located')


def minimum_window(app,kind):
    ui=app.ui;app.ui.menu_pick('View','Breadboard');app.part_click(2)
    original=ui._refresh(app.win)
    def resize(width,height):
        live=ui._refresh(app.win)
        print('  grow before', (live.x,live.y,live.w,live.h,live.flags),ui._dragwords(),flush=True)
        ui.mo.drag(live.x+live.w-6,live.y+live.h-6,
                   min(live.x+width-6,G.word(ui.m,'vid_w')-1),min(live.y+height-6,G.word(ui.m,'vid_h')-1),settle=0)
        ui._wait(lambda:width<=ui._refresh(app.win).w<=width+7 and ui._refresh(app.win).h==height if width==320 else ui._refresh(app.win).w>=width-16 and ui._refresh(app.win).h>=height-2,
                 f'grow to {width}x{height}',60,snapshot=lambda:((ui._refresh(app.win).x,ui._refresh(app.win).y,ui._refresh(app.win).w,ui._refresh(app.win).h,ui._refresh(app.win).flags),ui._dragwords()))
        M.ui_done(ui.m,'minimum-window layout')
        live=ui._refresh(app.win);print('  grow achieved',(live.x,live.y,live.w,live.h),flush=True)
    resize(320,128)
    assert app.word('bu_compact')==1
    ui.menu_pick('View','Inspector');shot(ui,kind,'minimum-inspector')
    ui.menu_pick('View','Breadboard');value=app.part(2)[5]
    ui.type('e');app.wait(lambda q:q['bu_mode']==6,'minimum value editor',30)
    shot(ui,kind,'minimum-value');ui.press('Escape');assert app.part(2)[5]==value
    ui.menu_pick('View','Help');shot(ui,kind,'minimum-help')
    resize(original.w,original.h);ui.menu_pick('View','Breadboard')
    print('  native grow320x128 inspector/value/help and restore verified',flush=True)


def adapter(ui,offsets,kind,negative=False):
    app=App(ui,offsets);led(app,negative)
    if kind=='256':assert int.from_bytes(ui.m.read(0x413,2),'little')==256,'wrong RAM machine'
    expected_compact=kind in ('cga','256')
    assert bool(app.word('bu_compact'))==expected_compact,(kind,app.word('bu_compact'))
    if expected_compact:
        for _ in range(6):ui.press('ArrowDown')
        ui.menu_pick('View','Inspector');app.wait(lambda q:q['bu_mode']==2,'compact inspector callback',60)
        shot(ui,kind,'inspector')
        ui.menu_pick('View','Breadboard')
    shot(ui,kind,'led-board')
    ui.menu_pick('View','Oscilloscope');shot(ui,kind,'scope')
    if expected_compact:app.wait(lambda q:q['bu_mode']==3,'compact scope callback',60)
    ui.menu_pick('View','Breadboard')
    if app.state()['dirty']:app.save()
    minimum_window(app,kind);diagnostics(app,kind);app.close()


def bundle_image(folder):
    build=Path(os88build.at('build'));archive=(build/'BREADXT.WPK').read_bytes()
    assert not os88wire.arc_verify(archive,'BREADXT.WPK')
    header,entries=os88wire.arc_read(archive,'BREADXT.WPK')
    assert header['home']=='BREADXT'
    args=[];wanted={}
    for i,entry in enumerate(entries):
        path=Path(folder)/f'entry{i}'/entry['name'];path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(entry['data'])
        dest='/'.join((header['home'],)+entry['folders'])
        args.append(f'{dest}:{path}')
        wanted[dest+'/'+entry['name']]=entry['data']
    target=Path(folder)/'installed-wpk.img'
    subprocess.run([sys.executable,'tools/os88disk.py','--size','360','-o',str(target)]+args,cwd=ROOT,check=True)
    volume=os88flush.Volume(target.read_bytes())
    for name,data in wanted.items():assert volume.read(name)==data,name
    assert volume.read('BREADXT/BREADXT.O88')==(build/'breadxt.o88').read_bytes()
    return target


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--only',choices=tuple(MACHINES))
    parser.add_argument('--editor-only',action='store_true')
    parser.add_argument('--flasher-only',action='store_true')
    parser.add_argument('--negative-led',action='store_true')
    args=parser.parse_args();started=time.monotonic();os.chdir(ROOT)
    sys.stdout.reconfigure(line_buffering=True)
    assert not(args.editor_only and args.flasher_only),'choose one scenario'
    offsets=symbols()
    for kind in ([args.only] if args.only else MACHINES):
        begin=time.monotonic()
        with tempfile.TemporaryDirectory(prefix='breadxt-native-') as folder:
            image=str(bundle_image(folder)) if kind=='bundle' else 'build/breadxt360.img'
            with os88ui.boot('build/os8088-360.img',apps=image,machine=MACHINES[kind],quick=True) as ui:
                ui.open_drive('B')
                if kind=='bundle':
                    ui.open('BREADXT',expect='nav');app=App(ui,offsets);app.open_prefix='BREADXT/';led(app,args.negative_led)
                    shot(ui,kind,'fresh-install-led');minimum_window(app,kind);app.close()
                elif kind=='vga' and args.flasher_only:
                    app=App(ui,offsets);flasher(app,kind);diagnostics(app,kind);app.close()
                elif kind=='vga':
                    saved,before=editor(ui,offsets,folder,kind)
                else:
                    adapter(ui,offsets,kind,args.negative_led)
            if kind=='vga' and not args.flasher_only:
                # Disable snapshot reuse here: this is intentionally a cold
                # boot with the independently flushed, changed application disk.
                previous=os.environ.get('OS88_BOOTCACHE');os.environ['OS88_BOOTCACHE']='0'
                try:
                    with os88ui.boot('build/os8088-360.img',apps=str(saved),machine=MACHINES[kind],quick=True) as ui:
                        ui.open_drive('B');app=App(ui,offsets,'RUNLED.BBX')
                        app.wait(lambda q:q['np']==4 and not q['op'] and not q['dirty'] and app.p.read(app.s['_bu_name'],13).split(b'\0')[0]==b'RUNLED.BBX','cold association document load',90)
                        assert meaningful_doc(app.doc())==meaningful_doc(before),'cold association launch changed document'
                        print('  cold-boot BBX association retained source/probes/view',flush=True)
                        app.step();assert (app.currents()[0],app.currents()[2],app.currents()[3])==(-8824,8824,8824)
                        shot(ui,kind,'led-source-current')
                        if not args.editor_only:
                            flasher(app,kind)
                            # RC UI control behavior; exact numerical RC oracle
                            # is the independent target numeric row's ownership.
                            app.open('RC.BBX',4);app.step();t=app.state()['t']
                            app.ui.menu_pick('Simulate','Pause');assert app.state()['t']==t
                            app.step();assert app.state()['t']==t+app.state()['dt']
                            shot(ui,kind,'rc-stepped');diagnostics(app,kind)
                        app.close()
                finally:
                    if previous is None:os.environ.pop('OS88_BOOTCACHE',None)
                    else:os.environ['OS88_BOOTCACHE']=previous
        print(f'breadxt native {kind}: PASS elapsed {time.monotonic()-begin:.2f}s',flush=True)
    print(f'breadxt native total elapsed {time.monotonic()-started:.2f}s',flush=True)
    return 0


if __name__=='__main__':
    raise SystemExit(main())
