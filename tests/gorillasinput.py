#!/usr/bin/env python3
"""Measure real Gorillas keystroke handlers in MartyPC guest cycles.

Run after `make gorillas`. Includes interrupts and drawing, from gr_key entry
through its return; excludes keyboard delivery and the fullscreen tick wait.
Use --output to retain a before/after JSON report. No guest instrumentation.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time

import gorillas as G


def code_offsets(extra=()):
    source = (G.ROOT / 'apps/gorillas/gorillas.asm').read_text()
    names = ('gr_key', 'gr_tick', 'gr_fullpaint', 'gr_status', 'gr_prompt', 'gr_velocity',
             'gr_pausemsg', 'gr_roundmsg', 'gr_matchmsg', 'gr_animate',
             'gr_apeleft', 'gr_aperight') + tuple(extra)
    # Symbol-only probe: its appended table is not part of the shipped image.
    source = source.replace('OS88_IMAGE_END', '').replace('OS88_BSS GR_BSS', 'OS88_BSS 0')
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
    wind = struct.unpack('<h', p.data('wind', 2))[0]
    active = p.data('name2' if p.b('turn') else 'name1', 11).split(b'\0')[0]
    header = active.ljust(23) + ('Wind %+04d' % wind).encode()
    assert m.read(p.base + code['gr_status'], 33) == header + b'\0'
    velocity = 'gr_velocity' if p.b('state') == 0 and not p.b('paused') else None
    for row, name in enumerate(('gr_status', prompt, velocity)):
        line = m.read(p.base + code[name], 33).split(b'\0')[0] if name else b''
        if name in ('gr_prompt', 'gr_velocity'):
            selected = p.b('field') == row - 1
            label, value = ('Angle', p.w('angle')) if row == 1 else ('Velocity', p.w('power'))
            assert line == (('>' if selected else ' ') + '%s: %03d' % (label, value)).encode()
        ink = 15 if row == 0 else 9
        for col, ch in enumerate(line):
            for y, bits in enumerate(font[(ch-first)*8:(ch-first+1)*8]):
                for x in range(8):
                    if bits & (128 >> x):
                        at = (row*8+y)*128 + col*4 + x//2
                        expected[at] |= ink << (0 if x & 1 else 4)
    assert p.data('scene', len(expected)) == expected, 'HUD differs from OS glyphs'


def video_bytes(m, tag):
    if tag != 'vga':
        # CGA has 16 KB; the next 16 KB aliases it on this card model.
        return m.read(0xb0000, 32768) if tag == 'herc' else m.read(0xb8000, 16384)
    # MartyPC's side-effect-free VGA peek hardcodes plane 0. Actual guest
    # MOVSB reads honor Read Map Select; use a temporary stub and restore all
    # registers/scratch bytes. The caller is paused under the graphics lock.
    p = m.gorillas_probe
    stub, scratch = p.offsets['gr_under'], p.offsets['gr_band']
    source = (G.ROOT / 'apps/gorillas/gorillas.asm').read_text()
    scratch_size = int(re.search(r'^VAR gr_band, (\d+)$', source, re.M)[1])
    saved = m.regs()
    original_stub = m.read(p.base + stub, 9)
    original_scratch = m.read(p.base + scratch, scratch_size)
    m.write(p.base + stub, bytes.fromhex('fa b8 00 a0 8e d8 f3 a4 90'))
    index = m.inb(0x3ce)
    m.outb(0x3ce, 5)
    mode = m.inb(0x3cf)
    m.outb(0x3cf, mode & ~8)
    m.outb(0x3ce, 4)
    selected = m.inb(0x3cf)
    planes = []
    for plane in range(4):
        m.outb(0x3cf, plane)
        for offset in range(0, 38400, scratch_size):
            count = min(scratch_size, 38400-offset)
            m.cmd(cmd='park', cs=p.base >> 4, ip=stub)
            for reg, value in (('es', p.base >> 4), ('si', offset),
                               ('di', scratch), ('cx', count),
                               ('flags', saved['flags'] & ~0x600)):
                m.setreg(reg, value)
            m.bp_exec(p.base + stub + 8)
            m.run()
            assert m.wait_stop(30) == 'breakpoint', 'VGA readback stub hung'
            planes.append(m.read(p.base + scratch, count))
    m.outb(0x3cf, selected)
    m.outb(0x3ce, 5)
    m.outb(0x3cf, mode)
    m.outb(0x3ce, index)
    m.write(p.base + stub, original_stub)
    m.write(p.base + scratch, original_scratch)
    m.cmd(cmd='park', cs=saved['cs'], ip=saved['ip'])
    for reg in ('ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'sp',
                'ss', 'ds', 'es', 'flags'):
        m.setreg(reg, saved[reg])
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
                             sum(a != b for a, b in zip(before, after)),
                             [(i, a, b) for i, (a, b) in enumerate(zip(before, after))
                              if a != b][:12])
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
    G.new_match(m)
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
    G.new_match(m)
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
                G.M.pace(m, .3)
                p = G.Probe(ui, off)
                m.gorillas_probe = p
                G.setup(m)
                for mode in ('window', 'full'):
                    if mode == 'full':
                        G.new_match(m)
                        G.key(m, 'KeyF')
                        G.wait(m, lambda: p.b('fsready'), 'fullscreen ready')
                    terrain = p.data('scene', 16384)[24*128:]
                    values = [measure(m, p, code, name, tag, args.check_repaint)
                              for name in keys]
                    assert p.data('scene', 16384)[24*128:] == terrain
                    assert p.w('angle') == 180 and p.w('power') == 1
                    assert p.b('state') == 0 and p.b('paused') == 0
                    assert p.b('field') == 0 and p.w('grav10') == 98
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
