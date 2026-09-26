#!/usr/bin/env python3
"""Measure setup text on a 4.77 MHz guest and compare it to a full repaint."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile

import gorillas as G
import gorillasinput as I


def animation(m, p, code, tag):
    """Time an actual frame, then compare VRAM to the original scene renderer."""
    m.pause()
    m.bp_exec(p.base + code['gr_animate'])
    m.run()
    assert m.wait_stop(30) == 'breakpoint'
    # Ensure this tick is due; preserve real interrupt/drawing costs.
    m.write(p.base + p.offsets['gr_animdue'], m.read(m.sym('ticks'), 2))
    r = m.regs()
    ret = struct.unpack('<H', m.readseg(r['ss'], r['sp'], 2))[0]
    start = m.status()['cycles']
    m.bp_exec((r['cs'] << 4) + ret)
    m.run()
    assert m.wait_stop(30) == 'breakpoint'
    ms = (m.status()['cycles'] - start) / G.M.GUEST_HZ * 1000
    scene = p.data('scene', 16384)
    reference = bytearray(scene)
    def pixel(x, y, ink):
        at, shift = y*128+x//2, 0 if x & 1 else 4
        reference[at] = (reference[at] & ~(15 << shift)) | ink << shift
    def spark(x, y):
        for dx, dy in ((1, 0), (0, 1), (1, 1), (2, 1), (1, 2)):
            pixel(x+dx, y+dy, 6)
    phase = p.b('animphase')
    for x in range(phase*4, 253, 20):
        spark(x, 0)
        spark(252-x, 125)
    for y in range(phase*4+5, 123, 20):
        spark(0, y)
        spark(253, 125-y)
    for ape, x in enumerate((80, 160)):
        label = 'gr_aperight' if (ape ^ p.b('pose')) & 1 else 'gr_apeleft'
        raw = m.read(p.base + code[label], 160)
        for y in range(20):
            for col in range(16):
                ink = (raw[y*8+col//2] >> (0 if col & 1 else 4)) & 15
                pixel(x+col, 80+y, ink)
    state, music = p.b('state'), p.w('musicptr')
    m.write(p.base + p.offsets['gr_scene'], reference)
    m.write(p.base + p.offsets['gr_state'], b'\0')
    m.write(p.base + p.offsets['gr_musicptr'], b'\0\0')
    I.check_repaint(m, p, code, tag)
    m.write(p.base + p.offsets['gr_scene'], scene)
    m.write(p.base + p.offsets['gr_state'], bytes([state]))
    m.write(p.base + p.offsets['gr_musicptr'], struct.pack('<H', music))
    m.bp_exec()
    m.run()
    return ms


def measure(m, p, code, key, tag):
    m.pause()
    m.bp_exec(p.base + code['gr_key'])
    m.key(key)
    m.run()
    assert m.wait_stop(30) == 'breakpoint'
    r = m.regs()
    ret = struct.unpack('<H', m.readseg(r['ss'], r['sp'], 2))[0]
    start = m.status()['cycles']
    m.bp_exec((r['cs'] << 4) + ret)
    m.run()
    assert m.wait_stop(30) == 'breakpoint'
    ms = (m.status()['cycles'] - start) / G.M.GUEST_HZ * 1000
    # All setup pixels must match the cached glyphs, including erased errors.
    first = p.b('fontfirst')
    font = m.read((p.w('fontseg') << 4) + p.w('fontoff'), (127-first)*8)
    expected = bytearray(16384)
    for cell, ch in enumerate(p.data('hudchars', 512)):
        if ch < first:
            continue
        for y, bits in enumerate(font[(ch-first)*8:(ch-first+1)*8]):
            for x in range(8):
                if bits & (128 >> x):
                    at = (cell//32*8+y)*128 + cell%32*4 + x//2
                    expected[at] |= 15 << (0 if x & 1 else 4)
    assert p.data('scene', 16384) == expected, 'setup differs from font'
    I.check_repaint(m, p, code, tag)
    m.bp_exec()
    m.run()
    G.M.pace(m, .08)
    if not p.b('fs'):
        G.M.ui_done(m)
    return ms


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--arm', choices=G.ARMS)
    ap.add_argument('--output', type=Path)
    ap.add_argument('--max-transition-ms', type=float, default=150)
    args = ap.parse_args()
    off, code, report = G.offsets(), I.code_offsets(), {}
    with tempfile.TemporaryDirectory() as td:
        disk = Path(td) / 'gorillas.img'
        subprocess.run(['python3', 'tools/os88disk.py', '-o', str(disk),
                        '--size', '360', G.os88build.at('build/gorillas.o88')],
                       cwd=G.ROOT, check=True)
        for tag in ([args.arm] if args.arm else G.ARMS):
            with G.os88ui.boot(G.os88build.at('build/os8088-360.img'),
                              apps=str(disk), machine=G.ARMS[tag]) as ui:
                m = ui.m
                ui.open_drive('B')
                ui.open('GORILLAS.O88')
                p = G.Probe(ui, off)
                m.gorillas_probe = p
                for mode in ('window', 'full'):
                    if mode == 'full':
                        m.alt('Enter')
                        G.wait(m, lambda: p.b('fsready'), 'fullscreen intro')
                    frames = [animation(m, p, code, tag) for _ in range(5)]
                    print(tag, mode, 'animation ms:', frames, flush=True)
                    report[tag + '-' + mode + '-animation'] = frames
                    assert max(frames) < 75, ('animation exceeds frame budget', frames)
                G.key(m, 'Escape')
                G.wait(m, lambda: not p.b('fs'), 'intro fullscreen exit')
                G.key(m, 'Space')
                G.wait(m, lambda: p.b('state') == 5, 'setup')
                for mode in ('window', 'full'):
                    if mode == 'full':
                        # Start setup again through the actual gameplay key.
                        G.key(m, 'KeyP')
                        G.key(m, 'KeyN')
                        m.alt('Enter')
                        G.wait(m, lambda: p.b('fsready'), 'fullscreen setup')
                    keys = ('Digit0', 'Enter', 'Backspace', 'Enter',
                            'KeyA', 'Backspace', 'Enter', 'Enter',
                            'Enter', 'Enter')
                    values = [measure(m, p, code, key, tag) for key in keys]
                    assert p.b('state') == 6 and p.w('grav10') == 98
                    label = tag + '-' + mode
                    report[label] = list(zip(keys, values))
                    print(label, ' '.join('%s=%.2fms' % pair
                                         for pair in zip(keys, values)), flush=True)
                    if args.max_transition_ms:
                        assert max(values) < args.max_transition_ms, (label, values)
                        assert max(values[i] for i in (0, 4, 5)) < 20, (label, values)
                G.key(m, 'Escape')
                G.wait(m, lambda: not p.b('fs'), 'fullscreen exit')
                ui.close(ui.window('Gorillas'))
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
