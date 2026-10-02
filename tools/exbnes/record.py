#!/usr/bin/env python3
"""Offline NES oracle: record.py --refresh regenerates the committed fixture.

Requires EXCITEBIKE_REF and host cc only when explicitly invoked. All data
processing and dependency fetching use Python's standard library.
"""
import argparse
import gzip
import hashlib
import json
import io
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from exbref import Reader
from getagnes import acquire, COMMIT

FIXTURES = ROOT / 'tests' / 'fixtures' / 'excitebike'
DEFAULT_SCRIPT = FIXTURES / 'attract-race.pad'
PIXELS = '0,559,900,1199,1200,1261,1812,2211'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def build(source=None):
    cache = ROOT / 'build' / 'exbnes'
    upstream = acquire(source=source)
    original = (upstream / 'agnes.c').read_text()
    needle = 'void cpu_write8(cpu_t *cpu, uint16_t addr, uint8_t val) {\n'
    if original.count(needle) != 1:
        raise ValueError('agnes CPU write instrumentation no longer matches the pin')
    instrumented = original.replace(needle, needle + '    oracle_write(addr, val, cpu->cycles, cpu->agnes->ppu.regs.w);\n')
    (cache / 'agnes-instrumented.c').write_text(instrumented)
    exe = cache / 'oracle'
    compiler = shlex.split(os.environ.get('CC', 'cc'))
    subprocess.run(compiler + ['-std=c11', '-O2', '-Wall', '-I', str(cache),
                    '-I', str(upstream), str(HERE / 'oracle.c'), '-o', str(exe)], check=True)
    return exe


def run(exe, rom, script, output, pixels='-', apu=False):
    args = [str(exe), str(rom), str(script), str(output), pixels or '-']
    if apu:
        args.append(str(output.with_suffix('.apu.jsonl')))
    subprocess.run(args, check=True)
    return output.read_bytes()


def compress(data):
    # Empty original filename and zero timestamp make refresh byte-identical.
    output = io.BytesIO()
    with gzip.GzipFile(filename='', mode='wb', fileobj=output, compresslevel=9, mtime=0) as stream:
        stream.write(data)
    return output.getvalue()


def inspect(data):
    frames = [json.loads(line) for line in data.splitlines()]
    if not frames or any(row['frame'] != i for i, row in enumerate(frames)):
        raise ValueError('recorder emitted incomplete frames')
    return frames


def selfcheck(exe, rom, script, work, pixels, apu):
    first = run(exe, rom, script, work / 'first.jsonl', pixels, apu)
    second = run(exe, rom, script, work / 'second.jsonl', pixels, apu)
    if first != second:
        raise ValueError('two NES recordings differ')
    if apu and (work / 'first.apu.jsonl').read_bytes() != (work / 'second.apu.jsonl').read_bytes():
        raise ValueError('two NES APU logs differ')
    frames = inspect(first)
    # The fixed refresh script must exercise demo and a human-controlled race.
    if script.resolve() == DEFAULT_SCRIPT.resolve():
        demo = [f for f in frames[560:1200] if f['ram']['race_started_004f'] == '01']
        race = [f for f in frames[1812:] if f['ram']['race_started_004f'] == '01']
        if not demo or not race or not any(int(f['ram']['speed_integer_0094'][:2],16) for f in race):
            raise ValueError('synthetic script did not reach both attract demo and throttled race')
    return first, frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--refresh', action='store_true', help='selfcheck and write committed fixture + manifest')
    parser.add_argument('--selfcheck', action='store_true', help='compare two runs, without updating fixtures')
    parser.add_argument('--script', type=Path, default=DEFAULT_SCRIPT)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--pixels', help='comma-separated zero-based frames; - omits pixels')
    parser.add_argument('--apu', action='store_true', help='write raw APU register writes, no audio emulation')
    parser.add_argument('--agnes-from', type=Path, help='fetch pinned agnes from verified local checkout')
    args = parser.parse_args()
    if not (args.refresh or args.selfcheck or args.out):
        parser.error('choose --refresh, --selfcheck or --out')
    if args.refresh and (args.script.resolve() != DEFAULT_SCRIPT.resolve() or args.pixels not in (None, PIXELS)):
        parser.error('--refresh uses the committed script and default pixel samples')
    pixels = args.pixels if args.pixels is not None else (PIXELS if args.script.resolve() == DEFAULT_SCRIPT.resolve() else '-')
    try:
        cartridge = Reader().cartridge()
        exe = build(args.agnes_from)
        with tempfile.TemporaryDirectory(prefix='exbnes-') as tmp:
            work = Path(tmp)
            rom = work / 'excitebike.nes'
            rom.write_bytes(cartridge)
            apu = args.apu or args.refresh or args.selfcheck
            if args.refresh or args.selfcheck:
                data, frames = selfcheck(exe, rom, args.script, work, pixels, apu)
            else:
                data = run(exe, rom, args.script, work / 'first.jsonl', pixels, apu)
                frames = inspect(data)
            if args.refresh:
                outputs = {
                    'attract-race.jsonl.gz': compress(data),
                    'attract-race.apu.jsonl.gz': compress((work / 'first.apu.jsonl').read_bytes()),
                }
                manifest = {
                    'format': 'EXB-NES-1', 'agnes_commit': COMMIT,
                    'cartridge_sha1': hashlib.sha1(cartridge).hexdigest(),
                    'script': args.script.name, 'script_sha256': digest(args.script.read_bytes()),
                    'frames': len(frames), 'pixel_frames': [f['frame'] for f in frames if 'pixels' in f],
                    'cpu_timing': 'per write: CPU cycles since agnes frame boundary; instruction granularity',
                    'frame_boundary': 'agnes_next_frame return, before the next frame controller input',
                    'pixels': '256x240, row-major NES palette indices, hex bytes; no CHR or ROM data',
                    'ram': {name: {'address': int(name.rsplit('_',1)[1],16), 'size': len(value)//2}
                            for name, value in frames[0]['ram'].items()},
                    'files': {name: {'bytes': len(payload), 'sha256': digest(payload)} for name, payload in outputs.items()},
                    'selfcheck': 'two complete frame and APU recordings byte-identical',
                }
                for name, payload in outputs.items():
                    (FIXTURES / name).write_bytes(payload)
                (FIXTURES / 'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
            if args.out:
                args.out.parent.mkdir(parents=True, exist_ok=True)
                args.out.write_bytes(compress(data) if args.out.suffix == '.gz' else data)
                if apu:
                    args.out.with_suffix('.apu.jsonl').write_bytes((work / 'first.apu.jsonl').read_bytes())
            print('exbnes: %d frames%s' % (len(frames), '; two runs identical' if args.refresh or args.selfcheck else ''))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'exbnes: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
