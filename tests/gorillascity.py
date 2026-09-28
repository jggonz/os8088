#!/usr/bin/env python3
"""Time skyline construction/repaint at 4.77 MHz and check native VRAM pixels.

Debugger calls run inside a real keyboard handler's graphics lock. Rendering
is checked against a host pixel decoder, not another call to the same blitter.
--max-paint-ms 0 permits measurements of the original slow implementation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import time

import gorillas as G
import gorillasinput as I

DESKTOP = (1, 6, 12, 14, 0, 7, 4, 3, 8, 15, 14, 6, 8, 0, 14, 15)
MONO = (0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1)
CGA = (0, 3, 2, 3, 0, 2, 2, 1, 0, 3, 3, 2, 2, 0, 3, 3)


def call(m, code, name, **args):
    saved = m.regs()
    registers = ('ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'sp',
                 'ss', 'ds', 'es', 'flags')
    m.cmd(cmd='park', cs=saved['cs'], ip=code[name])
    for reg in registers:
        m.setreg(reg, args.get(reg, saved[reg]))
    sp = (saved['sp'] - 2) & 65535
    m.setreg('sp', sp)
    m.write((saved['ss'] << 4) + sp, struct.pack('<H', saved['ip']))
    start = m.status()['cycles']
    m.bp_exec((saved['cs'] << 4) + saved['ip'])
    m.run()
    assert m.wait_stop(30) == 'breakpoint', name + ' never returned'
    ms = (m.status()['cycles'] - start) / G.M.GUEST_HZ * 1000
    for reg in registers:
        m.setreg(reg, saved[reg])
    return ms


def lock(m, p, code):
    m.pause()
    m.bp_exec(p.base + code['gr_key'])
    m.key('KeyG')                 # no-op during human aiming
    m.run()
    assert m.wait_stop(30) == 'breakpoint'
    r = m.regs()
    ret = struct.unpack('<H', m.readseg(r['ss'], r['sp'], 2))[0]
    m.bp_exec((r['cs'] << 4) + ret)
    m.run()
    assert m.wait_stop(30) == 'breakpoint'


def check_pixels(m, p, tag, before=None, clip=None):
    vram = I.video_bytes(m, tag)
    expected = bytearray(vram if before is None else before)
    scene = p.data('scene', 16384)
    ox, oy, sx, sy = (p.w(n) for n in ('ox', 'oy', 'sx', 'sy'))
    vga, cga = p.b('vga'), p.b('cga')
    for y in range(128*sy):
        yy = y + oy
        for x in range(256*sx):
            xx = x + ox
            if clip and not (clip[0] <= xx <= clip[2] and clip[1] <= yy <= clip[3]):
                continue
            logical = x // sx
            ink = (scene[(y//sy)*128+logical//2] >> (0 if logical & 1 else 4)) & 15
            if tag == 'vga':
                at, bit = yy*80+xx//8, 128 >> (xx & 7)
                color = ink if vga else (
                    (12 if x % 2 == 0 else 14) if sx == 2 and ink == 1 else DESKTOP[ink])
                for plane in range(4):
                    loc = plane*38400+at
                    expected[loc] = (expected[loc] & ~bit) | (bit if color & (1 << plane) else 0)
            elif cga:
                at, shift = (yy & 1)*8192+(yy//2)*80+xx//4, 6-2*(xx & 3)
                expected[at] = (expected[at] & ~(3 << shift)) | CGA[ink] << shift
            else:
                at = ((yy & 3)*8192+(yy//4)*90+xx//8 if tag == 'herc' else
                      (yy & 1)*8192+(yy//2)*80+xx//8)
                bit = 128 >> (xx & 7)
                expected[at] = (expected[at] & ~bit) | (bit if MONO[ink] else 0)
    assert vram == expected, (tag, 'incorrect VRAM',
                             [(i, a, b) for i, (a, b) in enumerate(zip(vram, expected))
                              if a != b][:12])


def clipped(m, p, code, tag):
    """Force a partially visible window rectangle under the existing lock.

    Odd physical edges require exact clipping. Compare every VRAM byte, so
    writes outside the visible fragment (including other windows) fail.
    """
    n, tab = m.sym('wm_clip_n'), m.sym('wm_clip_tab')
    saved_n, saved_tab = m.read(n, 2), m.read(tab, 8)
    ox, oy, sx, sy = (p.w(k) for k in ('ox', 'oy', 'sx', 'sy'))
    clip = (ox+3, oy+25*sy+1, ox+253*sx, oy+122*sy)
    before = I.video_bytes(m, tag)
    m.write(n, struct.pack('<H', 1))
    m.write(tab, struct.pack('<4H', *clip))
    m.write(p.base + p.offsets['gr_scene'], bytes(range(255, -1, -1))*64)
    call(m, code, 'gr_blit', ax=0, bx=24, cx=256, dx=104)
    assert m.read(n, 2) == struct.pack('<H', 1), 'clip was disarmed'
    check_pixels(m, p, tag, before, clip)
    m.write(n, saved_n)
    m.write(tab, saved_tab)


def check_city(p):
    starts, widths, roofs = (p.data(n, p.w('buildings')) for n in ('lots', 'widths', 'roofs'))
    assert starts[0] == 0 and sum(widths) == 256
    assert all(18 <= w <= 36 for w in widths), widths
    assert len(set(widths)) > 1, 'uniform building widths'
    assert list(starts[1:]) == [sum(widths[:i]) for i in range(1, len(widths))]
    xs, ys = (struct.unpack('<2H', p.data(n, 4)) for n in ('gx', 'gy'))
    lots = [next(i for i, (s, w) in enumerate(zip(starts, widths))
                 if s < x and x+15 < s+w-1) for x in xs]
    assert lots[0] in (1, 2) and lots[1] in (len(widths)-2, len(widths)-3), lots
    assert lots[1] - lots[0] - 1 >= 2, ('too few buildings between players', lots)
    assert all(y+20 == roofs[i] for y, i in zip(ys, lots)), 'gorilla off roof'
    scene = p.data('scene', 16384)
    def ink(x, y):
        return (scene[y*128+x//2] >> (0 if x & 1 else 4)) & 15
    for s, w, roof in zip(starts, widths, roofs):
        assert 48 <= roof <= 103
        assert all(ink(x, roof) == 12 for x in range(s+1, s+w-1)), 'broken roof'
        assert all(ink(x, y) == 0 for x in (s, s+w-1)
                   for y in range(48, 116)), 'windows spill into gutter'
    return lots


def check_variety(m, p, code):
    positions = [set(), set()]
    counts, colors, profiles, winds, pairs = set(), set(), set(), set(), set()
    for seed in range(32):
        m.write(p.base + p.offsets['gr_seed'], struct.pack('<H', seed))
        call(m, code, 'gr_city')
        counts.add(p.w("buildings"))
        colors.add(p.data("colors", p.w("buildings")))
        profiles.add(p.b("profile"))
        winds.add(struct.unpack("<h", p.data("wind", 2))[0])
        lots = check_city(p)
        pairs.add((lots[0], p.w("buildings")-lots[1]))
        for seen, lot in zip(positions, lots):
            seen.add(lot)
    assert positions[0] == {1, 2}, positions
    assert len(pairs) == 4, pairs
    assert counts == set(range(8, 13)), counts
    assert len(colors) > 20 and profiles == set(range(6))
    assert min(winds) < -10 and max(winds) > 10, winds
    # A new aiming pass follows the actual human, including positions far
    # from the old hard-coded x=48 target. Release errors vary both ways and
    # remain legal even when the best candidate is at a velocity limit.
    m.write(p.base + p.offsets['gr_players'], b'\x01')
    m.write(p.base + p.offsets['gr_turn'], b'\x01')
    call(m, code, 'gr_aitick')
    assert p.w('aitarget') == (p.w('gx')+8)*64
    for best in (1, 70, 359):
        powers = set()
        for seed in range(32):
            m.write(p.base + p.offsets['gr_seed'], struct.pack('<H', seed))
            m.write(p.base + p.offsets['gr_aibest'], struct.pack('<H', best))
            call(m, code, 'gr_aitick.fire')
            powers.add(p.w('power'))
        assert all(1 <= power <= 360 and abs(power-best) <= 8 for power in powers)
        assert len(powers) >= 8, ('computer release lacks variety', powers)
        if best == 70:
            assert min(powers) < best < max(powers)
    m.write(p.base + p.offsets['gr_players'], b'\x02')
    m.write(p.base + p.offsets['gr_turn'], b'\x00')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--arm', choices=G.ARMS)
    ap.add_argument('--output', type=Path)
    ap.add_argument('--max-paint-ms', type=float, default=1800)
    args = ap.parse_args()
    off, code = G.offsets(), I.code_offsets(('gr_city', 'gr_blit', 'gr_aitick',
                                          'gr_aitick.fire'))
    report = {}
    seeded_scene = None
    started = time.monotonic()
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
                    lock(m, p, code)
                    if mode == 'window' and not report:
                        check_variety(m, p, code)
                    m.write(p.base + off['gr_seed'], struct.pack('<H', 12345))
                    build = call(m, code, 'gr_city')
                    scene = p.data('scene', 16384)
                    check_city(p)
                    if seeded_scene is None:
                        seeded_scene = scene
                    assert scene == seeded_scene, 'seeded city differs across modes/adapters'
                    paint = call(m, code, 'gr_fullpaint')
                    check_pixels(m, p, tag)
                    report[tag+'-'+mode] = dict(city_ms=build, paint_ms=paint,
                                               scene_sha256=hashlib.sha256(scene).hexdigest())
                    print(tag, mode, report[tag+'-'+mode], flush=True)
                    if args.max_paint_ms:
                        budget = args.max_paint_ms if tag == 'vga' else args.max_paint_ms/3
                        assert paint < budget, ('slow repaint', paint, budget)
                        assert build < 400, ('slow city generation', build)
                    # Negative control: a changed scene without a repaint must
                    # fail, including VGA plane 1 (the old peek missed it).
                    m.write(p.base + off['gr_scene'], bytes([scene[0] ^ 0x20]))
                    try:
                        check_pixels(m, p, tag)
                    except AssertionError:
                        pass
                    else:
                        raise AssertionError('pixel oracle missed changed ink')
                    m.write(p.base + off['gr_scene'], scene[:1])
                    # Exercise all 256 packed ink pairs, both scales, without
                    # assuming the skyline happens to contain every palette entry.
                    # Flight's full-scene path permits pixels in the HUD rows.
                    m.write(p.base + off['gr_state'], b'\x01')
                    m.write(p.base + off['gr_scene'], bytes(range(256))*64)
                    call(m, code, 'gr_fullpaint')
                    check_pixels(m, p, tag)
                    if p.w('sx') == 2:
                        m.write(p.base + off['gr_sx'], struct.pack('<H', 1))
                        call(m, code, 'gr_fullpaint')
                        check_pixels(m, p, tag)
                        m.write(p.base + off['gr_sx'], struct.pack('<H', 2))
                        call(m, code, 'gr_fullpaint')
                    # A crater/banana-sized update must leave all other pixels.
                    patch = bytearray(p.data('scene', 16384))
                    for row in range(60, 73):
                        patch[row*128+8:row*128+16] = bytes([0x81])*8
                    before = I.video_bytes(m, tag)
                    m.write(p.base + off['gr_scene'], patch)
                    call(m, code, 'gr_blit', ax=16, bx=60, cx=16, dx=13)
                    check_pixels(m, p, tag, before)
                    if mode == 'window':
                        clipped(m, p, code, tag)
                    m.write(p.base + off['gr_scene'], scene)
                    m.write(p.base + off['gr_state'], b'\x00')
                    call(m, code, 'gr_fullpaint')
                    m.bp_exec()
                    m.run()
    report['wall_seconds'] = time.monotonic() - started
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
