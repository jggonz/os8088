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
    assert len(counts) == 12 and counts == ref['workload_counts']
    assert all(0 < c < 0xFFFFFFFF for c in counts)
    sym = R.symbols()
    import hashlib
    image = Path(at(str(ROOT / 'build/redline.bin'))).read_bytes()
    assert hashlib.sha256(image[sym['rl_alu']:sym['rl_detect']]).hexdigest() == ref['workload_code_sha256'], 'baseline does not match workload code; recalibrate'
    assert len(ref['trials']) == 3
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
        p = R.Probe(ui, sym)
        R.key(ui, 'KeyR')
        R.wait(ui, lambda: p.word('rl_runs') == 1, 'native run did not finish')
        counts = p.counts()
        assert all(0 < n < 0xFFFFFFFF for n in counts), counts
        assert p.data('bl_full') == b'\0'
        assert p.word('rl_convkb') == 640
        assert p.data('ru_view') == b'\0'
        measured_indices = struct.unpack('<12I', p.data('ru_scores', 48))
        reference = json.loads((ROOT/'apps/redline/reference.json').read_text())
        expected_indices = tuple(min(b*1000//n, 0xFFFFFFFF) for b,n in
                                 zip(reference['workload_counts'], counts))
        assert measured_indices == expected_indices
        overall = struct.unpack('<I', p.data('ru_overall', 4))[0]
        assert overall == sum(expected_indices[:6])//6
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
        generation = p.word('rl_runs')
        R.key(ui, 'KeyR')
        R.wait(ui, lambda: p.word('rl_runs') != generation, 'rerun did not finish')
        R.key(ui, 'KeyS')
        R.wait(ui, lambda: p.data('bl_saved') == b'\1', 'save did not finish')
        ui.m.advance(frames=60)
        report = F.Flush(marty=ui.m).volume(1).read('REDLINE.TXT').decode('ascii')
        (out / 'REDLINE.TXT').write_text(report)
        assert cpu in report and 'BIOS conventional KB           640' in report
        assert 'MUL 64 fixed operands' in report and 'Packed 4bpp blit 64x32' in report
        assert 'REPORT TRUNCATED' not in report
        if p.word('rl_video') & 255 == 2:
            assert 'Graphics index unavailable' not in report
            indices = [int(x) for x in re.findall(r'^.*?\s+(\d+)\r?$',
                       report.split('PC index:')[1], re.M)]
            assert len(indices) == 12, indices
            if 'v20' not in machine:
                assert all(900 < n < 1100 for n in indices), indices
        else:
            assert 'Graphics index unavailable' in report
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
        if p.word('ru_rows') < 12:
            R.wait(ui, lambda:p.word('ru_page') > 0, 'compact results did not paginate')
        R.key(ui, 'Home')
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
    mov ax, 0x0211
    mov cx, 1
    mov dh, 1
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
            assert len(blob) <= 34*512
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
    ap.add_argument('--machine')
    args = ap.parse_args()
    if args.host: host()
    if args.modern: modern()
    if args.nec: nec()
    if args.machine: native(args.machine)
    assert args.host or args.modern or args.nec or args.machine


if __name__ == '__main__':
    main()
