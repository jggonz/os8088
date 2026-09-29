#!/usr/bin/env python3
"""1942 on an XT with speaker, AdLib only, and Sound Blaster: make 1942soundtest.

Exercise real driver claims/key bits, record each physical audio sink, and
measure guest cycles. Reuse the existing isolated AdLib profile/WAV helpers.
"""
import argparse
import array
import json
import os
import struct
from pathlib import Path
import n1942 as N
import importlib.util
_spec=importlib.util.spec_from_file_location('drmario_audio_test',Path(__file__).with_name('drmario_audio.py'))
_shared=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(_shared)
adlib_run,driver_symbols=_shared.adlib_run,_shared.driver_symbols


def check_capture(out,backend,sampled):
    peaks={}
    for p in out.glob('audio-'+backend+'.*.wav'):
        raw=bytearray(p.read_bytes());samples=array.array('h',raw[44:])
        peaks[p.name]=max(map(abs,samples),default=0)
        struct.pack_into('<I',raw,4,len(raw)-8);struct.pack_into('<I',raw,40,len(raw)-44)
        p.write_bytes(raw)
    sinks=['pc_speaker'] if backend=='speaker' else ['adlib_music_synthesizer']
    if sampled:sinks.append('sound_blaster')
    for sink in sinks:assert peaks['audio-'+backend+'.'+sink+'.wav']>100,peaks
    return peaks


def guest(backend,off,code):
    sampled=backend=='sb' and N.rom_build() is not None
    ds=driver_symbols()
    out=N.ROOT/'build/1942-proof';out.mkdir(exist_ok=True)
    os.environ['MARTYPC_WAV']=str(out/('audio-'+backend))
    machine='os8088_xt_vga' if backend=='speaker' else 'os8088_xt_vga_sb'
    try:
        with adlib_run(backend) as kw, N.os88ui.boot('build/os8088-360.img',
                apps='build/1942-360.img',machine=machine,**kw) as ui:
            m=ui.m;ui.open_drive('B');ui.open('1942.O88');g=N.Game(ui,off,code)
            m.key('Enter');g.frame()
            assert g.get('audiofm',1)==(backend!='speaker')
            assert bool(g.get('pcmseg'))==sampled
            fseg=struct.unpack('<H',m.read(m.sym('drv_fseg'),2))[0]<<4
            def driver(n):return m.read(fseg+ds[n],9 if n.startswith('opl_') else 1)
            if backend!='speaker':
                assert all(v!=255 for v in driver('opl_own')[:2])
                assert driver('sbl_up')[0]==(backend=='sb')
            def call(name,**regs):
                for r,v in regs.items():m.setreg(r,v)
                before=m.status()['cycles'];g.call('n_'+name)
                return (m.status()['cycles']-before)*1000/N.M.GUEST_HZ
            def stream(name,hz,wait=100):
                # Scratch game objects, while invoking only the audio routines.
                ptr=off['n_enemies']+(0 if name=='music' else 24)
                m.write(g.base+ptr,struct.pack('<6H',hz,wait,0,2,65535,0))
                g.put(name+'ptr',ptr);g.put(name+'wait',0)
                return ptr
            def quiet():
                if backend=='sb':assert driver('sbl_str_act')==b'\0'
                if backend=='speaker':assert m.inb(0x61)&3==0
                else:assert all(not b&32 for b in driver('opl_b0')[:2])

            g.put('paused',0,1);g.put('sound',1,1)
            mp=stream('music',440);ep=stream('effect',880)
            peak=call('audio')
            assert g.get('effectfreq')==880
            if backend!='speaker':
                assert g.get('musicfreq')==440
                assert all(b&32 for b in driver('opl_b0')[:2]),'music/effect must overlap'
            else:
                assert g.get('musicptr')==mp,'speaker effect must preempt music'
                assert g.get('tonesent')==880 and m.inb(0x61)&3==3
            held=[call('audio') for _ in range(16)]
            # Mute and pause freeze cursors/waits; resuming a long held note
            # must restore it immediately rather than waiting for a boundary.
            for field in ('sound','paused'):
                g.put(field,0 if field=='sound' else 1,1)
                frozen=[g.data(n+'ptr',6) for n in ('music','effect')]
                call('audio');quiet();idle=call('audio')
                assert frozen==[g.data(n+'ptr',6) for n in ('music','effect')]
                g.put(field,1 if field=='sound' else 0,1);call('audio')
                if backend!='speaker':assert all(b&32 for b in driver('opl_b0')[:2])
                else:assert m.inb(0x61)&3==3
            # Explicit rest, then terminator: no held effect left behind.
            g.put('effectwait',0);call('audio')
            assert g.get('effectfreq')==0
            if backend!='speaker':assert not driver('opl_b0')[1]&32
            g.put('effectwait',0);call('audio')
            assert g.get('effectptr')==0 and g.get('musicfreq')==440
            # Every imported cue must terminate, including cartridge loops.
            table=struct.unpack('<23H',m.read(g.base+code['n_soundtab'],46))
            for sid,start in enumerate(table):
                # Feed the cartridge stream directly: combat cues now have
                # their own FM/PCM arrangements, tested separately below.
                g.put('effectptr',start);g.put('effectwait',0)
                for _ in range(514):
                    g.put('effectwait',0);call('audio')
                    if g.get('effectptr')==0:break
                else:raise AssertionError(('effect did not end',sid,start))
            # The actual gameplay variation repeats twice; Game Over ends.
            songs=json.loads((N.ROOT/N.os88build.at('build/1942-audio.json')).read_text())
            for sid in (14,23,24,7):
                call('music',ax=sid)
                notes=songs[sid]['notes']
                laps=[notes,notes[songs[sid].get('loop_at',0):]] if sid!=7 else [notes]
                for lap in laps:
                    for hz,ticks in lap:
                        g.put('musicwait',0);call('audio')
                        assert g.get('musicfreq')==hz,(sid,hz,g.get('musicfreq'))
                g.put('musicwait',0);call('audio')
                assert bool(g.get('musicptr'))==(sid!=7)
            # Equal adjacent notes retrigger on FM.
            ptr=stream('music',440)
            m.write(g.base+ptr,struct.pack('<6H',440,10,65535,ptr,0,0))
            call('audio');g.put('musicwait',0);call('audio')
            assert g.get('musicptr')==ptr+4 and g.get('musicfreq')==440
            if backend!='speaker':
                assert driver('opl_b0')[0]&32
                assert driver('sbl_str_act')==b'\0','FM must not start a DMA stream'
                # Out-of-range cartridge pitches retain their pitch class.
                for hz,expected in ((12000,6000),(9,36)):
                    stream('music',hz);call('audio');assert g.get('musicsent')==expected
            pcm_cost=[]
            if sampled:
                g.fixture();g.put('flightphase',0);g.put('grace',60000)
                bank=(N.ROOT/'build/1942.SFX').read_bytes()
                for sid,offset,length in ((18,16,1024),(19,1040,3072),(20,4112,4096)):
                    call('audio_quiet');call('effect',ax=sid)
                    pcm_cost.append(call('audio'))
                    assert g.get('pcmactive',1)==1 and driver('sbl_str_act')==b'\x01'
                    ring=m.read((g.get('pcmseg')<<4)+(0,4112,8224)[sid-18],4096)
                    assert ring[:length]==bank[offset:offset+length]
                    assert ring[length:]==b'\x80'*(4096-length)
                    assert driver('opl_b0')[0]&32,'PCM stopped FM music'
                    if sid==19:
                        call('effect',ax=18)
                        assert g.get('pcmpending',1)==0,'shot interrupted explosion'
                    g.frame(14)
                    assert g.get('pcmactive',1)==0,'one-shot did not finish'
                for field in ('sound','paused'):
                    call('effect',ax=20);call('audio')
                    assert g.get('pcmactive',1)==1
                    g.put(field,0 if field=='sound' else 1,1);call('audio')
                    assert g.get('pcmactive',1)==0 and driver('sbl_str_act')==b'\0'
                    g.put(field,1 if field=='sound' else 0,1)
                # Coalesce several events in a frame to the strongest sample.
                call('effect',ax=18);call('effect',ax=20);call('effect',ax=18)
                assert g.get('pcmpending',1)==3
                call('audio');assert g.get('pcmlen')==4096
                call('audio_quiet')
                # An absent optional bank is a playable FM fallback.
                call('pcm_close');at=g.base+code['n_pcmfile'];saved=m.read(at,1)
                m.write(at,b'Z');call('pcm_open');assert g.get('pcmseg')==0
                call('effect',ax=18);call('audio');assert g.get('effectfreq')==180
                m.write(at,saved)
                # Driver refusal (invalid external-ring flags) also falls back
                # within the same frame and releases the DMA claim.
                call('pcm_open');assert g.get('pcmseg')!=0
                g.put('pcmflags',2,1);call('effect',ax=18);call('audio')
                assert g.get('pcmseg')==0 and g.get('pcmactive',1)==0
                assert g.get('effectfreq')==180
                assert max(pcm_cost[1:])<15,pcm_cost # first call discovers IRQ
            elif backend!='speaker':
                call('effect',ax=18);call('audio');assert g.get('effectfreq')==180
                call('effect',ax=19);call('audio');assert g.get('effectfreq')==80
            call('audio_close');quiet()
            if backend!='speaker':
                assert driver('opl_own')[:2]==b'\xff\xff'
                # Explicit speaker preference wins even with a physical card.
                route=m.sym('snd_route');oldroute=m.read(route,1)
                m.write(route,b'\x01');call('audio_open')
                assert g.get('audiofm',1)==0
                assert driver('opl_own')[:2]==b'\xff\xff'
                call('audio_close');m.write(route,oldroute)
                # Second-channel refusal rolls back the first claim only.
                m.write(fseg+ds['opl_own']+1,b'\xfe')
                call('audio_open');assert g.get('audiofm',1)==0
                assert driver('opl_own')[:2]==b'\xff\xfe'
                stream('effect',880);call('audio');assert g.get('tonesent')==880
                call('audio_close');m.write(fseg+ds['opl_own']+1,b'\xff')
            call('audio_open')
            # Restore actual game objects and test the real keyboard/exit path.
            g.fixture();g.frame(2)
            g.key('KeyM');assert g.get('sound',1)==0;quiet()
            g.key('KeyM');assert g.get('sound',1)==1
            g.key('KeyP');assert g.get('paused',1)==1;quiet()
            m.bp_exec();m.key('Escape');m.run();N.M.ui_done(m)
            assert g.get('infs',1)==0 and g.get('audiofm',1)==0 and g.get('pcmseg')==0;quiet()
            if backend!='speaker':assert driver('opl_own')[:2]==b'\xff\xff'
            m.key('Enter');g.frame()
            assert g.get('audiofm',1)==(backend!='speaker')
            # Failed graphics load after acquiring FM must also release it.
            m.bp_exec();m.key('Escape');m.run();N.M.ui_done(m)
            at=g.base+code['n_vfile'];saved=m.read(at,1);m.write(at,b'Z')
            m.key('Enter');m.run()
            N.M.until(m,lambda _:g.get('error',1)==2 and g.get('infs',1)==0,'bad bank cleanup',limit=60)
            N.M.ui_done(m);quiet()
            if backend!='speaker':assert driver('opl_own')[:2]==b'\xff\xff'
            m.write(at,saved)
            result=dict(two_notes_ms=peak,held_mean_ms=sum(held)/len(held),muted_ms=idle,pcm_start_ms=pcm_cost)
            assert peak<8 and max(held)<8,result
            print(backend,result,flush=True)
    finally:
        os.environ.pop('MARTYPC_WAV',None)
    result['capture_peak']=check_capture(out,backend,sampled)
    return result


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--backend',choices=('speaker','adlib','sb','all'),default='all')
    a=ap.parse_args();off,code=N.symbols()
    result={b:guest(b,off,code) for b in (('speaker','adlib','sb') if a.backend=='all' else (a.backend,))}
    (N.ROOT/'build/1942-proof/audio-results.json').write_text(json.dumps(result,indent=2)+'\n')
