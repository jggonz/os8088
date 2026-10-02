#!/usr/bin/env python3
"""Independent committed NES recording reader; needs neither cc nor reference.

Registered full-tier row. Checks fixture hashes, controller timing, named RAM,
PPU/OAM/pixel fields, the attract-to-race sequence and APU ordering. Negative
controls flip one recorded bit and shift controller input by one frame.
"""
import gzip
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'tests' / 'fixtures' / 'excitebike'
# This second reader pins RAM fields independently of the recording code.
RAM = {
    'rng_0018': 8, 'start_flags_0024': 1, 'flow_0041': 1, 'track_0043': 1,
    'frame_phase_004c': 1, 'race_started_004f': 1, 'element_0058': 4,
    'clock_0068': 4, 'screen_x_0080': 4, 'screen_y_008c': 4,
    'drawing_order_0088': 4, 'speed_fraction_0090': 4, 'speed_integer_0094': 4,
    'crash_phase_009c': 4, 'object_active_00a8': 4, 'animation_timer_0036': 4,
    'wheelie_crash_0098': 4, 'pitch_00ac': 4, 'air_00b0': 4,
    'lane_y_00b8': 4, 'height_00bc': 4, 'script_cursor_00c4': 4, 'heat_03b5': 1,
}


def pin(payload, entry):
    assert len(payload) == entry['bytes'], 'fixture byte length'
    assert hashlib.sha256(payload).hexdigest() == entry['sha256'], 'fixture digest; make excitebike-fixtures'


def controllers(script):
    frames = []
    for line in script.splitlines():
        fields = line.split('#',1)[0].split()
        if fields:
            assert len(fields) in (2,3), 'script fields'
            repeat = int(fields[0],10)
            pads = [int(x,16) for x in fields[1:]]
            if len(pads) == 1:
                pads.append(0)
            assert 0 < repeat <= 10000000 and all(0 <= x <= 255 for x in pads)
            frames.extend([pads] * repeat)
    return frames


def input_timing(frames, expected):
    assert len(frames) == len(expected), 'script frame count'
    for i, (row, pads) in enumerate(zip(frames,expected)):
        assert row['frame'] == i and row['pad'] == pads, 'controller offset at frame %d' % i


def hexbytes(value, size):
    assert isinstance(value,str) and re.fullmatch('[0-9a-f]{%d}' % (2*size),value), 'hex byte field'
    return bytes.fromhex(value)


def rejects(callback):
    try:
        callback()
    except AssertionError:
        return
    raise AssertionError('negative control was accepted')


def main():
    manifest = json.loads((FIXTURES / 'manifest.json').read_text())
    assert manifest['format'] == 'EXB-NES-1'
    assert manifest['agnes_commit'] == '0e4220b084c467e39c04805d955e78c463feadd0'
    assert manifest['cartridge_sha1'] == '2e9897846e54a4a9865e87de7517c6710bdec255'
    script = (FIXTURES / manifest['script']).read_bytes()
    assert hashlib.sha256(script).hexdigest() == manifest['script_sha256']
    payloads = {}
    for name, entry in manifest['files'].items():
        assert Path(name).name == name and name.endswith('.jsonl.gz')
        payload = (FIXTURES / name).read_bytes()
        pin(payload,entry)
        payloads[name] = payload
    trace = payloads['attract-race.jsonl.gz']
    frames = [json.loads(line) for line in gzip.decompress(trace).splitlines()]
    expected = controllers(script.decode('ascii'))
    input_timing(frames,expected)
    assert len(frames) == manifest['frames'] == 2212
    assert {name: entry['size'] for name,entry in manifest['ram'].items()} == RAM
    for name, entry in manifest['ram'].items():
        assert entry['address'] == int(name.rsplit('_',1)[1],16)
    shots = []
    for row in frames:
        assert set(row['ram']) == set(RAM)
        for name,size in RAM.items():
            hexbytes(row['ram'][name],size)
        hexbytes(row['oam'],256)
        palette = hexbytes(row['palette'],32)
        assert all(value <= 63 for value in palette)
        scroll = row['scroll']
        assert set(scroll) == {'x','y','v','t','fine_x','latch'}
        assert all(0 <= scroll[key] <= 255 for key in ('x','y'))
        assert 0 <= scroll['fine_x'] < 8 and scroll['latch'] in (0,1)
        assert all(0 <= scroll[key] <= 0x7fff for key in ('v','t'))
        previous = -1
        for cycle,address,value in row['scroll_writes']:
            assert previous <= cycle <= 40000 and address == 0x2005 and 0 <= value <= 255
            previous = cycle
        if 'pixels' in row:
            assert all(value < 64 for value in hexbytes(row['pixels'],256*240))
            shots.append(row['frame'])
    assert shots == manifest['pixel_frames']
    assert any(f['ram']['race_started_004f'] == '01' for f in frames[560:1200]), 'attract demo'
    assert frames[1200]['ram']['flow_0041'] == '01', 'track selection after attract'
    assert frames[1261]['ram']['flow_0041'] == '02', 'race selected'
    assert frames[-1]['ram']['race_started_004f'] == '01', 'playable race'
    assert int(frames[-1]['ram']['speed_integer_0094'][:2],16) > 0, 'human throttle'
    assert frames[-1]['ram']['clock_0068'] != frames[1812]['ram']['clock_0068'], 'race clock advances'
    apu = [json.loads(line) for line in gzip.decompress(payloads['attract-race.apu.jsonl.gz']).splitlines()]
    assert apu, 'APU writes recorded'
    previous = (-1,-1)
    for row in apu:
        position = row['frame'],row['cycle']
        assert previous <= position and 0 <= row['frame'] < len(frames) and 0 <= row['cycle'] <= 40000
        assert 0x4000 <= row['address'] <= 0x4017 and row['address'] not in (0x4014,0x4016)
        assert 0 <= row['value'] <= 255
        previous = position
    flipped = bytearray(trace)
    flipped[len(flipped)//2] ^= 1
    rejects(lambda: pin(bytes(flipped),manifest['files']['attract-race.jsonl.gz']))
    rejects(lambda: input_timing(frames,[expected[0]] + expected[:-1]))
    print('excitebikeoracle: %d frames, %d pixel samples, %d APU writes; negative controls rejected' %
          (len(frames),len(shots),len(apu)))


if __name__ == '__main__':
    main()
