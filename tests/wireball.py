#!/usr/bin/env python3
"""WireBall's real assembly on TWO OS instances over a serial null modem.

The bridge forwards UART bytes, not game state. Assertions read the package
and video RAM. Noise and silent cable removal are controlled at the wire;
short physics fixtures are installed through QEMU's debugger to test both
paddle extremes and the last point without waiting eleven whole matches.
Every emulator is owned and killed. QEMU waits use its tick clock; the
MartyPC splash/mono passes use guest cycles. Speaker captures verify pitches.
Registered in the soak tier; make wireballtest is the standalone spelling.
"""
import atexit
import argparse
from pathlib import Path
import selectors
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
import sndcheck
import dispcp
import dispapps
import os88build
import os88qemu
import os88sym
from telansi import Qemu

S = os88sym.linear
DISK_OVERRIDE = None


def artifact(name):
    return str(ROOT / os88build.at('build/' + name))


class Mouse:
    def __init__(self, machine):
        self.m = machine

    def run(self, *args):
        subprocess.run([sys.executable, str(ROOT / 'tools/mouse.py'), self.m.path,
                        *map(str, args)], check=True, capture_output=True, cwd=ROOT)

    def to(self, x, y):
        self.run('to', x, y)

    def dblclick(self, x, y):
        self.to(x, y)
        subprocess.run([sys.executable, str(ROOT / 'tools/qmp.py'), self.m.path,
                        'mouse_button 1', 'sleep 0.08', 'mouse_button 0', 'sleep 0.12',
                        'mouse_button 1', 'sleep 0.08', 'mouse_button 0', 'gsleep 0.4'],
                       check=True, capture_output=True, cwd=ROOT)

    def click(self, x, y):
        self.run('click', x, y)


def settle(m, **kw):
    if not os88qemu.acted(m, lambda: os88qemu.ui_idle(m, S), secs=12, what='UI completion'):
        m.hmp(f'screendump "{artifact("wireball-front-failure.ppm")}"')
        print(m.hmp('info registers'), flush=True)
        print('UI lock', m.read(S('gfx_lock_flag'), 1).hex(), 'windows', dispcp.win_list(m, S), flush=True)
        raise AssertionError('desktop did not finish the gesture')


class Machine(Qemu):
    def __init__(self, directory, index):
        self.directory = directory
        self.linkport = 1-index
        self.linkpath = str(directory / f'link{index}.sock')
        self.gdbpath = str(directory / f'gdb{index}.sock')
        self.pidfile = str(directory / f'qemu{index}.pid')
        path = str(directory / f'qmp{index}.sock')
        args = ['qemu-system-i386', '-machine', 'pc,vmport=off,pcspk-audiodev=speaker', '-audiodev', f'wav,id=speaker,path={directory / ("speaker" + str(index) + ".wav")}', '-m', '16M', '-serial', 'none', '-parallel', 'none',
                '-drive', f'file={artifact("os8088.img")},if=floppy,index=0,format=raw,snapshot=on',
                '-drive', f'file={(DISK_OVERRIDE or artifact("wireball.img"))},if=floppy,index=1,format=raw,snapshot=on',
                '-boot', 'a', '-display', 'none', '-nic', 'none',
                '-chardev', 'msmouse,id=mouse', '-device', f'isa-serial,chardev=mouse,index={1-self.linkport}',
                '-chardev', f'socket,id=link,path={self.linkpath},server=on,wait=off',
                '-device', f'isa-serial,chardev=link,index={self.linkport}',
                '-qmp', f'unix:{path},server=on,wait=off',
                '-gdb', f'unix:{self.gdbpath},server=on,wait=off']
        self.log = open(directory / f'qemu{index}.log', 'w')
        self.proc = subprocess.Popen(args, cwd=ROOT, stdout=self.log, stderr=self.log)
        Path(self.pidfile).write_text(str(self.proc.pid))
        os88qemu.own(self.pidfile, path)
        atexit.register(self.cleanup)
        super().__init__(path)
        assert os88qemu.acted(self, lambda: (os88qemu.ticks(self) > 3 and self.read(S('spl_live'), 1) == b'\0' and dispcp.drive_ordinal(self, S, 'B') is not None),
                             secs=45, what='desktop boot'), 'boot never reached the desktop'
        settle(self)
        self.mo = Mouse(self)
        # The serial-mouse contest consumes its first packets before lock-on.
        self.mo.to(2, 2)
        self.mo.to(2, 2)
        assert struct.unpack('<HH', self.read(S('mouse_x'), 4)) == (2, 2)
        dispcp.open_drive(self, self.mo, S, settle, 'B')
        assert os88qemu.acted(self, lambda: bool(dispcp.win_list(self, S)),
                             secs=8, what='Disk B window'), 'double-click did not open Disk B'
        wx, wy = dispcp.win_rect(self, S, dispcp.win_list(self, S)[-1])[:2]
        dispcp.open_named(self, self.mo, S, settle, wx, wy, 'WIREBALL.O88')
        assert os88qemu.acted(self, lambda: dispapps.pkg_seg(self, 0) is not None,
                             secs=8, what='WireBall loaded'), 'package did not load'
        self.seg = dispapps.pkg_seg(self, 0)[1]
        self.sy = dispapps._map('wireball')
        self.base = self.sy['os88_image_end']
        self.bss_size = int.from_bytes(self.readseg(self.seg, 10, 2), 'little')

    def cleanup(self):
        if getattr(self, 'proc', None) is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        # The filename uses launch index, which is the complementary link-port index.
        capture = self.directory / ('speaker' + str(1-self.linkport) + '.wav')
        if capture.exists() and capture.stat().st_size > 44:
            shutil.copyfile(capture, artifact('wireball-speaker-' + str(1-self.linkport) + '.wav'))
        if hasattr(self, 'tmp'):
            shutil.rmtree(self.tmp, ignore_errors=True)
        if hasattr(self, 'log') and not self.log.closed:
            self.log.close()

    def value(self, name, n=1):
        return int.from_bytes(self.readseg(self.seg, self.sy[name], n), 'little')

    def game(self):
        b = self.readseg(self.seg, self.base, self.bss_size)
        def val(name, n=1):
            i = self.sy['wb_' + name] - self.base
            return int.from_bytes(b[i:i+n], 'little')
        return {name: val(name, n) for name, n in
                [('phase', 1), ('side', 1), ('left', 1), ('right', 1),
                 ('ballx', 2), ('bally', 2), ('flags', 1), ('count', 1),
                 ('scorel', 1), ('scorer', 1), ('command', 1), ('ack', 1),
                 ('frames', 2), ('crossings', 2)]}

    def put(self, values):
        """Install a fixture while stopped; use the shipped binary's own map."""
        self.hmp('stop')
        try:
            with socket.socket(socket.AF_UNIX) as s:
                s.settimeout(10)
                s.connect(self.gdbpath)
                for name, value, n in values:
                    address = (self.seg << 4) + self.sy['wb_' + name]
                    data = value.to_bytes(n, 'little')
                    payload = f'M{address:x},{n:x}:{data.hex()}'.encode()
                    s.sendall(b'$' + payload + b'#' + f'{sum(payload) & 255:02x}'.encode())
                    response = b''
                    while b'#' not in response:
                        response += s.recv(1024)
                    assert b'$OK#' in response, response
                    s.sendall(b'+')
        finally:
            self.hmp('cont')

    def wait(self, condition, what, secs=8):
        assert os88qemu.acted(self, lambda: condition(self.game()), secs=secs,
                             what=what), f'{what}: {self.game()}, latch={self.value("wb_latch")}, keyboard={self.read(S("kbd_dnmap"), 16).hex()}'


class Cable:
    def __init__(self, machines):
        self.sockets = []
        for m in machines:
            s = socket.socket(socket.AF_UNIX)
            s.connect(m.linkpath)
            self.sockets.append(s)
        self.enabled = True
        self.stop = threading.Event()
        self.counts = [0, 0]
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        with selectors.DefaultSelector() as sel:
            for i, s in enumerate(self.sockets):
                sel.register(s, selectors.EVENT_READ, i)
            while not self.stop.is_set():
                for event, _ in sel.select(0.05):
                    data = event.fileobj.recv(1024)
                    if not data:
                        return
                    self.counts[event.data] += len(data)
                    if self.enabled:
                        self.sockets[1-event.data].sendall(data)

    def inject(self, destination, data):
        self.sockets[destination].sendall(data)

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)
        for s in self.sockets:
            s.close()


def frame(sender, recipient, x=310, checksum=True, version=2, vx=8, vy=2):
    b = bytearray(b'WB' + bytes([version, 1]) + struct.pack('<II', sender, recipient))
    b += bytes([8, 8]) + struct.pack('<HH', x, 40)
    b += bytes([vx & 255, vy & 255, 11, 0, 2, 0, 0])
    crc = 0
    for v in b:
        crc ^= v
    b.append(crc if checksum else crc ^ 1)
    out = bytearray([0x7e])
    for v in b:
        out.extend([0x7d, v ^ 0x20] if v in (0x7d, 0x7e) else [v])
    out.append(0x7e)
    return bytes(out)


def check(condition, message):
    assert condition, message
    print('PASS  ' + message, flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='wireball-') as d:
        machines = []
        cable = None
        try:
            machines.append(Machine(Path(d), 0))
            machines.append(Machine(Path(d), 1))
            cable = Cable(machines)
            for m in machines:
                m.key('s')
            for m in machines:
                m.wait(lambda g: g['phase'] == 2, 'automatic discovery and handshake', secs=10)
            for m in machines:
                chosen = m.readseg(m.seg, m.value('wb_port', 2), 2)
                check(int.from_bytes(chosen, 'little') == (0x3f8 if m.linkport == 0 else 0x2f8),
                      'discovery selects the wired port (COM1 and COM2 on opposite computers)')
            games = [m.game() for m in machines]
            check(sorted(g['side'] for g in games) == [0, 1], 'automatic, complementary player sides')
            host = next(m for m in machines if m.game()['side'] == 0)
            client = next(m for m in machines if m is not host)
            # Make the crossing independent of randomized serves or an idle opponent.
            host.put([('ballx', 78*4-4, 2), ('bally', 40, 2), ('vx', 8, 1),
                      ('vy', 1, 1), ('count', 0, 1), ('flags', 0, 1)])
            host.wait(lambda g: g['crossings'] >= 1, 'ball traverses the screen seam', secs=12)
            # Hold an unambiguous position and compare state to the actual other screen's cell.
            host.key('p')
            host.wait(lambda g: g['flags'] & 1, 'host pause')
            client.wait(lambda g: g['flags'] & 1, 'pause reaches remote machine')
            host.put([('ballx', 100*4, 2), ('bally', 7*4, 2), ('vx', 12, 1), ('vy', 7, 1)])
            client.wait(lambda g: g['ballx'] == 400 and g['bally'] == 28, 'authoritative ball state')
            os88qemu.pace(client, 0.2)
            vrams = [m.read(m.value('wb_vseg', 2) << 4, 4000)[::2] for m in [host, client]]
            check(0xfe not in vrams[0][3*80:23*80] and vrams[1][10*80+23] == 0xfe,
                  'ball leaves the left monitor and appears on the right monitor')
            check(client.value('wb_vx') == 12 and client.value('wb_vy') == 7,
                  'variable-speed velocity crosses the serial link')
            client.hmp('sendkey down 500')
            client.wait(lambda g: g['right'] >= 14, 'held-key movement')
            host.wait(lambda g: g['right'] >= 14, 'paddle input crosses the cable')
            os88qemu.pace(client, 0.7)
            check(client.game()['right'] == host.game()['right'], 'remote echoes preserve local paddle input')
            client.key('p')
            host.wait(lambda g: not g['flags'] & 1, 'remote pause request resumes host')
            client.wait(lambda g: g['command'] == g['ack'], 'command acknowledged')
            check(True, 'either player can pause and commands are acknowledged')
            # Physics fixtures run the actual assembler's collisions and scoring.
            host.put([('left', 0, 1), ('ballx', 12, 2), ('bally', 2, 2),
                      ('vx', 248, 1), ('vy', 0, 1), ('count', 0, 1), ('flags', 0, 1),
                      ('scorel', 0, 1), ('scorer', 0, 1), ('returns', 0, 1)])
            host.wait(lambda g: g['ballx'] > 12 and g['scorel'] == 0 and g['scorer'] == 0,
                      'top paddle bounce', secs=1)
            host.put([('left', 16, 1), ('ballx', 12, 2), ('bally', 74, 2),
                      ('vx', 248, 1), ('vy', 0, 1), ('count', 0, 1)])
            host.wait(lambda g: g['ballx'] > 12 and g['scorer'] == 0, 'bottom paddle bounce', secs=1)
            host.put([('left', 0, 1), ('ballx', 12, 2), ('bally', 60, 2),
                      ('vx', 248, 1), ('vy', 0, 1), ('scorel', 0, 1), ('scorer', 10, 1),
                      ('count', 0, 1), ('flags', 0, 1)])
            host.wait(lambda g: g['scorer'] == 11 and g['flags'] & 2, 'first-to-11 match end', secs=1)
            client.wait(lambda g: g['scorer'] == 11 and g['flags'] & 2, 'remote final score')
            os88qemu.pace(host, 0.5)  # let all three victory notes play before restarting
            check(host.value('wb_soundnotes', 2) > 0 and client.value('wb_soundnotes', 2) > 0,
                  'both players receive nonblocking gameplay sounds')
            check(True, 'paddle tips bounce, misses score, and both computers see the winner')
            client.key('n')
            host.wait(lambda g: g['scorel'] == g['scorer'] == 0 and not g['flags'] & 2,
                      'remote new match')
            client.wait(lambda g: g['command'] == g['ack'], 'restart acknowledged')
            host.key('p')
            host.wait(lambda g: g['flags'] & 1, 'pause for malformed-frame checks')
            client.wait(lambda g: g['flags'] & 1, 'remote fixture pause')
            # Negative controls: plausible peer frames with malformed bounds, bad
            # checksum, wrong peer, wrong recipient, and overlong/partial payloads.
            cable.enabled = False
            hi = host.value('wb_identity_value', 4)
            ci = client.value('wb_identity_value', 4)
            bad = (frame(hi, ci, x=65000) + frame(hi, ci, checksum=False)
                   + frame(hi ^ 1, ci) + frame(hi, ci ^ 1)
                   + frame(hi, ci, version=1) + frame(hi, ci, vx=0)
                   + frame(hi, ci, vx=17) + frame(hi, ci, vy=9)
                   + b'\x7e' + b'x'*60 + b'\x7eWB\x7d\x7e')
            cable.inject(machines.index(client), bad)
            os88qemu.pace(client, 0.8)
            check(client.game()['scorer'] == 0 and client.game()['flags'] == 1,
                  'damaged, foreign and out-of-bounds frames cannot apply state')
            for m in machines:
                m.wait(lambda g: g['phase'] == 0, 'silent cable removal stops match', secs=5)
            cable.enabled = True
            for m in machines:
                m.wait(lambda g: g['phase'] == 2 and g['scorel'] == g['scorer'] == 0,
                       'automatic reconnection starts a fresh match', secs=10)
            check(True, 'silent unplug pauses play; plugging back in discovers a fresh match')
            for i, m in enumerate(machines):
                m.hmp(f'screendump "{artifact("wireball-" + str(i) + ".ppm")}"')
            # Leave one side: BYE stops the other; then both return to desktop.
            host.key('esc')
            assert os88qemu.acted(host, lambda: host.read(S('fsx_task'), 1) == b'\xff',
                                 secs=3, what='FSX restoration')
            client.wait(lambda g: g['phase'] == 0, 'peer farewell', secs=4)
            client.key('esc')
            settle(host)
            settle(client)
            # The mouse is the strongest end-to-end restore check: a real serial
            # packet must move the kernel pointer after the game borrowed its UART.
            for m in machines:
                m.mo.to(111, 99)
                xy = m.read(S('mouse_x'), 4)
                check(struct.unpack('<HH', xy) == (111, 99), 'desktop serial mouse works after exit')
            # Local practice never needs the peer or a serial cable.
            host.key('l')
            host.wait(lambda g: g['phase'] == 2, 'practice launch')
            host.wait(lambda g: g['ballx'] != 156, 'practice simulation advances', secs=4)
            host.key('m')
            assert os88qemu.acted(host, lambda: host.value('wb_muted') == 1, secs=2, what='mute toggles')
            notes = host.value('wb_soundnotes', 2)
            host.put([('count', 0, 1), ('flags', 0, 1), ('ballx', 12, 2),
                      ('bally', 2, 2), ('left', 0, 1), ('vx', 248, 1), ('vy', 0, 1)])
            os88qemu.pace(host, 0.5)
            check(host.value('wb_soundnotes', 2) == notes and host.value('wb_soundactive') == 0,
                  'mute suppresses collision tones immediately')
            host.key('esc')
            settle(host)
            check(host.read(S('snd_town_act'), 1) == b'\0', 'exit leaves the speaker silent')
            check(all(n > 200 for n in cable.counts), 'both emulated UARTs exchanged actual framed bytes')
        finally:
            if cable:
                cable.close()
            for m in machines:
                m.cleanup()
        for i in range(2):
            rate, samples, _ = sndcheck.load(artifact('wireball-speaker-' + str(i) + '.wav'))
            check(max(abs(v) for v in samples) > 0.05,
                  'player %d PC speaker capture contains audible tones' % (i+1))
            pitches = set()
            for start in range(0, len(samples)-2048, 1024):
                block = samples[start:start+2048]
                if sum(v*v for v in block)/len(block) < 0.002:
                    continue
                pitches.add(max((165,220,440,660,784,880,988,1175),
                                key=lambda hz: sndcheck.goertzel(block, rate, hz)))
            check(880 in pitches and 1175 in pitches,
                  'player %d capture contains paddle and victory pitches' % (i+1))


def mono():
    import os88marty as M
    import os88ui
    for adapter, machine, video in [('cga', 'os8088_5150_cga_gla', 0xb800),
                                    ('herc', 'os8088_5150_herc_gla', 0xb000)]:
        with os88ui.boot(artifact('os8088-360.img'), apps=DISK_OVERRIDE or artifact('wireball360.img'),
                        machine=machine) as ui:
            ui.path('B:/WIREBALL.O88')
            ui.settle()
            m = ui.m
            seg = dispapps.pkg_seg(m, 0)[1]
            sy = dispapps._map('wireball')
            def val(name, n=1):
                return int.from_bytes(m.readseg(seg, sy['wb_' + name], n), 'little')
            def key(name):
                m.key(name, up=False)
                M.pace(m, 0.15)
                m.key(name, down=False)
                M.pace(m, 0.15)
            def uart(base):
                # Stop the CPU around DLAB changes so the mouse ISR cannot race.
                lcr = m.inb(base + 3)
                try:
                    m.outb(base + 3, lcr & 0x7f)
                    ier, mcr = m.inb(base + 1), m.inb(base + 4)
                    m.outb(base + 3, lcr | 0x80)
                    dll, dlm = m.inb(base), m.inb(base + 1)
                    return lcr, ier, dll, dlm, mcr
                finally:
                    m.outb(base + 3, lcr)
            m.pause()
            saved = [uart(base) for base in (0x3f8, 0x2f8)]
            pic = m.inb(0x21)
            scratch = m.inb(0x2ff)
            m.outb(0x2ff, 0x5a)
            check(m.inb(0x2ff) != 0x5a, adapter + ' regression machine has an original 8250 without a scratch register')
            m.outb(0x2ff, scratch)
            m.run()
            key('KeyS')
            ports = sy['wb_ports']
            stride = (sy['wb_packet'] - ports) // 2
            M.until(m, lambda _: m.readseg(seg, ports, 2) == b'\xf8\x03'
                    and m.readseg(seg, ports + stride, 2) == b'\xf8\x02',
                    'original 8250 UART discovery on ' + adapter, guest=5, poll=0.03)
            check(True, adapter + ' serial match finds both original 8250 UARTs')
            m.pause()
            active = [uart(base) for base in (0x3f8, 0x2f8)]
            check(all(row[:4] == (3, 0, 48, 0) and row[4] & 0x18 == 0 for row in active),
                  adapter + ' serial match programs 2400 baud 8N1 with UART interrupts disabled')
            m.run()
            key('Escape')
            M.until(m, lambda _: m.read(m.sym('fsx_task'), 1) == b'\xff',
                    'serial match returns to desktop', guest=8, poll=0.03)
            ui.settle()
            m.pause()
            check([uart(base) for base in (0x3f8, 0x2f8)] == saved,
                  adapter + ' serial exit restores both UARTs including their original divisors')
            check(m.inb(0x21) & 0x18 == pic & 0x18,
                  adapter + ' serial exit restores the mouse IRQ masks')
            m.run()
            key('KeyL')
            M.until(m, lambda _: val('phase') == 2, 'practice opens on ' + adapter, guest=5, poll=0.03)
            check(val('vseg', 2) == video, adapter + ' uses its native text framebuffer')
            start = val('ballx', 2)
            M.until(m, lambda _: val('ballx', 2) != start, 'practice ball moves', guest=5, poll=0.03)
            key('KeyP')
            M.until(m, lambda _: val('flags') & 1, 'practice pause', guest=3, poll=0.03)
            cells = m.read(video << 4, 4000)[::2]
            check(0xfe in cells[3*80:23*80] and cells.count(0xdb) == 8,
                  adapter + ' shows the ball and both four-cell paddles')
            before = val('left')
            key('ArrowDown')
            check(val('left') > before, adapter + ' held-key paddle control works')
            w, h, rgb = m.fbuf()
            M.write_png_rgb(artifact('wireball-' + adapter + '.png'), w, h, rgb)
            check(any(rgb), adapter + ' raster displays the court')
            key('Escape')
            M.until(m, lambda _: m.read(m.sym('fsx_task'), 1) == b'\xff',
                    'practice returns to desktop', guest=8, poll=0.03)
            ui.settle()
            ui.mo.to(100, 90)
            xy = struct.unpack('<HH', m.read(m.sym('mouse_x'), 4))
            check(xy == (100, 90), adapter + ' desktop pointer works after practice')


def front():
    import os88marty as M
    import os88ui
    from PIL import Image
    for adapter, machine in [('vga', 'os8088_xt_vga'),
                             ('cga', 'os8088_5150_cga_gla'),
                             ('herc', 'os8088_5150_herc_gla')]:
        with os88ui.boot(artifact('os8088-360.img'),
                        apps=DISK_OVERRIDE or artifact('wireball360.img'), machine=machine) as ui:
            ui.path('B:/WIREBALL.O88')
            ui.settle()
            m = ui.m
            seg = dispapps.pkg_seg(m, 0)[1]
            sy = dispapps._map('wireball')
            def val(name, n=1):
                return int.from_bytes(m.readseg(seg, sy['wb_' + name], n), 'little')
            def key(name):
                m.key(name, up=False)
                M.guest_sleep(m, 0.08, poll=0.005)
                m.key(name, down=False)
                M.guest_sleep(m, 0.12, poll=0.005)
            def shot(label):
                ui.mo.to(2, 2)
                ui.settle()
                w, h, rgb = m.fbuf()
                path = artifact('wireball-' + adapter + '-' + label + '.png')
                M.write_png_rgb(path, w, h, rgb)
                return Image.open(path).convert('RGB')
            def centre(index):
                rect = struct.unpack('<HHHH', m.readseg(seg, sy['wb_brects'] + index*8, 8))
                return ((rect[0]+rect[2])//2, (rect[1]+rect[3])//2)
            initial = shot('splash')
            ox, oy, ax, ah = val('ox', 2), val('oy', 2), val('artx', 2), val('arth', 2)
            artwork = initial.crop((ox+ax, oy, ox+ax+416, oy+ah))
            colors = artwork.getcolors(100000)
            black = sum(n for n, rgb in colors if max(rgb) < 20)
            check(black < 416*ah*0.85, adapter + ' renders the illustrated title, not an empty banner')
            if adapter == 'vga':
                check(len(colors) >= 6, 'VGA splash preserves color artwork')
            else:
                check(ah == (52 if adapter == 'cga' else 104), adapter + ' uses its fitted monochrome artwork')
            play = centre(0)
            ui.mo.to(*play, l=True)
            M.pace(m, 0.2)
            check(m.read(m.sym('fsx_task'), 1) == b'\xff', adapter + ' Play waits for mouse release')
            ui.mo.to(2, 2, l=True)
            ui.mo.to(2, 2, l=False)
            M.pace(m, 0.2)
            check(m.read(m.sym('fsx_task'), 1) == b'\xff', adapter + ' sliding off cancels Play')
            ui.mo.click(*centre(2))
            M.until(m, lambda _: val('help') == 1, 'controls button', guest=4, poll=0.03)
            controls = shot('controls')
            check(controls.crop((ox+ax, oy, ox+ax+416, oy+ah)).tobytes() == artwork.tobytes(),
                  adapter + ' controls page retains the title artwork')
            ui.mo.click(*centre(2))
            M.until(m, lambda _: val('help') == 0, 'Back button', guest=4, poll=0.03)
            ui.mo.click(*centre(3))
            M.until(m, lambda _: val('muted') == 1, 'Sound button', guest=4, poll=0.03)
            key('KeyM')
            M.until(m, lambda _: val('muted') == 0, 'M unmutes', guest=5, poll=0.03)
            check(True, adapter + ' button and M key share the sound preference')
            ui.mo.click(*centre(1))
            M.until(m, lambda _: val('phase') == 2, 'Practice button launches', guest=5, poll=0.03)
            M.until(m, lambda _: val('soundnotes', 2) > 0, 'practice connection cue', guest=4, poll=0.03)
            key('Escape')
            M.until(m, lambda _: m.read(m.sym('fsx_task'), 1) == b'\xff',
                    'return to illustrated splash', guest=8, poll=0.03)
            ui.settle()
            check(val('soundactive') == 0, adapter + ' returning to splash releases game sound')


def physics():
    """Run the shipped integer routines on an 8088, with controlled contacts."""
    import os88marty as M
    import os88ui
    with os88ui.boot(artifact('os8088-360.img'),
                    apps=DISK_OVERRIDE or artifact('wireball360.img'), machine='os8088_5150_cga_gla') as ui:
        ui.path('B:/WIREBALL.O88')
        ui.settle()
        m = ui.m
        seg = dispapps.pkg_seg(m, 0)[1]
        sy = dispapps._map('wireball')
        m.pause()
        def put(name, value, n=1):
            m.write((seg << 4) + sy['wb_' + name], (value % (1 << (n*8))).to_bytes(n, 'little'))
        def val(name, n=1, signed=False):
            return int.from_bytes(m.readseg(seg, sy['wb_' + name], n), 'little', signed=signed)
        def call(name):
            saved = m.regs()
            regs = ('ax','bx','cx','dx','si','di','bp','sp','ss','ds','es','flags')
            sp = (saved['sp'] - 2) & 0xffff
            address = (saved['ss'] << 4) + sp
            old = m.read(address, 2)
            m.cmd(cmd='park', cs=seg, ip=sy['wb_' + name])
            for reg in regs:
                m.setreg(reg, saved[reg])
            m.setreg('ds', seg)
            m.setreg('es', seg)
            m.setreg('flags', saved['flags'] & ~0x600)
            m.setreg('sp', sp)
            target = sy['wb_keys.out']
            m.write(address, struct.pack('<H', target))
            m.bp_exec((seg << 4) + target)
            start = m.status()['cycles']
            m.run()
            assert m.wait_stop(10) == 'breakpoint', name + ' never returned'
            cycles = m.status()['cycles'] - start
            m.bp_exec()
            m.cmd(cmd='park', cs=saved['cs'], ip=saved['ip'])
            for reg in regs:
                m.setreg(reg, saved[reg])
            m.write(address, old)
            return cycles
        def fixture(x=8, y=38, vx=-8, vy=0, side=0, returns=0, spin=0):
            for name, value, n in [('ballx',x,2), ('bally',y,2), ('vx',vx,1), ('vy',vy,1),
                                   ('side',side,1), ('width',624,2), ('left',8,1), ('right',8,1),
                                   ('phase',2,1), ('practice',1,1), ('count',0,1), ('flags',0,1),
                                   ('scorel',0,1), ('scorer',0,1), ('returns',returns,1),
                                   ('lspin',spin,1), ('rspin',spin,1), ('fracx',0,1), ('fracy',0,1),
                                   ('muted',1,1)]:
                put(name, value, n)
        fixture()
        call('collide')
        check(val('vx', signed=True) == 8 and val('vy', signed=True) == 0,
              'center contact produces a flat, playable return')
        shots = []
        for offset in (-10,-8,-4,0,4,8,10):
            fixture(y=38+offset)
            call('collide')
            shots.append((val('vx', signed=True), val('vy', signed=True)))
        check(all(shot[0] > 0 for shot in shots) and [v for _,v in shots] == sorted(v for _,v in shots)
              and shots[0][1] < 0 < shots[-1][1] and shots[0][0] < shots[3][0],
              'end hits aim steeper than center hits, symmetrically above and below')
        for spin in (-1, 1):
            fixture(spin=spin)
            call('collide')
            check(val('vy', signed=True) * spin > 0, 'moving paddle transfers its direction into the return')
        fixture(y=48)
        call('collide')
        check(val('scorer') == 0 and val('vx', signed=True) > 0,
              'ball extent permits a grazing hit at the paddle end')
        fixture(y=49)
        call('collide')
        check(val('scorer') == 1 and val('count') == 36 and val('returns') == 0
              and abs(val('vx', signed=True)) == 8, 'a clean miss scores once and resets rally speed')
        fixture(x=612, y=38, vx=16, returns=16)
        call('collide')
        check(val('vx', signed=True) == -16, 'right paddle returns toward the other screen')
        speeds = []
        for count in (0,1,7,15,16):
            fixture(returns=count)
            call('collide')
            speeds.append(val('vx'))
        check(speeds == [8,9,12,16,16], 'rallies accelerate every two returns and stop at the speed cap')
        for vx, vy in [(9,3),(-9,-3),(16,8),(-16,-8)]:
            fixture(x=100, y=38, vx=vx, vy=vy)
            call('integrate')
            check(val('ballx',2) == 100+vx and val('bally',2) == 38+vy,
                  'substeps preserve full-tick displacement for velocity %d/%d' % (vx,vy))
        for y,vy,expected in [(1,-8,7),(75,8,69),(0,-1,1)]:
            fixture(x=100, y=y, vy=vy)
            call('integrate')
            check(val('bally',2) == expected and val('vy',signed=True) == -vy,
                  'wall reflection preserves overshoot and speed at %d/%d' % (y,vy))
        fixture(x=10,y=38,vx=-16,returns=16)
        cycles = call('integrate')
        check(val('vx',signed=True) > 0 and val('ballx',2) > 8 and val('scorer') == 0,
              'maximum-speed shot hits once and continues moving after contact')
        check(cycles < 19000, 'four collision steps fit within one 2400-baud byte on a 4.77MHz 8088 (%d cycles)' % cycles)
        for y, vy, paddle in [(0,-8,0),(76,8,16)]:
            fixture(x=10,y=y,vx=-16,vy=vy,returns=16)
            put('left',paddle)
            call('integrate')
            check(0 <= val('bally',2) <= 76 and val('vx',signed=True) > 0 and val('scorer') == 0,
                  'fast wall/paddle corner contact remains in bounds at row %d' % y)
        fixture(x=100,y=38,vx=16,vy=8,side=1)
        call('integrate')
        check(val('ballx',2) == 116 and val('bally',2) == 46,
              'remote prediction uses all four substeps at variable speeds')
        m.run()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mono', action='store_true', help='CGA/Hercules practice on a cycle-accurate 8088')
    parser.add_argument('--front', action='store_true', help='illustrated splash and release-fired controls on all adapters')
    parser.add_argument('--physics', action='store_true', help='integer contact, spin, speed and substep regression on an 8088')
    parser.add_argument('--disk', help='test a staged application disk instead of the standard build image')
    args = parser.parse_args()
    DISK_OVERRIDE = str(Path(args.disk).resolve()) if args.disk else None
    physics() if args.physics else front() if args.front else mono() if args.mono else main()
