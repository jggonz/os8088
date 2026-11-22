#!/usr/bin/env python3
"""Verify the optional soundtrack and event sounds at the emulated Sound Blaster output."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import os88btpcm as codec
import os88sym
import os88ui
import boratype_audio_target as audio
import boratype_target as target


def synthetic_pack(folder):
    """Use short, distinct band-limited signals; CI needs no personal recordings."""
    rng = np.random.default_rng(134)
    def track(seconds, amplitude):
        count = codec.RATE * seconds
        samples = np.interp(np.arange(count), np.arange(0, count, 5),
                            rng.uniform(-amplitude, amplitude, (count+4)//5))
        return np.rint(samples).astype('int16').reshape(-1, 4)
    music = codec.bank([track(5, 27)], True)
    effects = codec.bank([track(1, 89) for _ in codec.EFFECTS], False)
    (folder/'BTMUSIC.BTA').write_bytes(music)
    (folder/'BTCLIPS.BTA').write_bytes(effects)
    (folder/'BTAUDIO.IDX').write_bytes(struct.pack('<4sHH',b'BTP1',len(music),len(effects)))


def decoded(raw, index, music):
    pos, n = struct.unpack_from('<HH', raw, 12+4*index)
    table = np.frombuffer(raw[64:1088], dtype='uint8' if music else 'int8').reshape(256,4)
    return table[np.frombuffer(raw[pos:pos+n], dtype='uint8')].reshape(-1).astype(float) - (128 if music else 0)


def matches(capture, reference, period=None):
    reference = reference - reference.mean()
    n = len(reference)
    size = 1 << (len(capture)+n-1).bit_length()
    corr = np.fft.irfft(np.fft.rfft(capture,size)*np.fft.rfft(reference[::-1],size),size)
    energy = np.r_[0, np.cumsum(capture*capture)]
    scores = corr[n-1:len(capture)] / np.sqrt(np.maximum(1,energy[n:]-energy[:-n]) * (reference @ reference))
    if period is not None:
        # Pausing or reopening a stream can produce another match. Require
        # the same excerpt to recur exactly one encoded track length later.
        first = int(np.minimum(scores[:-period], scores[period:]).argmax())
        return [(i / codec.RATE, float(scores[i])) for i in (first, first + period)]
    peaks = []
    for _ in range(2):
        i = int(scores.argmax())
        peaks.append((i/codec.RATE, float(scores[i])))
        scores[max(0,i-n):i+n] = 0
    return peaks


def exercise(build, pack, out):
    disk = out/'test.img'
    cap = out/'audio'
    subprocess.run([sys.executable,str(ROOT/'tools/os88disk.py'),'-o',str(disk),'--size','360',
                    str(build/'boratype.o88'),*(str(pack/name) for name in ('BTMUSIC.BTA','BTCLIPS.BTA','BTAUDIO.IDX')),
                    '--folder','SYSTEM/APPDATA'],check=True)
    os.environ['MARTYPC_WAV'] = str(cap)
    symbols, layout = target.app_symbols(build), target.target_layout()
    evidence = {'app_build':str(build)}
    with os88ui.boot(str(build/'mixer360.img'),apps=str(disk),machine='os8088_xt_vga_sb',quick=True) as ui:
        ui.open_drive('B')
        app = target.App(ui,ui.path('B:/BORATYPE.O88',limit=110),symbols,layout,evidence)
        probe = audio.Probe(ui,app,evidence,cap,'pcm')
        probe.wait(lambda: probe.v('bt_pcm_live',1)==1, 'PCM stream opened')
        probe.tickwait(42)
        last = [probe.v('btp_musicpos')]
        def wrapped():
            pos = probe.v('btp_musicpos')
            old, last[0] = last[0], pos
            return old-pos > 3000
        probe.wait(wrapped, 'music source wrapped', 55)
        # Let a second recognizable portion reach the card.
        probe.tickwait(64)
        app.press('F2').wait_screen('LESSONS')
        app.press('Enter').wait_screen('PLAY')
        evidence["large_portrait"] = probe.v("tf_bigready")
        assert evidence["large_portrait"], "sampled soundtrack displaced the full-size portrait"
        assert probe.v("tf_sceneparked"), "lesson retained the menu artwork"
        probe.tickwait(40)
        app.key('z',False)
        probe.wait(lambda: probe.v('btp_fxleft')>0, 'mistake sample', 8)
        probe.tickwait(45)
        app.key('f',True)
        probe.wait(lambda: probe.v('btp_fxleft')>0, 'praise sample', 8)
        probe.tickwait(38)
        for key, active in [('F4',0),('F4',1),('F3',0),('F3',1)]:
            app.press(key)
            probe.wait(lambda: probe.v('bt_pcm_live',1)==active, 'PCM transport after '+key)
        app.press('F2').wait_screen('LESSONS')
        scene_bytes = (build / "BTAN2.BIN").stat().st_size
        scene_claim = ((scene_bytes + 511) // 512) * 512

        def menu_restored():
            if probe.v("tf_sceneparked"):
                return False
            return any(c["bytes"] == scene_claim for c in app.resource_snapshot()["claims"])

        probe.wait(menu_restored, "large frames released and menu art reloaded")
        audio.close_probe(probe)
        assert probe.driver.word('sbl_poolseg') == 0 and not probe.driver.byte('sbl_str_act')
    wav = cap.with_suffix('.sound_blaster.wav')
    audio.finish_wav(wav)
    raw = subprocess.check_output(['ffmpeg','-v','error','-i',str(wav),'-ac','1','-ar','8000','-f','s16le','-'])
    capture = np.frombuffer(raw,dtype='<i2').astype(float)
    assert len(capture) > codec.RATE*5, 'no Sound Blaster audio captured'
    music, fx = (pack/'BTMUSIC.BTA').read_bytes(), (pack/'BTCLIPS.BTA').read_bytes()
    background = decoded(music,0,True)
    assert max(abs(background)) <= 32, 'quarter gain exceeded'
    for name,track,start in [('music',background,16500),('welcome',decoded(fx,0,False),1600),
                             ('mistake',decoded(fx,1,False),1500),('praise',decoded(fx,4,False),500)]:
        peaks = matches(capture,track[start:start+2000],len(track) if name=="music" else None)
        evidence[name] = peaks
        assert peaks[0][1] > .8,(name,peaks)
        if name=='music':
            assert peaks[1][1] > .8, peaks
    (out/'report.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('PCM: captured backing loop, greeting, mistake and praise; pause, mute and cleanup passed')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack',type=Path,help='existing local pack directory; default uses synthetic audio')
    parser.add_argument('--evidence',type=Path,default=ROOT/'build/boratype-pcm-test')
    args=parser.parse_args()
    out=args.evidence.resolve()
    out.mkdir(exist_ok=True,parents=True)
    with tempfile.TemporaryDirectory() as work:
        pack=args.pack or Path(work)
        if args.pack is None:
            synthetic_pack(pack)
        exercise(Path(os88sym.kernel_image()).parent,pack,out)
