#!/usr/bin/env python3
"""P1 jump/braking envelopes and guest checks, run by existing Stickio rows."""
from copy import deepcopy
import json
import re
import struct

from stickio_sim import Player, Simulation, FIELDS, BYTE_FIELDS


def flat_room():
    return [[0]*6+[1, 1] for _ in range(64)]


def launches():
    # Expected tiers are explicit; include both edges and both velocity signs.
    for speed, tier in ((0, 0), (255, 0), (256, 1), (639, 1), (640, 2), (768, 2)):
        for sign in ((1,) if speed == 0 else (-1, 1)):
            yield speed*sign, tier


def scenarios():
    flat = flat_room()
    for sign in (-1, 1):
        key = 2 if sign < 0 else 1
        for speed in (1, 47, 48, 95, 96, 97, 448, 768):
            for ground in (0, 1):
                yield 'brake-edge-%d-%d' % (sign*speed, ground), flat, dict(
                    x=400, vx=sign*speed, ground=ground,
                    y=(72 if ground else 0)*256, coyote=0), [key]
        yield 'brake-run-'+str(sign), flat, dict(x=400, vx=sign*768), [key | 8]*25
        yield 'brake-walk-'+str(sign), flat, dict(x=400, vx=sign*448), [key]*20
        for neutral in (0, 3):
            yield 'brake-neutral-%d-%d' % (sign, neutral), flat, dict(
                x=400, vx=sign*768), [neutral]*15
            yield 'brake-air-neutral-%d-%d' % (sign, neutral), flat, dict(
                x=400, vx=sign*768, y=0, ground=0, coyote=0), [neutral]*15
        yield 'brake-jump-'+str(sign), flat, dict(x=400, vx=sign*768), [key | 4]*36
        yield 'brake-landing-'+str(sign), flat, dict(x=400, vx=sign*768,
            y=71*256, vy=256, ground=0, coyote=0), [key]*15
        wall = deepcopy(flat)
        wall[26 if sign > 0 else 24][:6] = [2]*6
        yield 'brake-wall-'+str(sign), wall, dict(x=400, vx=sign*768), [key]*12
        pit = deepcopy(flat)
        for col in pit[23:28]:
            col[6:] = [0, 0]
        yield 'brake-edge-departure-'+str(sign), pit, dict(x=400, vx=sign*768), [key]*12
    for speed, tier in launches():
        key = 13 if speed < 0 else 14
        yield 'threshold-'+str(speed), flat, dict(x=400, vx=speed), [key]
    yield 'run-button-standing', flat, dict(x=400), [12]*40
    for speed in (0, 448, 768, -448, -768):
        key = 4 if speed == 0 else (5 if speed < 0 else 6) | (8 if abs(speed) == 768 else 0)
        for held in (True, False):
            yield 'envelope-%d-%s' % (speed, held), flat, dict(x=400, vx=speed), [
                key if held or i == 0 else key & ~4 for i in range(40)]
    yield 'air-reversal', flat, dict(x=400, vx=768), [14]+[5]*40
    yield 'buffered-run-tap', flat, dict(x=400, vx=768, jumpbuf=2), [10]*40
    yield 'buffered-landing-speed', flat, dict(x=400, vx=639, y=71*256+128,
        ground=0, coyote=0, jumpbuf=5), [10]*40
    yield 'coyote-run', flat, dict(x=400, vx=768, ground=0, coyote=2), [14]*40
    yield 'expired-coyote', flat, dict(x=400, vx=768, ground=0, coyote=1), [14]*8
    yield 'expired-buffer', flat, dict(x=400, vx=768, jumpbuf=1), [10]*8
    for tier in (0, 1, 2):
        for vy in (-1, 0, 1535):
            yield 'gravity-%d-%d' % (tier, vy), flat, dict(
                x=400, y=0, vy=vy, ground=0, coyote=0, jumpheld=1, jump_tier=tier), [4]
    ceiling = deepcopy(flat)
    for col in ceiling[20:50]:
        col[3] = 2
    yield 'fast-ceiling', ceiling, dict(x=400, vx=768), [14]*40
    wall = deepcopy(flat)
    wall[26][:6] = [2]*6
    yield 'fast-wall', wall, dict(x=400, vx=768), [14]*40
    ledge = deepcopy(flat)
    for col in ledge[20:50]:
        col[4] = 9
    yield 'fast-oneway', ledge, dict(x=400, vx=768), [14]*40
    spring = deepcopy(flat)
    spring[25][5] = 6
    for tier in (1, 2):
        yield 'spring-resets-%d' % tier, spring, dict(
            x=400, y=55*256, vy=256, ground=0, coyote=0, jump_tier=tier), [4]*40


def host(raw, sym):
    from stickio import ROOT, at
    expected = dict(ST_JUMP_WALK_SPEED=256, ST_JUMP_RUN_SPEED=640,
        ST_JUMP_WALK=-1728, ST_JUMP_RUN=-1792,
        ST_JUMP_ASCENT_SLOW=96, ST_JUMP_ASCENT_WALK=96, ST_JUMP_ASCENT_RUN=96,
        ST_JUMP_DESCENT_SLOW=96, ST_JUMP_DESCENT_WALK=112, ST_JUMP_DESCENT_RUN=128)
    constants = dict((name, int(value)) for name, value in re.findall(
        r'^%define (ST_\w+) (-?\d+)$', (ROOT/'apps/stickio/const.inc').read_text(), re.M))
    assert all(constants[k] == v for k, v in expected.items())
    assert all(constants[k] == v for k, v in dict(ST_ACCEL=48, ST_AIR_ACCEL=48,
        ST_BRAKE=96, ST_FRICTION=64, ST_SKID_RIGHT=30, ST_SKID_LEFT=31,
        ST_SPRITE_COUNT=32).items())
    braking_host()
    for name, values in (('launch', (-1664, -1728, -1792)),
                         ('ascent', (96, 96, 96)), ('descent', (96, 112, 128))):
        assert struct.unpack_from('<3h', raw, sym['st_jump_'+name]) == values
    for speed, tier in launches():
        model = Simulation(flat_room(), Player(x=400, vx=speed))
        model.step(13 if speed < 0 else 14)
        assert model.player.jump_tier == tier, (speed, tier, model.player)
        assert model.player.vy == (-1568, -1632, -1696)[tier]
    model = Simulation(flat_room(), Player(x=400))
    model.step(12)
    assert model.player.jump_tier == 0, 'run button selected a fast jump at rest'
    model = Simulation(flat_room(), Player(x=400, vx=639, y=71*256+128,
        ground=0, coyote=0, jumpbuf=5))
    model.step(10); model.step(10)
    assert model.player.ground and model.player.jumpbuf == 3
    model.step(10)
    assert model.player.jump_tier == 2 and model.player.vy == -1696, 'buffer sampled speed before landing'
    envelopes = []
    for speed, rise, steps, tap_rise, tap_steps in (
            (0, 53.125, 34, 13.25, 16),
            (448, 57.375, 34, 13.5, 16),
            (768, 61.875, 35, 13.75, 15)):
        for sign in ((1,) if speed == 0 else (-1, 1)):
            for held in (True, False):
                vx = speed*sign
                key = 4 if speed == 0 else (5 if sign < 0 else 6) | (8 if speed == 768 else 0)
                model = Simulation(flat_room(), Player(x=400, vx=vx))
                minimum = model.player.y
                for i in range(60):
                    model.step(key if held or i == 0 else key & ~4)
                    minimum = min(minimum, model.player.y)
                    if model.player.ground:
                        break
                else:
                    raise AssertionError('jump failed to land')
                actual_rise = (72*256-minimum)/256
                assert (actual_rise, i+1) == ((rise, steps) if held else (tap_rise, tap_steps))
                assert model.player.jump_tier == 0, 'landing did not reset profile'
                travel = model.player.x+model.player.frac/256-400
                assert travel == vx/256*(i+1), 'constant-speed jump travel'
                envelopes.append(dict(speed=vx, held=held, rise_pixels=actual_rise,
                                      steps=i+1, travel_pixels=travel))
    # Steering cannot reselect an airborne profile; cap falling after gravity.
    model = Simulation(flat_room(), Player(x=400, vx=768))
    model.step(14)
    for _ in range(20):
        model.step(5)
        assert model.player.jump_tier == 2 and not model.player.ground
    assert model.player.vx < 0
    for tier in (0, 1, 2):
        for vy, expected_vy in ((-1, 95), (0, (96, 112, 128)[tier]), (1535, 1536)):
            model = Simulation(flat_room(), Player(x=400, y=0, vy=vy,
                ground=0, coyote=0, jumpheld=1, jump_tier=tier))
            model.step(4)
            assert model.player.vy == expected_vy, (tier, vy, model.player.vy)
    proof = at('build/stickio-proof')
    proof.mkdir(exist_ok=True)
    (proof/'p1-jump-envelopes.json').write_text(json.dumps(dict(
        scope='flat-room integer model; no campaign or human playability claim',
        envelopes=envelopes), indent=2)+'\n')
    print('P1 jump host: signed thresholds, launch order, held/tap envelopes, latched air reversal and gravity cap')


def guest(p, tag):
    from stickio_p0 import snapshot
    traces = 0
    for name, mp, values, keys in scenarios():
        p.put('level', 0, 2); p.call('newcourse'); p.call('load')
        p.put('ne', 0, 2); p.put('jumpheld', 0); p.put('keys', 0)
        p.m.write(p.a('map'), bytes(v for col in mp for v in col))
        for field, value in values.items():
            p.put(field, value, 1 if field in BYTE_FIELDS else 2)
        model = Simulation(mp, Player(**snapshot(p)))
        for index, key in enumerate(keys):
            p.put('keys', key); p.call('step'); model.step(key)
            actual = snapshot(p)
            expected = {field: getattr(model.player, field) for field in FIELDS}
            assert actual == expected, (name, index, key,
                {f: (actual[f], expected[f]) for f in FIELDS if actual[f] != expected[f]})
            traces += 1
    # Stomps retain their original bounce even after a speed-tiered jump.
    for tier in (1, 2):
        p.call('newcourse'); p.call('load'); p.put('ne', 1, 2)
        p.m.write(p.a('enemies'), struct.pack('<HHBbHHHHH',64,72,1,1,0,0,0,72,72))
        p.put('x',64,2); p.put('y',55*256,2); p.put('vy',1024,2)
        p.put('ground',0); p.put('coyote',0); p.put('keys',0); p.put('jump_tier',tier)
        p.call('step')
        assert p.w('vy') == (-1152 & 65535) and p.b('jump_tier') == 0
        p.put('ne',0,2); p.put('keys',4); p.put('jumpheld',1); p.call('step')
        assert p.w('vy') == (-1056 & 65535), 'stomp inherited faster descent'
        p.put('jump_tier',tier); p.call('load')
        assert p.b('jump_tier') == 0, 'load retained jump profile'
        p.put('jump_tier',tier); p.put('lives',3); p.call('respawn')
        assert p.b('jump_tier') == 0, 'retry retained jump profile'
    p.call('newcourse'); p.call('load')
    braking_guest(p, tag)
    print('P1 jump/braking guest:',traces,'per-step host matches; thresholds, envelopes, assists, terrain, stomp/spring and retry',flush=True)


def braking_host():
    # Explicit outcomes verify stopping time, no overshoot and step ordering.
    for sign in (-1, 1):
        key = 2 if sign < 0 else 1
        for speed, velocities in ((448, (352, 256, 160, 64, 0, -48)),
                                  (768, (672, 576, 480, 384, 288, 192, 96, 0, -48))):
            model = Simulation(flat_room(), Player(x=400, vx=sign*speed))
            for expected in velocities:
                model.step(key | 8)
                assert model.player.vx == sign*expected, (sign, speed, expected, model.player)
                assert model.player.facing == (sign > 0)
                if expected == 0:
                    travel = model.player.x+model.player.frac/256-400
                    assert travel == sign*(10.5 if speed == 768 else 3.25)
        for speed in (1, 47, 48, 95, 96, 97, 448, 768):
            for ground in (0, 1):
                model = Simulation(flat_room(), Player(x=400, vx=sign*speed,
                    ground=ground, y=(72 if ground else 0)*256, coyote=0))
                model.step(key)
                expected = max(speed-96, 0) if ground else speed-48
                assert model.player.vx == sign*expected
        for ground in (0, 1):
            for neutral in (0, 3):
                model = Simulation(flat_room(), Player(x=400, vx=sign*768,
                    ground=ground, y=(72 if ground else 0)*256, coyote=0))
                model.step(neutral)
                assert model.player.vx == sign*704
        model = Simulation(flat_room(), Player(x=400, vx=sign*768))
        model.step(key | 4)
        assert model.player.vx == sign*720 and model.player.jump_tier == 2
        model = Simulation(flat_room(), Player(x=400, vx=sign*768,
            ground=0, y=71*256, vy=256, coyote=0))
        model.step(key)
        assert model.player.ground and model.player.vx == sign*720
        model.step(key)
        assert model.player.vx == sign*624
        pit = deepcopy(flat_room())
        for col in pit[23:28]:
            col[6:] = [0, 0]
        model = Simulation(pit, Player(x=400, vx=sign*768))
        model.step(key)
        assert not model.player.ground and model.player.vx == sign*672
        model.step(key)
        assert model.player.vx == sign*624
        model.respawn()
        assert model.player.vx == 0
    print('P1 braking host: signed zero clamp, walk/run stops, air steering, neutral input and jump/landing order')


def braking_guest(p, tag):
    from stickio import pixelcheck
    # Independent expected sprite indices check pose priority and cancellation.
    cases = ((768, 1, 1, 0, 0, 31), (-768, 2, 1, 0, 0, 30),
             (768, 1, 1, 5, 0, 31), (-768, 2, 1, 5, 0, 30),
             (0, 1, 1, 0, 0, 20), (768, 0, 1, 0, 0, 0),
             (768, 3, 1, 0, 0, 0), (768, 2, 1, 0, 0, 0),
             (768, 1, 0, 0, -100, 21), (-768, 2, 0, 0, 100, 10))
    for vx, key, ground, land, vy, expected in cases:
        p.call('newcourse'); p.call('load'); p.put('ne', 0, 2)
        p.put('invuln', 0); p.put('keys', key); p.put('ground', ground)
        p.put('land', land); p.put('vx', vx, 2); p.put('vy', vy, 2)
        p.put('facing', 1 if key == 1 else 0)
        p.call('render'); pixelcheck(p, tag)
        assert p.w('sprite') == p.sym['st_sprites']+expected*96, ('skid pose', vx, key, expected)
    for sign in (-1, 1):
        p.call('newcourse'); p.call('load'); p.put('ne', 0, 2)
        p.m.write(p.a('map'), bytes(v for col in flat_room() for v in col))
        p.put('invuln', 0); p.put('x', 120, 2); p.put('vx', sign*768, 2)
        p.put('keys', 2 if sign < 0 else 1)
        for index in range(9):
            p.call('step'); p.call('render'); pixelcheck(p, tag)
            if index < 7:
                expected = 30 if sign < 0 else 31
                assert p.w('sprite') == p.sym['st_sprites']+expected*96
            else:
                assert p.w('sprite') < p.sym['st_sprites']+24*96, 'stale skid mask after stop'
        p.put('lives', 3); p.call('respawn')
        assert p.w('vx') == 0, 'retry retained braking momentum'
    p.call('newcourse'); p.call('load')
    print('P1 skid guest: pose priority, cancellation, mirrored stops and incremental framebuffer checks', flush=True)
