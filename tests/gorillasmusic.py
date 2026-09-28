#!/usr/bin/env python3
"""Exercise Gorillas FM scores in the guest, on AdLib, SB and speaker only.

Real keyboard setup/fullscreen and worker progression, then calls under the
existing graphics lock to examine complete loops and timing boundaries.
Reads SOUND.DRV's ownership and keyed-channel state, not only game counters.
--clobber-loop replaces a loop marker with a rest: the loop assertion goes red.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import time

import gorillas as G
import gorillascity as C
import gorillasinput as I
from cycweb import pkg_syms

MACHINES = {'adlib': 'os8088_5150_herc_adlib_720_gla',
            'sb': 'os8088_5150_herc_sb_gla',
            'speaker': 'os8088_5150_herc_gla'}


def arm(tag, disk, off, code, driver, clobber):
    with G.os88ui.boot(G.os88build.at('build/os8088-360.img'),
                      apps=str(disk), machine=MACHINES[tag]) as ui:
        m = ui.m
        ui.path('B:/GORILLAS.O88')
        p = G.Probe(ui, off)
        m.gorillas_probe = p
        G.setup(m, music='' if tag == 'speaker' else 'No')
        if tag == 'speaker':
            assert p.b('bgmstatus') == 2, 'missing FM did not degrade cleanly'
            G.key(m, 'Enter'); G.key(m, 'Digit1'); G.key(m, 'Enter')
            G.wait(m, lambda: p.b('state') == 2, 'speaker-only throw still scores')
            print('PASS speaker: no FM, gameplay and reference effects', flush=True)
            return

        assert not p.b('bgmenabled') and p.b('bgmstatus') == 0
        before = p.w('bgmptr')
        G.M.guest_sleep(m, .5)
        assert p.w('bgmptr') == before, 'No in setup still played music'
        G.key(m, 'KeyM')
        G.wait(m, lambda: p.b('bgmstatus') == 1, 'M enables music after No in setup')
        # The driver's movable segment is always read from its live row.
        def dr(name, size=8):
            seg = struct.unpack('<H', m.read(m.sym('drv_tab') + 2, 2))[0]
            assert seg, 'sound driver not loaded'
            return m.readseg(seg, driver[name], size)

        channels = list(p.data('bgmchannels', 3))
        assert len(set(channels)) == 3 and max(channels) < 8
        owner = dr('opl_own')[channels[0]]
        assert owner != 255 and all(dr('opl_own')[c] == owner for c in channels)
        # Real worker progression during idle aiming (there is no animation).
        before = p.w('bgmptr')
        G.wait(m, lambda: p.w('bgmptr') != before, 'idle worker plays next notes')
        G.key(m, 'KeyP')
        assert p.b('paused') and p.b('bgmstatus') == 0
        assert owner not in dr('opl_own'), 'pause left FM claims'
        before = p.w('bgmptr')
        G.M.guest_sleep(m, .5)
        assert p.w('bgmptr') == before, 'music advanced while paused'
        G.key(m, 'KeyM')
        assert not p.b('bgmenabled') and p.b('bgmstatus') == 0
        G.key(m, 'KeyM')
        assert p.b('bgmenabled') and p.b('bgmstatus') == 0, 'M bypassed pause'
        G.key(m, 'KeyP')
        G.wait(m, lambda: p.b('bgmstatus') == 1, 'FM resumes')
        G.key(m, 'KeyF')
        G.wait(m, lambda: p.b('fsready'), 'fullscreen ready')
        before = p.w('bgmptr')
        G.wait(m, lambda: p.w('bgmptr') != before, 'fullscreen music advances')
        G.key(m, 'KeyM')
        assert not p.b('bgmenabled') and owner not in dr('opl_own')
        G.key(m, 'KeyM')
        G.wait(m, lambda: p.b('bgmstatus') == 1, 'fullscreen M resumes')
        G.key(m, 'Escape')
        G.wait(m, lambda: not p.b('fs'), 'window restored')
        G.M.ui_done(m)
        ui.raise_window(ui.window('Disk'))
        G.wait(m, lambda: p.b('bgmstatus') == 0, 'unfocused music releases channels')
        assert owner not in dr('opl_own')
        before = p.w('bgmptr')
        G.M.guest_sleep(m, .5)
        assert p.w('bgmptr') == before
        ui.raise_window(ui.window('Gorillas'))
        G.wait(m, lambda: p.b('bgmstatus') == 1, 'refocused music resumes')
        C.lock(m, p, code)

        def call(name, **args):
            return C.call(m, code, 'gr_' + name, **args)

        def byte(name, value):
            m.write(p.base + off['gr_' + name], bytes([value]))

        def word(name, value):
            m.write(p.base + off['gr_' + name], struct.pack('<H', value & 65535))

        def now():
            return struct.unpack('<H', m.read(m.sym('ticks'), 2))[0]

        call('about')
        before = p.w('bgmptr')
        call('tick')
        assert p.b('abon') and p.w('bgmptr') == before and owner not in dr('opl_own')
        call('key', ax=32)
        call('tick')
        assert not p.b('abon') and p.b('bgmstatus') == 1

        # Exact acceptance, including mixed case and single-letter answers.
        call('setup')
        for answer, enabled in (('yes', 1), ('YeS', 1), ('Y', 1), ('', 1),
                                ('nO', 0), ('N', 0), ('nope', None), ('ye', None)):
            byte('state', 5)
            byte('setupfield', 5)
            byte('bgmenabled', 1)
            byte('inputlen', len(answer))
            m.write(p.base + off['gr_input'], answer.encode() + b'\0')
            call('key', ax=13)
            if enabled is None:
                assert p.b('state') == 5 and p.b('setupfield') == 5
                assert b'Enter Yes or No' in p.data('hudchars', 512)
            else:
                assert p.b('state') == 6 and p.b('bgmenabled') == enabled
        call('new')
        call('bgmtick')

        # The key dispatcher must honor both cases throughout gameplay,
        # including AI aiming and celebrations, without interrupting SFX.
        call('musicstart', si=code['gr_musicvictory'])
        effect = p.w('musicptr')
        for state, dancing, players, turn in ((0, 0, 2, 0), (1, 0, 2, 0),
                                             (2, 8, 2, 0), (3, 8, 2, 0),
                                             (0, 0, 1, 1)):
            for name, value in (('state', state), ('dancing', dancing),
                                ('players', players), ('turn', turn)):
                byte(name, value)
            call('key', ax=ord('m'))
            before = p.w('bgmptr')
            call('bgmtick')
            assert not p.b('bgmenabled') and owner not in dr('opl_own')
            assert p.w('bgmptr') == before and p.w('musicptr') == effect
            call('key', ax=ord('M'))
            assert p.b('bgmenabled') and p.b('bgmstatus') == 1
            assert p.w('musicptr') == effect
        call('musicstop')
        byte('players', 2)
        byte('dancing', 0)
        call('key', ax=ord('m'))
        byte('state', 2)
        byte('roundwait', 1)
        call('roundtick')
        assert not p.b('bgmenabled') and owner not in dr('opl_own'), 'new skyline unmuted music'
        call('key', ax=ord('m'))

        starts = []
        for level in range(4):
            # Trigger the automatic next-skyline path after its one-tick wait.
            byte('scores', level)
            byte('state', 2)
            byte('roundwait', 1)
            call('roundtick')
            assert p.b('state') == 0 and p.b('bgmtrack') == level % 3
            start = p.w('bgmstart')
            starts.append(start)
            call('bgmhold')
            word('bgmptr', start)
            if clobber:
                m.write(p.base + start + 128*7, b'\x03')
            # Walk all 128 rows and the seam with actual FM driver calls.
            for row in range(130):
                word('bgmdue', now())
                call('bgmtick')
                index = row % 128
                assert p.w('bgmptr') == start + (index+1)*7, \
                    ('loop did not return to first row', tag, level, row)
                channels = list(p.data('bgmchannels', 3))
                notes = struct.unpack('<3H', m.read(p.base + start + index*7+1, 6))
                keyed = dr('opl_b0')
                assert [bool(keyed[c] & 32) for c in channels] == [bool(n) for n in notes], \
                    ('card key-on/rest state differs from score', tag, level, row)
                assert all(dr('opl_own')[c] == owner for c in channels)
            word('bgmdue', now()+20)
            before = p.w('bgmptr')
            call('bgmtick')
            assert p.w('bgmptr') == before, 'notes ignored their deadline'
        assert len(set(starts[:3])) == 3 and starts[3] == starts[0]

        # A long gap must resync once, without rushing missed rows.
        word('bgmdue', now()-200)
        call('bgmtick')
        assert 0 < ((p.w('bgmdue')-now()) & 65535) < 10

        # Simulate another instance's claims, including insufficient channels.
        call('bgmhold')
        seg = struct.unpack('<H', m.read(m.sym('drv_tab')+2, 2))[0]
        owners_at = (seg << 4) + driver['opl_own']
        original = m.read(owners_at, 8)
        foreign = (owner+1) % 16
        try:
            m.write(owners_at, bytes([foreign])*2 + bytes([255])*6)
            call('bgmtick')
            assert list(p.data('bgmchannels', 3)) == [2, 3, 4]
            call('bgmhold')
            m.write(owners_at, bytes([foreign])*7 + b'\xff')
            call('bgmtick')
            assert p.b('bgmstatus') == 2
            assert m.read(owners_at, 8) == bytes([foreign])*7 + b'\xff', \
                'partial allocation leaked a claim or stole another instance'
        finally:
            m.write(owners_at, original)

        call('new')
        call('bgmtick')
        assert p.b('bgmtrack') == 0 and p.b('bgmstatus') == 1
        call('results')
        assert not p.w('bgmptr') and owner not in dr('opl_own')
        call('new')
        call('bgmtick')
        call('setup')
        assert not p.w('bgmptr') and owner not in dr('opl_own')
        call('new')
        call('fullpaint')
        m.bp_exec()
        m.run()
        G.wait(m, lambda: p.b('bgmstatus') == 1, 'music before closing')
        ui.close(ui.window('Gorillas'))
        assert owner not in dr('opl_own'), 'close leaked FM channels'
        print('PASS', tag, 'Yes/No setup, M toggle in all gameplay states, three loops, level rotation, '
              'FM notes/rests, pause, focus, About, fullscreen, '
              'contention, setup/results/restart and close', flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--arm', choices=MACHINES)
    ap.add_argument('--clobber-loop', action='store_true')
    args = ap.parse_args()
    started = time.monotonic()
    subprocess.run(['python3', 'tools/gorillas_bgm.py', '--check'], cwd=G.ROOT, check=True)
    off = G.offsets()
    code = I.code_offsets(('gr_bgmtick', 'gr_bgmhold', 'gr_new', 'gr_results',
                           'gr_setup', 'gr_roundtick', 'gr_about',
                           'gr_musicstart', 'gr_musicvictory', 'gr_musicstop'))
    driver, _ = pkg_syms('drivers/sound/sound.asm',
                         ('drivers/sound/', 'drivers/', 'apps/'))
    with tempfile.TemporaryDirectory() as td:
        disk = Path(td) / 'gorillas.img'
        subprocess.run(['python3', 'tools/os88disk.py', '-o', str(disk), '--size', '360',
                        G.os88build.at('build/gorillas.o88')], cwd=G.ROOT, check=True)
        for tag in ([args.arm] if args.arm else MACHINES):
            arm(tag, disk, off, code, driver, args.clobber_loop)
    print('Gorillas music: %.1fs' % (time.monotonic()-started))


if __name__ == '__main__':
    main()
