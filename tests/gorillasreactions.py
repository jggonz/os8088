#!/usr/bin/env python3
"""Check sun expressions and musical scoring poses in the actual 8086 guest."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

import gorillas as G
import gorillascity as C
import gorillasinput as I


def reactions(m, p, code, tag):
    def byte(name, value):
        m.write(p.base + p.offsets['gr_' + name], bytes([value]))

    def word(name, value):
        m.write(p.base + p.offsets['gr_' + name], struct.pack('<H', value & 65535))

    def call(name, **kwargs):
        return C.call(m, code, 'gr_' + name, **kwargs)

    def pixels(x, y, w, h):
        scene = p.data('scene', 16384)
        return bytes((scene[yy*128+xx//2] >> (0 if xx & 1 else 4)) & 15
                     for yy in range(y, y+h) for xx in range(x, x+w))

    def shot(x, y, vx=0):
        byte('state', 1)
        byte('saved', 0)
        for name, value in (('px', x*64), ('py', y*64), ('pyhi', 0),
                            ('vx', vx), ('vy', 0), ('age', 0), ('wind', 0),
                            ('grem', 0), ('wrem', 0)):
            word(name, value)
        call('hudpaint')

    call('city')
    call('fullpaint')
    happy = pixels(116, 25, 25, 21)
    shot(110, 25)
    call('tick')
    assert not p.b('sunhit'), 'near miss shocked the sun'
    call('unbanana')

    shot(114, 35, 256)
    call('tick')
    assert p.b('sunhit') == 1 and p.b('state') == 1, 'sun stopped or ignored shot'
    assert p.w('px') == 118*64, 'sun repaint corrupted swept substeps'
    assert pixels(128, 38, 1, 1) == b'\0', 'mouth did not open'
    # Traverse the mouth with the saved banana patch, then move away.
    for _ in range(8):
        call('tick')
    call('unbanana')
    shocked = pixels(116, 25, 25, 21)
    assert shocked != happy and p.b('sunhit') == 1
    assert pixels(128, 38, 1, 1) == b'\0', 'banana restore erased the open mouth'
    C.check_pixels(m, p, tag)
    I.check_repaint(m, p, code, tag)
    byte('paused', 1)
    frozen = p.data('scene', 16384)
    call('tick')
    assert p.data('scene', 16384) == frozen and p.b('sunhit') == 1
    byte('paused', 0)
    word('px', 255*64)
    call('tick')
    assert p.b('state') == 0 and not p.b('sunhit')
    assert pixels(116, 25, 25, 21) == happy, 'smile did not return after miss'
    C.check_pixels(m, p, tag)

    # Both winners, an opponent hit, a self-hit, and a match-ending point.
    for thrower, victim, target in ((0, 1, 3), (0, 0, 1)):
        call('city')
        call('fullpaint')
        word('scores', 0)
        byte('turn', thrower)
        byte('target', target)
        xs, ys = (struct.unpack('<2H', p.data(n, 4)) for n in ('gx', 'gy'))
        shot(xs[victim]+8, ys[victim]+8)
        call('tick')
        winner = victim ^ 1
        assert p.b('winner') == winner and p.data('scores', 2)[winner] == 1
        assert p.b('state') == (3 if target == 1 else 2)
        assert p.b('dancing') == 9, 'point did not queue victory dance'
        loser = pixels(xs[victim], ys[victim], 16, 20)
        call('key', ax=13)
        assert p.b('dancing') == 9, 'Enter skipped the celebration'
        for remaining in range(8, 0, -1):
            # Fast-forward only the previous phrase; invoke real game ticks.
            call('musicstop')
            call('tick')
            assert p.b('dancing') == remaining
            pose = 'gr_apeleft' if remaining % 2 == 0 else 'gr_aperight'
            packed = m.read(p.base + code[pose], 160)
            expected = bytes(v for pair in packed for v in (pair >> 4, pair & 15))
            assert pixels(xs[winner], ys[winner], 16, 20) == expected, 'wrong arm pose'
            assert pixels(xs[victim], ys[victim], 16, 20) == loser, 'loser danced'
            assert p.w('musicptr') == code['gr_musicvictory'] + 4
            assert p.w('musicdue'), 'victory notes were not scheduled'
            C.check_pixels(m, p, tag)
            if remaining == 8:
                byte('paused', 1)
                call('tick')
                assert p.b('dancing') == 8 and p.w('musicptr') == code['gr_musicvictory']+4
                byte('paused', 0)
        call('musicstop')
        call('tick')
        assert not p.b('dancing') and not p.w('musicptr')
        I.check_repaint(m, p, code, tag)
        if target > 1:
            call('key', ax=13)
            assert p.b('state') == 0, 'Enter did not advance after dance'

    notes = struct.unpack('<17H', m.read(p.base + code['gr_musicvictory'], 34))
    assert notes == (82, 1, 87, 1, 98, 1, 82, 1, 87, 1, 73, 1, 65, 1, 0, 4, 65535)
    call('city')
    call('fullpaint')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--arm', choices=G.ARMS)
    args = ap.parse_args()
    off = G.offsets()
    code = I.code_offsets(('gr_city', 'gr_hudpaint', 'gr_unbanana',
                           'gr_musicstop', 'gr_musicvictory'))
    subprocess.run(['python3', 'tools/gorillas_music.py', '--check'], cwd=G.ROOT, check=True)
    with tempfile.TemporaryDirectory() as td:
        disk = Path(td) / 'gorillas.img'
        subprocess.run(['python3', 'tools/os88disk.py', '-o', str(disk), '--size', '360',
                        G.os88build.at('build/gorillas.o88')], cwd=G.ROOT, check=True)
        for tag in ([args.arm] if args.arm else G.ARMS):
            with G.os88ui.boot(G.os88build.at('build/os8088-360.img'), apps=str(disk),
                              machine=G.ARMS[tag]) as ui:
                m = ui.m
                ui.open_drive('B')
                ui.open('GORILLAS.O88')
                p = G.Probe(ui, off)
                m.gorillas_probe = p
                G.setup(m)
                for mode in ('window', 'full'):
                    if mode == 'full':
                        G.key(m, 'KeyF')
                        G.wait(m, lambda: p.b('fsready'), 'fullscreen ready')
                    C.lock(m, p, code)
                    reactions(m, p, code, tag)
                    m.run()
                    print('PASS', tag, mode, 'sun, banana patches, scoring dance, tune, pixels', flush=True)


if __name__ == '__main__':
    main()
