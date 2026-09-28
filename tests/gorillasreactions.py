#!/usr/bin/env python3
"""Check sun expressions and musical scoring poses in the actual 8086 guest."""
import argparse
import math
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
        byte('throwticks', 0)
        byte('blast', 0)
        byte('roundwait', 0)
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
    assert not p.b('saved'), 'banana must be hidden behind the sun'
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
        assert p.b('blast') == 1 and not p.b('dancing'), 'missing death animation'
        frames = set()
        for frame in range(36):
            call('tick')
            frames.add(p.data('scene', 16384))
            if frame in (0, 17, 35):
                C.check_pixels(m, p, tag)
                I.check_repaint(m, p, code, tag)
        assert len(frames) > 20 and not p.b('blast') and p.b('dancing') == 9
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
        assert p.b('roundwait') == 18
        for _ in range(18):
            call('tick')
        assert p.b('state') == (0 if target > 1 else 8), 'automatic round/results transition failed'
        if target > 1:
            assert p.b('turn') == (thrower ^ 1), 'round did not alternate throwers'

    notes = struct.unpack('<17H', m.read(p.base + code['gr_musicvictory'], 34))
    assert notes == (82, 1, 87, 1, 98, 1, 82, 1, 87, 1, 73, 1, 65, 1, 0, 4, 65535)
    parity(m, p, code, tag)
    call('city')
    call('fullpaint')


def parity(m, p, code, tag):
    def byte(name, value):
        m.write(p.base + p.offsets['gr_' + name], bytes([value]))
    def word(name, value):
        m.write(p.base + p.offsets['gr_' + name], struct.pack('<H', value & 65535))
    def call(name, **kwargs):
        return C.call(m, code, 'gr_' + name, **kwargs)
    def ink(x, y):
        v = p.data('scene', 16384)[y*128+x//2]
        return (v >> (0 if x & 1 else 4)) & 15

    # Overall winner is independent of the final point's scorer; ties are
    # explicit. Verify glyph cache, native pixels after repaint, and restart.
    for scores, winner in (((3, 1), b'Player 1'), ((1, 3), b'Player 2'), ((2, 2), None)):
        m.write(p.base + p.offsets['gr_scores'], bytes(scores))
        byte('winner', 1 if scores[0] > scores[1] else 0)
        call('results')
        chars = p.data('hudchars', 512)
        assert b'GAME OVER!' in chars
        assert b'Player 1' in chars[160:192] and ('%03d' % scores[0]).encode() in chars[160:192]
        assert b'Player 2' in chars[224:256] and ('%03d' % scores[1]).encode() in chars[224:256]
        assert (winner + b' ' in chars[320:352] and b'wins!' in chars[320:352]) if winner else b'tied match' in chars
        I.check_repaint(m, p, code, tag)
        # Marquee must animate without drawing intro gorillas over the results.
        before = I.video_bytes(m, tag)
        word('animdue', 0)
        call('animate')
        after = I.video_bytes(m, tag)
        assert before != after, 'final scorecard border did not animate'
        I.check_repaint(m, p, code, tag)
        from gorillasfront import screenshot
        out = G.ROOT / 'build/gorillas-proof'
        out.mkdir(exist_ok=True)
        screenshot(m, p, out / ('%s-results-%s-%s.png' %
                   (tag, 'full' if p.b('fs') else 'window', 'tie' if winner is None else winner.decode().replace(' ', ''))))
        call('key', ax=ord('x'))
        assert p.b('state') == 5

    call('city'); call('fullpaint')
    # Switching turns clears the previous name, including a ten-to-one-char
    # transition, while preserving both scores and the incremental display.
    originals = [p.data(name, 11) for name in ('name1', 'name2')]
    for name, value in (('name1', b'ABCDEFGHIJ'), ('name2', b'Z')):
        m.write(p.base+p.offsets['gr_'+name], value.ljust(11, b'\0'))
    for turn in (0, 1, 0):
        byte('turn', turn)
        call('hudpaint')
        I.check_hud(m, p, code)
        I.check_repaint(m, p, code, tag)
    for name, value in zip(('name1', 'name2'), originals):
        m.write(p.base+p.offsets['gr_'+name], value)
    call('hudpaint')
    # Both aiming fields ignore decimal points without replacing a value.
    # Exercise bounds, deletion, zero and unpadded values through real keys.
    for field, name in enumerate(('angle', 'power')):
        byte('field', field); byte('edit', 0)
        for ch in '225': call('key', ax=ord(ch))
        call('key', ax=ord('.'))
        assert p.w(name) == 225
        call('key', ax=8)
        assert p.w(name) == 22
        for value in (360, 0, 7, 70):
            byte('edit', 0)
            call('key', ax=ord('.'))
            assert not p.b('edit')
            for ch in str(value): call('key', ax=ord(ch))
            assert p.w(name) == value
            if value == 360:
                call('key', ax=ord('1'))
                call('key', ax=0x4800)
                assert p.w(name) == 360
            call('hudpaint')
            I.check_hud(m, p, code)
            I.check_repaint(m, p, code, tag)
    # Gravity retains decimal limits and its original 9.8 default.
    for text, valid, value in (('0.001', True, (0, 1)), ('9999.999', True, (9999, 999)),
                               ('10000', False, None), ('1.2.3', False, None),
                               ('1.2345', False, None)):
        # Run through setup acceptance, not a duplicate host parser.
        byte('state', 5); byte('setupfield', 4)
        byte('inputlen', len(text))
        m.write(p.base+p.offsets['gr_input'], text.encode()+b'\0')
        call('key', ax=13)
        assert (p.b('state') == 6) == valid, text
        if valid: assert (p.w('gwhole'), p.w('gfrac')) == value
    word('gwhole', 9); word('gfrac', 800); call('gravity')
    call('city'); call('fullpaint')

    # Empty silhouette pixels do not count as hits. A real arm pixel does.
    gx, gy = p.w('gx'), p.w('gy')
    for dx, dy, hit in ((0, 0, False), (0, 9, True)):
        byte('state', 1); byte('turn', 0); byte('saved', 0)
        byte('blast', 0); byte('throwticks', 0); byte('dancing', 0)
        word('px', (gx+dx)*64); word('py', (gy+dy)*64); word('pyhi', 0)
        word('vx', 0); word('vy', 0)
        call('tick')
        assert bool(p.b('blast')) == hit, 'gorilla silhouette collision'
        call('unbanana')
    byte('blast', 0); byte('dancing', 0)
    call('city'); call('fullpaint')

    # Building blast grows and shrinks, preserves its hole and pauses.
    byte('state', 1)
    call('blaststart', cx=128, dx=105, ax=7)
    original = p.data('scene', 16384)
    byte('paused', 1); call('tick')
    assert p.b('blast') == 1 and p.data('scene', 16384) == original
    byte('paused', 0)
    for _ in range(14): call('tick')
    assert not p.b('blast') and ink(128, 105) == 0
    C.check_pixels(m, p, tag); I.check_repaint(m, p, code, tag)

    # Worst byte alignment: flooring the dirty left edge must not omit the
    # outer two pixels on the right of a radius-18 explosion.
    byte('state', 2)
    call('blaststart', cx=71, dx=55, ax=18)
    byte('blast', 18)
    call('tick')
    assert ink(89, 55) != 0
    C.check_pixels(m, p, tag); I.check_repaint(m, p, code, tag)
    byte('blast', 0); byte('state', 0)

    # The four masks are visibly distinct; the launch pose returns to rest.
    images = set()
    for frame in range(4):
        word('px', 20*64); word('py', 28*64); word('pyhi', 0)
        word('age', frame); byte('insun', 0)
        call('banana')
        images.add(p.data('scene', 16384))
        C.check_pixels(m, p, tag)
        call('unbanana')
    assert len(images) == 4
    byte('turn', 0); word('angle', 45); word('power', 100)
    call('fire'); call('hudpaint')
    assert p.b('throwticks') == 2
    posed = p.data('scene', 16384)
    call('tick'); call('tick')
    assert not p.b('throwticks') and p.data('scene', 16384) != posed
    byte('state', 0); call('musicstop')

    # Reference equation oracle, independent of guest's integer integrator.
    # A temporary guest loop advances 20 frames without drawing/collision;
    # its scratch bytes and all register state are restored by the harness.
    scratch = p.offsets['gr_band']
    asm = f"""cpu 8086
org {scratch}
mov di, 20
.frame:
call {code['gr_framebegin']}
mov bp, [word {p.offsets['gr_steps']}]
.step:
call {code['gr_motionstep']}
dec bp
jnz .step
xor bx, bx
call {code['gr_accelerate']}
dec di
jnz .frame
ret
"""
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td)/'curve.asm', Path(td)/'curve.bin'
        src.write_text(asm)
        subprocess.run(['nasm', '-f', 'bin', '-o', str(dst), str(src)], check=True)
        stub = dst.read_bytes()
    saved = p.data('band', len(stub))
    m.write(p.base+scratch, stub)
    code['curve'] = scratch
    for angle, power, wind in ((0, 360, 15), (45, 70, -14),
                               (90, 150, 0), (180, 360, 0),
                               (225, 200, -14), (270, 100, 0),
                               (360, 360, 15)):
        word('wind', wind)
        call('initmotion', si=p.offsets['gr_px'], bp=0, ax=angle, di=power)
        x0, y0 = p.w('px')/64, p.w('py')/64
        C.call(m, code, 'curve', si=p.offsets['gr_px'])
        theta, t = math.radians(angle), 2
        x = x0+(power*math.cos(theta)*t+.5*wind/5*t*t)*256/640
        y = y0+(-power*math.sin(theta)*t+.5*9.8*t*t)*128/350
        actualx = struct.unpack('<h', p.data('px', 2))[0]/64
        actualy = struct.unpack('<i', p.data('py', 4))[0]/64
        assert abs(actualx-x) < .75 and abs(actualy-y) < .75, (angle, actualx, x, actualy, y)
    m.write(p.base+scratch, saved)
    print('PASS', tag, 'scorecard/tie, numeric ranges, pixel collisions, blasts, sprites, reference curves', flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--arm', choices=G.ARMS)
    args = ap.parse_args()
    off = G.offsets()
    code = I.code_offsets(('gr_city', 'gr_hudpaint', 'gr_unbanana',
                           'gr_musicstop', 'gr_musicvictory', 'gr_results', 'gr_fire',
                           'gr_gravity', 'gr_initmotion', 'gr_framebegin',
                           'gr_motionstep', 'gr_accelerate', 'gr_decimalparse',
                           'gr_ape', 'gr_banana', 'gr_blaststart'))
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
