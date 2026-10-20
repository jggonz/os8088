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
    # The solo opponent never aims at the human (SPEC.md 99): a skyline's
    # first throw is a wild guess, and each landing teaches it one bit -
    # short of the rival or past it - which brackets the next release.
    W = lambda name, value: m.write(p.base + p.offsets['gr_' + name],
                                    struct.pack('<H', value))
    def forget(angle=0, lo=0, hi=0, last=0):
        for name, value in (('aiangle', angle), ('ailo', lo), ('aihi', hi),
                            ('ailast', last)):
            W(name, value)
    def throws(seeds=range(32), **state):
        out = []
        for seed in seeds:
            forget(**state)
            W('seed', seed)
            call(m, code, 'gr_aichoose')
            out.append((p.w('angle'), p.w('power')))
        return out
    m.write(p.base + p.offsets['gr_players'], b'\x01')
    m.write(p.base + p.offsets['gr_turn'], b'\x01')
    first = throws()
    assert all(45 <= a <= 70 and 40 <= v <= 100 for a, v in first), first
    assert len({v for _, v in first}) >= 8, ('first guess lacks variety', first)
    assert len({a for a, _ in first}) >= 6, ('first arc lacks variety', first)
    # Where the human stands changes nothing about the release.
    gx = p.w('gx')
    W('gx', (gx + 37) % 200)
    assert throws() == first, 'computer aim reads the human position'
    W('gx', gx)
    # Each bracket state releases inside its own band, both ways.
    for state, low, high in ((dict(angle=55, lo=70), 92, 118),
                             (dict(angle=55, hi=90), 52, 68),
                             (dict(angle=55, lo=60, hi=100), 70, 90),
                             (dict(angle=55, lo=79, hi=81), 78, 82),
                             (dict(angle=55, lo=300), 360, 360)):
        powers = {v for a, v in throws(**state)}
        assert all(a == 55 for a, _ in throws(range(4), **state))
        assert low <= min(powers) and max(powers) <= high, (state, powers)
        if high - low > 4:
            assert min(powers) < (low+high)//2 < max(powers), (state, powers)
    # Learning: a short landing raises the floor, a long one lowers the
    # ceiling, and a verdict that contradicts the bracket replaces it.
    rival = p.w('gx') + 8
    for state, x, lo, hi in ((dict(last=70), rival + 30, 70, 0),
                             (dict(last=70), rival - 30, 0, 70),
                             (dict(last=70), -3, 0, 70),          # off the left
                             (dict(last=70), 260, 70, 0),         # blown back
                             (dict(lo=60, hi=100, last=80), rival + 1, 80, 100),
                             (dict(lo=60, hi=100, last=80), rival - 1, 60, 80),
                             (dict(lo=60, hi=100, last=50), rival + 1, 60, 100),
                             (dict(lo=60, hi=100, last=110), rival + 1, 110, 0),
                             (dict(lo=60, hi=100, last=40), rival - 1, 0, 40)):
        forget(angle=55, **state)
        call(m, code, 'gr_ailearn', cx=x & 0xffff)
        assert (p.w('ailo'), p.w('aihi')) == (lo, hi), (state, x)
    # Cratering its own rooftop steepens the arc and starts over; at the
    # steepest arc the same landing is only a short throw.
    own = struct.unpack('<HH', p.data('gx', 4))[1]
    forget(angle=55, lo=60, hi=100, last=80)
    call(m, code, 'gr_aiterrain', cx=own)
    assert (p.w('aiangle'), p.w('ailo'), p.w('aihi')) == (65, 0, 0)
    forget(angle=80, lo=60, hi=100, last=80)
    call(m, code, 'gr_aiterrain', cx=own)
    assert (p.w('aiangle'), p.w('ailo'), p.w('aihi')) == (80, 80, 100)
    forget(angle=55, last=80)
    call(m, code, 'gr_aiterrain', cx=(rival + own) // 2)
    assert (p.w('aiangle'), p.w('ailo')) == (55, 80), 'mid-city crater is short'
    # Nothing is learned from the human's own throws.
    m.write(p.base + p.offsets['gr_turn'], b'\x00')
    forget(angle=55, last=80)
    call(m, code, 'gr_ailearn', cx=rival + 30)
    assert (p.w('ailo'), p.w('aihi')) == (0, 0), 'learned from the human'
    # A new skyline forgets it all.
    forget(angle=65, lo=60, hi=100, last=80)
    call(m, code, 'gr_city')
    assert all(p.w(n) == 0 for n in ('aiangle', 'ailo', 'aihi', 'ailast'))
    m.write(p.base + p.offsets['gr_players'], b'\x02')
    m.write(p.base + p.offsets['gr_turn'], b'\x00')


SURFACES = ((4, 0, 0, 1, 1), (4, 0, 0, 1, 3), (4, 0, 0, 2, 1), (4, 0, 0, 2, 2),
            (4, 0, 0, 2, 3), (4, 1, 0, 2, 2), (4, 1, 0, 2, 3), (1, 0, 0, 1, 1),
            (1, 0, 0, 2, 1), (1, 0, 0, 2, 2), (2, 0, 1, 1, 1))


def check_surfaces(m, p, code):
    """Every surface's derived tables against the generator (SPEC.md 99.3, 99.4).

    The ink-pair tables, marquee masks and intro sprites are built by the
    guest when the surface changes; a surface no arm of this row can reach
    (sx 1 planar, sy 3, a foreign VGA at sx 1) is reached by setting the
    variables gr_inktables and gr_frontprepare key on. The clipped desktop
    VGA intro packs its pose on demand (gr_widepose): checked here too."""
    names = ('depth', 'vga', 'cga', 'sx', 'sy', 'tablekey', 'cachekey')
    saved = {n: p.data(n, 2) for n in names}
    for depth, vga, cga, sx, sy in SURFACES + ((4, 1, 0, 1, 1),):
        for name, value in (('depth', depth), ('vga', vga), ('cga', cga)):
            m.write(p.base + p.offsets['gr_' + name], bytes([value]))
        for name, value in (('sx', sx), ('sy', sy)):
            m.write(p.base + p.offsets['gr_' + name], struct.pack('<H', value))
        call(m, code, 'gr_inktables')
        G.check_inkpairs(p)
        if vga and sx == 1:
            continue                # no intro: a foreign VGA is always sx 2
        call(m, code, 'gr_frontprepare')
        G.check_caches(p)
        if depth == 4 and not vga:
            wide = G.gorillas_art.front_sprite('win', sx)
            for pose in (0, 1):
                call(m, code, 'gr_widepose', bx=pose)
                got = p.data('band', 5376)[5376-320:5376-320+len(wide)//2]
                assert got == wide[pose*len(wide)//2:(pose+1)*len(wide)//2], \
                    ('clipped intro pose differs', depth, sx, pose)
    for name, value in saved.items():
        m.write(p.base + p.offsets['gr_' + name], value)
    # The next draw must rebuild for the real surface.
    m.write(p.base + p.offsets['gr_tablekey'], b'\0')
    m.write(p.base + p.offsets['gr_cachekey'], b'\0\0')
    call(m, code, 'gr_inktables')


def check_guide(m, p, code, tag):
    """The aim guide (SPEC.md 99): a ray along the exit angle that thins out
    and stops short, every ink restored, the glass matching the scene."""
    W = lambda name, value: m.write(p.base + p.offsets['gr_' + name],
                                    struct.pack('<H', value))
    B = lambda name, value: m.write(p.base + p.offsets['gr_' + name], bytes([value]))
    clean = p.data('scene', 16384)
    def ink(scene, x, y):
        return (scene[y*128+x//2] >> (0 if x & 1 else 4)) & 15
    def dots():
        n, raw = p.b('guidedots'), p.data('guidesave', 32)
        return [struct.unpack_from('<HBB', raw, 4*i) for i in range(n)]
    gx = struct.unpack('<HH', p.data('gx', 4))
    gy = struct.unpack('<HH', p.data('gy', 4))
    B('state', 0)
    B('guide', 0); B('turn', 0); W('angle', 45)
    call(m, code, 'gr_guideshow')
    assert p.b('guidedots') == 0 and p.data('scene', 16384) == clean, 'guide drawn while off'
    B('guide', 1)
    for turn, angle in ((0, 0), (0, 45), (0, 70), (1, 30), (1, 60)):
        B('turn', turn); W('angle', angle)
        call(m, code, 'gr_guideshow')
        assert p.w('guideat') == angle + 512*turn
        scene, seen = p.data('scene', 16384), dots()
        # The first three dots are the angle, and room for them is promised
        # even on the tallest roof; the far ones may leave into the HUD rows.
        assert len(seen) >= 3, (turn, angle, seen)
        x0 = gx[turn] + (-2 if turn else 17)
        y0 = gy[turn] + 4
        side = -1 if turn else 1
        reach = [((x-x0)**2 + (y-y0)**2) ** .5 for x, y, _ in seen]
        assert all(a < b for a, b in zip(reach, reach[1:])), ('not outward', seen)
        assert max(reach) <= 36, ('guide runs on toward the landing', seen)
        gaps = [b - a for a, b in zip(reach, reach[1:])]
        if len(seen) >= 6:
            assert gaps[-1] > 2*gaps[0], ('guide does not thin out', gaps)
        if angle == 0:
            assert len(seen) == 8 and all(abs(y - y0) <= 3 for _, y, _ in seen)
            assert {ink(scene, x, y) for x, y, _ in seen[4:]} == {7}
            continue
        for x, y, _ in seen[:3]:              # the first dots ARE the angle
            assert side*(x - x0) > 0 and y < y0
            # slope in the scene's aspect: dy/dx = tan(angle) * 15/16
            import math
            want = math.atan2(math.sin(math.radians(angle))*15/16,
                              math.cos(math.radians(angle)))
            got = math.atan2(y0 - y, side*(x - x0))
            assert abs(got - want) < .3, (angle, x, y, got, want)
        assert {ink(scene, x, y) for x, y, _ in seen[:3]} == {15}
        assert {ink(scene, x, y) for x, y, _ in seen[4:]} <= {7}, 'far dots do not fade'
        drawn = {(x, y) for x, y, _ in seen}
        for y in range(128):
            for x in range(256):
                if (x, y) not in drawn:
                    assert ink(scene, x, y) == ink(clean, x, y), ('stale dot', x, y)
        for x, y, under in seen:
            if sum(1 for d in seen if d[:2] == (x, y)) == 1:
                assert under == ink(clean, x, y)
        check_pixels(m, p, tag)
    # Same angle again: nothing redrawn. Then a throw takes it all out.
    call(m, code, 'gr_guideshow')
    assert dots() == seen
    call(m, code, 'gr_guidehide')
    assert p.b('guidedots') == 0 and p.w('guideat') == 0xffff
    assert p.data('scene', 16384) == clean, 'guide left ink behind'
    check_pixels(m, p, tag)
    B('state', 1)
    call(m, code, 'gr_guideshow')
    assert p.b('guidedots') == 0, 'guide drawn in flight'
    B('state', 0); B('guide', 0); B('turn', 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--arm', choices=G.ARMS)
    ap.add_argument('--output', type=Path)
    ap.add_argument('--max-paint-ms', type=float, default=1800)
    args = ap.parse_args()
    off, code = G.offsets(), I.code_offsets(('gr_city', 'gr_blit', 'gr_aitick',
                                          'gr_aichoose', 'gr_ailearn',
                                          'gr_guideshow', 'gr_guidehide',
                                          'gr_aiterrain', 'gr_inktables',
                                          'gr_frontprepare', 'gr_widepose'))
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
                    if mode == 'window':
                        check_surfaces(m, p, code)
                    G.check_inkpairs(p)
                    m.write(p.base + off['gr_seed'], struct.pack('<H', 12345))
                    build = call(m, code, 'gr_city')
                    scene = p.data('scene', 16384)
                    check_city(p)
                    if seeded_scene is None:
                        seeded_scene = scene
                    assert scene == seeded_scene, 'seeded city differs across modes/adapters'
                    paint = call(m, code, 'gr_fullpaint')
                    check_pixels(m, p, tag)
                    check_guide(m, p, code, tag)
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
