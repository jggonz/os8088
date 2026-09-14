#!/usr/bin/env python3
"""Run the unmodified vendored BASIC demos in the native app under QEMU.

Loads source through the debugger into the editor's existing far buffer, then
presses F5 through the normal UI. Reads runtime state and saves actual QEMU
screenshots. The debugger never executes BASIC or supplies graphics results.
All disks use QEMU snapshots; the runner owns and terminates its own process.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import os88sym


class QMP:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(str(path))
        self.f = self.s.makefile('rwb', buffering=0)
        self.f.readline()
        self.call('qmp_capabilities')

    def call(self, name, **args):
        self.f.write((json.dumps({'execute': name, 'arguments': args}) + '\n').encode())
        while True:
            reply = json.loads(self.f.readline())
            if 'error' in reply:
                raise RuntimeError(reply['error'])
            if 'return' in reply:
                return reply['return']

    def hmp(self, command):
        return self.call('human-monitor-command', **{'command-line': command})


class Debugger:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(str(path))
        self.s.settimeout(10)

    def cmd(self, command):
        data = command.encode()
        self.s.sendall(b'$' + data + b'#' + ('%02x' % (sum(data) & 255)).encode())
        while self.s.recv(1) != b'$':
            pass
        data = bytearray()
        while True:
            b = self.s.recv(1)
            if b == b'#':
                break
            data += b
        self.s.recv(2)
        self.s.sendall(b'+')
        return data.decode()

    def read(self, addr, size):
        return bytes.fromhex(self.cmd('m%x,%x' % (addr, size)))

    def write(self, addr, data):
        for i in range(0, len(data), 1024):
            b = data[i:i+1024]
            assert self.cmd('M%x,%x:%s' % (addr+i, len(b), b.hex())) == 'OK'


def package_symbols(work):
    prefix = work / 'map.inc'
    mapping = work / 'speedy.map'
    binary = work / 'speedy.bin'
    prefix.write_text('[map all %s]\n' % mapping)
    subprocess.run(['nasm', '-f', 'bin', '-I', 'apps/', '-I', 'build/',
                    '-p', str(prefix), '-o', str(binary),
                    'apps/speedybasic/speedybasic.asm'], check=True, cwd=ROOT)
    assert binary.read_bytes() == (ROOT / 'build/speedybasic.bin').read_bytes()
    result = {}
    for line in mapping.read_text().splitlines():
        fields = line.split()
        if len(fields) == 3:
            try:
                result[fields[2]] = int(fields[1], 16)
            except ValueError:
                pass
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('demos', nargs='*')
    ap.add_argument('--seconds', type=float, default=60, help='Maximum initialization time per demo')
    ap.add_argument('--exercise', action='store_true', help='Send controls and verify clean termination')
    ap.add_argument('--software-fp', action='store_true', help='Disable the x87 fast path in the native runtime')
    ap.add_argument('--step', action='store_true')
    ap.add_argument('--trace', action='store_true')
    ap.add_argument('--output', default='build/speedybasic-qemu')
    args = ap.parse_args()
    os.chdir(ROOT)
    out = Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    names = args.demos or [s for s in (ROOT / 'apps/speedybasic/demos/MANIFEST.TXT').read_text().splitlines() if s and not s.startswith('#')]
    table = os88sym.linear('inst_tab')
    report = []
    with tempfile.TemporaryDirectory(prefix='sbq-') as temp:
        work = Path(temp)
        symbols = package_symbols(work)
        shutil.copyfile(ROOT/'build/os8088-360.img', work/'boot.img')
        shutil.copyfile(ROOT/'build/speedybasic360.img', work/'apps.img')
        process = subprocess.Popen([
            'qemu-system-i386', '-machine', 'pc,vmport=off',
            '-drive', 'file=%s,format=raw,if=floppy,snapshot=on' % (work/'boot.img'),
            '-drive', 'file=%s,format=raw,if=floppy,index=1,snapshot=on' % (work/'apps.img'),
            '-boot', 'a', '-chardev', 'msmouse,id=m0', '-serial', 'chardev:m0',
            '-display', 'none', '-qmp', 'unix:%s,server=on,wait=off' % (work/'qmp'),
            '-chardev', 'socket,path=%s,server=on,wait=off,id=dbg' % (work/'gdb'),
            '-gdb', 'chardev:dbg'])
        try:
            deadline = time.monotonic() + 10
            while not (work/'qmp').exists():
                if time.monotonic() > deadline:
                    raise RuntimeError('QEMU did not start')
                time.sleep(.05)
            q = QMP(work/'qmp')
            d = Debugger(work/'gdb')
            q.call('cont')
            time.sleep(3)
            def click(x, y):
                for _ in range(11):
                    q.hmp('mouse_move 60 60'); time.sleep(.06)
                dx, dy = x-639, y-479
                while dx or dy:
                    sx, sy = max(-60, min(60, dx)), max(-60, min(60, dy))
                    q.hmp('mouse_move %d %d' % (sx, sy)); time.sleep(.06)
                    dx -= sx; dy -= sy
                time.sleep(.15)
                for _ in range(2):
                    q.hmp('mouse_button 1'); time.sleep(.08)
                    q.hmp('mouse_button 0'); time.sleep(.10)
                time.sleep(.6)
            click(600, 109)
            click(159, 128)
            click(194, 175)
            q.call('stop')
            instances = d.read(table, 32*12)
            base = None
            for i in range(12):
                r = instances[i*32:(i+1)*32]
                if r[0] == 1 and r[12:28].split(b'\0')[0] == b'SPEEDYBA':
                    base = struct.unpack_from('<H', r, 6)[0] * 16
            if base is None:
                q.hmp('screendump '+str(out/'launch-failure.ppm'))
                raise RuntimeError('SpeedyBASIC did not launch')
            def word(name):
                return struct.unpack('<H', d.read(base+symbols['_'+name], 2))[0]
            def put(name, value):
                d.write(base+symbols['_'+name], struct.pack('<H', value & 65535))
            expected = (ROOT/'build/speedybasic.o88').read_bytes()
            assert d.read(base, 32) == expected[:32], 'Loaded package differs from symbol map build'
            source = word('sb_source_seg') * 16
            if args.software_fp:
                d.write(base+symbols['fp_hw'], b'\0')
            for name in names:
                if '.' not in name:
                    name += '.BAS'
                path = Path(name)
                if not path.is_file():
                    path = ROOT / 'apps/speedybasic/demos' / name.upper()
                data = path.read_bytes()
                d.write(source, data+b'\0')
                put('sb_source_len', len(data))
                put('sbc_status', 0)
                put('sbu_view', 0)
                q.call('cont')
                if args.trace:
                    for step in range(20):
                        q.hmp('sendkey f8'); time.sleep(.15); q.call('stop')
                        print('step', step, 'pc', word('sbc_pc'), 'count', word('sbc_code_count'), 'len', word('sbc_source_len'), 'state', word('sbc_status'), flush=True)
                        q.call('cont')
                q.hmp('sendkey '+('f8' if args.step else 'f5'))
                deadline = time.monotonic() + args.seconds
                ready = False
                while True:
                    time.sleep(.25)
                    q.call('stop')
                    state = word('sbc_status')
                    if '_sbc_keypolls' in symbols:
                        ready = state == 4 or word('sbc_keypolls') >= 2 or word('sbc_frames') >= 2
                        if name.upper() == 'MANDEL.BAS':
                            ready = state == 4 or word('sbc_frames') >= 2
                        if state == 3 and '_sbv_input_sym' in symbols:
                            ready |= word('sbv_input_sym') != 65535
                    if ready or state == 5 or args.step or time.monotonic() >= deadline:
                        break
                    q.call('cont')
                instances = d.read(table, 32*12)
                for i in range(12):
                    r = instances[i*32:(i+1)*32]
                    if r[0] == 1 and r[12:28].split(b'\0')[0] == b'SPEEDYBA':
                        base = struct.unpack_from('<H', r, 6)[0] * 16
                state = word('sbc_status')
                mode = word('sbs_mode')
                error = d.read(base+symbols['_sbc_err'], 80).split(b'\0')[0].decode('ascii', 'replace')
                row = dict(demo=name, state=state, line=word('sbc_lineno'), mode=mode, error=error, ready=ready)
                if mode == 0:
                    row['text'] = d.read(base+symbols['_sbs_ch'], 2000).decode('cp437').rstrip()
                row['base'] = base
                row['fpu'] = bool(d.read(base+symbols['fp_hw'],1)[0])
                if name.upper() == 'BOING.BAS' and ready:
                    entries = d.read(base+symbols['_sbc_symbols'],word('sbc_nsymbols')*28)
                    for offset in range(0,len(entries),28):
                        entry = entries[offset:offset+28]
                        label = entry[:16].split(b'\0')[0]
                        if label == b'BALL%':
                            seg, off = struct.unpack_from('<HH',entry,20)
                            data = b''.join(d.read(seg*16+off+i,min(1024,8002-i)) for i in range(0,8002,1024))
                            (out/'BOING-image.bin').write_bytes(data)
                            def image_color(x, y):
                                pixel = y*81+x
                                return (data[4+pixel//2] >> (0 if pixel&1 else 4))&15
                            row['ball_pattern'] = [''.join({15:'W',4:'R'}.get(image_color(x,y),'.') for x in range(10,75,5)) for y in range(10,75,5)]
                            row['boing_checker'] = image_color(30,40)==15 and image_color(40,40)==4
                for diagnostic in ('sbc_pc','sbc_code_count','sbc_nclaims','sbc_nsymbols','sbc_code_seg','sbc_source_len','sb_source_len','sbc_frames','sbc_keypolls'):
                    if '_'+diagnostic in symbols:
                        row[diagnostic] = word(diagnostic)
                if state == 5 and '_sbc_code_seg' in symbols:
                    row['code_head'] = d.read(word('sbc_code_seg')*16, 128).hex()
                seg = word('sbs_gseg')
                if seg:
                    pixels = b''.join(d.read(seg*16+i, min(1024,32000-i)) for i in range(0,32000,1024))
                    row['nonzero_bytes'] = sum(b != 0 for b in pixels)
                shot = out / (Path(name).stem+'.ppm')
                q.hmp('screendump '+str(shot))
                if args.exercise and ready and state != 4:
                    q.call('cont')
                    def key(k, delay=.18):
                        q.hmp('sendkey '+k); time.sleep(delay)
                    def type_text(s):
                        keys = {'=':'equal', '+':'shift-equal', ':':'shift-semicolon', ' ':'spc'}
                        for ch in s:
                            key(keys.get(ch, ch.lower()))
                        key('ret', 1)
                    if name.upper() == 'MINICALC.BAS':
                        for command in ('a1=10','a2=20','a3=a1+a2'):
                            type_text(command)
                        q.call('stop')
                        rows = d.read(base+symbols['_sbc_symbols'], word('sbc_nsymbols')*28)
                        for i in range(0,len(rows),28):
                            r = rows[i:i+28]
                            if r[:16].split(b'\0')[0] == b'CV' and struct.unpack_from('<H',r,16)[0] == 0:
                                seg, offset = struct.unpack_from('<HH',r,20)
                                row['minicalc_a3'] = struct.unpack('<d',d.read(seg*16+offset+96,8))[0]
                        q.hmp('screendump '+str(out/(Path(name).stem+'-input.ppm')))
                        q.call('cont'); type_text('quit')
                    elif name.upper() == 'TANKS.BAS':
                        key('1', 3); key('spc', 5); key('spc', 3)
                    elif name.upper() in ('WORD.BAS','WORDXT.BAS'):
                        key('z', 1); key('left'); key('right'); key('backspace', 1)
                    elif name.upper() == 'PIANO.BAS':
                        key('a', 1); key('1', 2)
                    elif name.upper() in ('LANDER.BAS','LANDERHD.BAS'):
                        key('spc'); key('left', 1)
                    elif name.upper() == 'SNAKE.BAS':
                        key('down', .4); key('right', .4)
                    q.hmp('screendump '+str(out/(Path(name).stem+'-controls.ppm')))
                    key('esc', 1)
                    if name.upper() == 'MANDEL.BAS':
                        key('esc', 1)
                    deadline = time.monotonic()+20
                    while True:
                        q.call('stop'); after=word('sbc_status')
                        if after in (4,5) or time.monotonic()>=deadline:
                            break
                        q.call('cont'); time.sleep(.25)
                    row['exit_state'] = after
                    row['exit_error'] = d.read(base+symbols['_sbc_err'],80).split(b'\0')[0].decode('ascii','replace')
                    q.hmp('screendump '+str(out/(Path(name).stem+'-exit.ppm')))
                report.append(row)
                (out/'results.json').write_text(json.dumps(report, indent=2)+'\n')
                print(json.dumps(row), flush=True)
                put('sbc_status', 6)
            (out/'results.json').write_text(json.dumps(report, indent=2)+'\n')
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
    return int(any(r['state'] == 5 or not r['ready'] or r.get('exit_state',4)!=4 or r.get('minicalc_a3',30)!=30 or not r.get('boing_checker',True) for r in report))


if __name__ == '__main__':
    sys.exit(main())
