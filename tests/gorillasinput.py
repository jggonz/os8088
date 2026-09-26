#!/usr/bin/env python3
"""Measure real Gorillas keystroke handlers in MartyPC guest cycles.

Run after `make gorillas`. Includes interrupts and drawing, from gr_key entry
through its return; excludes keyboard delivery and the fullscreen tick wait.
Use --output to retain a before/after JSON report. No guest instrumentation.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import time

import gorillas as G


def code_offsets():
    source = (G.ROOT / 'apps/gorillas/gorillas.asm').read_text()
    names = ('gr_key', 'gr_tick', 'gr_fullpaint', 'gr_status', 'gr_prompt', 'gr_help',
             'gr_pausemsg', 'gr_roundmsg', 'gr_matchmsg')
    source = source.replace('OS88_IMAGE_END', '')
    source += '\n' + '\n'.join('dw ' + n for n in names) + '\nOS88_IMAGE_END\n'
    with tempfile.TemporaryDirectory() as td:
        asm, binary = Path(td) / 'probe.asm', Path(td) / 'probe.bin'
        asm.write_text(source)
        subprocess.run(['nasm', '-f', 'bin', '-I', 'apps/', '-I', 'apps/gorillas/',
                        '-o', str(binary), str(asm)], cwd=G.ROOT, check=True)
        return dict(zip(names, struct.unpack('<%dH' % len(names),
                                            binary.read_bytes()[-2*len(names):])))


def check_hud(m, p, code):
    """Compare scene pixels to the OS font, including erased old glyphs."""
    if p.b('state') == 1:
        expected = bytearray(24*128)
        if p.b('saved'):
            x, y = p.w('px') >> 6, p.w('by')
            tip = x + 2 if p.w('age') & 2 else x
            for px, py in ((x, y), (x+1, y), (x+1, y+1), (tip, y+2)):
                if py < 24:
                    expected[py*128 + px//2] |= 1 << (0 if px & 1 else 4)
        assert p.data('scene', len(expected)) == expected, \
            'flight sky contains text or an incorrect banana'
        return
    first = p.b('fontfirst')
    font = m.read((p.w('fontseg') << 4) + p.w('fontoff'), (127-first)*8)
    expected = bytearray(24*128)
    prompt = ('gr_pausemsg' if p.b('paused') else
              {0: 'gr_prompt', 2: 'gr_roundmsg', 3: 'gr_matchmsg'}[p.b('state')])
    for row, name in enumerate(('gr_status', prompt, 'gr_help')):
        line = m.read(p.base + code[name], 32).split(b'\0')[0]
        ink = 9 if row == 1 else 15
        for col, ch in enumerate(line):
            for y, bits in enumerate(font[(ch-first)*8:(ch-first+1)*8]):
                for x in range(8):
                    if bits & (128 >> x):
                        at = (row*8+y)*128 + col*4 + x//2
                        expected[at] |= ink << (0 if x & 1 else 4)
    assert p.data('scene', len(expected)) == expected, 'HUD differs from OS glyphs'


def video_bytes(m, tag):
    if tag != 'vga':
        return m.read(0xb0000 if tag == 'herc' else 0xb8000, 32768)
    # Read each VGA plane under Read Map Select, then restore the controller.
    index = m.inb(0x3ce)
    m.outb(0x3ce, 4)
    selected = m.inb(0x3cf)
    planes = []
    for plane in range(4):
        m.outb(0x3cf, plane)
        planes.append(m.read(0xa0000, 38400))
    m.outb(0x3cf, selected)
    m.outb(0x3ce, index)
    return b''.join(planes)


def check_repaint(m, p, code, tag):
    """At the handler's return, still under its drawing lock, call fullpaint.

    Compare actual video memory, not just the scene backing it. Save/restore
    CPU registers around this untimed debugger call; park flushes prefetch.
    """
    before = video_bytes(m, tag)
    saved = m.regs()
    registers = ('ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'sp',
                 'ss', 'ds', 'es', 'flags')
    m.cmd(cmd='park', cs=saved['cs'], ip=code['gr_fullpaint'])
    for reg in registers:
        m.setreg(reg, saved[reg])
    sp = (saved['sp'] - 2) & 65535
    m.setreg('sp', sp)
    m.write((saved['ss'] << 4) + sp, struct.pack('<H', saved['ip']))
    m.bp_exec((saved['cs'] << 4) + saved['ip'])
    m.run()
    assert m.wait_stop(30) == 'breakpoint', 'full repaint never returned'
    after = video_bytes(m, tag)
    assert after == before, ('incremental display differs from full repaint', tag,
                             sum(a != b for a, b in zip(before, after)))
    for reg in registers:
        m.setreg(reg, saved[reg])


def measure(m, p, code, name, tag, repaint):
    m.pause()
    m.bp_exec(p.base + code['gr_key'])
    m.key(name)
    m.run()
    assert m.wait_stop(30) == 'breakpoint', 'key handler never entered'
    r = m.regs()
    ret = struct.unpack('<H', m.readseg(r['ss'], r['sp'], 2))[0]
    start = m.status()['cycles']
    m.bp_exec((r['cs'] << 4) + ret)
    m.run()
    assert m.wait_stop(30) == 'breakpoint', 'key handler never returned'
    cycles = m.status()['cycles'] - start
    check_hud(m, p, code)
    if repaint:
        check_repaint(m, p, code, tag)
    m.bp_exec()
    m.run()
    G.M.pace(m, .08)
    if not p.b('fs') and p.b('state') != 1:
        G.M.ui_done(m)
    return cycles


def flight_hud(m, p, code, tag, repaint):
    """Follow a real throw into the former text rows at frame boundaries."""
    G.key(m, 'KeyN')
    G.key(m, 'Digit9'); G.key(m, 'Digit0'); G.key(m, 'Enter')
    measure(m, p, code, 'Enter', tag, repaint)
    m.pause()
    for _ in range(120):
        m.bp_exec(p.base + code['gr_tick'])
        m.run()
        assert m.wait_stop(30) == 'breakpoint', 'flight tick never entered'
        r = m.regs()
        ret = struct.unpack('<H', m.readseg(r['ss'], r['sp'], 2))[0]
        m.bp_exec((r['cs'] << 4) + ret)
        m.run()
        assert m.wait_stop(30) == 'breakpoint', 'flight tick never returned'
        assert p.b('state') == 1, 'shot ended before reaching the text rows'
        check_hud(m, p, code)
        if p.b('saved') and p.w('by') < 24:
            if repaint:
                check_repaint(m, p, code, tag)
            break
    else:
        raise AssertionError('banana never appeared in the text rows')
    m.bp_exec()
    m.run()
    measure(m, p, code, 'KeyP', tag, repaint)
    assert p.b('paused') == 1
    measure(m, p, code, 'KeyP', tag, repaint)
    G.wait(m, lambda: p.b('state') != 1, 'shot ends and text returns')
    G.M.pace(m, .3)
    m.pause()
    check_hud(m, p, code)
    m.run()


def queued_input(m, p, code):
    """Six already-buffered BIOS keys must not pay six fullscreen tick waits."""
    G.key(m, 'KeyN')
    m.pause()
    m.write(0x41a, struct.pack('<HH', 0x1e, 0x2a))
    m.write(0x41e, struct.pack('<6H', 0x0a39, 0x0b30, 0x0f09,
                             0x0231, 0x0635, 0x0b30))  # 90<Tab>150
    m.bp_exec(p.base + code['gr_key'])
    for i in range(6):
        m.run()
        assert m.wait_stop(30) == 'breakpoint', 'buffered key lost'
        if i == 0:
            start = m.status()['cycles']
    r = m.regs()
    ret = struct.unpack('<H', m.readseg(r['ss'], r['sp'], 2))[0]
    m.bp_exec((r['cs'] << 4) + ret)
    m.run()
    assert m.wait_stop(30) == 'breakpoint'
    ms = (m.status()['cycles'] - start)/G.M.GUEST_HZ*1000
    assert p.w('angle') == 90 and p.w('power') == 150
    assert p.b('field') == 1 and p.b('state') == 0
    check_hud(m, p, code)
    assert ms < 55, ('buffered input waited for frame ticks', ms)
    m.bp_exec()
    m.run()
    return ms


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--arm', choices=G.ARMS)
    ap.add_argument('--output', type=Path)
    ap.add_argument('--check-repaint', action='store_true',
                    help='compare incremental VRAM against a full repaint after every key')
    ap.add_argument('--max-input-ms', type=float, default=20,
                    help='digit/edit/selection handler budget; 0 disables for baseline runs')
    args = ap.parse_args()
    started = time.monotonic()
    off, code, report = G.offsets(), code_offsets(), {}
    keys = ('Digit9', 'Digit0', 'Digit9', 'Backspace', 'Enter',
            'Digit1', 'Digit5', 'Digit0', 'ArrowDown', 'ArrowUp',
            'Tab', 'KeyG', 'KeyP', 'KeyP',
            # Hidden edits must survive resume; zero velocity cannot launch.
            'KeyP', 'Digit1', 'Backspace', 'Tab', 'Digit0', 'Enter',
            'Enter', 'Backspace', 'ArrowDown', 'ArrowUp', 'Tab',
            'Digit1', 'Digit8', 'Digit0', 'Digit1', 'ArrowUp')
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
                ui.settle()
                p = G.Probe(ui, off)
                m.gorillas_probe = p
                for mode in ('window', 'full'):
                    if mode == 'full':
                        G.key(m, 'KeyN')
                        G.key(m, 'KeyF')
                        G.wait(m, lambda: p.b('fsready'), 'fullscreen ready')
                    terrain = p.data('scene', 16384)[24*128:]
                    values = [measure(m, p, code, name, tag, args.check_repaint)
                              for name in keys]
                    assert p.data('scene', 16384)[24*128:] == terrain
                    assert p.w('angle') == 180 and p.w('power') == 1
                    assert p.b('state') == 0 and p.b('paused') == 0
                    assert p.b('field') == 0 and p.b('gravidx') == 1
                    label = tag + '-' + mode
                    report[label] = list(zip(keys, values))
                    print(label, ' '.join('%s=%.2fms' % (k, c/G.M.GUEST_HZ*1000)
                                         for k, c in zip(keys, values)), flush=True)
                    if args.max_input_ms:
                        for i, (name, cycles) in enumerate(zip(keys, values)):
                            # The first Enter selects velocity; later Enter
                            # may replace the entire pause message on resume.
                            if name not in ('KeyG', 'KeyP', 'Enter') or i == 4:
                                assert cycles/G.M.GUEST_HZ*1000 < args.max_input_ms, \
                                    ('input latency budget exceeded', label, name, cycles)
                    if mode == 'full' and args.max_input_ms:
                        print(label, 'buffered 90<Tab>150: %.2fms' %
                              queued_input(m, p, code), flush=True)
                    flight_hud(m, p, code, tag, args.check_repaint)
                    print(label, 'PASS hidden flight text, banana in text rows, '
                          'pause/resume and restored text', flush=True)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print('Gorillas input: %.1fs' % (time.monotonic() - started))


if __name__ == '__main__':
    main()
