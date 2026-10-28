#!/usr/bin/env python3
"""A port reporting line errors cannot win the mouse contest (SPEC.md 9.5.6).

Registered as mouerrq in soak. The field case: two Compaq Portable IIIs on a
null modem, WireBall or WireFleet open on one, the other booting. The peer's
2400-baud stream reaches the booting machine's mouse port at 1200 7N1, some
of the mangled bytes have a mouse packet's shape, and on a ONE-port machine
([mou_need] = 1) the first one settled the contest on the cable - the pointer
ran off to a side and the keyboard mouse retired.

Two legs, because no one emulator here has both halves:

  qemu   the STAMP and the gate together, on the field's shape: one serial
         port, nothing on it but a cable. A telnet BREAK on QEMU's chardev is
         LSR break + data ready (QEMU's serial_receive_break), which the ISR
         must stamp; a well-formed packet straight after must NOT settle the
         contest, and the same packet once the port has been quiet MOU_ERRQ
         ticks must. Against the kernel before 9.5.6 the first packet settles.
  marty  the gate alone on an 8088 with a real serial mouse: stamp the
         mouse's port by hand (MartyPC's 8250 reports no framing errors),
         move the mouse inside the window and it must not win; move it after
         and it must - a mouse that glitched once is delayed, not lost.

`--leg qemu` / `--leg marty` runs one.
"""
import argparse
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]

MOU_ERRQ = 36          # kernel/mouse.inc
PACKET = bytes([0x40, 0x05, 0x00])   # a Microsoft packet: header, dx=5, dy=0


def leg_qemu():
    import os88qemu
    import os88sym
    from telansi import Qemu
    S = os88sym.linear
    d = Path(tempfile.mkdtemp(prefix='mouerrq-'))
    link, qmp = str(d / 'link.sock'), str(d / 'qmp.sock')
    args = ['qemu-system-i386', '-machine', 'pc,vmport=off', '-m', '16M',
            '-serial', 'none', '-parallel', 'none',
            '-drive', f'file={ROOT}/build/os8088.img,if=floppy,index=0,format=raw,snapshot=on',
            '-boot', 'a', '-display', 'none', '-nic', 'none',
            '-chardev', f'socket,id=link,path={link},server=on,wait=off,telnet=on',
            '-device', 'isa-serial,chardev=link,index=0',     # ONE port: the cable
            '-qmp', f'unix:{qmp},server=on,wait=off']
    p = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1)
        m = Qemu(qmp)
        b = lambda n: m.read(S(n), 1)[0]
        w = lambda n, i=0: struct.unpack_from('<H', m.read(S(n) + i, 2))[0]
        assert os88qemu.acted(m, lambda: os88qemu.ticks(m) > 3 and b('spl_live') == 0,
                              secs=90, what='desktop'), 'no desktop'
        assert w('mou_bases') == 0x3F8 and w('mou_bases', 2) == 0, 'not a one-port machine'
        assert b('mou_need') == 1, 'one port should need ONE packet (SPEC.md 9.5)'
        kt = lambda: w('ticks')
        assert b('mou_seen') == 0, 'settled before the cable said anything'
        print('      boot left mou_errt %04x at kernel tick %04x' % (w('mou_errt'), kt()))
        s = socket.socket(socket.AF_UNIX)
        s.connect(link)
        os88qemu.pace(m, 0.5)
        s.sendall(b'\xff\xf3')                    # telnet BREAK -> LSR BI|DR
        assert os88qemu.acted(m, lambda: (kt() - w('mou_errt')) & 0xFFFF < MOU_ERRQ,
                              secs=3, what='the break stamped'), \
            'the ISR did not stamp the break (mou_errt %04x, ticks %04x)' % (
                w('mou_errt'), kt())
        stamp = w('mou_errt')
        s.sendall(PACKET)
        os88qemu.pace(m, 0.5)
        assert (kt() - stamp) & 0xFFFF < MOU_ERRQ, 'the test was too slow'
        assert b('mou_seen') == 0, 'a packet straight after a line error settled the contest'
        print('PASS  qemu: a BREAK on the one-port cable, then a packet - not settled')
        assert os88qemu.acted(m, lambda: (kt() - stamp) & 0xFFFF > MOU_ERRQ + 2,
                              secs=6, what='the window to pass')
        s.sendall(PACKET)
        assert os88qemu.acted(m, lambda: b('mou_seen') == 1, secs=3, what='settled'), \
            'a clean packet after the window did not settle - a real mouse would be locked out'
        assert b('mou_port') == 0
        print('PASS  qemu: the same packet %d ticks clean later settles port 0' % MOU_ERRQ)
        s.close()
    finally:
        p.terminate()
        p.wait()


def leg_marty():
    import os88marty as M
    import os88ui
    with os88ui.boot(str(ROOT / 'build/os8088-360.img'), machine='os8088_5150_cga_gla') as ui:
        m = ui.m
        b = lambda n: m.read(m.sym(n), 1)[0]
        tick = lambda: struct.unpack('<H', m.read(m.sym('ticks'), 2))[0]
        assert b('mou_seen') == 0, 'the mouse settled before the row moved it'
        m.pause()
        m.write(m.sym('mou_errt'), struct.pack('<HH', tick(), tick()))
        stamp = tick()
        m.run()
        for i in range(12):                       # 12 packets: half again a run
            m.mouse(3 if i & 1 else -3, 0)
        M.until(m, lambda _: b('mou_run') or (tick() - stamp) & 0xFFFF > 20,
                'the packets to arrive', guest=3, poll=.02)
        assert (tick() - stamp) & 0xFFFF < MOU_ERRQ, 'the moves outran the window'
        assert b('mou_seen') == 0, 'the mouse won on a port stamped inside the window'
        print('PASS  marty: inside the window, 12 packets do not settle')
        M.until(m, lambda _: (tick() - stamp) & 0xFFFF > MOU_ERRQ + 2, 'the window to pass',
                guest=6, poll=.05)
        for i in range(12):
            m.mouse(3 if i & 1 else -3, 0)
        M.until(m, lambda _: b('mou_seen') == 1, 'the mouse to win after the window',
                guest=5, poll=.05)
        print('PASS  marty: after the window the mouse wins port %d' % b('mou_port'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--leg', choices=('qemu', 'marty'))
    a = ap.parse_args()
    if a.leg in (None, 'qemu'):
        leg_qemu()
    if a.leg in (None, 'marty'):
        leg_marty()
    print('mouerrq: ok')


if __name__ == '__main__':
    main()
