#!/usr/bin/env python3
"""Opt-in local-reference audio gate: actual 8088 speaker, AdLib and SB FM.

Use --reference-cpu with py65 installed to independently execute the original
6502 sequencer and compare every imported pitch and duration, including loops.
"""
import argparse
import array
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import drmario as D
import importlib.util
spec=importlib.util.spec_from_file_location('dm_audio_compiler',D.ROOT/'tools/drmario_audio.py')
A=importlib.util.module_from_spec(spec);spec.loader.exec_module(A)


def reference_cpu(source):
    from py65.devices.mpu6502 import MPU
    rom=A.read_rom(source/'bank_FF.asm')
    for name,number in A.SONGS:
        cpu=MPU();mem=cpu.memory
        for addr,b in rom.items():mem[addr]=b
        mem[0xef]=number;mem[0x6cc]=number-1
        def call(pc,trace=None):
            cpu.sp=0xfd;mem[0x1fe]=0xfe;mem[0x1ff]=0x7f;cpu.pc=pc
            for _ in range(20000):
                if cpu.pc==0x7fff:return
                if trace and cpu.pc==0xdf55:trace(cpu.x)
                cpu.step()
            raise AssertionError(f'6502 hung at {cpu.pc:04x}')
        call(0xdc83)
        voices=A.decode(rom,number)
        expected=[]
        for v in voices:
            blocks=v['blocks']
            if v['loop'] is not None:blocks=blocks+blocks[v['loop']:]*2
            expected.append([(d,h) for b in blocks for d,h in b])
        got=[[] for _ in range(3)]
        finite=all(v['loop'] is None for v in voices)
        for frame in range(40000):
            def trace(ch):
                if ch>=3:return
                hz=0
                if rom[0xe16b+mem[0x6c3+ch]]:
                    period=mem[0x680+4*ch]|((mem[0x681+4*ch]&7)<<8)
                    hz=round(1789773/((32 if ch==2 else 16)*(period+1)))
                got[ch].append((mem[0x6b8+ch],hz))
            call(0xddef,trace)
            if finite:
                if mem[0x6fd]==0:break
            elif all(len(g)>=len(e) for e,g in zip(expected,got)):break
        else:raise AssertionError('6502 did not finish the comparison')
        for ch,(e,g) in enumerate(zip(expected,got)):
            if finite:
                # Independently trim at the frame when the 6502 cleared music.
                remaining=frame;trimmed=[]
                for duration,hz in g:
                    if remaining>0:trimmed.append((min(duration,remaining),hz))
                    remaining-=duration
                g=trimmed
                assert sum(d for d,h in e)==frame,(name,ch,'stop frame')
            assert g[:len(e)]==e,(name,ch,'6502 note/duration mismatch')
        print(name,'6502 agrees (intro + three loop passes)',list(map(len,expected)),flush=True)


def driver_symbols():
    names=('opl_own','opl_b0','sbl_str_act','sbl_up')
    source=(D.ROOT/'drivers/sound/sound.asm').read_text()
    with tempfile.TemporaryDirectory() as td:
        p=Path(td)/'probe.asm';b=Path(td)/'probe.bin'
        p.write_text(source+'\n'+'\n'.join('dw '+n for n in names))
        subprocess.run(['nasm','-f','bin','-I','drivers/','-I','drivers/sound/',
                        '-I','apps/','-I','build/','-o',str(b),str(p)],cwd=D.ROOT,check=True)
        return dict(zip(names,struct.unpack('<4H',b.read_bytes()[-8:])))


@contextmanager
def adlib_run(backend):
    if backend!='adlib':yield {};return
    # Private copy of the same XT/VGA profile, with the DSP physically absent.
    base=Path(D.M.base_run_dir())
    with tempfile.TemporaryDirectory(prefix='dm-adlib-',dir=D.ROOT/'build') as td:
        root=Path(td)
        for p in base.iterdir():
            if p.name not in ('configs','media'): (root/p.name).symlink_to(p)
        shutil.copytree(base/'configs',root/'configs')
        for p in (root/'configs').rglob('*.toml'):
            text=p.read_text()
            start=text.find('name = "os8088_xt_vga_sb"')
            if start<0:continue
            end=text.find('\n[[machine]]',start)
            if end<0:end=len(text)
            section=text[start:end]
            import re
            section=re.sub(r'    \[\[machine.sound\]\]\n    type = "SoundBlaster"\n(?:    [^\n]*\n)*','',section)
            p.write_text(text[:start]+section+text[end:])
        (root/'media').mkdir()
        (root/'media/hdds').mkdir()
        for p in (base/'media').iterdir():
            if p.name not in ('floppies','hdds'):(root/'media'/p.name).symlink_to(p)
        try:
            yield dict(run_dir=str(root))
        except Exception:
            log=root/'martypc.log'
            if log.exists():print(log.read_text()[-4000:])
            raise


def check_capture(out,backend):
    peaks={}
    for p in out.glob('audio-'+backend+'.*.wav'):
        raw=bytearray(p.read_bytes())
        assert raw[:4]==b'RIFF' and raw[8:12]==b'WAVE'
        samples=array.array('h',raw[44:])
        peaks[p.name]=max(map(abs,samples),default=0)
        # MartyPC is terminated by the harness, leaving RIFF sizes at zero.
        # Finalize completed captures for ordinary WAV players.
        struct.pack_into('<I',raw,4,len(raw)-8)
        struct.pack_into('<I',raw,40,len(raw)-44)
        p.write_bytes(raw)
    sink='pc_speaker' if backend=='speaker' else 'adlib_music_synthesizer'
    assert peaks['audio-'+backend+'.'+sink+'.wav']>100,peaks
    if backend=='sb':assert peaks['audio-sb.sound_blaster.wav']==0,peaks
    return peaks


def guest(backend,source):
    s=D.symbols();ds=driver_symbols();rom=A.read_rom(source/'bank_FF.asm')
    machine='os8088_xt_vga' if backend=='speaker' else 'os8088_xt_vga_sb'
    out=D.ROOT/'build/drmario-proof';out.mkdir(exist_ok=True)
    os.environ['MARTYPC_WAV']=str(out/('audio-'+backend))
    with adlib_run(backend) as kw, D.os88ui.boot(str(D.ROOT/'build/os8088-360.img'),
            apps=str(D.ROOT/'build/drmario360.img'),machine=machine,**kw) as ui:
        m=ui.m;ui.open_drive('B');ui.open('DRMARCO.O88');ui.settle()
        p=D.Probe(ui,s)
        assert p.data('setting',33)==b'Level 00  Speed LOW  Music FEVER\0'
        m.pause();m.bp_exec(p.addr('input'));m.key('Enter');m.run()
        assert m.wait_stop(30)=='breakpoint'
        assert p.b('audio_live')==1 and p.b('audio_fm')==(backend!='speaker')
        fseg=struct.unpack('<H',m.read(m.sym('drv_fseg'),2))[0]<<4
        def driver(n):return m.read(fseg+ds[n],9 if n.startswith('opl_') else 1)
        if backend!='speaker':
            assert all(v!=255 for v in driver('opl_own')[:4])
            assert driver('sbl_up')[0]==(backend=='sb')
        board=p.data('board',128);seed=p.data('seed',2);seq=p.data('sequence',128)
        result={}
        for song in (0,1):
            p.put('music',song);p.put('paused',0);p.call('audio_song',ax=song)
            voices=A.decode(rom,A.SONGS[song][1]);cursors=[0,0,0];ends=[0,0,0]
            events=[[(d,h) for b in v['blocks'] for d,h in b] for v in voices]
            frames=0;phase=0;times=[]
            for tick in range(256):
                phase+=54;step=1
                if phase>=546:phase-=546;step+=1
                frames+=step
                times.append(p.call('audio_tick'))
                for ch in range(1 if backend=='speaker' else 3):
                    while ends[ch]<frames:
                        duration,hz=events[ch][cursors[ch]]
                        cursors[ch]+=1;ends[ch]+=duration
                    actual=struct.unpack_from('<H',p.data('audio_voices',24),ch*8+6)[0]
                    assert actual==events[ch][cursors[ch]-1][1],(backend,song,tick,ch,actual)
            result[A.SONGS[song][0]]={'mean_ms':sum(times)/len(times),'max_ms':max(times),'min_ms':min(times)}
        # Force three simultaneous music boundaries and the longest effect.
        p.call('audio_song',ax=0);p.call('effect',ax=7)
        result['simultaneous_ms']=p.call('audio_tick')
        # Includes IRQ0/driver work that can land inside the bracket.
        assert result['simultaneous_ms']<8,result
        assert all(result[n]['max_ms']<8 for n in ('fever','chill')),result
        for _ in range(3):p.call('audio_tick')
        if backend!='speaker':assert p.data('audio_pending',3)==bytes(3)
        assert board==p.data('board',128) and seed==p.data('seed',2) and seq==p.data('sequence',128)
        # Ordinary input + logic + move + renderer, charged with the worst
        # sampled audio call, still fits one 54.6Hz XT frame. Animation's
        # pre-existing multi-frame peaks are measured by drmario.py.
        frame_cost=p.call('input')+p.call('tick')+p.call('move',ax=1)+p.call('render')
        peak=max(result['simultaneous_ms'],*(result[n]['max_ms'] for n in ('fever','chill')))
        result['move_frame_with_peak_audio_ms']=frame_cost+peak
        assert frame_cost+peak<1000/54.6,result
        # Music OFF leaves all event cues available, and each ends.
        p.call('audio_quiet');p.call('audio_open');p.put('music',2)
        for effect in range(1,9):
            p.call('effect',ax=effect);p.call('audio_tick')
            assert p.b('fx_priority')==effect
            assert struct.unpack('<H',p.data('fx_hz',2))[0]>0
            for _ in range(16):p.call('audio_tick')
            assert p.b('fx_priority')==0
        p.call('effect',ax=7);p.call('effect',ax=2)
        assert p.b('fx_priority')==7,'move displaced clear'
        # Pausing preserves position, while the pause cue finishes.
        p.put('music',0);p.call('audio_song',ax=0);p.call('audio_tick')
        p.put('paused',1);p.call('audio_quiet');p.call('audio_open');p.call('effect',ax=8)
        frozen=p.data('audio_voices',24)
        for _ in range(20):p.call('audio_tick')
        assert p.data('audio_voices',24)==frozen and p.b('fx_priority')==0
        p.put('paused',0);p.call('audio_restore');p.call('audio_tick')
        assert p.data('audio_voices',24)!=frozen
        p.call('audio_quiet')
        if backend!='speaker':
            assert driver('opl_own')[:4]==bytes([255])*4
            assert all(not b&32 for b in driver('opl_b0')[:4])
            assert driver('sbl_str_act')==b'\x00','game started DMA playback'
            # A foreign claim survives a partial claim refusal and cleanup.
            m.write(fseg+ds['opl_own']+2,b'\xfe')
            p.call('audio_open');assert p.b('audio_fm')==0
            assert driver('opl_own')[:3]==b'\xff\xff\xfe'
            p.call('audio_quiet');m.write(fseg+ds['opl_own']+2,b'\xff')
        else:assert m.inb(0x61)&3==0
        p.call('audio_open');p.call('audio_restore')
        # Real key edges, exit and reentry: no stuck note/claim, board retained.
        D.live_key(p,'KeyM');assert p.b('music')==1
        D.live_key(p,'KeyM');assert p.b('music')==2
        D.live_key(p,'KeyM');assert p.b('music')==0
        D.live_key(p,'KeyP');assert p.b('paused')==1
        D.live_key(p,'KeyP');assert p.b('paused')==0
        m.bp_exec();m.key('Escape');m.run();D.M.pace(m,.3);ui.settle()
        assert p.b('audio_live')==0 and p.b('fs')==0
        if backend!='speaker':assert driver('opl_own')[:4]==b'\xff'*4
        else:assert m.inb(0x61)&3==0
        m.pause();m.bp_exec(p.addr('input'));m.key('Enter');m.run()
        assert m.wait_stop(30)=='breakpoint' and p.b('audio_live')==1
        print(backend,result,flush=True)
    os.environ.pop('MARTYPC_WAV',None)
    result['capture_peak']=check_capture(out,backend)
    return result


if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--backend',choices=('speaker','adlib','sb','all'),default='all')
    ap.add_argument('--source',type=Path,default=D.SOURCE)
    ap.add_argument('--reference-cpu',action='store_true');a=ap.parse_args()
    if a.reference_cpu:reference_cpu(a.source)
    else:
        result={b:guest(b,a.source) for b in (('speaker','adlib','sb') if a.backend=='all' else (a.backend,))}
        path=D.ROOT/('build/drmario-proof/audio-'+a.backend+'-results.json')
        path.write_text(json.dumps(result,indent=2)+'\n')
