#!/usr/bin/env python3
"""Exercise the shipped 8086 Gorillas on VGA, CGA and Hercules.

Uses real keyboard throws, including low-power self-hits through five rounds
(first-to-three), pause/resume, terrain destruction, and repeated fullscreen
entry/exit. Reads package state, checks rendered pixels and saves proof images.
The CGA leg requires the foreign-mode color backend, not a monochrome frame.
"""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import os88build
import os88geom as G
import os88marty as M
import os88ui

ARMS = {'vga': 'os8088_xt_vga', 'cga': 'os8088_5150_cga_gla',
        'herc': 'os8088_5150_herc_gla'}


def offsets():
    source = (ROOT / 'apps/gorillas/gorillas.asm').read_text()
    names = re.findall(r'^VAR (gr_\w+),', source, re.M)
    probe = source.replace('OS88_IMAGE_END', '') + '\n'
    probe += '\n'.join('dw %s-os88_image_end' % n for n in names)
    probe += '\nOS88_IMAGE_END\n'
    with tempfile.TemporaryDirectory() as td:
        asm, binary = Path(td) / 'probe.asm', Path(td) / 'probe.bin'
        asm.write_text(probe)
        subprocess.run(['nasm', '-f', 'bin', '-I', 'apps/', '-I', 'apps/gorillas/', '-o', str(binary),
                        str(asm)], cwd=ROOT, check=True)
        values = struct.unpack('<%dH' % len(names),
                               binary.read_bytes()[-2 * len(names):])
    size = struct.unpack_from('<H', Path(os88build.at('build/gorillas.bin')).read_bytes(), 8)[0]
    return {n: size + v for n, v in zip(names, values)}


class Probe:
    def __init__(self, ui, offsets):
        self.m, self.offsets = ui.m, offsets
        win = ui.window('Gorillas')
        raw = self.m.read(self.m.sym('wm_wins'), G.MAX_WIN * G.WIN_SIZE)
        self.base = struct.unpack_from('<H', raw, win.i * G.WIN_SIZE + G.W_SEG)[0] << 4
        assert self.base

    def data(self, name, count=1):
        return self.m.read(self.base + self.offsets['gr_' + name], count)

    def b(self, name):
        return self.data(name)[0]

    def w(self, name):
        return struct.unpack('<H', self.data(name, 2))[0]


def wait(m, pred, what):
    try:
        M.until(m, lambda _: pred(), what, poll=.1, limit=30)
    except M.MartyError:
        p = getattr(m, 'gorillas_probe', None)
        if p:
            print('Gorillas failure state:',
                  {n: p.b(n) for n in ('state', 'field', 'turn', 'fs', 'vga', 'cga')},
                  {n: p.w(n) for n in ('angle', 'power', 'px', 'py', 'age')},
                  flush=True)
        raise


def key(m, name):
    m.key(name)
    M.pace(m, .15)
    # An active worker takes the drawing lock every tick; waiting for a
    # sustained idle UI can accidentally wait for the entire shot to end.
    if not m.gorillas_probe.b('fs') and m.gorillas_probe.b('state') != 1:
        M.ui_done(m)


def capture(m, p, path):
    # Native 1bpp VRAM decoding is invalid in CGA's four-color mode.
    if m.video()['type'] == 'vga' or p.b('cga'):
        w, h, pixels = m.fbuf(0)
        M.write_png_rgb(str(path), w, h, pixels)
        colors = set(zip(pixels[0::3], pixels[1::3], pixels[2::3]))
        assert len(colors) >= 4, ('missing color scene', colors)
        assert any(r < 20 and g < 20 and b > 120 for r, g, b in colors), \
            ('the reference blue sky is missing', colors)
        if p.b('cga'):
            assert any(r < 120 and g > 200 and b < 120 for r, g, b in colors), \
                ('CGA warm palette lacks bright green', colors)
            assert any(r > 200 and g < 120 and b < 120 for r, g, b in colors), \
                ('CGA warm palette lacks bright red', colors)
            assert not any(r > 200 and g < 120 and b > 200 for r, g, b in colors), \
                'CGA still uses the old magenta palette'
        else:
            for name, predicate in (
                ('gray building', lambda r,g,b: 130 < r < 200 and r == g == b),
                ('red building', lambda r,g,b: r > 120 and g < 20 and b < 20),
                ('cyan building', lambda r,g,b: r < 20 and g > 120 and b > 120),
                ('yellow windows', lambda r,g,b: r > 200 and g > 200 and b < 120),
            ):
                assert any(predicate(*rgb) for rgb in colors), (name, colors)
            if p.b('vga'):
                assert any(r > 200 and 120 < g < 210 and 25 < b < 110
                           for r,g,b in colors), ('original orange is missing', colors)
            else:
                assert (0, 0, 0) in colors, 'desktop black was recolored on return'

    else:
        w, h, rows = m.vram()
        M.write_png(str(path), w, h, rows)
        ox, oy, sx, sy = (p.w(n) for n in ('ox', 'oy', 'sx', 'sy'))
        scene = [v for row in rows[oy + 24*sy:oy + 128*sy]
                 for v in row[ox:ox + 256*sx]]
        assert 0 < sum(scene) < len(scene), 'blank monochrome scene'
        hud = [v for row in rows[oy + 8*sy:oy + 16*sy]
               for v in row[ox:ox + 256*sx]]
        assert sum(hud) > 20, 'monochrome aiming/status text disappeared'



def arm(tag, off, disk, out):
    with os88ui.boot(os88build.at('build/os8088-360.img'), apps=str(disk),
                     machine=ARMS[tag]) as ui:
        m = ui.m
        ui.open_drive('B')
        ui.open('GORILLAS.O88')
        ui.settle()
        p = Probe(ui, off)
        m.gorillas_probe = p
        assert p.b('spawned') == 1
        assert p.w('angle') == 45 and p.w('power') == 70
        capture(m, p, out / (tag + '-window.png'))

        # Editing bounds, field selection, backspace and per-field replacement.
        key(m, 'Digit9'); key(m, 'Digit0')
        assert p.w('angle') == 90
        key(m, 'Digit9')                 # 909 rejected
        assert p.w('angle') == 90
        key(m, 'Backspace')
        assert p.w('angle') == 9
        key(m, 'Enter')
        assert p.b('field') == 1
        key(m, 'Digit1'); key(m, 'Digit5'); key(m, 'Digit0')
        assert p.w('power') == 150
        key(m, 'Digit1')                 # 1501 rejected
        assert p.w('power') == 150
        key(m, 'KeyG')
        assert p.b('gravidx') == 1
        key(m, 'KeyN')
        assert p.b('gravidx') == 0 and p.b('turn') == 0

        # Near-horizontal throw into terrain: persistent hole, turn changes.
        key(m, 'Digit0'); key(m, 'Enter')
        key(m, 'Digit3'); key(m, 'Digit0')
        before = p.data('scene', 16384)
        key(m, 'Enter')
        wait(m, lambda: p.b('state') != 1, 'terrain impact')
        after = p.data('scene', 16384)
        assert p.b('turn') == 1 and p.b('state') == 0
        erased = any(((a >> shift) & 15) and not ((b >> shift) & 15)
                     for a, b in zip(before[42*128:], after[42*128:])
                     for shift in (0, 4))
        assert erased, 'impact did not destroy terrain'

        # Pause is sticky: launch vertically with sufficient airtime.
        key(m, 'KeyN'); key(m, 'Digit9'); key(m, 'Digit0')
        key(m, 'Enter'); key(m, 'Digit1'); key(m, 'Digit5'); key(m, 'Digit0')
        key(m, 'Enter'); key(m, 'KeyP')
        assert p.b('state') == 1 and p.b('paused') == 1
        paused = p.data('px', 8)
        M.pace(m, .5)
        assert p.data('px', 8) == paused, 'paused projectile moved'
        key(m, 'Enter')
        wait(m, lambda: p.data('px', 8) != paused, 'Enter resumes paused flight')
        key(m, 'KeyN')

        # Actual keyboard throws, no poked scores/state: a power-one throw
        # returns onto the thrower. The OTHER player must receive the point.
        for round_no in range(5):
            thrower = p.b('turn')
            scores = list(p.data('scores', 2))
            key(m, 'Enter'); key(m, 'Digit1'); key(m, 'Enter')
            wait(m, lambda: p.b('state') in (2, 3), 'self-hit and score')
            scores[thrower ^ 1] += 1
            assert list(p.data('scores', 2)) == scores
            assert p.b('winner') == thrower ^ 1
            assert p.b('state') == (3 if max(scores) == 3 else 2)
            if round_no < 4:
                key(m, 'Enter')
                assert p.b('state') == 0
        ui.settle()
        capture(m, p, out / (tag + '-match.png'))
        key(m, 'Enter')
        assert p.data('scores', 2) == b'\0\0'

        # Exclusive mode restores the exact logical terrain; repeated entry
        # catches stale fullscreen flags and framebuffer geometry.
        for attempt in range(2):
            if attempt == 0:
                key(m, 'KeyF')
            else:
                m.alt('Enter')
            wait(m, lambda: p.b('fsready') == 1, 'fullscreen scene ready')
            assert not (m.read(0x417, 1)[0] & 8), 'Alt release lost across mode switch'
            assert p.b('cga') == (tag == 'cga')
            if tag == 'cga':
                assert p.b('depth') == 2
            # Play inside the bracket too: its own loop must advance a shot.
            key(m, 'Enter'); key(m, 'Digit1'); key(m, 'Enter')
            wait(m, lambda: p.b('state') == 2, 'fullscreen hit')
            M.pace(m, .3)
            capture(m, p, out / (tag + '-full.png'))
            scene = p.data('scene', 16384)
            if attempt == 0:
                key(m, 'Escape')
            else:
                m.alt('Enter', hold=.15)
            wait(m, lambda: p.b('fs') == 0, 'fullscreen exit')
            ui.settle()
            assert p.b('cga') == 0
            assert p.data('scene', 16384) == scene
            key(m, 'Enter')
        capture(m, p, out / (tag + '-restored.png'))
        ui.close(ui.window('Gorillas'))
        print('PASS', tag, 'input, terrain, pause, five rounds, fullscreen/restore, close', flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--arm', choices=ARMS)
    ap.add_argument('--capture', type=Path, default=ROOT / 'build/gorillas-proof')
    args = ap.parse_args()
    args.capture.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    subprocess.run([sys.executable, 'tools/gorillas_art.py', '--check'],
                   cwd=ROOT, check=True)
    off = offsets()
    with tempfile.TemporaryDirectory() as td:
        disk = Path(td) / 'gorillas.img'
        subprocess.run([sys.executable, 'tools/os88disk.py', '-o', str(disk),
                        '--size', '360', os88build.at('build/gorillas.o88')],
                       cwd=ROOT, check=True)
        for tag in ([args.arm] if args.arm else ARMS):
            arm(tag, off, disk, args.capture)
    print('Gorillas: %.1fs' % (time.monotonic() - started))


if __name__ == '__main__':
    main()
