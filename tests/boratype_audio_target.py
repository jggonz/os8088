#!/usr/bin/env python3
"""Actual XT music/effects/visual-reaction checks, with no audio clips.

Build boratype, mixertest and os8088-360.img. Drive the installed native
package, capture actual FM or speaker output, verify mute/routes/cleanup.
Waveform activity does not establish subjective music-quality audition.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import traceback
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
import os88ui
import os88marty as M
import os88sym
import os88drvsym
from os88fat import Fat12
import boratype_target as T

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def wav_meta(path):
    if not path.exists():return {'samples':0,'rate':44100,'bytes':0}
    with path.open('rb') as f:raw=f.read(44)
    size=path.stat().st_size
    if len(raw)<44:return {'samples':0,'rate':44100,'bytes':size}
    rate=struct.unpack_from('<I',raw,24)[0];channels=struct.unpack_from('<H',raw,22)[0]
    return {'samples':(size-44)//(2*channels),'rate':rate,'bytes':size}

def finish_wav(path):
    raw=bytearray(path.read_bytes())
    assert raw[:4]==b'RIFF' and raw[36:40]==b'data',path
    struct.pack_into('<I',raw,4,len(raw)-8);struct.pack_into('<I',raw,40,len(raw)-44)
    path.write_bytes(raw)

def signal(path,start,end):
    import numpy as np
    raw=path.read_bytes()
    assert raw[:4]==b'RIFF' and raw[36:40]==b'data',path
    rate=struct.unpack_from('<I',raw,24)[0];channels=struct.unpack_from('<H',raw,22)[0]
    data=np.frombuffer(raw[44:],dtype='<i2')
    if channels>1:data=data.reshape(-1,channels).mean(axis=1)
    data=data[max(0,start):min(len(data),end)].astype(float)/32768
    return {'samples':len(data),'rate':rate,'seconds':len(data)/rate,
            'peak':float(np.max(np.abs(data))) if len(data) else 0,
            'rms':float(np.sqrt(np.mean(data*data))) if len(data) else 0,
            'nonzero':int(np.count_nonzero(data))}

class Driver:
    def __init__(self,m,base,sym):self.m,self.base,self.sym=m,base,sym
    def read(self,off,n):return self.m.read((self.base() if callable(self.base) else self.base)+off,n)
    def byte(self,name):return self.read(self.sym[name],1)[0]
    def word(self,name):return int.from_bytes(self.read(self.sym[name],2),'little')

def validate_signals(ev):
    sink='adlib' if ev['backend']=='sb' else 'pc_speaker'
    for stage in ('title','lesson','success','error'):
        rows=[x for x in ev['signals'] if x['stage']==stage and sink in x['sink']]
        assert rows and rows[0]['rms']>.0005,(stage,'missing actual output')
    muted=[x for x in ev['signals'] if x['stage']=='mute' and sink in x['sink']]
    assert muted and muted[0]['rms']<.0002,'master mute left audible output'

class Probe:
    def __init__(self,ui,app,evidence,cap,backend):
        self.ui,self.app,self.m,self.ev,self.cap=ui,app,ui.m,evidence,cap
        self.markers=[];self.driver=None
        seg=ui._word('drv_fseg')
        if backend=='speaker':assert not seg,'unexpected sound driver on speaker machine'
        else:
            assert seg,'SOUND.DRV not attached'
            D=os88drvsym.symbols('sound.bin');E=os88sym.equates()
            origin=E['DRV_HMA_ORG'] if seg>=0xf000 else 0
            self.driver=Driver(self.m,lambda:(ui._word('drv_fseg')<<4)+origin,D)
            assert self.driver.byte('sbl_up')==1 and self.driver.byte('sbl_str_act')==0
    def v(self,name,n=2):
        at=self.app.sym[name if name in self.app.sym else '_'+name]
        return int.from_bytes(self.app.p.read(at,n),'little')
    def audio(self):
        fields=(('bta_focus',1),('bta_on',1),('bta_hold',1),('bta_lasttag',2),
                ('bta_song',2),('bta_spoke',2),('gkm_cur',2),('gkm_rt',1),('gkm_held',1))
        spans=[(self.app.sym[n if n in self.app.sym else '_'+n],size) for n,size in fields]
        return dict(zip((n for n,_ in fields),(int.from_bytes(raw,'little') for raw in self.app.p.reads(spans))))
    def wait(self,condition,what,seconds=30):
        self.ui._wait(condition,what,seconds,snapshot=lambda:{'audio':self.audio(),'session':self.app.state()})
    def press(self,key,screen=None):
        M.key_taken(self.m,key,ui=False,cap=.675)
        if screen:self.app.wait_screen(screen)
    def type_one(self,ch):
        old=self.app.state()['attempts'];cycles=self.m.status()['cycles']
        M.text_taken(self.m,ch,ui=False)
        self.wait(lambda:self.app.state()['attempts']==old+1,'one scored key')
        self.ev.setdefault('key_state_cycles',[]).append(self.m.status()['cycles']-cycles)
    def tickwait(self,n):
        start=self.ui._word('ticks')
        self.wait(lambda:((self.ui._word('ticks')-start)&65535)>=n,'guest ticks',n/18.2+10)
    def mark(self,name):
        item={'name':name,'guest_cycles':self.m.status()['cycles'],'ticks':self.ui._word('ticks'),
              'audio':self.audio(),'sinks':{p.name:wav_meta(p) for p in self.cap.parent.glob(self.cap.name+'.*.wav')}}
        self.markers.append(item);self.ev['markers']=self.markers
        print(name,json.dumps(item['audio']),flush=True);return item
    def shot(self,name):
        w,h,rgb=self.m.fbuf();path=self.cap.parent/(name+'.png');M.write_png_rgb(str(path),w,h,rgb)
        self.ev.setdefault('screenshots',[]).append(str(path))
    def wrong_with_effect(self):
        # Observe the actual public sequencer call made by real keyboard input.
        self.m.pause();self.m.bp_exec((self.app.p.seg<<4)+self.app.sym['_gkm_sfx']);self.m.run()
        before=self.app.state()['attempts']
        try:
            self.m.key('KeyX');assert self.m.wait_stop(guest=20)=='breakpoint'
            regs=self.m.regs();arg=int.from_bytes(self.m.read((regs['ss']<<4)+regs['sp']+2,2),'little')
            assert arg==3,'wrong input did not request the distinct error effect'
            self.ev.setdefault('effect_cues',[]).append(arg)
        finally:self.m.bp_exec();self.m.run()
        self.wait(lambda:self.app.state()['attempts']==before+1,'wrong key consumed')

def close_probe(p):
    ledger=p.app.resource_snapshot();owner=ledger['instance'];oldseg=ledger['package_segment']
    p.ev['owned_before_close']=ledger
    p.m.key('ControlLeft',down=True,up=False);p.press('KeyQ');p.m.key('ControlLeft',down=False,up=True)
    p.wait(lambda:not any(w.title=='Borat Teaches Typing' for w in p.ui.windows()),'window closed',40)
    p.tickwait(36)
    E=os88sym.equates()
    def owned(mm):
        raw=mm.read(p.ui._S('mem_tab'),E['MEM_MAX']*E['MC_SIZE']);claims=[]
        for i in range(E['MEM_MAX']):
            row=raw[i*E['MC_SIZE']:(i+1)*E['MC_SIZE']]
            seg=struct.unpack_from('<H',row,E['MC_SEG'])[0]
            own=struct.unpack_from('<H',row,E['MC_OWN'])[0]
            if seg and own in (owner,oldseg):claims.append((seg,own))
        return claims
    claims=M.heap_read(p.m,owned,'closed app resource accounting')
    p.ev['owned_after_close']=claims
    assert not claims,'closing Boratype leaked its heap claims'

def panel_page(p,cp,record,E):
    hide=p.ui._byte('cp_hide');row=sum(not(hide&(1<<r)) for r in range(record));cx,cy=cp.content[:2]
    p.ui.mo.click(cx+E['CP_IX']+30,cy+E['CP_I0Y']+row*E['CP_IROWH']+E['CP_IROWH']//2,settle=0)
    p.ui._wait(lambda:p.ui._byte('cp_sel')==record,'OS control page selected',20)

def route_probe(p):
    ui,app,ev=p.ui,p.app,p.ev;E=os88sym.equates();position=app.state()['pos']
    def panel(record):
        ui.menu_pick('Apple','Control Panel');cp=ui.wait_window('Control Panel')
        panel_page(p,cp,record,E);return cp
    def tier(cp,row,route):
        cx,cy=cp.content[:2]
        ui.mo.click(cx+E['CP_RX']+E['CP_PGX']+8,cy+E['CP_B0Y1']+row*E['CP_PROWH']+E['CP_PROWH']//2,settle=0)
        ui._wait(lambda:ui._byte('snd_route')==route,'actual OS output route changed',30)
    def soundbutton(cp):
        table=p.m.read(ui._S('drv_tab'),E['DRV_MAX']*E['DRVR_SIZE'])
        row=next(i for i in range(E['DRV_MAX']) if table[i*E['DRVR_SIZE']+E['DRVR_CLASS']]==E['DRVC_SOUND'])
        visible=row-ui._byte('cp_dtop');assert 0<=visible<E['CP_DVIS'];cx,cy=cp.content[:2]
        return cx+E['CP_RX']+40,cy+E['CP_DBY1']+visible*E['CP_DROWH']+E['CP_DROWH']//2
    p.press('F4','PAUSE');cp=panel(E['CP_ISND'])
    p.wait(lambda:p.v('gkm_held',1)==0,'paused app releases FM channels')
    tier(cp,0,E['SND_RT_SPK']);ui.close(cp);ui.raise_window(app.win);p.press('F4','PLAY')
    p.wait(lambda:p.v('gkm_rt',1)==2 and p.v('gkm_cur')==2,'ordinary resume selects speaker')
    p.mark('route-speaker-start');p.tickwait(36);p.mark('route-speaker-end')
    p.press('F4','PAUSE');cp=panel(E['CP_IDRV']);ui.mo.click(*soundbutton(cp),settle=0)
    ui._wait(lambda:ui._word('drv_fseg')==0,'actual SOUND driver detached',30)
    ui.close(cp);ui.raise_window(app.win);p.press('F4','PLAY')
    p.wait(lambda:p.v('gkm_rt',1)==2 and p.v('gkm_cur')==2,'ordinary resume after detach uses speaker')
    p.press('F4','PAUSE');cp=panel(E['CP_IDRV']);ui.mo.click(*soundbutton(cp),settle=0)
    ui._wait(lambda:ui._word('drv_fseg')!=0,'actual SOUND driver reattached',45)
    panel_page(p,cp,E['CP_ISND'],E);tier(cp,2,E['SND_RT_SB']);ui.close(cp);ui.raise_window(app.win)
    p.press('F4','PLAY');p.wait(lambda:p.v('gkm_rt',1)==1 and p.v('gkm_cur')==2,'ordinary resume restores FM')
    assert app.state()['pos']==position
    ev['os_route_change']='PASS Pause/OS speaker route/Resume, real SOUND detach/Resume fallback, reattach/Resume FM; no Test Music or hardware-loss simulation'

def scenario(p,args):
    ui,app,ev,cap=p.ui,p.app,p.ev,p.cap;sink='adlib' if args.backend=='sb' else 'pc_speaker'
    p.wait(lambda:p.v('gkm_cur')==1,'menu score playing')
    if args.negative:
        p.press('KeyS','SETTINGS');p.press('KeyM');p.press('Escape','HOME')
        p.wait(lambda:p.v('gkm_cur')==0,'negative-control Music disabled');p.tickwait(45)
    start=p.mark('title-start');p.tickwait(45);end=p.mark('title-end')
    titlepath=next(cap.parent/x for x in end['sinks'] if sink in x)
    assert signal(titlepath,start['sinks'][titlepath.name]['samples'],end['sinks'][titlepath.name]['samples'])['rms']>.001,'title waveform silent (intentional with --negative)'
    p.press('F2','LESSONS');p.press('Enter','PLAY');p.wait(lambda:p.v('gkm_cur')==2,'lesson score')
    p.mark('lesson-start');p.tickwait(40);p.mark('lesson-end')
    p.wait(lambda:((ui._word('ticks')-p.v('bta_spoke'))&65535)>=219,'visual reaction cooldown')
    p.mark('error-start');p.wrong_with_effect();p.type_one('x');p.type_one('x')
    p.wait(lambda:p.v('bta_lasttag')==2,'three-error visual roast');p.tickwait(12);p.mark('error-end');p.shot('error-roast')
    if args.route_probe and args.backend=='sb':route_probe(p)
    p.press('F3');p.wait(lambda:app.global_word('bt_master_sound')==0 and p.v('gkm_cur')==0,'immediate master mute')
    p.tickwait(36);p.mark('mute-start');p.tickwait(36);p.mark('mute-end')
    p.press('F3');p.wait(lambda:app.global_word('bt_master_sound')==1 and p.v('gkm_cur')==2,'music resumed')
    passage=app.state()['passage']
    for ch in passage:p.type_one(ch)
    app.wait_screen('RESULTS');p.wait(lambda:p.v('gkm_cur') in (4,5,6),'actual result stinger',60)
    ev['result_stinger']=p.v('gkm_cur')-1;p.mark('success-start');p.shot('result-stinger')
    assert app.state()['committed']==1 and app.state()['net']==len(passage)
    p.wait(lambda:p.v('gkm_cur')==1,'result stinger returned to menu loop',60);p.mark('success-end')
    close_probe(p)
    if args.backend=='sb':
        assert p.driver.byte('sbl_str_act')==0 and p.driver.word('sbl_poolseg')==0
        assert not any(x&32 for x in p.driver.read(p.driver.sym['opl_b0'],9))
    else:assert p.m.inb(0x61)&3==0
    ev['close_cleanup']='PASS app heap claims freed and music/effects resources released'


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--evidence',type=Path,default=ROOT/'build/boratype-audio-target')
    ap.add_argument('--backend',choices=('sb','speaker'),default='sb');ap.add_argument('--machine')
    ap.add_argument('--route-probe',action='store_true');ap.add_argument('--negative',action='store_true')
    args=ap.parse_args();args.evidence=args.evidence.resolve();args.evidence.mkdir(parents=True,exist_ok=True)
    if not args.machine:args.machine='os8088_xt_vga_sb' if args.backend=='sb' else 'os8088_xt_vga'
    build=Path(os88sym.kernel_image()).parent;boot=build/('mixer360.img' if args.backend=='sb' else 'os8088-360.img')
    disk=args.evidence/'scratch-apps.img'
    subprocess.run([sys.executable,str(ROOT/'tools/os88disk.py'),'-o',str(disk),'--size','360',str(build/'boratype.o88'),'--folder','SYSTEM/APPDATA'],check=True)
    assert Fat12(boot).read('KERNEL.SYS')==(build/'kernel.sys').read_bytes()
    assert Fat12(disk).read('BORATYPE.O88')==(build/'boratype.o88').read_bytes()
    if args.backend=='sb':assert Fat12(boot).read('SOUND.DRV')==(build/'sound.drv').read_bytes()
    symbols=T.app_symbols(build);layout=T.target_layout();os88sym.syms()
    ev={'status':'RUNNING','backend':args.backend,'machine':args.machine,
        'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'package_sha256':sha(build/'boratype.o88'),'kernel_sha256':sha(build/'kernel.sys'),
        'boot_sha256':sha(boot),'scratch_sha256':sha(disk),
        'scope':'Original music, effects and visual messages; no sound clips or imports',
        'subjective_listening':'UNVERIFIED: audio helper confirmed audio input is unsupported; waveform assertions are technical only'}
    cap=args.evidence/'audio';os.environ['MARTYPC_WAV']=str(cap);started=time.monotonic()
    try:
        with os88ui.boot(str(boot),apps=str(disk),machine=args.machine,quick=True) as ui:
            ui.open_drive('B');app=T.App(ui,ui.path('B:/BORATYPE.O88',limit=90),symbols,layout,ev)
            scenario(Probe(ui,app,ev,cap,args.backend),args)
        ev['status']='PASS_TECHNICAL'
    except BaseException as error:
        ev['status']='FAIL';ev['error']=repr(error);ev['traceback']=traceback.format_exc();raise
    finally:
        os.environ.pop('MARTYPC_WAV',None);ev['host_seconds']=round(time.monotonic()-started,3)
        for path in cap.parent.glob(cap.name+'.*.wav'):finish_wav(path)
        ev['captures']={path.name:signal(path,0,10**12) for path in cap.parent.glob(cap.name+'.*.wav')}
        ev['signals']=[];marks={m['name']:m for m in ev.get('markers',[])}
        for stage in ('title','lesson','error','mute','success','route-speaker'):
            if stage+'-start' not in marks or stage+'-end' not in marks:continue
            a,b=marks[stage+'-start'],marks[stage+'-end']
            for name,stop in b['sinks'].items():
                ev['signals'].append({'stage':stage,'sink':name,**signal(cap.parent/name,a['sinks'][name]['samples'],stop['samples'])})
        try:
            if ev['status']=='PASS_TECHNICAL':validate_signals(ev)
        except BaseException as error:
            ev['status']='FAIL';ev['error']=repr(error);ev['traceback']=traceback.format_exc();raise
        finally:(args.evidence/'report.json').write_text(json.dumps(ev,indent=2)+'\n')
    print(json.dumps(ev,indent=2));print('Boratype music/effects technical assertions PASS; subjective audition unverified')
if __name__=='__main__':main()
