#!/usr/bin/env python3
"""REDLINE: actual CPU probes, timing arithmetic, report/media and adapter fences.

--host walks all four floppies independently and verifies the saved reference.
--modern runs the shipped probes in a BIOS boot harness on QEMU's 486/Pentium
models (including CPUID vendor overrides, not real Cyrix/Transmeta hardware).
--machine runs the complete native package on MartyPC, verifies buffers, scores,
repeat-safe CPU probes, 48/32 arithmetic, pagination and report saving.
"""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests/unit')]
import redline_profile as R
from os88build import at


def host():
    from t_image import Vol
    exe = Path(at(str(ROOT / 'build/redline.o88'))).read_bytes()
    for suffix, size in [('', 1440), ('720', 720), ('120', 1200), ('360', 360)]:
        path = ROOT / ('build/redline%s.img' % suffix)
        path = Path(at(str(path)))
        v = Vol(path.read_bytes(), str(path))
        assert len(v.blob) == size * 1024
        found = None
        for folder, name, attr, clus, length in v.walk():
            if name == b'REDLINE O88':
                chain, end = v.chain(clus)
                assert end >= 0xFF8
                found = b''.join(v.blob[v.cluster_lba(c)*512:
                                       (v.cluster_lba(c)+v.spc)*512] for c in chain)[:length]
        assert found == exe, (path, 'package absent or chain damaged')
        subprocess.run([sys.executable, str(ROOT / 'tools/os88disk.py'), '--verify', str(path)],
                       check=True)
    ref = json.loads((ROOT / 'apps/redline/reference.json').read_text())
    inc = (ROOT / 'apps/redline/baseline.inc').read_text()
    counts = [int(x) for x in re.findall(r'^\s+dd (\d+)', inc, re.M)]
    assert len(counts) == R.ROWS and counts == ref['workload_counts']
    assert all(0 < c < 0xFFFFFFFF for c in counts)
    sym = R.symbols()
    import hashlib
    image = Path(at(str(ROOT / 'build/redline.bin'))).read_bytes()
    assert hashlib.sha256(image[sym['rl_alu']:sym['rl_detect']]).hexdigest() == ref['workload_code_sha256'], 'baseline does not match workload code; recalibrate'
    assert len(ref['trials']) == 3
    for adapter in ('cga', 'herc', 'vga'):
        suffix = '' if adapter == 'cga' else '-' + adapter
        record = json.loads((ROOT / ('apps/redline/reference%s.json' % suffix)).read_text())
        table = (ROOT / ('apps/redline/baseline%s.inc' % suffix)).read_text()
        expected = record['workload_counts'] if adapter == 'cga' else record['workload_counts'][6:]
        assert [int(x) for x in re.findall(r'^\s+dd (\d+)', table, re.M)] == expected
        assert all(0 < n <= 0xFFFFFFFF for n in record['workload_counts'])
        assert record['workload_code_sha256'] == ref['workload_code_sha256']
        assert record['adapter'] == adapter and record['runs_per_trial'] == R.RUNS
        assert record['canvas_height'] == (64 if adapter == 'cga' else 128)
        assert record['rotation_frames_per_row'] == 48
        assert record['fractal_grid'] == [64,32] and record['fractal_iteration_cap'] == 24
        assert len(record['sample_flags']) == 3
        assert all(len(x) == R.ROWS*R.RUNS and set(x) <= set('PTw!') for x in record['sample_flags'])
        for mean, samples in zip(record['trials'], record['sample_runs']):
            assert mean == [sum(values)//R.RUNS for values in zip(*samples)]
        assert record['workload_counts'] == [sorted(x)[1] for x in zip(*record['trials'])]
    assert ref['cpu'] == 'Intel8088' and not ref['turbo']
    assert abs(ref['clock_hz'] - 4772727.272727273) < .01
    print('REDLINE four media geometries and measured reference: OK', flush=True)


def invoke(p, name, **args):
    """Call actual assembled code while stopped, returning registers."""
    m = p.m
    m.pause()
    old = m.regs()
    regs = ('ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'sp', 'ss', 'ds', 'es', 'flags')
    sentinel = p.base + p.sym['rl_key']
    m.cmd(cmd='park', cs=p.base >> 4, ip=p.sym[name])
    for r in regs:
        value = args.get(r, (p.base >> 4) if r == 'ds' else old[r])
        if r == 'flags':
            # Debugger writes bypass POPF's V20 native-mode protection.
            # Preserve MD when requesting interrupt flags for a probe.
            value = (value & 0x7FFF) | (old[r] & 0x8000)
        m.setreg(r, value)
    sp = (old['sp'] - 2) & 0xFFFF
    m.setreg('sp', sp)
    m.write((old['ss'] << 4) + sp, struct.pack('<H', p.sym['rl_key']))
    m.bp_exec(sentinel)
    m.run()
    assert m.wait_stop(20) == 'breakpoint', name + ' did not return'
    answer = m.regs()
    m.bp_exec()
    m.cmd(cmd='park', cs=old['cs'], ip=old['ip'])
    for r in regs:
        m.setreg(r, old[r])
    return answer


def native(machine):
    import os88ui
    import os88flush as F
    import os88marty as M
    sym = R.symbols()
    out = ROOT / ('build/redline-test-' + machine)
    out.mkdir(exist_ok=True)
    with os88ui.boot(str(ROOT / 'build/os8088-360.img'),
                     apps=str(ROOT / 'build/redline360.img'), machine=machine) as ui:
        ui.open_drive('B')
        ui.open('REDLINE.O88')
        original_frame = (ui.window('REDLINE').w, ui.window('REDLINE').h)
        p = R.Probe(ui, sym)
        R.key(ui, 'KeyR')
        R.wait(ui, lambda:p.data('rl_busy') == b'\1', 'run has no visible progress state')
        R.wait(ui, lambda:any(w.title == 'REDLINE Graphics Lab' for w in ui.windows()),
               'graphics lab did not open')
        R.wait(ui, lambda: p.word('rl_runs') == 1, 'native run did not finish')
        counts = p.counts()
        assert all(0 < n < 0xFFFFFFFF for n in counts), counts
        assert p.data('bl_full') == b'\0'
        assert p.word('rl_convkb') == 640
        assert p.data('ru_view') == b'\0'
        assert list(counts) == [sum(x)//R.RUNS for x in zip(*p.samples())]
        assert p.word('rl_pass') == R.RUNS and p.data('rl_busy') == b'\0'
        assert (ui.window('REDLINE').w, ui.window('REDLINE').h) == original_frame
        assert p.word('rl_labwin') == 0
        assert p.word('rl_canvas_h') == (64 if p.word('rl_video') & 255 == 2 else 128)
        # Execute shipped integer kernels: rotating poses must change the
        # projection and return to the initial pose after one revolution.
        ui.m.write(p.addr('rl_angle'), struct.pack('<H',0))
        poses = []
        for _ in range(12):
            invoke(p,'rl_cube_setup')
            invoke(p,'rl_project')
            poses.append(p.data('rl_projected',32))
            points = struct.unpack('<16H',poses[-1])
            assert all(p.word('rl_x') <= x < p.word('rl_x')+256 for x in points[::2]), points
            assert all(p.word('rl_y') <= y < p.word('rl_y')+p.word('rl_canvas_h') for y in points[1::2]), points
        assert len(set(poses)) == 12 and p.word('rl_angle') == 0
        for real,imag,escape in [(0,0,24),(-256,0,24),(256,0,2),(768,0,1),(0,256,24)]:
            regs = invoke(p,'rl_fractal_point',ax=real&65535,bx=imag&65535)
            assert regs['ax'] == escape, (real,imag,regs['ax'],escape)
        assert not any(w.title == 'REDLINE Graphics Lab' for w in ui.windows())
        measured_indices = struct.unpack('<%dI' % R.ROWS, p.data('ru_scores', R.ROWS*4))
        reference = json.loads((ROOT/'apps/redline/reference.json').read_text())
        adapter = {0:'vga', 1:'herc', 2:'cga'}[p.word('rl_video') & 255]
        gfx_suffix = '' if adapter == 'cga' else '-' + adapter
        graphics_reference = json.loads((ROOT/('apps/redline/reference%s.json' % gfx_suffix)).read_text())
        bases = reference['workload_counts'][:6] + graphics_reference['workload_counts'][6:]
        expected_indices = tuple(min(b*1000//n, 0xFFFFFFFF) for b,n in zip(bases, counts))
        assert measured_indices == expected_indices, (measured_indices, expected_indices, bases, counts)
        overall = struct.unpack('<I', p.data('ru_overall', 4))[0]
        assert overall == sum(expected_indices[:6])//6
        # Each bucket's mean: CPU rows 0-3, RAM 4-5, graphics 6-24 (103.4.1).
        groups = struct.unpack('<3I', p.data('ru_gscores', 12))
        assert groups == (sum(expected_indices[:4])//4, sum(expected_indices[4:6])//2,
                          sum(expected_indices[6:])//19), (groups, expected_indices)
        scale = struct.unpack('<I', p.data('ru_scale', 4))[0]
        assert scale == min(((max(expected_indices)+999)//1000+5)*1000, 0xFFFFFFFF)
        R.wait(ui, lambda:p.word('ru_anim') == 12, 'completion animation did not finish')
        ptr = p.word('rl_cpuname')
        cpu = ui.m.read(p.base + ptr, 32).split(b'\0')[0].decode('ascii')
        if 'v20' in machine:
            assert cpu == 'NEC V20', cpu
        else:
            assert cpu == '8088-compatible', cpu
        # Re-enter the exact CPU probe twice. Self-modifying queue test must
        # restore its bytes and retain CPU identity on both runs.
        for _ in range(2):
            invoke(p, 'rl_detect', flags=0x202)
            assert p.word('rl_cpuname') == ptr
        # Boundary arithmetic actually executes on an 8088. A truncated
        # denominator would pass small examples and fail these large cases.
        cases = [(1234567*1000, 1234567), ((1 << 48)-1, 0xFFFFFFFF),
                 (0, 17), (1 << 40, 0x80000001), (19, 0),
                 (65536*1000, 65536), ((1 << 48)-1, 1)]
        for num, den in cases:
            ui.m.write(p.addr('bl_m'), num.to_bytes(6, 'little'))
            ans = invoke(p, 'rl_div48by32', bx=den & 65535, cx=den >> 16)
            got = (ans['dx'] << 16) | ans['ax']
            expect = min(num // den, 0xFFFFFFFF) if den else 0xFFFFFFFF
            assert got == expect, (num, den, got, expect)
        # Exercise real 8088 scale/format code with faster machines and 32-bit
        # boundaries. Restore measured scores before normal UI checks.
        original_scores = p.data('ru_scores', R.ROWS*4)
        for value, ceiling in [(0,5000), (999,6000), (1000,6000),
                               (69000,74000), (69001,75000), (99999,105000),
                               (100000,105000), (250000,255000), (123456700,123462000),
                               (0xFFFFFFFF,0xFFFFFFFF)]:
            values = [0]*R.ROWS
            values[4] = value      # a single RAM result sets every graph's scale
            ui.m.write(p.addr('ru_scores'), struct.pack('<%dI' % R.ROWS, *values))
            invoke(p, 'ru_scale_compute')
            assert struct.unpack('<I', p.data('ru_scale',4))[0] == ceiling
        for value, text in [(0,'0x'), (74000,'74x'), (18500,'18.50x'), (1500,'1.50x')]:
            regs = invoke(p,'ru_axis_label',ax=value&65535,dx=value>>16)
            got = ui.m.read(p.base+regs['si'],16).split(b'\0')[0].decode('ascii')
            assert got == text, (value,text,got)
        for value, text in [(0,'n/a'), (999,'0.99x'), (1007,'1.00x'), (1317,'1.31x'),
                            (1000,'1.00x'), (100000,'100.00x'),
                            (123456780,'123456.78x'), (0xFFFFFFFF,'4294967.29x')]:
            regs = invoke(p,'ru_ratio',ax=value&65535,dx=value>>16)
            got = ui.m.read(p.base+regs['si'],16).split(b'\0')[0].decode('ascii')
            assert got == text, (value, text, got, regs['si'])
        ui.m.write(p.addr('ru_scores'), original_scores)
        invoke(p,'ru_scale_compute')
        # Unknown geometry never borrows a VGA graphics reference.
        old_h = p.word('rl_vh')
        ui.m.write(p.addr('rl_vh'), struct.pack('<H',123))
        invoke(p,'rl_select_reference')
        assert p.word('rl_gfxbase') == 0
        ui.m.write(p.addr('rl_vh'),struct.pack('<H',old_h))
        invoke(p,'rl_select_reference')
        assert p.word('rl_gfxbase') != 0
        generation = p.word('rl_runs')
        R.key(ui, 'KeyR')
        R.wait(ui, lambda: p.word('rl_runs') != generation, 'rerun did not finish')
        R.wait(ui, lambda:p.word('ru_anim') == 12, 'rerun animation did not finish')
        R.key(ui, 'KeyS')
        R.wait(ui, lambda: p.data('bl_saved') == b'\1', 'save did not finish')
        ui.m.advance(frames=60)
        report = F.Flush(marty=ui.m).volume(1).read('REDLINE.TXT').decode('ascii')
        (out / 'REDLINE.TXT').write_text(report)
        assert cpu in report and 'BIOS conventional KB           640' in report
        assert 'MUL 64 fixed operands' in report and 'Packed 4bpp blit 64x32' in report
        assert 'REPORT TRUNCATED' not in report
        assert 'Graphics index unavailable' not in report
        assert 'Graphics reference' in report
        for label, value in zip(('CPU mean index (4)', 'RAM mean index (2)',
                                 'Graphics mean index (19)'),
                                struct.unpack('<3I', p.data('ru_gscores', 12))):
            assert re.search(r'(?m)^%s\s+%d\r?$' % (re.escape(label), value), report), label
        assert 'Arithmetic mean of 3 complete runs' in report
        assert 'L3 Projected wireframe cube' in report and 'L3 Projected shaded cube' in report
        assert 'L3 Window resize/repaint' in report
        assert list(p.counts()) == [sum(x)//R.RUNS for x in zip(*p.samples())]
        flags = p.data('rl_sampleflags', R.ROWS*R.RUNS).decode('ascii')
        assert all(flags[i*R.ROWS+R.ROWS-1] == 'T' for i in range(R.RUNS)), flags
        assert 'L3 Mandelbrot 64x32' in report and 'Canvas height pixels' in report
        indices = [int(x) for x in re.findall(r'^.*?\s+(\d+)\r?$',
                   report.split('Bar scale maximum (x)')[1], re.M)][1:]
        assert len(indices) == R.ROWS, indices
        assert indices == list(struct.unpack('<%dI' % R.ROWS, p.data('ru_scores', R.ROWS*4)))
        if machine in ('os8088_redline_pc_gla','os8088_redline_herc_gla','os8088_redline_vga_gla'):
            assert all(800 < n < 1200 for n in indices), indices
        def screenshot(name):
            ui.m.advance(frames=60)
            if p.word('rl_video') & 255 == 0:
                w, h, pixels = ui.m.fbuf()
                M.write_png_rgb(str(out/name), w, h, pixels)
            else:
                kind = 'herc' if p.word('rl_video') & 255 == 1 else 'cga'
                w, h, rows = ui.m.vram(kind)
                M.write_png(str(out/name), w, h, rows)

        screenshot('summary.png')
        if adapter == 'vga':
            # Inspect actual bar pixels, not the palette chosen in source.
            # Lines: 0 CPU average, 1-4 CPU rows, 5 RAM average, 6-7 RAM rows,
            # 8 Graphics average, 9.. graphics rows.
            w,h,pixels = ui.m.fbuf()
            x = p.word('ru_x')+153
            y = p.word('ru_y')+p.word('ru_lower')+p.word('ru_offset')+1
            def rgb(px,py): return tuple(pixels[(py*w+px)*3:(py*w+px)*3+3])
            colors = [rgb(x,y+i*p.word('ru_pitch')) for i in range(10)]
            for line in (0, 1):
                assert colors[line][2] > colors[line][0] and colors[line][2] > colors[line][1], colors
            for line in (5, 6):
                assert colors[line][1] > colors[line][0] and colors[line][1] > colors[line][2], colors
            for line in (8, 9):
                assert colors[line][0] > colors[line][1] and colors[line][0] > colors[line][2], colors
        scrollpanes(ui, p)
        # Detail acts on release. A held press cannot change views, and
        # dragging off it must cancel rather than leave an inverted button.
        rects = struct.unpack('<24H', p.data('ru_rects', 48))
        x1,y1,x2,y2 = rects[4:8]
        ui.m.run()
        ui.mo.to((x1+x2)//2, (y1+y2)//2)
        ui.mo._sep()
        ui.mo._edge(True)
        ui.m.advance(frames=8)
        assert p.data('ru_view') == b'\0', 'Detailed fired on press'
        assert struct.unpack('<H', ui.m.read(p.addr('ru_buttons')+10, 2))[0] == 2
        ui.m.run()
        ui.mo.to(x1, y1-12)
        ui.mo._edge(False)
        ui.m.advance(frames=8)
        assert p.data('ru_view') == b'\0', 'cancelled press changed view'
        ui.m.run()
        ui.mo.click((x1+x2)//2, (y1+y2)//2, settle=0)
        R.wait(ui, lambda:p.data('ru_view') == b'\1', 'Detailed button did not fire')
        R.key(ui, 'End')
        R.wait(ui, lambda:p.word('bl_top') > 0, 'Detailed End did not paginate')
        screenshot('detailed.png')
        R.key(ui, 'KeyC')
        R.wait(ui, lambda:p.data('ru_view') == b'\2', 'Compare key did not switch view')
        R.key(ui, 'Home')
        screenshot('compare.png')
        R.key(ui, 'KeyU')
        R.wait(ui, lambda:p.data('ru_view') == b'\0', 'Summary key did not switch view')
        R.key(ui, 'PageDown')
        if p.word('ru_rows') < LINES:
            R.wait(ui, lambda:p.word('ru_page') > 0, 'compact results did not paginate')
            R.key(ui, 'ArrowDown')
            R.wait(ui, lambda:p.word('ru_page') == min(p.word('ru_rows') + 1, LINES - p.word('ru_rows')),
                   'Down did not step the results one line')
            screenshot('paged.png')
        R.key(ui, 'Home')
        R.wait(ui, lambda:p.word('ru_page') == 0, 'Home did not return to the first line')
        R.key(ui, 'F1')
        R.wait(ui, lambda:p.data('ru_view') == b'\1' and p.word('bl_top') == 0,
               'F1 did not open Detailed at provenance')
        screenshot('redline.png')
        # Quit is a real application button through the deferred close API.
        x1,y1,x2,y2 = struct.unpack('<24H', p.data('ru_rects', 48))[20:24]
        ui.m.run()
        ui.mo.click((x1+x2)//2, (y1+y2)//2, settle=0)
        M.until(ui.m, lambda _:not any(w.title == 'REDLINE' for w in ui.windows()),
                'Quit closes REDLINE', limit=20)
        print('REDLINE %s: %s, timings, Summary/Detailed/Compare, gestures and Quit OK' %
              (machine, cpu), flush=True)


LINES = 28                          # RL_ROWS + three bucket headers (103.4.1)


def pane(p, name):
    """A scroll pane record: top, visible, count, and its bar's x1 y1 x2 y2."""
    w = struct.unpack('<11H', p.data(name, 22))
    return dict(top=w[0], vis=w[1], cnt=w[2], bar=w[4:8])


def scrollpanes(ui, p):
    """Every pane that overflows has a working bar; every pane that fits has
    none to hit (103.4.2). Clicks go through the real press path."""
    for name in ('ru_pres', 'ru_psnap', 'ru_pdet'):
        pn = pane(p, name)
        x1, y1, x2, y2 = pn['bar']
        if pn['vis'] >= pn['cnt']:
            continue
        assert pn['top'] == 0, (name, pn)
        ui.m.run()
        ui.mo.click((x1+x2)//2, y2-4, settle=0)        # the down arrow
        R.wait(ui, lambda: pane(p, name)['top'] == 1, '%s down arrow did not scroll' % name)
        ui.m.run()
        ui.mo.click((x1+x2)//2, y1+4, settle=0)        # the up arrow
        R.wait(ui, lambda: pane(p, name)['top'] == 0, '%s up arrow did not scroll' % name)
        if y2 - y1 > 40:                               # a track to page in
            ui.m.run()
            ui.mo.click((x1+x2)//2, y2-14, settle=0)
            R.wait(ui, lambda: pane(p, name)['top'] == min(pn['vis'], pn['cnt']-pn['vis']),
                   '%s track did not page' % name)
            if name == 'ru_pres':
                R.key(ui, 'Home')
        print('  %s scrolls: %d of %d lines visible' % (name, pn['vis'], pn['cnt']), flush=True)
    assert p.data('ru_view') == b'\0', 'a scroll-bar press reached a view button'


def scene():
    import os88ui
    import os88marty as M
    sym = R.symbols()
    out = ROOT/'build/redline-scene'
    out.mkdir(exist_ok=True)
    with os88ui.boot(str(ROOT/'build/os8088-360.img'),
                     apps=str(ROOT/'build/redline360.img'), machine='os8088_redline_vga_gla') as ui:
        ui.open_drive('B')
        ui.open('REDLINE.O88')
        p = R.Probe(ui,sym)
        def settled_pixels():
            # VGA scanout can lag VRAM at a breakpoint. Park for two frames
            # at the BIOS scratch end; these display-only runs never calibrate.
            old = ui.m.regs()
            scratch = ui.m.read(0x500,2)
            ui.m.write(0x500,b'\xEB\xFE')
            ui.m.cmd(cmd='park',cs=0,ip=0x500)
            ui.m.advance(frames=2)
            pixels = ui.m.fbuf()
            ui.m.write(0x500,scratch)
            ui.m.cmd(cmd='park',cs=old['cs'],ip=old['ip'])
            for reg in ('ax','bx','cx','dx','si','di','bp','sp','ss','ds','es','flags'):
                ui.m.setreg(reg,old[reg])
            return pixels
        ui.m.bp_exec(p.addr('rl_shaded'))
        R.key(ui,'KeyR')
        ui.m.run()
        assert ui.m.wait_stop(60) == 'breakpoint', 'wireframe did not render'
        assert p.data('rl_busy') == b'\1' and p.word('rl_pass') == 0
        assert any(w.title == 'REDLINE Graphics Lab' for w in ui.windows())
        w,h,pixels = settled_pixels()
        M.write_png_rgb(str(out/'wireframe.png'),w,h,pixels)
        ui.m.bp_exec(p.addr('rl_fractal'))
        ui.m.run()
        assert ui.m.wait_stop(60) == 'breakpoint', 'shaded cube did not render'
        w,h,shaded = settled_pixels()
        M.write_png_rgb(str(out/'shaded.png'),w,h,shaded)
        assert shaded != pixels, 'wireframe and shaded output are identical'
        lab = ui.window('REDLINE Graphics Lab')
        assert lab.h == 178 and p.word('rl_canvas_h') == 128
        print('Live lab frame:',lab.x,lab.y,lab.w,lab.h,'; parent:',ui.window('REDLINE').x,ui.window('REDLINE').y,ui.window('REDLINE').w,ui.window('REDLINE').h,flush=True)
        crop = [tuple(shaded[(y*w+x)*3:(y*w+x)*3+3])
                for y in range(lab.y+20,lab.y+lab.h-2)
                for x in range(lab.x+2,lab.x+lab.w-2)]
        colored = set(crop)-{(0,0,0),(255,255,255)}
        assert len(colored) >= 3, colored
        ui.m.bp_exec(p.addr('rl_patternblit'))
        ui.m.run()
        assert ui.m.wait_stop(120) == 'breakpoint', 'fractal did not render'
        w,h,fractal = settled_pixels()
        M.write_png_rgb(str(out/'fractal.png'),w,h,fractal)
        crop = [tuple(fractal[(y*w+x)*3:(y*w+x)*3+3])
                for y in range(lab.y+25,lab.y+153)
                for x in range(lab.x+9,lab.x+265)]
        assert len(set(crop)-{(0,0,0),(255,255,255)}) >= 6
        assert crop.count((0,0,0)) > len(crop)//10, 'Mandelbrot interior is absent'
        ui.m.bp_exec(p.addr('rl_resize'))
        ui.m.run()
        assert ui.m.wait_stop(60) == 'breakpoint', 'nested moving windows did not render'
        positions = struct.unpack('<10H',p.data('rl_object_xy',20))
        assert positions[2] == positions[0]+14
        assert positions[4] == positions[2]+5
        assert positions[6] == positions[4]+5
        assert struct.unpack('<H',p.data('rl_objects',20)[18:20])[0] == 6
        w,h,pixels = settled_pixels()
        M.write_png_rgb(str(out/'nested-windows.png'),w,h,pixels)
        ui.m.bp_exec()
    print('REDLINE live lab: tall canvas, rotating/shaded 3D, Mandelbrot, nested moving objects OK',flush=True)


def modern():
    # Boot the SHIPPED routines with a tiny CPU_INFO cell, actual BIOS RAM
    # discovery and real QEMU CPUID. Hardware timings here are never baselines.
    src = (ROOT / 'apps/redline/redline.asm').read_text()
    fields = ('rl_cpuname', 'rl_cpuid', 'rl_nominal', 'rl_family', 'rl_model',
              'rl_mapok', 'rl_ramkb', 'rl_tscmhz', 'rl_features', 'rl_clockspan', 'rl_tscstart', 'rl_facts_start', 'rl_facts_end')
    results = {}
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        for cpu, ram, tier, expected in [
            ('486', 16, 1, '80286 class'),
            ('486', 16, 2, None),
            ('pentium', 16, 2, 'GenuineIntel'),
            ('pentium,vendor=CyrixInstead', 16, 2, 'Cyrix (CPUID)'),
            ('pentium,vendor=GenuineTMx86', 16, 2, 'Transmeta (CPUID)'),
            ('pentium,vendor=TransmetaCPU', 16, 2, 'Transmeta (CPUID)'),
            ('pentium3', 64, 2, 'GenuineIntel')]:
            wrapper = '''
cpu 386
rl_test_entry:
    push cs
    pop ds
    mov ax, 0x60
    mov es, ax
    mov byte [es:0x155], 0xB8
    mov word [es:0x156], TIER
    mov byte [es:0x158], 0xCB
    call rl_detect
    cmp byte [rl_tier], CPU_386
    jne .dump
    call rl_e820
    call rl_tsc_mhz
.dump:
    mov si, rl_facts_start
    mov cx, rl_facts_end - rl_facts_start
    mov dx, 0xE9
    cld
.byte:
    lodsb
    out dx, al
    loop .byte
    mov al, 0x10
    out 0xF4, al
.halt:
    hlt
    jmp .halt
'''.replace('TIER', str(tier))
            asm, binary = td / 'probe.asm', td / 'probe.bin'
            asm.write_text(src + wrapper + '\ndw rl_test_entry\n' + '\n'.join('dw '+n for n in fields))
            subprocess.run(['nasm', '-f', 'bin', '-I', str(ROOT/'apps')+'/', '-I', str(ROOT/'tests')+'/',
                            '-o', str(binary), str(asm)], check=True, cwd=ROOT)
            blob = binary.read_bytes()
            syms = dict(zip(('entry',)+fields, struct.unpack('<%dH' % (len(fields)+1),
                           blob[-2*(len(fields)+1):])))
            boot = td / 'boot.asm'
            boot.write_text('''cpu 8086
org 0x7C00
    cli
    xor ax, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov ax, 0x0211
    mov cx, 2
    xor dx, dx
    int 0x13
    jc fail
    mov bx, 0x2200
    mov ax, 0x0212
    mov cx, 1
    mov dh, 1
    int 0x13
    jc fail
    mov bx, 0x4600
    mov ax, 0x0212
    mov cx, 0x0101
    xor dh, dh
    int 0x13
    jc fail
    jmp 0x1000:ENTRY
fail:
    mov al, 0x11
    out 0xF4, al
    hlt
    times 510-($-$$) db 0
    dw 0xAA55
'''.replace('ENTRY', str(syms['entry'])))
            bootbin = td / 'boot.bin'
            subprocess.run(['nasm', '-f', 'bin', '-o', str(bootbin), str(boot)], check=True)
            disk, dump = td / 'probe.img', td / 'probe.dump'
            assert len(blob) <= 53*512
            disk.write_bytes((bootbin.read_bytes()+blob).ljust(1440*1024, b'\0'))
            run = subprocess.run(['qemu-system-i386', '-machine', 'pc', '-cpu', cpu, '-m', str(ram),
                                  '-drive', 'file=%s,format=raw,if=floppy' % disk,
                                  '-display', 'none', '-monitor', 'none', '-serial', 'none',
                                  '-debugcon', 'file:'+str(dump), '-no-reboot',
                                  '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04'],
                                 capture_output=True, timeout=25)
            assert run.returncode == 33, (cpu, run.returncode, run.stderr.decode())
            raw = dump.read_bytes()
            base = syms['rl_facts_start']
            def at(name): return syms[name] - base
            ptr = struct.unpack_from('<H', raw, at('rl_cpuname'))[0]
            # Vendor buffer lives in the dumped facts; constant names live in image.
            label = (raw[ptr-base:] if base <= ptr < syms['rl_facts_end'] else blob[ptr:])
            name = label.split(b'\0')[0].decode('ascii')
            if expected:
                assert name == expected, (cpu, name, expected)
            if tier == 2:
                assert raw[at('rl_mapok')] == 1, (cpu, 'no E820')
                kb = struct.unpack_from('<I', raw, at('rl_ramkb'))[0]
                assert ram*1024-1024 < kb <= ram*1024, (cpu, kb)
                if cpu.startswith('pentium'):
                    assert raw[at('rl_cpuid')] == 1
                    mhz = struct.unpack_from('<I', raw, at('rl_tscmhz'))[0]
                    assert mhz > 0, (cpu, 'TSC sample absent', {n:raw[at(n):at(n)+8].hex() for n in ('rl_features', 'rl_clockspan', 'rl_tscstart')})
            results[cpu] = name
            print('CPU probe %s / %sMB / tier %s: %s' % (cpu, ram, tier, name), flush=True)
    print('REDLINE modern CPU vendor, opcode gates and BIOS RAM probes: OK', flush=True)


def nec():
    """Exercise the shipped early probe with an independent IRQ0 harness.

    Install a minimal IRQ0 handler, a real PIT/PIC and the CPU_INFO tier cell.
    Both calls execute the same instructions as the package, with timer IRQs.
    """
    import os88marty as M
    wrapper = '''
cpu 8086
rl_nec_entry:
    cli
    push cs
    pop ds
    xor ax, ax
    mov es, ax
    mov word [es:0x20], rl_nec_irq
    mov [es:0x22], cs
    mov ax, 0x60
    mov es, ax
    mov byte [es:0x155], 0xB8
    mov word [es:0x156], 0
    mov byte [es:0x158], 0xCB
    mov al, 0x13
    out 0x20, al
    mov al, 8
    out 0x21, al
    mov al, 1
    out 0x21, al
    mov al, 0xFE
    out 0x21, al
    mov al, 0x34
    out 0x43, al
    xor al, al
    out 0x40, al
    out 0x40, al
    sti
    call rl_detect
    mov ax, [rl_cpuname]
    mov [rl_nec_first], ax
    call rl_detect
rl_nec_done:
    jmp rl_nec_done
rl_nec_irq:
    push ax
    push ds
    mov ax, 0x40
    mov ds, ax
    inc word [0x6C]
    mov al, 0x20
    out 0x20, al
    pop ds
    pop ax
    iret
rl_nec_first: dw 0
'''
    labels = ['rl_nec_entry', 'rl_nec_done', 'rl_nec_first', 'rl_cpuname']
    with tempfile.TemporaryDirectory() as td:
        asm, binary = Path(td)/'nec.asm', Path(td)/'nec.bin'
        asm.write_text((ROOT/'apps/redline/redline.asm').read_text() + wrapper +
                       '\n' + '\n'.join('dw '+n for n in labels))
        subprocess.run(['nasm', '-f', 'bin', '-I', str(ROOT/'apps')+'/',
                        '-I', str(ROOT/'tests')+'/', '-o', str(binary), str(asm)],
                       check=True, cwd=ROOT)
        blob = binary.read_bytes()
        sym = dict(zip(labels, struct.unpack('<4H', blob[-8:])))
        with M.launch(str(ROOT/'build/os8088-360.img'),
                      machine='os8088_redline_v20_gla', boot=False) as m:
            base = 0x10000
            m.write(base, blob)
            m.write(0x46C, b'\0'*4)
            m.cmd(cmd='park', cs=base>>4, ip=sym['rl_nec_entry'])
            m.setreg('ss', 0x9000)
            m.setreg('sp', 0xFFF0)
            m.bp_exec(base+sym['rl_nec_done'])
            m.run()
            assert m.wait_stop(20) == 'breakpoint', 'NEC probe did not finish'
            first = struct.unpack('<H', m.read(base+sym['rl_nec_first'], 2))[0]
            second = struct.unpack('<H', m.read(base+sym['rl_cpuname'], 2))[0]
            name = m.read(base+second, 32).split(b'\0')[0].decode('ascii')
            assert first == second and name == 'NEC V20', (first, second, name)
            assert struct.unpack('<H', m.read(0x46C, 2))[0] > 0
    print('REDLINE NEC V20: real IRQ0 restart test and repeat-safe queue probe OK', flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--host', action='store_true')
    ap.add_argument('--modern', action='store_true')
    ap.add_argument('--nec', action='store_true')
    ap.add_argument('--scene', action='store_true')
    ap.add_argument('--machine')
    args = ap.parse_args()
    if args.host: host()
    if args.modern: modern()
    if args.nec: nec()
    if args.scene: scene()
    if args.machine: native(args.machine)
    assert args.host or args.modern or args.nec or args.machine or args.scene


if __name__ == '__main__':
    main()
