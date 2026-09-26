#!/usr/bin/env python3
"""Guest gate for the splash, setup, dance, sound scheduling and solo opponent."""
import argparse
from pathlib import Path
import subprocess
import tempfile

import gorillas as G


def screenshot(m, p, path):
    G.M.pace(m, .2)
    if m.video()['type'] == 'vga' or p.b('cga'):
        w, h, pixels = m.fbuf(0)
        G.M.write_png_rgb(str(path), w, h, pixels)
    else:
        w, h, pixels = m.vram()
        G.M.write_png(str(path), w, h, pixels)


def type_text(m, text):
    for ch in text:
        key = ('Digit' + ch if ch.isdigit() else 'Period' if ch == '.'
               else 'Key' + ch.upper())
        G.key(m, key)


def arm(tag, off, disk, out):
    with G.os88ui.boot(G.os88build.at('build/os8088-360.img'), apps=str(disk),
                      machine=G.ARMS[tag]) as ui:
        m = ui.m
        ui.open_drive('B')
        ui.open('GORILLAS.O88')
        p = G.Probe(ui, off)
        m.gorillas_probe = p
        assert p.b('state') == 4
        G.wait(m, lambda: p.b('pose') == 1, 'splash gorillas drawn')
        first = m.read(0xb0000 if tag == 'herc' else
                       0xb8000 if tag == 'cga' else 0xa0000, 32768)
        G.wait(m, lambda: p.b('pose') == 0, 'splash gorillas change pose')
        second = m.read(0xb0000 if tag == 'herc' else
                        0xb8000 if tag == 'cga' else 0xa0000, 32768)
        assert first != second, 'intro did not animate'
        screenshot(m, p, out / (tag + '-splash.png'))
        assert p.b('introseq') >= 1 and p.w('musicptr')
        G.wait(m, lambda: p.b('introseq') == 2, 'startup dance song begins')
        G.wait(m, lambda: p.b('state') == 5, 'musical startup ends in setup')
        G.M.ui_done(m)
        assert p.b('state') == 5 and p.b('setupfield') == 0
        assert p.w('musicptr') == 0
        type_text(m, '0'); G.key(m, 'Enter')
        assert p.b('setupfield') == 0, 'zero players accepted'
        G.key(m, 'Backspace'); type_text(m, '1'); G.key(m, 'Enter')
        # Letters previously bound to fullscreen, new game and pause are names.
        type_text(m, 'fnpabcdefghijk')
        assert p.b('inputlen') == 10 and p.b('fs') == 0
        G.key(m, 'Backspace'); type_text(m, 'z'); G.key(m, 'Enter')
        assert p.data('name1', 11) == b'fnpabcdefz\0'
        G.key(m, 'Enter')
        assert p.data('name2', 9) == b'Computer\0'
        type_text(m, '100'); G.key(m, 'Enter')
        assert p.b('setupfield') == 3, '100 points accepted'
        for _ in range(3):
            G.key(m, 'Backspace')
        type_text(m, '1'); G.key(m, 'Enter')
        type_text(m, '1.2.3'); G.key(m, 'Enter')
        assert p.b('setupfield') == 4, 'malformed gravity accepted'
        for _ in range(5):
            G.key(m, 'Backspace')
        type_text(m, '16.2')
        screenshot(m, p, out / (tag + '-setup.png'))
        # Fullscreen preserves the partly typed setup; Escape restores it.
        m.alt('Enter')
        G.wait(m, lambda: p.b('fsready'), 'fullscreen setup')
        assert p.data('input', 5) == b'16.2\0'
        G.key(m, 'Escape')
        G.wait(m, lambda: not p.b('fs'), 'windowed setup')
        G.key(m, 'Enter')
        assert p.b('state') == 6 and p.w('grav10') == 162
        assert p.b('players') == 1 and p.b('target') == 1
        G.key(m, 'KeyV')
        G.wait(m, lambda: p.b('state') == 7, 'intro dance')
        music = p.w('musicptr')
        assert music
        G.wait(m, lambda: p.w('musicptr') != music, 'next original tune note')
        screenshot(m, p, out / (tag + '-dance.png'))
        G.key(m, 'Space')
        G.wait(m, lambda: p.b('state') == 0, 'skip dance into game')
        assert p.w('musicptr') == 0
        if tag == 'vga':
            m.alt('Enter')
            G.wait(m, lambda: p.b('fsready'), 'solo fullscreen game')
        G.key(m, 'Digit0'); G.key(m, 'Enter')
        G.key(m, 'Digit3'); G.key(m, 'Digit0'); G.key(m, 'Enter')
        G.wait(m, lambda: p.b('turn') == 1, 'human throw ends')
        G.key(m, 'KeyP')
        assert p.b('paused')
        aim = p.w('aipower')
        G.M.pace(m, .4)
        assert p.w('aipower') == aim, 'paused computer kept aiming'
        G.key(m, 'KeyP')
        G.wait(m, lambda: p.b('state') == 1, 'computer launches its own throw')
        assert p.b('turn') == 1 and p.w('angle') == 55
        G.wait(m, lambda: p.b('state') != 1, 'computer throw lands')
        assert p.b('turn') == 0 or p.b('state') == 3
        if p.b('fs'):
            G.key(m, 'Escape')
            G.wait(m, lambda: not p.b('fs'), 'solo fullscreen exit')
        # Defaults, fractional gravity and complete (unskipped) dance.
        G.key(m, 'KeyN')
        for _ in range(4):
            G.key(m, 'Enter')
        type_text(m, '.1'); G.key(m, 'Enter')
        assert p.b('players') == 2 and p.b('target') == 3
        assert p.w('grav10') == 1
        G.key(m, 'KeyV')
        G.wait(m, lambda: p.b('state') == 0, 'dance completes automatically')
        assert p.w('grav10') == 1, 'new skyline reset chosen gravity'
        G.key(m, 'Digit9'); G.key(m, 'Digit0'); G.key(m, 'Enter'); G.key(m, 'Enter')
        G.wait(m, lambda: p.w('age') >= 30, 'fractional-gravity flight')
        G.key(m, 'KeyP')
        assert p.b('state') == 1 and p.b('paused')
        assert p.w('vy') == (-224 + p.w('age')*4//98) & 65535
        ui.close(ui.window('Gorillas'))
        print('PASS', tag, 'splash, setup validation, names, music, dance, solo, fullscreen', flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--arm', choices=G.ARMS)
    args = ap.parse_args()
    out = G.ROOT / 'build/gorillas-proof'
    out.mkdir(exist_ok=True)
    subprocess.run(['python3', 'tools/gorillas_music.py', '--check'], cwd=G.ROOT, check=True)
    off = G.offsets()
    with tempfile.TemporaryDirectory() as td:
        disk = Path(td) / 'gorillas.img'
        subprocess.run(['python3', 'tools/os88disk.py', '-o', str(disk), '--size', '360',
                        G.os88build.at('build/gorillas.o88')], cwd=G.ROOT, check=True)
        for tag in ([args.arm] if args.arm else G.ARMS):
            arm(tag, off, disk, out)


if __name__ == '__main__':
    main()
