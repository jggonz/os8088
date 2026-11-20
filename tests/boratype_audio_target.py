#!/usr/bin/env python3
"""Actual XT Sound Blaster/OPL Boratype capture, optional local pack.

make boratypedisk mixertest first. --voice-pack points to local restricted
material outside the repository; it is copied ONLY to an external scratch disk.
No listening claim follows from waveform activity. Needs MartyPC, numpy and
actual built native artifacts. --negative proves the audible title assertion.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import traceback
import wave
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
import os88ui
import os88marty as M
import os88sym
import os88drvsym
from os88fat import Fat12
import boratype_target as T
import os88btvoice as V

def synthetic_pack(directory):
    """An original stdlib-only transport fixture, never character/package audio."""
    directory.mkdir(parents=True,exist_ok=True)
    rows=bytearray();payload=bytearray();meta=[];offset=V.HEADER+3*V.ROW
    for ident,tag,hz in ((0,1,440),(1,2,180),(2,9,880)):
        label={1:'welcome',2:'error',9:'success'}[tag]
        pcm=bytearray()
        for i in range(8000):
            freq=hz if i<4000 else hz*1.5
            envelope=min(1,i/80,(7999-i)/80)
            pcm.append(128+int(96*envelope*math.sin(2*math.pi*freq*i/8000)))
        pcm+=b'\x80'*192
        caption=('Test tone: '+label).encode('ascii')
        rows+=struct.pack('<BBBBHH48s',ident,tag,1,ident,offset,len(pcm),caption)
        payload+=pcm;offset+=len(pcm)
        with wave.open(str(directory/(label+'.wav')),'wb') as w:
            w.setparams((1,1,8000,0,'NONE','not compressed'));w.writeframes(pcm)
        meta.append({'id':ident,'tag':tag,'caption':caption.decode(),'rights':'Original synthetic test signal'})
    data=bytearray(struct.pack('<4sBBHHH4s',b'BTV1',3,0,8000,offset,0,b'\0'*4))+rows+payload
    struct.pack_into('<H',data,10,V.checksum(data));V.check(data)
    pack=directory/'TEST.VPK';pack.write_bytes(data)
    (directory/'sources.json').write_text(json.dumps(meta,indent=2)+'\n')
    return pack

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

def pcm_identity(path,pack):
    """Match the captured DAC waveform to the imported greeting, without ASR.

    The legacy DSP time constant rounds a requested rate to1MHz/integer.
    Emulated DAC holds each8-bit source value between sample deadlines.
    Allow a few initial output samples, compare normalized waveform shape.
    This establishes source identity, not subjective intelligibility.
    """
    import numpy as np
    raw=path.read_bytes();output=np.frombuffer(raw[44:],dtype='<i2').astype(float)
    rate=struct.unpack_from('<I',raw,24)[0];inputrate=struct.unpack_from('<H',pack,6)[0]
    row=next(struct.unpack_from('<BBBBHH48s',pack,16+56*i) for i in range(pack[4])
             if pack[16+56*i+1]==1)
    pcm=np.frombuffer(pack[row[4]:row[4]+row[5]],dtype='u1').astype(float)-128
    # Use a rational division: the DSP's true rate is not integerHz.
    physicalrate=1000000/(1000000//inputrate)
    n=int(len(pcm)*rate/physicalrate)
    expected=pcm[np.minimum(len(pcm)-1,(np.arange(n)*physicalrate/rate).astype(int))]
    matches=[]
    for lag in range(40):
        obs=output[lag:lag+n]
        if len(obs)==n:matches.append((float(np.corrcoef(expected,obs)[0,1]),lag))
    corr,lag=max(matches)
    return {'correlation':corr,'lag_samples':lag,'compared_samples':n,
            'source_rate':inputrate,'physical_rate':physicalrate,'capture_rate':rate}

def validate_signals(ev):
    sink='adlib' if ev['backend']=='sb' else 'pc_speaker'
    for stage in ('title','lesson','success'):
        rows=[x for x in ev['signals'] if x['stage']==stage and sink in x['sink']]
        assert rows and rows[0]['rms']>(.001 if stage=='title' else .0005),(stage,'missing OPL activity')
    muted=[x for x in ev['signals'] if x['stage']=='mute' and sink in x['sink']]
    assert muted and muted[0]['rms']<.0002,'master mute left audible OPL output'
    if ev['backend']=='sb':
        gaps=[x for x in ev.get('pcm_midclip_pause_observations',[]) if x['samples']]
        assert not gaps,'PCM ring paused before clip end: UI rendering starved the nonblocking stream'
        assert ev['captures']['audio.sound_blaster.wav']['rms']>.01,'PCM capture silent'
        assert ev['pcm_waveform_identity']['correlation']>.8,'captured PCM differs from imported voice'

class Driver:
    def __init__(self,m,base,sym):self.m,self.base,self.sym=m,base,sym
    def read(self,off,n):return self.m.read((self.base() if callable(self.base) else self.base)+off,n)
    def byte(self,name):return self.read(self.sym[name],1)[0]
    def word(self,name):return int.from_bytes(self.read(self.sym[name],2),'little')

class Probe:
    def __init__(self,ui,app,evidence,cap,backend="sb"):
        self.ui,self.app,self.m,self.ev,self.cap=ui,app,ui.m,evidence,cap
        self.D=os88drvsym.symbols('sound.bin')
        seg=ui._word('drv_fseg')
        self.driver=None
        if backend=='speaker':
            assert not seg,'speaker fixture unexpectedly attached SOUND.DRV'
            self.markers=[];return
        assert seg,'SOUND.DRV not attached'
        origin=os88sym.equates()['DRV_HMA_ORG'] if seg>=0xf000 else 0
        self.driver=Driver(self.m,lambda:(ui._word('drv_fseg')<<4)+origin,self.D)
        assert self.driver.byte('sbl_up')==1,'attached driver has no Sound Blaster'
        self.markers=[]
    def v(self,name,n=2):
        at=self.app.sym[name if name in self.app.sym else '_'+name]
        return int.from_bytes(self.app.p.read(at,n),'little')
    def audio(self):
        names=('bta_voice','btv_live','btv_granted','bta_focus','bta_on','bta_hold',
               'bta_pack','bta_lasttag','bta_song','btv_source','btv_length','btv_total','btv_rate')
        spans=[(self.app.sym[n if n in self.app.sym else '_'+n],1 if i<6 else 2) for i,n in enumerate(names)]
        return dict(zip(names,(int.from_bytes(raw,'little') for raw in self.app.p.reads(spans))))
    def wait(self,condition,what,seconds=20):
        self.ui._wait(condition,what,seconds,snapshot=lambda:{'audio':self.audio(),'session':self.app.state()})
    def press(self,key,screen=None):
        M.key_taken(self.m,key,ui=False,cap=0.675)
        if screen:self.app.wait_screen(screen)
    def type_one(self,ch):
        old=self.app.state()['attempts'];c=self.m.status()['cycles']
        M.text_taken(self.m,ch,ui=False)
        self.wait(lambda:self.app.state()['attempts']==old+1,'one scored printable key',30)
        self.ev.setdefault('key_state_latency_cycles',[]).append(self.m.status()['cycles']-c)
    def tickwait(self,n):
        start=self.ui._word('ticks')
        self.wait(lambda:((self.ui._word('ticks')-start)&65535)>=n,f'{n} guest ticks',n/18.2+10)
    def mark(self,name):
        item={'name':name,'guest_cycles':self.m.status()['cycles'],'ticks':self.ui._word('ticks'),
              'audio':self.audio(),'sinks':{p.name:wav_meta(p) for p in self.cap.parent.glob(self.cap.name+'.*.wav')}}
        self.markers.append(item);self.ev['markers']=self.markers
        print(name,json.dumps(item['audio']),flush=True);return item
    def shot(self,name):
        w,h,rgb=self.m.fbuf();p=self.cap.parent/(name+'.png');M.write_png_rgb(str(p),w,h,rgb)
        self.ev.setdefault('screenshots',[]).append(str(p))
    def voice(self,tag,trigger):
        before=self.mark('voice-'+str(tag)+'-before')
        trigger()
        self.wait(lambda:self.v('btv_live',1)==1,'nonblocking PCM voice opened',30)
        state=self.audio();assert state['bta_voice']==1 and state['btv_rate'] in range(4000,11026)
        assert self.driver.byte('sbl_str_act')==1
        assert self.v('gkm_cur')==0 and self.v('bta_hold',1)==1,'music not paused around voice'
        # The real driver's grant holds PCM copied from the preloaded pack.
        grant=self.v('btv_grant');pool=self.driver.word('sbl_poolseg')
        source=self.m.read((state['bta_pack']<<4)+state['btv_source'],min(4096,state['btv_length']))
        staged=self.m.read((pool<<4)+grant,len(source));assert staged==source,'driver grant contains wrong PCM'
        self.ev.setdefault('pcm_byte_checks',[]).append({'tag':tag,'source_sha256':hashlib.sha256(source).hexdigest(),
            'bytes_compared':len(source),'length':state['btv_length'],'rate':state['btv_rate'],
            'caption':self.app.p.read(self.app.sym['btv_text'],48).split(b'\0')[0].decode('ascii')})
        self.shot('voice-'+str(tag)+'-playing')
        midclip=[]
        def finished():
            if not self.v('btv_live',1):return True
            if self.driver.byte('sbl_paused') and self.driver.word('sbl_consumed')<state['btv_length']:
                midclip.append({'cycles':self.m.status()['cycles'],'consumed':self.driver.word('sbl_consumed'),
                                'total':self.v('btv_total')})
            return False
        M.until(self.m,lambda _:finished(),'PCM padded clip reached end',poll=.005,guest=15)
        self.ev.setdefault('pcm_midclip_pause_observations',[]).append({'tag':tag,'samples':midclip})
        assert self.driver.byte('sbl_str_act')==0 and self.v('btv_granted',1)==0
        self.tickwait(8)
        if tag==2:
            # Wait on the renderer's committed caption cache before claiming
            # an on-glass written roast; an in-flight repaint is not evidence.
            idx=self.v('bta_message');ptr=self.app.p.word(self.app.sym['_bt_jokes']+idx*6)
            text=self.app.p.read(ptr,48).split(b'\0')[0]
            h=5381
            for ch in text:h=((h*33)^ch)&65535
            self.wait(lambda:self.v('tf_captionkey')==h,'written roast painted',30)
            self.shot('voice-2-roast-painted')
        after=self.mark('voice-'+str(tag)+'-after')
        self.ev.setdefault('voice_spans',[]).append((before,after))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--voice-pack',type=Path,help='optional real local pack; default is original synthetic Test tones')
    ap.add_argument('--evidence',type=Path,default=ROOT/'build/boratype-audio-target')
    ap.add_argument('--validate-report',type=Path,help='recheck captured waveform/continuity assertions without another guest run')
    ap.add_argument('--machine')
    ap.add_argument('--backend',choices=('sb','speaker'),default='sb')
    ap.add_argument('--route-probe',action='store_true',help='also change the actual OS output route and detach SOUND')
    ap.add_argument('--negative',action='store_true')
    args=ap.parse_args()
    if args.validate_report:
        validate_signals(json.loads(args.validate_report.read_text()))
        print('Captured audio assertions PASS');return
    args.evidence=args.evidence.resolve();args.evidence.mkdir(parents=True,exist_ok=True)
    # Keep restricted sources and captures out of the source checkout/build distribution.
    actual_pack=args.voice_pack is not None
    if actual_pack:assert not args.evidence.is_relative_to(ROOT),'restricted local pack evidence must be outside repository'
    else:args.voice_pack=synthetic_pack(args.evidence/'synthetic-transport-fixture')
    args.voice_pack=args.voice_pack.resolve();V.check(args.voice_pack.read_bytes())
    if not args.machine:args.machine='os8088_xt_vga_sb' if args.backend=='sb' else 'os8088_xt_vga'
    build=Path(os88sym.kernel_image()).parent
    stage=args.evidence/'scratch';stage.mkdir(exist_ok=True)
    shutil.copy2(build/'boratype.o88',stage/'BORATYPE.O88');shutil.copy2(args.voice_pack,stage/'BORAT.VPK')
    disk=args.evidence/'scratch-apps.img'
    subprocess.run([sys.executable,str(ROOT/'tools/os88disk.py'),'-o',str(disk),'--size','360',
        str(stage/'BORATYPE.O88'),str(stage/'BORAT.VPK'),'--folder','SYSTEM/APPDATA'],check=True)
    boot=build/('mixer360.img' if args.backend=='sb' else 'os8088-360.img')
    assert Fat12(boot).read('KERNEL.SYS')==(build/'kernel.sys').read_bytes(), 'boot disk contains a stale compressed kernel'
    assert Fat12(disk).read('BORATYPE.O88')==(build/'boratype.o88').read_bytes()
    if args.backend=='sb':assert Fat12(boot).read('SOUND.DRV')==(build/'sound.drv').read_bytes(), 'boot driver differs from mapped artifact'
    syms=T.app_symbols(build);layout=T.target_layout();os88sym.syms()
    ev={'status':'RUNNING','source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'machine':args.machine,'kernel_sha256':sha(os88sym.kernel_image()),'package_sha256':sha(build/'boratype.o88'),
        'boot_sha256':sha(boot),'scratch_disk_sha256':sha(disk),'voice_pack_sha256':sha(args.voice_pack),
        'subjective_listening':'BLOCKED: no audio-input/listening tool; no claim from waveform/ffprobe/afplay',
        'voice_fixture':'actual restricted local pack' if actual_pack else 'synthetic transport fixture; does not pass character-voice US10',
        'backend':args.backend,'driver_sha256':sha(build/'sound.drv') if args.backend=='sb' else None,'restricted_audio':'external local test only; no distribution assets' if actual_pack else 'none'}
    cap=args.evidence/'audio';os.environ['MARTYPC_WAV']=str(cap)
    started=time.monotonic()
    try:
      with os88ui.boot(str(boot),apps=str(disk),machine=args.machine,quick=True) as ui:
        ev['emulator_ping']=ui.m.cmd(cmd='ping')
        ui.open_drive('B');app=T.App(ui,ui.path('B:/BORATYPE.O88',limit=90),syms,layout,ev)
        p=Probe(ui,app,ev,cap,args.backend);assert p.v('bta_pack') and p.v('bta_packstate')==1,'local PCM pack did not preload'
        if args.backend=='speaker':
            speaker_scenario(p,args)
        else:
            sb_scenario(p,args)
      ev['status']='PASS_TECHNICAL'
    except BaseException as error:
      ev['status']='FAIL';ev['error']=repr(error);ev['traceback']=traceback.format_exc();raise
    finally:
      os.environ.pop('MARTYPC_WAV',None)
      ev['host_seconds']=round(time.monotonic()-started,3)
      for path in cap.parent.glob(cap.name+'.*.wav'):finish_wav(path)
      ev['captures']={path.name:signal(path,0,10**12) for path in cap.parent.glob(cap.name+'.*.wav')}
      ev['signals']=[]
      marks={m['name']:m for m in ev.get('markers',[])}
      for name in ('title','lesson','mute','success','error','route-speaker'):
        if name+'-start' not in marks or name+'-end' not in marks:continue
        a,b=marks[name+'-start'],marks[name+'-end']
        for sink,end in b['sinks'].items():
          path=cap.parent/sink;start=a['sinks'].get(sink,{'samples':0})['samples']
          ev['signals'].append({'stage':name,'sink':sink,**signal(path,start,end['samples'])})
      try:
        if ev['status']=='PASS_TECHNICAL':
          if args.backend=='sb':
            ev['pcm_waveform_identity']=pcm_identity(cap.parent/'audio.sound_blaster.wav',args.voice_pack.read_bytes())
          validate_signals(ev)
      except BaseException as error:
        ev['status']='FAIL';ev['error']=repr(error);ev['traceback']=traceback.format_exc();raise
      finally:(args.evidence/'report.json').write_text(json.dumps(ev,indent=2)+'\n')
    print(json.dumps(ev,indent=2));print('Boratype actual guest technical audio checks complete; subjective listening remains blocked')

def sb_scenario(p,args):
        ui,app,ev,cap=p.ui,p.app,p.ev,p.cap
        p.wait(lambda:p.v('gkm_cur')==1,'menu score playing')
        assert p.v('bta_voice_have',1)==1 and p.v('btv_length')>0,'automatic welcome never streamed'
        assert p.v('bta_voice_at')>0 and p.v('bta_spoke')==p.v('bta_voice_at'),'welcome used stale tick0'
        ev['automatic_welcome']='PASS first-focused tick streamed imported welcome without Test Voice input'
        if args.negative:
            # Break the subject through actual UI: a muted title must fail
            # the same audible-title assertion, not a synthetic threshold.
            p.press('KeyS','SETTINGS');p.press('KeyM');p.press('Escape','HOME')
            p.wait(lambda:p.v('gkm_cur')==0,'negative-control music disabled');p.tickwait(45)
        p.mark('title-start');p.tickwait(45);end=p.mark('title-end')
        start=p.markers[-2]
        titlepath=next(cap.parent/x for x in end['sinks'] if 'adlib' in x)
        title=signal(titlepath,start['sinks'][titlepath.name]['samples'],end['sinks'][titlepath.name]['samples'])
        assert title['rms']>.001,'title OPL waveform is silent (intentional with --negative)'
        assert p.v('gkm_held',1)==1 and any(x&32 for x in p.driver.read(p.D['opl_b0'],9))
        # Settings A auditions real imported greeting through live UI.
        p.press('KeyS','SETTINGS');p.voice(1,lambda:p.m.key('KeyA'))
        p.press('Escape','HOME');p.press('F2','LESSONS');p.press('Enter','PLAY')
        p.wait(lambda:p.v('bta_song')==1,'relaxed lesson score');p.mark('lesson-start');p.tickwait(40);p.mark('lesson-end')
        p.wait(lambda:((ui._word('ticks')-p.v('bta_spoke'))&65535)>=219,'spoken reaction cooldown',30)
        p.type_one('x');p.type_one('x');p.voice(2,lambda:p.m.key('KeyX'))
        assert app.state()['error_run']==3 and p.v('bta_lasttag')==2
        # Interrupt a live greeting, rather than muting an already quiet card.
        p.press('F2','LESSONS');p.press('Escape','HOME');p.press('KeyS','SETTINGS');p.m.key('KeyA')
        p.wait(lambda:p.v('btv_live',1)==1,'audition active before mute')
        p.mark('mute-before');p.press('F3');p.wait(lambda:app.global_word('bt_master_sound')==0,'master muted')
        p.tickwait(36);p.mark('mute-start');p.tickwait(36);p.mark('mute-end')
        assert p.driver.byte('sbl_str_act')==0 and p.v('gkm_cur')==0
        assert not any(x&32 for x in p.driver.read(p.D['opl_b0'],9))
        p.press('F3');p.wait(lambda:app.global_word('bt_master_sound')==1 and p.v('gkm_cur')==1,'music resumed after mute')
        p.press('Escape','HOME');p.press('F2','LESSONS');p.press('Enter','PLAY')
        # Complete the actual loaded drill; no score/callback injection.
        text=app.state()['passage'];p.mark('completion-before')
        for ch in text:p.type_one(ch)
        app.wait_screen('RESULTS');p.mark('success-result');p.shot('results')
        p.wait(lambda:p.v('gkm_cur') in (4,5,6),'result stinger actually started',60)
        ev['result_stinger']=p.v('gkm_cur')-1;p.mark('success-start')
        assert app.state()['committed']==1 and app.state()['net']==len(text)
        p.wait(lambda:p.v('gkm_cur')==1,'result stinger returned to menu loop',60)
        p.mark('success-end')
        if args.route_probe:route_probe(p)
        # Close while a real voice is live: kernel teardown must free its grant.
        p.press('Escape','HOME');p.press('KeyS','SETTINGS');p.m.key('KeyA')
        p.wait(lambda:p.v('btv_live',1)==1,'voice active before close');p.mark('close-voice')
        close_probe(p);assert p.driver.byte('sbl_str_act')==0
        assert not any(x&32 for x in p.driver.read(p.D['opl_b0'],9))
        ev['close_cleanup']='PASS no stream and no OPL key-on after real close'

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

def speaker_scenario(p,args):
    app,ui,ev=p.app,p.ui,p.ev
    assert p.v('gkm_rt',1)==2 and not ui._word('drv_fseg')
    p.wait(lambda:p.v('gkm_cur')==1,'speaker menu loop');p.mark('title-start');p.tickwait(45);p.mark('title-end')
    p.press('F2','LESSONS');p.press('Enter','PLAY')
    p.wait(lambda:p.v('gkm_cur')==2,'speaker lesson loop');p.mark('lesson-start');p.tickwait(40);p.mark('lesson-end')
    p.wait(lambda:((ui._word('ticks')-p.v('bta_spoke'))&65535)>=219,'caption cooldown',30)
    p.mark('error-start')
    for _ in range(3):p.type_one('x')
    p.wait(lambda:p.v('bta_lasttag')==2,'visual roast with speaker fallback')
    p.tickwait(12);p.mark('error-end');p.shot('speaker-error')
    assert not p.v('btv_live',1) and not p.v('btv_granted',1)
    ev['pcm_unavailable']='PASS no sound driver; original written roast and speaker cue, no PCM claim'
    text=app.state()['passage'];p.mark('completion-before')
    for ch in text:p.type_one(ch)
    app.wait_screen('RESULTS');p.wait(lambda:p.v('gkm_cur') in (4,5,6),'speaker result stinger',60)
    ev['result_stinger']=p.v('gkm_cur')-1;p.mark('success-start')
    p.wait(lambda:p.v('gkm_cur')==1,'speaker stinger returned to menu loop',60);p.mark('success-end')
    p.press('F3');p.wait(lambda:p.v('gkm_cur')==0,'speaker master mute')
    p.tickwait(36);p.mark('mute-start');p.tickwait(36);p.mark('mute-end')
    assert p.m.inb(0x61)&3==0,'muted speaker gate remains on'
    p.press('F3');p.wait(lambda:p.v('gkm_cur')==1,'speaker unmute')
    close_probe(p);assert p.m.inb(0x61)&3==0
    ev['close_cleanup']='PASS speaker gate off and every app-owned heap claim freed'

def route_probe(p):
    """Actual OS controls: focus loss precedes route changes/detach.

    This is supported user-facing reconfiguration, not a simulated hot-unplug.
    Control rectangles come from live window geometry and assembled equates.
    """
    ui,app,ev=p.ui,p.app,p.ev;E=os88sym.equates()
    p.press('Escape','HOME');p.press('KeyS','SETTINGS');p.m.key('KeyA')
    p.wait(lambda:p.v('btv_live',1)==1,'voice active before OS panel');p.mark('route-before-panel')
    ui.menu_pick('Apple','Control Panel');cp=ui.wait_window('Control Panel')
    p.wait(lambda:p.v('bta_focus',1)==0 and p.v('btv_live',1)==0 and p.v('gkm_held',1)==0,
           'focus loss releases voice and FM before reconfiguration',30)
    def select(record):
        hide=ui._byte('cp_hide');row=sum(not(hide&(1<<r)) for r in range(record))
        cx,cy=cp.content[:2]
        ui.mo.click(cx+E['CP_IX']+30,cy+E['CP_I0Y']+row*E['CP_IROWH']+E['CP_IROWH']//2,settle=0)
        ui._wait(lambda:ui._byte('cp_sel')==record,'OS control page selected',20)
    def tier(row,route):
        cx,cy=cp.content[:2]
        ui.mo.click(cx+E['CP_RX']+E['CP_PGX']+8,
            cy+E['CP_B0Y1']+row*E['CP_PROWH']+E['CP_PROWH']//2,settle=0)
        ui._wait(lambda:ui._byte('snd_route')==route,'OS audio output route changed',30)
    select(E['CP_ISND']);tier(0,E['SND_RT_SPK']);ui.close(cp)
    ui.raise_window(app.win);p.wait(lambda:p.v('gkm_rt',1)==2 and p.v('gkm_cur')==1,'app resumes on real speaker route',30)
    p.mark('route-speaker-start');p.tickwait(36);p.mark('route-speaker-end')
    # Detach using Drivers after foreground focus has been relinquished.
    ui.menu_pick('Apple','Control Panel');cp=ui.wait_window('Control Panel');select(E['CP_IDRV'])
    p.wait(lambda:p.v('gkm_held',1)==0 and not p.v('btv_live',1),'app audio released for detach')
    cx,cy=cp.content[:2]
    table=p.m.read(ui._S('drv_tab'),E['DRV_MAX']*E['DRVR_SIZE'])
    soundrow=next(i for i in range(E['DRV_MAX']) if table[i*E['DRVR_SIZE']+E['DRVR_CLASS']]==E['DRVC_SOUND'])
    visible=soundrow-ui._byte('cp_dtop');assert 0<=visible<E['CP_DVIS']
    button=(cx+E['CP_RX']+40,cy+E['CP_DBY1']+visible*E['CP_DROWH']+E['CP_DROWH']//2)
    ui.mo.click(*button,settle=0);ui._wait(lambda:ui._word('drv_fseg')==0,'actual SOUND driver detached',30)
    ev['device_change']='PASS actual Drivers detach after focus cleanup; no hot-unplug/hardware-loss claim'
    ui.mo.click(*button,settle=0);ui._wait(lambda:ui._word('drv_fseg')!=0,'actual SOUND driver reattached',45)
    select(E['CP_ISND']);tier(2,E['SND_RT_SB']);ui.close(cp);ui.raise_window(app.win)
    p.wait(lambda:p.v('gkm_rt',1)==1 and p.v('gkm_cur')==1,'FM restored after OS reattach',30)
    ev['os_route_change']='PASS Sound page speaker route and restored Sound Blaster route; focus cleanup verified'
if __name__=='__main__':main()
