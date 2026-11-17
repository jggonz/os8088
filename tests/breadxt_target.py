#!/usr/bin/env python3
"""Actual 8088 numeric ABI, bridge regressions and stack/cycle evidence.

Needs breadxtcheckdisk. Host-double tests cannot stand in for this row.
All input goes through os88ui; all completion gates are guest state.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import os88geom
import os88marty as M
import os88sym
import os88ui


def symbols(name, source):
    """Ask NASM for the current linked offsets; verify identical image bytes."""
    with tempfile.TemporaryDirectory(prefix='breadxt-symbols-') as tmp:
        mp, binary = Path(tmp)/'map', Path(tmp)/'image'
        subprocess.run(['nasm', '-f', 'bin', '-w+error', '-I', 'apps/',
                        '-I', 'build/', '--before', f'[map all {mp}]',
                        '-o', str(binary), source], cwd=ROOT, check=True)
        assert binary.read_bytes() == (ROOT/f'build/{name}.bin').read_bytes()
        result = {}
        for line in mp.read_text().splitlines():
            match = re.match(r'\s*([0-9A-F]+)\s+([0-9A-F]+)\s+(\w+)\s*$', line)
            if match:
                result[match[3]] = int(match[1], 16)
        return result


def water_seed(ui):
    eq = os88sym.equates(())
    for _ in range(40):
        ui.m.pause()
        cur = ui.m.read(ui._S('sch_cur'), 1)[0]
        task = ui.m.read(ui._S('sch_tasks'), 8)
        sp = int.from_bytes(task[2:4], 'little')
        if cur != 0 and task[0] == 2 and eq['STK0_TOP']-64 < sp <= eq['STK0_TOP']:
            lo = eq['LOW_SEG']*16 + eq['KLOW_SIZE'] + 2  # preserve canary
            top = eq['LOW_SEG']*16 + eq['STK0_TOP'] + 2
            ui.m.write(lo, bytes([0xcc])*(top-64-lo))
            ui.m.run()
            return lo, top
        ui.m.run()
        ui.still()
    raise AssertionError('UI task did not sleep with a provably shallow frame')


def water_read(ui, water):
    lo, top = water
    raw = ui.m.read(lo, top-64-lo)
    first = next((i for i, byte in enumerate(raw) if byte != 0xcc), len(raw))
    used = top-(lo+first)
    assert used < 512, used
    return used


def screenshot(ui, path):
    ui.still()
    w, h, rgb = ui.m.fbuf()
    M.write_png_rgb(str(path), w, h, rgb)


def scope_cycles(ui, app):
    """Measure the render Pause asks for, entry-to-caller-return, including
    interrupts: the workbench face repaints the scope's section with the run
    state's (SPEC.md 130.6.4), so this is the scope's paint and its peers'."""
    entry = app.p.seg*16 + app.s['_bu_render']
    clocks = []

    def hit(mm, record):
        regs = record['regs']
        if record['addr'] == entry:
            clocks.append(record['cycles'])
            ret = int.from_bytes(mm.read(regs['ss']*16+regs['sp'], 2), 'little')
            mm.breakpoints([{'type': 'exec', 'addr': regs['cs']*16+ret}])
        else:
            clocks.append(record['cycles'])
            mm.breakpoints([])

    with M.bp_trace(ui.m, entry, regs=True, on_hit=hit) as trace:
        ui.menu_pick('Simulate', 'Pause')
        trace.until(lambda: len(clocks) == 2, 'render returned', limit=120)
    assert len(clocks) == 2 and clocks[1] > clocks[0], clocks
    return clocks[1]-clocks[0]


def capacity(offsets):
    started = time.monotonic()
    evidence = {}
    with os88ui.boot('build/os8088-360.img', apps='build/breadxt-check360.img',
                     machine='os8088_xt_vga', quick=True) as ui:
        ui.open_drive('B')
        app = App(ui, offsets)
        app.open('MAX24.BBX', 27)
        c0 = ui.m.status()['cycles']
        ui.menu_pick('Simulate', 'Step')
        ui._wait(lambda: app.word('bu_active') and app.word('bb_snu') == 24,
                 '24 unknowns compiled', 600)
        assert not app.word('bb_valid'), 'capacity solve finished before input probe'
        ci = ui.m.status()['cycles']
        ui.press('F1', done=False)
        ui._wait(lambda: app.word('bu_mode') == 4, 'Help while elimination is pending', 60)
        evidence['pending_solve_help_input_cycles'] = ui.m.status()['cycles']-ci
        assert not app.word('bb_valid'), 'input probe did not overlap calculation'
        ui.press('Escape', done=False)
        ui._wait(lambda: app.word('bu_mode') == 0, 'Help dismissed', 60)
        ui._wait(lambda: app.word('bb_valid') and not app.word('bu_step_once'),
                 '24-node solve committed', 3600)
        evidence['MAX24_step_cycles'] = ui.m.status()['cycles']-c0
        assert app.word('bb_snu') == 24
        for i in range(25):
            current = struct.unpack('<h', app.p.read(offsets['_bb_result']+128+2*(i+2), 2))[0]
            assert current == 20, (i, current)
            assert app.pin(i+2, 2)[0] == 4800-200*i, (i, app.pin(i+2, 2))
        assert struct.unpack('<h', app.p.read(offsets['_bb_result']+128, 2))[0] == -20
        evidence['scope_samples_at_measurement'] = app.word('bb_scope', 2050)
        evidence['pause_render_cycles'] = scope_cycles(ui, app)
        screenshot(ui, ROOT/'build/breadxt-max24-vga.png')
    evidence['elapsed_host_seconds'] = round(time.monotonic()-started, 2)
    (ROOT/'build/breadxt-capacity-evidence.json').write_text(json.dumps(evidence, indent=2)+'\n')
    print(json.dumps(evidence, indent=2), flush=True)
    print('breadxt capacity: actual24 unknowns, every voltage/current, pending Help input PASS')


class App:
    def __init__(self, ui, offsets):
        self.ui, self.s = ui, offsets
        win = ui.open('BREADXT.O88')
        self.p = M.Pkg(ui.m, win)
        ui._wait(lambda: any(m[0] == 'Parts' for m in ui.menus()), 'BreadXT menus', 30)
        ui._wait(lambda: self.word('bu_initialized') and not self.word('bu_op'),
                 'completed application initialization', 30)

    def word(self, name, offset=0):
        return self.p.word(self.s['_'+name]+offset)

    def open(self, name, count):
        self.ui.menu_pick('File', 'Open...')
        self.ui.chooser_open(name)
        self.ui._wait(lambda: self.word('bb_doc', 3072) == count
                      and self.p.read(self.s['_bu_name'], 13).split(b'\0')[0].decode('ascii') == name
                      and not self.word('bu_op') and not self.word('bb_valid')
                      and self.word('bb_recompile'), name+' loaded and stale', 40)

    def pin_hole(self, index, pin):
        data = struct.unpack('<8H', self.p.read(self.s['_bb_doc']+16*index, 16))
        _, typ, a, b, c, _, _, flags = data
        if typ in range(10, 15) or typ == 16:
            n = 8 if typ == 16 else 14
            half = n//2
            row, pos = (7, pin-1) if pin <= half else (6, n-pin)
            if flags & 1:
                row, pos = 13-row, half-1-pos
            return row*30 + a%30 + pos
        if flags & 1 and pin < 3:
            pin = 3-pin
        return (a, b, c)[pin-1]

    def pin(self, index, pin):
        net = self.p.read(self.s['_bb_hnet']+self.pin_hole(index, pin), 1)[0]
        assert net < 64, net
        mv = struct.unpack('<h', self.p.read(self.s['_bb_result']+2*net, 2))[0]
        logic = self.p.read(self.s['_bb_result']+256+net, 1)[0]
        return mv, logic

    def step(self):
        c0 = self.ui.m.status()['cycles']
        self.ui.menu_pick('Simulate', 'Step')
        self.ui._wait(lambda: self.word('bb_valid') and not self.word('bu_op')
                      and not self.word('bu_step_once'), 'committed target step', 600)
        return self.ui.m.status()['cycles']-c0

    def stamp(self):
        return self.word('bb_result', 320) + 65536*self.word('bb_result', 322)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--negative', action='store_true', help='prove target assertion goes red')
    parser.add_argument('--capacity', action='store_true', help='largest analog matrix and XT cycle probe')
    parser.add_argument('--numeric-only', action='store_true', help='numeric and raster ABI fixture only')
    args = parser.parse_args()
    started = time.monotonic()
    bn = symbols('bbnum', 'tests/breadxtnum/bbnum.asm')
    bb = symbols('breadxt', 'apps/breadxt/breadxt.asm')
    if args.capacity:
        capacity(bb)
        return
    evidence = {}
    with os88ui.boot('build/os8088-360.img', apps='build/breadxt-check360.img',
                     machine='os8088_xt_vga', quick=True) as ui:
        water = water_seed(ui)
        ui.open_drive('B')
        win = ui.open('BBNUM.O88')
        p = M.Pkg(ui.m, win)
        ui._wait(lambda: p.word(bn['_bn_done']), 'numeric ABI fixture completion', 90)
        assert (p.word(bn['_bn_pass']), p.word(bn['_bn_fail'])) == (15, 0)
        ui.type('n')
        ui._wait(lambda: p.word(bn['_bn_negative']) and p.word(bn['_bn_done']), 'numeric negative control', 90)
        assert (p.word(bn['_bn_pass']), p.word(bn['_bn_fail'])) == (14, 1)
        if args.negative:
            assert p.word(bn['_bn_fail']) == 0, 'intentional wrong numeric answer rejected'
        ui.type('n')
        ui._wait(lambda: not p.word(bn['_bn_negative']) and p.word(bn['_bn_done']), 'numeric positive restoration', 90)
        assert p.word(bn['_bn_fail']) == 0
        screenshot(ui, ROOT/'build/breadxt-numeric-vga.png')
        ui.close(win)
        if args.numeric_only:
            print('breadxt ABI:13 numeric +2 raster cases PASS; red control rejected and restored')
            return
        app = App(ui, bb)
        for name, input_mv, output in [('HCLOW', 1250, (5000, 1)),
                                        ('HCMID', 2500, (-2, 2)),
                                        ('HCHIGH', 3750, (0, 0))]:
            app.open(name+'.BBX', 5)
            evidence[name+'_step_cycles'] = app.step()
            assert app.pin(0, 1)[0] == input_mv, (name, app.pin(0, 1))
            assert app.pin(0, 2) == output, (name, app.pin(0, 2))
            print(name, app.pin(0, 1), app.pin(0, 2), flush=True)
        app.open('RESET.BBX', 3)
        app.step()
        assert app.pin(0, 3) == (0, 0) and app.pin(0, 7) == (0, 0)
        assert app.pin(0, 2)[0] == -1 and app.pin(0, 6)[0] == -1
        app.open('FLOATLED.BBX', 3)
        app.step()
        assert app.pin(2, 1)[0] == -1
        assert struct.unpack('<h', app.p.read(bb['_bb_result']+128+4, 2))[0] == 0
        app.open('LEDTEST.BBX', 4)
        evidence['LED_step_cycles'] = app.step()
        currents = struct.unpack('<4h', app.p.read(bb['_bb_result']+128, 8))
        assert currents[0] == -8824 and currents[2:] == (8824, 8824), currents
        app.open('BCLK.BBX', 3)
        ui.menu_pick('Simulate', 'Run')
        ui._wait(lambda: app.stamp() >= 1200, 'channel B clock captured', 1200)
        ui.menu_pick('Simulate', 'Pause')
        ui._wait(lambda: not app.word('bb_running'), 'clock paused', 20)
        count = app.word('bb_scope', 2050)
        assert 4 <= count <= 256, count
        samples = struct.unpack('<256h', app.p.read(bb['_bb_scope']+512, 512))
        undersampled = app.word('bb_scope', 2056)
        assert (0 in samples[:count] and 5000 in samples[:count]) or undersampled
        screenshot(ui, ROOT/'build/breadxt-clock-vga.png')
        app.open('RC.BBX', 4)
        c0 = ui.m.status()['cycles']
        ui.menu_pick('Simulate', 'Run')
        ui._wait(lambda: app.stamp() >= 1000, 'actual target RC charge', 1800)
        ui.menu_pick('Simulate', 'Pause')
        ui._wait(lambda: not app.word('bb_running'), 'RC paused', 20)
        elapsed = app.stamp()
        mv = app.pin(3, 1)[0]
        expected = 5000*(1-(1.01)**(-elapsed/10))
        assert abs(mv-expected) <= 2, (elapsed, mv, expected)
        evidence.update(RC_time_100us=elapsed, RC_mv=mv, RC_expected_mv=expected,
                        RC_cycles=ui.m.status()['cycles']-c0)
        screenshot(ui, ROOT/'build/breadxt-rc-vga.png')
        evidence['ui_stack_high_water_bytes'] = water_read(ui, water)
        print(json.dumps(evidence, indent=2), flush=True)
    evidence['elapsed_host_seconds'] = round(time.monotonic()-started, 2)
    (ROOT/'build/breadxt-target-evidence.json').write_text(json.dumps(evidence, indent=2)+'\n')
    print('breadxt target: numeric ABI, red control, HC bridges, reset, LED, B clock, RC and stack PASS')
    print('elapsed host seconds:', evidence['elapsed_host_seconds'])


if __name__ == '__main__':
    main()
